# Test Plan Reference

This document defines the structure and field constraints for `test/plans/**/*.plan.toml`.

## Purpose

Test plans describe how the repository test runner executes Camel cases, compares outputs, and records regressions.

- runner entry: `node scripts/test.js`
- executor: `node test/tools/run-tests.mjs`
- plan file suffix: `.plan.toml`
- case root: `test/cases/`
- shared variable templates: `test/vars.toml`

When adding a new feature or test case, manually run the case from the command line first, inspect the raw execution flow and stdout/stderr, then write the plan assertions that lock in the intended behavior.

## Running The Tests

- `npm run test` runs the **fast** layer: what a developer runs after a change.
- `npm run test:all` runs everything, fast and slow (`--tier all`). It does **not** run the debug
  layer, which needs a Debug build.
- `npm run release-gate` runs the release evidence instead of everything: the fast layer, then the
  runnable slow plans **by explicit plan file** (never a directory, never `--tier all`), then the
  static checks. It skips the plans `agents/test.md` reserves for a milestone on a quiet machine,
  prints them by name, and refuses when asked for one; see `docs/npm-scripts.md`.
- `node scripts/test.js <plan file>` runs that plan in full: naming the file is the intent, so
  its slow tests run whatever `--tier` says. A **directory** target selects test by test and
  follows `--tier`; add `--tier all` to include the slow tests of a directory or of the whole
  tree. A target that is not a directory but names a plan may omit the suffix:
  `test/plans/feat/exemplars` resolves to `test/plans/feat/exemplars.plan.toml`, and a bare plan
  id such as `feat.exemplars` resolves the same way. A path that is neither a directory nor a
  plan is still an error.
- `node scripts/test.js --tier debug` runs the debug layer (see "The Debug Build And The Debug
  Layer"); it builds nothing and refuses to run without a Debug binary.
- `--build release|debug` picks the build a run addresses: `release` is `out/latest` (the
  default), `debug` is `out/debug`. `CAMEL_TEST_BUILD` sets the same thing for a shell session.
- `--camel <path>` names the install a run measures instead: the `camel` binary at that path is
  `${camel}`, the binary `preflightBuild` checks, and the root every test child reads as
  `CAMEL_HOME`. With none, the install is the `--build` one above. `CAMEL_HOME` never chooses it;
  an exported `CAMEL_HOME` is only what a program outside the runner reads to find its runtime.
- `--jobs N` sets how many tests run at once (default: the processor count). `--jobs 1` is the
  serial execution, useful to reproduce a failure or to compare output.
- `node scripts/test.js --changed` selects the plans a change touches (see "Selecting plans from
  a change").

The runner prints one line per test and, at the end, `[SUMMARY]` counts and the wall time with
the job count and layer, then the gate's split:

```
[SPLIT] build 370.0 s, test 193.3 s, total 563.3 s
```

`build` is the build's own clock, passed by the remote gate helper as
`CAMEL_GATE_BUILD_SECONDS`; a run that built nothing (`-NoBuild`, or a local plan run) prints
`build 0.0 s`. `test` is the runner's wall time, and `total` is their sum. The format and the
arithmetic are pinned by `test/tools/selftest/run.mjs`.

`scripts/gate-time-check.mjs` measures the build side on the gate host: an unchanged tree must
be a no-op (`ninja -n` builds nothing), touching one header must rebuild exactly the objects that
include it (the expected set comes from `ninja -t deps`, and the count is printed), and
`scripts/build-info.js check` must accept the install, which is the precondition of `-NoBuild`.

`node test/tools/selftest/run.mjs` checks the runner itself: the back-end matrix, the per-test
scratch directories and the run root, parallel versus serial output, the tier filter and the
named-plan rule, the risk-based sweep sample, and the `--changed` mapping (directory rule, file
rules, `ownCases`, the layered plans, `CAMEL_TEST_BASE`). It also walks every plan file under
`test/plans/` and fails, by name, when no case path a plan names selects it, and it checks the
suffix-less plan target (H201). Its mirror (H205) fails, by name, when a fast case path selects a
slow-cost plan in full, unless the plan is the only one running that case or
`test/plan-selection.json` records the shared case in `slowCaseOwners` with a reason.

## Layers: fast, slow And debug

`tier` puts a test in one of three layers. It may be set on the plan (the default for all its
tests) or on one `[[tests]]` entry (an override), with the values `fast`, `slow` and `debug`:

- `fast` is the default layer and the one `npm run test` runs. It must stay useful for a
  developer loop: on an 8-core machine a parallel fast run is expected to finish in about two
  minutes (measured on a machine that other worktrees were using at the same time, so it
  took longer).
- `slow` is everything whose result is still worth having but whose cost does not belong in
  the loop: the comparisons against PyTorch, the collector stress and poison scans, the random
  program checks (progen and fuzzing), the performance benchmarks, and any other single test
  that takes about five seconds or more.
- `debug` is a test that is only meaningful with assertions compiled in: it aborts on purpose,
  or it checks something a Release build removes. It runs only under `--tier debug`, never under
  `fast`, `slow` or `all`, because those three run the Release binary.

Rules for a new test:

- It is fast by default: do not mark a test slow to make the default set look fast.
- If it takes about five seconds or more, it **must** be marked `slow` and say in its `desc` (or
  in a comment) why the time is inherent. The same applies when a plan's tests are slow as a
  group: put `tier = "slow"` on the plan itself and say why in the plan's `desc`.
- A test that needs another test's result (`compare_to`, `backends_agree`) is kept in the same
  layer as that test. When a layer filter would separate them, the runner keeps the reference
  too, so a selected test never fails only because its baseline was filtered out.
- A plan written before the split still works: any `tier` other than `slow`/`debug` is the fast
  layer.
- Naming a plan **file** as the target runs all its tests, whatever `--tier` says; the layer
  still filters a directory target and the whole tree. This keeps a named slow test reachable
  without `--tier all` and keeps `--tier` meaningful for a broad run.
- A debug test that expects an abort states `expect_exit` with the abort's code and asserts the
  message, and also asserts that the "not caught" line is **absent**: the positive form is what
  makes a missing guard a failure instead of a pass.

## The Debug Build And The Debug Layer

A Release build compiles the debug assertions out (`NDEBUG`), so a test that relies on one can
only run against a Debug build. The two builds are kept in separate trees:

| Build | `--build` | Artifacts | Binary marker |
|-------|-----------|-----------|---------------|
| Release | `release` (default) | `out/latest` | `(Build <footprint>)` |
| Debug | `debug` | `out/debug` | `(Debug <footprint>)` |

`npm run debug` builds Debug and `scripts/collect-out.js debug` collects it to `out/debug`; the
runner never reads `out/latest` for a debug run and never silently falls back. Before a run it
checks that the binary exists and that `camel --version` says `(Debug ...)`, and exits 2 with
both the path it wanted and the build command when either check fails.

`--tier debug` selects:

- the plans marked `debug_tier = true`: the plans that exercise views, data blocks and the
  executor hot paths, where a view or a store bug would surface, and
- the tests whose own `tier` is `debug` (the deliberate abort).

The plans marked `debug_tier` keep their ordinary layer: they run fast in Release and are
re-run against the Debug binary by the debug layer. Each test's timeout is multiplied by four on
the Debug build, because unoptimised code runs the same loops tens of times slower; a test that
exceeds that is still a failure, not a slow machine. With no target on the command line the
debug layer scans the whole plan tree, because its abort case lives in `test/plans/debug`, which
the ordinary `smoke` + `feat` default does not reach. Naming a plan file still narrows it:

```powershell
npm run debug
node scripts/test.js --tier debug                       # every marked plan + test/plans/debug
node scripts/test.js --tier debug test\plans\feat\linear # only what is marked under that directory
node scripts/test.js --build debug test\plans\feat\mm\complex.plan.toml  # one plan, in full
```

The debug layer is opt-in: `npm run test`, `npm run test:all` and `--changed` never run it, so an
ordinary run pays nothing for it.

## Parallel Execution

Tests run in parallel by default, one per core (`--jobs N` overrides; `--jobs 1` is serial).
Every test already has its own scratch directory under the run's own root, so files do not
collide, and two runner instances can run at the same time. What must not run next to another
test is marked instead of being dropped:

- `parallel = false` on a `[[tests]]` entry, or on the plan to mark all its tests, runs that
  test alone: no other test runs while it does.
- `kind = "benchmark"` tests run alone unless they say `parallel = true`, because the machine's
  load is part of what they measure.

Use `parallel = false` only when a test shares something outside `${test_tmp}`: a compiler
cache or a build directory, a port, a global counter file. A fixed output path under `tmp/test`
is no longer a reason: it belongs in `${test_tmp}`. The examples and mnist tests used to be
marked because they wrote `tmp/examples` or shared `tmp/mnist`; both now run in parallel,
against their own `${test_tmp}`, and the marks are gone.

Two declarations are checked before the run rather than left to the scheduler:
`node test/tools/check-load-class.mjs` reports a path in `args` or `envs` that the plan does not
already hold as a file and that a second test also names, a literal port (`--port 8765`,
`port=8765`), and a fixed OS temporary path in a case. The runner's margin rule (above) covers
the wall-clock half of the same class. The guard says in its header what it cannot see: a path
computed from a variable, a port a test's own client picks at run time.

The output is the serial run's output: results are printed in plan and test order, whichever
test finished first, and the exit code and failure text are the same. The run's log directory
and `summary.json` land under `test/results/log/` as before.

### What Holds a Slot, And Where The Rest Of The Time Goes

A slot is held for the test program. Everything the runner does around it is its own work on its
own thread, and none of it needs a slot: it happens with the slot already back, next to the tests
that are still running. Two of those steps are processes of their own, and each gets a width of
its own instead of a test slot:

- **A `skip_unless` precondition** is the runner asking the host a question, not the test running.
  It runs before the slot is taken, under a width of `--jobs` like the verification scripts: a
  probe that hangs for its whole `skip_unless_timeout_ms` cannot take a slot away from a test, and
  it costs one lane rather than every probe behind it. A one-wide lane was measured and rejected:
  two 120 s MNIST preconditions left 42 probe tests queued and the run's last 150 s with no slot
  held at all, because a test waits for its probe before it takes a slot.
- **A verification script** starts after the slot went back, under a width of `--jobs`: the test's
  own process is gone and its output is already checked, so the verification's node starts next to
  the next test instead of in its place.

A test marked `parallel = false` (and a benchmark, `isSerial`) is the exception: it keeps its slot
through its probe, its verification and its cleanup, because it was marked precisely because it
shares something outside its own scratch directory.

`summary.json` records where every slot went, so the next person does not have to guess.
`slot_timing` is the whole run: `child_ms_total` (children running), `slot_ms_total` (slots held),
`no_child_ms_total` (slots held with no child in them) with its distribution, and the same for
`pre_spawn_ms` (slot taken -> child created) and `post_exit_ms` (child gone -> slot released).
Each step is reported twice, because moving a step out of the slot changes the first number and
not the second: `verify_ms_total` / `verify_in_slot_ms_total` and the same pair for `probe_ms`,
`settle_ms` (resuming from the child's `close` and normalizing), `validate_ms`, `cleanup_ms`
(removing the test's scratch directory) and `verify_wait_ms_total` (the queue in front of the
verification width). `components_ms` is the partition of `no_child_ms_total` into `scratch`,
`probe`, `prep`, `spawn`, `settle`, `validate`, `verify`, `cleanup` and `tail`, so it always sums
to it. There is a one-second `timeline` of slots held and children running, a `loop_lag` block (a
timer that reports how late it fired: the runner's own thread being unavailable is what makes a
chatty child wait on a full stdout pipe), and the top contributors by name for each field — a
total without names is how the wrong thing gets optimized. `test_timings` is one record per test
with all of those intervals and its `at` marks relative to the start of the run. The run prints
one `[SLOT]` line with the ratio and the totals.

Measured on the `smoke` + `feat` fast layer at `-Jobs 8` (5,667 tests, a 32-core host shared with
other gates), eight runs before and eight after:

| per run | before | after |
|---|---|---|
| slots held with no child in them | 255-532 s, 28% of all held slot time | 51-78 s, 6.9% |
| of it: verification scripts booting node | 168-265 s (1,920 scripts, 88-145 ms each) | 0 s in the slot, 209-277 s beside it |
| of it: `skip_unless` probes | 44-304 s (75-83 tests) | 0 s in the slot, 35-46 s beside it |
| of it: everything else | 2.9-4.4 s (scratch, prep, settle, validate, cleanup, tail) | `prep` 13-17 s, `scratch` 0.7 s |
| children / plan wall | 5.64 of 8 (median) | 6.70 of 8 (median) |
| plan wall | 124.2 s (median, 114-197 s) | 124.4 s (median, 97-272 s) |

The plan wall is the one number that does not move, and the reason is in the same table: the host
runs several gates at once, and the summed child time alone varied from 648 s to 995 s between runs
of the same tests, more than the change is worth. Where the load happened to match, the wall moved:
the final gate run summed 713 s of child time against 674-721 s in three of the before runs, and took
100.0 s against their 118-128 s.

The two mechanisms the numbers rule out are worth naming: writing the log files blocks the runner's
thread for 4-6 s over the run and is charged to the slots it blocks (`log_block_ms`), not to the
hundreds of seconds above; and `validateTest`'s own JavaScript (resuming, normalizing, comparing)
is 0.7 s over 5,667 tests. The irreducible part of what is left is `spawn` itself: 6.6-11 ms per
test, 37-62 s of synchronous work in the runner's thread for the run.

Removing those 213-346 s from the slots does not remove them from the plan: the same work now runs
beside the tests, so the wall is bounded by the machine rather than by the eight slots. On a host
loaded by other work the plan wall moves by more than the change does — the children alone varied
from 648 s to 995 s between runs — which is why the ratio and the slot accounting, not the wall, are
the numbers this section reports.

## Runner Output

A run writes two trees inside the checkout, and neither is part of it. `test/results/log/<timestamp>_<pid>/`
is the run's record: `run.json` (what the run was), `summary.json`, and one directory per test holding
the captured `stdout.txt`, `stderr.txt`, `output.txt`, `meta.json` and any verification output.
`tmp/test/run-<pid>` is the run's scratch root. Both are gitignored, so `git clean` does not remove
them, and both grow per run rather than per test.

The runner bounds the record itself, at the start of every run: **`test/results/log` keeps the newest
20 run directories** (`RUN_DIRS_KEPT`, next to the policy it belongs to, in
`test/tools/runner-output.mjs`). A failing run's log is read while the run is happening and just after
it -- nothing in the repository reads an older run back, and no tool reads a run directory at all -- so
what ages out is the record of a run nobody is looking at. Three directories are never removed,
whatever their age or their place in the order:

- the directory of the run doing the pruning;
- a directory whose `<pid>` is a process that is still alive: the runner's own self-test starts nested
  runs beside an outer one, and they write into the same tree;
- a directory the runner did not write, whose name is not `<timestamp>_<pid>`: it is counted and
  reported, not deleted.

A scratch root `tmp/test/run-<pid>` goes when its run is gone and the root has not been touched for an
hour. The runner keeps its root when a test failed precisely so that the files can be read, and that
read happens seconds after the run; an hour retires a root whose run was killed.

The run prints one `[RETAIN]` line with what the bound cost: how many run directories it examined, how
many it removed, and the entries and bytes of what went. The numbers are counted while the directories
are removed, so the receipt is the tree's own measurement. A directory that ages out is removed
recursively at the start of the next run, and the next run's line says so.

Nothing is lost with it. A run's `run.json` carries the revision it ran at, the build, the tier and
every plan it selected, and each test's `meta.json` carries `plan_path` and `case_path`; neither the
plan nor the case is copied into the record. The copy was written once per test and nothing ever read
it back -- the guards that name `test/results` skip it, and the one reader is a person looking at the
run they just made -- so a `smoke` + `feat` gate rewrote the same plan file once for every test in it.
That copy, not the output, was most of what a run directory weighed: of the 43,898 entries and
245.0 MB of a full `smoke` + `feat` fast layer run directory (5,691 tests, measured on the integration
checkout), the copy of the plan and of the case was 11,382 files and 214.2 MB -- 87% of the bytes and
26% of the entries. What a test's record actually holds is its captured output and its `meta.json`:
the same gate run on this branch wrote 32,461 entries and 31.0 MB (112 MB of disk blocks, at 4 KB a
file), and every one of the 26,578 files in it is one the runner wrote. Twenty of those is the whole
bound, at most about 0.6 GB of content, and the `run.json` that replaced the plan copies is 11 KB.
What the tree actually settles at is smaller still, because a nested run is a run: a gate starts the
runner's self-test, which starts dozens of nested runs of its own, and their directories take places
in the window like any other. Measured right after a gate on this host: 22 run directories, 114 MB,
26,992 files, one of them the gate's own -- the newest twenty runs, which is about one gate's worth of
directories rather than twenty gates of history. The window is measured in runs, and the runs a gate
starts are most of them.

`isRunnerOutput(path)` in the same module is also the answer to *which* trees a run writes, for
everything that walks the checkout (`test/tools/check-no-algo.mjs`, `scripts/check-doc-links.mjs`,
`scripts/headers.mjs --root`, `scripts/clean.js`). A guard that hard-codes `test/results` is one more
place a new tree has to be remembered, and the cost of remembering late is measured: the doc-form
guard spent 41.4 s in a bare walk of a 6.8 M-entry log tree against 0.42 s in a checkout that had run
the gate once -- 99.4% of its 70 s budget, which pushed a passing check past the runner's margin and
turned it red on a busy host. That tree reached 52,442 MB and 6,809,438 entries in 3,963 run
directories, and within four hours of being pruned to 20 by hand it was back to about 660 run
directories and 8.8 GB, every one of them written that morning: a bound a person applies by hand is
a bound for one afternoon.

The bound is a property, not a count of what happens to be there: `retention.policy` and
`retention.runner` (`test/plans/feat/tools/retention.plan.toml`) prune the same planted tree under two
bounds -- one that must remove and one that must not -- and then run the real runner twice into a
scratch tree, with `CAMEL_TEST_RESULTS_ROOT` and `CAMEL_TEST_RUN_DIRS_KEEP=1`, and check what survived.

`npm run clean -- --tmp` removes both trees whole, for a checkout that wants them gone now.

## Selecting Plans From A Change

`node scripts/test.js --changed` takes the files that differ from the base branch (committed,
staged or not) and runs the plans they touch, so a small change does not start the whole fast
layer. The base is `origin/develop`; `CAMEL_TEST_BASE` overrides it for a release branch or a
local comparison.

`test/plan-selection.json` is applied to every changed file, and both kinds of rule contribute:

- **Directory rules** (`directoryRules`): a change under `modules/`, `tools/` or `test/cases/`
  selects the plan mirroring its directory. The deepest existing match wins, so
  `modules/tensor/ops/matmul.cpp` selects `test/plans/feat/modules/tensor/`,
  `test/cases/compile/enum/shapes.cml` selects `test/plans/feat/compile/enum.plan.toml`, and
  `test/cases/modules/tensor/bounds.cml` selects `test/plans/feat/modules/tensor/`. This names
  the plan that mirrors the path; it cannot name a plan that lives outside that tree.
- **File rules** (`rules`): the first matching prefix adds the plans the directory rule cannot
  reach but the same change can break. This is how `modules/tiles/**` selects the GPU plans under
  `test/plans/gpu/` (a change to the tiles C++ used to select only the CPU tiles plan),
  `modules/onnx/**` selects the exporter dumps in `feat/trans`, and `modules/tensor/**` selects
  autodiff and the operator/index semantics plans. The union of the directory match and the file
  rule is the selection; a file rule with `"self": true` selects the changed plan file itself, and
  its `"except"` list keeps the forbidden plans out (see below).

A rule entry is a path under `test/plans` (a directory or a `*.plan.toml`), or
`{ "plan": "...", "layer": true }`; `"layer": true` on the rule is the default for its entries. A
layered plan stays a named target but follows `--tier` like a directory: `--tier fast` runs its
fast cases and filters its slow ones. That is the honest rule for a plan whose slow cases are
comparisons the project will not run from an ordinary change (autodiff's five PyTorch gradients,
onnx's four ONNX Runtime/PyTorch comparisons): naming the file in full would run them, and naming
a directory would filter cases the change does need. A plan without `layer` is named and runs in
full whatever `--tier` says.

An entry may also be `{ "plan": "...", "ownCases": true }`: the plan is added only when the
changed file is one of the `case` paths that plan names. A plan that does not run the changed case
cannot be invalidated by it, so a slow plan must not ride a shared directory into an edit it never
reads (H205). This is how a tiles case edit stops selecting the three GPU plans; the
`modules/tiles/` rule still names them in full, because a change to the module can invalidate all
of them, while the case-directory rule adds each GPU plan only for the cases it runs.

The rules are checked in the other direction too (H201): `test/tools/selftest/run.mjs` walks every
plan file under `test/plans/`, resolves the `case` paths it names the way the runner does, and
feeds each path and its directory to the same selection function `--changed` uses. A plan that no
case path selects is a plan whose cases nobody runs on the change that breaks them, and the
self-test fails by name. Only the plans the project deliberately keeps out of an ordinary change
are exempt: `perf/**`, the GC-stress/`progen` plans on the self rule's `except` list, plans in the
debug layer, and a sweep-only plan (`feat/mm/gc_poison_sweep`), which has no cases of its own. The
rules that came out of that check name the plans a directory mirror cannot reach: the linear plans
whose cases are under `test/cases/loops/` and `test/cases/macro/` (`hof_declared`,
`loops_in_arms`, `loop_collect`), the JSON cases under `test/cases/modules/std/json/` (the three
JSON plans and the std facade), the unicode-path case (`feat/modules/unicode_paths`),
`feat/consistency/print_formats`, the compiler benchmark (`feat/tools/compile_bench`), and the
progen finding cases (`progen_simplify` with `capacity`). That last rule also stops those cases
from resolving, through the generic `test/cases/` rule, to `feat/opt/progen.plan.toml`, the slow
random-program check an ordinary change must not run: a narrower directory rule for
`test/cases/opt/progen/` comes first and selects no plan for the finding directory by itself.

The opposite direction is checked as well (H205). A plan file named as a target runs in full
whatever `--tier` says, so a directory rule that resolves a fast case directory to a slow plan
makes every edit in that directory pay for the whole plan. `test/tools/selftest/over-selection.mjs`
derives each plan's cost class from the tree (its `tier`, its tests' tiers, a test timeout of five
minutes or more), the plans that run each case from their `case` entries, and the selection
`--changed` returns; a fast case path that selects a slow-cost plan in full fails by name unless
that plan is the only plan running the case. The two exceptions the original rules hid are named
now: the graph-space gradient case in `test/cases/feat/mm/gc/` and the formatted-print case in
`test/cases/trans/cpp/exec/` each resolve through their directory to a slow plan that does not run
them, so a narrower directory rule comes first and the file rule names the plan that does. A slow
plan that genuinely shares a case with a fast plan (the GPU tiles plan) is recorded in
`slowCaseOwners` with its reason; the self-test rejects an entry that is not slow-cost or that
shares no case with a fast plan, so the list cannot excuse nothing.

What a rule deliberately does **not** select, because the project forbids it on a shared host:

- `test/plans/perf/**` (benchmarks and the PyTorch showcase comparisons);
- the GC-stress plans `feat/mm/gc`, `feat/mm/gc_collections`, `feat/mm/gc_elder_exhaustion`,
  `gpu/tiles_regions`;
- the random-program check `feat/opt/progen.plan.toml`.

No file rule names them, and the `test/plans/` self rule excludes them: editing one of those plan
files falls back to the fast layer instead of running the plan. A change that is only relevant to
one of them (say `benchmarks/`) therefore runs the fast layer, and the report says so; the only
way to run them is to name the plan file deliberately. The ordinary opt selection still includes
`feat/opt`, but as a **directory**, so `--tier fast` runs its fast plans and filters the slow
progen plan while `--tier all` runs it.

When no rule matches, or git cannot read the base branch, the whole fast layer runs. `--tier all`
combines with `--changed` to run the slow tests of directory targets too; a layered plan then runs
its slow cases, so `--tier all` is what reaches the autodiff/onnx comparisons. `--changed --list`
prints the plan targets the changed files select without running them.

A non-plan file under `test/plans/` (a golden, a fixture) selects the nearest directory that
holds a plan: editing `feat/parse/golden/ast_hashes.txt` runs `feat/parse`, and editing a golden
under `feat/modules/tensor/` runs that plan directory. The file has no plan of its own, and this
is the plan that reads it.

A tracked file that matches no rule used to be invisible in a mixed change: another file's rule
kept the fallback away, and this file contributed nothing. Two things now make that
visible. `--changed` prints a `[WARN]` naming every changed file no rule matched (the plan files
the policy excludes are not called holes). And the self-test `test/tools/selftest/run.mjs` walks
every file in the index plus the untracked, not ignored files, and fails on a class that neither
a directory rule nor a file rule selects, unless the class is on its allowlist with a reason. The
plan side is checked the same way, except that its verdict is about the branch: every plan file in
git's index must be selected by a case path it names, or the self-test fails and names the plan
(H201). A plan file that is only in the working tree -- an agent's scratch plan, written to try a
case out -- is not one of the branch's plans, and the self-test names it as a note instead of
failing the run: the live tree is shared with whoever is working in it, and two gates of another
task went red on exactly such a draft (H368). Committing the file makes the check red for real.
The
allowlist names only the deliberately unselectable layers (perf, GC stress, progen, the PyTorch
comparisons) and files no plan reads (vendored code, packaging, CI configuration, repository
metadata). A new kind of file therefore cannot appear without a rule or a reason: add
the rule, or add the class and its reason to the self-test.

That check is why the file rules now cover the classes the first rule set could not have known about: the
`stdlib/<module>.cml` declaration bodies (`feat/modules` plus `feat/compile`) and their computed
halves in `stdlib/signatures/` (the registry plan, which runs `--verify-ops` and `--verify-doc`),
the documentation and release-gate scripts (`feat/docs_links`), the debugger race harness
(`feat/tools/debugger`), exemplars, root Markdown and assets, the public headers under
`include/` (mirroring `src/`), the rest of `src/` and the test harnesses. The plan-reachability
check added the case directories whose plans live elsewhere: the loop, macro, JSON, unicode-path,
print-format and compiler-benchmark cases (H201). `test/perf-baseline.json`
and `scripts/perf-gate.mjs` are the one class left without a perf plan on purpose: perf is manual
(the rule that perf is manual), so a change there must not select a perf plan.

Keep the file short and honest: map the smallest set of plans that would catch a regression in
that area, specific rules first (`src/passes/sched/` before `src/passes/`, `modules/tiles/` before
`modules/`). A rule that selects one plan too many is better than a rule that selects none, but say
the cost: `stdlib/rules.cml` selects the whole `feat/opt` directory (the rewrite engine can change
any optimized result), which is 710 tests plus smoke; `modules/tiles/**` selects six plan files and
the 87 GPU tests among them. Add a rule when a new subsystem gets a plan of its own; do not add a
rule that maps every change to every plan.

## Top-Level Plan Fields

Each plan file may define:

- `id`
  Stable plan identifier such as `linear.control_flow_and_ops`.
- `order`
  Numeric ordering key used when the runner sorts plans.
- `tier`
  The plan's default layer, `fast`, `slow` or `debug` (see "Layers"). A test may override it.
- `debug_tier`
  `true` selects the plan under `--tier debug` while leaving its ordinary layer alone: the plan
  runs fast in Release and is re-run on the Debug binary by the debug layer. Use it on a plan
  that exercises views, data blocks or the executor hot paths.
- `parallel`
  `false` runs every test of the plan alone; a per-test `parallel` is narrower and preferred.
- `tags`
  Free-form string array for indexing and search.
- `desc`
  Optional plan description shown in logs.
- `[defaults]`
  A table of test fields every entry of the plan inherits (see "Plan Defaults").
- `[[tests]]`
  Repeated test entries.
- `[[sweeps]]`
  Repeated sweep entries (see "Sweeps").

The schema is closed: a key that is not listed here, in "Common Test Fields", or in the section
of the kind it configures is an **error**, not a comment. A plan with an unknown key fails as
one failing `<invalid plan>` test in that plan — the error names the plan file, the test and the
key (with a suggestion when the key is a near miss), the plan's tests do not run, and the rest of
the run continues to its summary. A key the runner silently ignored used to be the worse
failure: the test passed while asserting nothing. There is deliberately no escape hatch,
neither in the plan file nor in the environment: a plan the runner only partly understands is
not evidence.

## Common Test Fields

Every `[[tests]]` entry may use these common fields:

- `name`
  Required. Must be unique across the run because differential tests reference prior test names directly.
- `desc`
  Optional human-readable description.
- `kind`
  Required. Supported values are `behavior`, `snapshot`, `differential`, and `benchmark`.
  `negative` is also in use: it is an ordinary behavior test whose case is expected to fail, and
  the runner treats it as `behavior`; it is a label for readers, not a different execution path.
- `case`
  Required. Path to the `.cml` case, resolved relative to the plan file.
- `args`
  Required in practice. Argument vector passed to `camel`. Variable tokens like `${case}` or `${run_jit}` are expanded before execution.
- `expect_exit`
  Optional integer expected process exit code.
- `timeout_ms`
  Optional process timeout in milliseconds. Default is `10000`. A timeout is a pass condition, so it
  is a bound only with room under it: the runner fails a test that used more than a quarter of its
  timeout **of its own CPU time** (`TIMEOUT_MARGIN` in `test/tools/run-tests.mjs`), and prints the
  measured number and the wall clock beside it. The quantity is the work's, not the host's: on a
  shared host a passing test's wall clock is who else is running, and a rule that read it failed the
  plan runner's own self-test at random (H368). A host whose kernel reports no CPU time (no
  `/proc/self/stat`) has no such reading, so there the rule falls back to the wall clock and the run
  says so once; a host that has the reading and does not get one for a test fails that test, because
  a margin nobody can read is not a margin. A test that legitimately needs a large bound states the
  measurement in a comment next to the value ("20 s: 2.1 s alone, 4.2 s at `-Jobs 8`"); a test that
  sits at its bound on a quiet machine has no margin for a busy one and is fixed, not raised. A fast
  case with no reason for a special bound uses the runner default (`10000`): a 4 s bound is a speed
  assertion, and it is rejected as soon as the case takes a quarter of it in its own CPU time. A
  test whose own CPU time exceeds its wall clock is multi-threaded work: the reading sums its
  threads, so a bound on its total work belongs in `cpu_budget_ms` (measured over the whole fast
  layer on 2026-10-24: 21 of 5,873 tests, all ONNX-runtime, dlpack and fuzz cases).
- `cpu_budget_ms`
  Optional. The test's cost bound in milliseconds of **CPU time** (user + system) of the test's own
  process tree, read by `test/tools/child-cpu.mjs`, which the runner runs the command under. A test
  that declares it is judged on that figure and not on the wall clock: `timeout_ms` then states the
  liveness bound (a hang, not a slow run), the `TIMEOUT_MARGIN` rule above is not applied to it, and
  a reading that cannot be made is a failure, never a pass. The unit comes from the author's ruling
  of 2026-10-23 (`docs/handover/reviews/questions-2026-10-23.md`, "declaration-cost-gate" (a)): the
  wall clock on this shared host drifts with who else is running, while a process's own CPU time
  barely moves, so a crossed CPU budget is the test's work growing and never "the host was busy". The
  budget is declared together with a timeout (the runner refuses `cpu_budget_ms` without
  `timeout_ms`), and, like `timeout_ms`, it states the measurement its number came from in a comment
  next to the value.
- `normalizers`
  Optional string array applied to captured output before assertions.
- `envs`
  Optional string array of `NAME=value` entries merged into the child process environment for this
  test only. (`env` is not a field: a plan that writes it is rejected.)
- `failure_note`
  Optional explanatory note printed only when the test fails.
- `skip_unless`
  Optional precondition command (same variable expansion as `args`, run from the repository root).
  A non-zero exit skips the test and prints the command's output as the reason; skipped tests
  count as neither passed nor failed. Use it only for conditions outside the code under test,
  such as a device or an optional Python package:
  `skip_unless = ["node", "test/tools/have-cuda-device.mjs", "0"]`,
  `skip_unless = ["${python}", "-c", "import onnxruntime"]`.
- `skip_unless_timeout_ms`
  Optional timeout for the precondition command. Default is `120000`.
- `cuda_devices`
  Optional boolean, default `false`: `true` declares that this test's Python child wants the
  machine's CUDA devices, and the runner then leaves `CUDA_VISIBLE_DEVICES` exactly as it found it.
  A test that does not declare it sees no device at all, so a CPU-only PyTorch program cannot die
  on a card another tenant holds. See "CUDA visibility"; every plan tagged `gpu` declares it in
  `[defaults]`.
- `tier`
  `fast`, `slow` or `debug`, overriding the plan's layer for this test only.
- `parallel`
  `false` runs the test alone (no other test of the run starts while it runs); `true` opts a
  `kind = "benchmark"` test into the parallel pool. See "Parallel Execution".

## Plan Defaults

A plan may start its tests with a `[defaults]` table. Every field in it is a field of every
`[[tests]]` entry of the plan that does not set the field itself:

```toml
[defaults]
kind = "behavior"
args = ["${case}", "${backend}"]
backends = ["nvm", "fvm", "jit", "prl"]
normalizers = ["strip_ansi", "normalize_newlines", "trim_trailing_space", "normalize_paths"]
```

## One Test On Several Back Ends

A test entry with `backends` is one test per back end, so that a case meant to behave the same
everywhere is written once:

- `backends`
  String array, for example `["nvm", "fvm", "jit", "prl"]`. The entry becomes the tests
  `<name>.nvm`, `<name>.fvm`, ... in that order. In `args`, `${backend}` is the pass list
  `run_<backend>` of `test/vars.toml` (`run_inline_jit` gives the back end `inline_jit`). Every
  assertion of the entry is checked for each of them. `backends = false` in an entry switches
  off a `backends` of the plan defaults.
- `backends_agree`
  Fields on which every back end after the first must equal the first: the differential fields
  below (`exit_code`, `stdout_normalized`, `stderr_normalized`, `output_normalized`) and
  `diagnostics` (name, code, line and column of every diagnostic, in the order printed).
- `known_fail`
  Back ends of `backends` on which the test is known to fail. Such a test is reported as
  `[XFAIL]`, counted apart in the summary (`known failures: N`) and does not fail the run. When
  it passes it fails the run, with the request to remove the mark: a mark cannot go stale
  unnoticed.
- `known_fail_note`
  Printed with an `[XFAIL]`: why, and what will fix it.

`test/tools/selftest/run.mjs` checks these fields on fixture plans, together with the parallel
ordering (the same status lines as a serial run, tests really overlapping), the serial mark (a
`parallel = false` test overlaps nothing), the tier filter (`fast`, `slow` and `all` select
the expected tests) and the `--changed` mapping (`src/passes/sched/` selects the scheduler
plans, `docs/` the documentation checks, `modules/tiles/` the GPU plans, `modules/onnx/` a
layered plan, a plan file itself, the forbidden perf/GC-stress/progen plans nothing, an unknown
path nothing); it runs with the default test set (`test/plans/feat/consistency/runner.plan.toml`).

## Supported Normalizers

`normalizers` currently supports:

- `strip_ansi`
- `normalize_newlines`
- `trim_trailing_space`
- `normalize_paths`
  Replaces the repository root with `<ROOT>` and writes the rest of that path with forward
  slashes. On Windows the root is recognised with native separators and with escaped backslashes
  (a path inside a quoted DOT or JSON string) as well. Use it on every snapshot whose output
  carries an absolute path (for example the `srcfile` attribute of `std::rgir` / `std::argir`),
  otherwise the golden only matches the checkout it was recorded in.
- `normalize_addresses`
- `normalize_origins`
  Replaces an origin id with `<ORIGIN>`, in the DOT form (`origin=63` in `std::gir`, `std::rgir`,
  `std::agir` and `std::argir`) and in the JSON form (`"origin": "63"` in `std::jgir`). An origin
  id is a compiler-internal identifier for a source position: parsing or compiling any other file
  first renumbers it, so it is an identifier, not graph structure, and a golden that pins it fails
  when an unrelated file changes. The source position the id stands for stays in the golden as
  `span` and `srcfile` (the span fields in `jgir`), and `origin=<ORIGIN>` still asserts that a node
  has an origin. Use it on a `text_exact` snapshot that prints origins; a `gir_dot_isomorphic`
  comparison already ignores them.
  A *profile* golden pins a source position too: `test/cases/modules/tensor/fusion_group_none.profile.golden.json`
  records an `origin` that is a position in the concatenated module source, not a compiler id.
  Adding a declaration to `stdlib/tensor.cml` moved it (604 -> 615) and reddened
  `fusion_group.profile.none` with no change to the report. Regenerate that golden from the current
  build (the case's `none` mode prints the failing check; write the two goldens from
  `normalize_doc` / `normalize_text`), do not hand-edit the number, and do not loosen the case: a
  profile row's position is not a count and moves whenever an imported declaration does.
- `normalize_node_ids`
  Renumbers `node#N` in a rich DOT dump (`std::rgir`, `std::argir`) by first appearance *within
  each graph*: the first id a graph prints becomes `n0`, the next distinct one `n1`, and an id
  already seen keeps its label. A tooltip names the slot a node's value lives in — its
  `dataIndex`, a frame slot when positive and a static slot when negative — which the compiler's
  allocator numbers: a lowering change that allocates one temporary more, or materializes a
  constant earlier, moves every id after it while the graph the dump describes is the same. The
  relation the ids state is what is worth pinning, and it survives renumbering: two nodes that
  name one slot are one value (a `GATE` names its producer's slot, a constant written twice shares
  a static slot) and two that name different slots must not collapse into one. The graph name is
  printed beside the label, so renumbering per graph is what the ids mean: static slot 1 of `main`
  and static slot 1 of `gate` are different slots. Use it on a `text_exact` snapshot of a rich
  dump, whose golden keeps the absolute ids as the record of what the tree emitted; a
  `gir_dot_isomorphic` comparison ignores tooltips entirely and needs nothing. `std::agir` and
  `std::gir` print no tooltip, so their snapshots do not name it. A real change still reddens such
  a golden: it moves the nodes' order, the set of nodes sharing a slot, or any other attribute,
  and `feat/trans/annotated_dump`'s `boundaries.tooltip_slots` states the slot relation itself as
  an assertion rather than as text.
- `normalize_duration_seconds`

`test/tools/normalizers.mjs` holds the one definition of these; the plan runner and
`exemplars/check.mjs` (whose goldens the plan only launches) both call it, so a separate
comparison path cannot keep comparing less than a plan snapshot does. `exemplars/check.mjs` names
`normalize_node_ids` too, so an exemplar golden recorded from a rich dump cannot pin the
compiler's numbering either, and it additionally refuses a golden that still records a raw origin
id, so the next such pin fails where it is recorded instead of on another machine.

Normalizers affect:

- inline and path-based verification payloads under `input.normalized.*`
- snapshot comparisons
- `expect_stdout_contains`, `expect_stdout_exact`, `expect_stdout`,
  `expect_stdout_starts_with`, `expect_stderr_contains`, `expect_output_contains` and
  `expect_output_not_contains`
- differential field comparisons that use normalized streams

## Tools

`tool` selects the program a test runs (default `camel`): `camel-format`, `python` (the active
virtual environment's interpreter, else `benchmarks/.venv`'s, else PATH's; also `${python}`) and
`node` (the interpreter running the test runner). `case` and `args` are passed to it as usual.

## Behavior Assertions

Behavior-style tests may combine:

- `expect_exit`
- `expect_stdout_contains`
- `expect_stdout_exact`
- `expect_stdout`
- `expect_stdout_starts_with`
- `expect_stderr_contains`
- `expect_output_contains`
- `expect_output_not_contains`
- `expect_diagnostic_name`
- `expect_diagnostic_code`
- `expect_diagnostic_line`
- `expect_diagnostic_column`
- `verify_script`
- `verify_script_path`
- `verify_timeout_ms`

Every assertion above is checked in addition to the others: a test may combine them. The runner
checks every field it is given; a field it does not know is a plan-schema error (see "Top-Level
Plan Fields"), never a silent no-op.

### `expect_stdout_contains` / `expect_stderr_contains`

Use these for simple substring checks after normalizers are applied.

### `expect_stdout_exact` / `expect_stdout`

The whole normalized standard output must equal the string, character for character: the trailing
newline a program printed is part of the comparison, and so is a trailing space. Use it when the
test is about exactly what the program printed — the order of lines, the absence of an extra
line, the formatting. Prefer it over a set of `expect_stdout_contains` strings when the output is
short enough to state once.

`expect_stdout` is the older name of the same comparison; new tests use
`expect_stdout_exact`. Both keys may be written (they then assert the same output), but a test
does not need both. The failure names the first differing offset and shows the whole text when it
is short (the text around it when it is long).

A multiline value written as `"""` followed by a newline starts with that newline, exactly as TOML
specifies; put the first output line on the same line as the opening `"""` when the program does
not print a leading blank line. The reader of a failing test should not have to discover that
difference by hand.

### `expect_stdout_starts_with`

A string that the normalized standard output must begin with: what a program printed before a
fault is there, complete and in order, ahead of the diagnostic.

### `expect_output_contains` / `expect_output_not_contains`

The same substring checks on the merged output (stdout followed by stderr), for text whose
stream does not matter, such as a diagnostic's detail. `expect_output_not_contains` fails when
any of its strings appears (for example output that must not be reached).

### Diagnostic Expectations

The runner parses compiler/runtime diagnostics from merged process output and can match:

- diagnostic symbolic name
- diagnostic code
- line
- column

These fields are independent filters over the parsed diagnostic list.

## Verification Script Fields

Use exactly one of the following:

- `verify_script`
  Inline JavaScript verification source stored directly in TOML.
- `verify_script_path`
  Relative path to an external `.mjs` verification script.

Do not set both in the same test.

### `verify_script`

`verify_script` is for short, case-local assertions. It accepts either:

- a function expression
- a function body

Function expression example:

```toml
verify_script = """
({ input, assert, match }) => {
    assert(input.exit_code === 0)
    const found = match(/value=([0-9]+)/, input.normalized.stdout)
    assert(Number(found[1]) === 42)
}
"""
```

Function body example:

```toml
verify_script = """
assert(input.exit_code === 0)
const found = match(/value=([0-9]+)/, input.normalized.stdout)
assert(Number(found[1]) === 42)
"""
```

Available helpers:

- `input`
  JSON payload describing the executed test result.
- `assert(condition, message?)`
- `fail(message?)`
- `approx(actual, expected, tolerance?)`
- `match(pattern, text?)`

Important payload fields:

- `input.command`
- `input.exit_code`
- `input.signal`
- `input.timed_out`
- `input.stdout`
- `input.stderr`
- `input.output`
- `input.normalized.stdout`
- `input.normalized.stderr`
- `input.normalized.output`
- `input.diagnostics`
- `input.benchmark`
- `input.cpu_ms`, `input.cpu_budget_ms` — the test's own CPU time (user + system) and the budget it
  was judged against for a test that declared `cpu_budget_ms`, and `null` for every other test: the
  payload of a test whose bound is CPU carries the numbers its verdict came from.
- `input.case_path` — the resolved absolute path of the case: the plan's `case` resolved against
  the plan's directory, which is what the tool was invoked with. The output may repeat it (a
  graph dump names its source file), and it is absolute, so it differs between two checkouts of
  the same commit. A verify script that digests such output must replace `case_path` (or the
  absolute prefix of the case's file name) before hashing; a digest that still contains it is
  only valid in the checkout that recorded it.
- `input.repo_root`, `input.test_root` — the repository root and the `test/` root, absolute.
- `input.plan_path`, `input.test_name` — the plan file and the test's name, including a sweep
  prefix when the test is a sweep copy.
- `input.test_tmp`, `input.test_tmp_relative` — the test's scratch directory, absolute and
  repository-relative.

### `verify_script_path`

Use `verify_script_path` when the assertion logic is large or shared.

Example:

```toml
verify_script_path = "./verify/check-loss.mjs"
```

The runner invokes:

```powershell
node relative\script.mjs <payload.json>
```

The script reads the JSON payload from `process.argv[2]`.

### `verify_timeout_ms`

Optional timeout for the verification script process. Default is `5000`.

## Sweeps

A plan may list `[[sweeps]]` next to its tests. A sweep runs the tests of other plans again
under an extra environment, with all their assertions; the copies are named
`<sweep name>:<source plan id>:<original name>` and count as tests of the plan that lists the
sweep. The source plan's `id` (its file name when it has none) is part of the name because two
source plans may both have a test called `shapes` — `feat/compile/enum` and `feat/para/handoff`
do. Without it the copies collide, and a back end waits for, and compares against, whichever
copy of its first back end ran last: the run reports a failure although every test ran.
One source test reused under two sweeps keeps two distinct copies, one per sweep name. A source
plan listed twice in the same sweep is expanded once.

```toml
[[sweeps]]
name = "poison"
plans = ["../modules", "../linear"]   # plan files or directories, relative to this plan
exclude = ["modules/onnx"]            # substrings of plan paths that are left out
envs = ["CAMEL_GC_POISON=1"]
skip_env_prefix = "CAMEL_GC_"         # leave out tests that set such a variable themselves
timeout_scale = 20                    # their timeouts are multiplied by this
fast_sample = 1                       # keep one test per source plan in the fast layer
risk_tags = ["collections", "closures", "iterators"]  # plans whose sample is larger
risk_sample = 2                       # keep two or three of those instead
```

Only `behavior` and `snapshot` tests of the `camel` tool are taken; a test marked
`known_fail` or `sweep = false` is left out.

The fast sample is risk-based. A source plan whose `tags` include one of `risk_tags` keeps
`risk_sample` tests (two or three: a collection, a closure or an iterator moves objects across
safepoints in more ways, so one sample is thin there); every other source plan keeps
`fast_sample`. The first tests of each source plan are taken, preferring one whose program runs
to completion; without either field every copy follows the plan's layer. `--tier all` keeps
every copy, and that is what a milestone run uses. `feat/mm/gc_poison_sweep.plan.toml` is the
sweep: it checks the handle rules of the collector (`docs/technical/36_gc_handle_rules.md`)
against the tests of the modules, so an operator added later is covered as soon as it has a
test. A test that cannot run under a sweep (it asserts wall-clock time, or which graphs a back
end compiles) says `sweep = false` with a comment.

## Runner environment

- `CAMEL_TEST_TIMEOUT_SCALE=<n>` multiplies every test and verification timeout: for running
  whole plans under a mode that slows every program down, e.g.
  `CAMEL_GC_POISON=1 CAMEL_TEST_TIMEOUT_SCALE=20 node scripts/test.js feat`.
- Variables in the environment of the runner reach every test; a test's own `envs` win. The one
  exception is `CUDA_VISIBLE_DEVICES`, which the runner writes for a Python child that did not ask
  for the devices: see "CUDA visibility".
- `--jobs N` limits the parallel tests; it is an option of the runner process, not an
  environment variable, so a test cannot change how many of its neighbours run.
- `CAMEL_TEST_RESULTS_ROOT=<dir>` writes the run's record into `<dir>` instead of `test/results`, and
  `CAMEL_TEST_RUN_DIRS_KEEP=<n>` lowers the retention bound for that run ("Runner Output"). Both are
  knobs a check uses: the retention check runs two real runs into a scratch tree with a bound of one,
  so the pruning is watched over runs that really happened rather than over a fixture alone. The bound
  follows the record: a run whose record was moved prunes the tree it is writing to, not the checkout's
  `test/results`, which nothing in that run is adding to.
- The timeouts above apply to one test; a parallel run does not relax them, and a test whose
  time depends on the machine's load belongs alone (`parallel = false`) or in the slow layer.

## CUDA visibility

A Python test that imports a CUDA-aware library can reach a card without naming one, and then it
answers to whoever else is on the host. `torch.optim.Adam(...).step()` calls torch's
`_accelerator_graph_capture_health_check()`, which asks for the current accelerator and its current
stream and so retains the primary context of every card; `cuDevicePrimaryCtxRetain` returns
`CUDA_ERROR_UNKNOWN` (999) while another tenant holds one, and the process dies before its first
assertion. `SGD` makes no such call, so a CPU-only program's `Adam` step is green on an idle host
and red on a busy one: one test of `feat/modules/onnx` failed for a reason that had nothing to do
with the code it tested, and the same program with `SGD` in its place passed.

The runner hides the machine's devices from every Python child of a test that did not ask for them:
it writes `CUDA_VISIBLE_DEVICES=` (empty, CUDA's spelling for "names no device") into the
environment of a `tool = "python"` test process and of a `skip_unless = ["${python}", ...]`
precondition. The value is written before the test's own `envs`, so a test that names the variable
itself keeps its value; `CUDA_VISIBLE_DEVICES=-1` is the other spelling that names no device.

A test that really wants a device says so:

```toml
[[tests]]
name = "compiles_and_runs_on_a_card"
cuda_devices = true
case = "../../cases/..."
```

`cuda_devices = true` means "do not touch my Python child's devices": the runner then leaves the
machine's `CUDA_VISIBLE_DEVICES` exactly as it found it, so a test that uses several cards keeps
them, and a missing or busy card stays that test's own failure instead of something the runner
hides. Every plan tagged `gpu` declares it in `[defaults]`, because its tests exist to run against
the cards.

The default lives in the runner rather than in a pin per plan, deliberately. The protection has to
reach a plan nobody has written yet; a test's `envs` never reach its `skip_unless` process (a
precondition that stops holding is a silent skip, so a lost pin would cost coverage rather than
fail a run); a pinned value has to name an ordinal, and `CUDA_VISIBLE_DEVICES=0` cannot express
"the cards" for a test that uses several; and a `[defaults]` line or a generated copy can be
dropped without the plan noticing. The price of the default is that a device test which forgets
its declaration measures the CPU and still passes, so
`test/tools/check-cuda-visibility.mjs` refuses a Python test in a plan tagged `gpu` that does not
declare, and a test that names a card in its own `envs` without declaring.

The guard reads every plan under `test/plans/` -- the perf layer excepted, because those plans are
manual and pin inside `showcase/` -- and refuses a test that reaches Python with a CUDA-aware import
and is neither hidden (by the runner's default, by its plan, or by the case script) nor declared.
It prints how many tests it examined and fails when that number moves, so it cannot stop examining
anything unnoticed. `test/tools/selftest/cuda-visibility/` is its fixture and the runner's:
`visibility.plan.toml` runs the default, its precondition, the opt-in and a test's own value against
an untouched `node` reference, and `unpinned/` is the plan the guard must refuse, test by test.

## Snapshot Tests

Snapshot tests require:

- `kind = "snapshot"`
- `snapshot_stream`
  `stdout` or `stderr`
- `snapshot_compare`
  Currently `text_exact` or `gir_dot_isomorphic`
- `golden`
  Golden file path, resolved relative to the plan file

When the runner is called with `--update-golden`, the selected snapshot stream overwrites the golden file before comparison.

## Differential Tests

Differential tests require:

- `kind = "differential"`
- `compare_to`
  Name of a previously executed test in the same run
- `compare_fields`
  Supported values:
  - `exit_code`
  - `stdout_normalized`
  - `stderr_normalized`
  - `output_normalized`

The referenced test must have already run. Keep dependent test ordering explicit inside the plan.

## Benchmark Tests

Benchmark tests may use:

- `kind = "benchmark"`
- `perf_budget_mean_ms`
- `perf_budget_tolerance_pct`
- `perf_budget_platform`
  Optional string naming the platform the budget was recorded on (`local.sample` in
  `perf/fib.plan.toml`). It is provenance, not an assertion: the runner accepts it, copies it
  into the test's `meta.json`, and does not change the budget comparison with it.

The case must emit the expected JSON benchmark payload format used by the runner.

## Performance Measurement

The plans under `test/plans/perf/` -- one plan file per benchmark, every one of them carrying the
`slow` tier -- are measured on purpose, never by a gate. They are not in
`npm run test` (which runs `--tier fast`), editing one of them does not select it from `--changed`
(see "Selecting Plans From A Change"), and no CI job runs them: the only tooling that runs them is
the plan runner, with the plan file named as its target. That is the policy, not an
oversight: `agents/test.md` treats the perf/timeit plans as heavy -- they saturate every core for
minutes and are worth running only at a milestone, by a maintainer, on a quiet machine -- and the
performance notes record what running the whole layer on a shared host would cost. The price of
the exclusion is that nothing notices a performance regression, so the
measurement has one manual entry point:

Three of those plans are also in the release gate's slow layer, where a plan file is named one by
one: `perf.para_speedup`, whose guard is a property of one run rather than a stored number, and the
count guards `perf.step_counts` and `perf.reduce_layout_counts`, which a 2026-10-25 ruling split out
of `perf.step_budget` and `perf.reduce_layout`. What those three assert is a count or a property --
the operator calls one round of the step issues, the blocks `sum_to` allocates -- and never a
duration, which is what lets them ride a shared host; the measured halves of the two split plans
stay here and run before a release.

```bash
node scripts/perf-gate.mjs --list        # every plan of the directory, what each measures, and why it is in or out
node scripts/perf-gate.mjs               # measure the gateable subset and compare with the baseline
node scripts/perf-gate.mjs --update      # reseed test/perf-baseline.json on the host that measured it
node scripts/perf-gate.mjs --reps 9 --threshold 20
```

`--list` prints the classification of every plan in that directory and the kind of each refusal
(a repetition that costs minutes, a measurement that only means anything inside its own run, or the
policy `agents/test.md` quotes), and the classification is checked against the directory itself, so
a plan added under `test/plans/perf/` cannot go unclassified or be classified twice.

`node scripts/perf-gate.mjs` measures only the plans whose commands can be repeated often enough:
today `perf.startup` (a few seconds per repetition) and `perf.fib` (about 11 s); every other plan is
refused by name with the kind `--list` prints. Running one of those stays a milestone decision: name
the plan file to the runner yourself.

How the numbers are read:

- one fresh `camel` process per repetition, with the repetitions interleaved and the order rotated;
- the wall clock, and on Linux the child's own CPU time (user+system, from `/proc/self/stat`).
  A case whose CPU median is at least 250 ms is judged on CPU, which does not grow when the host
  is busy; the millisecond cases are judged on the wall clock, where the spread is the protection;
- the median and its p10..p90 spread per case. A regression is never claimed when the spreads
  overlap;
- the host's 1-minute load is recorded. A wall-clock case is compared only when the run is inside
  the baseline's band (`load + 2 + 10%`); outside it the case is **inconclusive**, never a pass:
  it says "measured at load N", and the next step is to rerun inside the band. The CPU-judged
  cases compare at any load, because child CPU time does not grow with the host's load. A gate
  that reds for being busy is worse than no gate.

`test/perf-baseline.json` is data, not a ceiling: the medians, both spreads, the exact command of
every case, the commit, the build's own stamp, the repetition count and the host load. Seed it on
the host whose numbers it records; a baseline from another machine is not comparable. The
performance notes have the cost of each plan and the evidence for the
split. The plans left out keep the measurement they already have: `perf.startup` carries its own
CPU-time ceilings and `declaration.cost`'s per-declaration ceiling, absolute CPU total and
structural assertion (the 30% compile-work share retired with the 2026-10-23 ruling), `perf.gc`
asserts what must not grow with the length of the run, the showcases compare against PyTorch, and
`docs/performance/graph-store-baseline.md` is a separate manual baseline of the graph store.

A plan in any layer may carry the same unit on one test: `cpu_budget_ms` (above) makes the runner
read the test's own CPU time through `test/tools/child-cpu.mjs` and fail the test when that figure
crosses the budget, with `timeout_ms` left as the liveness bound. That is the form for a test whose
wall clock is the host's, not the test's: `modules.autodiff/benchmark_gradients_vs_torch` reads
943-979 s of its own CPU against 127-211 s of wall clock, and it used to be red on the runner's
`TIMEOUT_MARGIN` for that reason alone.

## Variable Expansion

`args` tokens support `${name}` interpolation from:

- `test/vars.toml`
- the implicit `${case}` variable
- the implicit `${test_tmp}` variable, described under "Test scratch"
- the built-in `${python}` variable: the active virtual environment's interpreter, else `python`
  on Windows and `python3` elsewhere (`test/vars.toml` may override it)
- the built-in `${camel}` variable: the compiler of the build under test
  (`out/<tag>/bin/camel[.exe]`, the path the runner prints and records in `summary.json`). A test
  whose program starts a compiler names the install with `--camel ${camel}`: an explicitly named
  install travels as a command-line argument, never as an environment switch (the author's ruling of
  2026-10-25, `docs/handover/reviews/questions-2026-10-24-evening.md`,
  "stdlib-declaration-check-measures-a-foreign-install"); `CAMEL_HOME` is the install location's
  convention and stays

Rules:

- a token equal to `${name}` may expand to one string or many arguments
- embedded `${name}` inside larger text must resolve to a scalar string
- missing variables are errors

`${test_tmp}` is also expanded in `envs` values and in the `expect_*` string fields, so that a
test can name its own output path in an assertion about what a program printed.

## Test Scratch

Every test runs with its own fresh scratch directory, exposed three ways:

- `${test_tmp}`: its repository-relative path with forward slashes
  (`tmp/test/run-<pid>/<plan>/<test>`; the root is one per runner process)
- `CAMEL_TEST_TMPDIR`: its absolute path in the child process environment
- `test_tmp` / `test_tmp_relative`: the absolute and repository-relative paths in the
  `verify_script` payload (`input.test_tmp`, `input.test_tmp_relative`)

A test that writes a file writes it under `${test_tmp}` (or `CAMEL_TEST_TMPDIR`), never under a
fixed path that an earlier test or an earlier run could have written. The directory is removed
after the test; the run's root (`tmp/test/run-<pid>`) is removed when the run succeeds, and is
kept, with its path printed (`[INFO] test scratch kept: ...`), when it fails. Nothing else under
`tmp/test` is cleared: a file another run keeps there survives, which is what lets two runner
instances run at the same time without seeing each other's files. A case run outside the runner
(a benchmark, or a direct `camel` command) may keep its older fixed path as a fallback when
`CAMEL_TEST_TMPDIR` is unset; the examples and mnist cases take it through
`get_env('CAMEL_TEST_TMPDIR')`, and the one ONNX refusal case that writes a model gets an
`onnx::export out="${test_tmp}/..."` path.

## Field Constraints And Style

- A field must be one this document defines. The runner rejects an unknown key (and a known key
  of the wrong type for the fields it types) with an error naming the plan file, the test and
  the key; fix the plan, do not work around the runner. A new field is added to the runner and
  to this document in the same change, so the next plan can rely on it.
- Prefer explicit `expect_exit = 0` for successful behavior tests.
- Prefer `verify_script` over `verify_script_path` for short, local checks.
- Prefer `verify_script_path` for reusable or multi-test logic.
- Do not encode final pass/fail semantics only inside Camel output like `"ok"`.
- Print observable runtime state from the case, then assert on it externally.
- Keep `name` stable; logs and differentials depend on it.
- Keep `normalizers` intentional. Over-normalizing can hide real regressions.
- When a snapshot pins a compiler-internal counter (an origin id, a node's slot, an index into a
  table), compare the *identity* the counter stands for, not the number. A normalizer that
  renumbers the counter by first appearance (`normalize_origins`, `normalize_node_ids`) keeps the
  relation the counter states and stops reddening when unrelated code moves the allocator; a
  counter that *is* the artifact under test (an exported operator list, a growth budget, the node
  count a pass reports) stays exactly as printed.

## Migration Note

`verify_js` is deprecated. Use:

- `verify_script` for inline JavaScript
- `verify_script_path` for external scripts

`expect_stdout` is the older name of `expect_stdout_exact`; both are supported and compare the
whole normalized standard output. New tests use `expect_stdout_exact`.