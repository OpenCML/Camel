# The verdict table's surface (H247, slice S0): two readings and one finding

Lane H247 (`handover/h420-rule-verification`). The rulings of 2026-10-24
(`docs/handover/reviews/questions-2026-10-23.md`, sections `### verification-verdict-surface`,
`### verification-licence-to-precondition` and `### accuracy-check-sampling`) ask for the verdict
surface to be published by `check` itself. Slice S0 lands the vocabulary and the table and measures
it (`test/cases/modules/rules/verify_report.cml`, `verify_report_row.cml`,
`verify_report_instance.cml`, and the updated entries of `test/plans/feat/modules/rules.plan.toml`,
131 tests, all green). Three readings had to be taken to render the ruled rows, and one rule of the
library turns out to be `refuted` once the seed follows the rule's identity. All four are recorded
here for the coordinator.

## Taken as written

* The verdict words are `proved`, `refuted` and `sampled`, and the fourth word the check used to
  print is gone from the report (`verification-verdict-surface`, 2). A rule nothing could be
  examined on is `sampled` with its reason (licence 3, option A).
* The proposition follows the permission the rule asks for (licence 2): `equality`,
  `real equality + IEEE on special values` (`reassociate`, `contract`) or `error bound`
  (`refine_accuracy`, `change_precision`).
* Evidence is `solver` / `samples <n>` / `counterexample` / `none`, and `proved` is never published
  by this build: there is no solver, and comparing samples is not establishing a proposition.
* A rule's samples come from a stream derived from the rule's identity -- its name and its
  instance's type tuple -- mixed with the call's seed (`accuracy-check-sampling`, 1), not from its
  place in the array the caller passed.

## Reading 1: `rule` and `instance` are two columns, so the instance name is split

The engine names an instance of a generic law `int_add_zero<int>` -- one string, the type tuple
included, and that is the name `explain` prints. The ruled row carries `rule` *and* `instance`, so
the row splits the name: `rule` is `int_add_zero`, `instance` is `int`, and the fixed rendering
rejoins them, so a reader of the report still sees the engine's name.

Measured (`verify_report_instance.cml`): the two instances of `stdlib/rules.cml`'s integer family
print as `row 0 | int_add_zero | int | sampled` and `row 1 | int_add_zero | i32 | sampled`.

The alternative reading -- `rule` is the engine's instantiated name and `instance` is the tuple --
writes the tuple twice, and for the law of two type parameters it does so visibly:
`pair_sum_self<int, int><int, int>`. That is what the first version of this slice printed, and it is
why the split was taken. If the coordinator wants the literal engine name in the `rule` column, it
is the removal of the suffix strip at the head of `checkOneRule` (`modules/rewrite/module.cpp`) and
the assertions that pin the two columns.

## Reading 2: the reason is a column of the row, not a second line

Licence 3, option A says the `sampled` verdict and "nothing was examined" are written apart and not
mixed. This is rendered as one row whose `reason` column is always written: a rule with no numeric
bound prints

    sampled anywhere: proposition equality, evidence none, reason no numeric bound

`verification-verdict-surface`, 2 and 4 ask for one row per rule or instance, so a physically second
line would make the row count disagree with the number of rules the check was handed. A reader that
only wants the honest downgrade reads `evidence none` and the `reason` column.

## Finding: one rule of `stdlib/rules.cml` is `refuted` under the identity seed

`tensor_f32_combine_mixed` is `refuted` on the rewritten left side:

    refuted tensor_f32_combine_mixed: proposition real equality + IEEE on special values, evidence
    counterexample, the rewritten left side differs at (tensor<f32> [2, 2, 2] {-0.5, 76.488,
    -79.654, 1e+30, -349.024, 1e-30, -669.542, 1e-40}, 48, -47): ... (bound 11 ulps, 0 relative,
    scale 2401)

The rule asks for `reassociate, no_infs, no_signed_zeros`, so its guard is `combineMixedSign`
(opposite signs, nonzero, nonzero sum). At `c1 = 48`, `c2 = -47` and an element of `1e+30`, the
cancellation `x*48 + x*(-47)` loses five digits of that element: `1.0000041e+30` against `1e+30`,
which is tens of ulps of the result against the eleven the reassociate bound allows. The guard's own
comment says the opposite-sign case is the one cancellation makes unsafe, and assumes the
permissions cover it.

Before this slice the rule was `ok`, because the seed followed the rule's *place in the array the
case passed*; with the seed following the rule's identity (`accuracy-check-sampling`, 1) the draw
that exposes it is reached. The verdict is pinned as `refuted` in
`rules.library_verdicts_are_exactly_these`, and `verify/check_covers_every_rule.mjs` names it as the
one known `refuted` rule, so the CI reading of `refuted` (`verification-verdict-surface`, 1 and 3:
`refuted` hard-fails) still fails the run for any other rule.

Decision needed: whether the library's guard or the reassociate bound is wrong. The check's tolerance
is not this lane's to move -- moving it to hide the finding is what licence 1 of
`verification-licence-to-precondition` forbids -- and the fix belongs to whoever owns
`stdlib/rules.cml`.

## The sampling this slice changed, and what it measured

The stream of a rule is `FNV-1a` of its identity mixed with the call's seed
(`sampleStreamOf(seed, "name<tuple>")`, `modules/rewrite/module.cpp`), so a rule draws the same
samples in any call and in any arrangement. Measured on the host, `rules.plan.toml`, 131 tests, all
green after the change:

* `accuracy_check_compares_error`: `accurate_log1p` worst error ratio 0.193, `softmax_axis2` 0,
  `softmax_vector` and `softmax_matrix` 1; `worse_log1p` is `refuted` at `(0.1)` with
  `7.32014e-16` against `3.98143e-18` (ratio 1.64835).
* `accuracy_library_passes_the_check`: `float_log1p<f32>` 0.201, `float_log1p<float>` 0.269,
  `float_expm1<f32>` 0.201, `float_expm1<float>` 0.233, and `tensor_log_softmax_of_softmax` worst
  error ratio 1 with `fired 8 of 8, shapes [2, 2, 2] [2, 3] [3], axes -1 0 1 2`. Under the new 7.2
  definition (right error not worse, bound held, strictly better somewhere) that is `sampled` and
  not `refuted`; ruling `accuracy-check-sampling`, 2 asked for `proved` or a genuine counterexample,
  and `proved` needs the solver this build does not have.
* The two floating instances at `bf16` and `f16` are `sampled` with `evidence none` and the reason
  the sampler draws no such value, exactly as before the seed change: the seed moved, the drawable
  set did not.
* `tensor_f32_combine_mixed` is `refuted` under the new draw and was `ok` under the old one (the
  finding above).

## Not in S0

* `program: Program? = null` is still not declared (see `check-program-parameter.md`): S0's `check`
  is the three-argument form.
* The solver is a build unit of its own asked through a registered hook
  (`verification-verdict-surface`, 2); with no solver in this build, `proved` is unreachable. That is
  the slice the proposition vocabulary is waiting for.
* `check_text` writes the rendering a caller can hold as a value; the generated report file beside
  the rules library (`verification-verdict-surface`, 1) and its `--check` mode are later slices.

## A base discrepancy this lane ran into (not a question about this surface)

The ruling that `when` is written *before* the body it guards is **not** in this branch's base
(`origin/develop` = `2c926f031`): `git grep WhenAfterBody origin/develop -- src/parse` finds nothing,
the merge `699e59b31` ("61 rewrites across 19 Camel sources") is not an ancestor of `origin/develop`,
and `stdlib/rules.cml` here writes `when` after the body. Anyone writing a new `when` rule against
`origin/develop` must therefore use the old position, and a lane that branches after the landing has
to rewrite those cases. It is recorded here because it decides how a slice's own cases are written.