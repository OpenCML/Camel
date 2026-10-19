# 运行时、图模型、优化器、调度器、调试器：设计审查（2026-10-08 以来）

检出：`Camel-fsh-dev`，分支 fsh-dev，HEAD 6540169d。只读。以下每条都在代码里核对过；行号以 HEAD 为准。
判断标准是"长期该不该这样设计"，不是"能不能跑"。按严重程度排序。

---

## 1. 遍参数靠 `lookupPassFactory` 里一段写死的解析 —— 高

- **位置**：`src/execute/pass.cpp:632-669`，294fe990（2026-10-18，algebra 与 safe_fp）。
- **现在的做法**：
  ```cpp
  if (const auto open = name.find('('); open != std::string::npos) {
      ...
      if (path != "std::opt" && path != "std::opt::algebra") return nullptr;
      if (equal == npos || trim(args.substr(0, equal)) != "safe_fp") return nullptr;
      if (value != "true" && value != "false") return nullptr;
  ```
  帮助文字也写死：`"Optimization options: std::opt(safe_fp=true|false), std::opt::algebra(safe_fp=true|false)"`（pass.cpp:807）。
- **为什么不对**：和 `@numeric` 是同一类问题。2.15 说"安全浮点模式是**可选的遍参数**"，2.16 又要求"后端提供安全模式参数"。也就是说，遍参数是流水线的一项通用能力，现在却只给两个名字、一个键、一个布尔值。这段解析跑在别名解析和 `ModulePassRegistry` 之前，所以模块贡献的遍、调度器和 Tiles 都拿不到参数。其他配置散在环境变量里：`CAMEL_FVM_NATIVE_LOOP`（fastvm.cpp:486）、`CAMEL_JIT_HOT_THRESHOLD`（fastvm.h:280）、内联增长预算（inline/engine.cpp:1035）、taskflow 的阈值（handoff.cpp:45、taskflow.cpp:234）。测试遍是 16 段一模一样的 `if (resolved == kXTestPass && std::getenv(...))`（pass.cpp:704-776），而旁边已经有现成的注册表。
- **更好的做法**：注册遍的时候同时声明它的参数表（名字、类型、默认值、说明）。`name(k=v, ...)` 由流水线统一解析和校验，再交给工厂；帮助文字从参数表生成。模块遍、调度器、Tiles 用同一套。测试遍注册到同一个注册表里，挂一个"只在测试时可见"的标志。
- **要不要语言作者裁定**：要定一件事。问题："`std::opt(safe_fp=true)` 这种写法是不是流水线的通用语法？是的话，`std::tiles(safe=true)`、`std::fvm(native_loop=true)`、模块遍 `tensor::fuse(level=2)` 都应当合法；值的类型是只有布尔/整数/字符串，还是能写 Camel 的常量表达式？"

## 2. 核心编译器按 URI 字符串把 `tree` 模块的算子展开 —— 高（不在本区，顺带报告）

- **位置**：`src/compile/gir/tree.cpp:57-67`，23d97db7（2026-10-18）；`modules/tree/module.cpp` 的 `TreeResolver::resolve` 直接 `return std::nullopt; // The compiler resolves callbacks separately for each leaf.`
- **现在的做法**：
  ```cpp
  if (uri != "tree:tree_map" && uri != "tree:tree_reduce" && uri != "tree:tree_unzip" &&
      uri != "tree:tree_paths" && uri != "tree:tree_leaves")
      return nullptr;
  const bool map = uri == "tree:tree_map", reduce = uri == "tree:tree_reduce";
  ```
- **为什么不对**：和 `@numeric` 写死在 AST 构建器里完全同类：一个库模块的五个算子，由核心建图器按名字认出来，再用专门的 C++ 展开。这个模块自己只是个空壳。`operator_table.h` 开头写着"Generic graph passes ... must not know about any particular module"。2.16 要的是"树的形状由类型决定，操作在编译期展开为逐叶的普通节点"，要的是这个效果，没有说要核心认名字。往后别的库也会有"按类型在编译期展开"的需要，比如 `Device<T>` 的逐字段搬运、序列化，到时候只能接着往 tree.cpp 里加名字。
- **更好的做法**：给模块一个通用的"建图期展开"钩子，和类型解析器（resolver）挂在同一处：拿到实参类型，通过一个受限的建图 API 产出节点。或者用 Camel 自己的类型函数加 `macro` 来写。之后 `tree` 模块自己注册这个钩子，核心里不出现 `tree:`。
- **要不要语言作者裁定**：要。问题："类型驱动的编译期展开，是做成 C++ 模块的建图钩子（模块作者用 C++ 写），还是做成 Camel 层的机制（比如对类型递归的 `macro`、类型函数）？比如 `tree_map<f>(params, grads)`，应该由谁、在哪一层决定逐叶调用 `f`？"

## 3. `@numeric` 在图这一侧：专用的一字节"许可列"，8 位全用完，零值兼作"未指定" —— 高（已在重做，这里补全图侧的清单）

- **位置**：`include/camel/core/operator_semantics.h:115-131`（`enum Permission : uint8_t`，`kFlushSubnormals = 1 << 7`，`kAllPermissions = 0xFF`）；`include/camel/runtime/graph.h:40-41, 403-407`（`uint8_t nodePermissions`）；`include/camel/runtime/draft.h:97-102, 365-367`；`src/runtime/draft_construct.cpp:447-457`。来自 fc36ea1b、d05050f7。
- **现在的做法**：
  ```cpp
  // A rewritten node keeps its source origin and its numeric permissions unless the rewrite
  // gives it others.
  if (init.permissions == 0) { node(id)->header.permissions = permissions; }
  ```
  这里 0 有两个意思："没有许可"和"本次改写没说"。结果是改写没法把一个节点重建成"无许可"。
- **为什么不对**：图上只为这一种属性开了专门的列。知道这一列存在的文件有 20 处左右，包括 `draft.cpp`、`draft_clone.cpp`、`draft_construct.cpp`、`graph.cpp`（布局与编码）、`graph_build.h`、`draft_session.cpp:676-685`（判断是否没变）、`definitions.cpp:370-405`（展开时取并）、`core_facts.cpp:290-350`、`fact_persist.cpp:117`、`operator_instance.cpp:196`、`json/gir.cpp:132-134`、`graphviz.cpp`、`generic.cpp:856`（CSE 的键）、`algebra.cpp:151,163`，还有 Tiles 的 9 个文件。每加一种节点属性，这些地方都要再改一遍。传播规则（内联保留自己的、展开取并、CSE 不同就不合并，op-semantics 6.4）也分散写在各处的代码里。事实库其实已经有对应的概念：`facts.h:26-28` 的 (i) 类 `GraphContent`，即"图里推不出来、本身就是图内容的信息"。但它目前只能去读这条专用列。
- **更好的做法**：做成通用的节点属性。像事实种类一样登记属性种类：键、值的编码、是否进内容散列和缓存，以及它在内联、克隆、定义展开、CSE、新造节点时怎样传播（"保留自己的""取并""取交否则不合并""取触发节点的"）。图里按需给每种属性开一列。`core.permissions` 成为这种属性的一个普通实例。下面第 8 条的"内联来源"也走同一机制。
- **要不要语言作者裁定**：许可本身已有裁定。要确认的是：节点属性对外是图数据模型的一部分（2.11："图的数据模型要定义清楚、可以对外"）。用 Camel 写的遍能不能读写自定义属性？

## 4. 关于效应和写入，现在有两到三套各自独立的真相，优化遍一套都没用资源声明 —— 高

- **位置**：资源声明与查询 `include/camel/runtime/resources.h`（0ef28c61），文件头写明"Nothing here changes a graph or what any back end does."。`resourcesOf/resourceConflict/requiredOrderings/footprintOf/mayAlias` 的使用者只有 `src/passes/test/resources.cpp` 和 `src/runtime/core_facts.cpp`（grep 核对过）。
- **优化遍实际用的是另几样东西**：
  - `isVarArgument`、`isWrittenInPlace`（`draft_writes.cpp:164-202`）：根据重载签名里的 `var` 形参来推断。
  - algebra 自己写了一份 `WrittenSlots`（`algebra.cpp:64-105`）：用并查集把 GATE 和它转发的槽并成一类。注释直说是为了躲开 `isWrittenInPlace` "exponential walks through a shared DAG"。也就是绕开了公用函数的毛病，没有去修它（`draft_writes.cpp:173-174`："A GATE reached twice is only looked at twice"，没有 visited 集合）。两份的判定也不完全一样：`WrittenSlots` 跳过纯算子，`isWrittenInPlace` 不跳。
  - CSE 的 `readsVariable` 用 `oper::isVariableRead`，只认 `:var/get`、`:var/peek` 两个固定 id（generic.cpp:831-843）。
  - `EffectSummary` 的 `writesVariables` 用 `isAssignment`（loop.cpp:79），而 `isAssignment` 来自 URI 前缀：`if (uri.starts_with(":op/assn")) family |= kFamilyAssignment;`（operator_table.cpp:82-84）。
  - 纯度也是按名字前缀定的：`const bool pure = (key.starts_with("op/") && !key.starts_with("op/assn")) || key.starts_with("coll/") ...`（builtin.cpp:345-346）。
- **三处说法互相矛盾的实例**：`:var/take`
  - 签名是 `(var variable: T[]) => T[]`，表示会写（globals.cpp:303-305）。
  - 资源声明是 `cell(AccessMode::ReadModifyWrite)`（builtin_semantics.cpp:219-221）。
  - traits 是 `.stateOnly = true`（builtin.cpp:373），而 `stateOnly` 的文档写的是"it creates or reads a variable"（operator_table.h:145-149）。
  - 它不是 `:op/assn*`，所以 `EffectSummary.writesVariables` 不把它算作写入。
  - `checkResources` 只拿资源声明和 traits 对照，不和签名里的 `var` 对照（resource_semantics.cpp 里查不到 var 形参）。
- **为什么不对**：op-semantics 第 8 节把资源定为"关于读写什么的规范声明"。2.12 要求"有身份的东西要列明"，2.6 说"副作用的声明是一个闭环"。现在闭环有了，却被晾在一边，优化遍各自按签名、名字前缀和 GATE 的槽别名去猜。这样有两个后果：一是声明错了也没人发现；二是 Tiles 和后来的遍会再写第四套。另外"GATE 与它转发的值共用一个帧槽"是存储层的约定（draft_inline.cpp 里新建 GATE 时取 `valueHeader->dataIndex`），现在也渗进了优化遍的正确性论证。
- **更好的做法**：只留一个来源。由资源声明加签名的一致性检查（登记时：声明写某个操作数，当且仅当对应形参是 `var`）推出 `isWrittenInPlace`、`EffectSummary`、"读变量"、"是不是赋值"这些查询，做成按图缓存的事实（`core.resources` 已经有了）。traits 里的 `stateOnly` 和 URI 前缀家族改成从声明派生，或者干脆去掉。纯度应当由模块逐个声明，不靠 `op/` 前缀推断。
- **要不要语言作者裁定**：不用，这是实现上的统一。不过要确认一点：签名里的 `var` 和资源声明以哪个为准？比如一个 C++ 算子形参标了 `var`，资源却声明"只读"，这是错误，还是细化（就像赋值族在 traits 里算外部效应、资源里只写单元那样）？

## 5. `std::opt::algebra` 写死 12 个算子 URI，不读算子语义；2.15 和 2.16 的"默认策略"在语义层没有表示 —— 中

- **位置**：`src/passes/opt/generic/algebra.cpp:48-62`，294fe990、c8c657e2。
  ```cpp
  row(type::TypeCode::Int32, ":op/add_i", ":op/sub_i", ":op/mul_i"),
  row(type::TypeCode::Int64, ":op/add_l", ":op/sub_l", ":op/mul_l"),
  row(type::TypeCode::Float32, ":op/add_f", ":op/sub_f", ":op/mul_f"),
  row(type::TypeCode::Float64, ":op/add_d", ":op/sub_d", ":op/mul_d")};
  ```
  策略写成 C++ 条件：`const uint8_t required = sameSign ? 0 : sem::kNoInfs | sem::kNoSignedZeros;`（:265）、`if (safeFp || !permitted(draft, node, sem::kNoInfs))`（:277）。
- **为什么不对**：2.15 说 algebra 是"独立的**通用**算式优化遍"，但它只认内建标量的加减乘，张量的逐元素算子和模块里的算子都进不来。可结合、可交换、单位元、是否精确这些代数事实，op-semantics 第 7 节已经为归约建了 `ReductionLaw`，二元算子却没有对应的声明，所以 algebra 只能自己硬编码。同样，`sem::reorderFreedom(law, granted)`（index_semantics.cpp:945-956）只看节点许可。2.15 的"算式遍默认放宽"和 2.16 的"后端默认可以改变累加顺序、safe 模式关掉"都是**策略**，但语义层没有策略这个输入，所以 algebra 写一份，Tiles 再写一份（`cuda.cpp:116`、`schedule.cpp:412,524` 各有自己的 `relaxed` 掩码）。
- **更好的做法**：两件事。（a）在 `OperatorSemantics` 里加"二元代数律"的声明（与 `ReductionLaw` 并列）：结合律和交换律是否精确成立、单位元的位模式、和哪个乘法构成分配。algebra 按声明匹配，内建标量和张量逐元素算子一视同仁。（b）在语义层引入"数值策略"（默认放宽了哪些、是不是 safe），统一成 `freedom(law, nodePermissions, policy)` 一个函数，algebra、`core.reorder`、Tiles 都调它。
- **要不要语言作者裁定**：要定适用范围。问题："2.15 的默认放宽适用于张量逐元素的 `a*2 + a*3 → a*5` 吗？还是只限标量？"

## 6. 标记算子（`:mark/`）是封闭清单，每个后端各写一遍；有了原生子图调用服务以后也没收拢 —— 中

- **位置**：
  - `operator_table.h:102-121`：11 个写死的 id（`MapArr`…`Isolate`）。
  - `operator_table.cpp:79-81`：按 URI 前缀 `":mark/"` 定家族。
  - 实现：`nodevm.cpp:851-1110`（`evalMarkedOperator_*`）、`fastvm.cpp:563-790`、`taskflow.cpp:1663-1690` 和 `2184-2203`（同一个 switch 在同一文件里出现两次）、`trans/cpp/function_emitter.cpp:1613`。
  - 原生子图调用（76165753、4a5eac43、e6386a04）。
- **现在的做法**：设计稿 `docs/design/native-subgraph-call/README.md` 自己把"The schedulers implement built-in higher-order operators separately"列为问题证据，可是服务做好以后 map、filter、reduce、foreach、isolate 仍走各后端的特例。结果两个标志并存，说的是同一件事：
  - `handoff.cpp:157`：`if (table.isMarked(body->oper) || (traits && traits->callsGraphs))`。
  - 两者已经不一致：para 模块把 `:mark/unordered_foreach_arr`、`:mark/unordered_reduce_arr` 只声明成 `{.pure = false}`（`modules/para/module.cpp:128-130`，经 `declare()` 落成 `set(..., {.pure = pure})`），所以 `callsGraphs(UnorderedReduceArr) == false`，`EffectSummary` 也不给它们置 `mayNotTerminate`（loop.cpp:80-84）。而 builtin.cpp:356-361 里另外五个 `:mark/` 算子声明了 `callsGraphs = true`。
  - 另外，核心建图器按 URI 认 foreach：`uri == ":mark/foreach_arr" || uri == ":mark/unordered_foreach_arr"`（compile/gir/builder.cpp:5833）。
  - 宏求值器调原生算子时不建 `FunctionCallScope`（macro/eval.cpp:1009：`slot_t result = (*op)(withView, normView, *context_);`）。设计稿第 7 条要求"must never inherit an unrelated runtime scope"：嵌在别的作用域里时，它不会屏蔽外层作用域。
- **为什么不对**：高阶算子是"一个算子节点，元素函数作为函数值传入"（第 1 节对照表），本不该是一种节点种类。现在却是封闭的家族，每个后端各实现一遍，要加 `scan`、`zip_with` 之类就得改 4 到 5 处。
- **更好的做法**：顺序语义的 map、filter、reduce、foreach 写成普通原生算子，通过 `invokeFunction` 调元素函数，只实现一份。"能不能并行"由算子语义声明（比如 map 的下标语义：元素之间互相独立；`unordered_*` 的 `Unordered` 律），taskflow 根据声明选择并行策略，不再靠 `:mark/` 前缀。`isolate`（`attempt`）要不要保留成调度器特例，单独讨论。`isMarked` 并进 `callsGraphs`。宏求值器给原生算子装一个"不支持"的屏蔽作用域，或者装它自己的调用器。
- **要不要语言作者裁定**：要确认一件事。问题："`map` 的并行是调度器从'元素互不依赖'这条声明推出来的，还是 `map` 这个名字本身带来的？用户自己写的 `func my_map<T>(xs, f)` 如果声明了同样的下标语义，并行调度器能不能也拿来并行？"

## 7. LOOP 的轮次驱动写了三遍半，降级与否靠环境变量和调试器在不在 —— 中

- **位置**：`nodevm.cpp:999-1068`、`fastvm.cpp:639-708`、`macro/eval.cpp:716-790`，再加上 `loop_lowering.cpp`（降成尾递归）。来自 6ab0e673、eb93e96a。
- **现在的做法**：前三处各有一份几乎一样的 `bind` lambda（"(元素,) 状态..., 捕获"），各写一遍"先条件后取元素"（三处都有注释 "Each round: the condition first, then the next element"）。FastVM 默认先降级，原因是"today that is faster here than the native instruction"：
  ```cpp
  const char *nativeLoops = std::getenv("CAMEL_FVM_NATIVE_LOOP");
  if (camel::execute::executionObserver() == nullptr && !(nativeLoops ... == "1"))
      graph = camel::runtime::lowerLoopsToTailRecursion(context_, graph);
  ```
  这样调试器下跑的是一条路径，平时跑的是另一条。宏里的轮数上限是常量：`constexpr size_t kMaxMacroLoopRounds = 1'000'000;`（eval.cpp:74），注释还写着"the bound is a question to the language author"。可是 2.14 已经裁定"`std::macro` 中循环至多 100 万轮（**与递归上限一样可调**）"。
- **为什么不对**：2.14 的轮次规则以后还要扩展（"`for` 目前只遍历数组，字符串、映射、张量以后再加"），每扩一次都要同时改三个解释器和降级。轮次协议本该是 `LoopView` 的一部分。
- **更好的做法**：在 `loop.h` 里提供一个和后端无关的驱动，`runLoop(view, callGraph, readSlot, ...)`，把"先条件后元素"、端口布局、迭代对象的取元素都收进去，各后端只提供"调用一张图"和"读写槽"。`kMaxMacroLoopRounds` 改成和 `--max-macro-depth` 一样的选项加环境变量，并删掉那条过期注释。
- **要不要语言作者裁定**：不用，2.14 已有裁定。

## 8. 内联来源没有记在图上，调试器按源码位置去猜，和 2.14 不符 —— 中

- **位置**：`tools/debugger/src/core/frames.cpp:204-209`，`debug_core.cpp:643-645`，`docs/technical/35_debugger.md:244,253-254`。
- **现在的做法**：`function = index_.functionOf(f.loc); if (function != f.info->function ...) name += " [inlined code of " + from + "]";`。文档写的是 "there is no 'inlined at' chain"，"The stack shows one frame ... `DESIGN-QUESTION(inlined-frames)`"。
- **为什么不对**：2.14 已经裁定"被内联的函数仍显示为它自己的帧（用户写的是两个函数，就看到两个帧）"。按源码位置推断所在函数有几种情况做不到：同一函数在一个帧里被内联两次、嵌套内联、递归被展开。根本原因在图模型：位置列只记"源码在哪"，不记"从哪次调用内联进来"。
- **更好的做法**：给节点加一项"内联于"属性，走第 3 条的通用节点属性机制，或者像 LLVM 的 `inlinedAt` 那样挂在位置表里。由内联器和定义展开写入，调试器据此还原虚拟帧。同时删掉代码里已经被 2.14 裁定掉的 DESIGN-QUESTION。
- **要不要语言作者裁定**：不用。

## 9. `:graph/original` 与 `originalGraph_`：引用表之外的第二种"图引用图" —— 中低

- **位置**：`include/camel/runtime/graph.h:421-428`；`src/runtime/graph.cpp:1734-1746`；`src/runtime/loop_lowering.cpp:404-416, 427-466`；540ea90c。
- **现在的做法**：后端拿到的降级副本里，每张图都有一个裸指针 `originalGraph_` 指回原图。GC 单独 trace 它；`replaceRoot` 又为它单独留一段"保活"逻辑。降级时，凡是 `inspectsGraphs` 算子的操作数，只要类型是 `TypeCode::Function`，就在前面插一个 `:graph/original` 节点。
- **为什么不对**：graph.h 第 7 点写的是"A graph refers to other graphs only through its reference table"，2.10 写的是"图与图之间只有图引用图（调用）这一种关系"，这个指针两条都没遵守。映射也只覆盖"直接以函数类型出现的操作数"：函数如果包在元组或结构体里传给读图的算子，就不会被换回原图（`throughOriginals` 只看 `type->code() == TypeCode::Function`）。往根上说，这是 FastVM 和 taskflow 还没有原生跑 LOOP 时的过渡办法，现在却被做进了图的数据结构。
- **更好的做法**：一种是各后端都原生跑 LOOP（2.10："之后逐个换成原生处理"，第 7 条的共享驱动是前提），降级副本连同这个指针一起去掉。另一种是等图模型第 5 步有了稳定的 GraphId，读图的算子按 GraphId 去查程序的快照，不用在运行时换函数值。
- **要不要语言作者裁定**：不用。

## 10. 控制边约简藏在编码器和建图器里，"给内联器让路"的例外写进了通用函数 —— 低

- **位置**：`src/compile/gir/graph_encode.cpp:618-620`（`reduceControlDependencies(*prepared.draft, 8192)`）；`builder.cpp:421-423`（建同步边前先做一次 64 步的路径搜索）；`runtime/control.cpp:107-114`。来自 6037d820、70f62b4a。
- **现在的做法**：前端产出的图在没有任何遍的情况下已经做过一次有预算的传递约简，所以"源码写下的图"长什么样取决于 8192、64 这些预算。`appendControlDependency` 对 FUNC/CALL 不做值覆盖判断，理由写在注释里："the inliner intentionally adds explicit argument ordering just before it removes the call"。这是一个只对某一个调用者成立的时序约定，却写进了公用函数。各处的预算常量（32、64、128、256、8192、250000、`slots*4`、`slots*8`、`cost > ... + 8`）互相没有关联，也不能配置。
- **为什么不对**：第 4 节说"不加任何遍时"程序就是前端建的图，`.cmo` 存的也是前端的图（graph-model 4.6），这种图的形状应当由规则决定，不该受搜索预算影响。语义上目前没看到错，约简只删"能证明冗余"的边。
- **更好的做法**：建图器只做精确、局部的去重（同一前驱已经是直接值输入时不再加控制边）。传递约简只留在 `std::opt::ctrl`。内联器在删除调用之前，自己把实参的顺序移交给函数体的节点，通用函数里不再为内联开例外。
- **要不要语言作者裁定**：不用。

## 11. 杂项（低）

- **OperatorTraits 的位快满了**：一个字节已用 7 位（`kInspectsGraphs = 1 << 6`，operator_table.h:364-372），第 4 条里的"读写变量""分配资源"一类以后还要加。这和许可字节是同一种隐患，建议跟第 4 条一起，把 traits 收成从语义记录派生的视图。
- **`EffectSummary` 放在 `loop.h` 里**（loop.h:64-90），可它是图级概念，事实、定义检查都在用。应当移到图模型或效应相关的头文件。
- **文档与代码对不上**：graph-model 4.1 的节点记录写的是"负载偏移以 4 字节为单位、0 表示无"，代码是 8 字节一字、`kNoPayload = 0xFFFF`。偏差已在 3b 的实施记录里写明，只是 4.1 的结构体示意没有同步。

---

## 查过、认为合理的地方

- 16 位节点、边、负载寻址与 65,535 的单图容量：graph-model 的 R4 已经裁定"不放宽"，超限时报 `GraphTooLarge`（capacity.h），不是缺陷。
- 事实库（facts.h）：印记、三类信息的划分、`Cone` 依赖、陈旧时读成"没有"的规则，设计清楚。
- `LoopView` 与提交时的 I18、I19 校验；`for … while` 在三个解释器和降级里都是"先条件后元素"，与 2.14 一致。只是写了几遍，见第 7 条。
- 原生子图调用服务：线程局部的作用域栈、空调用器用来屏蔽外层、各后端复用自己的递归计数，设计合理。宏求值器那一路的缺口见第 6 条。
- `control.cpp` 的约简：预算耗尽时保留边，不穿过 BRCH/JOIN，GATE 至少留一条 CTRL，只删能证明冗余的边。语义上保守，没发现会丢效应。
- `pruneBranch`、`replaceNode`：去掉分支或换成常量时，用 `orderedSources` 把条件和臂上的效应顺序转接出去（generic.cpp:400-407），与 2.1、2.12 一致。
- CSE 把许可放进键、许可不同就不合并：符合 op-semantics 6.4 允许的"不愿取交就不合并"。
- 常量折叠不折会故障的常量表达式、给出警告：属于遍自己的取舍，2.10 允许。
- algebra 的浮点规则：同号系数合并、用槽位位模式区分 ±0、`exactDoubling` 的论证，与 2.15 "保留特殊值行为" 一致，没发现改变 NaN、无穷或零号的情形。问题只在扩展性（第 5 条）。
- 定义图（definitions.h）：按上下文惰性编译、检查签名、效应和递归环，展开时许可取并，与 op-semantics 6.4、9.3 一致。
- 引用表（6e9175a7）：FUNC 和 LOOP 的负载按引用表下标指名被调图，满足 2.10。例外是第 9 条。
- 调试器的流水线停点（pipeline.cpp）：在程序线程上抓取快照，不持有 GC 指针，不会再编译一次，做法干净。
