# 删掉过渡类型之后：四处开口，与 ONNX 一族的两条新读数（h550 lane，2026-10-25）

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的提交号若在 develop 上查不到，属于那条线。


**问题一句话**：删除步把"元素类型待定"的类型删掉之后，四处原本由它开口的地方现在没有答案——①`stdlib/optim.cml:311`
那个"元素类型谁也不写"的叶子（`if` 两支的元素类型 f32/f64 现在没有 join）、②`show([])` 没有候选、
③`to_bytes` 等张量算子接受的元素类型集合（`bool` 算不算）、④两个元素类型不同的数组 join 成什么
（`unify(...)` 今天返回 `nullptr`）。

**卡住**：删除步自己的收尾——`stdlib/optim.cml` 一族把快层 **27** 条条目连同所有 `import optim` 的计划一起带红、
`native.tensor_show_empty_array` **1** 条、`to_bytes` 的 **5** 条加 `f32_program_bytes_unchanged` 与
`poison:…binary_roundtrip`、`src/core/type/array_type_selftest.cpp` 里钉 `unify(...) == nullptr` 的那条；
以及 ONNX 一族改成写明元素类型之后新暴露的 4 个 case（第五节）与递归拒绝位置（第六节）。

> 本文只报选题与逐字读数，不定语义。作者页面（`docs/handover/README.cn.md`、`reviews/**`、
> `docs/design-principles*`、`docs/design/**`、`docs/language/**`）未改，除
> `docs/design/arrays-memory/plan.cn.md` 第 5.1a 节依赖表里一处已删符号的引用（文档链接检查器要求）。

本 lane 按第 5.1a 节做删除步：`ArrayType::numericUnknown*` 与 `numericUnknown_` 删除、`Tensor` 的
`Reference` 变成"元素类型必填的名字"、签名里的裸 `Tensor` 报 `SemanticDiag::TensorElementTypeRequired`
（语义段新号 `0x0001ED`）。下面的四处读数都在这个状态下取的。

## 一、`stdlib/optim.cml:311` 的"元素类型谁也不写"的叶子

`adam_value_leaf` 用一个运行期条件在两种写法之间选：`size(to_bytes(p)) == numel(p) * 8` 时走 f64 的
`adam_wide`，否则走 f32 的 `narrow_tensors(adam_wide(...))`；两条分支的结果交给 `unstated` 这个恒等函数，
它的返回类型原先写裸 `Tensor`：

```cml
func unstated::<A, B, C>(t: (A, B, C)): (Tensor, Tensor, Tensor) {
    return (t.0, t.1, t.2)
}
```

现在的读数（`camel stdlib/optim.cml`）：`The type 'Tensor' states no element type` 落在 `optim.cml:311`
的返回类型上；所有 `import optim` 的计划随之红（快层 27 条，路径落在 `out/latest/stdlib/`）。

问题的形状：这个函数存在，是因为**同一个叶子在一次遍历里只有一种类型**，而两条分支的元素类型不同
（f32 / f64），值自己知道该是哪一种。类型系统里已经没有"元素类型待定"的类型，于是 `if` 两支的类型
join（`ArrayType::unify`）现在没有答案——删除步之后它返回"没有共同类型"，而不是原来的标记数组。

选项（都要作者裁定）：

1. **A**：叶子改成总是 f64——f32 那一支也算 double、由存入时一次舍入（`modules/tensor/kernels/update.h` 的
   融合内核就是这么做的），`narrow_tensors` 不再参与类型。代价：f32 那一支多一次舍入，且这个函数不再
   能表达"值自己知道是哪一种"。
2. **B**：保留两种写法，但在 `if` 的一支上显式写转换，让两支同型。代价：多一次转换写在叶子体内，且
   `unstated` 这个恒等函数的返回类型仍要写明一种元素类型。
3. **C**：语言给出"两个元素类型不同的数组 join 成什么"的裁定（见第四节），本处随之消失。

## 二、`show` 的伞形形参：元素类型谁也不写的数组没有候选

`stdlib/tensor.cml` 里那条 `show(t: Tensor)` 是伞形声明（任何数组、任何秩、任何元素类型）。
删除步把它的单个 arm 写成 `show::<A>(t: A[..])`：秩 2、秩 1 的数组都仍然有候选（实测
`show(new([[1.0, 2.0], [3.0, 4.0]]))` 与 `show(new([1.0, 2.0]))` 都编译并打印），但**元素类型谁也不写的
数组没有候选**：

```
$ camel show_probe2.cml            # show([])
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (void[]).
help: ... Available overloads:
          <tensor:show>: (t: A[..]) => void uses stdout
```

`A[..]` 绑不出元素类型（空数组字面量 `[]` 的类型是 `Void[]`，绑 `A = Void` 会走到"元素类型待定的数组"
本身，它已经不是值类型），所以这不是写法问题。快层因此红了 1 条实测用例
（`native.tensor_show_empty_array`，它原先钉的是"编译通过、运行期由内核报错"）。

选项：

1. **A**：`[]` 该有元素类型（由上下文给出）。代价：字面量的位置类型要能进入空数组字面量，这条路径今天
   不存在，`void[]` 这个类型要重新只说"没有元素类型"。
2. **B**：`show([])` 应当就是编译错误，用例改写成负例。代价：`native.tensor_show_empty_array` 的期望
   要改，`[]` 通过 `show` 打印这条路消失。

## 三、`to_bytes` 等算子的 `when` 子句：bool 元素类型算不算

删掉的那 7 个 arm 里，`to_bytes(t: Tensor)` 接受**任何**元素类型；旁边的泛型 arm 写的是
`when is_numeric::<T>()`。于是 `bool` 数组丢了算子（快层 4 条 + `f32_program_bytes_unchanged` +
`poison:…binary_roundtrip`）：

```
No candidate of 'to_bytes' applies to (<> (bool[3])): every candidate's 'when' condition is false
```

值层面的字节编码是支持 bool 的（`test/cases/modules/tensor/binary_bool_error.cml`、`binary_bytes.cml`
钉的就是它）。

选项（要作者定"每个张量算子接受的元素类型集合"）：

1. **A**：给 `to_bytes` 加一个含 bool 的谓词。代价：谓词多一个，"算子的签名只有一个来源"那条规则要
   多一处声明。
2. **B**：去掉 `when`（回到伞形 arm 的宽度，由内核在运行期拒）。代价：删掉的宽度又回来，`when` 想挡的
   情况要重新在运行期处理。
3. **C**：保持现状，改这 5 条用例。代价：值层面支持 bool 的事实与签名不符，字节往返里 bool 数组这条
   路径不再被编译期接受。

## 四、两个元素类型不同的数组 join 成什么

删除步之前，`unify(int[3], f32[3])` 的答案是"元素类型待定的数组"（标记数组）。现在它没有答案：
`src/core/type/array_type_selftest.cpp` 钉的是 `unify(...) == nullptr`，快层里没有因此变红的用例
（第一节的 `optim` 是唯一已知的消费者）。

选项：

1. **A**：这个 join 的拒绝是终态想要的，程序需要 join（比如 `if c then xs else ys`，两支元素类型不同）
   时由写法显式转换。代价：第一节的 `unstated` 必须按选项 A 或 B 改写，且这种 `if` 一律要写转换。
2. **B**：语言给一个答案（某个元素类型胜出，或一个共同的元素类型）。代价：`unify` 的规则要写进设计原则
   与语言参考，`array_type_selftest.cpp` 那条钉子跟着改。

## 五、ONNX 一族：4 个 case 的实测，与第一节同一类（h562 lane，2026-10-25）

ONNX 一族的用例按删除步改成写明元素类型之后，实测的结论是：**任何合法的签名拼写都保不住旧行为，
只有把转换写进 body 才行**——这正是第一节那条缝（一个 f64 的值/字面量遇到 f32 数组）在 ONNX 用例上的
四处实例，逐字读数：

| case | 位置 | 逐字读数 |
|---|---|---|
| `test/cases/modules/onnx/export_var_compound.cml` | `23:16` | `Invalid call to '__mul__' with argument types <> (f32[..], float)`，诊断自己教你写 `f32(value)`（`:23` 逐字 `return x * s + float(k)`，`s` 是 f64 的 `var`） |
| `export_linear_recursion.cml` | `fold` 的右折叠，`:20` | 结果要乘 `float(i + 1)`：`i` 是 `int`、折叠结果是 f32，转换不写出来就不成形 |
| `export_slice_loop.cml` | `18:16` | `scan` 的第 4 个实参 `zeros(...)` 是 f32、形参 `h: float[..]` 是 f64：`No matching function found … (int, int, float[..], f32[?, ?])` |
| `export_train_step_bn.cml` | `30:5` | 返回记录里 `{bn1_rm: f32[..]}`（`r1.1` 是 f32）与 `type BnStats` 的声明 `{bn1_rm: float[..]}` 不符 |

这四条与第一节是同一条缝，所以并入本页，不另开页。

## 六、一条行为移动：递归的拒绝位置（h562 lane，2026-10-25）

导出对递归的拒绝位置从 `export_nontail_recursion.cml:12:47` 移到 `17:47`（消息、退出码、
`file written: False` 三者不变）；h562 实测**4 种合法闭包拼写都如此**——移的不是某一种写法，而是所有
写法。计划自己也没有钉住这件事：`test/plans/feat/modules/onnx.plan.toml:1670` 的断言是
`/at [^ ]*export_nontail_recursion[.]cml:1[0-9]:[0-9]+[)]/`，`1[0-9]` 同时接受 12 与 17。

要问的一条：**这是删除步的正确后果，还是一个缺陷？**（拒绝从函数体内的递归调用点移到 `main` 里
`export_model` 的闭包处；把位置钉回调用点，还是承认"拒绝在入口处报"是终态。）