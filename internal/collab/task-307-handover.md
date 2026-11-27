# task 307 handover: `constas` on a body-less declaration (principle 464)

Branch `lane/native-constas`, tree `D:\Projects\Camel-wt\reuse`. Base `58c6443893`; commit `cf88b95d9b`.

## Done (committed)

- The refusal `ConstasInNativeDeclaration` is gone: emission point (`src/compile/src/gir/builder.cpp`, the
  block that threw on `funcType->hasConstas()`), message block (`messages/semantic.cpp`), enumerator
  (`messages/semantic.h`), `registry.tsv` row, `examples.tsv` row, generated `docs/diagnostics.md`
  (regenerated with `node scripts/diagnostics-catalog.mjs --write`), the case that pinned it
  (`test/cases/compile/constas/constas_native.cml`) and its plan entry.
- `src/stdlib/prelude.cml`: `:op/access` now `with <constas at: int>`, `:op/access_key` now
  `with <constas key: string>`.
- Control reading: a body-less declaration with a `constas` parameter compiles and a literal position
  runs (`[PASS] access_literal`, `binary built 2026-11-28T03:45:28`); the 20 entries of
  `feat/compile/constas` stay green.

## Measured, and where it stops

`takeConstasValues` (`src/compile/src/gir/builder_generics.cpp:1069`) is the only place that refuses a
`with` argument that is not a compile-time constant (`ConstasValueNotConstant` at `:1104`). It is called
from `instantiateForCallBinding` (`:1663`, `signature = tmpl->funcType`) and `builder_rest.cpp:183` --
the path of a *function value's* call. A call to a native operator does not go through it: it resolves
through `kernel->group->resolveForCall(...)` with `withInputTypes` (`builder_call.cpp:1321`), and nothing
on that path asks whether a `with` argument is a constant.

The declaration's signature does reach the resolver: `Context::claimBuiltinSignature` stores it
(`context.cpp:756`) and `checkPreludeDeclarations` grafts it onto the rule
(`rule->graftDeclaredSignature`, `context.cpp:816`), which keeps its `constas` indices
(`FunctionType` key includes `constasIndices`, `func.cpp:206-207`). So the types agree; what is missing
is the check at the operator call.

## Next step (concrete)

At the operator call, where the `with` arguments are still expressions, consult the declared signature's
`withIsConstasAt(i)` and refuse a non-constant with the existing diagnostic and wording -- the place is
`src/compile/src/gir/builder_call.cpp:1787` or `builder_operators.cpp:1525`, both of which already hold
`rule->declaredSignature()`. Reuse `ConstasValueNotConstant` (its wording already says "the value selects
the instance of the declaration"); do not add a second diagnostic.

Then the two cases already written but uncommitted become the reading:

- `test/cases/compile/access_const/at_runtime.cml` -- the position is a run-time value
- `test/cases/compile/access_const/key_runtime.cml` -- the key is a run-time value

Both are accepted today (empty stderr, exit 0). They belong in
`test/plans/feat/compile/access_const.plan.toml` as negative entries asserting
`SemanticDiag::ConstasValueNotConstant`; that plan currently carries only the control entry
(`access_literal`).

## Conflicts

`func.h`/`func.cpp`: another branch is renaming `withConstIndices_` to `withConstasIndices_`
(`D-1128-19`). This branch does not touch that field. Rebase before writing there; if the rename has not
landed, keep the old name and do not rename it here.

## Remaining after that

`nn` convolution/pooling `stride`/`padding`/kernel size to `constas` (principle 464 names them); the
adjacent plans (`feat/compile`, `feat/opt`, `feat/modules`) have not been run on this branch.