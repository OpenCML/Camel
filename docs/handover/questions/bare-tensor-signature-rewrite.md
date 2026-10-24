# 签名里的裸 `Tensor` 形参：本 lane 只做到了「加一条显式泛型臂」

裁决（`docs/handover/reviews/questions-2026-10-23.md`，
§`bare-tensor-parameter-instantiation-measured` 第 576 行、§`bare-tensor-parameter-instantiation` 第 373 行）
的终点是**签名里不许写裸 `Tensor`**：把它改写成头部声明的 `::<T>` 加 `T[..]`，那是显式泛型，按
design principles 5.5 实例化。本 lane 落地了这个拼写，但**删掉裸 `Tensor` 那一臂做不到**，下面是实测。

## 一、已经落地的

`stdlib/tensor.cml` 的六个裸 `Tensor` 形参都多了一条显式泛型臂，两条臂并存：

```camel
export func shape::<T>(t: T[..]): int[] when is_numeric::<T>()
export func shape(t: Tensor): int[]
```

`numel` / `dim`（`int` 与 `i32` 两条）/ `to_bytes` / `save_npy` 同形。配套的两处让它在端到端也成立：

* `when is_numeric::<T>()` 要从 `types` 导入（`import { is_numeric } from types`），否则
  `SemanticDiag::WhenPredicateNotImported`。
* 执行器原来只认模块的张量：`modules/tensor/ops/{layout,gradient,utility}_ops.cpp` 里这六个算子的
  参数种类是 `ParamKind::Tensor`（只接受 `TensorType`），取参数走 `tensorArg(norm, 0)`。改成
  `ParamKind::TensorLike` 加 `tensorArg(norm, 0, true)` 之后，原生数组才真能进来
  （`tensorFromArray` 那一步本来就在 `tensorArg` 里）。少了这一步，`shape(f32[3])` 能编过，
  运行期报 `RuntimeDiag::RuntimeError: Expected Tensor argument.`——编译期与运行期会各说一套。

## 二、为什么删不掉裸 `Tensor` 那一臂（实测，不是推断）

在没有 P4 别名（`modules/tensor/module.cpp:84` 仍 `exportType(Reference("Tensor"), TensorType::Default())`）
的今天，两种拼写接受的**参数集合不相交**：

* `T[..]` 只接受原生数组。`shape(ones([2, 3]))`、`shape(zeros([4]))`、以及 `let t: Tensor` 的 `shape(t)`
  全部 `SemanticDiag::GenericArgumentsMismatch`：「argument 1 has type 'Tensor<float32, [2, 3]>',
  but its parameter 't: T[..]' needs a type of that form to bind its type variables.」
* 裸 `Tensor` 只接受张量模块产出的值：`f32[]` 进来是
  `SemanticDiag::NoMatchingFunction`（候选表里只有 `<tensor:shape>: (t: Tensor) => int[]`）。

做过破坏性实验（把裸臂删掉重建）：`test/plans/feat/modules/tensor.plan.toml` 从 68 全绿变成
`total: 66, pass: 39, fail: 27`（`RUN_EXIT=1`）；调用点统计里 `shape` 133 处、`numel` 35、`dim` 24、
`to_bytes` 139、`save_npy` 166、`show` 230，其中相当一部分传的是张量模块的返回值。

也就是说：**「签名里不许写裸 `Tensor`」与「整层绿」在别名落地之前不能同时成立**。终点那一步属于 P4，
前置条件是 `Tensor` 就是数值数组别名（`ArrayType::numericUnknown({})`）——那是
`docs/handover/questions/tensor-alias-transition.md` 那条车道的事，本 lane 不替它做。

## 三、`show` 不动，理由是已有的测试

`show` 的两条臂是 `show(t: Tensor)` 与 `show::<A>(t: A[])`，第二条是 C++ 解析器那个伞形参数（张量**或任意**数组）。
`test/plans/feat/compile/native_declarations.plan.toml:966` 附近的用例把「它不窄化任何东西」钉住了：
`show([])` 绑到空数组的元素类型，运行期才以 `show: Unsupported Tensor element type.` 报错。
再加一条 `show::<T>(t: T[..]) when is_numeric::<T>()` 会让**每个一秩数值数组**与那条伞形臂同时命中，
变成歧义调用。所以 `show` 的三条臂方案不成立，两条臂的方案要等 P4 把伞形参数收掉。

`load_npy(path: string): Tensor` 的裸 `Tensor` 在**结果**位置，裁决说的是形参；P4 的方案
（`docs/design/arrays-memory/plan.cn.md` §5.5）也把 `from_bytes` 一类的结果留成「任意数值数组」。这里不动。

## 四、「签名里再写裸 `Tensor`」点名诊断加不出来

要一条新诊断，得同时满足三件事，本 lane 一件都不满足：

1. **作用域**。裸 `Tensor` 出现在签名里是**合法且必须保留**的（上面第二节：460 行现存签名、以及本 lane 保留的
   六条臂）。所以它只能是「警告」，且必须只在某个新作用域里报。
2. **`--coverage-check`**。诊断注册表要求每个码至少被一个测试触发；一个「本 lane 之后再没人写」的警告，
   触发它的测试会在删掉裸臂时一起消失。
3. **自伤**。若作用域定在「无体声明」，它立刻会在 `stdlib/tensor.cml` 自己和 `stdlib/signatures/**` 上响，
   `feat.modules.stdlib_registry` 与 `docs` 计划直接红。

可用的替代是本 lane 已经钉住的那条：元素类型约束不满足时报
`SemanticDiag::WhenConditionUnmet`（**复用，无新码**，注册表不动），它点名候选与条件：

```
No candidate of 'shape' applies to (<> (string[])): every candidate's 'when' condition is false.
Candidates: (t: T[..]) => int[] — when is_numeric::<T>()
```

## 五、`stdlib/signatures/**` 没改，两条理由

`stdlib/signatures/tensor.cml`（约 55 处）与 `nn.cml`（约 17 处）里的裸 `Tensor` 是**参考材料**，
不是编译的声明（`stdlib-reference.mjs` 只按名字、参数个数、参数名与 pure/effect 比对注册的算子，不比类型）。
不改的两条理由：

1. **归约车道已经拥有这个文件里 `sum`/`mean` 那些归约臂的改写权**
   （§`H401 三份` 第 520 行附近：改写由归约车道承担）。两个车道同时改一个文件会冲突。
2. 那里的臂大量是并集（`Tensor | number`、`Tensor | number[]`），照「一种接受的类型一条声明」拆开是
   design principles 4.1 的另一件事，且会让参考页出现悬空的 `T`（`signatureText` 不渲染头部的 `<T>`）。
   要做就得连 `scripts/stdlib-reference.mjs` 的渲染一起改。

## 六、留给作者/协调者的两条路

1. **等 P4 别名**（推荐）：别名落地后把六条裸臂删掉，`T[..]` 一条就够，签名里真的不再有裸 `Tensor`；
   `show` 的伞形臂同时收掉。本 lane 的 `::<T>`+`T[..]`+`when` 与执行器的 `TensorLike` 是那一步的现成前置。
2. **放弃「签名里不许写裸 `Tensor`」的强制**：承认它在过渡期只是一条书写约定，没有诊断兜底（第四节）。