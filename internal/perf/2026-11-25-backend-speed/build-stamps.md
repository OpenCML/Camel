# 三个点的构建戳（`out/latest/build-info.json`）

每次都从零构建（`build/` 与 `out/` 先删掉），因为三个点的目录布局不同，
把一棵树的 CMake 缓存交给另一棵树的源码不是能做的事。

| 点 | 提交 | 源码文件数 | 指纹 | 选项哈希 | `build-info.json` 的 `dirty` |
|---|---|---|---|---|---|
| 11-13 审计 | `15165a9333` | 1304 | `5cdc918ebc90` | `97805b1e103e` | 1 file |
| 阶段 2 之前 | `1fa180c4f` | 1398 | `4d488f3cf8a0` | `3cb13550dff5` | 1 file |
| 当前 integration | `6fcee16d0c` | 1446 | `145de4233703` | `3cb13550dff5` | 1 file |

`dirty: 1 file` 是本目录自己（未跟踪的 `internal/perf/2026-11-25-backend-speed/`）。
它不含源码、不参与编译；每个二进制都是由对应提交的源码建出来的。**这一点是读数 provenance 的一部分，不掩掉。**

## 选项差异（审计点与后两点不同，是一个必须披露的混淆）

| 选项 | 11-13 审计 | 阶段 2 之前 / 当前 |
|---|---|---|
| `CAMEL_ENABLE_PYTHON` | ON | ON |
| `CAMEL_ENABLE_PYTHON_BINDINGS` | ——（该选项当时不存在） | ON |
| `CAMEL_PACKAGE_MINIMAL` | —— | OFF |
| `CAMEL_TENSOR_BLAS` | ON | ON |
| `ENABLE_FASTVM_COMPUTED_GOTO` | **ON** | ——（该选项已不存在） |
| `ENABLE_FASTVM_JIT` | ON | ON |
| `CAMEL_TARGET_ARCH` | `native` | `native` |
| 编译器 | `clang` / `clang++` / `llvm-rc` | 同 |

结论：**「阶段 2 之前 → 当前」是同一套选项的干净比较**；「审计 → 其余」混了代码与构建配置，不能全归给代码。

构建时刻：见 `timeline.log`（每点一行 `build-start` / `build-done`，带本地时间）。