# 57. Python 绑定：数组经 DLPack 零拷贝进出、诊断变成带位置的异常、`pip install` 能装

这里要的是"从 Python 里调用 Camel 模块的函数"这条采用路径：**在 Python 项目里用一部分 Camel**，
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
- **先把 C API 抽成一个稳定的 C 头**：那是更大的工程（要定义 ABI、版本、生命周期），而这里要的是
  一条能走的路；现在的门是 `include/` 里已有的 C++ 接口，扩展模块直接编译在仓库里，不需要新的 ABI。

## 57.3 实现

- `bindings/python/module.cpp`：整个扩展模块。它用运行时的门（`Context`、`libraryFunction`、
  `NodeVMSchedPass`），把 Python 对象转成 `slot_t`，把结果转回 Python；`camel.load` 一次建一个
  进程级 `Context`，模块只编译一次。
- `bindings/python/CMakeLists.txt`：`find_package(Python3 ... Development.Module QUIET)`，没有
  Python 开发文件时**直接跳过**（不进 `out/<tag>/python/`，Python 侧的测试随之跳过），产物落在
  `build/python/_camel*.so`。
- `scripts/collect-out.js`：把 `_camel*` 收进 `out/<tag>/python/`，和 `libcamel` 并排。
- `pypi/camel-lang/src/camel/_native.py`：找扩展模块、预载 `libcamel`、并在包里带了完整安装时把
  `CAMEL_HOME` 指到 `camel/.cmlenv/`（`pip install` 之后不需要任何环境变量）。
- `pypi/camel-lang/src/camel/__init__.py`：`load`、`Module`、`Function`，以及把 DLPack capsule 变成
  `numpy.from_dlpack` 的鸭子类型包装（没有 NumPy 时把 capsule 原样给出）。

## 57.4 证据：零拷贝

同一个进程、同一个缓冲区，`in` 是调用前 Python 侧看到的地址，`out` 是调用返回的 capsule 里
`dl_tensor.data`：

```
zero-copy in:  in 0x737ac7188190 == out 0x737ac7188190, 4 f32, no copy
copy out:      in 0x737ac7188190 != out 0x737a6cfd9040, 4 f32, caller's buffer untouched
scalars:       energy = 30.0
numpy:         the array rules.identity returns points at 0x737a76e19d10 as well
```

`identity(xs)` 原样返回参数，所以它必须给出**同一个地址**；`scale(xs, k)` 自己建块，所以地址必须
不同，而调用方那块缓冲区必须没被写过。

"不拷贝"在时间上的形状是：**调用成本不随数组增长，拷贝成本随数组增长**。下面是同一个进程里
**单次调用的最小值**（抢占只会让单次调用变慢、不会变快，所以最小值是这件工作本身的成本，不随宿主
的调度漂移；`memmove` 是这次调用省掉的那一次拷贝，同样在本进程、同样这些字节上测；1/4/16/64 MiB
各取 200/60/20/10 次，使每一档的取样时长相近）：

```
size          pass-through (the binding)   memmove (a copy of the same bytes)
    1 MiB            9.4 us                   190.2 us
    4 MiB           11.0 us                   159.2 us
   16 MiB           11.6 us                   639.9 us
   64 MiB           11.9 us                  6305.0 us
therefore:    64 MiB by call 11.9 us, by copy 6305.0 us (10.6 GB/s), a factor of 530
```

（1 MiB 那格比 4 MiB 还慢，是本机反复出现的、尺寸自身的效应，不是负载：旧的 23 份均值读表里也有
7 份是同样的次序。推断与固定开销和大页/L3 驻留有关，未单独做实验。两次断言只看首尾两格。）

内存上的形状：连续 8 次返回，`identity` 的结果指向**同一块**，`scale` 的结果是 8 块。

```
memory:        8 results of a 4 MiB pass-through hold 4.0 MiB, 8 results of a 4 MiB copy hold 34.0 MiB
```

（4.0 MiB 是输入那块自己；34.0 MiB = 4.0 + 8×4.0 取整后的读数。）

数组进来的这条路只在分配器是 `mm::autoSpace()` 时零拷贝（`src/core/array/dlpack.cpp` 的
`fromDLPack`），扩展模块用的就是它；其余分配器会 memcpy，这一点没有改变。

## 57.5 诊断与故障 → Python 异常（逐字映射）

三种异常类型都是 `camel` 模块上的类：`CamelError` 是基类，`DiagnosticError` 是**编译期**
（加载/查找/转换参数）的问题，`FaultError` 是**运行期**的错误值。

| Camel 侧发生的事 | `type(e).__name__` | `e.name` | `e.code` | `e.file:line:column` | `e.message`（逐字） |
|---|---|---|---|---|---|
| 库里有未声明的名字 `missing_name` | `DiagnosticError` | `SemanticDiag::UnresolvedReference` | `0x0300000B` | `<...>/python_broken.cml:9:12` | `Unresolved reference: 'missing_name'.` |
| 函数参数个数不对 | `DiagnosticError` | `ArgumentCountMismatch` | 空 | 空（`:0:0`） | `scale takes 2 argument(s), 1 given` |
| 参数不是数组也不是 capsule | `DiagnosticError` | `ArgumentNotConvertible` | 空 | 空（`:0:0`） | `argument 1 of energy: a numeric array must offer __dlpack__ (NumPy, PyTorch, CuPy and JAX do), or be a DLPack capsule` |
| 模块没有这个导出 | `DiagnosticError` | `NoSuchExport` | 空 | 空（`:0:0`） | `'python_rules' exports no 'not_declared'` |
| 模块声明了但没 `export` | `DiagnosticError` | `FunctionNotExported` | 空 | 空（`:0:0`） | 运行时给的那句（`libraryFunction` 的 `problem`） |
| 模块找不到 | `DiagnosticError` | `LibraryUnavailable` | 空 | 空（`:0:0`） | 运行时给的那句 |
| `.cml` 路径不存在 | `FileNotFoundError` | — | — | — | `no Camel module at <path>` |
| `throw error('RangeError', '...')` | `FaultError` | `RangeError`（错误值的 `kind`） | 空 | `python_rules.cml:36:15` | `the factor must not be negative` |
| 运行期故障（`RuntimeFault`） | `FaultError` | 故障的诊断名 | `0x04......` | 有位置时给出 | 故障的诊断消息 |

逐字的运行输出（测试脚本的原文）：

```
error value:   camel.FaultError name=RangeError at python_rules.cml:36:15
               message=the factor must not be negative
diagnostic:    camel.DiagnosticError name=SemanticDiag::UnresolvedReference code=0x0300000B at <work>/test/cases/feat/bindings/python_broken.cml:9:12
               message=Unresolved reference: 'missing_name'.
absent name:   camel.DiagnosticError name=NoSuchExport: 'python_rules' exports no 'not_declared'
```

几点必须说清楚的：

- **位置是编译器给的，不是绑定拼的**。`SemanticDiag::UnresolvedReference` 的 `9:12` 来自
  `Diagnostic::range` 经 `resolveSourceRange` 解出来的行列，`str(e)` 里就是
  `<file>:<line>:<column>: error[<name>] (<code>): <message>`。
- **`e.code` 为空的那些不是 `Diagnostic`**。它们由绑定自己产生（查找失败、参数转换失败），没有
  诊断目录里的码；空的 `code` 是诚实的，不是丢字段。
- **`FaultError` 的位置来自错误值本身**：`T!` 的 `location` 字段是 `"file:line:column"`（`throw` 的
  位置）；如果错误值的 `location` 是一个算子的名字而不是位置，`e.line`/`e.column` 就是 `0`，
  这时 `str(e)` 里写的是那个名字，不编造位置。
- **诊断文本原样保留**在 `e.camel_text` 里，就是命令行 `camel` 会打印的那段（含源码上下文），
  便于 Python 侧直接转述。

## 57.6 `pip install`

打包这条路今天的现状是：已有的 `scripts/pypi-rollup.js` 会把 `out/<tag>/` 折进包、建 wheel、
改 tag、放到 `dist/pypi/`。这一片给它补了三件事，都是"在一台拿不到包索引的机器上也要成立"：

1. `CAMEL_PYPI_SKIP_BUILD=1`：只折 `out/<tag>/`、不重新构建（wheel 的内容本来就来自 `out/<tag>/`）。
2. 建 wheel 用 `build` 模块，没有它就用 `pip wheel --no-build-isolation`（离线机器装不上 `build`）。
3. 改 tag 不再用 `wheel tags`——wheel 0.45 已经删掉了那个命令行——改用仓库自己的
   `scripts/wheel_retag.py`（`zipfile` 重写 `*.dist-info/WHEEL` 的 `Tag:` 并改名）。
   顺带：每次 rollup 先清掉 `pypi/camel-lang/{build,dist}`，否则上一次失败留下的 `dist/*.whl`
   会被一起发布。

逐字命令与输出（参照主机，无网络）：

```sh
npm run build
CAMEL_PYPI_SKIP_BUILD=1 npm run pypi:rollup
```

```
[STEP] Retagging wheel(s): cp310-cp310-linux_x86_64
[DONE] Final wheel(s) in dist/pypi/: camel_lang-0.2.30-cp310-cp310-linux_x86_64.whl
-rw-rw-r-- 1 zjwei zjwei 12846898 camel_lang-0.2.30-cp310-cp310-linux_x86_64.whl
```

装它、用它（`--no-index` 是为了证明不需要网络；`--target` 只是为了不动那台机器的环境）：

```
$ python -m pip install --no-index --no-deps --target /tmp/camel-pkg dist/pypi/*.whl
Processing ./dist/pypi/camel_lang-0.2.30-cp310-cp310-linux_x86_64.whl
Installing collected packages: camel-lang
Successfully installed camel-lang-0.2.30
```

装完之后**不设任何环境变量**（连 `CAMEL_HOME` 都不设）跑 `examples/python/rules/basket.py`，
输出见 [examples/python/rules/README.md](../../examples/python/rules/README.md)：包把
`CAMEL_HOME` 指到自带的 `camel/.cmlenv/`，扩展模块与 `libcamel` 都在那里。

## 57.7 今天做不到的（边界，不是没实现完的语义）

- **只有非泛型的 `export func`**：`libraryFunction` 不接受泛型函数（它自己就拒绝），所以
  `python_rules.cml` 里的函数都写成具体类型。泛型函数如何从 Python 传类型实参，是**未定的语义**，

- **只有 CPU、连续、单 lane 的数值数组**：`fromDLPack` 对非 `kDLCPU`、`strides != nullptr`、
  `byte_offset != 0`、非 1 lane、未知 dtype 直接抛 `std::invalid_argument`，绑定把这些原样报给
  Python。这不是限制参数，是运行时的既有契约。
- **模块跑在 node VM 上**：扩展模块用的是运行时自己的门 `NodeVMSchedPass`，所以逐元素的工作是
  解释器速度（一个元素一次闭包调用）。Python 侧要挑遍、或者要 fvm/jit，今天没有门；这是取舍，
  不是语言语义。`examples/python/rules/README.md` 把这件事和实测数字一起写清楚了。
- **返回的 NumPy 数组是只读的**：DLPack 没有表达"别人这块内存可写"的字段，NumPy 因此把从
  capsule 导入的数组标成只读。要写就先 `.copy()`。
- **元素类型转换**：Python 的 `int`/`float`/`bool`/`str` 会按被调函数的形参转换，数组则要求 dtype
  与形参一致（`f32[]` 只收 float32）；不一致时报 `ArgumentNotConvertible`，不做隐式逐元素转换。

## 57.8 跑测试

```sh
node scripts/test.js test/plans/feat/bindings/python_binding.plan.toml --jobs 8
```

一个测试，7.24 s，`pass: 1, fail: 0`。没有 NumPy 的解释器也能跑：测试自己用 `ctypes` 拼 DLPack
capsule，NumPy 只在可导入时额外验一遍 `__dlpack__` 那条路。没有 Python 开发文件、扩展模块没被
构建出来时，`skip_unless` 直接跳过这个测试（不是失败）。
