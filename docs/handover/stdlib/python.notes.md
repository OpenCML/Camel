# python 重写说明（并含 pyplot 的去留）

稿件：`python.cml`、`python.signatures.cml`（换进 `stdlib/signatures/python.cml`）。未编译。

## 方向

本模块是 **Camel 调 Python**（嵌入解释器）。**Python 调 Camel** 是另一个东西：pip 包 `camel`（`bindings/python`，DLPack 零拷贝，`docs/technical/57_python_bindings.md`），不在标准库里。两个方向交换数组都走 DLPack。

## 改了什么

1. **删掉 `py_` 前缀**：模块名已经是 `python`，`python::exec` 比 `python::py_exec` 好读（conventions 3：同一件事一个名字，不重复限定）。`py_exec`→`exec`、`py_eval`→`eval`、`py_run`→`run_file`、`py_import`→`import_module`（`import` 是关键字）、`py_attr`→`attr`、`py_call`→`call`。
2. **Python 的异常是错误值，不是故障**：Python 代码抛异常、模块缺失、属性缺失，都是"外部世界的失败"，返回 `T!`，种类 `PythonError`，消息含异常类型、文本、回溯（原先一律 `RuntimeError` 故障）。
3. **没有 `PyObject<T>`**：`py_wrap`/`py_unwrap` 用类型参数记着包的是什么，这是"模块类型的类型变量实参"那个缺口的来源（`InvalidTypeArgument`）。换成两个按类型转换的函数，同 `json::decode_as`：`to_py::<T>(x): PyObject!` 与 `from_py::<T>(obj): T!`（`PythonTypeError` 报路径与实际所见）。数组走 DLPack 零拷贝（同设备、同元素类型、同形状；元素类型不符是错误，不转换，5.1），标量按值，元组/结构体/映射/其他数组按部分转成 Python 的 `tuple`/`dict`/`list`。
4. **`py_call` 的三种形态收成一个**：被调对象是 `with` 参数 `fn`（函子的 this），参数数组是数据，关键字参数 `kwargs` 是 `with <… = null>`（等于 `functools.partial`）：`[to_py(2.0)]->call<fn>`，`call<fn>` 本身是函数值。变参 `...args` 与 `string | PyObject` 联合、`Struct<...PyObject>` 都不再需要。按点分名字调用（`"math.sqrt"`）删除：`import_module('math') !-> attr<'sqrt'>` 是同一件事的唯一写法。
5. **`py_print`、`py_println`、`py_dispose` 删除**：打印用 `to_string` 取文本再 `println`（Python 的输出走 Python 的 stdout，两条输出流的先后没有保证，是旧接口的陷阱之一）；`PyObject` 由 GC 的终结器释放（10.5：外部指针由 Camel 原生对象管理），不需要手动释放。新增 `to_string(obj): string!`。
6. **没有资源子句**：Python 代码什么都可能做，原生算子没写子句就按"全局"资源，与一切效应保持顺序，这正是需要的；模块说明里写明，不是遗漏。
7. **稳定 ABI**：解释器在第一次使用时启动，通过 Python 3 的稳定 ABI（`Py_LIMITED_API`）链接，同一份构建对本机的 Python 3 都可用（见 C++ 侧）。

## 函数表

| 项 | 处理 |
|---|---|
| `py_exec` | 改名 `exec`，`void!` |
| `py_eval` | 改名 `eval`，`PyObject!` |
| `py_run` | 改名 `run_file`，`void!` |
| `py_import` | 改名 `import_module`，`PyObject!` |
| `py_attr(obj, attr)` | 改为 `obj->attr<name>`，`PyObject!` |
| `py_call`（两个 URI） | 改为 `args->call<fn, kwargs>`，`PyObject!` |
| `py_wrap`、`py_unwrap` | **删除** → `to_py::<T>`、`from_py::<T>`（签名文件） |
| `py_print`、`py_println`、`py_dispose` | **删除** |
| `to_string` | 新增 |

## C++ 侧改动（`modules/python`）

- 嵌入从 pybind11 改为直接用 CPython 的稳定 ABI（`Py_LIMITED_API`，运行时按 `libpython3` 动态加载，找不到时 `import_module` 等返回 `PythonError`，其余程序不受影响）；`modules/python/sdks/` 里按版本拷贝的 `python3xx.dll` 可删，打包脚本 `sync-python-sdks` 随之去掉。**这是最大的一项工程**，`PythonObjectHolder` 的 GIL 与释放逻辑（现有）保留。
- 数组：`to_py` 用 `toDLPack`（`include/camel/core/array/dlpack.h`，现成）生成 capsule，Python 一侧用 `numpy.from_dlpack`/`torch.from_dlpack`；`from_py` 用 `fromDLPack`。两个函数先设成一个计算签名的 `DynamicFuncTypeResolver`，`has_python_form::<T>()` 作为谓词在 `.cml` 或 C++ 中实现。
- 失败路径：`throwRuntimeFault(RuntimeError, "python: …")` 全部改为 `makeError("PythonError", …)`。
- `py_call_kw`、`py_print*`、`py_wrap*`、`py_dispose` 的算子表项与执行器删除。
- 解释器的启动时机（`docs/technical/50_pyplot_cpython_init.md`）不变：第一次使用。

## 调用点（约 16 个文件）

`test/cases/modules/std/python/{basic,main}.cml`、`test/cases/consistency/{attempt/resource_release,cleanup/python_output_*}.cml`（"Python 的输出先于故障"这类用例要重写：现在没有 `py_print`，改为 `exec("print(1)")`）、`test/cases/compile/native/python_*.cml`、`test/cases/compile/param_defaults/stdlib_no_place_for_name.cml`、`test/plans/feat/modules/interpreter_first_use.plan.toml`、`test/plans/feat/compile/{native_declarations,param_defaults}.plan.toml`、`docs/cli.md`、`docs/diagnostics.md`、`docs/technical/{23,24,36,50}_*.md`。

## pyplot：删除出标准库

`pyplot` 只有一个函数 `plot(data, filename)`，八条按元素类型/元数展开的声明，用 matplotlib，没有时写 SVG。判断：
- 它是互操作的一个特例，不是语言库：有了 `python` 模块，画图就是 `import_module('matplotlib.pyplot')` 加几次 `call`，一个用户自己的十行 Camel 函数，这类东西是示例，不是标准库；
- 设计稿（`docs/design/stdlib/README.cn.md`）已把绘图列为将来的 `plot` 模块（声明式，输出 SVG/PNG/HTML），那才是标准库的位置；SVG 兜底应属于它，而不是 `pyplot`；
- 八条声明是 conventions 8 里"伞形参数"的反例。

处理：删除 `stdlib/pyplot.cml` 与 `modules/pyplot`；用到的 6 个文件（`test/cases/modules/std/pyplot/simple.cml`、`test/cases/compile/native/pyplot_{arity,declaration_import,type}.cml`、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/stdlib-reference.md`）中，`simple.cml` 改成 `examples/` 下一个用 `python` 模块画图的示例（matplotlib 没装时跳过），三个 `native` 用例随模块删除。

## 开放问题

1. 已定：现在删除 `pyplot`。SVG 写出器留在 `modules/pyplot` 的历史里，将来的 `plot` 需要时再取。
2. **稳定 ABI 的代价**：`Py_LIMITED_API` 不暴露一些宏和结构体字段（NumPy 的 C API 也不能用），但本模块只做调用、属性、转换，DLPack 走 capsule，够用。推荐做；pybind11 留在 `bindings/python`（那条路径本来就是按版本构建的扩展）。
3. **`has_python_form::<T>()`**：谓词的写法依赖按结构展开（结构体字段、数组元素类型）；元素类型提取没有先例，先在 C++ 里做，等泛型按结构展开落地后搬进 `.cml`。
