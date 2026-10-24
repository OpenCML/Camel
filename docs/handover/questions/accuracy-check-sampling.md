# `tensor_log_softmax_of_softmax`: the verdict depends on the draw

## What was measured (2026-10-24)

`check([tensor_log_softmax_of_softmax], 100, seed)` on the folded library, one entry in the array,
`std::nvm`:

| seed | 100 samples | first failing sample |
|---:|---|---|
| 3 | FAIL | `{inf, ...}` |
| 5 | ok, worst error ratio 1 | |
| 7 | FAIL | `{-607.572, 3.4e+38, -111. ...}` |
| 11 | ok, worst error ratio 1 | |
| 12 | ok, worst error ratio 1 | |
| 13 | ok, worst error ratio 1.57 | |
| 17 | FAIL | `{3.4e+38, 2, 377.894}` |
| 23 | ok, worst error ratio 1 | |

With the rule as the *third* entry of the array and seed 11, the case failed on
`(tensor<f32> [2, 3] {inf, 2.955, -2, 906.555, -0, -1}, 0): its error 1.09431e-06 exceeds the left
side's 6.54033e-07 (ratio 1.67317)`; at 1000 samples and seed 11 it fails as well. Every failing
sample holds a magnitude in the saturation region (`inf`, `3.4e+38`, `-607`).

## Why the draw moved

`check` seeds each entry's sampler with `seed * 1000003 + index` (`modules/rewrite/module.cpp:2431`),
so an entry's samples are decided by its position in the array as well as by the seed. The fold made
`float_log1p` and `float_expm1` generic, `check` reports a generic rule as unchecked and samples
nothing for it (the ruling of 2026-10-23 on
`generic-rule-check-surface-and-multi-parameter-instances`, §1), and the log-of-softmax entry
therefore moved from the fifth position to the third: same seed, different draws.

## Why this is not decided in this lane

The mechanism is the trade already measured in `docs/handover/reports/accuracy-softmax-trade.md`:
subtracting the maximum avoids overflow and underflow but grows the relative error of the elements
far below the maximum (the subtraction's own rounding, and `exp`'s argument reduction), so on a
saturated sample the fused `log_softmax` form can be the *less* accurate side as `check` measures
it. That report leaves two options open for the softmax rule (a) document the trade behind the
licence, or (b) wait for a subtract-max kernel; the log-of-softmax rule inherits the same question,
and neither the rule nor the measure belongs to this lane.

## What this lane did

Re-pointed `test/cases/modules/rules/check_accuracy_library.cml` to check the log-of-softmax rule in
a call of its own -- the same seed (11) and the same sample count (100) the case has always used --
and pinned the resulting line, `ok tensor_log_softmax_of_softmax: 100 samples, worst error ratio 1,
fired 8 of 8`. The two folded scalar laws are checked in a second call, where `check` reports them
as unchecked. The sample dependence is written in the case and in the plan entry rather than hidden;
a rule that must hold on every draw needs the author's decision above.