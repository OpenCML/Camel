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
