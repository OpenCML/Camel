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

**默认（也是唯一的模式）**：按 **当前编译用的解释器** 做 **一次** 原生构建，出 **两只** wheel（`cp310-abi3` 本体+互操作、`py3-none` 只有本体，见第 6 节）。

1. 用 **`CAMEL_PYPI_PYTHON`**（若设置）或依次尝试 **`python` / `python3`**（激活的 venv 即为此时的「编译环境」）得到 `sys.executable` 与 `version_info`。
2. **优先**：在 `modules/python/sdks/python3*/` 中查找 **`patchlevel.h` 与上述版本一致** 的目录（与同步进来的官方树一致）。**若无匹配**：用解释器自带的开发文件——**`sysconfig.get_path('include')`** 与 **`sys.base_prefix` 下的 `libs`/`Libs`（Windows）或 `LIBDIR`（Unix）**，无需单独的 `sdks/` 副本；venv 一般仍指向基座安装的 `Include`/`Libs`。
3. 文件夹后缀 `mm`（或从版本推导的 `312`/`39` 等）写入 **`CAMEL_PYPI_SINGLE_SDK_MM`**，CMake **只**生成对应的 **`py_bridge`**。
4. **`build({ rollupMm, pythonRootDir, pythonExecutable, cleanBuildDir: true })`**：**删除整个 `build/`** 再配置 / 编译，避免 CMake 缓存串版本。
5. wheel 的 **PEP 425** 三元组：`platform` 取自构建机的 `sysconfig.get_platform()`（随后由 `auditwheel repair` / `delocate` 改成可发布的标签），`python` / `abi` 是决定里的两个常量——互操作那只 `cp310` + `abi3`，只有本体那只 `py3` + `none`。**不再**取 `packaging.tags.sys_tags()` 的第一项（0.2.31 的 `linux_x86_64` 就是那么来的），也不再按解释器版本命名编译出来的扩展。

**`CAMEL_PYPI_ALL_SDKS=1` 已作废**：稳定 ABI 下一只扩展覆盖 3.10+，按 SDK 各打一轮只会产出重名的 wheel；脚本打印一行警告后仍按「当前编译解释器」打一轮。

**用途**：CI matrix「每 job 一个 Python」与本地「当前 venv 打一只 wheel」对齐；避免在装有多个 `sdks/` 时误跑六轮构建。

---

## 3. 环境变量（PyPI rollup）

| 变量 | 作用 |
|------|------|
| **`CAMEL_PYPI_PYTHON`** | 可选。指向用于探测「编译时版本」的解释器路径；不设则用 `python` / `python3`。 |
| **`CAMEL_PYPI_ALL_SDKS`** | 已作废（稳定 ABI 下一只扩展覆盖 3.10+）：设成 `1` / `true` / `yes` 只打印一行警告，仍按当前编译解释器打一轮。 |
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
- 存在 **`include` 目录下的 `Python.h`**；
- 存在 **`libs/`**（Windows 下为 `.lib` 等）；

才视为一个有效 SDK。

**解释器可执行文件**（用于 `python -m build`，与链接用的头文件/库一致）：rollup 用 **编译时解释器**——`CAMEL_PYPI_PYTHON`，
否则 `python` / `python3`——并由 `resolveRollupPythonExecutable`（`build.js`）在 `VIRTUAL_ENV` / `CONDA_PREFIX` / `PATH` 之间回落。
都没有时该轮 **失败退出**；请通过 `sync-python-sdks` 或手工补齐树。稳定 ABI 之后这个解释器只决定两件事：嵌入模块
`python.cmo` 链哪个 `libpython`，以及 `python -m build` 用哪个 Python（wheel 的名字与它无关，见第 6 节）。

**Windows**：`libs/` 下需有 **带版本号的导入库**（如 `python39.lib`，对应 3.9）或回退用的 `python3.lib`。仅拷贝 embeddable 包、缺少 `.lib` 时，CMake 的 `FindPython` 无法完成 `Development` / `Development.Embed`。本仓库在 `camel_module.cmake` 中对「仅 SDK 树」场景会设置 `Python_FIND_REGISTRY NEVER` 并显式指向 `include/` 与 `pythonXY.lib`，避免误用注册表里其它 Python；若仍失败，请核对 `libs` 是否完整。

**无 SDK 时的回退**（例如 CI 仅有 `actions/setup-python`、未同步 `sdks/`）：

- rollup 只跑 **一轮** `build()`（不删 `build/`、不设 `CAMEL_PYPI_SINGLE_SDK_MM`）；
- wheel 的 `cp` / `abi` / `platform` 全部由 **`packaging.tags`** 对 **PATH 上的 `python`** 解析。

---

## 5. 单次「按 SDK 打包」流水线（rollup 内每一轮）

永远只有 **一轮**（当前解释器 ↔ 一个 `sdks/` 树；无 `sdks/` 时仍为单轮，用 PATH 上的 Python）。这一轮出两只 wheel，顺序为：

1. **`build({ rollupMm, pythonRootDir, pythonExecutable, cleanBuildDir: true })`**  
   - 传入 CMake：`-DPython3_ROOT_DIR`、`-DPython_EXECUTABLE` 等（见 `build.js`）。  
   - `collect-out` 在 `build.js` 末尾以 `Release` 执行，更新 **`out/latest/`**（或 `CAMEL_PYPI_TAG` 指定 tag）。

2. **组装 wheel 内容**（`packageWheelBundle`）  
   - 将 `out/<tag>/` **整树**复制到 `pypi/camel-lang/src/camel/.cmlenv/`。  
   - 在 `wrapper-dist/` 生成启动脚本（Windows：每个 `bin` 下 `.exe` 对应一个 `.cmd`；Unix：对应 shell 包装）。  
   - 临时改写 `pyproject.toml` 中的 `script-files`，指向上述 wrapper。

3. **构建与改标签**（同一份 packaging 打两遍，内容差一个 `.cmlenv/python/`）  
   - 使用该 SDK 的 **`python`**（或回退时的 `python`）执行：  
     `pip install build wheel packaging` → `python -m build --wheel`（先得到 **`py3-none-any` 占位 wheel**，属 setuptools 常态）。  
   - 用 **`scripts/wheel_retag.py`** 写入三元组（`wheel tags` 在 wheel 0.45 里去掉了命令行），两次：  
     互操作那只 `--python-tag cp310 --abi-tag abi3`，只有本体那只 `--python-tag py3 --abi-tag none`；  
     `--platform-tag` 在 Linux 上先写 `linux_<arch>`，随后由 `auditwheel repair` 改成 `manylinux_2_28_*`。
   - 互操作那只的 `.cmlenv/python/` 里必须有 `_camel.abi3.<ext>`（脚本自己查，缺了直接失败）。

4. **输出与校验**  
   - 将两只 `*.whl` **复制到** `dist/pypi/`。  
   - 末尾跑 `scripts/check_wheel_tags.py --ruling dist/pypi/*.whl`：确认两只形态齐全、名字与内容一致（0.2.31 那种错误的标签在
     出 wheel 的这一步就失败，而不是等到索引拒收）。
   - 删除 `pypi/camel-lang` 下临时 `dist/`、`build/`、`.cmlenv`、`wrapper-dist`、`*.egg-info`，并恢复 `pyproject.toml`。

**整次 rollup 开始前**：会删除 `dist/pypi/` 下所有已有 `*.whl`，保证本次产物集合干净、可预期。

---

## 6. Wheel 与 pip 的对应关系

一次原生构建出一个平台的**两只** wheel，名字直接说清 pip 该怎么挑（2026-10-25 的决定，2026-11-02 落地；
`docs/handover/README.cn.md` 第 7 条、评审索引 C28）：

| 文件名 | 内容 | pip 在哪些解释器上选它 |
|--------|------|------------------------|
| `camel_lang-<ver>-cp310-abi3-<平台>.whl` | 本体 + 互操作扩展 `_camel.abi3.<ext>` | CPython **3.10 及以后**的每一个版本 |
| `camel_lang-<ver>-py3-none-<平台>.whl` | 只有本体（不含 `.cmlenv/python/`） | 其余任何 Python 3：3.9 及更早、PyPy、自由线程版 |

- `abi3` 是 PEP 384 的稳定 ABI：扩展用 `Py_LIMITED_API=0x030A0000` 编（`bindings/python/CMakeLists.txt` 的
  `Python3_add_library(... USE_SABI 3.10)`），所以一只文件覆盖 3.10 以后的**所有**版本，包括还没发布的。
  **`cp310` 是稳定 ABI 的下限，不是"编它时的解释器"**：Linux 上构建用的是 `python3.11`，出来的文件名仍是 `cp310-abi3`。
- `py3-none` 那只**必须**没有扩展，`cp310-abi3` 那只**必须**有；`scripts/check_wheel_tags.py --ruling` 查文件名也查文件内容。
- 嵌入方向（`modules/python` 的 `python.cmo`/`pyplot.cmo`，pybind11 嵌入）仍按构建解释器链接 `libpython`，它与互操作
  扩展无关，也不改任何 wheel 的名字（见 7.1）。

---

## 7. 与 GitHub Actions 的关系

工作流 **`.github/workflows/build-pypi.yml`** 在矩阵 job 内执行 `npm run pypi:rollup`。典型 CI 镜像 **未** 提交 `modules/python/sdks/`，因此走 **「无 SDK 回退」**：每个 job 用 `setup-python` 的单一版本编一次，出该平台的两只 wheel（`cp310-abi3` + `py3-none`）。

### 7.1 各平台 interop wheel 的 CPython 版本（3.10+，一只）

`cp310-abi3-<platform>` 这只 wheel 里的 `_camel` 扩展是用**稳定 ABI** 编的，所以它不再与构建用的解释器版本绑定：
Linux job 用 `/usr/bin/python3.11`（`dnf install python3.11-devel python3.11-pip`）编，文件名照样是 `cp310-abi3`，
3.10、3.12、3.13、3.14 都能装。

构建机上那个解释器仍然**必须**有，因为 `python.cmo` 用 `pybind11::embed` 把解释器嵌进 Camel，CMake 的
`Development.Embed` 只认**共享** libpython：

- 镜像自带的 `/opt/python/cp3*` 是**静态**解释器（`Py_ENABLE_SHARED=0`，`lib/` 下没有 `libpython3.x.so`），
  用 `/opt/python/cp310-cp310` 会在编译前就停：`Could NOT find Python3 (missing: Python3_LIBRARIES Development.Embed)`。
- 该频道**没有 python3.10 包**：`dnf list --available 'python3.10*'` → `Error: No matching Packages to list`。

这两条以前把 Linux 的 interop wheel 钉在 `cp311`、让 Linux 上的 3.10 只能装 `py3-none`；现在它们只影响"用哪个解释器编"，
与文件名无关。

`bindings/python` 用的是**原始 C API**（没有 pybind11），`-DPy_LIMITED_API=0x030A0000` 下要改的只有 3 处 `PyTuple_*`
宏和 3 处 `PyTypeObject` 成员访问（`tp_name`、`tp_alloc`、`tp_free`），已全部换成函数形式（`PyTuple_Size` /
`PyTuple_GetItem` / `PyTuple_SetItem`、`PyObject_GetAttrString(__name__)`、`PyType_GenericAlloc`、`PyObject_Free`）。
注意 `docs/pypi-packaging.md` 早先记的是"5 处"（3 处宏 + 2 处成员访问），实际成员访问是 3 处，共 6 处；数量漂移本身记在这里。

`modules/python` 那一侧**改不了**：pybind11 不支持受限 API（`buffer_info.h` 里 `Py_buffer` 不可见），`-DPy_LIMITED_API`
直接编译失败——这是 2026-10-25 决定之前记下的读数，本次没有动 `modules/python`，所以没有重测。决定对这种情况的出口是
"在报告里列出是哪几个、换成了什么写法"，而这里连"换写法"都不在 `bindings` 里：嵌入模块的接口由 pybind11 提供，不是
本文件能改的。结论：extension 是 abi3，`python.cmo` 仍只能对着某一个解释器编。

### 7.2 发布冒烟：每个平台三个解释器（清单）

决定要求一个平台只编一次，冒烟就要证明两件事：**3.10 起的每一个版本都能 `import camel`**，**其余解释器只拿到本体也能跑
`camel` 命令**。每一格都在干净环境里只 `pip install` wheelhouse 里的 wheel（`--no-index --find-links`，避免测到索引上的旧
版本），然后跑同一段脚本（`camel --version`、`camel hello.cml`、`camel hello.cml std::jit`，以及 `import camel`）。
失败任何一格即本次发布失败。

- [ ] **CPython 3.10 —— 稳定 ABI 的下限**：`python:3.10-slim`（Windows 用 `actions/setup-python` 的 `3.10`）。
      pip 必须选 `cp310-abi3` 那只；三条命令全过；`import camel` 成功；NumPy 数组经 `__dlpack__` 过界一次
      （`rules.double_totals`）。
- [ ] **当前最新的 CPython —— 互操作必须能用**：`python:3.14-slim`（2026-10-25 作决定时最新稳定版是 3.14；发布当天若已有
      更新的稳定版，就用那一版，并把 3.14 留在矩阵里）。pip 仍应选同一只 `cp310-abi3`（这一格就是 abi3 的全部意义）；
      三条命令 + `import camel` + 数组过界全过。
- [ ] **只拿到本体的解释器 —— CPython 3.9**：`python:3.9-slim`。pip 选 `py3-none` 那只；三条命令全过；`import camel`
      **必须**失败，且 ImportError 里同时出现 `interop wheel` 与 `py3-none`（说明装的是哪只、需要 CPython 3.10+），
      不是「找不到扩展模块」。PyPy 3.10 是同一格的替代（`pypy:3.10`；它的 pip 是 `pypy3 -m pip`，工作流目前只跑 3.9）。

工作流里对应的三格是 `smoke-linux` 的 `python:3.10-slim` / `python:3.14-slim` / `python:3.9-slim`；Windows 那一腿在
build job 内用 `setup-python` 的 3.10 做第一格（互操作）。

推送 `v*` / `V*` 版本 tag 后，所有矩阵构建和 wheel 安装检查成功，工作流会自动发布到 **PyPI**。手动运行工作流时，`publish_to` 仍可选择 `none`（仅构建，默认）、`pypi` 或 `testpypi`；TestPyPI 只通过手动选择发布。

发布使用 **Trusted Publishing（OIDC）**，无需在 GitHub 中保存长期 PyPI token。正式发布前需检查：

- GitHub 仓库的 `pypi` environment 允许版本 tag 部署；若配置了 required reviewers，发布会等待人工审批。
- PyPI 的 `camel-lang` 项目在 **Manage → Publishing** 中配置 GitHub Actions publisher：owner 为 `OpenCML`，repository 为 `Camel`，workflow filename 为 `build-pypi.yml`，environment 为 `pypi`。
- 如需发布到 TestPyPI，在 TestPyPI 单独配置同样的 publisher，并将 environment 改为 `testpypi`；GitHub 中也使用对应 environment。

workflow 的发布 job 已声明 `id-token: write`。Trusted Publisher 的配置必须与工作流身份匹配；只有成功执行 OIDC 上传，才能验证整条发布链路。

---

## 8. 相关命令速查

```powershell
# 同步 Windows 官方 Python 多版本到 sdks（示例）
node scripts/sync-python-sdks.js D:\path\to\PythonArchivesRoot

# 日常完整原生构建（多 bridge）
npm run build

# 生成 PyPI wheel（一次原生构建 → cp310-abi3 与 py3-none 两只）
npm run pypi:rollup

# 只查已有 wheel 的标签：两只形态是否齐全、名字与内容是否一致
python3 scripts/check_wheel_tags.py --ruling dist/pypi/*.whl
```

更通用的构建约定见 [`AGENTS.md`](../AGENTS.md)；CMake 可选开关（如 `CAMEL_ENABLE_PYTHON`、`CAMEL_SKIP_PYTHON`）见 [`build-options.cn.md`](build-options.cn.md)；Python 运行时收集与 `collect-out` 行为见 [`cpp_export_build.md`](cpp_export_build.md) 第 6 节。
