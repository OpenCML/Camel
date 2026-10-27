# python 模块（.cmo 外源模块示例）

本目录将 **python** 模块单独编译为动态库 `python.cmo`，作为“外源模块”加载示例。通过 pybind11 嵌入 Python 解释器，可在 Camel 中调用 Python 函数、执行脚本、进行类型互转。

## 构建

在项目根目录执行 `npm run build`（或 `debug` / `profile`）后，构建产物包含 `python.cmo`。需安装 Python 开发环境，CMake 会通过 Conan 获取 pybind11。

## 使用

`python` 为标准库模块，随 Camel 发布，直接 `import python` 即可。若自编译或需单独部署，可将 `python.cmo` 放置在 Camel 能访问的模块搜索路径下。`import` 不执行代码（设计原则 3.1）：导入只注册执行器与算子，Python 解释器在第一次调用任一 `python` 算子时创建；若 `pyplot` 已经创建了它，这里只附着，不会新建第二个。

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
| `exec` | `(code: string) => void!` | 执行任意 Python 代码（语句）；定义与赋值对之后的调用可见。 |
| `eval` | `(expression: string) => PyObject!` | 计算表达式并返回结果。 |
| `run_file` | `(path: string) => void!` | 执行指定路径的 Python 脚本文件。 |
| `call` | `with <fn: PyObject, kwargs: Map<string, PyObject>? = null> (args: PyObject[]) => PyObject!` | 以数组里的 `PyObject` 为位置参数、`kwargs` 为关键字参数调用 `fn`：`[must(to_py(2.0))]->call<sqrt>`。 |

### 模块与属性

| 算子 | 签名 | 说明 |
|------|------|------|
| `import_module` | `(name: string) => PyObject!` | 导入 Python 模块，返回模块对象。 |
| `attr` | `with <name: string> (obj: PyObject) => PyObject!` | 获取对象属性：`m->attr<'pi'>`。`name` 可含 `.` 表示链式访问（如 `"a.b.c"`）。 |
| `to_string` | `(obj: PyObject) => string!` | Python 的 `str(obj)`。 |

### 类型互转

| 算子 | 签名 | 说明 |
|------|------|------|
| `to_py` | `(x: T) => PyObject!` | Camel → Python：将 Camel 值转为 Python 对象。支持 Int/Float/Bool/String/Bytes/Array/Tuple/Struct/Map 及嵌套。 |
| `from_py` | `(obj: PyObject) => T!` | Python → Camel：将 Python 对象按目标类型转回 Camel；类型写在调用处：`obj->from_py::<int>`。对象不是 `T` 所要的那种就是 `PythonTypeError`（`bool` 只收 bool，不收 `5`；`int` 不收 bool 与 float；`string` 不收 bytes；`f32` 放不下 `1e300`），消息给出路径与所见的类型。 |

**Python 一侧的失败是错误值，不是故障**：调用、求值、导入、取属性、取文本的结果是 `T!`，Python 抛出的异常是种类 `PythonError` 的错误值，消息以异常类名开头（`KeyError: ...`），后接回溯；用 `!!`、`->` 或 `match` 拆开。参数个数、类型不对仍是故障。

**目标类型写在调用处**：`eval` / `call` 返回的是 `PyObject`，`from_py` 的返回类型 `T` 由调用写出（`from_py::<int>(obj)`）或取自结果所在位置的类型，例如：

```cml
let obj = must([to_py(42)]->call<some_fn>)   // PyObject
let val = obj->from_py::<int> !! 0           // T=int，返回 int
```

## 示例

```cml
import { import_module, eval, exec, run_file, attr, call, to_string, to_py, from_py, PyObject } from python

func must(r: PyObject!): PyObject {
    return match r {
        case error e => fail<'InvalidArgument'>(e.message)
        case v => v
    }
}

func main(): int sync {
    // 调用 Python 函数
    let print = must(import_module("builtins")->attr<"print">)
    let _ = [must(to_py("Hello from Camel!"))]->call<print>

    // 计算表达式
    let sum_obj = must(eval("1 + 2 * 3"))
    let _ = [sum_obj]->call<print>  // 7

    // 导入模块、取属性、调用
    let math = must(import_module("math"))
    let sqrt = must(math->attr<"sqrt">)
    let result = must([must(to_py(16))]->call<sqrt>)
    let _ = [result]->call<print>  // 4.0

    // 执行代码与脚本
    let _ = exec("print('Exec: Hello!')")
    let _ = run_file("scripts/hello.py")

    // Python → Camel
    let val = must(eval("1+1"))->from_py::<int> !! 0
    println(val, to_string(result) !! "")

    // 位置参数与关键字参数
    let _ = exec("def _greet(name, greeting='Hello'): print(f'{greeting}, {name}!')")
    let greet = must(import_module("__main__")->attr<"_greet">)
    let hi = must(to_py("Hi"))
    let kwargs = [("greeting", hi)] as Map<string, PyObject>
    let _ = [must(to_py("World"))]->call<greet, kwargs>  // 输出 Hi, World!
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
