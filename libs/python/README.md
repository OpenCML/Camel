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
| `py_call` | `(fn: string \| PyObject, ...args) => PyObject` | 调用 Python 函数。`fn` 为字符串时：单标识符（如 `"print"`）解析为 `builtins.print`；含 `.` 时（如 `"math.sqrt"`）按 `module.attr` 解析。也可传入 `PyObject` 作为可调用对象。 |
| `py_exec` | `(code: string) => PyObject` | 执行任意 Python 代码（语句），返回 `None`。 |
| `py_eval` | `(expr: string) => PyObject` | 计算表达式并返回结果。 |
| `py_run` | `(file_path: string) => PyObject` | 执行指定路径的 Python 脚本文件，返回 `None`。 |

### 模块与属性

| 算子 | 签名 | 说明 |
|------|------|------|
| `py_import` | `(module_name: string) => PyObject` | 导入 Python 模块，返回模块对象。 |
| `py_attr` | `(obj: PyObject, attr: string) => PyObject` | 获取对象属性。`attr` 可含 `.` 表示链式访问（如 `"a.b.c"`）。 |

### 类型互转

| 算子 | 签名 | 说明 |
|------|------|------|
| `to_py` | `(x: T) => PyObject<T>` | Camel → Python：将 Camel 值转为 Python 对象。支持 Int/Float/Bool/String/Array/Tuple/Struct 及嵌套。 |
| `from_py` | `(obj: PyObject<T>) => T` | Python → Camel：将 Python 对象按目标类型转回 Camel。需指定泛型 `PyObject<T>`。 |

## 示例

```cml
import { py_call, py_eval, py_import, py_attr, py_exec, py_run, to_py, from_py } from python

func main() sync {
    // 调用 builtins.print
    py_call("print", to_py("Hello from Camel!"))

    // 表达式求值
    let sum_obj = py_eval("1 + 2 * 3")
    py_call("print", sum_obj)  // 7

    // 导入模块并获取属性
    let math = py_import("math")
    let sqrt = py_attr(math, "sqrt")
    let result = py_call(sqrt, to_py(16))

    // 执行代码字符串
    py_exec("print('Exec: Hello!')")

    // 执行脚本文件
    py_run("scripts/hello.py")

    // 类型互转：from_py 需传入带泛型参数的 PyObject<T>
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
