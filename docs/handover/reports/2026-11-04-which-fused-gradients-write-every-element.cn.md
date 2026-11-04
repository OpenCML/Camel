# 2026-11-04 融合梯度算子：哪些内核建出来的宽度与它声明的宽度不符

## 一句话

`grad(sum(gelu(x)))` 在 f64 上读出来的 `1.5769529523e-314` 不是"未初始化内存"，是**宽度不符**：
内核 `createTensor(TypeCode::Float32, …)` 建了 12 字节并按 f32 把自己那块内存**写满了**，而算子
声明（`#gradientLikeTyped` → `tensorOf(like.dtype, like.shape)`）说结果是 f64，读要 24 字节。
f32 的 4 字节落在 double 的低半、高半为 0，读出来就是 1e-314 量级的 denormal。**它对给定输入是
确定的**：同一程序跑两次逐位相同，换成另一次分配、另一个进程也是同一个数；变的只有输入——
`x=[-1,0,1]` 读 `1.5769529523e-314`，`x=[1,0,0]` 读 `5.30626085e-315`。所以"跑两次是否相同"
这条判据会把它整类漏掉。

## 判据（三条；第一条最硬）

1. **`len(to_bytes(g)) == numel(g) × 声明元素类型的宽度`**。`to_bytes` 按存储自己的 dtype 写字节，
   所以长度就是存储的真实宽度：f64 `[3]` 应 24，读到 12 就是这一类。
2. 值的 dtype 与算子声明的结果类型一致（梯度算子声明的是"参数自己的元素类型"）。
3. 内核里 `createTensor(TypeCode::…)` 的 TypeCode 是否**写死 f32**，而读操作数走
   `dataAs<float>()`——`#asFloat` 只校验 f32/f64，**不转换**（`modules/tensor/kernels/gradient.cpp` 的注释写明）。

"跑两次是否相同"抓不到这一类：它每次都相同，所以不能用它当唯一判据。

## 清单（tensor 模块 12 个 + nn 模块 9 个）

| 算子 | 注册处 | 建出来的宽度 = 声明宽度？ | 实测判据（读数） | 处置 |
|---|---|---|---|---|
| `gelu_grad` | `modules/tensor/ops/gradient_ops.cpp#gradientOps` | **否**（f64 `[3]`：写 12 B / 声明 24 B） | `bytes=12 d=[0.0, 1.5769529523e-314, 0.0]`；`[1,0,0]` 换成 `5.30626085e-315` | **已修**：加 f64 臂 |
| `erf_grad` | 同上 | **否** | `bytes=12 d=[0.0, 5.098027537e-315, 0.0]` | **已修**：加 f64 臂 |
| `erfc`（经 `erf_grad` 取负） | `modules/tensor/ops/elementwise_ops.cpp#erfcVjp` | **否**（与 `erf_grad` 同一内核） | `bytes=12 d=[0.0, 1.570800649e-314, 0.0]` | 随 `erf_grad` 修好 |
| `layer_norm_grad` | `modules/tensor/ops/gradient_ops.cpp#gradientOps` | 是（`#layerNormGradArm` 已按 `SumElement<Code>` 分臂） | f64 `[2,3]`：`dx bytes=48`、`dgamma/dbeta bytes=24`，值 = 解析导数 | 只对齐 `#layerNormGradInfer` 的陈述（原写死 f32），值不动 |
| `sum_to` | 同上 | 是（`#sumToArm` 按 `SumElement<Code>` 分臂 + `zeroInit`） | f64：`dx bytes=48`、`dp bytes=24`，`dp=[5,7,9]` 是列和 | 不动 |
| `broadcast_like` | 同上 | 是（`k::fill` 按 `like` 的 dtype 逐元素写） | f64：`bytes=48`，全 2.0 | 不动 |
| `expand_axis` | 同上 | 是（`k::broadcastTo` 逐元素写） | f64：`bytes=48`，全 1.0 | 不动 |
| `matmul_grad_lhs` / `_rhs` | 同上 | 是（f64 不走 GEMM 快路，走通用 matmul） | f64：`bytes=48/48`，`dA=[[1,1,4],[1,1,4]]`、`dC=[[5,5],[7,7],[9,9]]` | 不动 |
| `permute_inverse` | 同上 | 是（`k::permute` 逐元素写） | f64：`bytes=48/48`，`dx=wᵀ` | 不动 |
| `cast_like` | 同上 | 是（写的是 `like` 的 dtype，与 `#castLikeInfer` 说的一致；等宽时直接返回 `g`） | 读码；未造探针 | 不动（可疑未复现，理由见下） |
| `zeros_like` / `ones_like` | 同上 | 是（`k::full` 逐元素写，dtype 取参数） | 间接：它们是 tangent 的零元/单位元，f64 梯度里未出现宽度错 | 不动 |
| `take_grad` | `modules/tensor/ops/layout_ops.cpp#takeGradKernel` | 是（`zeroInit=true` 后用 `input->dtype()` 累加） | f64 `[4]`：`bytes=32`，`[0,2,0,1]`（索引 `[1,1,3]`） | 不动 |
| `row_add_at` | `modules/tensor/ops/layout_ops.cpp#rowAddAtKernel` | 是（`memcpy` 整块 `acc` 后再加一行） | 读码；未造探针 | 不动（可疑未复现，理由见下） |
| `softmax_grad` | `modules/tensor/ops/reduction_ops.cpp#softmaxGradKernel` | 是（`dtypeOf<T>()`） | f64：`bytes=24`；`~1e-17` 是 `y(1-Σy)` 的算术残差，不是垃圾 | 不动 |
| `nn:conv2d_input_grad` / `_kernel_grad` | `modules/nn/tensor_ops.cpp#nnTensorOps` | 是（`zeroInit=true` 的 f32 累积） | 读码：`#requireF32` + 签名只有 `f32[..]` 重载；实测同类见 pool 那条 | 不动 |
| `nn:conv2d_bias_grad` | 同上 | 是（每个 `db[o]` 都写） | 同上 | 不动 |
| `nn:max_pool2d_grad` / `avg_pool2d_grad` | 同上 | 是（`zeroInit=true`，散射前先清零） | **实测**：f64 输入被拒，原文 `RuntimeDiag::OperandTypeUnsupported: 'pool2d_grad' does not support a value of type 'f64[..]'; it takes f32 tensors.` | 不动 |
| `nn:batch_norm_train_grad` | 同上 | 是（dx 逐元素、dgamma/dbeta 每通道都写） | 读码：`#requireF32` + 签名只有 f32 重载 | 不动 |
| `nn:embedding_table_grad` | `modules/nn/tensor_ops.cpp#nnTensorOps`（内核 `modules/nn/layers.cpp#embeddingTableGrad`） | 是（`zeroInit=true`，`table->dtype()`） | 读码：签名 `f32[..]`，无 f64 重载 | 不动 |
| `nn:softmax_cross_entropy_grad` / `_labels_grad` | `modules/nn/tensor_ops.cpp#nnTensorOps`（内核 `modules/nn/losses.cpp#softmaxCrossEntropyGradLogits` / `#softmaxCrossEntropyLabelsGradLogits`） | 是（每个 `(row, col)` 都 `setFromDouble`） | 读码：签名 `f32[..]`，无 f64 重载 | 不动 |

nn 那 9 个的结论：**签名里只有 `f32[..]` 重载**（`stdlib/signatures/nn.cml` 逐行读过），内核另加
`#requireF32`；实测拿 `max_pool2d_grad` 打了一次 f64，得到上面那条诊断原文——它在**运行期就被拒**，
所以这 9 个不会静默给错数。要给 f64 开臂是另一件事，不在本笔。

## 修了什么，证据是什么

- `#geluGrad` / `#erfGrad`：改为按输入元素类型分臂（`#geluGradArm` / `#erfGradArm`，`SumElement<Code>`
  取宽度，与 `#sumToArm`、`#layerNormGradArm` 同一套写法）。
  **f32 臂原样保留**（同一 `#geluGradUnit`、同一 `kScale` 字面量、同一乘积次序），f64 臂是新写的。
  另加一条：`dy` 与 `x` 的元素类型不同就 `refuseTensorElementType` 拒绝，而不是拿 `x` 的宽度去读 `dy`
  （`#asFloat` 不转换，混宽读就是同一类错）。
- f64 逐元素正确（中心差分 `(f(x+h)-f(x-h))/2h`，对**前向**做差分，不是对导数再做一次）：

| 位置 | 本笔读数 | 中心差分 | 差 |
|---|---|---|---|
| `gelu'(-1)` | -0.08296408384578255 | h=1e-3: -0.08296396355907121（h=1e-6: -0.08296408385288778） | 1.2e-7（7.1e-12） |
| `gelu'(0)` | 0.5 | 0.5 | 0 |
| `gelu'(1)` | 1.0829640838457826 | h=1e-3: 1.0829639635590027 | 1.2e-7 |
| `erf'(-1)` / `erf'(1)` | ±0.4151074974205947 | h=1e-3: 0.4151076357353928 | 1.4e-7 |
| `erf'(0)` | 1.1283791670955126 | h=1e-3: 1.1283787911200993 | 3.8e-7 |
| `layer_norm` dx（两行相同） | 0.4082360432562664, -0.8164904572722602, 0.40825441401599355 | h=1e-3 同上三值 ±1.4e-7；（h=1e-6 时 ≤4.2e-10） | ≤1.4e-7（≤4.2e-10） |
| `layer_norm` dgamma / dbeta | -2.4494713718167804, 0, 2.4494713718167804 / 2, 2, 2 | h=1e-6: 同 | ≤1.7e-10 |

  1e-7 是 **h=1e-3 的中心差分截断误差**（O(h²)），不是内核的误差：h=1e-6 时同样几个数落到 1e-11
  量级；`gelu` f64 的读数与按解析式独立算出的 double 是**同一个数**（`-0.08296408384578255`）。

- **f32 逐字不变**：`gelu_grad.bits` 与 `gelu_grad.bits.generic_unit` 两个 digest 用例（它们钉的是
  f32 梯度存储位的 sha256）在计划里原样通过，一位没动。**前向**：本笔不碰任何前向内核；
  `fwd gelu/erf/layer_norm` 的读数修前修后相同（`bytes=24/24/48`，值相同）。
- 用例：`test/cases/modules/tensor/grad_f64_width.cml` + 计划条目 `grad.f64.width`（nvm/fvm 两后端
  必须一致）。它同时钉 `bytes=`（宽度）和数值（1e-12），所以"把缓冲重新变窄"这件事会直接红。
  修复前该用例的读数：`f64.gelu bytes=12 d=[0.0, 1.5769529523e-314, 0.0]`。

## 可疑未复现（读码可疑、实测没复现，写清理由）

- `cast_like`：写出的 dtype 是 `like` 的 dtype，正是 `#castLikeInfer` 声明的那个；两边等宽时它直接
  返回 `g`（不拷贝），宽度也不可能错。没造出只靠 `grad` 触达它的探针，故只列读码结论。
- `row_add_at`：`memcpy` 把 `acc` 整块复制过去再加一行，等于整块都被写。触达它要 `for row in t`
  的循环梯度，本笔没造那个探针。
- `matmul_grad_lhs/_rhs` 的 GEMM 快路只在 f32 上生效（`#matmulGradGemm` 检查 `Float32`），f64 落到
  通用 matmul 路径。f64 的读数是正确宽度与正确值，但**慢**：这是性能观察，不是缺陷。
- `softmax_grad` 的 `1e-17`：`y_i(1-Σy)` 的算术残差（`Σy` 在 f64 下偏离 1 约 1e-16），不是未写的字节。

## 与 lane-uninit 的关系（合并前必读）

本笔在 `#geluGrad` / `#erfGrad` 里独立写了同一处修复，f64 读数与那条车道钉的三组数**逐位相同**
（`[-0.08296408384578255, 0.5, 1.0829640838457826]` 等）。两条都合会在这两个函数和
`tensor.plan.toml` 尾部冲突，**取一份即可**；本笔另外多两样：`layer_norm_grad` 的 infer 陈述对齐、
混宽操作数的显式拒绝、以及按"字节数 = 元素数 × 声明宽度"判的用例。

## 闸门（主机 88zjwei，`-Tier fast -Jobs 4`，一次一个目标；load 为 1m/5m/15m）

| 目标 | 基线（本树回退 2 个源文件、用例仍在） | 本笔 | load |
|---|---|---|---|
| `test/plans/feat/modules/tensor.plan.toml` | 114 = **112 / 2**（两条红正是 `grad.f64.width.nvm/.fvm`，读数 `bytes=12 …1.5769529523e-314`） | 114 = **114 / 0** | 21.0 / 25.6 / 24.1（基线那次 36.7）；提交后复跑 exit 0，15.6 / 20.9 / 22.5 |
| `test/plans/feat/modules/autodiff.plan.toml` | 未单独跑（本笔只动梯度内核与一条 infer） | 263 = **263 / 0** | 峰值 44.9 |
| `test/plans/feat/modules/nn.plan.toml` | 同上 | 68 = **68 / 0** | 25.8 / 27.0 / 24.5 |

基线那一栏的用法：回退源码后**只有两条用例红**，说明"这条用例抓住了缺陷、其余 112 条不因这笔动"。