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

libcamel 内部的模块工厂表 `builtinModuleFactories`（`src/core/module/builtin.cpp`）只包含全局内置模块，以及调试构建下的 `profiler`。`algo`、`math`、`tensor`、`nn`、`process`、`io`、`json`、`python` 等都是 `modules/` 下的 `.cmo` 模块。

## 2. OperatorGroup 与类型解析

### 2.1 结构

`OperatorGroup`（`include/camel/core/operator.h`）包含：

- **name**：算子组名，如 `"sort"`、`"__add__"`，即模块导出的名字。
- **resolvers**：`vector<pair<uri, resolver_ptr_t>>`，每个重载对应一个 URI 和一个类型解析器。

`OperatorGroup::resolve()` 按声明顺序依次尝试各重载，返回第一个接受实参的重载的 `OperatorIndex`（组名、函数类型、URI）。因此重载的先后顺序有意义。

```cpp
OperatorGroup::create(
    "sort",
    {
        {
            "algo:sort",
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
- `algo:sort` → protocol=`algo`，suffix=`sort`
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

第 3 步使一个模块的算子组可以引用另一个模块的实现：全局内置模块导出的 `sleep` 与 `exit` 的 URI 是 `process:sleep`、`process:exit`，首次查找时自动加载 `process` 模块，程序不必显式导入它。

找不到实现时 `find` 返回 `nullopt`，各调度器据此报告运行时诊断 `UnrecognizedOperatorURI`。

### 3.3 全局内置算子

- protocol 为空，由 `BasicBuiltinExecutor` 处理；`GlobalsBuiltinModule::load()` 以空协议名注册其工厂。
- 实现表是 `src/builtin/executors/builtin.cpp` 中的 `getOpsImplMap()`，key 形如 `op/itoi`、`op/add_i`、`str/format`、`struct/len_arr`、`coll/len`。
- 算子组在 `src/builtin/modules/globals.cpp` 中声明，URI 形如 `:op/add_i`、`:str/format`，suffix 与实现表的 key 一致。
- 各算子的实现按主题分布在 `src/builtin/operators/` 下（`ops`、`cast`、`str`、`struct`、`collection`、`tagged`、`io`、`macro` 等）。

### 3.4 `.cmo` 模块算子

以 `algo` 为例（`modules/algo/`）：

- `module.cpp`：`getOperatorGroups()` 声明算子组，URI 为 `algo:sort`、`algo:merge_sort` 等；模块构造函数逐个 `exportEntity(group->name(), group)`；`load()` 调用 `context_->registerExecutorFactory("algo", factory)`。
- `operators.cpp`：`getAlgoOpsMap()` 返回 suffix → `operator_t` 的映射，key 为 `sort`、`sort_in_place` 等（`*_in_place` 是 `xs = xs->sort` 用的就地形式，见 `OperatorTable::setInPlaceForm`）。
- `executor.cpp`：`createAlgoExecutor()` 用上述映射构造 `Executor` 的子类。

`.cmo` 模块的类继承 `camel::core::module::BuiltinModule`，以模块名作为名字。

### 3.5 模块导出的值

C++ 模块也可以导出值（设计原则 10.1）：在构造函数里 `exportValue(Reference("pi"), std::make_shared<DoubleData>(std::numbers::pi))`（`Module::exportValue`，公开接口 `include/camel/core/module/module.h`）。程序写 `math::pi`（或 `import { pi } from math` 后写 `pi`），不带括号；用到的地方就是这个常量，与写一个字面量相同：图上是使用处所在的图里的一个 `DATA` 节点，没有 root 上的节点，也不需要捕获。能导出的是能作为图中常量的数据（标量、字符串以及它们组成的复合数据），类型就是数据的类型。同一个名字只能导出一样东西：一个值与一个算子组不能同名。

## 4. 语义性质（纯度）注册

算子表 `OperatorTable`（`include/camel/core/operator_table.h`）为每个算子 URI 保存一项，项的编号是 `OperId`，`OPER` 节点的负载里存的就是这个编号（URI 文本驻留在符号表里，全进程只存一份）。表项上的 `OperatorTraits` **不是另存的一份声明**：每个字段都由该算子的语义记录 `sem::OperatorSemantics` 的对应事实派生（`OperatorTable::traitsOf`，`src/core/operator_table.cpp`），派生是唯一的映射。`set(uri, traits)` 登记的是记录上的效果事实（`effect`、`elementwise`、`callsGraphs`、`readsGraphs`、`faults`），`declareSemantics` 登记记录的其余部分，两者写同一份记录、互不覆盖。因此不可能出现“trait 说纯、语义记录说会写”的分歧：只有一份事实。

| 字段 | 含义 | 记录上的来源 |
|------|------|------|
| `pure` | 无副作用、无隐藏状态，结果只依赖实参。这样的调用可以在编译期求值、与相同调用合并、未被使用时删除。 | `effect == Pure` |
| `elementwise` | 输出的第 i 个元素只依赖各输入（广播后）的第 i 个元素。 | `elementwise`，或 `binary.elementwise` |
| `stateOnly` | 不是纯函数，但效果不超出程序自身的变量（创建或读取变量）。不会被折叠、合并或跨越写操作移动，也不作为用户可观察的效果报告。 | `effect == StateOnly` |
| `mayFault` | 可能出故障（除零、越界、转换放不进目标类型）。**缺省为真**；故障表为空表示确定不会出故障。目前没有任何遍依据它改变行为（图数据模型设计稿 Q1 待定）。 | `faults`：缺省→真，空表→假，非空→真 |
| `callsGraphs` | 执行中会调用实参里的函数值（`map`、`reduce` 等高阶算子与 `attempt`），它做什么取决于传进来的函数。 | `callsGraphs`，或声明了 `elementCalls` |
| `readsGraphs` | 读实参里的函数值的图（编译、导出、微分），而不只是调用它们。 | `readsGraphs` |

遍与调度器按编号认算子，不比较 URI 文本：

- 核心认得的算子有编译期常量形式的编号（`camel::core::oper::VarNew`、`VarGet`、`MapArr` 等 `:mark/` 算子、`Isolate`），算子表构造时按固定顺序先驻留它们的 URI；
- 模块自己的遍要认本模块的算子时，把 URI 驻留一次（函数内的静态量）再比较编号，例如 `autodiff:grad`、`tensor:matmul`、`onnx:export_model`；
- 三个族是表项上的标志，在 URI 第一次驻留时由前缀定下：`isBuiltin`（`:` 开头，没有模块协议）、`isMarked`（`:mark/`，调度器自己执行、会调用收到的函数值）、`isAssignment`（`:op/assn` 开头的赋值与复合赋值）。

URI 仍是调度器向执行器管理器查实现时的键，也是诊断里显示的名字。

**没有登记的算子按保守方式处理，即视为有副作用。** 新增一个纯算子时若不登记，结果仍然正确，但通用图遍不会对它做折叠、合并或删除。

登记发生在模块加载时：

- 全局内置模块：`GlobalsBuiltinModule` 构造时调用 `registerBuiltinOperatorTraits()`（`src/builtin/executors/builtin.cpp`）。`op/` 下除 `op/assn` 原地赋值族之外的算子、`coll/` 下的全部算子，以及一份列明的字符串与数组辅助算子登记为纯；`:var/new`、`:var/get`、`:var/peek`、`:var/take` 与各算子的原地形式（`:struct/append_arr_in_place`、`:op/set_index_in_place`）登记为 `stateOnly`；`io/`、`profiler/` 不登记。`append`、`extend` 返回新数组，登记为纯（设计原则 11.3）；`:op/assn_owned`（写成语句的 `acc = acc->append<v>`、`xs = xs->sort`）属于 `op/assn` 族，不纯。
- `.cmo` 模块：在 `load()` 中直接调用 `OperatorTable::instance().set(uri, traits)`。例如 `modules/math/module.cpp` 把 `getMathOpsMap()` 中的全部算子登记为纯。

另有两处按 URI 或按值类型登记的信息，供需要的模块使用：

- 算子表上的类型折叠（`OperatorTable::setTypeFolder`）：当实参类型已能确定结果时（如类型带形状的张量的形状、元素个数），给出该常量结果。
- `ValueTypeRefinerRegistry`（`include/camel/core/operator_traits.h`）：给出常量的精确类型（如张量常量的 dtype 与形状）。

可微算子的反向规则登记在 `DerivativeRegistry`（`include/camel/core/derivative.h`）。

### 4.1 单一来源的算子定义（tensor）

`tensor` 模块不分别书写上述各表，而是用一份 `OpDef`（`modules/tensor/ops/op_def.h`）描述一个算子：名字（URI suffix）、导出名、参数、类型推断函数、kernel、性质、反向规则、类型折叠函数。`OpRegistry::add()`（`modules/tensor/ops/registry.cpp`）据此向各核心注册表登记，`OpRegistry::operatorGroups()` 与 `kernelMap()` 再分别生成模块导出的算子组和 Executor 的 opsMap。同一 URI 重复登记会抛出错误。详见 [25_tensor_stack_and_onnx_export.md](25_tensor_stack_and_onnx_export.md)。

### 4.2 算子语义：数值、下标与归约律

同一份语义记录（`include/camel/core/operator_semantics.h`；设计见 [算子语义协议](../design/op-semantics/README.cn.md)）既带 §4 的效果事实，也带数值、下标、归约律等部分，由模块在加载时用 `OperatorTable::declareSemantics(uri, record)` 登记，登记前检查（键与 URI 一致、版本不降、数值部分自洽、归约律自洽；失败抛 `std::invalid_argument`）。`set` 先登记的效果事实在 `declareSemantics` 时保留，反之亦然。查询 `semantics(id)` 无锁；没有记录、或记录缺某一部分，读法都是保守的。

| 部分 | 内容 |
|------|------|
| `key` | `protocol:name@主.次`，跨进程的稳定标识；结果、故障、访问改变时主版本加一，只增加事实时次版本加一 |
| `effect` | 效果事实：`Pure`、`StateOnly`、`Effectful`；未声明（`Undeclared`）按 `Effectful` 读。§4 的 `pure`/`stateOnly` 由它派生 |
| `elementwise` | 逐元素事实；`binary.elementwise` 也计入 §4 的 `elementwise` |
| `callsGraphs` / `readsGraphs` | 调用/读取实参里的函数值；`elementCalls` 蕴含 `callsGraphs` |
| `numeric` | 精确、正确舍入（规范算法）、有 ulp 上界、不透明；舍入所在类型；对哪些 `@numeric` 许可敏感 |
| `faults` | 会出的故障（可缺省，缺省时按“可能出故障”读；空表表示不会出故障） |
| `indexing` | 实例化函数（`include/camel/core/index_semantics.h`）：一次调用的迭代域、各操作数的访问映射、写映射 |
| `laws` | 归约律（每种元素类型一条）：运算、输入/累加/结果类型、初值、顺序、空域、确切的结合与交换性、单位元 |

**下标语义是函数，不是数据**：同一个算子在不同秩、不同常量实参下的映射不同，所以登记的是 `InstantiateFn`，它对一次调用的类型（张量类型带形状，未知维为 -1）与常量实参返回 `IndexedInstance`，事实不足时返回 `nullopt`（秩未知、常量不是常量、某一维可能是 1 的广播）。表达式是准仿射的（常数、维、参数、和、乘以常数或参数、按正常数或正参数的 floordiv 与 mod），按实例驻留。查询：

- `sem::instantiate(id, InstanceContext{norm 类型, with 类型, statics})`：调用实例化函数，再做结构检查（`sem::validate`）；抛异常或检查不过都当作没有实例。
- `runtime::instanceOf(graph, node)`（`include/camel/runtime/operator_instance.h`）：冻结图上一个 `OPER` 节点的实例，事实取自输入节点的类型与静态 `DATA` 输入；`runtime::reorderFreedomOf(graph, node)` 再结合节点的许可列给出这次归约能否分组、置换（`sem::reorderFreedom`）。
- `sem::toString(instance, semantics)`：规范的文本形式；`camel inspect --instances <文件>` 对程序里每个声明了下标语义的算子节点打印一行 `INSTANCE {json}`。
- `sem::splitDims`（分块，原来的界成为 `guard`）、`sem::rowMajorView`（访问与行主序存储复合得到偏移与步长）。

`tensor` 模块的声明：数值部分在 `modules/tensor/ops/semantics.cpp` 的一张表里；下标语义与归约律在 `modules/tensor/ops/indexing.cpp`，与类型推断用同一个 `InferContext`，放在 `OpDef::index`（与 `OpDef::infer` 并列），注册时由 `OpRegistry::add` 包成 `indexing` 交给算子表。共用的构造工具是 `IndexBuilder`（`ops/indexing.h`；`nn` 模块的池化也用它）。已经持有 `InferContext` 的后端可以直接调 `ops::instantiate(def, ctx)`。内建的 `:op/` 标量算子与 `:mark/unordered_reduce_arr` 的记录在 `src/builtin/executors/builtin_semantics.cpp`。

**声明是代码，登记时查不出它与内核是否一致**，所以每个声明都要过声明求值器：`camel test/cases/modules/tensor/index_semantics.cml tensor::test_index_eval` 对每个已声明的算子生成大量随机调用（空、长度 1、广播、负轴、可选参数缺省、NaN、±∞、±0、次正规数、int64 与 bool），只按声明（域、访问映射及其越界策略、写映射、归约律）算出结果，与 CPU 内核逐位比较（有上界的 `exp`、`log` 按上界），并顺带检查映射的健全性（读不越界、写落在结果内、唯一写恰好写一次）。新增或修改一个声明时，在 `index_eval.cpp` 的表里加上它的用例生成器；测试计划 `test/plans/feat/modules/index_semantics.plan.toml` 要求全部一致。

## 5. 注册流程

### 5.1 全局内置算子

1. 在 `src/builtin/operators/` 中实现 `operator_t` 函数。
2. 在 `src/builtin/executors/builtin.cpp` 的 `getOpsImplMap()` 中加入 suffix → `operator_t`。
3. 在 `src/builtin/modules/globals.cpp` 的算子组中加入重载，URI 为 `:` 加上同一 suffix。
4. 若算子是纯函数且不在 `registerBuiltinOperatorTraits()` 已覆盖的前缀之内，把它加入该函数的纯算子列表。

### 5.2 `.cmo` 模块

1. 在 `modules/<name>/` 下建立模块目录，`CMakeLists.txt` 中 `include` `modules/camel_module.cmake` 并调用 `add_camel_module(NAME <name>)`。默认源文件为 `executor.cpp`、`module.cpp`、`operators.cpp`，可用 `SOURCES` 指定；输出为 `<name>.cmo`。
2. 该 CMake 函数会自动编入 `modules/camel_module_abi.cpp`，由它导出 `camel_module_abi_version()`。加载器（`loadCmoModule`，`include/camel/core/module/dynamic.h`）先比对 `CAMEL_MODULE_ABI_VERSION`，不一致则拒绝加载。

`ArgsView::resultType()` 提供本次单态调用的静态结果布局，供 `ckpt::load_tree::<T>` 等仅凭输入类型无法确定返回布局的原生算子使用。它不是一个运行时类型值，也不属于 `with` 或普通实参。节点、字节码/JIT 和并行调度器，以及标准库的原生常量求值入口，在调用前设置该元数据；自行构造 `ArgsView` 的宿主也必须为此类算子设置它。该字段改变 C++ 对象布局，模块 ABI 已升至 3，旧模块须与核心一起重建。算子不能跨调用保存实参中的堆对象裸指针。
3. 实现并导出 `extern "C" Module *camel_module_create(Context *ctx)`，返回新建的模块对象，所有权交给运行时。
4. 在模块构造函数中用 `exportEntity()` 导出各 `OperatorGroup`（需要时用 `exportType()` 导出类型），URI 使用 `<name>:suffix` 格式。
5. 在 `load()` 中调用 `registerExecutorFactory("<name>", factory)`，factory 创建持有 suffix → `operator_t` 映射的 Executor；并在此登记纯算子的 `OperatorTraits`。
6. 确保 opsMap 的 key 与算子组中 URI 的 suffix 一一对应。

导入模块时，`Context` 在导入方是文件模块时先在它自己的目录里查找 `<name>.cmo` 或同名源文件（H112），再按模块搜索路径查找，找不到再查 libcamel 内部的模块工厂表。

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

设计原则 3.3：算子的纯度必须声明；没有声明的算子按有副作用对待（少优化，不会算错）。

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

## 8. 声明与标准库参考

- 原生实现的签名写成**没有函数体的 `export func`**（设计原则 6.4；H19 第 1 步）：`stdlib/<模块>.cml`
  与同目录的 `<模块>.cmo` 是同一个模块，这份 `.cml` 就是它的声明模块，参与编译，签名与参数名以它为准。
  `///` 注释写说明，`@pure` 或 `@effect <原因>`、`@fails <失败时的行为>`、`@example <代码>    // <结果>`；
  URI 默认是 `<模块>:<名字>`，算子名不是 Camel 标识符或是本模块算子的另一个前缀时，用
  `#op<'模块:算子'>` 标签写出。声明没有函数体：带函数体的 `func` 就是普通函数（声明模块里则报 `DeclarationModuleHasBody`）。
- `stdlib/signatures/<模块>.cml` 只作参考、不参与编译：它保留尚未声明化的 C++ 模块的签名，以及声明
  表达不了的计算签名（`json` 的 `encode`/`decode_as`）。条目同样是无函数体的声明（`*` 通配一组只在
  类型上不同的重载）；`--verify-ops` 拒绝其中带函数体的条目。
- 用 Camel 写的库（`stdlib/<模块>.cml`，同目录没有 `<模块>.cmo`）本身就是签名文件：读它的 `export func`
  与 `///` 注释；这些函数有函数体，声明与实现是同一份。
- C++ 模块导出的值不能写成声明（`export let` 需要初始值，写了就是第二份值）：声明模块里用
  `// value 名字: 类型` 的注释记录参考页条目，值本身仍由 C++ 导出。不参与编译的签名文件可以直接写
  `export let 名字: 类型 = 值`，带 `///` 说明与 `@example`，不写 `@pure` 与 URI（它不是算子，`--verify-ops` 不核对它）。
- `node scripts/stdlib-reference.mjs --write` 由它们生成 `docs/stdlib-reference.md`；`--verify-doc` 检查
  文档是最新的；`--verify-ops` 用 `camel inspect --ops` 的结果核对声明：每个已注册的算子都有声明、
  每条声明都对应一个已注册的算子、参数个数一致、`@pure`/`@effect` 与登记的纯度一致，并拒绝签名文件里
  带函数体的条目。三者都在默认测试集里（同一个测试计划）。新增或修改算子后：改声明，运行 `--write`，
  提交生成的文档。

## 9. 参考

- `include/camel/core/operator.h`：`operator_t`、`ArgsView`、`OperatorGroup`、`OperatorIndex`。
- `include/camel/core/operator_table.h`：`OperId`、`OperatorTraits`、`OperatorTable`（性质、类型解析器、类型折叠）；`include/camel/core/symbol.h`：符号表。
- `include/camel/core/operator_semantics.h`、`include/camel/core/index_semantics.h`、`include/camel/runtime/operator_instance.h`：算子语义的记录、下标语义与归约律、节点上的实例（4.2 节）。
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
