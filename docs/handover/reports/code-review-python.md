# Python 互操作与打包代码审查

范围：`bindings/python/`（`module.cpp` 831 行、`CMakeLists.txt`）、`pypi/camel-lang/`（`pyproject.toml`、`src/camel/__init__.py`、`_native.py`、`src/camel_cli.py`）、`modules/python/`（`module.cpp`、`operators.cpp`、`embed.h`、`type.cpp`、`bridge_runtime.cpp`、`CMakeLists.txt`）、`src/core/array/dlpack.cpp`、`.github/workflows/build-pypi.yml`，对照设计原则 §10.7、`docs/pypi-packaging.md`、`test/cases/feat/bindings/`、`test/cases/modules/std/python/`。只读代码，没有构建、没有运行；标"需确认"的是由代码推断、未实测。

总体：错误对象的设计（`DiagnosticError/FaultError` 带名称、代码、位置、`camel_text`）和"数组按 DLPack 零拷贝"的主路径是认真做的；问题在边界的失败路径、可写别名、全局单例、与 §10.7 的差距，以及打包只发 per-CPython 的扩展。

按严重度排序。

## 高

### P1 DLPack 导入在失败路径泄漏生产者的内存，并把真实原因说成"没有 `__dlpack__`"

- `bindings/python/module.cpp:263`：`adoptArray` 先 `PyCapsule_SetName(capsule, "used_dltensor")`（宣告"我接管了"），然后才调 `fromDLPack`（268-275）。`fromDLPack` 在设备不是 CPU、dtype 不支持、非连续、`ndim<0` 时抛异常（`src/core/array/dlpack.cpp:151-177`）；此时容器已改名，协议规定的"未被接管则由胶囊析构器调用 deleter"不再成立，Camel 又没有调用 deleter：生产者的张量（NumPy 的 base 引用、PyTorch 的存储）永远不释放。
- 同函数 246-251：`__dlpack__()` 抛的任何异常（NumPy ≥ 2 对只读数组抛 `BufferError`、CUDA 张量要求 stream、被释放的对象）都被 `PyErr_Clear()` 吃掉，报错一律是"must offer `__dlpack__`"。用户拿到的是错误的诊断。
- 失败场景：把一个 `float16` 数组、一个转置视图、一个 CUDA 张量传进去，得到 `ArgumentNotConvertible`，同时那块内存被泄漏；循环里反复失败即内存泄漏。
- 修法：改名放到 `fromDLPack` 成功之后；失败时不改名，让胶囊自己的析构器处理（或由 Camel 主动 `deleter`）。`__dlpack__` 的异常原样附在 `ArgumentNotConvertible` 的 `__cause__` 里，不要吞。

### P2 返回给 Python 的数组是可写别名，改它会改到 Camel 的值

- 导出（`dlpack.cpp:119-146` 的 `toDLPack`）只 `retain` 块并给出 `DLManagedTensor`（旧结构，没有 `READ_ONLY` 标志位）；`_CapsuleArray`（`camel/__init__.py:62-80`）和 NumPy 的 `from_dlpack` 得到的是可写数组。
- 失败场景：`f32[]` 函数返回模块级常量表或一个被别的 Camel 值共用的数组，Python 写 `y[0] = 5`，常量被改，之后每次调用都得到被改过的值；Camel 的"值语义"在边界上破裂。块的引用计数 ≥ 2 只阻止了 Camel 侧的就地写（`rtdata/array.cpp:1012` 要求 `unique() && camelOwned()`），挡不住 Python 侧。
- 修法：导出用 `DLManagedTensorVersioned` 并置 `DLPACK_FLAG_BITMASK_READ_ONLY`（NumPy ≥ 2.1 据此设 `writeable=False`）；`_as_python` 里无论如何 `array.flags.writeable = False`；需要可写的调用方自己 `.copy()`。这是 §10.7 之外但属于边界契约，应写进原则。

### P3 C++ 异常越过 CPython 的 C 帧

- `sessionCall`（`module.cpp:615-744`）只把 `callGraph` 包在 try 里。其余位置的异常无人接：`ensureHost`（`Context::create`，搜索路径坏时抛）、`libraryFunction`（编译库，内部可抛）、`convertArgument` 里的 `String::from` / `RootHandle` 构造（内存不足）、`toPython` 里 `toDLPack`（`ASSERT` / `std::invalid_argument`）。
- 失败场景：`camel.load("x.cml", dirs=["/不存在"])` 之类触发 `Context::create` 的异常，C++ 异常穿过 `PyCFunction`，解释器 `std::terminate`，整个 Python 进程崩溃，而不是一个 `CamelError`。
- 修法：`sessionCall` 的整个函数体放进一个 `try { … } catch (const std::exception &)`，映射为 `CamelError`（`HostFailure`）；`toPython` 里的 `toDLPack` 同。

### P4 进程内只有一个 Context，后来的 `Session` 的搜索路径被静默忽略

- `ensureHost`（`module.cpp:188-205`）用函数内 `static` 初始化，只取第一次调用的 `entryDir` 与 `dirs`。`camel.load` 的文档（`__init__.py`）承认"read by the first `Session`"。
- 失败场景：`camel.load("a/rules.cml")`（把 `a/` 加进搜索根）之后 `camel.load("b/pricing.cml")` 找不到 `pricing`（`NoSuchExport`，信息指向一个"没有这个库"的模块），或更糟：两个目录有同名模块时静默用了第一个。库是按名字缓存的（`DefinitionStore::functions_`，`definitions.cpp:251` 起，失败的查找也缓存），改了源文件、修了错误后在同一进程里无法重新加载（Jupyter 里很常见）。
- 修法：搜索根按 `Session` 增量追加到共享 Context（或每个不同 `(entry_dir, dirs)` 一个 Context）；提供 `camel.reload(name)` 或 `Session.close()`；缓存失败结果的策略按"文件时间戳变了就重试"。

### P5 与 §10.7 的差距：泛型函数、GIL

- §10.7（`docs/design-principles.cn.md:724-726`）规定：泛型 `export func` 按宿主实参类型实例化并缓存；执行 Camel 期间释放 GIL，Camel 回调 Python 时再取得。
- 现状：`module.cpp:32-35` 声明"只有非泛型函数可调，泛型是语言作者的问题"；`sessionCall` 没有 `Py_BEGIN_ALLOW_THREADS`，`callGraph` 全程持有 GIL。失败场景：一个跑几秒的 Camel 函数让所有 Python 线程（包括 asyncio 事件循环、数据加载线程）一起卡住；也就没有 Python 与 Camel 并行的可能。
- 释放 GIL 的前置工作：`Arguments` 里的 `RootHandle` 与结果转换要在重新取得 GIL 后做；`ensureHost` 与 Context 的并发保护（目前全进程一个 Context，靠 GIL 串行化，释放 GIL 后必须加锁，或每线程一个 `NodeVMSchedPass`，Context 本身线程安全需确认）。泛型实例化：宿主实参的类型（数组的元素类型、标量按字面量）→ `Definitions` 的实例化入口，设计已定，缺实现。
- 宿主标量按字面量："放不下报错"目前只对 `int32` 范围与整数溢出做了；`float32` 形参接收 `1e300` 会静默变 `inf`（`module.cpp:319-320`）。

### P6 扩展按 CPython 版本各一份，没有稳定 ABI；多数用户装到的是只有 CLI 的 wheel

- 流程：`pyproject.toml` 的纯 CLI wheel 是 `py3-none-*`；互操作 wheel 是 `cp311`（Linux，容器里只有 `python3.11-devel` 带共享 libpython，`build-pypi.yml:76-88` 注释）和 `cp310`（Windows，`setup-python 3.10`），macOS 没有。冒烟测试里 `python:3.12-slim` 与 `ubuntu:22.04` 的期望值就是 `body-only`（`build-pypi.yml:420-436`）。
- 失败场景：装了 Python 3.12 或 3.13 的用户 `pip install camel-lang` 成功，`import camel` 抛 ImportError（信息写得很清楚，但功能不可用）。`docs/pypi-packaging.md:145-148` 自己记录：`-DPy_LIMITED_API=0x030A0000` 下扩展只剩 5 处报错（`PyTuple_SET_ITEM` 等宏），`bindings/python/module.cpp` 用的都是 `PyType_FromSpec`、`PyCapsule_*`、`PyModule_AddObjectRef`，本来就接近稳定 ABI；`modules/python`（pybind11 嵌入）与互操作扩展无关，不受影响。
- 修法：扩展改用 `Python3_add_library(... USE_SABI 3.10)` 与 `Py_LIMITED_API=0x030A0000`，`PyTuple_GET_SIZE/GET_ITEM/SET_ITEM` 换成函数形式；wheel 变成 `cp310-abi3-<platform>` 一份覆盖 3.10 以上全部版本，Linux 不再受"只有 3.11 有共享 libpython"的限制（扩展不链接 libpython）。

### P7 `modules/python`：`pyToCamel` 吞掉了所有具体错误，并有静默转换

- `operators.cpp` 的 `pyToCamel`（约 `:520-640`）整个包在 `try { … } catch (...) { throwRuntimeFault("conversion failed") }` 里；函数内部自己抛的 `throwRuntimeFault`（"array target requires Python list"、"struct missing field 'x'"、"tuple length mismatch"…）也被这个 `catch (...)` 接住，重新抛成同一句 "python.unwrap: conversion failed"。失败场景：`py_unwrap(obj as PyObject<(int, int)>)` 少一个元素，用户看不到长度不匹配，只看到"转换失败"。
- 静默转换（pybind11 `py::cast<T>`，convert 默认开）：`Bool` 目标接受任何有 `__bool__` 的对象（`5` 变 `true`）；`String` 目标接受 `bytes`；`Float32` 目标把 `1e300` 变成 `inf`；`Int64` 目标接受 `True`。与"不做隐式数值转换"（5.1）和 §10.7 的"宿主标量按字面量处理、放不下报错"不一致。
- 数组：`Array::create(mm::autoSpace(), n)`（`:590` 附近）建的是槽位数组（头文件说明：元素类型未定时用），再 `arr->set(i, *s, arrayType)`；`py_unwrap(x as PyObject<f32[]>)` 得到的是否是 compact 的 f32 数组需确认。若不是，后续 tensor 算子会读到错误布局（`derivative` 代码里记录过同类的 `sum: Unsupported Tensor dtype`）。应改为 `Array::create(allocator, arrayType, shape)`。
- 修法：去掉外层 `catch (...)`，只转换 `py::error_already_set` / `py::cast_error`；转换表（目标类型 → 接受的 Python 类型）写成一张表，并有测试逐项覆盖。

## 中

### P8 CLI 与 Python API 的行为差异（需确认其中几项）

- `sessionCall` 用 `libraryFunction` 得到图后直接 `NodeVMSchedPass::callGraph`，不经过 `camel` 命令的 pass 管线（`std::macro`、`std::opt`、`tiles::compile`、`onnx::export` 等）。`grad/with_grad` 靠运行时核函数（`autodiff/operators.cpp:90` `gradFunction`）还能跑，但慢、且不做 `std::macro` 的编译期拒绝信息；`#gpu` 范围没有 `tiles::compile` 就不会上 GPU，也不报错（需确认）。`camel` 与 `import camel` 对同一个函数可能给出不同的性能甚至不同的诊断。
- 调度器固定为节点 VM；CLI 默认可选 fvm/jit。Camel 的 `println` 直接写文件描述符 1，不经 `sys.stdout`，Jupyter / pytest 的捕获看不到，和 Python 自己的输出顺序也无保证。
- 轮子里 `CAMEL_HOME`：`_native._adopt_bundled_install` 只在未设置时指向轮内安装；`_preload_library`（`_native.py:68-84`）的搜索顺序却是轮内目录在先、`CAMEL_HOME/libs` 在后。用户设了 `CAMEL_HOME` 指向另一份安装时，加载的是轮内的 `libcamel`，读的是 `CAMEL_HOME` 的标准库，版本错配。CLI（`camel_cli.py`）从可执行文件路径推安装根，忽略 `CAMEL_HOME`。三条路径的优先级应统一并写明。

### P9 Python 异常变成笼统的运行时故障，类型丢失；没有 `T!` 的出口

- `modules/python/operators.cpp` 所有算子把 `py::error_already_set` 变成 `RuntimeDiag::RuntimeError` + `e.what()` 全文（含 traceback），异常类名、`args` 都丢了；Camel 程序不能区分 `KeyError` 与 `ImportError`，也不能把它当错误值处理（`py_call` 的结果类型是裸 `PyObject`，不是 `PyObject!`）。与"错误值统一成种类名"（§10.7、3.x 错误模型）不一致；至少把 Python 异常的类名作为 `Error.kind`，并让 `py_call/py_eval/py_import` 返回 `PyObject!`。
- 与此对称，Python → Camel 的 `FaultError` 里 `name` 来自 Camel 错误值的 kind，这部分是对的。

### P10 嵌入解释器的 GIL 与 GC 交互，以及退出次序（需确认）

- `deletePythonObjectHolder`（`operators.cpp:40-50`）在 GC 终结线程里 `py::gil_scoped_acquire`。并行调度器下，线程 A 持有 GIL 并在算子里分配 Camel 对象（`wrapPyObject` → `ForeignResourceObject::create`）而触发 GC 的 stop-the-world；终结器线程 B 等 GIL，A 等 GC 完成：存在锁序反转的死锁可能。`test/cases/modules/std/python` 有 `integration.prl`，但没有 GC 压力（`CAMEL_GC_STRESS_*`）下的 PyObject 用例。
- 绑定侧：`static context_ptr_t shared` 在 C++ 静态析构阶段销毁，晚于 `Py_Finalize`；仍引用 NumPy 内存的 Camel 数组此时释放，会调用生产者 deleter（NumPy 的 deleter 要 GIL 和活着的解释器）。需要 `atexit` 里先释放 Context，或在 `Py_AtExit` 回调中清理。
- 对 `pyToCamel` 里创建的 `Array/Tuple/Struct` 没有 `RootHandle` 或 `NoCollectionScope`（`tiles` 的同类场景用了 `NoCollectionScope`），嵌套分配期间若触发 GC，裸指针是否安全需对照 `docs/technical/36_gc_handle_rules.md` 确认。

### P11 `modules/python` 的环境逻辑：两份 site-packages 路径，其中一份是错的；单次初始化绑到第一个 Context

- `embed.h:130-150` `activeEnvSitePackagePaths()` 在 POSIX 上返回 `venv + "/lib/python/site-packages"`（缺版本目录），目录不存在，`prepend_path_if_missing` 因 `!exists` 直接跳过，等于什么都没做；正确的版本化路径在 `ensure_site_packages_in_path()` 里（带 `"3.11"` 硬编码的兜底）。两份逻辑重复且一份无效。
- `ensurePythonInterpreter`（`module.cpp:~277`）的 `std::call_once` 把"把入口目录加入 `sys.path`"也绑在第一次：第二个 Context（不同入口目录）永远不会同步自己的路径。
- `embed.h` 是头文件，被 `python.cmo` 与 `pyplot.cmo` 各编译一份，里面的函数内 `static`（如 `python_home_storage`）各有一份；注释已说明一次性状态放在 libcamel 里，这个静态没有。

### P12 打包与 CI

- 冒烟测试可能测到的是 PyPI 上的 wheel：`build-pypi.yml:332-352`（原生）与 `smoke-linux` 里都是 `pip install --find-links <dir> camel-lang`，没有 `--no-index`。若 PyPI 上已有同名且版本相同或更高的发布，pip 会选择它，冒烟通过并不说明刚构建的 wheel 可用。应加 `--no-index`（`numpy` 另装）。
- Windows 的冒烟只做 `import camel`（`:360-370`），Linux 才真正调用 `rules.double_totals(...)`；Windows 的 DLL 搜索路径（无 RPATH，靠 `_native._preload_library` 里的 `os.add_dll_directory`，且预加载失败时 `OSError` 被吞，目录没加）恰恰最需要这一步。
- 发布没有校验"tag 的版本 == `pyproject.toml` 的版本"；`publish-testpypi` 缺少 `check_wheel_tags.py` 那一步；`pypa/gh-action-pypi-publish@release/v1` 是分支引用，发布 job 持有 `id-token: write`，应固定到提交哈希。
- `bindings/python/CMakeLists.txt` 的注释说"在 `pip install` 能构建它的地方也能构建"，但没有 sdist，`MANIFEST.in` 只包含预先放好的 `.cmlenv`；源码安装得到的是没有二进制的包。要么提供 sdist 的构建后端，要么删这句话。
- `pyproject.toml` 版本 0.2.32，工作流注释里写 0.2.31；`camel/__init__.py` 的 `__description__` 与 `pyproject.toml` 的 `description`、作者邮箱写了两份且不一致。`requires-python = ">=3.8"` 而扩展要求 3.10+，信息只写在 ImportError 里，可以在 README 的安装一节说清。

### P13 DLPack 导入的其余细节

- `fromDLPack`（`dlpack.cpp:149-203`）不检查 `data + byte_offset` 的对齐；NumPy 的 `frombuffer` 视图可以是未对齐的，f64/i64 被当作对齐数据读（x86 不崩，ARM/部分 SIMD 路径会）。应当检查并拒绝或复制。
- 使用旧结构 `DLManagedTensor`，不识别 `DLManagedTensorVersioned` 的 `READ_ONLY` 标志，也没有 `__dlpack__(max_version=…, stream=…)` 协商：只读数组被 NumPy 2 拒绝导出（见 P1）而不是被 Camel 当作只读借用。借用的块也不会被 Camel 就地改写（`camelOwned()` 检查在位），这点是对的，应在文档里写成契约。
- 形参声明了形状（`f32[3, 4]`）时只比较元素类型（`module.cpp:343-346`），秩与形状不核对。
- 标量：`bool` 形参要求 `PyBool`（拒绝 `numpy.bool_`），`int` 形参却接受 `True`（`PyLong_Check` 为真）；`numpy.int64` / `numpy.float32` 标量被拒绝（`np.float64` 因继承 `float` 而通过）。规则不对称，也与"宿主标量按字面量"的描述不符。

## 低

- 重复与大函数：`sessionCall` 约 130 行（查找、签名检查、转换、运行、故障映射、结果转换）；`modules/python/operators.cpp` 十二个算子都重复"`ensurePythonInterpreter` + `gil_scoped_acquire` + 参数个数检查 + 参数类型检查 + try/catch 映射"的样板，应抽成一个包装。两处各自实现的转换规则（`bindings/python` 的 `convertArgument/toPython` 与 `modules/python` 的 `camelToPy/pyToCamel`）覆盖面不同（前者只有标量、字符串、数值数组、元组；后者有 `Bytes`、结构体⇄dict、数组⇄list，没有 DLPack），应共用同一张规则表与同一套测试。
- `modules/python/operators.cpp` 的行内注释大量是中文（`// 递归：按类型与 slot 将 Camel 值转为 Python 对象…`、`// py_call: 调用 Python 函数…`），与"代码注释用英文"的约定不一致。
- 历史叙述型注释：`embed.h` 头部（"`modules/pyplot` used to create an interpreter with a shorter sequence and a program that plotted and then imported `_ssl` failed; that is fixed here"）、`modules/python/module.cpp`（"The venv check the module has always made…"、"the INFO lines `load()` used to write"）、`embed.h` 尾部（"as they did when the interpreter was created by a `load()`"）；`operators.cpp` 里同一句"The interpreter is created at the operator's first use, not by `import python`."重复十一次。
- `raiseReport`（`module.cpp:156-161`）注释说"清掉这个错误会掩盖本模块的缺陷"，随即 `PyErr_Print()`（打印并清除）再设置 `RuntimeError`，行为与注释相反。
- `Session`、`Function`、`Module` 的 `__getattr__` 对任意名字都返回 `Function`，拼写错误要到调用时才暴露，且错误来自 `NoSuchExport` 而不是 `AttributeError`，`hasattr(module, "x")` 恒为真。
- 扩展以顶层名 `_camel` 导入（`_native.py` 把目录插入 `sys.path[0]` 后 `import_module("_camel")`），而异常类型与 `Session` 的 `__module__` 写的是 `camel.*`；`sys.path` 被改，且该目录排在最前。
- `testing`：`test/cases/feat/bindings/python_rules.cml` 用了已被裁定移除的 `throw error(...)`（应改为 `fail<'RangeError'>(…)`），按当前语义这个用例的源码本身可能已经编译不过（需确认）。

## 测试缺口

`test/cases/feat/bindings/python_binding.py`（452 行，不依赖 NumPy，用 ctypes 手造胶囊）覆盖了零拷贝、错误值到 `FaultError`、`DiagnosticError`、`NoSuchExport`、步长数组被拒。缺：

1. DLPack 导入失败时生产者 deleter 是否被调用（P1）；只读数组、CUDA 张量、未对齐、`float16` 的报错文本（P1、P13）。
2. 返回数组的可写性（P2）：返回模块常量，Python 写入后再调一次。
3. `Context::create` 失败、传入不存在的 `dirs`（P3）。
4. 两个不同目录的模块先后 `load`；同名模块；修好源文件后重新加载（P4）。
5. 多线程同时调用同一个 `Session`；Camel 长时间运行时另一个 Python 线程能否前进（P5）。
6. 标量规则表：`True` 给 `int`、`np.int64`、`np.bool_`、`float32` 溢出、`int32` 边界（P5、P13）。
7. `modules/python`：`py_unwrap` 每种失败分支的具体错误文本（P7）、`bytes` 给 `String`、`py_dispose` 之后再用、Python 异常的类型（P9）、GC 压力 + 并行调度下的 `PyObject`（P10）。
8. 发布工作流：tag 与版本号一致的检查；Windows 冒烟里真正调用一次数组函数（P12）。
