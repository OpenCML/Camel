# 构建戳（`out/latest/build-info.json`）

每次都从零构建（`build/` 与 `out/` 先删掉），因为这些点的目录布局不同，
把一棵树的 CMake 缓存交给另一棵树的源码不是能做的事。

| 点 | 提交 | 源码文件数 | 指纹 | 选项哈希 | `build-info.json` 的 `dirty` |
|---|---|---|---|---|---|
| 11-13 审计 | `15165a9333` | 1304 | `5cdc918ebc90` | `97805b1e103e` | 1 file |
| 阶段 2 之前 | `1fa180c4f` | 1398 | `4d488f3cf8a0` | `3cb13550dff5` | 1 file |
| 11-25 integration | `6fcee16d0c` | 1446 | `145de4233703` | `3cb13550dff5` | 1 file |
| 11-26 重测的 `before` | `6fcee16d0c` | 1446 | `145de4233703` | `3cb13550dff5` | 1 file |
| 11-26 重测的 `tip` | `c542b0666`（代码 = `250346d8d`） | 1455 | `50d650537e5c` | `3cb13550dff5` | 0 |

后两行是 2026-11-26 重定基那一轮（`reading-2026-11-26.cn.md`）：同一台机器上又各从零构建了一次。
`6fcee16d0c` 重来的指纹与 11-25 那次一字不差，说明重测的就是同一套源码；`tip` 那一行多出的九个源码文件
是 11-25 之后进 `origin/integration` 的。`tip` 那一次的 `build-info.json` 被后一次构建覆盖了，
表里那几列是构建当时读下的值。

`dirty: 1 file` 是本目录自己（未跟踪的 `internal/perf/2026-11-25-backend-speed/`）。
它不含源码、不参与编译；每个二进制都是由对应提交的源码建出来的。**这一点是读数 provenance 的一部分，不掩掉。**

## 选项差异（审计点与后两点不同，是一个必须披露的混淆）

| 选项 | 11-13 审计 | 阶段 2 之前 / 11-25 / 11-26 的两点 |
|---|---|---|
| `CAMEL_ENABLE_PYTHON` | ON | ON |
| `CAMEL_ENABLE_PYTHON_BINDINGS` | ——（该选项当时不存在） | ON |
| `CAMEL_PACKAGE_MINIMAL` | —— | OFF |
| `CAMEL_TENSOR_BLAS` | ON | ON |
| `ENABLE_FASTVM_COMPUTED_GOTO` | **ON** | ——（该选项已不存在） |
| `ENABLE_FASTVM_JIT` | ON | ON |
| `CAMEL_TARGET_ARCH` | `native` | `native` |
| 编译器 | `clang` / `clang++` / `llvm-rc` | 同 |

结论：**「阶段 2 之前 → 11-25 / 11-26」是同一套选项的干净比较**；「审计 → 其余」混了代码与构建配置，不能全归给代码。
11-26 两点的选项哈希与 11-25 的相同（`3cb13550dff5`），三者的差只有代码。

一处构建环境的事实（不是选项）：2026-11-26 的两次 configure 都报
`CAMEL_TENSOR_BLAS: no CBLAS found; the builtin GEMM is the library path`，即这台机器上没找到 CBLAS。
11-25 那三次构建只留了尾巴日志，configure 那一行不在其中，所以本页不声明它当时找到过 CBLAS；
`tensor_train` 那一行的绝对值要在这条下读。

构建时刻：11-25 的三次见 `timeline.log`（每点一行 `build-start` / `build-done`，带本地时间）；
11-26 两次的 `builtAt`（UTC）分别是 `08:32:00`（`before`）与 `08:22:42`（`tip`），
文件在 `results/build-info-before-6fcee16d0c.json` 与 `results/build-tip-250346d8d.tail.log`。