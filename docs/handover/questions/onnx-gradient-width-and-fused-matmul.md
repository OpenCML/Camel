# ONNX 梯度链的宽度撞上 ONNX Runtime 的 `FusedMatMul`（一处读数，两条待裁口径）

这是"`pass_export_equals_export_model` 这条红"诊断后**实测撞到的**、我们不敢单方面定的事。读数全部来自主机 `88zjwei`，逐条带逐字证据与 `文件:行`；我们没有改 `src/**`、没有改期望、没有动 `skip`/`known`。

## 先说结论：这条红不在 pass 路径上，两条链产出的文件**字节相同**

- 计划项 `pass_export_equals_export_model`（`test/plans/feat/modules/onnx/onnx.plan.toml:839`）在 `b4461bda1` 上仍是 `expected exit 0, got 1`；traceback 停在 `test/cases/modules/onnx/pass_export.py:111`，即 `a = ort.InferenceSession(str(by_op))` —— **`export_model` 直出模型**那一次会话，程序是 `PROGRAMS` 里第 2 个 `export_train_step.cml`。
- 同一次运行里 `export_train_step` 的两条链各写一个文件，**内容完全相同**：`sha256 fed599ccf940b0130dfc1ea6a4c4afc9efb44a524261b5c1a4f1e993d1ad273f`，2157 B，37 节点、2 个 `Cast`。所以"只有 pass 那条链产出的模型出问题"这个前提是**误读**（前一次记录的 14/37 节点是 `export_fold` 与 `export_train_step` 两个程序的，不是两条链的）。

## 变量只有一个：梯度链的**元素宽度**

同一程序 `test/cases/modules/onnx/export_train_step.cml`（其签名写 `float[..]`，即 f64）：

| 时点 | 模型 | 判读 |
|---|---|---|
| `c6487dc6a`(10-24) | 44 节点／**10 个 Cast**（梯度在 f32 内运算、边界转回 f64） | ORT EXTENDED/ALL **通过** |
| `d059d7690`(10-25) | 44 节点／10 Cast | 通过 |
| `9b486a51f`(10-26) | 45 节点／10 Cast | 通过 |
| **`e18fa57b0`(10-26 06:42)** | 37 节点／**2 Cast**，5 个 `MatMul` 全 `tensor(double)` | **失败**（父 `9b486a51f` 绿、子红） |
| `fad4051a8`(10-26 07:52，develop 首个带上它的提交) | 37 节点／2 Cast | 失败 |
| `b4461bda1`(HEAD) | 37 节点／2 Cast | 失败 |

出问题的节点在两个文件里都一样，且属性只有 `perm`：

- `n_MatMul_3 = MatMul(Transpose_1(Relu), Mul_1)`，即 `lhs^T @ dy`，产出点 `modules/onnx/lowering_grad.cpp:276`（同族 `:265` 是 `dy @ rhs^T`），转置来自 `swapLast`（`modules/onnx/lowering_grad.cpp:217-224`）。
- `com.microsoft.FusedMatMul` **不在文件里**：它是 ONNX Runtime 1.23.2 自己的 level-2 优化 `MatmulTransposeFusion` 在会话初始化时造的，节点名 `n_MatMul_3/MatmulTransposeFusion/`。ORT 逐字给出原因：

  > `NOT_IMPLEMENTED : Failed to find kernel for com.microsoft.FusedMatMul(1) (node:'n_MatMul_3/MatmulTransposeFusion/' ep:'CPUExecutionProvider'). … the types are incompatible. This op has been implemented only for the following types (tensor(float),), but the node in the model has the following type (tensor(double))`

- 同一文件按优化级别分档：`ORT_DISABLE_ALL`／`ORT_ENABLE_BASIC` **通过**，`ORT_ENABLE_EXTENDED`／`ORT_ENABLE_ALL` 失败（`ort.InferenceSession` 的默认档就是 ALL）；两个模型 `onnx.checker.check_model(full_check=True)` 都是 ok。

**对照组（HEAD 上现跑）**：把同一个程序（含 `export_train_step.cml`）的两条链分别用 f32 重跑 ——

- `test/cases/modules/onnx/export_grad_matmul_vector.cml`（f32，58 节点，含 4 个 `MatMul(Transpose…)/…` 梯度）两条链 ORT 全通过，计划里 `pass_export_grad_matmul_vector` 也正是绿的；
- 把 `export_train_step.cml` 的类型改写成 `f32[..]` 的等价程序（38 节点，同样有 `n_MatMul_3 = MatMul(Transpose(…), Mul)`）ORT 全通过。

⇒ 触发变量是**元素宽度 f64**，不是"Transpose 喂 MatMul"这个形态本身。

## 二分逐次读数（探测＝导出该 case 两条链，读 `MatMul` 的元素类型 + ORT EXTENDED/ALL）

```
c6487dc6a  10-24        44/44 节点  f32内  OK
d059d7690  10-25        44/44 节点  f32内  OK
9b486a51f  10-26 06:3x  45/45 节点  10 Cast OK   <- 绿
e18fa57b0  10-26 06:42  37/37 节点  2 Cast  FAILED <- 第一个红（父绿子红）
122012992  develop 落地前 44/44 节点  9 Cast OK
fad4051a8  10-26 07:52  37/37 节点  2 Cast  FAILED <- develop 首个红
b4461bda1  HEAD         37/37 节点  2 Cast  FAILED
```

引入提交：`e18fa57b0`「Answer the eight gradient operators at the value's own width instead of f32: the f32 they declared is what made a sum_to of an f64 step a mixed pair, and declaring the width the operator actually reads removes it at the source without a conversion node」（Zhenjie Wei，2026-10-26 06:42:43，父 `9b486a51f`），由 develop 的 `fad4051a8`（2026-10-26 07:52，父 `122012992`）落地。

候选集排除（按祖先关系，无需再建树）：

| 提交 | 与绿点 `9b486a51f` / 红点 `e18fa57b0` 的关系 | 判读 |
|---|---|---|
| `4f9345c29`、`086181606`、`f20039abe`、`395e8e0d0` | 都是 `9b486a51f` 的**祖先** | 绿状态里已含它们 ⇒ 不是因 |
| `487ad3ea9`、`b8c0090b1`、`7b8021245`（参数名/默认值从声明读）、`4e77ad6a5`（具名 `with` 合并）、`d924829ea`（单一声明） | 都以 `e18fa57b0` 为**祖先**（落在红之后） | 不是因 |

## 要请你定的两条

**(a) 导出器要不要为 `double` 宽度避开会被 ORT 融成 `FusedMatMul` 的形态？**
最直接的形态是 `MatMul(Transpose(A), B)`（只出现在梯度里，`modules/onnx/lowering_grad.cpp:265,276`）。避开它的一条路是 rank-2 时改写 `Gemm(A, B, transA=1)`（`lowering.cpp:293-298` 的 `linear` 已经这么走 `Gemm`）。若可以，请一并裁定 **rank/广播面**：`Gemm` 只吃 2 维、且不广播；批量/向量/高维那几个形状要么留在 `MatMul`（继续撞 ORT 的融合），要么另找拼写。我们倾向：**只在会撞的宽度与形状上改写**，其余不动，但这会让同一算子的两种形状有两种 ONNX 拼写。

**(b) 若不走 (a)：这个 case/期望该怎么站？**
两条都各有一半不合现有裁定：

- 把 `export_train_step.cml` 的签名从 `float[..]` 退回 `f32[..]`（即回到 10-25 之前那条链的宽度）—— 与"八个梯度算子按被微分值的宽度累加"这条新裁定**相反**；而且该裁定自己的新 case 正在 pin "f64 的 d/dw 在 f64 宽度累加"（`8d91c03a7`「Pin the printed wide gradient and the narrow column sums …」）。
- 或者承认这是 **ORT 侧限制**：模型合法（`onnx.checker full_check` ok）、错在 ORT 自己的优化器造出它 CPU EP 只有 `tensor(float)` 实现的算子。那么这条期望应当改成"double 模型不要求 ORT 能建会话"（例如 double 程序只比 `onnx.checker` 与两条链的字节一致，ORT 只跑 f32 程序），而不是记成我们的缺陷。

在这两条定下来之前我们**一行都没改**：`src/**`、`test/**` 期望、`skip`/`known` 都是原样。读数与模型留在主机 `/tmp/diag/`（`dump.py`、`probe.sh`、`control.sh`、`probe-<sha>/*.onnx`）。