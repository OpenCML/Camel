# NN Autograd Review Log

This log records adversarial review results required by
`docs/technical/nn-autograd-targets.md` section 2.10.

## 2026-05-05 Phase 1 Review

Review scope:

- `modules/nn/operators.cpp`
- `modules/nn/vjp_registry.cpp`
- `modules/nn/operators.h`
- `modules/nn/vjp_registry.h`
- `test/cases/modules/nn/*`
- `test/plans/feat/modules/nn.plan.toml`
- `docs/technical/nn-autograd.md`

Blocking findings:

- None.

Non-blocking findings:

- Helper-call support is intentionally static and conservative. The lowering path
  rejects recursive, `sync`, macro, indirect, and opaque calls instead of trying
  partial dynamic behavior. This matches Phase 1 scope and is documented in
  `docs/technical/nn-autograd.md`.
- Custom function VJP metadata is still recorded by `nn.vjp`, but the call-aware
  training path inlines helper bodies before primitive VJP traversal. It does not
  invoke custom function VJP graphs. This is documented as a current limit.
- Parameter aliasing is keyed by static root object plus field/index path. This
  correctly covers repeated reads through the same model path across branches
  and time steps, but it does not canonicalize arbitrary runtime `Parameter`
  object aliases stored under different static fields. This is documented as a
  current limit.
- Only one unsupported-call form has an automated negative case in Phase 1:
  recursive helper calls. The implementation has explicit diagnostics for the
  other unsupported forms, and the target requires at least one negative
  diagnostic test for this phase.

Model-specific shortcut check:

- No model-specific lowering, VJP rule, hard-coded function name, or hard-coded
  model shape branch was found in the runtime implementation.
- The new model cases use ordinary helper/layer calls and existing primitive VJP
  rules.
- Shared-parameter behavior is tested by counting `nn:add_grad` and `nn:sgd`
  nodes in the generated `nn::autograd_sgd_step` graph for the Siamese case.

Diagnostics check:

- Recursive helper graph diagnostic names the unsupported helper graph.
- Indirect calls, opaque calls, `sync` helpers, macro helpers, surviving helper
  calls after lowering, and missing primitive VJP rules all have explicit error
  paths.
- Missing VJP diagnostics remain primitive-key based, which is appropriate for
  the lowered primitive tape.

Documentation check:

- `docs/technical/nn-autograd.md` records the actual call-lowering strategy,
  supported and unsupported helper-call scope, parameter aliasing invariant,
  Phase 1 model/test coverage, and current limits.
- The target document remains a source-of-truth roadmap; progress and review are
  recorded here and in the execution log.

Conclusion:

- Phase 1 implementation satisfies the roadmap acceptance criteria after normal
  verification.
- Remaining limitations are bounded, documented, and do not block Phase 1.

## 2026-05-05 Phase 2 Review

Review scope:

- `modules/nn/conv.cpp`
- `modules/nn/layers.cpp`
- `modules/nn/losses.cpp`
- `modules/nn/operators.cpp`
- `modules/nn/operators.h`
- `modules/nn/vjp_registry.cpp`
- `modules/tensor/nn_ops.cpp`
- `test/cases/modules/nn/*`
- `test/plans/feat/modules/nn.plan.toml`
- `docs/technical/nn-autograd.md`
- `docs/technical/nn-autograd-execution-log.md`

Blocking findings:

- None.

Long-term operator assessment:

- `nn:softmax_cross_entropy` is a deliberate fused primitive for the current
  rank-2 classification path. It avoids pretending Camel has complete axis,
  log, gather, and reduction VJPs. The fused API, label-as-non-trainable rule,
  and limits are documented.
- `nn:embedding` uses a real gather forward path and a dense table-gradient VJP.
  Repeated indices accumulate into the same row, unvisited rows stay zero, and
  out-of-range indices have a negative test. Sparse optimizer state is outside
  Phase 2 and documented.
- `tensor:softmax` is row-wise rank-2 only. Tiny Attention uses that exact
  supported subset, and non-rank-2 input has a negative diagnostic test.
- `nn:conv2d` is a narrow CPU NCHW, stride-1, valid-padding primitive with dense
  input/kernel/bias gradients. The layout, rank, bias, and unsupported stride,
  padding, dilation, groups, NHWC, and backend-performance scope are documented.

Model-path assessment:

- Tiny CNN, Softmax Classifier, Embedding MF, and Tiny Attention all train
  through helper/layer functions and the normal `nn.apply_gradients` pipeline.
- The implementation does not add model-specific training templates or bypass
  the call-aware autograd path.
- Grep review found model names only in tests and docs, not in runtime/VJP
  implementation branches.
- Tiny CNN uses MSE to isolate conv2d backward coverage. Softmax Classifier
  separately covers the classification loss path, so this is not a model-specific
  shortcut.

Diagnostics and coverage:

- Operator-level tests cover finite-difference or equivalent gradient sanity for
  softmax cross entropy, embedding repeated-index accumulation, tensor softmax,
  and conv2d kernel/bias gradients.
- GIR tests confirm fused backward helpers appear in `nn::autograd_sgd_step` for
  softmax classification, embedding, attention softmax, and Tiny CNN conv2d.
- Negative tests cover embedding index bounds, softmax rank mismatch, conv2d
  channel mismatch, recursive helper calls, and missing builtin VJP through
  trainable `tensor:concat`.

Non-blocking findings:

- Shape diagnostics are representative rather than exhaustive. There are no
  separate automated cases yet for every conv2d bias/kernel-spatial mismatch or
  softmax-cross-entropy target-shape mismatch, but those checks have explicit
  runtime diagnostics and documented limits.
- Attention mask support is not exposed, so there is no mask-unsupported
  negative test yet. Mask semantics should get their own diagnostic tests if a
  mask parameter is added later.
- Fused primitives are intentionally small public surfaces, not compatibility
  shims. They should be expanded or decomposed only when Camel adds first-class
  axis/gather/reduction VJP semantics.

Documentation and comments:

- `docs/technical/nn-autograd.md` records the public API, VJP coverage, model
  coverage, and limits for softmax cross entropy, embedding, tensor softmax,
  conv2d, and explicit missing-VJP boundaries.
- New operator files include responsibility comments and document the core shape
  constraints. The conv2d implementation comments focus on layout and dense
  backward scope rather than restating loop mechanics.

Conclusion:

- Phase 2 satisfies the roadmap acceptance criteria after normal verification.
- No blocking temporary design, model-specific path, or hard-to-delete shim was
  found.
- Remaining limitations are bounded to the documented small-operator subsets and
  should be extended with focused negative tests when those APIs grow.
