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
- `npm run test:all` runs everything, fast and slow (`--tier all`).
- `node scripts/test.js <plan file>` runs that plan in full: naming the file is the intent, so
  its slow tests run whatever `--tier` says. A **directory** target selects test by test and
  follows `--tier`; add `--tier all` to include the slow tests of a directory or of the whole
  tree.
- `--jobs N` sets how many tests run at once (default: the processor count). `--jobs 1` is the
  serial execution, useful to reproduce a failure or to compare output.
- `node scripts/test.js --changed` selects the plans a change touches (see "Selecting plans from
  a change").

The runner prints one line per test and, at the end, `[SUMMARY]` counts and the wall time with
the job count and layer.

`node test/tools/selftest/run.mjs` checks the runner itself: the back-end matrix, the per-test
scratch directories and the run root, parallel versus serial output, the tier filter and the
named-plan rule, the risk-based sweep sample, and the `--changed` mapping (directory rule,
file rules, `CAMEL_TEST_BASE`).

## Layers: fast And slow

`tier` puts a test in one of two layers. It may be set on the plan (the default for all its
tests) or on one `[[tests]]` entry (an override), with the values `fast` and `slow`:

- `fast` is the default layer and the one `npm run test` runs. It must stay useful for a
  developer loop: on an 8-core machine a parallel fast run is expected to finish in about two
  minutes (H10 measured it on a machine that other worktrees were using at the same time, so it
  took longer; the numbers are in `docs/handover/reports/H10.md`).
- `slow` is everything whose result is still worth having but whose cost does not belong in
  the loop: the comparisons against PyTorch, the collector stress and poison scans, the random
  program checks (progen and fuzzing), the performance benchmarks, and any other single test
  that takes about five seconds or more.

Rules for a new test:

- It is fast by default: do not mark a test slow to make the default set look fast.
- If it takes about five seconds or more, it **must** be marked `slow` and say in its `desc` (or
  in a comment) why the time is inherent. The same applies when a plan's tests are slow as a
  group: put `tier = "slow"` on the plan itself and say why in the plan's `desc`.
- A test that needs another test's result (`compare_to`, `backends_agree`) is kept in the same
  layer as that test. When a layer filter would separate them, the runner keeps the reference
  too, so a selected test never fails only because its baseline was filtered out.
- A plan written before the split still works: any `tier` other than `slow` is the fast layer.
- Naming a plan **file** as the target runs all its tests, whatever `--tier` says; the layer
  still filters a directory target and the whole tree. This keeps a named slow test reachable
  without `--tier all` and keeps `--tier` meaningful for a broad run.

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

The output is the serial run's output: results are printed in plan and test order, whichever
test finished first, and the exit code and failure text are the same. The run's log directory
and `summary.json` land under `test/results/log/` as before.

## Selecting Plans From A Change

`node scripts/test.js --changed` takes the files that differ from the base branch (committed,
staged or not) and runs the plans they touch, so a small change does not start the whole fast
layer. The base is `origin/develop`; `CAMEL_TEST_BASE` overrides it for a release branch or a
local comparison.

`test/plan-selection.json` is applied in two steps:

- **Directory rules first** (`directoryRules`): a change under `modules/`, `tools/` or
  `test/cases/` selects the plan mirroring its directory. The deepest existing match wins, so
  `modules/tensor/ops/matmul.cpp` selects `test/plans/feat/modules/tensor/`,
  `test/cases/compile/enum/shapes.cml` selects `test/plans/feat/compile/enum.plan.toml`, and
  `test/cases/modules/tensor/bounds.cml` selects `test/plans/feat/modules/tensor/`. A plan file
  found this way is a named target and runs in full.
- **File rules second**: only when no plan directory mirrors the path. Each rule is a path
  prefix and the plans it selects, as paths under `test/plans`; a rule with `"self": true`
  selects the changed plan file itself (a change to `test/plans/...` runs that plan). Rules
  may name plan directories or plan files; the first matching rule wins.

The union of everything selected runs in plan order. When no rule matches, or git cannot read the
base branch, the whole fast layer runs. `--tier all` combines with `--changed` to run the slow
tests of directory targets too.

Keep the file short and honest: map the smallest set of plans that would catch a regression in
that area, specific rules first (`src/passes/sched/` before `src/passes/`). Add a rule when a
new subsystem gets a plan of its own; do not add a rule that maps every change to every plan.

## Top-Level Plan Fields

Each plan file may define:

- `id`
  Stable plan identifier such as `linear.control_flow_and_ops`.
- `order`
  Numeric ordering key used when the runner sorts plans.
- `tier`
  The plan's default layer, `fast` or `slow` (see "Layers"). A test may override it.
- `parallel`
  `false` runs every test of the plan alone; a per-test `parallel` is narrower and preferred.
- `tags`
  Free-form string array for indexing and search.
- `desc`
  Optional plan description shown in logs.
- `[[tests]]`
  Repeated test entries.

## Common Test Fields

Every `[[tests]]` entry may use these common fields:

- `name`
  Required. Must be unique across the run because differential tests reference prior test names directly.
- `desc`
  Optional human-readable description.
- `kind`
  Required. Supported values are `behavior`, `snapshot`, `differential`, and `benchmark`.
- `case`
  Required. Path to the `.cml` case, resolved relative to the plan file.
- `args`
  Required in practice. Argument vector passed to `camel`. Variable tokens like `${case}` or `${run_jit}` are expanded before execution.
- `expect_exit`
  Optional integer expected process exit code.
- `timeout_ms`
  Optional process timeout in milliseconds. Default is `10000`.
- `normalizers`
  Optional string array applied to captured output before assertions.
- `env`
  Optional string array of `NAME=value` entries merged into the child process environment for this
  test only.
- `failure_note`
  Optional explanatory note printed only when the test fails.
- `skip_unless`
  Optional precondition command (same variable expansion as `args`, run from the repository root).
  A non-zero exit skips the test and prints the command's output as the reason; skipped tests
  count as neither passed nor failed. Use it only for conditions outside the code under test, such
  as network access for a dataset download:
  `skip_unless = ["${python}", "test/cases/modules/nn/mnist_loader.py", "--ensure", "tmp"]`.
- `skip_unless_timeout_ms`
  Optional timeout for the precondition command. Default is `120000`.
- `tier`
  `fast` or `slow`, overriding the plan's layer for this test only.
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
plans, `docs/` the documentation checks, a plan file itself, an unknown path nothing); it runs
with the default test set (`test/plans/feat/consistency/runner.plan.toml`).

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
- `normalize_duration_seconds`

Normalizers affect:

- inline and path-based verification payloads under `input.normalized.*`
- snapshot comparisons
- `expect_stdout_contains`, `expect_stderr_contains`, `expect_output_contains` and
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

### `expect_stdout_contains` / `expect_stderr_contains`

Use these for simple substring checks after normalizers are applied.

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
`<sweep name>:<original name>` and count as tests of the plan that lists the sweep.

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
- Variables in the environment of the runner reach every test; a test's own `envs` win.
- `--jobs N` limits the parallel tests; it is an option of the runner process, not an
  environment variable, so a test cannot change how many of its neighbours run.
- The timeouts above apply to one test; a parallel run does not relax them, and a test whose
  time depends on the machine's load belongs alone (`parallel = false`) or in the slow layer.

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

The case must emit the expected JSON benchmark payload format used by the runner.

## Variable Expansion

`args` tokens support `${name}` interpolation from:

- `test/vars.toml`
- the implicit `${case}` variable
- the implicit `${test_tmp}` variable, described under "Test scratch"
- the built-in `${python}` variable: the active virtual environment's interpreter, else `python`
  on Windows and `python3` elsewhere (`test/vars.toml` may override it)

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
`onnx::export(out="${test_tmp}/...")` path.

## Field Constraints And Style

- Prefer explicit `expect_exit = 0` for successful behavior tests.
- Prefer `verify_script` over `verify_script_path` for short, local checks.
- Prefer `verify_script_path` for reusable or multi-test logic.
- Do not encode final pass/fail semantics only inside Camel output like `"ok"`.
- Print observable runtime state from the case, then assert on it externally.
- Keep `name` stable; logs and differentials depend on it.
- Keep `normalizers` intentional. Over-normalizing can hide real regressions.

## Migration Note

`verify_js` is deprecated. Use:

- `verify_script` for inline JavaScript
- `verify_script_path` for external scripts
