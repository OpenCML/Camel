# The conformance matrix

`scripts/check-conformance.mjs` compares a program's **recorded reading** -- what the
specification says it prints and returns -- with what the implementation does on the unoptimized
pipeline and after `std::opt`. It is the standing gate for the three comparisons a conformance
study makes, kept in this tree so that a change to the builder, the effects or a pass cannot move
a program's result without a red line naming the program, the comparison and both results.

The cases are in `cases.json`; the programs are the tree's own, under `test/cases/`, so a case
cannot drift from the plan that already runs it.

## Running it

```powershell
node scripts/check-conformance.mjs                        # this tree's out/latest
node scripts/check-conformance.mjs --camel out/debug/bin/camel
node scripts/check-conformance.mjs --case order           # cases whose name contains "order"
```

It is part of the repository's check set, not a second harness beside it:

- `scripts/release-gate.mjs` runs it as one of the static checks, with the install the gate
  picked, so `node scripts/release-gate.mjs --only static` runs it with the others;
- `camel-remote.ps1 -Check conformance` (or `-Check all`) runs it on the host against the tree
  that run built and patched.

Exit 0 is every column green, 1 is at least one column red, 2 is a run that could not start
(no install, an install that is not this tree's build, an unknown argument). A refusal never
reports a reading.

A red run prints one line per failure under the column that owns it:

```
[conformance] FAIL before optimization: 1 of 7 cases -- the unoptimized implementation matches the recorded reading
[conformance]   case 'same_resource_order', nvm: expected exit 9, stdout "9\n0\n", actual exit 9, stdout "0\n9\n" (3.3, ...)
```

Every run -- green or red -- prints the seven limits below ("What this gate does not cover") as
one line each, before the columns, so a log on its own says what the reading it records does not
prove.

## The three columns

| column | what it compares |
|---|---|
| **before optimization** | For each case and each back end (`nvm`, `fvm`, `jit`, `prl`), the unoptimized run's exit code and standard output equal the recorded reading. |
| **across optimization** | The reading recorded before optimization equals the one recorded after it, and for each case and back end the optimized run's exit code and standard output equal the unoptimized run's. |
| **after optimization** | For each case and each back end, the run with `std::opt` in front of the back end has the same exit code and standard output as the recorded reading. |

The reading is one value per case: the specification does not allow a program's result to depend
on whether it was optimized, so `cases.json` records it once and a case that needs to record two
different readings is itself the defect the middle column reports. The optional `reading_after`
field exists only so that a study that has genuinely recorded two model runs can compare them; the
cases here do not set it.

**The columns do not fail independently, and that is the design.** A wrong reading reddens
**before** and **after** together, because both compare a run with the same record; a wrong
`reading_after` reddens **across** and **after**, because the middle column owns the comparison of
the two records and the right column owns the one of the second record. The middle column is
therefore never red on its own: it states a property of the record and of the two runs, not a third
independent measurement, and every per-back-end failure it reports is also a failure of one of the
outer columns. A red run that names one column only is a wrong `case`, `exit` or `optimized` entry
on the side that column reads.

Standard output is normalized with the runner's own normalizers (`strip_ansi`,
`normalize_newlines`, `trim_trailing_space`), so the comparison is the one the plan runner makes.
Standard error is **not** compared: it carries diagnostics that the model has no counterpart for
(the `EffectNotExecuted` warning, for one). The exit code is compared because the specification
makes `main`'s return value the program's exit code.

## Where a reading comes from

A reading is derived from the principle named beside the case in `cases.json`, not from what the
implementation happened to print. The four cases under `test/cases/opt/effects/` and the two under
`test/cases/linear/runtime/` were written from the review's minimal programs for the effect and
ordering defects; their readings are the same numbers `effects_kept.plan.toml` and
`same_resource_order.plan.toml` assert, because both were derived from the same principle. A case
whose reading disagrees with its plan is a defect of one of the two, and the fix is to decide which
one the principle supports -- never to copy the implementation's output into either.

## Coverage

Seven cases, four back ends, two pipelines: 56 program runs. What each column covers, by the
specification's own entries:

| entry | what it states | covered by |
|---|---|---|
| 3.1, evaluated by dependency | an effect nothing uses does not run; what the program uses runs | `unused_effect_not_executed` (a pass may not start it), `fold_keeps_arm_effect`, `branch_keeps_outer_effect` |
| 3.3, resources and `uses` | the effects that run on one resource run in written order; a call is one effect on the resources its callee reaches | `same_resource_order`, `same_resource_order_call` |
| 3.4, `sync` blocks | the effects of the statements a sync block writes happen, wherever they sit in the expression | `dce_effect_under_pure_arithmetic`, `inline_argument_effect` |
| 9.6 / 9.7, passes and effects | a rewrite may neither drop an effect that runs nor start one that does not, and the result is the source program's | all seven cases, on all four back ends, through `std::opt` |

Any one case is read by all three columns: the same program, the same reading, compared on both
sides of the optimizer.

## What this gate does not cover

Named here so that a green run is not read as more than it is:

- **The model itself is not re-executed.** The Lean model and the conformance corpus of the study
  live outside this repository (`Camel-Archive/ECOOP2027/study/conformance/`), and no Lean
  toolchain is installed here. A reading stands in for the model's run: the gate proves the
  implementation matches the record, **not** that the record matches the model. If a reading is
  wrong, every column is green and the gate is silent -- which is why a reading is derived from a
  principle and named with it.
- **The study's corpus is not here.** The 174 programs compared against the model, the 24
  hand-written programs, and the 2019-program export corpus are not in this tree; only the seven
  cases above run. A defect they would expose is unknown to this gate until its minimal program is
  added as a case.
- **Programs that must be refused are not cases.** The matrix compares the output and exit code of
  programs that run. Parse errors, type errors, refused declarations, run-time faults and error
  values have their own plans (`feat/compile`, `feat/consistency/faults`); a fault's output being
  preserved through the optimizer is covered there, not here.
- **Non-determinism is not asserted.** Orderings the specification leaves open -- concurrent
  schedulers, unordered resources, `first_of`, time -- are not compared, because a reading cannot
  state one.
- **Graph shape is not compared.** Whether an order edge sits on the chain, on a `BRCH`/`LOOP`, or
  outside a region is a property of the graph, not of the result; the linear plans' snapshots own
  it.
- **Path coverage is stdout only.** Effects on `fs`, the clock, the environment, user-declared
  resources and handle types are not exercised; every case here observes `stdout` and the exit
  code.
- **Device and export paths.** GPU execution, ONNX export, the Python SDK and the C++ translation
  path have their own plans; the matrix runs the four CPU schedulers only.

## Adding a case

Add the program under `test/cases/` (or, when it exists only for the matrix, under
`test/conformance/cases/`), then add to `cases.json`:

```json
{
  "name": "what the program is called",
  "case": "test/cases/.../program.cml",
  "reading": "what it prints\n",
  "exit": 0,
  "principle": "3.4, the entry the reading comes from",
  "what": "one sentence: what the program does and what the reading states"
}
```

Keep `reading` derived from `principle`, and keep the case on the fast layer's cost budget: every
case is 8 program runs (four back ends, two pipelines). A case that reproduces a defect keeps the
principle in `principle` so that the next reader can check the reading without running anything.

Two optional fields: `reading_after` records a second model reading when a study has one (a case
without it is read as "the result does not change under optimization", which is what the
specification requires), and `optimized` names a pass list for this case only, for a case that
belongs to one pass rather than to `std::opt`.