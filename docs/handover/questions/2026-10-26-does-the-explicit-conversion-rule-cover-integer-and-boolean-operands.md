# 显式转换这条规则，管不管整数与布尔操作数？

**问谁**：语言作者（负责人）
**问什么**：逐元素算子里，"元素类型不同就必须显式写出转换"这条，是否也覆盖整数与布尔操作数；若是，标准库与 `nn` 里那批把 `f32` 张量与 `bool` 掩码/整数一起算的写法要不要一并改成 `cast(...)`。
**为什么要问**：这不是"实现没做完"，而是**两条现行规则在这里交叉**，按哪一条走会得到两套不同的语言；而按"任何不同元素类型都拒"走，会一次改动 28 处库内既有写法。

## 已落地的部分（本轮已在 develop）

按 2026-10-24 的裁定（`gradientLikeTyped` 三处返工）"**混合只看元素类型，形状知不知道都一样**"，逐元素算子的判据已改：两个张量操作数**元素类型不同且都是 `f32`/`f64`** 时，由**签名路径**拒（`SemanticDiag::ArgumentsRejected`，不新造按名字的拒绝、不放宽成隐式转换）。四条用例钉住（`dtype.plan.toml`）：

- `mixed_width_known_shapes`：`float[2] × f32[2]` 拒——消息逐字为 `the two tensor operands have different element types ('float' and 'f32')` 并给出 `cast(value, "float") or cast(value, "f32")`；
- `mixed_width_open_shapes`：`float[..] × f32[..]` 同样拒（**形状未知不放过**）；
- `mixed_width_scalar_value`：`f32(2)` 这种"类型是自己的"值在 `f64` 数组旁被拒；
- `one_width_arithmetic`：正例，`f32 × f32`、`f64 × f64`、以及字面量 `x * 2.0`（字面量按上下文取类型）照旧。

改前现状是**静默算出结果**（`f64[2] × f32[2]` 打印 `5.0`，已知形状与开放形状都一样）。

## 为什么没有一次切到"任何不同元素类型都拒"

按最宽写法（任何不同的元素类型都拒）跑整层，**28 条红**，全部是库内既有写法：

- `f32` 张量与 `bool` 掩码一起算：`nn.svm`、`nn.embedding_*` 等；
- 整数操作数参与逐元素算术。

这 28 处不是错误用法**在现行文档下**：`modules/tensor/dtype.cpp` 的 `promoteTensorTypes` 是**带文档的"widening only"规则**（并且它在 `f16`/`bf16` 混用时**自己就报 cast**）——即"同类数值宽度之间允许隐式加宽，存储类型之间必须写转换"。

## 两条规则在这里交叉

1. **2026-10-24 的裁定**："按上下文取类型的是**字面量**；一个 `f64` 的变量就是 `f64`，遇到 `f32` 数组要显式转换。"
2. **同一天的 `broadcast_like`/`sum_to` 裁定**："混合元素类型非法，**由签名拒、不按名字拒**"，并且"声明求值器不要再采样 `(i64[], f32[])`/`(bool[], f32[])` 这两种组合"——**这句把整数/布尔与浮点的混合也归进了"非法"**。

第 1 条只提了浮点两种宽度；第 2 条的括号里点了整数与布尔。**同一个原则在两处的外延不同**，我们不敢替您定。

## 候选

- **甲：只有浮点宽度之间需要显式转换。** 整数与布尔操作数保留 `promoteTensorTypes` 的加宽规则（那 28 处库内写法不动）。理由：掩码与索引本来就不是"同一种数的两种宽度"。
- **乙：任何不同的元素类型都必须显式转换。** 那么 `f32 × bool`、`f64 × i64` 也要写成 `cast(...)`（或改用 `select`/`where` 一类算子），**28 处库内写法要一并改**，`promoteTensorTypes` 的"widening only"文档要跟着改写（它现在的措辞会与裁定冲突）。
- **丙：别的划法**（例如只允许"整数→整数"与"布尔→整数"的加宽，浮点任何方向都必须显式；或反过来只允许"布尔→浮点"作为掩码例外）。

## 附带需要一并确认的两点

1. **加宽与显式转换的方向**：现有实现里 `bool → f32`、`i32 → f64` 这类**加宽**是隐式的；若走乙，则"张量操作数之间的任何类型差异"都要写出来，`promoteTensorTypes` 保留给谁？
2. **同一族的其它算子要不要同步**（本轮**只切了逐元素算术**，其余只量未改）：① 比较族 `compareInfer` **完全不看元素类型**，`f64[..] == f32[..]` 现在放过；② `matmul`/`linear` 用同一个 `promote`；③ ONNX 导出器自己那份推断（`modules/onnx/lowering.cpp`）也 `promote`，任何改动必须与算子同步，否则导出与执行两套口径。

## 证据位置（都在 develop 上）

- 判据改动：`modules/tensor/ops/elementwise_ops.cpp` 的 `operandType`（原为两个张量一律 `promote`）。
- 文档化的加宽规则：`modules/tensor/dtype.cpp` 的 `promoteTensorTypes`。
- 一族未改处：`modules/tensor/ops/elementwise_ops.cpp:172`（`compareInfer`）、`modules/tensor/ops/linalg_ops.cpp:71,85`、`modules/onnx/lowering.cpp:222,1023`。
- 本轮按"写转换"改掉的 6 处用例内混合对：`nn/embedding_grad_sanity.cml`、`nn/softmax_cross_entropy_sanity.cml`、`onnx/export_impure.cml`（算术与打印值一字未动，改前行为本来就是 promote 到 `f64`）；`onnx.plan.toml` 里一处钉住的不纯调用列号随之从 `13:51` 移到 `13:56`（只改位置，断言语义未动）。