# NN Autograd Architecture

本文档记录当前 `nn`/`tensor` 小型自动求导实现。当前主入口是
`nn.apply_gradients(loss)`；旧的 `compile_step` 入口和 tensor-only 线性 MSE
模板已经移除，不再作为公共 API 或兼容路径保留。

## 1. Scope

当前实现目标是 CPU-only toy-model 训练，而不是完整动态图框架。它支持由一组
已注册 VJP 的 tensor/scalar primitive 组成的静态 scalar loss graph，也支持
loss graph 中的静态、非 sync、非 macro、非递归 helper/layer function calls。
`apply_gradients` 会先把这些 helper call lower 到同一条 primitive tape，再为
其中的 `nn:value(Parameter)` 叶子生成 SGD 更新。

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
2. 从 loss output 出发验证可达 helper-call closure。递归、`sync` helper、
   macro helper、indirect `CALL` 和 opaque callee 会在 lowering 前报错。
3. 用 `RuntimeGraphDraftSession` 复制 loss graph，作为训练 step 的 draft。
4. 在 norm 参数末尾追加 `lr: float`，并把函数类型改为 `sync` step。
5. 在 output-reachable tape 上反复调用 draft inline，把 static helper/layer
   `FUNC` node lower 到 primitive 节点，直到训练路径上不再有 helper call。
6. 从 lowered loss output 收集可达 forward tape。
7. 用 `1.0` seed loss cotangent，按逆拓扑顺序应用 VJP rule。
8. 对每个参数梯度按参数节点 id 排序后生成：

```text
nn:zero_grad(parameter)
nn:add_grad(parameter, gradient)
nn:sgd(parameter, lr)
```

9. 用 `GATE(loss, ctrl=last_sgd)` 保证返回 loss 之前所有更新已完成。

不可微常量/形状路径（例如 `tensor:zeros`、`tensor:full`、`tensor:shape`、
`:op/idx_arr`、`:op/ltod`）会停止梯度。带梯度的未知 trainable op 会报出明确的
“No builtin VJP rule” 诊断。如果 helper `FUNC` 或 indirect `CALL` 在 lowering
后仍处于 trainable path，VJP traversal 会把它作为 unsupported call 报错，而不是
静默跳过。

## 3. Helper Calls And Parameter Aliasing

Call-aware autograd uses tape-before-inline lowering. It is intentionally static:

- supported calls must resolve to a callee graph at compile/macro time;
- helper functions must be non-`sync`, non-macro, and non-recursive;
- helper bodies may capture model fields, target tensors, and constants through
  `with` context;
- helper returns are expected to feed the scalar loss as `Tensor` or `float` values;
- all differentiable primitives on the trainable path must have builtin VJP rules.

Unsupported forms fail with explicit diagnostics. Current errors cover recursive
helper graphs, `sync` helpers, macro helpers, indirect call nodes, opaque callees,
surviving helper calls after lowering, and missing VJP rules on trainable paths.

Inlining clones structural access nodes at each call site, so parameter gradients
are merged by static alias key rather than by the cloned `nn:value` node. The alias
key is:

```text
root with-context object node + field/index access path
```

This makes repeated reads such as `model.encoder.weight` across branches or time
steps collapse to one `zero_grad/add_grad/sgd` update. The implementation does not
canonicalize arbitrary runtime object aliases stored under different static paths;
two distinct model fields are treated as two trainable leaves even if a program
constructs them from the same `Parameter` object.

## 4. Public API

`nn` 当前导出：

| API | Semantics |
| --- | --- |
| `Parameter` | Mutable trainable tensor object with same-shaped gradient buffer. |
| `parameter(t: Tensor) -> Parameter` | Clone floating tensor data and allocate zero grad. |
| `value(p: Parameter) -> Tensor` | Read parameter data; trainable leaf in autograd. |
| `grad(p: Parameter) -> Tensor` | Read accumulated gradient buffer. |
| `apply_gradients(loss) -> step` | Build forward/backward/update training graph. |
| `zero_grad(var p)` / `add_grad(var p, g)` / `sgd(var p, lr)` | Internal update primitives, still callable directly. |
| `softmax_cross_entropy(logits: Tensor, target: Tensor) -> float` | Stable fused rank-2 classification loss over `[batch, classes]` logits and one-hot/distribution targets. |
| `softmax_cross_entropy_grad(logits, target, dy) -> Tensor` | Fused logits-gradient primitive used by the VJP rule; callable for operator sanity checks. |
| `embedding(table: Tensor, indices: Tensor) -> Tensor` | Rank-2 table gather with rank-1 int64 indices. |
| `embedding_table_grad(table, indices, dy) -> Tensor` | Dense table-gradient primitive used by the embedding VJP; repeated indices accumulate into one row. |
| `conv2d(input, kernel, bias) -> Tensor` | Valid CPU NCHW convolution for input `[N,C,H,W]`, kernel `[O,C,KH,KW]`, rank-1 bias `[O]`, stride 1, no padding. |
| `conv2d_input_grad(input, kernel, dy) -> Tensor` | Dense input-gradient helper used by the conv2d VJP. |
| `conv2d_kernel_grad(input, kernel, dy) -> Tensor` | Dense kernel-gradient helper used by the conv2d VJP and operator sanity checks. |
| `conv2d_bias_grad(bias, dy) -> Tensor` | Dense bias-gradient helper used by the conv2d VJP and operator sanity checks. |
| `vjp<rule>(f)` | Records user VJP metadata; helper calls currently train by pre-VJP inlining, not by invoking custom function VJP graphs. |

`compile_step` is intentionally absent.

`tensor` also exposes `sigmoid(t: Tensor) -> Tensor` and `tanh(t: Tensor) -> Tensor`
for toy NN models. It also exposes row-wise rank-2 `softmax(t: Tensor) -> Tensor`
and `softmax_grad(output, dy) -> Tensor` for tiny attention and operator sanity
checks.

## 5. Builtin VJP Coverage

Registered builtin rules currently cover:

| Category | Operators |
| --- | --- |
| Tensor binary | `tensor:add`, `tensor:subtract`, `tensor:multiply`, `tensor:divide` |
| Linear algebra | `tensor:matmul`, `tensor:transpose` |
| Reduction/shape | `tensor:sum`, `tensor:reshape` |
| Numeric functions | `tensor:exp`, `tensor:log`, `tensor:sigmoid`, `tensor:tanh` |
| Attention support | `tensor:softmax` row-wise rank-2 path |
| Fused NN losses | `nn:softmax_cross_entropy` logits path |
| Fused NN layers | `nn:embedding` table path |
| Convolution | `nn:conv2d` input, kernel, and bias paths |
| Scalar arithmetic | `:op/add_d`, `:op/sub_d`, `:op/mul_d`, `:op/div_d` numerator path |
| Parameter leaf | `nn:value` |

Broadcast support is deliberately narrow:

- Tensor/scalar binary operations use the tensor module’s scalar paths.
- Rank-2 matrix plus row bias (`[N,D] + [D]` or `[N,D] + [1,D]`) is supported in
  tensor binary ops.
- `ParameterObject::addGrad` reduces leading broadcast dimensions before accumulation,
  so bias parameters receive same-shaped gradient buffers.

## 6. Tests And Toy Models

`test/plans/feat/modules/nn.plan.toml` covers:

- migrated `apply_gradients` linear MSE case;
- graph GIR checks for VJP-derived backward/update nodes;
- multi-parameter and bias-gradient accumulation;
- AutoEncoder with sigmoid encoder/decoder;
- fixed two-step Tiny RNN with tanh recurrence;
- Matrix Factorization using one-hot rows and parameter tables;
- fused `softmax_cross_entropy` finite-difference sanity check;
- helper-based Softmax Classifier using fused cross entropy;
- embedding repeated-index gradient sanity check;
- helper-based Embedding Matrix Factorization using true gather;
- negative out-of-range embedding index diagnostic;
- row-wise `softmax` finite-difference sanity check;
- helper-based Tiny Attention block with Q/K/V projections and softmax weights;
- negative non-rank-2 softmax diagnostic;
- Residual MLP helper calls with branch merge and skip parameters;
- Siamese Shared Encoder with two call sites sharing encoder parameters;
- GRU-lite fixed unroll with repeated gate parameter use;
- negative recursive-helper diagnostic for call-aware lowering;
- negative missing-VJP diagnostic for trainable `tensor:concat` paths;
- conv2d kernel finite-difference and bias-gradient sanity check;
- helper-based Tiny CNN using valid NCHW conv2d, tanh, reshape, and linear head;
- GIR checks that Tiny CNN autograd includes conv2d and conv2d backward helpers;
- negative conv2d channel-mismatch diagnostic;
- GIR checks that helper primitives appear inside `nn::autograd_sgd_step` and that
  helper `FUNC` nodes do not remain in the step graph;
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
camel test\cases\modules\nn\softmax_cross_entropy_sanity.cml
camel test\cases\modules\nn\softmax_classifier.cml std::macro std::nvm
camel test\cases\modules\nn\embedding_grad_sanity.cml
camel test\cases\modules\nn\embedding_mf.cml std::macro std::nvm
camel test\cases\modules\nn\softmax_grad_sanity.cml
camel test\cases\modules\nn\tiny_attention.cml std::macro std::nvm
camel test\cases\modules\nn\conv2d_grad_sanity.cml
camel test\cases\modules\nn\tiny_cnn.cml std::macro std::nvm
camel test\cases\modules\nn\tiny_cnn.cml std::macro std::gir
camel test\cases\modules\nn\residual_mlp.cml std::macro std::nvm
camel test\cases\modules\nn\siamese_shared_encoder.cml std::macro std::nvm
camel test\cases\modules\nn\gru_lite.cml std::macro std::nvm
camel test\cases\modules\nn\graph_autograd_multi_param_demo.cml std::macro std::gir
```

## 7. Known Limits

The implementation is still a small static-graph autograd skeleton:

- Helper-call lowering is static only. Dynamic dispatch, runtime function values,
  recursive helper graphs, `sync` helper bodies, macro helper bodies, and opaque
  external callees are rejected.
- Custom function VJP execution is not part of the helper-call path yet; helpers
  are inlined before primitive VJP traversal.
- `softmax_cross_entropy` currently supports rank-2 logits and rank-2 one-hot or
  distribution targets. Class-index labels, arbitrary softmax axes, gather, and a
  decomposed softmax/log VJP stack are still Phase 2 follow-up work.
- `embedding` currently supports rank-2 floating tables and rank-1 int64 index
  tensors. Its backward path emits dense table gradients with zero rows for
  unvisited indices; sparse optimizer state is not implemented.
- `tensor:softmax` is currently row-wise for rank-2 tensors only. There is no
  arbitrary axis argument, mask support, rank-3 batched attention, or decomposed
  `sum_axis` VJP yet.
- `nn:conv2d` currently supports floating rank-4 NCHW input, floating rank-4
  `[O,C,KH,KW]` kernels, rank-1 bias, stride 1, and valid padding only. The VJP
  emits dense input/kernel/bias gradients. There is no stride argument, padding
  mode, dilation, groups, NHWC layout, pooling, or optimized kernel backend.
- `tensor:concat` still has no builtin VJP. Trainable paths through concat fail
  explicitly instead of silently dropping gradients.
- Static alias keys merge repeated reads through the same model path, but not
  arbitrary runtime `Parameter` object aliases stored under different fields.
- No arrays/lists of `Parameter` are discovered.
- Broadcast semantics are not NumPy-complete.
- No optimizer state beyond SGD.
- No GPU, BLAS, checkpointing, dataloaders, full CNN stack, Transformer, or
  Diffusion support.
- `:op/div_d` only propagates through the numerator because current supported losses use
  non-trainable denominators.
