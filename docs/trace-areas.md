# Which area prints which reading

A reading is printed only when the run asks for its area (`--trace <area>`,
`include/camel/core/trace.h`). A plan entry that pins a reading therefore has to name that area in
its `args`; an entry that does not is red for a reason that has nothing to do with what it measures.

This table is the one place that says which area owns which text. It is **read from the source**, not
tried against the tests: every row names the site that writes the line and the channel it writes
through, so a row can be checked again after any change.

## The rows

| The assertion names this | Area | Written by | Channel |
|---|---|---|---|
| `REINTERP ` | `transforms` | `modules/transforms/vmap_operator.cpp#vmapKernel` -> `modules/transforms/vmap.cpp#buildVmapGraph` | `readings()` |
| `kinds=`, `asked=`, `found=`, `missing=`, `rules=` | `transforms` | the same line as `REINTERP` (`modules/transforms/vmap.cpp#askedFoundMissing` builds the fields) | `readings()` |
| `TRANSPOSE ` | `transforms` | `modules/transforms/transpose_operator.cpp#transposeKernel`, `modules/transforms/transpose.cpp` | `readings()` |
| `SPLIT `, `JVP ` | `transforms` | `modules/transforms/transpose.cpp` (the split/jvp half of the same walk) | `readings()` |
| `in_recompute=`, `out_recompute=` | `transforms` | `modules/transforms/remat_operator.cpp` | `readings()` |
| `PLAN_REF_SITES ` | `plan` | `src/passes/sched/fastvm/compile.cpp#compileRuntimeGraph` | `trace(TraceArea::Plan)` |
| `effect containment: checked` | `effects` | `src/execute/pass.cpp#passBoundaryViolation` | `trace(TraceArea::Effects)` |
| `[DBG-FOLD] `, `[DBG-W] ` | `fold` | `src/compile/gir/builder_expr.cpp#FoldCountsReport` and the fold's own `[DBG-W]` sites | `trace(TraceArea::Fold)` |
| `READING_REFUSAL ` | none needed | `modules/transforms/refusal_readings.h#noteRefusal` | `verdicts()` -- **always printed** |

## Two rules that follow from the table

1. **A refusal is not a reading.** `verdicts()` is unconditional `std::cerr`: what a transform
   refused it says on every run, because a reader who never heard of `--trace` still has to be able
   to read why their derivative was refused. An entry that pins only `READING_REFUSAL` needs no area.
2. **One entry, one row per area it asserts.** An entry that pins `REINTERP` and `TRANSPOSE` needs
   `--trace transforms` once, not twice; an entry that pins `REINTERP` and `PLAN_REF_SITES` needs
   `--trace transforms,plan`.

## How the list of entries was obtained

Not by searching the plans for these strings. The areas were turned off and the entries that reddened
were read off the run: that is the only list that cannot miss an entry whose assertion names a
fragment (a criterion may pin `rhs=sum_stated_t`, which no search for `TRANSPOSE` would find). This
table then says which area each of those entries needs.

`test/plans/feat/compile` (2609 tests) went from 98 red to 12 with the entries in that layer asking
for their area; the 12 that remain are the GPU observation's (report-only, listed in
`internal/tools/gpu-plans.exceptions.txt`) and the `stream.generator` entries that belong to another
lane.
## The plans no gate runs

`test/tools/gate-targets.mjs` names the directories a gate measures. Everything else is a plan file
that exists and is never run by a gate, so no run ever reads it -- and a reading that is off by
default cannot redden an entry nobody runs. That is where this migration's remaining gap lives: the
entries below are wrong in exactly the way the compile layer's were, and no gate says so.

Measured by the merge lane's coverage scan on `b488758d0` (394 plan files, 6644 entries in the tree;
a gate runs 6122 of them):

| Not in a gate target | Files | Entries |
|---|---|---|
| `test/plans/feat` (top level) | 10 | 167 |
| `test/plans/feat/transforms` | 3 | 34 |
| `test/plans/feat/para` | 3 | 62 |
| `test/plans/feat/format` | 3 | 31 |
| `test/plans/feat/runtime` | 4 | 9 |
| `test/plans/feat/gate` | 3 | 3 |
| `test/plans/feat/bindings` | 1 | 2 |
| `test/plans/feat/docs` | 1 | 2 |
| `test/plans/perf` | 22 | 69 |
| `test/plans/probe` | 4 | 15 |
| `test/plans/debug` | 2 | 3 |
| `test/plans/gpu` | 6 | 125 |
| **Total** | **62** | **522** |

**522 entries -- 7.9% of the tree -- have never appeared in a red or a green we looked at.** On that
day 27 of them were red, all in `test/plans/feat/transforms` (48 tests: 13 pass, 27 fail, 8 known):
the same missing `--trace <area>` as the 96 in the compile layer, invisible for the same reason -- the
directory is not in a gate target, so the switch's migration could not reach it and nothing said so.

The 27 were fixed by asking for the area (20 entries in `split.plan.toml` and `floatgate.plan.toml`);
that directory now runs 48 tests with 40 pass, 0 fail and 8 known, so what the fix showed is that the
only thing wrong there was the expectation half. **The other 495 are still unmeasured.** Whether they
are red is not known, and this file does not claim they are green: a plan that no gate runs is neither.

This is recorded rather than repaired: putting a directory into a gate target is a decision about what
the project measures, not a fix for one reading's migration.