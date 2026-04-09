# CMake build options

[中文简体](build-options.cn.md) | English

Configurable CMake options are centralized in `cmake/Options.cmake`. During configuration, Camel prints a **`============ Build options: ============`** block so local and CI builds are easy to compare.

## Where options come from

| Mechanism | Role |
|-----------|------|
| **`cmake/options.json`** | Canonical list and defaults (used by scripts and `npm run config`) |
| **`.camel-opts.json`** | Persistent user overrides; created from `options.json` on first `npm run config` or build script run |
| **`npm run config`** | Toggles `ON`/`OFF` interactively and writes `.camel-opts.json` |
| **`npm run build` / `debug` / `profile`** | Reads the above via `scripts/cmake-opts.js` and passes `-D<NAME>=<ON\|OFF>` to CMake |

When invoking CMake directly, you can pass `-DENABLE_FASTVM_JIT=OFF` etc.; unspecified options use the `option()` defaults in `Options.cmake`.

## Current options

Names match the configure log (booleans are CMake `ON` / `OFF`).

### `ENABLE_FASTVM_JIT`

- **Meaning**: Enable FastVM JIT (native code generation) on supported x64 targets.
- **Note**: On non-x64 or when disabled, the log shows `OFF` (with a platform note when applicable).
- **Implementation**: `add_platform_option` sets compile definitions such as `ENABLE_FASTVM_JIT` and `JIT_TARGET_X64`.

### `ENABLE_FASTVM_COMPUTED_GOTO`

- **Meaning**: Use **computed goto** (label-value dispatch) in the FastVM interpreter.
- **Implementation**: `add_bool_option`, injecting `ENABLE_FASTVM_COMPUTED_GOTO=0|1` on `camel_options`.

### `CAMEL_ENABLE_PYTHON`

- **Meaning**: Build Python embedding artifacts: `python.cmo`, `pyplot.cmo`, `py_bridge*`, etc. Requires a Python **development** layout (headers and import library; see `modules/python` probing).
- **Implementation**: Plain `option()` — **not** `add_bool_option`, so there is no global `CAMEL_ENABLE_PYTHON=0|1` preprocessor macro; it only controls `add_subdirectory(modules/python)` and, when dev is available, `modules/pyplot`.
- **Environment override**: If **`CAMEL_SKIP_PYTHON`** is set to `1`, `ON`, `YES`, or `TRUE` (see `Options.cmake` for the exact pattern), **`CAMEL_ENABLE_PYTHON` is forced to `OFF`** at configure time (cache write; overrides prior cache). Useful for CI or builds that do not need Python modules.

### No Python development layout

- If `modules/python` cannot build `python.cmo`, it skips; when `CAMEL_PYTHON_DEV_AVAILABLE` is false, the root `CMakeLists.txt` does not add `modules/pyplot`, avoiding a failing `find_package(Python3 REQUIRED)`.

## Interaction with the CMake cache

Booleans are cached. If `CAMEL_ENABLE_PYTHON` was turned off, re-enable it by:

- Passing `-DCAMEL_ENABLE_PYTHON=ON`, or  
- Setting `"ON"` in `.camel-opts.json`, or  
- Removing the `build/` tree (or your build directory) and reconfiguring.

Clearing or unsetting **`CAMEL_SKIP_PYTHON`** alone does **not** flip a cached `OFF` back to `ON`; use one of the steps above.

## See also

- **`AGENTS.md`** (repo root): build commands, runtime env (`CAMEL_HOME`, etc.), logging — complements CMake options.
- **`docs/npm-scripts.md`**: `npm run config` and build scripts.
- **`docs/pypi-packaging.md`**: PyPI wheel layout vs SDK embedding (where strategies differ).
