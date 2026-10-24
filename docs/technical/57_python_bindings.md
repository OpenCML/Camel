# 57. Python 绑定：数组经 DLPack 零拷贝进出、诊断变成带位置的异常、`pip install` 能装（H253）

H253 要的是"从 Python 里调用 Camel 模块的函数"这条采用路径：**在 Python 项目里用一部分 Camel**，
不是整体迁移。这一页记的是盘点、选定的最小端到端路径、实现、以及每一处结论的证据。

## 57.1 盘点：这一片之前，Python 侧能做什么

逐条都给了命令与逐字输出。基线提交是 `578d43f3b`（`origin/develop`）。

| 事项 | 基线状态 | 证据 |
|------|----------|------|
| `pip install` 的包 | 有包壳，没有可调用的东西 | `pypi/camel-lang/src/camel/__init__.py` 全文只有 `__version__` 与作者元数据 |
| CPython 扩展模块 | 没有 | `git ls-tree 578d43f3b -- bindings` 输出为空，`bindings/` 目录当时不存在 |
| 数组的 DLPack 通道 | **已有** | `include/camel/core/array/dlpack.h`、`src/core/array/dlpack.cpp` 的 `toDLPack` / `fromDLPack` |
| "按名字调用一个模块的导出函数"的 C++ 门 | **已有** | `DefinitionStore::libraryFunction`（`include/camel/runtime/definitions.h`），既有调用方是 `src/execute/camel_pass.cpp` |
| 从 Python 调用模块函数 | 没有 | 没有扩展模块，也没有任何 Python 侧代码引用 `libraryFunction` |
| 诊断/故障变成 Python 异常 | 没有 | 同上 |
| ONNX 方向的 py_bridge | 有，但方向相反 | `modules/python/` 是 Camel 调 Python / 导出 ONNX 用的桥，不是 Python 调 Camel |

缺口的位置很清楚，也不在 C API 上：**运行时的门已经有了**（`libraryFunction` + `NodeVMSchedPass`），
**DLPack 的两个方向已经有了**，pybind11 也不必用（它解决的是 C++ 类的绑定，这里要绑的是"一个
模块名 + 一个函数名 + 一组 slot"，CPython 的 C API 更直接）。缺的是**把它们接起来的那一层**：
一个宿主扩展模块、包侧的发现逻辑、诊断到异常的映射、以及打包时把扩展模块收进 `out/<tag>/python/`
与 wheel。

## 57.2 选定的最小端到端路径，以及每一跳在今天的现状

目标形态：一个 Python 脚本 `import camel`，`camel.load("rules.cml")`，把数组传进去，拿到结果数组。

| 跳 | 走什么 | 基线现状 | 现在 |
|----|--------|----------|------|
| 1. `import camel` | 包 `camel` | 只有元数据 | 包 + `_native.py` 找扩展模块 |
| 2. `camel.load("rules.cml")` | 新 `Context`（`EntryConfig` + `buildModuleSearchPaths`） | 无 | 有了；`.cml` 路径的父目录成为第一个模块根（`buildModuleSearchPaths` 不把入口目录算作根） |
| 3. 找函数 | `DefinitionStore::libraryFunction` | 只有 C++ 调用方 | 有了；只认**非泛型的 `export func`** |
| 4. 数组进 | `fromDLPack` + `mm::autoSpace()` | 已有 | 有了；这条路是零拷贝的（分配器是 `autoSpace` 时不 memcpy） |
| 5. 执行 | `NodeVMSchedPass::callGraph` | 已有 | 有了 |
| 6. 数组出 | `toDLPack` | 已有 | 有了（要求数组紧凑、元素类型是数值） |
| 7. 诊断/故障 | — | 无 | 变成带 `file:line:column` 的 Python 异常，见 57.5 |
| 8. 打包 | rollup 脚本 | 有脚本，无扩展模块 | 脚本额外收录 `out/<tag>/python/`；wheel 里是 `camel/.cmlenv/python/_camel*.so` |

不选的两条路，以及理由：

- **pybind11**：仓库里已经有它（`modules/python`），但那一层要绑的是 C++ 类型；这里要绑的是
  模块名、函数名、一组 `slot_t`，用 `Python.h` 直接写反而少一层语义转换，也少一个"Python 版本与
  pybind11 版本"的组合。
- **先把 C API 抽成一个稳定的 C 头**：那是更大的工程（要定义 ABI、版本、生命周期），而 H253 要的是
  一条能走的路；现在的门是 `include/` 里已有的 C++ 接口，扩展模块直接编译在仓库里，不需要新的 ABI。
