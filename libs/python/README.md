# python 模块（.cmo 外源模块示例）

本目录将 **python** 模块单独编译为动态库 `python.cmo`，作为“外源模块”加载示例，而非内置模块。

## 构建

在项目根目录执行 `npm run build`（或 `debug` / `profile`）后，构建脚本会将 `libs/` 下各模块的 `.cmo` 复制到 **`stdlib/`**，例如 `stdlib/python.cmo`。

## 使用

1. 将 **`stdlib`** 加入 Camel 的**模块搜索路径**（如 `searchPaths: ["stdlib"]` 或入口同目录下的 `stdlib/`），或确保运行时能找到 `stdlib/python.cmo`。
2. 在 Camel 源码中：`import python`。
3. 运行时会在搜索路径中查找 `python.cmo` 并加载，无需把 python 注册进内置模块。

## 接口

- `python.eval(fn: string, ...args) => any`：调用 Python 可调用对象，`fn` 如 `"mymod.myfunc"`。
- `python.to_py(x: T) => any`：将 Camel 值转为 Python 对象（any）。
- `python.from_py_int` / `from_py_float` / `from_py_bool` / `from_py_string(x: any) => T`：将 any 转回对应 Camel 类型。

## 与内置模块的区别

- **内置模块**：在 `builtinModuleFactories` 中注册，算子表在 `getOpsImplMap()` 中，随 libcamel 一起编译。
- **本模块**：不进入内置表，通过 `camel_module_create` 导出模块，在 `load()` 中注册 `python` 协议的 executor，算子实现全部在本 .cmo 中。

第三方可按此方式实现自己的 `.cmo` 模块。
