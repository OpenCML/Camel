# Which area prints which reading

A reading is printed only when the run asks for its area (`--trace <area>`,
`kernel/include/camel/core/trace.h`). A plan entry that pins a reading therefore has to name that area in
its `args`; an entry that does not is red for a reason that has nothing to do with what it measures.

This table is the one place that says which area owns which text. It is **read from the source**, not
tried against the tests: every row names the site that writes the line and the channel it writes
through, so a row can be checked again after any change.

## The rows

| The assertion names this | Area | Written by | Channel |
|---|---|---|---|
| `REINTERP ` | `transforms` | `lib/transforms/vmap_operator.cpp#vmapKernel` -> `lib/transforms/vmap.cpp#buildVmapGraph` | `readings()` |
| `kinds=`, `asked=`, `found=`, `missing=`, `rules=` | `transforms` | the same line as `REINTERP` (`lib/transforms/vmap.cpp#askedFoundMissing` builds the fields) | `readings()` |
| `TRANSPOSE ` | `transforms` | `lib/transforms/transpose_operator.cpp#transposeKernel`, `lib/transforms/transpose/walk.cpp` | `readings()` |
| `SPLIT `, `JVP ` | `transforms` | `lib/transforms/transpose/walk.cpp` (the split/jvp half of the same walk) | `readings()` |
| `in_recompute=`, `out_recompute=` | `transforms` | `lib/transforms/remat_operator.cpp` | `readings()` |
| `PLAN_REF_SITES ` | `plan` | `lib/std/fvm/compile.cpp#compileRuntimeGraph` | `trace(TraceArea::Plan)` |
| `effect containment: checked` | `effects` | `kernel/execute/pass.cpp#passBoundaryViolation` | `trace(TraceArea::Effects)` |
| `[DBG-FOLD] `, `[DBG-W] ` | `fold` | `compile/src/gir/builder_variables.cpp#FoldCountsReport` and the fold's own `[DBG-W]` sites | `trace(TraceArea::Fold)` |
| `READING_REFUSAL ` | none needed | `lib/transforms/refusal_readings.h#noteRefusal` | `verdicts()` -- **always printed** |

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
## The runs we type, and the ones that pick themselves

**This section used to say that `test/tools/gate-targets.mjs` names the directories a gate measures.
No such file has ever existed in this repository** (`git log --all -- test/tools/gate-targets.mjs` is
empty), and a sentence pointing at a missing file is how an account turns into a premise. What decides
whether an entry runs is three things, and only one of them lives in the plans:

1. **The entry's tier**, by `test/tools/run-tests.mjs#testTier` -- the `tier` written on the test, else
   the plan's `tier`, else **fast** -- and `selectTier`, which keeps the entries whose tier is the layer
   asked for. A **directory** target selects test by test and follows `--tier`; a **plan file** named as
   the target runs in full whatever `--tier` says.
2. **The target list.** `npm test` is `node scripts/test.js --tier fast` with **no target**, and no
   target is the whole plan tree; `npm run release-gate` runs the same layer over
   `scripts/plan-inventory.mjs#FAST_TARGETS`, which since `381e55b45` is the single entry
   `['test/plans']`, then the plan files `SLOW_PLANS` names by name -- every file that owns a test
   whose own line says `tier = "slow"`, plus the whole-plan slow files (74 of them since 2026-11-24;
   76 at `1b7b986117`). They are the targets of one command. Neither is a hand-picked set of
   directories.
3. **What a lane types.** Most runs in this project are `camel-remote.ps1 -Plan <one directory or
   file>`, chosen by hand; and `.github/workflows/ci.yml` runs only on a push to `release` and on
   `v*`/`V*` tags, never on `develop`.

So the old figure was wrong in what it attributes, not in its arithmetic: the blocks below sit in the
twelve directories our `-Plan` calls happen not to name, which is a fact about our habit and not about
the repository. The same tree, read through the runner's own loader at `97259a85e`:

| | Plan files | `[[tests]]` blocks | entries (backend-expanded) | of which fast tier |
|---|---|---|---|---|
| the whole tree | 394 | 6652 | 9791 | 9356 |
| the twelve directories our `-Plan` calls do not name | 62 | 524 | 887 | 329 |

**329 of those 524 blocks are fast tier, and `npm test` selects them today**: the rule asks for the
layer, not for a directory list, so a directory nobody names by hand is still run whenever somebody
runs the fast layer. Whether they are red is a question about what we looked at, not a hole in the
repository. The other 195 blocks are slow, milestone or debug tier, and the fast layer reaches them
only through a named plan file or not at all.

| Not named by our `-Plan` calls | Files | Blocks | Entries | of which fast |
|---|---|---|---|---|
| `test/plans/feat` (top level) | 10 | 166 | 445 | 444 |
| `test/plans/feat/transforms` | 3 | 34 | 48 | 48 |
| `test/plans/feat/para` | 3 | 62 | 96 | 96 |
| `test/plans/feat/format` | 3 | 31 | 31 | 31 |
| `test/plans/feat/runtime` | 4 | 9 | 9 | 9 |
| `test/plans/feat/gate` | 3 | 3 | 3 | 3 |
| `test/plans/feat/bindings` | 1 | 2 | 2 | 2 |
| `test/plans/feat/docs` | 1 | 2 | 2 | 2 |
| `test/plans/perf` | 22 | 69 | 69 | 0 |
| `test/plans/probe` | 4 | 18 | 33 | 13 |
| `test/plans/debug` | 2 | 3 | 3 | 0 |
| `test/plans/gpu` | 6 | 125 | 146 | 8 |
| **Total** | **62** | **524** | **887** | **329** |

**The unit, because that is where the last error hid.** A `[[tests]]` block is not a run: the loader
expands a block's `backends` into one entry each, and a `[[sweeps]]` entry derives more at run time.
6652 blocks are 9791 entries, and the fast layer is 9584 cases (9356 entries + 228 derived), from
`node scripts/test.js --tier-report`. The old table's column headed "Entries" counted blocks, and its
"6644 in the tree, a gate runs 6122" compared two scans in that same block unit; neither number was
wrong as a count, and both were read as a statement about gates.

What the 2026-11-17 measurement did show is that a directory no merge lane had named was carrying 27
red: `test/plans/feat/transforms` read 48 tests / 13 pass / 27 fail / 8 known, all of it the same
missing `--trace <area>` the compile layer had, and `16f6bdcea` closed it by asking for the area -- the
directory now reads 40 pass / 0 fail / 8 known. The control run is what makes that a measurement
rather than a story: the plan file as it stood at `16f6bdcea^`, against the baseline binary,
reproduces 13 / 27 / 8 line for line, and `dc3981aa4` removed the one sentence left in that directory
still claiming its readings print unasked.

**These entries are not outside every gate; they are outside the runs we typed.** Whether they are red
today is still not known, and this file does not claim they are green: a plan a run never reads is
neither red nor green, and the repair is to type the layer, not to edit a list.