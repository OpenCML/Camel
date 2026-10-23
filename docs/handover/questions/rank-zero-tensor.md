# 秩 0 的张量在 `Tensor` 变成数组别名时怎么表示？

## 问题

H17 第 2 项 P4 要把 `Tensor<T, S>` 变成 `ArrayType` 的别名、删掉 `TensorType`。P4 的前置检查（h352，基线 `102dbc5d1`）发现计划 Q8 的前提「没有测试用秩 0 / `const []`」**不成立**，而秩 0 在 `ArrayType` 里**没有表示**：

- 生产秩 0 的地方：`modules/tensor/ops/utility_ops.cpp`（`iterInfer`：秩 1 张量的元素是 `TensorType::get(dtype, {})`，即秩 0）与 `modules/tensor/ops/layout_ops.cpp`（`reshapeInfer`：目标是 `[]` 时给出 `tensorOf(f32, {})`）。
- 读它的测试：`test/cases/loops/err_tensor_rank0.cml`（编译期 `LoopSequenceNotIterable`，见 `test/plans/feat/loops.plan.toml`）、`test/cases/loops/iterate_tensor.cml`（`len(shape(x)) == 0`）、`test/cases/loops/fault_tensor_rank0.cml`（运行时）。
- 计划的不变量：`docs/design/arrays-memory/plan.cn.md` 第 4 节禁止空的定长 `ArrayType`（会归一成 `{?}`），第 0 节把秩 0 划在 H17 之外。

于是 P4 的迁移表（`TensorType::get(dtype, shape)` → `ArrayType::create(elem, forms)`）会把秩 0 **静默映射成 `T[]`**，把今天一条**编译期拒绝**变成"能编译、运行时出错"——一个被测试钉住的诊断被削弱，而计划里没有这一步。

## 选项与代价

- **(A)（施工方准备照此做）让秩 0 保持编译期被拒**：`reshapeInfer` 对**常量**空目标直接拒绝（infer 里 `throw` 在普通调用处会变成 `SemanticDiag::ArgumentsRejected`，先例见 `test/plans/feat/modules/tensor/bounds.plan.toml`），`iterInfer` 给秩 1 输入的元素**裸 `Tensor` 类型**。代价：`loops.err_tensor_rank0` 的期望从第 10 行的 `LoopSequenceNotIterable` 变成第 9 行的 `ArgumentsRejected`（名字、描述与两个字段都变），是一个计划里没有的新拒绝——**期望的改动需要裁定**。收益：编译期保证保住，不引入秩 0 表示。
- **(B) 保留秩 0**：给 `ArrayType` 加秩 0 形式与拼法。代价：超出 H17 范围（计划第 0 节明确划出），且 `[]` 作为类型与"未知长度"的 `T[]` 拼法冲突，语言侧要另定一条。收益：`err_tensor_rank0` 的期望原样不动。
- **(C) 接受编译期保证的丢失**（秩 0 → `T[]`，改为运行时出错）。代价：一条被测试钉住的诊断被削弱，且改动不写在计划的任何一步里。收益：P4 的迁移表不用特例。

## 施工方建议

**A**：它把"秩 0 不受支持"这条事实留在编译期，只是把说这句话的位置从循环处挪到 reshape/迭代器推导处；`ArgumentsRejected` 带抛出文本，读者仍能看懂。若决策者更看重 `err_tensor_rank0` 的期望一字不动，则选 B，但那要给 `ArrayType` 一个秩 0 形式，属于新的语言工作。

## 施工方的三点提问（一并请裁）

1. 秩 0 选 A / B / C。
2. P4 可以在 A 之下随后更新 `loops.err_tensor_rank0` 的期望吗（那是唯一的测试期望改动）？
3. P4 是否允许**分几步、每步都绿**地做（先用别名把秩≥1 的路径接过去、`TensorType` 只留给秩 0，等本条裁定后再删），还是必须一次落地？施工方判断"一次提交内保证全绿"在本会话里做不到（约 140 处代码、39 个文件、约 150 行期望文本）。

## 来源

- h352 的前置检查报告（`handover/h352-tensor-alias`，基线 `102dbc5d1`；工作树留下 8 个红前用例作为 P4 的验收证据）。
- 计划：`docs/design/arrays-memory/plan.cn.md`（第 0、4 节与 Q8）。
- 代码：`modules/tensor/ops/utility_ops.cpp`、`modules/tensor/ops/layout_ops.cpp`、
  `test/cases/loops/err_tensor_rank0.cml`、`test/cases/loops/iterate_tensor.cml`、
  `test/cases/loops/fault_tensor_rank0.cml`、`test/plans/feat/loops.plan.toml`、
  `test/plans/feat/modules/tensor/bounds.plan.toml`。