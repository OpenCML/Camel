# NN Autograd and Toy Model Targets

本文档固定下一轮 `nn`/`tensor` 重构的工作目标。它描述本轮要交付的能力、
明确不做的范围，以及用于判断工作是否完成的验收标准。

## 0. 当前完成状态

截至本轮实现，主线目标已完成：

- `apply_gradients` 已成为唯一公共训练 step 宏；旧 `compile_step` 和
  tensor-only 线性 MSE 模板已移除。
- `apply_gradients` 现在基于输入 loss graph 构造 forward tape、VJP backward
  graph、参数梯度累加和 SGD update graph。
- 支持多个 `Parameter`，包括 struct 内嵌字段访问，更新顺序按参数节点 id
  确定。
- AutoEncoder、Tiny RNN、Matrix Factorization 三个 toy 模型均已加入
  `test/cases/modules/nn/`，并纳入 `test/plans/feat/modules/nn.plan.toml`。
- 已支持 `sigmoid` 和 `tanh` 两个激活函数，以及有限 bias broadcast 梯度归约。
- CNN/Transformer/Diffusion、完整动态图、完整 NumPy broadcasting、GPU 和
  optimizer state 仍按非目标处理。

仍明确延后：

- 通用函数 call-node 的反向 lowering；当前 `apply_gradients` loss lambda 中的
  可微路径应直接写成 primitive tensor/scalar ops。
- `sum_axis` VJP、完整 denominator gradient、数组/list 中的 `Parameter` 发现。

## 1. 总目标

本轮目标是把 `nn` 从线性 MSE 专用 demo 推进到可支撑多个 toy 模型的
CPU-only 小型自动求导框架。

核心交付：

- 长期公共 API 回到原始设计：`apply_gradients` 输入前向/损失计算图，
  输出带 forward、backward、梯度累加和 optimizer update 的完整训练计算图。
- 当前 `compile_step` 和线性 MSE 模板只是临时跑通路径，只能作为实现参考，
  不构成 API、行为或兼容性约束。
- 新的 `apply_gradients` 不再依赖线性 MSE 手写模板，而是基于 loss graph 构造
  forward tape、反向传播图和参数更新图。
- 支持由当前可微 tensor ops 组成的 scalar loss，对多个 `Parameter`
  自动累加梯度。
- 支持 struct 内递归发现 `Parameter`，并以确定性顺序执行 SGD 更新。
- 新增并跑通至少三个非 MLP toy 模型：AutoEncoder、Tiny RNN、
  Matrix Factorization。
- 保持 CPU-only，不追求训练效果、真实数据、checkpoint 或生产级性能。

## 2. 明确决策

本轮采用以下范围决策：

- 必要时允许彻底重构当前 `nn`/`tensor` 实现，不以保留短期兼容层为优先目标。
- 不被当前 public API 约束；现有 `compile_step`、旧 `apply_gradients` 签名和
  专用模板都可以删除、替换或重命名。
- 不要求前向兼容当前临时 API，一切以最优雅、最一致、最可维护的终态设计为准。
- 允许引入新的运行时数据类型、GIR 辅助结构或 autograd 专用中间结构。
- 当存在多条实现路径时，优先选择长线架构更清晰、边界更稳定的方案，而不是短期最快见效的方案。
- 避免留下长期双轨、模板 fallback、临时 shim 或难以删除的兼容桥；如果确实需要过渡层，必须明确删除条件。
- 每完成一个阶段性目标，可以及时创建 focused commit，避免大规模重构长期滞留在未提交状态。
- 允许大规模重构或移除 `compile_step`，不保留线性 MSE 双路径兼容作为硬要求。
- 优化器只做 SGD。
- 参数发现第一版只要求支持 struct 递归；tuple 可作为低成本补充。
- 不要求支持 array/list 中存放 `Parameter`。
- Broadcast 只做有限场景：scalar/Tensor 和常见 bias 形状。
- RNN 可以使用固定步数展开；不要求第一版支持任意递归 call graph 的反传。
- Matrix Factorization 可以先用 one-hot + matmul 模拟 embedding lookup。
- Tiny CNN 是 stretch goal，不阻塞主线交付。

## 3. 非目标

以下内容不属于本轮目标：

- 完整 NumPy/PyTorch broadcasting 语义。
- GPU 后端、BLAS 接入或 kernel 性能优化。
- Adam、momentum、weight decay、optimizer state dict。
- Checkpoint、模型保存加载、dataset/dataloader、batch shuffle。
- 完整 CNN、Transformer、Diffusion 架构。
- 真实数据集效果、准确率指标或长时间训练稳定性。
- 完整动态图 autograd。

## 4. Autograd 交付范围

`apply_gradients(loss_or_forward)` 应成为本轮和长期的主入口。当前
`compile_step` 可以作为临时代码被删除、内联或迁移为内部 helper。

推荐用户侧形态：

```camel
let step = apply_gradients(<model: Model, target: Tensor> (x: Tensor): float => {
    let pred = forward<model>(x)
    return loss(pred, target)
})
```

返回的训练 step 应表达为：

```text
sync <var model: Model, target: Tensor> (x: Tensor, lr: float) => float
```

语义上，`apply_gradients` 输入一个前向/损失计算图，输出一个完整训练计算图：

```text
forward graph
+ scalar loss
+ backward graph
+ gradient accumulation
+ optimizer update
+ gated loss return
```

目标行为：

- 接收静态 loss function。
- 从 loss graph clone 或 lower forward 计算到 step graph。
- 对 step graph 中可微节点按逆拓扑顺序应用 VJP。
- 将 loss cotangent seed 为 `1.0`。
- 将 `nn:value(Parameter)` 识别为 trainable leaf。
- 对每个发现的 `Parameter` 生成：

```text
nn:zero_grad(parameter)
nn:add_grad(parameter, gradient)
nn:sgd(parameter, lr)
```

- 用控制边保证更新完成后才返回 loss。
- 遇到缺失 VJP 的可微路径时给出明确错误诊断。

第一版不要求支持所有 GIR 节点。不可微或未支持节点应有明确策略：

- shape/index 这类只参与形状计算的路径可以标记为 non-differentiable。
- trainable path 上缺 VJP 必须报错。
- 与参数无关的常量路径可跳过。

## 5. 必需 VJP 和 Tensor 能力

本轮主线至少需要覆盖：

| 类别 | 必需项 |
|------|--------|
| 基础二元运算 | `tensor:add`、`tensor:subtract`、`tensor:multiply`、`tensor:divide` |
| 线性代数 | `tensor:matmul`、`tensor:transpose` |
| 规约/形状 | `tensor:sum`、有限 `tensor:sum_axis`、`tensor:reshape` |
| 数值函数 | `tensor:exp`、`tensor:log` |
| NN 函数 | `relu`、`sigmoid`、`tanh` 中至少两个；`mse_loss` 必需 |
| 参数叶子 | `nn:value` |

Broadcast 第一版只要求：

- Tensor 与 scalar 运算。
- `[N, D] + [D]` 或 `[N, D] + [1, D]` 的 bias 场景。
- 对被 broadcast 的 bias 梯度按 batch 维求和。

如果实现成本过高，`softmax_cross_entropy` 可以作为 fused loss 优先于完整
`softmax` 的逐 op backward。

## 6. 模型交付范围

### 6.1 AutoEncoder

目标：验证 dense encoder/decoder、非线性激活和 reconstruction loss。

建议结构：

```text
x -> Linear(d_in, h) -> activation -> Linear(h, d_in) -> mse_loss(x_hat, x)
```

验收：

- 使用 `Parameter` struct 表示两层权重和 bias。
- 不手写梯度。
- 训练若干 tiny step 后 loss finite，且至少一次更新后 loss 下降或参数变化。

### 6.2 Tiny RNN

目标：验证有状态序列模型和固定步数展开。

建议结构：

```text
h_t = tanh(x_t @ Wxh + h_{t-1} @ Whh + b)
y   = h_T @ Why + by
loss = mse_loss(y, target)
```

验收：

- 使用固定长度序列，长度可为 2-4。
- 可以手写展开 `step0/step1/step2`，不要求通用循环反传。
- `Wxh`、`Whh`、`Why` 至少两个参数发生更新。

### 6.3 Matrix Factorization

目标：验证非视觉/非 MLP 类模型，并推动参数矩阵作为表的使用方式。

第一版可以不用 true embedding：

```text
user_vec = one_hot_user @ user_table
item_vec = one_hot_item @ item_table
pred = sum(user_vec * item_vec)
loss = mse_loss(pred, rating)
```

验收：

- 使用两个参数表：`user_table` 和 `item_table`。
- 不手写梯度。
- tiny synthetic rating 上执行 step，loss finite，参数更新。

### 6.4 Tiny CNN Stretch Goal

如果主线提前完成，再考虑 Tiny CNN。

最小目标：

- CPU `conv2d` forward。
- 对 `conv2d` 增加内置 VJP 或显式 backward primitive。
- 支持单通道、小尺寸、固定 stride/padding。

验收：

- tiny image classifier 或 reconstruction case 能执行一次更新。
- 有 shape mismatch 诊断。

CNN 不应阻塞 AutoEncoder、Tiny RNN、Matrix Factorization 的交付。

## 7. 测试标准

新增测试应放在 `test/cases/modules/nn/` 和
`test/plans/feat/modules/nn.plan.toml`。

每个模型测试的最低断言：

- 程序退出码为 0。
- 输出包含 before/after loss 或参数更新摘要。
- loss 是 finite number。
- 至少一个训练 step 后参数发生变化。
- 如果数值稳定，断言 after loss 小于 before loss；否则只断言参数变化。

Autograd 基础测试还应包含：

- 多参数 loss 的 GIR 中出现多个 `nn:add_grad` / `nn:sgd`。
- 缺 VJP 的 trainable path 有清晰错误。
- bias broadcast 的梯度 shape 正确。

建议验证命令：

```powershell
npm run build
node scripts/test.js test\plans\feat\modules\nn.plan.toml
```

开发中可用单例快速运行：

```powershell
camel test\cases\modules\nn\autoencoder.cml std::macro std::nvm
camel test\cases\modules\nn\tiny_rnn.cml std::macro std::nvm
camel test\cases\modules\nn\matrix_factorization.cml std::macro std::nvm
```

## 8. 文档交付

本轮实现完成后需要同步更新：

- `docs/technical/nn-autograd.md`：记录新的 graph-based autograd 架构。
- 本文档：标记已完成、延后或变更的目标。

如果新增 `nn` 标准层或 loss API，应在文档中列出稳定签名和已知限制。

## 9. 成功定义

本轮完成的最低标准：

- `apply_gradients` 已从单一线性 MSE 模板推进为通用小图 autograd skeleton；
  `compile_step` 不再作为长期公共入口约束设计。
- AutoEncoder、Tiny RNN、Matrix Factorization 三个 toy 模型至少有两个通过自动求导训练 step。
- 现有 `nn` 测试继续通过，或旧线性 MSE 测试被有意迁移到新 API 并通过。
- 文档明确记录仍不支持 CNN/Transformer/Diffusion 完整架构。

理想标准：

- 三个主线 toy 模型全部通过。
- `std::gir` 测试覆盖多参数 backward/update 图。
- Tiny CNN stretch goal 至少完成 forward 和 shape 测试。
