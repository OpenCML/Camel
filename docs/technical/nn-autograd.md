# NN Autograd Architecture

本文档记录当前 `nn`/`tensor` 小型自动求导实现。当前主入口是
`nn.apply_gradients(loss)`；旧的 `compile_step` 入口和 tensor-only 线性 MSE
模板已经移除，不再作为公共 API 或兼容路径保留。

## 1. Scope

当前实现目标是 CPU-only toy-model 训练，而不是完整动态图框架。它支持由一组
已注册 VJP 的 tensor/scalar primitive 组成的静态 scalar loss graph，并为
loss graph 中的 `nn:value(Parameter)` 叶子生成 SGD 更新。

训练 step 的形态是：

```text
apply_gradients(<ctx...> (args...) => float)
    -> sync <var trainable_ctx...> (args..., lr: float) => float
```

`apply_gradients` 会把包含 `Parameter` 的 with-context 参数标记为 `var`，其它
context（例如 target tensor）保持只读。参数发现发生在反向图构造时：
`nn:value(p)` 的 VJP 把 cotangent 记录为 `p` 的参数梯度。

## 2. Graph Synthesis

宏执行时的核心流程：

1. 检查输入是静态、非 sync、非 macro 的 scalar loss function。
2. `GraphDraft::decode(lossGraph)` 复制 loss graph，作为训练 step 的 forward graph。
3. 在 norm 参数末尾追加 `lr: float`，并把函数类型改为 `sync` step。
4. 从 loss output 收集可达 forward tape。
5. 用 `1.0` seed loss cotangent，按逆拓扑顺序应用 VJP rule。
6. 对每个参数梯度按参数节点 id 排序后生成：

```text
nn:zero_grad(parameter)
nn:add_grad(parameter, gradient)
nn:sgd(parameter, lr)
```

7. 用 `GATE(loss, ctrl=last_sgd)` 保证返回 loss 之前所有更新已完成。

不可微常量/形状路径（例如 `tensor:zeros`、`tensor:full`、`tensor:shape`、
`:op/idx_arr`、`:op/ltod`）会停止梯度。带梯度的未知 trainable op 会报出明确的
“No builtin VJP rule” 诊断。

## 3. Public API

`nn` 当前导出：

| API | Semantics |
| --- | --- |
| `Parameter` | Mutable trainable tensor object with same-shaped gradient buffer. |
| `parameter(t: Tensor) -> Parameter` | Clone floating tensor data and allocate zero grad. |
| `value(p: Parameter) -> Tensor` | Read parameter data; trainable leaf in autograd. |
| `grad(p: Parameter) -> Tensor` | Read accumulated gradient buffer. |
| `apply_gradients(loss) -> step` | Build forward/backward/update training graph. |
| `zero_grad(var p)` / `add_grad(var p, g)` / `sgd(var p, lr)` | Internal update primitives, still callable directly. |
| `vjp<rule>(f)` | Records user VJP metadata; generic call-node lowering is not implemented yet. |

`compile_step` is intentionally absent.

`tensor` also exposes `sigmoid(t: Tensor) -> Tensor` and `tanh(t: Tensor) -> Tensor`
for toy NN models.

## 4. Builtin VJP Coverage

Registered builtin rules currently cover:

| Category | Operators |
| --- | --- |
| Tensor binary | `tensor:add`, `tensor:subtract`, `tensor:multiply`, `tensor:divide` |
| Linear algebra | `tensor:matmul`, `tensor:transpose` |
| Reduction/shape | `tensor:sum`, `tensor:reshape` |
| Numeric functions | `tensor:exp`, `tensor:log`, `tensor:sigmoid`, `tensor:tanh` |
| Scalar arithmetic | `:op/add_d`, `:op/sub_d`, `:op/mul_d`, `:op/div_d` numerator path |
| Parameter leaf | `nn:value` |

Broadcast support is deliberately narrow:

- Tensor/scalar binary operations use the tensor module’s scalar paths.
- Rank-2 matrix plus row bias (`[N,D] + [D]` or `[N,D] + [1,D]`) is supported in
  tensor binary ops.
- `ParameterObject::addGrad` reduces leading broadcast dimensions before accumulation,
  so bias parameters receive same-shaped gradient buffers.

## 5. Tests And Toy Models

`test/plans/feat/modules/nn.plan.toml` covers:

- migrated `apply_gradients` linear MSE case;
- graph GIR checks for VJP-derived backward/update nodes;
- multi-parameter and bias-gradient accumulation;
- AutoEncoder with sigmoid encoder/decoder;
- fixed two-step Tiny RNN with tanh recurrence;
- Matrix Factorization using one-hot rows and parameter tables;
- legacy direct tensor examples that do not use autograd.

Recommended verification:

```powershell
npm run build
node scripts/test.js test\plans\feat\modules\nn.plan.toml
```

Useful manual checks:

```powershell
camel test\cases\modules\nn\autoencoder.cml std::macro std::nvm
camel test\cases\modules\nn\tiny_rnn.cml std::macro std::nvm
camel test\cases\modules\nn\matrix_factorization.cml std::macro std::nvm
camel test\cases\modules\nn\graph_autograd_multi_param_demo.cml std::macro std::gir
```

## 6. Known Limits

The implementation is still a small static-graph autograd skeleton:

- Function-call VJP lowering is not implemented. Loss lambdas used by
  `apply_gradients` should inline differentiable tensor math directly.
- No arrays/lists of `Parameter` are discovered.
- Broadcast semantics are not NumPy-complete.
- No optimizer state beyond SGD.
- No GPU, BLAS, checkpointing, dataloaders, CNN, Transformer, or Diffusion support.
- `:op/div_d` only propagates through the numerator because current supported losses use
  non-trainable denominators.
