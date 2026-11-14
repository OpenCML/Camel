# 分层（路线四）：`Context` 与模块加载上移到 `execute` —— 一页方案

依据：第十一节第 8 路的那一行（`Context` 与模块加载上移到 `execute`；`graph_id.h`、`entry_args.h` 下移；`modules/math` 头文件移进核心；模块越层引用改走公开接口；定内核接口目录），以及目标架构第 3 节的层表与"内核接口"一段（`docs/design/architecture/`，在 `origin/fsh-dev` 上、尚未合入 `develop`，本分支看不到）。本页只写方案，不动代码；本轮实际改的只有 `modules/math` 的两个共享头（第 7 节）。

**结论**：`Context`（连同 `frame.h`、`module_resolver.h`）整页从 `core` 搬到 `execute`，`userdef` 跟着上去——这两处是 `core` 向上伸手的全部理由；`graph_id.h` 下到 `core`、`entry_args.h` 下到 `runtime`，把剩下两条向上引用也翻成向下；模块此后只经 `include/camel/kernel/` 的一份门看内核。`pass.cpp` 的拆分、登记表、测试遍移出 `libcamel` 是 R1 合入之后的事，不在本页。

## 1. 层与方向

`utils` < `core` < `graph`（现在的 `src/runtime`）< `frontend`（`parse`、`compile`）< `execute`（`Context`、模块加载、遍的执行）< `backends` < `passes`；`modules`、`tools` 在最上，只经内核接口。只许上层引用下层，而 `core` 今天向上伸手八条（6.1 的 #1–#8），另有五处值层与 profiler 的落点留给后面（6.2）。

## 2. 今天：两处各在哪、被谁依赖

### 2.1 `Context`

| 项 | 位置 | 读作 |
|---|---|---|
| 头 3 个 | `include/camel/core/context/{context.h,frame.h,module_resolver.h}` | `context.h` 132 个包含点、`frame.h` 27、`module_resolver.h` 1（只有 `context.h` 包它） |
| 源 2 个 | `src/core/context/{context.cpp,frame.cpp}` | `context.cpp` 里 7 个 include：6 个指向 `camel/runtime/`、1 个指向 `camel/execute/executor.h` |
| 包含点的层别 | `modules/` 68、`src/` 47（`builtin` 18、`passes` 12、`execute` 6、`core` 4、`runtime` 3、`compile` 3、`service` 1）、`include/camel/` 8、`tools/` 8、`bindings/python/` 1 | 合计 132 |
| 它自己的向上引用 | `camel/runtime/graph_id.h`（头里）、`runtime::{GCGraph,GCGraphManager,Program,DefinitionStore,fact::FactStore}`（头里前置声明、`context.cpp` 里 6 个 include）、`compile::SemanticRecorder`、`Executor`/`ExecutorManager`（头里前置声明，`context.cpp` 包 `camel/execute/executor.h`） | `core` 的头声明着 `graph`、`frontend`、`execute` 的类型，`core` 的源包着 `execute` 的头 |

一个 `core` 里的类握着 `runtime::Program`、`runtime::GCGraph`、`compile::SemanticRecorder` 与 `ExecutorManager`——它本来就是 `execute` 的东西，只是地址在 `core`。132 个包含点等于说：任何人想用 `Context`，就得把 `graph`、`frontend`、`execute` 一起拖进翻译单元。

### 2.2 `userdef`（模块加载）

| 项 | 位置 | 读作 |
|---|---|---|
| 头 1 个 | `include/camel/core/module/userdef.h` | 16 个包含点：`tools/camel-cli` 4、`src/core` 3、`src/compile` 2、`tools/debugger` 2、其余 5 处 |
| 源 2 个 | `src/core/module/userdef.cpp`、`src/core/module/userdef_internal.h` | 头包 `camel/compile/gct.h`、`camel/parse/parse.h`，源再包 `camel/compile/gir/encode.h` |
| 它调到的东西 | `GCT::Builder`、`gir::Builder::of(context_)`、`context_->importModule`、`checkPreludeDeclarations`、`graphBuildSession` | 一个 `core` 里的类把编译器整个装在里面 |

`userdef` 是"把 `.cml` 编译成模块"的那一层，它依赖 `frontend` 不是绕路而是本质；放在 `core` 只是因为它要那个在 `core` 里的 `Context`。**两处是一件事**：`Context` 上去，`userdef` 没有留在下面的理由。

## 3. 上移之后：谁依赖谁、哪些 `#include` 变

`execute` 依赖 `frontend`、`graph`、`core`；`core` 不再知道 `frontend` 与 `execute`，而对 `graph` 的引用只剩 6.2 列的那几处值层与求导的落点（`graph_id.h` 与 `entry_args.h` 下移之后）。路径一律保持现在的形状，只换前缀，因此每个包含点是一行机械改动：

| 今天 | 之后 | 实际改动 |
|---|---|---|
| `camel/core/context/context.h` | `camel/execute/context/context.h` | 132 个文件 |
| `camel/core/context/frame.h` | `camel/execute/context/frame.h` | 27 个文件 |
| `camel/core/context/module_resolver.h` | `camel/execute/context/module_resolver.h` | 1 个文件 |
| `camel/core/module/userdef.h` | `camel/execute/module/userdef.h` | 16 个文件 |
| `camel/runtime/graph_id.h` | `camel/core/graph_id.h` | 10 个文件 |
| `camel/execute/entry_args.h` | `camel/runtime/entry_args.h` | 21 个文件 |
| `modules/math/{minmax,mean}.h` | `camel/core/math/{minmax,mean}.h` | **本轮已做**，7 个 include 行 + 17 处注释 |

源文件同理：`src/core/context/*` → `src/execute/context/*`，`src/core/module/userdef*` → `src/execute/module/*`。命名空间跟不跟着改是 Q1。

**唯一的门**：`include/camel/kernel/`，本路先立 `camel/kernel/module.h` 一份（第 5 节）。它替掉模块今天直接包含的四类头：`camel/core/context/context.h`（modules 68 个文件）、`camel/execute/executor.h`（45）、`camel/core/module/builtin.h`（27）、`camel/core/operator_table.h`（21）。之后模块只剩两种包含：这一份门，和值层（`camel/core/{type.h,operator.h,rtdata/**,data/**}`、`camel/core/module/module.h`）——值层算不算门的一部分是 Q8。

## 4. `graph_id.h`、`entry_args.h` 为什么下移

- **`graph_id.h`**：`GraphId` 是 `enum class` 加一个计数器，零依赖。今天 5 个 `core` 头（`context.h`、`node_attrs.h`、`data/composite/func.h`、`rtdata/func.h`、`source/manager.h`）为了这一个 id，把 `graph` 层的头目录带进了 `core`。下移之后 `include/camel/core/` 里这 5 条边同时消失，另外 5 个包含者（`runtime/` 的 4 个与 `modules/graph/handle.h`）只换一行路径。受影响 10 个文件。
- **`entry_args.h`**：它今天在 `execute`，而 `frontend` 的建图器（`src/compile/gir/builder.cpp` 与 10 个 `builder_*.cpp`，共 11 个文件）为了 `entryFunctionOrigin`、`unboundEntryParameters`、`boundEntryArguments` 三个查询向上包含它——这是 `frontend` → `execute` 的边，也是 `compile` 里唯一的向上引用。入口参数是"建入口图时把值绑成常量"，属于 `graph` 层，不是驱动的事；下到 `include/camel/runtime/` 之后这条边变成向下。`execute` 那半（`bindEntryArguments`、`writeEntryResultTo`、loader/saver 登记、`reportPassErrorValue`）留在同一页：下到 `graph` 之后，两个调用方——`frontend` 的建图器与 `execute` 的驱动——都在它上面，都是向下引用。受影响 21 个文件。

## 5. 内核接口目录

`include/camel/kernel/`（名字取自目标架构第 3 节；现有 `include/camel/` 下没有同名目录，也不与 `camel/core/`、`camel/execute/` 的"目录同名于命名空间"约定冲突——接口目录按用途取名，正是目标架构的写法）。第一版只有一个文件：

- `camel/kernel/module.h`：`Context` 的不透明句柄（前置声明 + `context_ptr_t`）、`Module`/`BuiltinModule`、算子的 traits 与数值语义登记、执行器工厂登记、入口参数 loader/saver 登记。

图那五项（存储与提交、引擎、通用图算子、查询与编辑、执行后端的登记）由第 4 路与 R3 给出接口后再立，本路只定目录与第一份门（Q5）。模块目录里从此不该出现 `#include "camel/runtime/**"` 或 `"camel/compile/**"`——这正是守住它的那条引用检查（路线四的完成标志）要读的表。

## 6. 越层依赖逐条

### 6.1 这次会消掉、或变成合法方向的

| # | 边 | 今天怎么绕 | 上移之后 |
|---|---|---|---|
| 1 | `include/camel/core/context/context.h` → `camel/runtime/graph_id.h` | 直接包含，为一个 id 把 `graph` 的目录带进 `core` | `graph_id.h` 下移，边消失 |
| 2 | `include/camel/core/context/frame.h` → `camel/runtime/graph.h` | 直接包含 | 随 `Context` 上移，变成 `execute` → `graph`（向下） |
| 3 | `core/context/context.h` → `runtime::{GCGraph,GCGraphManager,Program,DefinitionStore}`、`compile::SemanticRecorder`、`Executor`、`ExecutorManager` | 头里全部前置声明，`context.cpp` 里再逐个包含 | 整页上移，全部同层或向下 |
| 4 | `src/core/context/context.cpp` → `camel/execute/executor.h` | `core` 的源包含 `execute` 的头 | 同层 |
| 5 | `include/camel/core/module/module.h` → `camel/core/context/context.h` | 只为 `context_ptr_t` 与 `handle()`/`mainModule()` 两处调用，整页包含，于是**每个模块**都拿到整个 `Context` | 前置声明 + `core` 侧两查询的小接口（Q2） |
| 6 | `include/camel/core/module/dynamic.h` → `camel/core/context/context.h` | 同上；`camel_module_create` 只要一个指针 | 前置声明即可 |
| 7 | `include/camel/core/module/userdef.h` → `camel/compile/gct.h`、`camel/parse/parse.h` | 直接包含（`core` 里装着编译器） | 上移 `execute`，边变向下 |
| 8 | `src/core/module/userdef.cpp` → `camel/compile/gir/encode.h`、`camel/parse/parse.h` | 源里包含 | 同上 |
| 9 | `src/compile/gir/builder*.cpp`（11 个文件）→ `camel/execute/entry_args.h` | `frontend` 向上拿三个建图期查询 | `entry_args.h` 下移，边变向下 |
| 10 | `include/camel/execute/entry_args.h` → `camel/core/context/context.h` | `execute` → `core`，合法；下移后它会变成 `graph` → `execute`，**必须**改成前置声明 | 见 Q4 |
| 11 | `modules/tensor/kernels/minmax.h` → `modules/math/minmax.h` | 模块 → 模块，今天唯一的一条模块间头引用 | 已改成 `camel/core/math/minmax.h`（模块 → `core`，向下） |

### 6.2 这次不动、要单列（别当成已经解决）

| # | 边 | 为什么还在 | 谁能去掉 |
|---|---|---|---|
| 1 | `include/camel/core/derivative.h` → `camel/runtime/graph.h` | `VjpBuilder` 在 `core` 里，按图节点写梯度 | 路线二/三（求导、优化各只剩一套）之后 |
| 2 | `src/core/data/composite/func.cpp`、`core/rtdata/{conv,func}.cpp`、`core/error/runtime.cpp`、`core/tags.cpp` → `runtime/{graph,program,draft}.h`（5 个文件、8 个 include） | 值层里装着函数的打印、常量、标签的落点 | `graph` 层整体下移之前不动 |
| 3 | `src/core/mm/{block,gen,growable}.cpp`、`core/rtdata/{array,variant}.cpp` → `camel/profile/*`（5 个文件、5 个 include） | 内存分配要报给 profiler | profiler 改成 `core` 侧的回调或计数接口；不在本路 |
| 4 | `include/camel/core/forward.h` → `camel/parse/ast/rule_head.h` | 前向规则索引要 `RuleMatch`；文件在本路独占名单里，但内容属于路线三 | Q6 |
| 5 | `modules/` 的 45 个文件 → `execute/executor.h`、25 → `runtime/graph.h`、21 → `core/operator_table.h` | 模块今天直接拿内核 | `camel/kernel/` 立起来之后逐模块改（Q5） |

## 7. `modules/math` 的头文件（本轮已落地）

8 个头：**2 个搬走，6 个留在原地**。

移走：`include/camel/core/math/minmax.h`、`include/camel/core/math/mean.h`。理由：两个都在 `namespace camel::core::math` 里，而且本来就是跨两层共用的（`core` 的 `array` 内核与 `tensor` 模块的 kernel 都调）；`minmax.h` 只依赖 `camel/core/rtdata/half.h`，`mean.h` 只依赖 `<cstdint>`，没有一处模块私有实现。目录按"目录同名于命名空间"的既有约定选 `include/camel/core/math/`（`camel/core/rtdata/`、`camel/core/module/`、`camel/standalone/` 都是这么排的）；不用 `include/camel/math/`，那个路径要求命名空间也叫 `camel::math`，那就不是纯搬移了。

留在原地，逐条：

| 头 | 为什么不宜进核心 |
|---|---|
| `module.h`（`MathModule`，`BuiltinModule` 的子类） | 模块自己的入口类型，只有 `camel_module_create` 与 `module.cpp` 用；搬进 `core` 等于让 `core` 认识每一个模块 |
| `executor.h`（`createMathExecutor`） | 模块自己的执行器工厂，`module.cpp` 与 `executor.cpp` 用 |
| `operators.h`（`getMathOpsMap` 与算子的声明） | 模块的算子表，注册用；只有 math 自己的源用 |
| `numeric.h` | 模块的数值算子表（一元/二元 kernel 的名字），同上 |
| `semantics.h`（`declareMathOperatorSemantics`） | 模块声明**自己**算子的数值语义（设计原则 7.1），属于"模块定义自己"，不是内核接口 |
| `half.h`（六个 `__math_*_bf__`/`_f6__` 的声明） | 这六个 kernel 在 `operators.cpp` 里定义，是模块私有的实现声明 |

引用点：改前 7 个 `#include` + 17 处注释里的旧路径，改后同数、全指向新路径。`minmax.h` 的 5 个 include 在 `modules/math/{operators,numeric}.cpp`、`modules/tensor/kernels/minmax.h`、`src/builtin/operators/{array_axis,array_reduce}.cpp`；`mean.h` 的 2 个在 `modules/tensor/kernels/reduce.cpp` 与 `src/builtin/operators/array_axis.cpp`。17 处注释分布在 `include/camel/standalone/{array,half,num}.h`（4）、`modules/tensor/kernels/{minmax.h,reduce.cpp}`（3）、`modules/tensor/ops/reduction_ops.cpp`（1）、`src/builtin/operators/{array_axis,array_reduce}.cpp`（3）、`src/passes/trans/cpp/operator_rules.cpp`（3）、`stdlib/array.cml`（1）、`test/cases/modules/stdlib/{array_reduction_kernels,reduced_minmax}.cml`（2）——旧路径留着 `docs.links.citations` 会红，所以一并改。数值、签名、符号名一个没动。

## 8. 需要作者裁的

**Q1 命名空间跟不跟着路径走。** 默认跟：`camel::core::context` → `camel::execute::context`，`camel::core::module::UserDefinedModule` → `camel::execute::module::UserDefinedModule`，作为同路的第二笔机械提交。132 个包含点里多数已经写成 `namespace ctx = camel::core::context` 这样一个别名，改一行就全编。不跟的代价是：一个 `camel/execute/` 里的头声明 `camel::core::context`，下一个人会以为它还在 `core`。

**Q2 `Module` 与 `Context` 之间的边怎么断。** 默认：`core` 侧加一个只含 `handle()`、`mainModule()` 两个查询的小接口（建议名 `ModuleOwner`），`execute::Context` 实现它，`Module` 只存 `std::shared_ptr<ModuleOwner>`。备选：`src/core/module/module.cpp` 一起上移进 `execute`，`module.h` 只留前置声明——代价是 `Module` 的声明与实现分居两层。

**Q3 `graph_id.h` 下移后命名空间。** 默认路径-only，留 `camel::runtime`：`graph` 这一层在代码里还叫 `runtime`（`src/runtime` 也还没改名），只搬一个文件改命名空间会把 10 个包含点里的 `runtime::GraphId` 一起动，而它换不来任何东西。改 `camel::core::GraphId` 作为独立一笔也可以，等 `src/runtime` 整体改名前一次做完更省。

**Q4 `entry_args.h` 整页下移还是拆两页。** 默认整页下到 `include/camel/runtime/`，并把 `Context` 参数改成前置声明（`context_ptr_t` 在 `core` 侧留一份别名，或在 `camel/core/` 给 `Context` 一个前置声明头）。备选：拆——建图期那三个查询下移，命令行那半（`bindEntryArguments`、`writeEntryResultTo`、loader/saver 登记）留在 `execute`。拆开的代价是 loader/saver 的登记表要与绑定分居两地，而登记表正是"模块定义的类型怎么从命令行读"的唯一一处。

**Q5 内核接口这一轮立多少。** 默认只立目录与 `camel/kernel/module.h` 一份门，图的五项等 R1 合入后由第 4 路与 R3 给接口。备选：现在就把五项立成空壳——会在第 4 路落地时改两次。

**Q6 `include/camel/core/forward.h` 动不动。** 默认不动。它在本路独占名单里，但内容是前向与转置规则的索引（`RuleKey`、`forwardRuleOf`），与分层无关；路线三正在改规则的名字特例，现在搬会撞。

**Q7 `modules/math` 那 6 个私有头。** 默认留原地（理由见第 7 节）。备选是把 `half.h`、`operators.h`、`numeric.h`、`semantics.h` 也搬进 `core`——那会把"math 有哪些算子、每个怎么算"写进公开头，而 8 个头里只剩 2 个在模块目录，目录就名不副实了。

**Q8 值层算不算内核接口的一部分。** 默认算：模块可以直接包含 `camel/core/{type.h,operator.h,rtdata/**,data/**}` 与 `camel/core/module/module.h`，只有图、引擎、登记表与 `Context` 走 `camel/kernel/`。备选是全经门转出——转发开销为零，但每个模块要多一行，而值层本来就是语言的值，不是内核的实现。