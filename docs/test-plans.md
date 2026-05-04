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
- `failure_note`
  Optional explanatory note printed only when the test fails.

## Supported Normalizers

`normalizers` currently supports:

- `strip_ansi`
- `normalize_newlines`
- `trim_trailing_space`
- `normalize_paths`
- `normalize_addresses`
- `normalize_duration_seconds`

Normalizers affect:

- inline and path-based verification payloads under `input.normalized.*`
- snapshot comparisons
- `expect_stdout_contains` and `expect_stderr_contains`
- differential field comparisons that use normalized streams

## Behavior Assertions

Behavior-style tests may combine:

- `expect_exit`
- `expect_stdout_contains`
- `expect_stderr_contains`
- `expect_diagnostic_name`
- `expect_diagnostic_code`
- `expect_diagnostic_line`
- `expect_diagnostic_column`
- `verify_script`
- `verify_script_path`
- `verify_timeout_ms`

### `expect_stdout_contains` / `expect_stderr_contains`

Use these for simple substring checks after normalizers are applied.

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

Rules:

- a token equal to `${name}` may expand to one string or many arguments
- embedded `${name}` inside larger text must resolve to a scalar string
- missing variables are errors

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
