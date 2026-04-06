# camel-lang PyPI 打包说明

本文档描述 **`npm run pypi:rollup`** 如何将原生 Camel 产物打成 **`camel-lang` wheel**，以及与日常 **`npm run build`** 在 Python / `py_bridge` 行为上的区别。

---

## 1. 涉及路径与脚本

| 路径 / 脚本 | 作用 |
|-------------|------|
| `pypi/camel-lang/` | setuptools 包根目录：`pyproject.toml`、`src/camel/`（打包时写入 `.cmlenv` 与 `wrapper-dist/`） |
| `scripts/pypi-rollup.js` | 编排：默认按**当前 `python`** 匹配一个 SDK 构建；可选 `CAMEL_PYPI_ALL_SDKS=1` 按目录循环；收集 `out/<tag>/`、打 wheel、写入 `dist/pypi/` |
| `scripts/build.js` | 被 rollup 以「单 SDK 模式」调用；被 `npm run build` 以「普通模式」调用 |
| `scripts/sync-python-sdks.js` | 将官方 Python 目录树同步到 `modules/python/sdks/python3xx/`（gitignored） |
| `modules/python/sdks/` | 多版本 Python 开发树（`include/`、`libs/`、可选 `python.exe` / `bin/python3`） |
| `dist/pypi/` | rollup **最终 wheel 输出目录**（每次完整 rollup 会先清空其中的 `*.whl` 再写入） |
| `out/latest/`（或 `CAMEL_PYPI_TAG`） | `collect-out` 产物根；rollup 将其整树拷入包内 `.cmlenv` |

---

## 2. 两种构建模式（务必区分）

### 2.1 日常开发：`npm run build`

- **不**设置环境变量 `CAMEL_PYPI_SINGLE_SDK_MM`。
- CMake 对 `modules/python/sdks/` 下**每一个**合法的 `python3xx` 目录各生成一个 **`py_bridge{xx}`**（例如 `py_bridge310`、`py_bridge312`），便于在本机/离线包中一次携带多版本桥接库。
- `python.cmo` 的链接仍按现有逻辑在 venv / 多 SDK 中选一个主解释器（与多 bridge 并存的设计一致）。

**用途**：本地开发、完整功能验证、非 PyPI 的整包分发。

### 2.2 PyPI 专用：`npm run pypi:rollup`

**默认（推荐）**：按 **当前编译用的解释器** 只打 **一只** wheel。

1. 用 **`CAMEL_PYPI_PYTHON`**（若设置）或依次尝试 **`python` / `python3`**（激活的 venv 即为此时的「编译环境」）得到 `sys.executable` 与 `version_info`。
2. **优先**：在 `modules/python/sdks/python3*/` 中查找 **`patchlevel.h` 与上述版本一致** 的目录（与同步进来的官方树一致）。**若无匹配**：用解释器自带的开发文件——**`sysconfig.get_path('include')`** 与 **`sys.base_prefix` 下的 `libs`/`Libs`（Windows）或 `LIBDIR`（Unix）**，无需单独的 `sdks/` 副本；venv 一般仍指向基座安装的 `Include`/`Libs`。
3. 文件夹后缀 `mm`（或从版本推导的 `312`/`39` 等）写入 **`CAMEL_PYPI_SINGLE_SDK_MM`**，CMake **只**生成对应的 **`py_bridge`**。
4. **`build({ rollupMm, pythonRootDir, pythonExecutable, cleanBuildDir: true })`**：**删除整个 `build/`** 再配置 / 编译，避免 CMake 缓存串版本。
5. wheel 的 **PEP 425** `python` / `abi` / `platform` 均由该解释器上运行的 **`packaging.tags.sys_tags()`** 决定（与 pip 一致），**不是**硬编码版本号。

**多 SDK 一次性打全量 wheel**（本机需装齐各版本运行时）：设置 **`CAMEL_PYPI_ALL_SDKS=1`**，则按目录枚举，**每个**合法 `python3xx` 一轮构建 + 一只 wheel（与旧行为一致）。

**用途**：CI matrix「每 job 一个 Python」与本地「当前 venv 打一只 wheel」对齐；避免在装有多个 `sdks/` 时误跑六轮构建。

---

## 3. 环境变量（PyPI rollup）

| 变量 | 作用 |
|------|------|
| **`CAMEL_PYPI_PYTHON`** | 可选。指向用于探测「编译时版本」的解释器路径；不设则用 `python` / `python3`。 |
| **`CAMEL_PYPI_ALL_SDKS`** | 设为 `1` / `true` / `yes` 时，在存在多个 `sdks/python3xx/` 时**逐个 SDK 打 wheel**（每轮需能解析到对应版本的运行时）。默认**不**设：只打与当前解释器匹配的 **一只** wheel。 |
| **`CAMEL_PYPI_SINGLE_SDK_MM`** | **由脚本内部**通过 `build({ rollupMm })` 设置（对应选中的 `sdks/python<mm>/`）。不要在 shell 里长期 `export`，否则普通 `npm run build` 也会被收窄。 |
| **`CAMEL_PYPI_SDK_ROOT`** / **`CAMEL_PYPI_PY_*`** / **`CAMEL_PYPI_PYTHON_EXECUTABLE`** | **由 `build.js` 内部**在「用解释器安装树、非 `modules/python/sdks/python*`」时设置，供 `camel_module.cmake` / `CMakeLists.txt` 使用；无需手设。 |

| 场景 | `CAMEL_PYPI_SINGLE_SDK_MM` |
|------|---------------------------|
| **仅** `pypi:rollup` → `build.js` 传入 `rollupMm` | 例如 `312`，CMake 只保留 `py_bridge` 与 `python<mm>` 一致的那一项。 |
| 普通 `npm run build` | 不设置，**所有** `py_bridge*` 均参与构建。 |

---

## 4. SDK 目录约定

rollup 扫描 `modules/python/sdks/`，仅当同时满足：

- 子目录名匹配 **`python3\d+`**（如 `python310`、`python312`）；
- 存在 **`include/Python.h`**；
- 存在 **`libs/`**（Windows 下为 `.lib` 等）；

才视为一个有效 SDK。

**解释器可执行文件**（用于 `python -m build` / `wheel tags`，与链接用头文件/库一致）按顺序查找：

1. `<sdk>/python.exe`（Windows 常见布局）
2. `<sdk>/bin/python3`
3. `<sdk>/bin/python`

若均不存在，该 SDK 轮次会 **失败退出**；请通过 `sync-python-sdks` 或手工补齐树。

**Windows**：`libs/` 下需有 **带版本号的导入库**（如 `python39.lib`，对应 3.9）或回退用的 `python3.lib`。仅拷贝 embeddable 包、缺少 `.lib` 时，CMake 的 `FindPython` 无法完成 `Development` / `Development.Embed`。本仓库在 `camel_module.cmake` 中对「仅 SDK 树」场景会设置 `Python_FIND_REGISTRY NEVER` 并显式指向 `include/` 与 `pythonXY.lib`，避免误用注册表里其它 Python；若仍失败，请核对 `libs` 是否完整。

**无 SDK 时的回退**（例如 CI 仅有 `actions/setup-python`、未同步 `sdks/`）：

- rollup 只跑 **一轮** `build()`（不删 `build/`、不设 `CAMEL_PYPI_SINGLE_SDK_MM`）；
- wheel 的 `cp` / `abi` / `platform` 全部由 **`packaging.tags`** 对 **PATH 上的 `python`** 解析。

---

## 5. 单次「按 SDK 打包」流水线（rollup 内每一轮）

默认仅 **一轮**（当前解释器 ↔ 一个 `sdks/` 树）。`CAMEL_PYPI_ALL_SDKS=1` 时对**每个** SDK 各一轮。无 `sdks/` 时仍为单轮（PATH Python）。每轮顺序为：

1. **`build({ rollupMm, pythonRootDir, pythonExecutable, cleanBuildDir: true })`**  
   - 传入 CMake：`-DPython3_ROOT_DIR`、`-DPython_EXECUTABLE` 等（见 `build.js`）。  
   - `collect-out` 在 `build.js` 末尾以 `Release` 执行，更新 **`out/latest/`**（或 `CAMEL_PYPI_TAG` 指定 tag）。

2. **组装 wheel 内容**（`packageWheelBundle`）  
   - 将 `out/<tag>/` **整树**复制到 `pypi/camel-lang/src/camel/.cmlenv/`。  
   - 在 `wrapper-dist/` 生成启动脚本（Windows：每个 `bin` 下 `.exe` 对应一个 `.cmd`；Unix：对应 shell 包装）。  
   - 临时改写 `pyproject.toml` 中的 `script-files`，指向上述 wrapper。

3. **构建与改标签**  
   - 使用该 SDK 的 **`python`**（或回退时的 `python`）执行：  
     `pip install build wheel packaging` → `python -m build --wheel`（先得到 **`py3-none-any` 占位 wheel**，属 setuptools 常态）。  
   - 使用 **`wheel tags`** 写入正确三元组：  
     `--python-tag cp<mm> --abi-tag cp<mm> --platform-tag <plat>`  
     其中 `<plat>` 在无多 SDK回退时同样来自 `packaging.tags`。

4. **输出与清理**  
   - 将本轮生成的 `*.whl` **复制到** `dist/pypi/`（多 SDK 时文件名不同，互不覆盖）。  
   - 删除 `pypi/camel-lang` 下临时 `dist/`、`build/`、`.cmlenv`、`wrapper-dist`、`*.egg-info`，并恢复 `pyproject.toml`。

**整次 rollup 开始前**：会删除 `dist/pypi/` 下所有已有 `*.whl`，保证本次产物集合干净、可预期。

---

## 6. Wheel 与 pip 的对应关系

- **`cp312-cp312-win_amd64`** 这类名字表示：仅应在 **CPython 3.12 + win_amd64** 上由 pip 选用；**不能**在 3.11 / 3.13 上替代使用（除非另有对应 wheel 或源码安装）。
- 多版本支持 = **多个 wheel 文件**（每个 `python3xx` 一轮构建），而不是单个 `py3-none` 包内混装多 ABI。

---

## 7. 与 GitHub Actions 的关系

工作流 **`.github/workflows/build-pypi.yml`** 在矩阵 job 内执行 `npm run pypi:rollup`。典型 CI 镜像 **未** 提交 `modules/python/sdks/`，因此走 **「无 SDK 回退」**：每个 job 用 `setup-python` 的单一版本打 **一个** 与该解释器一致的 wheel。

若要在 CI 中改为「按 sdks 多 wheel」，需在 job 中准备 `sdks/` 目录（下载 / 缓存 / 私有制品），并理解构建时间与磁盘成本会显著增加。

发布侧可使用 **Trusted Publishing（OIDC）**；仓库内 workflow 注释中有环境与 PyPI 配置要点。

---

## 8. 相关命令速查

```powershell
# 同步 Windows 官方 Python 多版本到 sdks（示例）
node scripts/sync-python-sdks.js D:\path\to\PythonArchivesRoot

# 日常完整原生构建（多 bridge）
npm run build

# 生成 PyPI wheel（多 SDK → 多个 wheel；无 SDK → 一个 wheel）
npm run pypi:rollup
```

更通用的构建约定见 [`AGENTS.md`](../AGENTS.md)；Python 运行时收集与 `collect-out` 行为见 [`cpp_export_build.md`](cpp_export_build.md) 第 6 节。
