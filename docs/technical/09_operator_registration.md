# 算子注册

本文档说明如何在 Camel 中注册算子（operator），包括类型解析、实现映射及内置模块与外源模块（.cmo）的差异。

## 1. 概述

算子由两部分组成：

- **类型解析（Type Resolver）**：编译期根据实参类型选择重载、推断返回类型。
- **运行时实现（operator_t）**：执行时由 Executor 按 URI 分派到具体函数。

模块通过 `OperatorGroup` 导出算子声明，通过 `Executor` / `ExecutorManager` 绑定实现。内置模块与动态模块的注册流程略有不同。

## 2. OperatorGroup 与类型解析

### 2.1 结构

`OperatorGroup`（`core/operator.h`）包含：

- **name**：算子组名，如 `"py_call"`、`"sort"`，用于模块内引用。
- **resolvers**：`vector<pair<uri, resolver_ptr_t>>`，每个重载对应一个 URI 和类型解析器。

```cpp
OperatorGroup::create(
    "py_attr",
    {
        {
            "python:py_attr",
            DynamicFuncTypeResolver::create(/* ... */),
        },
    });
```

### 2.2 类型解析器

- **StaticFuncTypeResolver**：参数类型固定，直接匹配。
- **DynamicFuncTypeResolver**：参数个数或类型可变，通过 ResolverFunc 动态推断。

`var_declare_t` 格式：`{count, vector<bool>}`

- `count`：`with` / `norm` 参数个数；`-1` 表示不限制（可变参数）。
- `vector<bool>`：各参数是否为可变参数（如 `var`）。

例如：

- `{{0, {}}, {2, {false, false}}}`：0 个 with，2 个 norm，均为非 var。
- `{{0, {}}, {-1, {}}}`：0 个 with，norm 个数不限。
- `{{0, {}}, {1, {true}}}`：1 个 norm，且为 var。

### 2.3 数量检查由框架完成

`DynamicFuncTypeResolver::resolve()` 在调用 `ResolverFunc` 前会检查参数个数：当 `count != -1` 且 `with.size() != count`（或 `norm` 同理）时直接返回 `nullopt`。因此，ResolverFunc 的 lambda 内**无需再检查** `norm.size()` / `with.size()`，框架已保证数量正确。

### 2.4 var_declare 一致性

构造函数会 `ASSERT`：当 `count != -1` 时，`count` 必须等于 `vector<bool>.size()`。若写成 `{2, {false}}` 会触发运行时断言。建议编写时仔细核对两者一致。

## 3. URI 与 Executor 分派

### 3.1 URI 格式

算子 URI 格式：`protocol:suffix`。

- **protocol**：执行协议名，如 `"python"`、`"tensor"`；空字符串 `""` 表示内置算子。
- **suffix**：协议内的算子标识，如 `py_call`、`algo/sorted`。

示例：

- `python:py_call` → protocol=`python`，suffix=`py_call`
- `:algo/sorted` → protocol=``，suffix=`algo/sorted`

### 3.2 Executor 与 opsMap

`Executor` 持有一个 `opsMap_`：`unordered_map<string, operator_t>`。`ExecutorManager::eval(uri)` 会：

1. 按 `:` 拆分得到 protocol 和 suffix；
2. 按 protocol 选择 Executor；
3. 调用 `executor->eval(suffix, ...)`。

因此 **opsMap 的 key 必须是 suffix**（即 `:` 后的部分），而不是完整 URI。

### 3.3 内置算子

- protocol 为空，由 `BasicBuiltinExecutor` 处理。
- `getOpsImplMap()` 的 key 形如：`algo/sorted`、`op/itoi`、`tensor/reshape` 等。
- OperatorGroup 中的 URI 形如 `:algo/sorted`、`:op/itoi`，suffix 与 opsMap key 一致。

### 3.4 动态模块算子（如 python）

- protocol 为模块名，如 `python`。
- 在 `load()` 中调用 `context_->registerExecutorFactory("python", factory)`。
- `getPythonOpsMap()` 的 key 为 suffix：`py_call`、`py_attr` 等。
- OperatorGroup 中的 URI 为 `python:py_call`、`python:py_attr`，suffix 与 opsMap key 必须一致。

## 4. 注册流程

### 4.1 内置模块

1. 在模块的 `getOperatorGroups()` 中创建 `OperatorGroup`，并为每个重载提供 URI 和 `FuncTypeResolver`。
2. 在 `exportEntity()` 中导出算子组。
3. 在 `src/builtin/executors/builtin.cpp` 的 `getOpsImplMap()` 中注册 URI suffix → `operator_t`。
4. 若模块使用独立 protocol（如 `tensor`），需在模块 `load()` 中调用 `registerExecutorFactory`，并实现对应 Executor。

### 4.2 外源模块（.cmo）

1. 实现 `camel_module_create(Context*)` 并返回 `Module*`。
2. 在 `Module::load()` 中：
   - 调用 `registerExecutorFactory(protocolName, factory)`；
   - 在 factory 中创建 Executor，传入 `getXxxOpsMap()`（suffix → `operator_t`）。
3. 在 `exportEntity()` 中导出 `OperatorGroup`，URI 使用 `protocol:suffix` 格式。
4. 确保 `getXxxOpsMap()` 的 key 与 OperatorGroup 中 URI 的 suffix 一一对应。

## 5. 注意事项汇总

| 事项 | 说明 |
|------|------|
| **URI 与 opsMap key** | opsMap 的 key 必须是 URI 的 suffix（`:` 后），不是完整 URI。 |
| **数量检查** | `DynamicFuncTypeResolver` 已做参数个数检查，ResolverFunc 中无需再检查 `norm.size()` / `with.size()`。 |
| **var_declare 一致** | `{count, vector<bool>}` 中 `count != -1` 时，`count` 必须等于 `vector<bool>.size()`。 |
| **protocol 与 Executor** | 非空 protocol 的模块必须在 `load()` 中注册 `registerExecutorFactory`，否则运行时报 `UnrecognizedExecutorProtocol`。 |
| **Executor 加载时机** | Executor 按需懒加载，首次调用该 protocol 的算子时才实例化。 |
| **模块导出顺序** | 先 `load()` 再使用；`load()` 中应完成 executor 注册和所需初始化（如 Python 解释器）。 |

## 6. 纯度声明

设计原则 2.9：算子的纯度必须声明；没有声明的算子按有副作用对待（少优化，不会算错）。

- 模块在 `load()` 里为**每一个**算子声明，有副作用的也要写出来：

  ```cpp
  camel::core::OperatorTraitsRegistry::instance().declare(
      "fs",
      {
          // 读取的结果取决于当时文件系统里有什么
          {"read_text", false},
          {"exists", false},
          {"write_text", false},
      });
  ```

  `declare(protocol, {{name, pure}, ...})` 逐个登记 `protocol:name`；需要 `elementwise` 等其他性质时用
  `set(uri, traits)`。
- **纯**的含义：结果只取决于实参。读时钟、随机数发生器、环境变量、文件的算子都不纯；原地修改实参
  （形参声明为 `var`）的算子不纯。登记为纯之后，`std::opt` 会提前求值、合并相同的调用、删除无人使用的
  调用，并行调度器会并发执行它。
- `camel inspect --ops <文件>` 列出内建模块与该程序导入的每个模块的全部算子：每个重载一行
  `OPERATOR {json}`，含模块、名字、URI、`with`/普通参数个数（`-1` 表示不限，`-2` 表示解析器没有给出）、
  声明的纯度（`pure`、`effectful`、`state`、`undeclared`）与签名。
- 从文件加载的模块如有未声明的算子，加载时给出一条 warn 级日志（`camel -v ...` 可见），列出这些算子；
  它们仍按有副作用对待。
- 仓库内的模块由测试约束（`test/plans/feat/modules/stdlib_registry.plan.toml`）：不得有未声明的算子。

## 7. 签名文件与标准库参考

- `stdlib/signatures/<模块>.cml` 逐个声明模块导出的算子（`globals.cml` 是内建函数）：`///` 注释写说明，
  `@pure` 或 `@effect <原因>`、`@fails <失败时的行为>`、`@example <代码>    // <结果>`，函数体里
  `return '<URI>'`（`*` 通配一组只在类型上不同的重载）。这些文件不参与编译。
- 用 Camel 写的库（`stdlib/<模块>.cml`）本身就是签名文件：读它的 `export func` 与 `///` 注释。
- `node scripts/stdlib-reference.mjs --write` 由它们生成 `docs/stdlib-reference.md`；`--verify-doc` 检查
  文档是最新的；`--verify-ops` 用 `camel inspect --ops` 的结果核对签名文件：每个已注册的算子都有声明、
  每条声明都对应一个已注册的算子、参数个数一致、`@pure`/`@effect` 与登记的纯度一致。三者都在默认测试
  集里（同一个测试计划）。新增或修改算子后：改签名文件，运行 `--write`，提交生成的文档。

## 8. 参考

- `include/camel/core/operator.h`：OperatorGroup、OperatorIndex。
- `include/camel/core/type/resolver.h`：StaticFuncTypeResolver、DynamicFuncTypeResolver。
- `include/camel/execute/executor.h`：Executor、ExecutorManager。
- `src/execute/executor.cpp`：URI 解析与 Executor 分派逻辑。
- `include/camel/core/operator_traits.h`：纯度等算子性质的登记。
- `include/camel/core/module/introspect.h`：列出模块的算子（`camel inspect --ops`）。
- `modules/str/`：一个只有纯算子的小模块，可作新模块的样板；`modules/python/`：带外部运行时的模块。
