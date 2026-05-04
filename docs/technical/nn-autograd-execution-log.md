# NN Autograd Execution Log

This log records implementation progress for `docs/technical/nn-autograd-targets.md`
without changing the target document itself.

## 2026-05-05 Phase 1: Call-Aware Autograd

Target references: sections 2.1 through 2.10 of
`docs/technical/nn-autograd-targets.md`.

Implemented:

- `nn.apply_gradients` now validates the output-reachable static helper-call
  closure before VJP synthesis.
- Static helper/layer `FUNC` nodes are lowered through `RuntimeGraphDraftSession`
  and `inlineCallableInDraft` until the trainable step graph contains primitive
  tensor/scalar nodes instead of helper calls.
- Unsupported helper forms now have explicit diagnostics for recursive helper
  graphs, `sync` helpers, macro helpers, indirect `CALL` nodes, opaque callees,
  surviving helper calls after lowering, and missing primitive VJP rules.
- Parameter gradient accumulation now merges repeated helper-call reads by static
  root object plus field/index access path. This collapses shared encoder and
  repeated unroll reads to one update per model parameter path.
- Added Phase 1 model cases:
  `test/cases/modules/nn/residual_mlp.cml`,
  `test/cases/modules/nn/siamese_shared_encoder.cml`, and
  `test/cases/modules/nn/gru_lite.cml`.
- Added recursive unsupported-call diagnostic case:
  `test/cases/modules/nn/autograd_recursive_helper_error.cml`.
- Updated `test/plans/feat/modules/nn.plan.toml` with behavior checks, GIR
  structure checks, shared-parameter update counts, and the negative diagnostic
  verifier.
- Updated `docs/technical/nn-autograd.md` with the implemented lowering strategy,
  supported/unsupported scope, diagnostics, aliasing invariant, tests, and known
  limits.

Manual raw-run evidence before verifier finalization:

```powershell
camel test\cases\modules\nn\residual_mlp.cml std::macro std::nvm
```

Observed metrics: before `1.5843181610107422`, step
`1.5843181610107422`, after `0.5600603818893433`.

```powershell
camel test\cases\modules\nn\siamese_shared_encoder.cml std::macro std::nvm
```

Observed metrics: before `0.01909344457089901`, after
`0.01296148356050253`.

```powershell
camel test\cases\modules\nn\gru_lite.cml std::macro std::nvm
```

Observed metrics: before `0.9544516801834106`, after
`0.7635006904602051`.

```powershell
camel test\cases\modules\nn\autograd_recursive_helper_error.cml std::macro std::nvm
```

Observed exit: nonzero, with diagnostic substring
`apply_gradients does not support recursive helper call graph 'recursive_pred'`.

GIR update-count checks used during implementation:

- `tiny_rnn`: `nn:sgd` count `5`.
- `graph_autograd_multi_param_demo`: `nn:sgd` count `2`.
- `residual_mlp`: `nn:sgd` count `6`.
- `siamese_shared_encoder`: `nn:sgd` count `2`.
- `gru_lite`: `nn:sgd` count `8`.

Final verification:

```powershell
npm run build
node scripts/test.js test\plans\feat\modules\nn.plan.toml
npm run test
```

Results:

- Build passed.
- Focused NN plan passed: 22 total, 22 pass.
- Default developer test set passed: 128 total, 128 pass.

## 2026-05-05 Phase 2 Partial: Fused Classifier Loss

Target references: sections 3.2, 3.3, 3.5, 3.8, 3.9, and 3.10 of
`docs/technical/nn-autograd-targets.md`.

Implemented:

- Added fused `nn.softmax_cross_entropy(logits, target)` for stable rank-2
  `[batch, classes]` classification loss with one-hot or distribution targets.
- Added `nn.softmax_cross_entropy_grad(logits, target, dy)` as the logits-gradient
  primitive used by the VJP rule and by the operator sanity case.
- Registered the builtin VJP for `nn:softmax_cross_entropy`, propagating only to
  logits. Targets are treated as labels, not trainable tensors.
- Added `test/cases/modules/nn/softmax_cross_entropy_sanity.cml`, which compares
  the fused logits gradient against a tiny finite-difference estimate.
- Added `test/cases/modules/nn/softmax_classifier.cml`, a helper-based classifier
  trained through `apply_gradients` and the fused classification loss.
- Updated `test/plans/feat/modules/nn.plan.toml` with behavior and GIR checks for
  the fused loss, fused logits gradient, and classifier parameter updates.
- Updated `docs/technical/nn-autograd.md` with the new API, VJP coverage, tests,
  and current limits.

Manual raw-run evidence before verifier use:

```powershell
camel test\cases\modules\nn\softmax_cross_entropy_sanity.cml
```

Observed metrics: loss `0.554355242566137`, gradient element
`-0.4255574941635132`, finite difference `-0.4255583505785031`.

```powershell
camel test\cases\modules\nn\softmax_classifier.cml std::macro std::nvm
```

Observed metrics: before `0.6931471805599453`, step
`0.6931471805599453`, after `0.5759394231408829`.

Focused verification:

```powershell
node scripts/test.js test\plans\feat\modules\nn.plan.toml
```

Result: 25 total, 25 pass.

Phase 2 status after this slice:

- Classification fused loss and one helper-based classifier model are covered.
- Remaining Phase 2 work includes CNN/`conv2d`, true embedding/gather, tiny
  attention/softmax path, additional operator-level tests, full Phase 2 docs, and
  the Phase 2 adversarial review.

## 2026-05-05 Phase 2 Partial: Embedding Gather

Target references: sections 3.2, 3.3, 3.6, 3.8, 3.9, and 3.10 of
`docs/technical/nn-autograd-targets.md`.

Implemented:

- Added `nn.embedding(table, indices)` for rank-2 floating tables and rank-1
  int64 index tensors.
- Added `nn.embedding_table_grad(table, indices, dy)` as a dense table-gradient
  primitive. Repeated indices accumulate into the same row; unvisited rows remain
  zero.
- Registered the builtin VJP for `nn:embedding`, propagating only to the table.
  Indices are treated as non-differentiable lookup data.
- Added `test/cases/modules/nn/embedding_grad_sanity.cml` to check repeated-index
  accumulation and unvisited-row zero gradients.
- Added `test/cases/modules/nn/embedding_mf.cml`, a helper-based embedding matrix
  factorization model trained through `apply_gradients`.
- Added `test/cases/modules/nn/embedding_index_error.cml` for out-of-range index
  diagnostics.
- Updated `test/plans/feat/modules/nn.plan.toml` with behavior, GIR, and negative
  diagnostic checks.
- Updated `docs/technical/nn-autograd.md` with the new API, VJP coverage, tests,
  and current limits.

Manual raw-run evidence before verifier use:

```powershell
camel test\cases\modules\nn\embedding_grad_sanity.cml
```

Observed metrics: gathered sum `2.5000000596046448`, row0 gradient sum `0`,
row1 gradient sum `7`, row2 gradient sum `9`.

```powershell
camel test\cases\modules\nn\embedding_mf.cml std::macro std::nvm
```

Observed metrics: before `0.8463999895811081`, step
`0.8463999895811081`, after `0.5865295435403175`.

```powershell
camel test\cases\modules\nn\embedding_index_error.cml
```

Observed exit: nonzero, with diagnostic substring `embedding index out of range`.

Focused verification:

```powershell
node scripts/test.js test\plans\feat\modules\nn.plan.toml
```

Result: 29 total, 29 pass.

Phase 2 status after this slice:

- Classification and embedding models are covered with operator-level sanity
  tests and GIR checks.
- Remaining Phase 2 work includes CNN/`conv2d`, tiny attention/softmax path,
  broader shape/rank diagnostics, full Phase 2 docs, and the Phase 2 adversarial
  review.

## 2026-05-05 Phase 2 Partial: Tiny Attention Softmax

Target references: sections 3.2, 3.3, 3.7, 3.8, 3.9, and 3.10 of
`docs/technical/nn-autograd-targets.md`.

Implemented:

- Added row-wise rank-2 `tensor.softmax(t)` and fused `tensor.softmax_grad(output, dy)`.
- Registered the builtin VJP for `tensor:softmax`.
- Added `test/cases/modules/nn/softmax_grad_sanity.cml`, which compares one
  softmax VJP component against a finite-difference estimate.
- Added `test/cases/modules/nn/tiny_attention.cml`, a helper-based single-head
  attention block with Q/K/V projections, row-wise softmax weights, weighted sum,
  and output projection.
- Added `test/cases/modules/nn/softmax_rank_error.cml` for non-rank-2 softmax
  diagnostics.
- Updated `test/plans/feat/modules/nn.plan.toml` with behavior, GIR, finite
  difference, and negative diagnostic checks.
- Updated `docs/technical/nn-autograd.md` with the new tensor API, VJP coverage,
  tests, and current limits.

Manual raw-run evidence before verifier use:

```powershell
camel test\cases\modules\nn\softmax_grad_sanity.cml
```

Observed metrics: probability sum `0.9999999701976776`, gradient element
`0.36668744683265686`, finite difference `0.3666579723358153`.

```powershell
camel test\cases\modules\nn\tiny_attention.cml std::macro std::nvm
```

Observed metrics: before `0.4615999920642935`, step
`0.4615999920642935`, after `0.429342576302588`.

```powershell
camel test\cases\modules\nn\softmax_rank_error.cml
```

Observed exit: nonzero, with diagnostic substring
`softmax currently requires a rank-2 tensor`.

Focused verification:

```powershell
node scripts/test.js test\plans\feat\modules\nn.plan.toml
```

Result: 33 total, 33 pass.

Phase 2 status after this slice:

- Softmax Classifier, Embedding Model, and Tiny Attention Block now pass
  automated model tests, each through helper/layer functions and `apply_gradients`.
- Remaining explicit target items include CNN/`conv2d`, Tiny CNN, final Phase 2
  documentation pass, and the Phase 2 adversarial review.

## 2026-05-05 Phase 2 Partial: Conv2d And Tiny CNN

Target references: sections 3.2, 3.3, 3.4, 3.8, 3.9, and 3.10 of
`docs/technical/nn-autograd-targets.md`.

Implemented:

- Added `nn.conv2d(input, kernel, bias)` for floating NCHW input
  `[N,C,H,W]`, floating kernels `[O,C,KH,KW]`, rank-1 bias `[O]`, stride 1,
  and valid padding.
- Added dense backward helpers `nn.conv2d_input_grad`,
  `nn.conv2d_kernel_grad`, and `nn.conv2d_bias_grad`.
- Registered the builtin VJP for `nn:conv2d`, propagating to input, kernel,
  and bias. The Tiny CNN case trains kernel and bias parameters through the
  ordinary `apply_gradients` path.
- Added `test/cases/modules/nn/conv2d_grad_sanity.cml`, which compares one
  kernel-gradient component against a tiny finite-difference estimate and checks
  the bias-gradient sum.
- Added `test/cases/modules/nn/tiny_cnn.cml`, a helper-based valid-convolution
  CNN with `conv2d`, `tanh`, `reshape`, a linear head, and MSE loss.
- Added `test/cases/modules/nn/conv2d_shape_error.cml` for channel-mismatch
  diagnostics.
- Updated `test/plans/feat/modules/nn.plan.toml` with behavior, GIR, finite
  difference, update-count, and negative diagnostic checks.
- Updated `docs/technical/nn-autograd.md` with the conv2d API, VJP coverage,
  tests, and current limits.

Manual raw-run evidence before verifier use:

```powershell
camel test\cases\modules\nn\conv2d_grad_sanity.cml
```

Observed metrics: kernel gradient element `12`, finite difference
`12.00008392333984`, bias-gradient sum `4`.

```powershell
camel test\cases\modules\nn\tiny_cnn.cml std::macro std::nvm
```

Observed metrics: before `0.960530161857605`, step `0.960530161857605`,
after `0.6478729248046875`; kernel, bias, head parameters and their gradients
were printed.

```powershell
camel test\cases\modules\nn\tiny_cnn.cml std::macro std::gir
```

Observed GIR contained `nn:conv2d`, `nn:conv2d_input_grad`,
`nn:conv2d_kernel_grad`, `nn:conv2d_bias_grad`, and three `nn:sgd` update
nodes inside `nn::autograd_sgd_step`.

```powershell
camel test\cases\modules\nn\conv2d_shape_error.cml
```

Observed exit: nonzero, with diagnostic substring
`conv2d input channels must match kernel channels`.

Focused verification:

```powershell
node scripts/test.js test\plans\feat\modules\nn.plan.toml
```

Result: 37 total, 37 pass.

Phase 2 status after this slice:

- Tiny CNN, Softmax Classifier, Embedding Model, and Tiny Attention Block all
  pass automated model tests through helper/layer functions and
  `apply_gradients`.
- Operator-level sanity and negative diagnostic coverage now exists for
  softmax cross entropy, embedding, tensor softmax, and conv2d.
- Remaining explicit target items are the full default verification run, the
  final Phase 2 adversarial review, and the completion audit.

## 2026-05-05 Phase 2 Completion Verification

Target references: sections 3.8, 3.10, 3.11, and 6 of
`docs/technical/nn-autograd-targets.md`.

Final additions before completion:

- Added `test/cases/modules/nn/autograd_missing_vjp_concat_error.cml` to lock
  the missing-VJP diagnostic for trainable paths through unsupported operators.
- Updated `test/plans/feat/modules/nn.plan.toml` to assert that diagnostic:
  `No builtin VJP rule registered for 'tensor:concat'`.
- Tightened the Tiny CNN GIR verifier to assert `nn:conv2d_input_grad` in
  addition to the kernel and bias gradient helpers.
- Updated `docs/technical/nn-autograd.md` to document `tensor:concat` as an
  explicit no-VJP boundary.

Manual raw-run evidence before verifier use:

```powershell
camel test\cases\modules\nn\autograd_missing_vjp_concat_error.cml std::macro std::nvm
```

Observed exit: nonzero, with diagnostic substring
`No builtin VJP rule registered for 'tensor:concat'`.

Final verification:

```powershell
npm run build
node scripts/test.js test\plans\feat\modules\nn.plan.toml
npm run test
```

Results:

- Build passed.
- Focused NN plan passed: 38 total, 38 pass.
- Default developer test set passed: 144 total, 144 pass.

Completion audit:

- Phase 1 call-aware helper/layer models and diagnostics still pass in the same
  NN plan.
- Phase 2 models now cover Tiny CNN, Softmax Classifier, Embedding Matrix
  Factorization, and Tiny Attention. All use helper/layer functions and
  `apply_gradients`.
- Operator-level sanity coverage exists for softmax cross entropy, embedding,
  tensor softmax, and conv2d.
- Negative diagnostics now cover recursive helper calls, embedding index
  bounds, softmax rank limits, conv2d channel mismatch, and missing builtin VJP
  on a trainable `tensor:concat` path.
- The Phase 2 adversarial review is recorded in
  `docs/technical/nn-autograd-review-log.md`.
