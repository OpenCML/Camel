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
