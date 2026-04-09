# CMake 编译选项

[English](build-options.en.md) | 中文简体

Camel 将可配置的 CMake 选项集中在 `cmake/Options.cmake`，并在配置阶段打印 **`============ Build options: ============`** 区块，便于对照 CI 与本地构建。

## 选项来源

| 机制 | 说明 |
|------|------|
| **`cmake/options.json`** | 选项清单与默认值（供脚本与 `npm run config` 使用） |
| **`.camel-opts.json`** | 用户持久化覆盖；首次运行 `npm run config` 或构建脚本时会从 `options.json` 生成 |
| **`npm run config`** | 交互式切换 `ON`/`OFF` 并写入 `.camel-opts.json` |
| **`npm run build` / `debug` / `profile`** | 通过 `scripts/cmake-opts.js` 读取上述文件，向 CMake 传入 `-D<选项名>=ON` 或 `OFF` |

直接调用 CMake 时，也可手动传入 `-DENABLE_FASTVM_JIT=OFF` 等；未传的选项使用 `Options.cmake` 中的 `option()` 默认值。

## 当前选项一览

配置输出中的名称与下列一致（布尔值为 CMake 的 `ON` / `OFF`）。

### `ENABLE_FASTVM_JIT`

- **含义**：是否为 x64 目标启用 FastVM 的 JIT（机器码生成）。
- **注意**：非 x64 或选项为 `OFF` 时，配置日志会标明 `OFF`（含平台原因时另有说明）。
- **实现**：通过 `add_platform_option` 同时设置编译宏（如 `ENABLE_FASTVM_JIT`、`JIT_TARGET_X64`）。

### `ENABLE_FASTVM_COMPUTED_GOTO`

- **含义**：FastVM 解释器是否使用 **computed goto**（标签值调度）。
- **实现**：`add_bool_option`，向 `camel_options` 注入 `ENABLE_FASTVM_COMPUTED_GOTO=0|1`。

### `CAMEL_ENABLE_PYTHON`

- **含义**：是否构建 Python 嵌入相关产物：`python.cmo`、`pyplot.cmo`、`py_bridge*` 等（需要本机具备 Python **开发**布局：头文件与导入库；详见 `modules/python` 的探测逻辑）。
- **实现**：普通 `option()`，**不**通过 `add_bool_option` 注入全局 `CAMEL_ENABLE_PYTHON=0|1` 编译宏；仅控制是否 `add_subdirectory(modules/python)` 与（在开发环境可用时）`modules/pyplot`。
- **环境覆盖**：若设置 **`CAMEL_SKIP_PYTHON`** 为 `1`、`ON`、`YES`、`TRUE`（不区分大小写匹配规则以 `Options.cmake` 为准），配置阶段会将 **`CAMEL_ENABLE_PYTHON` 强制为 `OFF`**（写入缓存，优先级高于此前缓存值）；用于 CI 或无需 Python 模块的构建。

### 无 Python 开发环境时

- `modules/python` 在检测到无法构建 `python.cmo` 时会跳过；`CAMEL_PYTHON_DEV_AVAILABLE` 为假时，根工程不会添加 `modules/pyplot`，避免 `find_package(Python3 REQUIRED)` 失败。

## 与缓存的关系

CMake 会缓存布尔选项。若曾经关闭过 `CAMEL_ENABLE_PYTHON`，再次打开需要：

- 显式传入 `-DCAMEL_ENABLE_PYTHON=ON`，或  
- 在 `.camel-opts.json` 中改为 `"ON"`，或  
- 删除 `build/`（或相应构建目录）后重新配置。

仅设置 `CAMEL_SKIP_PYTHON` 为空或取消该环境变量**不会**自动把已缓存的 `OFF` 改回 `ON`；需按上一条之一刷新。

## 相关文档

- 根目录 **`AGENTS.md`**：构建命令约定、运行时环境与日志等（与 `CMake` 选项互补）。
- **`docs/npm-scripts.md`**：`npm run config` 与构建脚本说明。
- **`docs/pypi-packaging.md`**：PyPI wheel 与 Python SDK 布局（与嵌入模块构建策略不同处见该文）。
