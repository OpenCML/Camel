# Agent Development Guide

## 1. Environment and Command Conventions
- Runtime shell: Windows PowerShell.
- Use `;` to chain commands. Do **not** use `&&`.

## 2. Build Policy
- Standard build: `npm run build`
- Debug build: `npm run debug`
- Profiling build: `npm run profile`
- Build artifacts: `out/latest/bin/`; executable: `camel.exe`
- Never modify or replace build artifacts manually.
- Use only the commands above. They handle artifact synchronization and ensure you run the latest binaries.

## 3. Runtime Prerequisites
- Required dynamic library: `libcamel.dll`
- `CAMEL_HOME` must point to the installation root (including `bin`, `lib`, etc.).
- Environment variable roles:
  - `CAMEL_HOME`: install root (used by module and DLL search fallback).
  - `CAMEL_STD_LIB`: override stdlib location.
  - `CAMEL_PACKAGES`: extra module search roots (`;` separated on Windows).
- Python module runtime (Windows):
  - Preferred runtime DLL drop-in path: `modules/python/dlls/` (gitignored).
  - Fallback runtime discovery uses active `VIRTUAL_ENV` / `CONDA_PREFIX`.
  - Python SDK root for build: `modules/python/sdks/` (gitignored).
  - SDK sync is manual: `node scripts/sync-python-sdks.js <python-archive-root>`.
  - Build uses SDKs first; if SDK root is missing, falls back to active virtual environment.

**PowerShell example:**
```
$env:CAMEL_HOME = "project-root\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
```

### Logging (CLI and builds)
- **Default**: no `-l` / `-v*` flags → library global threshold is **`fatal`**; logs go to **stderr** when enabled.
- **Levels** (CLI `--log-level`): `fatal`, `warn`, `info`, `debug`, `trace`, `off`. Shortcuts: `-v` → `warn`, `-vv` → `info`, `-vvv` → `debug`, `-vvvv` → `trace`.
- **Scope filtering**: `--log-preset none|wall|extra` and `--log-include a,b,c` (comma-separated scope prefixes). See [`docs/cli.md`](docs/cli.md).
- **Release vs debug build**: in **`npm run build` (NDEBUG)**, `CAMEL_LOG_DEBUG` / `CAMEL_LOG_TRACE` (and `_S` variants) are compiled out—no formatting, no runtime cost. Use **`npm run debug`** to exercise those sites.
- For high-call-volume workloads (for example, `fib 30`), low thresholds (`trace` / `debug`) can produce huge output and may stall execution or overflow the console.
- Logging flags must appear **before** the target file. Arguments after the target file are interpreted as passes.
  - Invalid: `camel fib.cml -v`
  - Valid: `camel -vv std::gir fib.cml`

**Recommended practice:**
- Prefer `build` mode for routine verification; raise threshold only when diagnosing (`-vv`, `--log-include`, etc.).
- During debugging, start with small inputs (for example, `fib 3`, `fib 4`) and scale incrementally.
- Use `Select-Object` when output throttling is required.

## 4. Pass Execution Model
- Canonical syntax: `camel xxx.cml pass1 pass2 ...`
- `std::nvm` is the fallback pass:
  - when no pass is specified, or
  - when specified passes complete without yielding an empty graph,
  - execution continues in `std::nvm`.

### Common Passes

| Pass | Description |
|------|-------------|
| `std::gir` | Translation pass: prints the current GIR and returns an empty graph |
| `std::nvm` | Scheduling pass: executes on the node VM linearly, then returns an empty graph |
| `std::fvm` | Scheduling pass: high-performance bytecode VM (faster than `nvm`), returns an empty graph |
| `std::jit` | Scheduling pass: JIT-enabled bytecode VM (fastest), returns an empty graph |
| `std::inline` | Optimization pass: inlines small functions (subgraphs) into larger graphs to reduce call overhead; returns an optimized graph |

### Examples
```
camel fib.cml std::gir               # Print GIR only
camel fib.cml std::inline std::fvm   # Inline first, then execute on high-performance VM
```

## 5. Agent Engineering Standards
- Prefer TDD: write tests before implementation whenever feasible.
- Place test cases under `test/`.
- For substantial refactors, update `docs/` in the same change set.
- Add comments for non-trivial or opaque logic, explicitly documenting intent and critical constraints.

## 6. Development Preferences and Quality Bar
- Target language standard: C++23. Prefer modern C++ idioms and features where appropriate.
- Maintain strong commentary (in English) and documentation discipline. Aim for approximately 30% comment density in new code; do not mirror legacy under-commented areas.
- Favor elegant, correctness-first designs. Avoid short-term bypasses introduced solely to minimize code churn.
- Follow industrial-grade best practices with a long-term perspective; refactor proactively and frequently to prevent technical debt accumulation.
- This project is currently internal-only. Unless explicitly required, do not optimize for forward compatibility. Prioritize cleanliness and correctness; avoid dual-track APIs.
- Escalate fundamental design conflicts or ambiguous trade-offs early. Record decisions and rationale in both documentation and code comments.
- Every implementation plan must define explicit acceptance criteria and validate against them during execution.
- If a single file grows beyond 800 lines, evaluate decomposition. New `.h/.cpp` files must include a standard file header (copyright notice, aligned with existing files), followed by a dedicated multi-line comment describing file responsibilities.
- Enforce strict declaration/implementation separation. Avoid implementation logic in header files unless strictly necessary. Keep headers in `include/` and implementations in `src/`.