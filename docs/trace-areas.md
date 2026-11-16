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