# 在**后面的维取一个位置**（`m[:, 0]`）降不出来，怎么办？

**问谁**：语言作者（负责人）
**问什么**：您刚裁过"`t[:, a:b]` 这类**切第一维以外**的下标，降低成以对应轴调用核心切片"——我们照做了（轴由 `intConstant(0)` 改为 `intConstant(sliceAxis)`，任意秩）。但同一个形态里还有一种**取位置**（不是取范围）：`m[:, 0]`，它今天**降不出来**，请您定怎么办。

## 量到的事实

- **核心节点表**里索引只有第一维：`:op/idx_arr`、`:op/idx_take`、`take`；**没有 reshape/squeeze，也没有"任意轴取位置"的节点**。
- **切片与索引的语义差别**（这是它降不出来的原因）：切片算子取**一个轴**、**保留其余维**；索引算子取**第一维**、并**丢弃**该维。`m[:, 0]` 要的是"取第 1 维的位置 0 并丢掉该维"，核心没有能表达它的单个节点，编译器也不许按名字去调模块算子。
- 原来那条 `test/cases/modules/tensor/subscript/slice_multidim.cml` 其实写的就是 `m[:, 0]`——它当时是 **negative 用例**（钉住"第一维以外不支持"那条 `ConstructNotSupported`）。条形码拆掉之后：
  - 该用例已按您的话改成**正例**（`t[:, a:b]` 那一形态：`ran\n[2, 2]\nsum=16.0\nd=0.0\n`）；
  - `m[:, 0]` 这一形态的覆盖被挪到**新的 negative 行** `index_other_dimension.cml`（钉住"多于一个维度上取范围或位置"的自述式拒绝）。**没有**把它做成正值——那样只能静默读错轴。
- 现在的拒绝是自述式的（无编号、无裁定引用）：`"a subscript that names a range or a position on more than one dimension"` + `"the slice operator takes one axis, and the index operator takes the first dimension"`。

## 需要您定的

- **甲**：`m[:, 0]` **就是要拒绝**（语言里"在后面的维取位置"没有意义，或应当显式写成两次下标/`take`）⇒ 我们把现在的自述式拒绝**保留下来并写进语言参考**（说明为什么），`index_other_dimension.cml` 继续钉它。**若如此，请顺便定一句话**：`m[:, 0]` 应当怎么写才对（例如 `m[:, 0:1]` 再取第一维？还是先 `take`/转置？）
- **乙**：`m[:, 0]` **合法**，要降低 ⇒ 请定降法，二者都是语言级改动，我们不擅自选：
 1. **核心加一个"任意轴取位置"的节点**（取一个轴、丢掉该维）；或
 2. **允许编译器给这类下标补 `permute` + `reshape`**（把目标轴换到第一维再索引），这需要核心有对应节点或允许按名字用模块算子。
- **丙**：别的划法（例如只允许**常量**位置在后面的维上、并要求它降成 `take` 的复合；或规定"位置只能写第一维，后面的维一律写范围"并据此**拒绝**且给出改法）。

## 现在的状态（不自行决定）

- 已落地并已合进 develop：**任意轴的切片**（`t[:, a:b]` 与 `t[a:b]` 是同一个节点，只是 axis 写那一维的序号；任意秩；负步长＝拷贝、步长 1＝视图，值与类型一致——秩 3 用例 `slice_multidim_rank3.cml` 钉 `[2, 3, 3]`/`back=234.0` 与 `[2, 2, 4]`/`mid=232.0`）。
- 那条"等以后"的 `ConstructNotSupported`（含它带的编号）**已按您的话整块删除**，轴不再恒为 0。
- `m[:, 0]` 这一形态**保持拒绝**（自述式文案），覆盖在 `index_other_dimension.cml`；**没有**做成正值。

## 相关状态（同一批交回、不在这里问，只报告）

1. **`ConstructNotSupported` 还剩 3 处使用点**：`src/compile/gct/builder.cpp`（本轮那条"多于一个维度上取范围或位置"——理由：无单节点，**宁可拒绝也不静默读错轴**）；`src/parse/ast/builder.cpp:879`（`rule<...>` 作用在函数上"not implemented"——"等以后"口气，不在这条车道）；同文件 `1434-1437` 的 `giveUp("fact", …)`（6 个调用点：fact 缺类型、缺 case、case 不命名变量等）。
2. **ONNX 侧一处"非单位步长"拒绝仍然活着**（核心与模块已归零，只剩"步长不能为 0"）：`modules/onnx/lowering.cpp:583` 的 `:tangent/scatter_slice_arr` 导出拒绝一切非单位步长（`the positions a step names are not the range ONNX Pad writes`）。这是**导出能力**上的缺口而不是语义分歧——要不要在 `export_matrix` 里按"逐步长"细化（现在只能整算子标 forward/gradient/none），也请您定一句。