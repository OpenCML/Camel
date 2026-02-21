# python 模块（.cmo 外源模块示例）

本目录将 **python** 模块单独编译为动态库 `python.cmo`，作为“外源模块”加载示例。通过 pybind11 嵌入 Python 解释器，可在 Camel 中调用 Python 函数、执行脚本、进行类型互转。

## 构建

在项目根目录执行 `npm run build`（或 `debug` / `profile`）后，构建产物包含 `python.cmo`。需安装 Python 开发环境，CMake 会通过 Conan 获取 pybind11。

## 使用

`python` 为标准库模块，随 Camel 发布，直接 `import python` 即可。若自编译或需单独部署，可将 `python.cmo` 放置在 Camel 能访问的模块搜索路径下。首次使用时会初始化 Python 解释器。

## 接口

### 调用与执行

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_call` | `(fn: string \| PyObject, ...args: PyObject[]) => PyObject` | 调用 Python 函数。**重载 1**：`fn` + 可变数量位置参数，所有 `args` 必须为 `PyObject`。 |
| | `(fn: string \| PyObject, args?: (...PyObject) \| PyObject[], kwargs?: Struct<...PyObject>) => PyObject` | **重载 2**：支持命名参数。`args` 为 `Tuple` 或 `Array<PyObject>` 传 `*args`；`kwargs` 为 `Struct`（字段值全为 `PyObject`）传 `**kwargs`。1–3 参均可，`args`/`kwargs` 可选。 |
| | | `fn` 为字符串时：单标识符（如 `"print"`）解析为 `builtins.print`；含 `.` 时（如 `"math.sqrt"`）按 `module.attr` 解析。也可传入 `PyObject` 作为可调用对象。 |
| `py_exec` | `(code: string) => PyObject` | 执行任意 Python 代码（语句），返回 `None`。 |
| `py_eval` | `(expr: string) => PyObject` | 计算表达式并返回结果。 |
| `py_run` | `(file_path: string) => PyObject` | 执行指定路径的 Python 脚本文件，返回 `None`。 |

### 模块与属性

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_import` | `(module_name: string) => PyObject` | 导入 Python 模块，返回模块对象。 |
| `py_attr` | `(obj: PyObject, attr: string) => PyObject` | 获取对象属性。`attr` 可含 `.` 表示链式访问（如 `"a.b.c"`）。 |
| `py_print` | `(...objs: PyObject[]) => PyObject?` | 调用 Python `print(..., end='')` 输出，不换行；透传首个参数。 |
| `py_println` | `(...objs: PyObject[]) => PyObject?` | 调用 Python `print(...)` 输出并换行；透传首个参数。 |

### 类型互转

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_wrap` | `(x: T) => PyObject<T>` | Camel → Python：将 Camel 值转为 Python 对象。支持 Int/Float/Bool/String/Array/Tuple/Struct 及嵌套。 |
| `py_unwrap` | `(obj: PyObject<T>) => T` | Python → Camel：将 Python 对象按目标类型转回 Camel。**必须**通过 `as PyObject<T>` 指定目标类型。 |

**`py_unwrap` 必须配合 `as` 使用**：`py_eval` / `py_call` 返回的是裸 `PyObject`（无类型参数），而 `py_unwrap` 需根据 `PyObject<T>` 的 `T` 决定返回类型并完成转换。因此必须先将结果用 `as PyObject<T>` 转为带泛型参数的 `PyObject<T>` 再传给 `py_unwrap`，例如：

```cml
let obj = py_call(some_fn, py_wrap(42))   // 返回 PyObject
let val = py_unwrap(obj as PyObject<int>) // 指定 T=int，返回 int
```

## 示例

```cml
import { py_call, py_eval, py_import, py_attr, py_exec, py_run, py_wrap, py_unwrap } from python

func main() sync {
    // 调用 builtins.print
    py_call("print", py_wrap("Hello from Camel!"))

    // 表达式求值
    let sum_obj = py_eval("1 + 2 * 3")
    py_call("print", sum_obj)  // 7

    // 导入模块并获取属性
    let math = py_import("math")
    let sqrt = py_attr(math, "sqrt")
    let result = py_call(sqrt, py_wrap(16))

    // 执行代码字符串
    py_exec("print('Exec: Hello!')")

    // 执行脚本文件
    py_run("scripts/hello.py")

    // 类型互转：py_unwrap 需配合 as PyObject<T> 指定目标类型
    let val = py_unwrap(py_eval("1+1") as PyObject<int>)

    // 命名参数：py_call(fn, args_tuple, kwargs_struct) 支持 *args 与 **kwargs
    py_exec("def _greet(name, greeting='Hello'): print(f'{greeting}, {name}!')")
    py_call("_greet", (py_wrap("World"),), { greeting: py_wrap("Hi") })  // 输出 Hi, World!
    return 0
}
```

## 测试

测试用例位于 `test/libs/python/`：

```bash
camel run test/libs/python/main.cml
```

## 与内置模块的区别

- **内置模块**：在 `builtinModuleFactories` 中注册，算子表在 `getOpsImplMap()` 中，随 libcamel 一起编译。
- **本模块**：不进入内置表，通过 `camel_module_create` 导出模块，在 `load()` 中注册 `python` 协议的 executor，算子实现全部在本 .cmo 中。

第三方可按此方式实现自己的 `.cmo` 模块。
