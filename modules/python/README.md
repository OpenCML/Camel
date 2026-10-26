# python 模块（.cmo 外源模块示例）

本目录将 **python** 模块单独编译为动态库 `python.cmo`，作为“外源模块”加载示例。通过 pybind11 嵌入 Python 解释器，可在 Camel 中调用 Python 函数、执行脚本、进行类型互转。

## 构建

在项目根目录执行 `npm run build`（或 `debug` / `profile`）后，构建产物包含 `python.cmo`。需安装 Python 开发环境，CMake 会通过 Conan 获取 pybind11。

## 使用

`python` 为标准库模块，随 Camel 发布，直接 `import python` 即可。若自编译或需单独部署，可将 `python.cmo` 放置在 Camel 能访问的模块搜索路径下。`import` 不执行代码（设计原则 3.1）：导入只注册执行器与算子，Python 解释器在第一次调用任一 `py_*` 算子时创建；若 `pyplot` 已经创建了它，这里只附着，不会新建第二个。

### 运行时环境兼容

- 默认行为：Camel 会优先复用激活的 `VIRTUAL_ENV` / `CONDA_PREFIX` 中的包，但在初始化嵌入式解释器时会隔离宿主 `PYTHONHOME` / `PYTHONPATH`，避免外部 Python 环境污染 Camel 运行时。
- `CAMEL_PYTHONPATH`：为 Camel 的嵌入式 Python 追加额外搜索路径；Windows 使用 `;` 分隔。
- `CAMEL_PYTHON_INHERIT_HOST_PYTHONPATH=1`：保留并继承宿主 `PYTHONPATH`。
- `CAMEL_PYTHON_INHERIT_HOST_ENV=1`：完全回退到旧行为，初始化时不屏蔽宿主 `PYTHONHOME` / `PYTHONPATH`。
- 若激活了版本不匹配的 venv，Camel 拒绝初始化解释器并给出明确版本错误，而不是继续在污染状态下运行；这条检查在**首次使用**时进行，不在 `import` 时。没有对应 bridge 的版本连 `.cmo` 都加载不了（`SemanticDiag::ModuleNotFound`，说明缺少哪个 `py_bridge`），那一步仍发生在导入期，因为它决定加载哪个二进制。

## 接口

### 调用与执行

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_call` | `(fn: string \| PyObject, ...args: PyObject[]) => PyObject!` | 调用 Python 函数。**重载 1**：`fn` + 可变数量位置参数，所有 `args` 必须为 `PyObject`。 |
| | `(fn: string \| PyObject, args?: (...PyObject) \| PyObject[], kwargs?: Struct<...PyObject>) => PyObject!` | **重载 2**：支持命名参数。`args` 为 `Tuple` 或 `Array<PyObject>` 传 `*args`；`kwargs` 为 `Struct`（字段值全为 `PyObject`）传 `**kwargs`。1–3 参均可，`args`/`kwargs` 可选。 |
| | | `fn` 为字符串时：单标识符（如 `"print"`）解析为 `builtins.print`；含 `.` 时（如 `"math.sqrt"`）按 `module.attr` 解析。也可传入 `PyObject` 作为可调用对象。 |
| `py_exec` | `(code: string) => PyObject!` | 执行任意 Python 代码（语句），返回 `None`。 |
| `py_eval` | `(expr: string) => PyObject!` | 计算表达式并返回结果。 |
| `py_run` | `(file_path: string) => PyObject!` | 执行指定路径的 Python 脚本文件，返回 `None`。 |

### 模块与属性

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_import` | `(module_name: string) => PyObject!` | 导入 Python 模块，返回模块对象。 |
| `py_attr` | `(obj: PyObject, attr: string) => PyObject!` | 获取对象属性。`attr` 可含 `.` 表示链式访问（如 `"a.b.c"`）。 |
| `py_print` | `(...objs: PyObject[]) => PyObject?` | 调用 Python `print(..., end='')` 输出，不换行；透传首个参数。 |
| `py_println` | `(...objs: PyObject[]) => PyObject?` | 调用 Python `print(...)` 输出并换行；透传首个参数。 |

### 类型互转

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_wrap` | `(x: T) => PyObject<T>` | Camel → Python：将 Camel 值转为 Python 对象。支持 Int/Float/Bool/String/Array/Tuple/Struct 及嵌套。 |
| `py_unwrap` | `(obj: PyObject<T>) => T!` | Python → Camel：将 Python 对象按目标类型转回 Camel。**必须**通过 `as PyObject<T>` 指定目标类型；对象不是 `T` 所要的那种就是 `PythonTypeError`（`bool` 只收 bool，不收 `5`；`int` 不收 bool 与 float；`string` 不收 bytes；`f32` 放不下 `1e300`），消息给出路径与所见的类型。 |

**Python 一侧的失败是错误值，不是故障**：调用、求值、导入、取属性的结果是 `PyObject!`，Python 抛出的异常是种类 `PythonError` 的错误值，消息以异常类名开头（`KeyError: ...`），后接回溯；用 `!!`、`?->` 或 `match` 拆开。参数个数、类型不对仍是故障。

**`py_unwrap` 必须配合 `as` 使用**：`py_eval` / `py_call` 返回的是裸 `PyObject`（无类型参数），而 `py_unwrap` 需根据 `PyObject<T>` 的 `T` 决定返回类型并完成转换。因此必须先将结果用 `as PyObject<T>` 转为带泛型参数的 `PyObject<T>` 再传给 `py_unwrap`，例如：

```cml
let obj = py_call(some_fn, py_wrap(42)) !! py_wrap(0)   // PyObject
let val = py_unwrap(obj as PyObject<int>) !! 0          // 指定 T=int，返回 int
```

## 示例

```cml
import { py_call, py_eval, py_import, py_attr, py_exec, py_run, py_wrap, py_unwrap } from python

// 这里的程序认为每次调用都成功；失败的是错误值，见上
func must(r: PyObject!): PyObject {
    return r !! py_wrap("python failed")
}

func main() sync {
    // 调用 builtins.print
    let _ = py_call("print", py_wrap("Hello from Camel!"))

    // 表达式求值
    let sum_obj = must(py_eval("1 + 2 * 3"))
    let _ = py_call("print", sum_obj)  // 7

    // 导入模块并获取属性
    let math = must(py_import("math"))
    let sqrt = must(py_attr(math, "sqrt"))
    let result = must(py_call(sqrt, py_wrap(16)))

    // 执行代码字符串
    let _ = py_exec("print('Exec: Hello!')")

    // 执行脚本文件
    let _ = py_run("scripts/hello.py")

    // 类型互转：py_unwrap 需配合 as PyObject<T> 指定目标类型
    let val = py_unwrap(must(py_eval("1+1")) as PyObject<int>) !! 0

    // 命名参数：py_call(fn, args_tuple, kwargs_struct) 支持 *args 与 **kwargs
    let _ = py_exec("def _greet(name, greeting='Hello'): print(f'{greeting}, {name}!')")
    let _ = py_call("_greet", (py_wrap("World"),), { greeting: py_wrap("Hi") })  // 输出 Hi, World!
    return 0
}
```

## 测试

测试用例位于 `test/modules/python/`：

```bash
camel run test/modules/python/main.cml
```

## 与内置模块的区别

- **内置模块**：在 `builtinModuleFactories` 中注册，算子表在 `getOpsImplMap()` 中，随 libcamel 一起编译。
- **本模块**：不进入内置表，通过 `camel_module_create` 导出模块，在 `load()` 中注册 `python` 协议的 executor 与算子的纯度，算子实现全部在本 .cmo 中；解释器的创建在第一次调用算子时（`modules/python/embed.h`，与 `pyplot` 共用）。

第三方可按此方式实现自己的 `.cmo` 模块。
