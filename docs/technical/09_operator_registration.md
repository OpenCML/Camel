# 算子注册

本文档说明 Camel 中算子（operator）如何声明、如何绑定到实现、如何声明语义性质（如纯度），以及全局内置模块与标准库模块（`.cmo`）在注册方式上的差异。

## 1. 概述

一个算子由三部分信息组成：

- **类型解析（Type Resolver）**：编译期根据实参类型选择重载、推断返回类型。
- **运行时实现（`operator_t`）**：执行时按算子 URI 找到的函数指针。
- **语义性质（`OperatorTraits`）**：算子是否为纯函数等，供常量折叠、公共子表达式消除、内联等图遍查询。

模块通过 `OperatorGroup` 导出算子声明，通过 `Executor` 提供实现，通过算子表 `OperatorTable` 声明性质。三者都以算子 URI 为键相互对应。

模块分两类：

| 类别 | 位置 | 协议名 | 加载方式 |
|------|------|--------|----------|
| 全局内置模块（`GlobalsBuiltinModule`） | `src/builtin/`，编入 libcamel | 空字符串 `""` | `Context::create` 时加载，其导出对每个用户模块默认可见 |
| 标准库与第三方模块 | `modules/<name>/`，各自编译为 `<name>.cmo` 动态库 | 模块名，如 `algo`、`math`、`tensor`、`python` | 被 `import` 时，或其协议下的算子首次被查找时加载 |

libcamel 内部的模块工厂表 `builtinModuleFactories`（`src/core/module/builtin.cpp`）只包含全局内置模块，以及调试构建下的 `profiler`。`algo`、`math`、`tensor`、`nn`、`os`、`io`、`json`、`python` 等都是 `modules/` 下的 `.cmo` 模块。

## 2. OperatorGroup 与类型解析

### 2.1 结构

`OperatorGroup`（`include/camel/core/operator.h`）包含：

- **name**：算子组名，如 `"sorted"`、`"__add__"`，即模块导出的名字。
- **resolvers**：`vector<pair<uri, resolver_ptr_t>>`，每个重载对应一个 URI 和一个类型解析器。

`OperatorGroup::resolve()` 按声明顺序依次尝试各重载，返回第一个接受实参的重载的 `OperatorIndex`（组名、函数类型、URI）。因此重载的先后顺序有意义。

```cpp
OperatorGroup::create(
    "sorted",
    {
        {
            "algo:sorted",
            DynamicFuncTypeResolver::create(
                {{0, {}}, {1, {false}}},
                "<> (array: T[]) => T[]",
                [](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
                    -> std::optional<Type *> {
                    if (norm[0]->code() != TypeCode::Array)
                        return std::nullopt;
                    return tt::as_ptr<ArrayType>(norm[0]);
                }),
        },
    });
```

构造 `OperatorGroup` 时，每个重载的解析器同时登记到算子表（`OperatorTable::setResolver`，`include/camel/core/operator_table.h`），以 URI 为键。图上的 `OPER` 节点只记录解析得到的算子编号（`OperId`）；改变了实参已知信息的遍（例如绑定输入形状）通过算子表重新运行对应重载的解析器。

### 2.2 类型解析器

解析器定义在 `include/camel/core/type/resolver.h`：

- **StaticFuncTypeResolver**：参数类型固定。实参个数须与声明一致，且每个实参可赋值给对应的形参类型。
- **DynamicFuncTypeResolver**：参数个数或类型可变，由 `ResolverFunc` 动态推断返回类型。`create` 的三个参数依次为参数声明、签名文本（用于诊断与文档）、`ResolverFunc`。

`DynamicFuncTypeResolver` 的参数声明是一对 `var_declare_t`，分别描述 `with` 参数与普通（`norm`）参数。`var_declare_t` 的格式为 `{count, vector<bool>}`：

- `count`：参数个数；`-1` 表示不限制个数。
- `vector<bool>`：各参数是否为 `var` 参数。

例如：

- `{{0, {}}, {2, {false, false}}}`：0 个 with，2 个 norm，均为非 var。
- `{{0, {}}, {-1, {}}}`：0 个 with，norm 个数不限。
- `{{0, {}}, {1, {true}}}`：1 个 norm，且为 var。

自定义的解析器可以继承 `FuncTypeResolver`，并按需重写：

- `resolveWith()`：在类型之外还能看到常量实参的编译期取值（`static_args_t`），用于返回类型依赖实参取值的算子（如由形状字面量决定结果形状的张量构造）。
- `explainRejection()`：当实参种类相符但被拒绝时，给出比“无匹配重载”更具体的原因。

### 2.3 数量检查由框架完成

`DynamicFuncTypeResolver::resolve()` 在调用 `ResolverFunc` 前会检查参数个数：当 `count != -1` 且实参个数不等于 `count` 时直接返回 `nullopt`。因此 `ResolverFunc` 内**无需再检查** `norm.size()` / `with.size()`。

对声明为 `var` 的参数，传给 `ResolverFunc` 的类型是变量所持值的类型，而不是变量本身的类型。

### 2.4 var_declare 一致性

构造函数会 `ASSERT`：当 `count != -1` 时，`count` 必须等于 `vector<bool>.size()`。写成 `{2, {false}}` 会触发断言。

## 3. URI 与 Executor 分派

### 3.1 URI 格式

算子 URI 格式：`protocol:suffix`。

- **protocol**：执行协议名。空字符串表示全局内置模块；其余为模块名，如 `algo`、`tensor`、`python`。
- **suffix**：协议内的算子标识。

示例：

- `:op/add_i` → protocol 为空，suffix=`op/add_i`
- `algo:sorted` → protocol=`algo`，suffix=`sorted`
- `python:py_call` → protocol=`python`，suffix=`py_call`

### 3.2 Executor 与 opsMap

`Executor`（`include/camel/execute/executor.h`）持有 `opsMap_`：`unordered_map<string, operator_t>`。`operator_t` 的签名为：

```cpp
using operator_t = slot_t (*)(ArgsView &with, ArgsView &norm, context::Context &ctx);
```

`ExecutorManager::find(uri)`（`src/execute/executor.cpp`）返回 URI 对应的 `operator_t`：

1. 按第一个 `:` 拆分得到 protocol 和 suffix；
2. 若该 protocol 的 Executor 已实例化，直接在其中查找 suffix；
3. 若该 protocol 尚未注册 Executor 工厂，以 protocol 为模块名调用 `Context::importModule`，由模块的 `load()` 注册工厂；
4. 首次使用时调用工厂实例化 Executor，再查找 suffix。

因此 **opsMap 的 key 必须是 suffix**（即 `:` 后的部分），而不是完整 URI。

第 3 步使一个模块的算子组可以引用另一个模块的实现：全局内置模块导出的 `sleep` 与 `exit` 的 URI 是 `os:sleep`、`os:exit`，首次查找时自动加载 `os` 模块，程序不必显式导入它。

找不到实现时 `find` 返回 `nullopt`，各调度器据此报告运行时诊断 `UnrecognizedOperatorURI`。

### 3.3 全局内置算子

- protocol 为空，由 `BasicBuiltinExecutor` 处理；`GlobalsBuiltinModule::load()` 以空协议名注册其工厂。
- 实现表是 `src/builtin/executors/builtin.cpp` 中的 `getOpsImplMap()`，key 形如 `op/itoi`、`op/add_i`、`str/format`、`struct/len_arr`、`coll/len`。
- 算子组在 `src/builtin/modules/globals.cpp` 中声明，URI 形如 `:op/add_i`、`:str/format`，suffix 与实现表的 key 一致。
- 各算子的实现按主题分布在 `src/builtin/operators/` 下（`ops`、`cast`、`str`、`struct`、`collection`、`tagged`、`io`、`macro` 等）。

### 3.4 `.cmo` 模块算子

以 `algo` 为例（`modules/algo/`）：

- `module.cpp`：`getOperatorGroups()` 声明算子组，URI 为 `algo:sorted`、`algo:sort` 等；模块构造函数逐个 `exportEntity(group->name(), group)`；`load()` 调用 `context_->registerExecutorFactory("algo", factory)`。
- `operators.cpp`：`getAlgoOpsMap()` 返回 suffix → `operator_t` 的映射，key 为 `sorted`、`sort` 等。
- `executor.cpp`：`createAlgoExecutor()` 用上述映射构造 `Executor` 的子类。

`.cmo` 模块的类继承 `camel::core::module::BuiltinModule`，以模块名作为名字。

## 4. 语义性质（纯度）注册

算子表 `OperatorTable`（`include/camel/core/operator_table.h`）为每个算子 URI 保存一项，项的编号是 `OperId`，`OPER` 节点的负载里存的就是这个编号（URI 文本驻留在符号表里，全进程只存一份）。表项上的 `OperatorTraits`：

| 字段 | 含义 |
|------|------|
| `pure` | 无副作用、无隐藏状态，结果只依赖实参。这样的调用可以在编译期求值、与相同调用合并、未被使用时删除。 |
| `elementwise` | 输出的第 i 个元素只依赖各输入（广播后）的第 i 个元素。 |
| `stateOnly` | 不是纯函数，但效果不超出程序自身的变量（创建或读取变量）。不会被折叠、合并或跨越写操作移动，也不作为用户可观察的效果报告。 |
| `mayFault` | 可能出故障（除零、越界、转换放不进目标类型）。**缺省为真**；确定不会出故障的才声明为假。目前没有任何遍依据它改变行为（图数据模型设计稿 Q1 待定）。 |
| `callsGraphs` | 执行中会调用实参里的函数值（`map`、`reduce` 等高阶算子与 `attempt`），它做什么取决于传进来的函数。 |

**没有登记的算子按保守方式处理，即视为有副作用。** 新增一个纯算子时若不登记，结果仍然正确，但通用图遍不会对它做折叠、合并或删除。

登记发生在模块加载时：

- 全局内置模块：`GlobalsBuiltinModule` 构造时调用 `registerBuiltinOperatorTraits()`（`src/builtin/executors/builtin.cpp`）。`op/` 下除 `op/assn` 原地赋值族之外的算子、`coll/` 下的全部算子，以及一份列明的字符串与数组辅助算子登记为纯；`:var/new`、`:var/get` 登记为 `stateOnly`；`io/`、`profiler/` 以及原地修改数组的算子不登记。
- `.cmo` 模块：在 `load()` 中直接调用 `OperatorTable::instance().set(uri, traits)`。例如 `modules/math/module.cpp` 把 `getMathOpsMap()` 中的全部算子登记为纯。

另有两处按 URI 或按值类型登记的信息，供需要的模块使用：

- 算子表上的类型折叠（`OperatorTable::setTypeFolder`）：当实参类型已能确定结果时（如类型带形状的张量的形状、元素个数），给出该常量结果。
- `ValueTypeRefinerRegistry`（`include/camel/core/operator_traits.h`）：给出常量的精确类型（如张量常量的 dtype 与形状）。

可微算子的反向规则登记在 `DerivativeRegistry`（`include/camel/core/derivative.h`）。

### 4.1 单一来源的算子定义（tensor）

`tensor` 模块不分别书写上述各表，而是用一份 `OpDef`（`modules/tensor/ops/op_def.h`）描述一个算子：名字（URI suffix）、导出名、参数、类型推断函数、kernel、性质、反向规则、类型折叠函数。`OpRegistry::add()`（`modules/tensor/ops/registry.cpp`）据此向各核心注册表登记，`OpRegistry::operatorGroups()` 与 `kernelMap()` 再分别生成模块导出的算子组和 Executor 的 opsMap。同一 URI 重复登记会抛出错误。详见 [25_tensor_stack_and_onnx_export.md](25_tensor_stack_and_onnx_export.md)。

## 5. 注册流程

### 5.1 全局内置算子

1. 在 `src/builtin/operators/` 中实现 `operator_t` 函数。
2. 在 `src/builtin/executors/builtin.cpp` 的 `getOpsImplMap()` 中加入 suffix → `operator_t`。
3. 在 `src/builtin/modules/globals.cpp` 的算子组中加入重载，URI 为 `:` 加上同一 suffix。
4. 若算子是纯函数且不在 `registerBuiltinOperatorTraits()` 已覆盖的前缀之内，把它加入该函数的纯算子列表。

### 5.2 `.cmo` 模块

1. 在 `modules/<name>/` 下建立模块目录，`CMakeLists.txt` 中 `include` `modules/camel_module.cmake` 并调用 `add_camel_module(NAME <name>)`。默认源文件为 `executor.cpp`、`module.cpp`、`operators.cpp`，可用 `SOURCES` 指定；输出为 `<name>.cmo`。
2. 该 CMake 函数会自动编入 `modules/camel_module_abi.cpp`，由它导出 `camel_module_abi_version()`。加载器（`loadCmoModule`，`include/camel/core/module/dynamic.h`）先比对 `CAMEL_MODULE_ABI_VERSION`，不一致则拒绝加载。
3. 实现并导出 `extern "C" Module *camel_module_create(Context *ctx)`，返回新建的模块对象，所有权交给运行时。
4. 在模块构造函数中用 `exportEntity()` 导出各 `OperatorGroup`（需要时用 `exportType()` 导出类型），URI 使用 `<name>:suffix` 格式。
5. 在 `load()` 中调用 `registerExecutorFactory("<name>", factory)`，factory 创建持有 suffix → `operator_t` 映射的 Executor；并在此登记纯算子的 `OperatorTraits`。
6. 确保 opsMap 的 key 与算子组中 URI 的 suffix 一一对应。

导入模块时，`Context` 先在模块搜索路径中查找 `<name>.cmo` 或同名源文件，找不到再查 libcamel 内部的模块工厂表。

## 6. 注意事项汇总

| 事项 | 说明 |
|------|------|
| **URI 与 opsMap key** | opsMap 的 key 必须是 URI 的 suffix（`:` 后），不是完整 URI。 |
| **协议名与模块名** | 非空 protocol 必须等于提供实现的模块名：未注册的 protocol 会被当作模块名导入。 |
| **重载顺序** | 同一算子组内按声明顺序匹配，第一个接受实参的重载生效。 |
| **数量检查** | `DynamicFuncTypeResolver` 已做参数个数检查，`ResolverFunc` 中无需再检查 `norm.size()` / `with.size()`。 |
| **var_declare 一致** | `{count, vector<bool>}` 中 `count != -1` 时，`count` 必须等于 `vector<bool>.size()`。 |
| **纯度默认保守** | 未在算子表中声明的算子视为有副作用、可能出故障；只有确实不读写外部状态的算子才能登记为 `pure`。 |
| **工厂只注册一次** | 同一 protocol 重复调用 `registerExecutorFactory` 会触发断言；`load()` 应以 `loaded_` 判重。 |
| **Executor 加载时机** | Executor 按需懒加载，首次查找该 protocol 的算子时才实例化。并行调度器可能从多个工作线程同时查找，`ExecutorManager` 内部已加锁。 |
| **`load()` 的职责** | `load()` 中应完成 Executor 工厂注册、性质登记和所需初始化（如 Python 解释器）。 |

## 7. 纯度声明的规则与检查

设计原则 2.9：算子的纯度必须声明；没有声明的算子按有副作用对待（少优化，不会算错）。

- 模块在 `load()` 里为**每一个**算子声明，有副作用的也要写出来：

  ```cpp
  camel::core::OperatorTable::instance().declare(
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

## 8. 签名文件与标准库参考

- `stdlib/signatures/<模块>.cml` 逐个声明模块导出的算子（`globals.cml` 是内建函数）：`///` 注释写说明，
  `@pure` 或 `@effect <原因>`、`@fails <失败时的行为>`、`@example <代码>    // <结果>`，函数体里
  `return '<URI>'`（`*` 通配一组只在类型上不同的重载）。这些文件不参与编译。
- 用 Camel 写的库（`stdlib/<模块>.cml`）本身就是签名文件：读它的 `export func` 与 `///` 注释。
- `node scripts/stdlib-reference.mjs --write` 由它们生成 `docs/stdlib-reference.md`；`--verify-doc` 检查
  文档是最新的；`--verify-ops` 用 `camel inspect --ops` 的结果核对签名文件：每个已注册的算子都有声明、
  每条声明都对应一个已注册的算子、参数个数一致、`@pure`/`@effect` 与登记的纯度一致。三者都在默认测试
  集里（同一个测试计划）。新增或修改算子后：改签名文件，运行 `--write`，提交生成的文档。

## 9. 参考

- `include/camel/core/operator.h`：`operator_t`、`ArgsView`、`OperatorGroup`、`OperatorIndex`。
- `include/camel/core/operator_table.h`：`OperId`、`OperatorTraits`、`OperatorTable`（性质、类型解析器、类型折叠）；`include/camel/core/symbol.h`：符号表。
- `include/camel/core/operator_traits.h`：`ValueTypeRefinerRegistry`。
- `include/camel/core/type/resolver.h`：`FuncTypeResolver`、`StaticFuncTypeResolver`、`DynamicFuncTypeResolver`。
- `include/camel/execute/executor.h`、`src/execute/executor.cpp`：`Executor`、`ExecutorManager`，URI 解析与分派。
- `include/camel/core/module/builtin.h`、`include/camel/core/module/dynamic.h`：`BuiltinModule`、`.cmo` 的 ABI 与加载入口。
- `src/builtin/modules/globals.cpp`、`src/builtin/executors/builtin.cpp`：全局内置模块的算子组、实现表与性质登记。
- `modules/camel_module.cmake`：`.cmo` 模块的 CMake 封装。
- `modules/algo/`：结构最简单的 `.cmo` 模块示例。
- `modules/python/`：带外部运行时初始化的 `.cmo` 模块示例（`python.cmo`）。
- `modules/tensor/ops/`：以 `OpDef` 为单一来源的算子注册。
- `include/camel/core/module/introspect.h`：列出模块的算子（`camel inspect --ops`）。
- `modules/str/`：一个只有纯算子的小模块，可作新模块的样板；`modules/python/`：带外部运行时的模块。
