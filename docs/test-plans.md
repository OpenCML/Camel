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

## Top-Level Plan Fields

Each plan file may define:

- `id`
  Stable plan identifier such as `linear.control_flow_and_ops`.
- `order`
  Numeric ordering key used when the runner sorts plans.
- `tier`
  Logical suite bucket such as `smoke`, `feat`, or `perf`.
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

`test/tools/selftest/run.mjs` checks these fields on a fixture plan; it runs with the default
test set (`test/plans/feat/consistency/runner.plan.toml`).

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
```

Only `behavior` and `snapshot` tests of the `camel` tool are taken; a test marked
`known_fail` or `sweep = false` is left out. Use a sweep for a property that every program
must keep whatever it computes: `feat/mm/gc_poison_sweep.plan.toml` checks the handle rules
of the collector (`docs/technical/36_gc_handle_rules.md`) against the tests of the modules,
so an operator added later is covered as soon as it has a test. A test that cannot run under
a sweep (it asserts wall-clock time, or which graphs a back end compiles) says
`sweep = false` with a comment.

## Runner environment

- `CAMEL_TEST_TIMEOUT_SCALE=<n>` multiplies every test and verification timeout: for running
  whole plans under a mode that slows every program down, e.g.
  `CAMEL_GC_POISON=1 CAMEL_TEST_TIMEOUT_SCALE=20 node scripts/test.js feat`.
- Variables in the environment of the runner reach every test; a test's own `envs` win.

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

- `${test_tmp}`: its repository-relative path with forward slashes (`tmp/test/run/<run>/<test>`)
- `CAMEL_TEST_TMPDIR`: its absolute path in the child process environment
- `test_tmp` / `test_tmp_relative`: the absolute and repository-relative paths in the
  `verify_script` payload (`input.test_tmp`, `input.test_tmp_relative`)

A test that writes a file writes it under `${test_tmp}` (or `CAMEL_TEST_TMPDIR`), never under a
fixed path that an earlier test or an earlier run could have written. The directory is removed
after the test, and `tmp/test` and `test/tmp` are cleared before and after a run, so repeated runs
do not accumulate files and a stale file cannot be mistaken for this run's output. Files that a
golden prints keep their fixed path (`tmp/examples`): those plans write there on purpose. A case
run outside the runner (e.g. a benchmark, which has no private working directory) may keep its
older fixed path as a fallback when `CAMEL_TEST_TMPDIR` is unset. Two runner instances must not
run at the same time: they share `tmp/test` (a parallel runner is a separate item).

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
