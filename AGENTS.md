# Agent Development Guide

This file is the entry point. Read the detailed rules under `agents/` before making changes.

## Core Rules
- Runtime shell: Windows PowerShell.
- Use `;` to chain commands. Do not use `&&`.
- Prefer the repo build commands documented below.
- Follow the refactor, test, and commit rules in `agents/refactor.md`, `agents/test.md`, and `agents/commit.md`.

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

### Common Passes

| Pass | Description |
|------|-------------|
| `std::gir` | Translation pass: prints the current GIR and returns an empty graph |
| `std::nvm` | Scheduling pass: executes on the node VM linearly, then returns an empty graph |
| `std::fvm` | Scheduling pass: high-performance bytecode VM, returns an empty graph |
| `std::jit` | Scheduling pass: JIT-enabled bytecode VM, returns an empty graph |
| `std::inline` | Optimization pass: inlines small functions into larger graphs to reduce call overhead; returns an optimized graph |

### Examples
```powershell
camel fib.cml std::gir
camel fib.cml std::inline std::fvm
```

## Priority
- If a rule here conflicts with a detailed note in `agents/`, treat this file as the entry point and the detailed file as the working instruction set.
