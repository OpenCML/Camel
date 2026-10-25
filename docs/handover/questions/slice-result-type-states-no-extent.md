# 切片的类型：核心的结果类型不写任何 extent

车道：`handover/h585-slice-all-calls`（族 = 核心接管全部 `slice(...)` 调用）。

## 一、是什么

核心切片的结果类型**保留输入的秩，但把每个 extent 都写成未知**（`f32[2,3]` 切成 `f32[?,?]`）。模块的 `sliceFunctionInfer` 原先会点明**未被切的轴**（有时还有切出来的长度），于是 `slice(x, 0, i, i + 1)` 在 `x: f32[?, 3]` 上是 `f32[?, 3]`；核心接管后是 `f32[?, ?]`。

原因是接口层面的：函数形式的参数表由 `DynamicFuncTypeResolver` 的**定签名**给出，而这个解析器的结果回调**拿不到 `static_args_t`**（它只收到 `with`、`norm`、`modifiers`），所以轴（以及轴上的 extent、常量的界）对它不可见。

**这不是可选优化，它挡着裁定第 4 条（删掉模块的切片）。** 删掉模块切片之后实测红 32 条，其中一簇正是"精确 extent 是硬需求"：程序把切片结果用在一个写死形状的声明处，今天靠模块的推断才通得过。

## 二、逐字证据

1. 核心（`src/builtin/modules/globals.cpp`，`sliceResultType`）：

```
if (array->anyRank()) {
    return ArrayType::openRank(elem);
}
const std::vector<ExtentForm> extents(array->rank(), ExtentForm::unknown());
return ArrayType::create(elem, extents);
```

2. 回调签名（`src/builtin/modules/globals.cpp`，`coreSliceFunctionResolver`）——第三个参数是 `ModifierSet`，没有 statics：

```
[](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &) -> optional<Type *>
```

3. 被删掉的模块推断（`modules/tensor/ops/layout_ops.cpp`，逐字）：

```
const int64_t s = range->start, e = range->end;
(*in.shape)[a]  = e > s ? (e - s + *step - 1) / *step : 0;
return tensorOf(in);
```

它把非轴的 extent 原样留下，把轴的 extent 换成切出来的长度。

4. 实测（删除态，`-Plan test/plans/feat/modules,test/plans/feat/exemplars,test/plans/feat/opt -Tier fast`，共 32 红；下面是其中一簇）：

`si_computed_slice` 计划里逐字钉着**模块类型**的那句诊断——它没有出现，出现的是更不精确的类型：

```
[INFO] output does not contain 'argument 1 ('slice(x, 0, i, i + 1)', for parameter 'a') has type 'f32[?, 3]', which is less exact than 'f32[1, 3]''
[stdout] (shape_inference_computed_slice)/.../computed_slice.cml:11:14: [Error]: No matching function found for argument types: <> (float[2, 3], int).
[stdout]     f: (x: f32[?, 3], i: int) => float
```

`computed_slice.cml:7-8` 逐字：

```
func f(x: Tensor<f32, [?, 3]>, i: int): float {
    return row(slice(x, 0, i, i + 1))
```

`train_step.loss.nvm` / `.fvm` / `.jit`（`examples/models/naive-in-expert-out`）也红，其子进程的逐字报错是 `camel exited 1`（运行器只给到这一层）；同一簇里还有 `declared_numeric_semantics`（`operator tensor:slice is not listed`）、`instances`、`declaration_evaluator`。

## 三、今天的读数

- 运行期形状是精确的：`shape(m[0:1])` 在 `f32[2,3]` 上读作 `[1, 3]`（`slice_rank2.nvm`，全绿）——缺的是**静态** extent，不是运行期形状。
- 静态 extent 也还在被用：`test/cases/modules/tensor/shape_inference/computed_slice.cml` 把切片结果交给 `row(a: Tensor<f32,[1,3]>)`，今天靠"未知 extent 可赋给已知 extent"通过；`si_computed_slice` 钉的则是"类型比 `f32[1,3]` 欠精确"那句诊断，两者只能有一个成立。
- 本车道未跑（也不该由本车道跑）的是"给解析器补 statics"这一改动：它会动 `DynamicFuncTypeResolver` 的契约，属于新机制。

## 四、选项与代价

**选项 A：给结果类型一条能看见 statics 的通路。** 例如让定签名的回调拿到 `static_args_t`（或让规则用 `resolveWith(with, norm, statics, modifiers)` 一类的包装重建类型：用 `FunctionTypeFactory` + `argNameAt` 保住参数名，逐轴把非轴的 extent 原样搬过来、把轴的 extent 换成算出来的长度）。代价：改的是所有动态规则的解析契约（今天没有第二个用例依赖它），要有自己的用例（多轴切片、常量界与计算界两种）；`docs/stdlib-reference.md`、`docs/reference/camel-for-ai.md` 的字符数要跟着重出。

**选项 B：维持"未知 extent"，把依赖精确形状的程序改成不依赖。** 代价：实测 6 条红（`train_step.loss.*` 三条、`si_computed_slice`、`instances`、`declaration_evaluator`），且语言层**失去今天已有的一种定型**——切片结果不再能喂给写死形状的声明；`computed_slice` 这类"欠精确"诊断的语义也要重写。

**选项 C：类型仍由模块的推断给，节点由核心答。** 这就是今天的过渡切分（一条规则管类型、另一条管节点），裁定第 3 条明确不许它进 `develop`；它也不会缩小，只会把"哪些调用归谁"变成两处口径。