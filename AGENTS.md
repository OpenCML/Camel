# Agent Development Guide

This file is the entry point. Read the detailed rules under `agents/` before making changes.

## Core Rules
- Runtime shell: Windows PowerShell.
- Use `;` to chain commands. Do not use `&&`.
- Prefer the repo build commands documented below.
- Follow the refactor, test, and commit rules in `agents/refactor.md`, `agents/test.md`, and `agents/commit.md`.
- When preparing a commit, describe the common purpose of the entire staged set, not just the most recent edit.

## Build And Runtime
- Standard build: `npm run build`
- Debug build: `npm run debug`
- Profiling build: `npm run profile`
- Build artifacts land in `out/latest/bin/`; executable is `camel.exe`.
- Do not modify or replace build artifacts manually.
- Use only the build commands above. They handle artifact synchronization and ensure you run the latest binaries.
- Required dynamic library: `libcamel.dll`
- `CAMEL_HOME` must point to the installation root, including `bin`, `lib`, and related directories.
- `CAMEL_STD_LIB` overrides the stdlib location.
- `CAMEL_PACKAGES` adds extra module search roots and uses `;` separators on Windows.
- Runtime DLLs for `out/.../libs/` come from `modules/python/sdks/python3xx/` after `collect-out`.
- `sync-python-sdks` copies `python3xx.dll`, `python3.dll`, and `vcruntime140*.dll` into the SDK cache.
- If SDK DLLs are missing, the build falls back to `VIRTUAL_ENV`, `CONDA_PREFIX`, or `python` on PATH.
- CMake build uses SDKs first; if the SDK root is missing, it falls back to the active virtual environment.
- SDK sync is manual: `node scripts/sync-python-sdks.js <python-archive-root>`.

**PowerShell example:**
```powershell
$env:CAMEL_HOME = "project-root\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
```

### Logging (CLI and builds)
- Default: no `-l` / `-v*` flags means the global threshold is `fatal`; logs go to stderr when enabled.
- Levels (`--log-level`): `fatal`, `warn`, `info`, `debug`, `trace`, `off`.
- Shortcuts: `-v` -> `warn`, `-vv` -> `info`, `-vvv` -> `debug`, `-vvvv` -> `trace`.
- Scope filtering: `--log-preset none|wall|extra` and `--log-include a,b,c`.
- In `npm run build` (`NDEBUG`), `CAMEL_LOG_DEBUG` / `CAMEL_LOG_TRACE` sites are compiled out.
- Low thresholds can flood output on workloads such as `fib 30`.
- Logging flags must appear before the target file; arguments after the target file are interpreted as passes.
- Use build mode for routine verification; raise thresholds only when diagnosing.
- Start with small inputs when debugging and use `Select-Object` when output throttling is required.

## Pass Execution Model
- Canonical syntax: `camel xxx.cml pass1 pass2 ...`
- `std::nvm` is the fallback pass when no pass is specified or when specified passes complete without yielding an empty graph.

## Test Execution
- Main entry: `node scripts/test.js`
- Test plans live under `test/plans/`; you may pass either a single `*.plan.toml` file or a plan directory.
- When adding a new feature or a new test case, first run the relevant case manually from the command line and inspect the execution flow and stdout/stderr directly before relying on the plan runner. Only add or refine automated `verify_*` logic after the manual run confirms the observable behavior you actually want to lock in.
- `npm run test` runs the default developer set: `smoke` plus all `feat/**` plans.
- `npm run test:all` runs all plans under `test/plans/`, including `perf/**`.
- Prefer passing the target explicitly instead of adding new `npm run test:*` aliases.
- Shorthand targets such as `smoke`, `feat`, `parse`, `compile`, `opt`, `trans`, `linear`, `para`, `modules`, and `perf` resolve to the matching plan directories.
- Direct examples:
```powershell
node scripts/test.js test\plans\smoke
node scripts/test.js test\plans\feat\para
node scripts/test.js test\plans\feat\linear\recursion_and_calls.plan.toml
npm run test -- perf.para
npm run test -- feat.trans.macro
node test\tools\run-tests.mjs --update-golden test\plans\smoke\core.plan.toml
```

### Common Passes

| Pass | Description |
|------|-------------|
| `std::gir` | Translation pass: prints the current GIR and returns an empty graph |
| `std::rgir` | Translation pass: prints an AI-readable GIR DOT with only `label` and `type` attributes, then returns an empty graph |
| `std::nvm` | Scheduling pass: executes on the node VM linearly, then returns an empty graph |
| `std::fvm` | Scheduling pass: high-performance bytecode VM, returns an empty graph |
| `std::jit` | Scheduling pass: JIT-enabled bytecode VM, returns an empty graph |
| `std::inline` | Optimization pass: inlines small functions into larger graphs to reduce call overhead; returns an optimized graph |

### Graph Inspection Preference
- Prefer `std::rgir` when inspecting graphs in terminal output, snapshots intended for AI review, or structural debugging where visual styling is noise.
- Use `std::gir` when Graphviz-oriented styling, shapes, sizing, or richer rendering metadata is specifically needed for human visualization.

### Examples
```powershell
camel fib.cml std::rgir
camel fib.cml std::gir
camel fib.cml std::inline std::fvm
```

## Priority
- If a rule here conflicts with a detailed note in `agents/`, treat this file as the entry point and the detailed file as the working instruction set.
