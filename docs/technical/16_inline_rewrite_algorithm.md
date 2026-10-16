# `std::opt::inline` 重写算法与 GIR API 协作详解

本文针对当前实现（`src/passes/opt/inline/inline.cpp`、`src/passes/opt/inline/engine.cpp` + `src/runtime/draft_inline.cpp`）进行工程化拆解，重点说明：

- `InlineRewritePass` 的真实执行顺序；
- 它依赖的运行时图重写 API 如何分层协作；
- 正确性不变量（invariant）如何被维持；
- 预算保护、提交时的检查与已知语义边界。

---

## 1. 总体设计目标

当前 `std::opt::inline` 在 **Pass 层**是一个**跨图事务式重写**过程，目标有三层：

1. **跨图引用一致性**：会话内所有图引用都以运行时图（`GCGraph`）的身份表示，提交时统一改写到新编码的图上。
2. **分支语义不破坏**：被内联的函数体留在调用原本所在的分支臂内，`BRCH` 的臂首/臂尾锚点随拼接迁移。
3. **可收敛性**：递归图不被整体复制进调用方，增长与次数受预算约束，避免展开失控。

### 1.1 重要：`std::opt::inline` 是策略化目标选择

实现上必须把下面两点分开理解：

| 层次 | 含义 |
|------|------|
| **`inlineCallableInDraft`**（`include/camel/runtime/draft_inline.h`） | **通用原语**：对**任意**满足前提的 `FUNC` 节点，把其被调图（`GCFuncBody::calleeGraph`）克隆进调用方并返回 `DraftInlineResult`。 |
| **`InlineRewritePass`（`std::opt::inline*`）** | **策略层**：先枚举 `FUNC` 候选，再按策略过滤，并通过 `inlineCallInOrder`（`include/camel/passes/opt/inline.h`）调用上述原语。支持 `Small` / `Arm` / `Hybrid` 三种策略（`InlineTargetStrategy`）。 |

当前 pass 入口命名与含义（注册表见 `src/execute/pass.cpp`）：

- `std::opt::inline`：默认综合策略（等价 `std::opt::inline::hybrid`）；
- `std::opt::inline::small`：只内联“小”被调图上的调用，且该调用**不是**分支臂首；
- `std::opt::inline::arm`：只内联位于分支臂首的调用，不要求被调图“小”；
- `std::opt::inline::hybrid`：综合策略（被调图“小”，或调用位于分支臂首，满足其一即内联）。

重要边界：

- `std::opt::inline*` 只负责 **inline target selection + inline rewrite**；
- 若需要运行聚合优化流水线（如 **specialization / devirtualization / inlining**），
  入口使用 `std::opt`：循环执行去虚化、特化、内联、常量折叠、公共子表达式消除与死代码消除，直到稳定或达到预算。
  `std::inline`（含 `small` / `arm` / `hybrid`）保留为 `std::opt::inline` 的别名。

控制依赖采用源头规避与末尾清理结合的方式。编译器加同步边时，最多检查 64 条 CTRL 边；
冻结每张图前再清理一次，最多检查 8192 条边。内联、特化、fold、cse、dce 转接依赖时，
通过 `controlFrontier` 保留足以表达原有顺序的前驱集合（每组最多 128 单位搜索工作），
通过 `appendControlDependency` 避免补入已有 CTRL 路径蕴含的边（每次最多检查 32 条边）。
预算耗尽时保留尚未证明冗余的依赖，不建立全图传递闭包。

`std::opt` 收敛或达到轮数预算后，最后执行一次 `std::opt::ctrl`，也可单独运行该遍。
清理阶段对每个目标最多检查 256 条边，整图的检查量限制为 `8 * (节点槽数 + CTRL 边数)`，
另有 250000 的上限；辅助存储与图大小线性相关。只删除能由其他 CTRL 路径证明的边，
不借普通值边作证明，不跨 BRCH/JOIN，也不改分支臂锚点或删掉 GATE 的最后一条 CTRL 输入。

其中“小”的判定见 §6.1：除了节点数阈值（除 `DATA/PORT` 外节点数 `<= 8`，
`InlineRewriteConfig::smallSubgraphMaxNonDataPortNodes`）之外，还考虑调用点个数、
被调图是否返回闭包，以及重复代码量预算。

---

## 2. Pass 入口与事务边界

`InlineRewritePass::apply` 以“仅开启内联”的 `OptimizeRewriteConfig` 调用 `applyOptimizeRewritePass`
（`src/passes/opt/inline/engine.cpp`）；`std::opt` 由 `simplifyGraph` 驱动，每轮调用该引擎完成
去虚化、特化与内联，再执行 fold、cse 与 dce。下面描述的是引擎内的重写会话。
每一轮重写会话的动作：

1. 用 `RuntimeGraphDraftSession session(context, runtimeRoot)` 开启重写事务；
2. 通过 `session.edit(graph)` 取得某张运行时图的可编辑 `GraphDraft`（按需从 `GCGraph` 解码），
   根图的 draft 为 `session.rootDraft()`；
3. 后续所有编辑都落在 draft 上（而非直接改 `GCGraph`）；
4. 结束时：
   - 若无改动，不提交，沿用原图；
   - 若有改动，调用 `session.commit()`，重新编码可达闭包并替换运行时根图。

一次会话只内联会话开始时已存在的调用；由内联新暴露出的调用留给下一次会话。
`applyOptimizeRewritePass` 重复开启会话直到某次会话不再有改动（会话数另有上限 `kMaxSessions`）。

这意味着 `inline` pass 在架构上遵守“**draft 内改写，commit 时一次性收口**”原则。

---

## 3. 第一阶段：会话与可编辑草稿

### 3.1 可达图收集

`session.collectReachableRuntimeGraphs()` 从会话的根图出发做广度优先遍历，返回本次会话需要考虑的
运行时图序列（根图在首位）。对每张图，后继按下面的方式取得：

- 该图在会话中还没有 draft：取运行时图上登记的 `dependencies()`、`subGraphs()` 与
  `staticGraphRefs()`（`GCGraph::traceGraphs`）；
- 该图已有 draft：取 draft 上登记的同名三张表，加上 draft 中每个 `FUNC` 节点的被调图，
  以及静态槽位中的值（如函数值）所引用的图。

因此遍历结果反映会话内已经做出的编辑：某张图只被 draft 引用时也在闭包内。

### 3.2 按需解码

`session.edit(graph)` 是取得可编辑图的唯一入口：

- 会话为每张运行时图至多保存一个 draft，以该图的 `GCGraph` 指针为键；
- 第一次请求时用 `GraphDraft::decode` 把运行时图解码成 draft，之后的请求返回同一个 draft；
- `session.tryDraft(graph)` 只查询不解码，图尚未被编辑时返回空；
- 解码不修改运行时图本身，`GCGraph` 在整个会话期间保持只读。

解码得到的 draft 保留节点的来源：`GraphDraft::sourceRefOf` 对解码出的节点返回它在运行时图中的
节点引用，对会话中新加的节点返回 `kInvalidNodeRef`。引擎用它区分“会话开始时已存在的调用”与
“内联带进来的调用”（见 §6.1）。

---

## 4. 第二阶段：会话内的跨图引用

### 4.1 图的身份

draft 中所有指向其他图的引用都是运行时图指针（`GCGraph *`）：

- `FUNC` 节点负载中的 `GCFuncBody::calleeGraph`；
- 静态槽位中的函数值所持有的图；
- draft 上登记的 `dependencies()`、`subGraphs()`、`staticGraphRefs()`。

一张图被 `session.edit` 之后，别的图对它的引用不变，仍然是原来的 `GCGraph` 指针；会话用这个指针
找到它的 draft。所以会话进行期间一张图只有一个身份，编辑某张图不需要改写其他图里的引用。

### 4.2 读取被调图

`inlineCallableInDraft` 读取被调图时先问 `session.tryDraft(calleeGraph)`：

- 被调图在本会话中已有 draft：从 draft 克隆（`cloneDraftGraphIntoDraft`），拿到的是包含本会话
  编辑结果的函数体；
- 否则直接从运行时图克隆（`cloneRuntimeGraphIntoDraft`）。

两种克隆都把被调图登记的 `dependencies()`、`subGraphs()`、`staticGraphRefs()` 追加到调用方 draft
（`addDependency` / `addSubGraph` / `addStaticGraphRef`，去重），所以函数体里带进来的图引用在调用方
仍然可达。这三张表在会话内只增不减。

### 4.3 提交时统一改写

`session.commit()` 负责把会话内的图引用换成新编码的图：

1. 重新收集可达闭包，并为闭包内每张图准备 draft（未编辑过的图在此时解码）；
2. 为每张图分配新的运行时图，得到“原图 → 新图”的映射；
3. 按闭包逆序逐图编码，编码时 `FUNC` 的被调图、三张登记表以及静态槽位中的函数值全部经该映射改写；
   编码出的依赖表由 draft 登记的依赖加上每个存活 `FUNC` 节点的被调图组成；
4. 全部编码完成后做结构检查（见 §8.2）。

draft 引用的图必须在闭包内，否则编码时触发断言。引擎每一轮开始时也做同样的检查：闭包内任何
`FUNC` 节点的被调图不在闭包中时抛出 `std::runtime_error`。

提交之后，不在闭包内的图不会被重新编码；仍被某张登记表或某个节点引用的图则保留在程序中。

---

## 5. 第三阶段：轮次与扫描顺序

### 5.1 会话内的轮次

一次会话由若干“轮”组成，每一轮：

1. 调 `session.collectReachableRuntimeGraphs()` 取得当前闭包；
2. 对闭包做调用图分析（`analyzeRuntimeCallGraphScc`，见 §5.2），并收集仍未展开的宏算子所作用的图
   （见 §6.1）；
3. **按闭包的逆序**逐张扫描：`session.edit` 取得 draft，枚举其中的调用，按 §6.1 选出候选；
4. 对候选逐个调用 `inlineCallInOrder`；**第一次内联成功后本轮立即结束并重新开始下一轮**——
   内联改变了同一张图里其余 `FUNC` 节点的值/控制上下文，也改变了闭包与调用计数，已算好的候选列表
   不能继续使用。

闭包由广度优先遍历得到、根图在首位，所以逆序扫描意味着离根图较远的图先被处理，根图最后处理。

会话在下列情况之一结束：某一轮扫描完整个闭包而没有任何改动；累计内联次数达到
`kRuntimeRewriteApplyBudget`。会话结束时若有改动则提交。

当去虚化与特化同时开启（`std::opt`）时，它们在同一次扫描中、先于内联作用于每张图；本文不展开。

### 5.2 调用图分析的用途

`analyzeRuntimeCallGraphScc` 以闭包内的运行时图为节点、以直接调用（`FUNC` 节点到其被调图）为边，
求强连通分量，结果记录在 `RuntimeCallGraphSccInfo` 中：

- `componentOf` / `components`：每张图所属的分量；
- `recursiveGraphs`：位于调用环上的图（分量多于一张图，或单张图直接调用自己）；
- `componentEntryGraphs`：被分量外的图调用的图（分量的入口）；递归分量若没有已知的外部入口，
  则把全部成员作为内联边界。DCE 可能删掉最后一个外部调用，但图注册表仍保留该分量；
  此时不能失去入口保护，让成员互相内联并展开整个递归环。
- `externallyCalledGraphs`：被闭包内某张图直接调用的图；
- `reachesRecursion`：能沿直接调用到达某张递归图的图（递归图自身也在内）。

分析读取的是运行时图而不是 draft，所以它描述的是会话开始时的调用关系。它**不决定扫描顺序**，
只为 §6.1 的递归保护与“小”的判定提供依据。

---

## 6. 第四阶段：目标选择与单次改写

### 6.1 候选枚举与策略过滤

整张图的排除：若某张图是仍未展开的宏算子的函数操作数，或可由这样的图经调用/函数常量到达，
则不向它内联任何调用（这类变换需要图保持编译时的形状）。从别处对这些图的调用照常内联。
宏算子的函数操作数不是图中的常量（例如是形参或某次调用的结果）时，无法确定它作用于哪张图，
本轮不向任何图内联。

对其余每张图，引擎枚举 draft 中的 `FUNC` 节点，满足以下条件的才进入判定：

- 节点是从运行时图解码出来的（`sourceRefOf` 有效），即会话开始时已存在；
- 被调图非空，且不是调用方自己（直接自调用不内联）。

随后按固定顺序判定，任何一步不通过，该调用就保持为调用，并记录原因（`InlineDecline`）：

1. **递归保护**（`shouldSkipRecursiveInline`，原因 `Recursive`）。被调图位于调用环上时：
   - 调用方与被调图不在同一分量：不内联——递归图不被整体复制进环外的调用方；
   - 在同一分量、且被调图是该分量的入口、且 `InlineRewriteConfig::blockCallsToSccEntryCallees`
     开启（默认开启）：不内联，防止 `fib -> arm -> fib` 这类场景把入口函数反复向内展开；
   - 同一分量内对非入口图的调用可以内联。
2. **“小”的判定**。先确定被调图是否“无环”：它参与了本轮调用图分析，并且不能到达任何递归图。
   被调图满足下列任一条即视为“小”：
   - 除 `DATA/PORT` 外的节点数不超过 `smallSubgraphMaxNonDataPortNodes`；
   - 无环，且全程序只有一处直接调用它（内联不产生重复代码）；
   - 无环，且返回类型可能含函数值（函数类型，或含函数类型的元组）——内联后调用方能看到它构造的闭包；
   - 无环，有多处调用，且（调用点数 − 1）× 被复制节点数不超过
     `InlineRewriteConfig::duplicationBudgetNodes`。

   其中“被复制节点数”是被调图中除 `DATA/PORT` 外的节点数；调用点数在闭包范围内统计，
   图有 draft 时按 draft 统计。
3. **是否分支臂首**（`isDraftBranchArmHead`）：调用节点的控制输入中有 `BRCH` 节点。
4. **`Hybrid` 下的一条抑制规则**（原因 `Strategy`）：调用方自身是节点数意义上的小图、被其他图直接
   调用、且不在调用环上时，不内联它的臂首调用。
5. **策略**（`shouldInlineTarget`）：
   - `Small`：“小”且不是臂首；
   - `Arm`：是臂首；
   - `Hybrid`：“小”或是臂首。

   不通过时的原因：被调图“小”但策略不选它为 `Strategy`；不“小”且不满足“无环”为
   `ReachesRecursion`；不“小”、无环且有多处调用为 `Duplication`；其余为 `Strategy`。
6. **增长检查**（最后做，原因 `OwnerSize` / `GrowthBudget`）：见 §8.1。

通过全部判定的调用成为候选。**内联带进调用方的 `FUNC` 节点不在本次会话中继续内联**，
留给下一次会话。

### 6.2 单目标改写步骤

对每个目标，pass 调用 `inlineCallInOrder(session, draft, funcNodeId)`。调用原本隐含地规定了先后次序：
它晚于其全部实参的来源运行，它的使用者晚于它运行。函数体未必读取每个实参，所以这些次序必须在拼接
前后显式化：

1. 用 `orderedSources`（`src/passes/opt/generic/rewrite.h`）求出调用各实参的**有序来源**：沿值输入
   （以及途中纯节点的控制输入）向上，遇到的第一批携带次序的节点——调用、分支、有副作用的算子、
   带控制输入的 `GATE` 等；
2. 被调图可能有副作用时（图中、其子图或其直接调用可达的图中含 `CALL` 节点或未登记为纯的算子），给调用节点
   补上到每个有序来源的控制边，使函数体整体晚于这些来源运行。纯的被调图不加这类边；创建变量的
   算子 `:var/new` 也不作为等待对象；
3. 把调用自身的控制前驱并入来源集合，然后对调用的每个使用者（值使用者与控制使用者）记录：哪些来源
   是它只经由该调用才晚于的；
4. 调 `inlineCallableInDraft(session, draft, funcNodeId)` 完成拼接（见 §7）；拼接失败时到此为止，
   返回无效结果；
5. 拼接成功后，为第 3 步记录的使用者补上到相应来源的控制边。

克隆、端口绑定、使用者重定向与删除原节点的顺序集中在 `inlineCallableInDraft` 内，pass 不自行编排。

### 6.3 GATE 门控的语义价值

内联后值出口通常是数据节点，而控制出口是另一条路径。直接给值节点硬挂控制边会污染调度语义。

`GATE` 在这里承担“值/控会合代理”，保证：

- JOIN 看到的是带控制约束的值；
- 不要求原值节点本体承载控制语义。

---

## 7. `inlineCallableInDraft` 的关键语义（底层 API）

`inlineCallableInDraft` 是唯一内联原语，返回 `DraftInlineResult`：

- `callNode`：被内联的 `FUNC` 节点；
- `valueExit`：值出口；
- `ctrlEntry`：控制入口；
- `ctrlExit`：控制出口。

结果可按布尔值判断：`valueExit` 有效即表示内联成功。

**前提**（任一不满足即返回无效结果，draft 不变）：目标是 `FUNC` 节点且被调图非空；调用的实参数
（普通输入加 `with` 输入）等于被调图的形参端口数（`normPorts` 加 `withPorts`）；被调图有入口节点与
输出节点。

### 7.1 函数体的等待对象

函数体需要等待的节点集合由两部分组成：

- 调用节点自身的控制前驱；
- 把调用**放进分支臂**的实参。调度器按输入决定一个节点是否属于某个臂；函数体里不读取任何形参的
  节点（对常量的分支、无参调用、副作用）在调用方没有任何输入，如果调用只是因为某个实参才位于臂内，
  这些节点会离开该臂。因此这样的实参也算作等待对象。

下文称这个集合为“外部控制前驱”。

### 7.2 克隆与形参绑定

1. 把被调图克隆进调用方 draft（来源见 §4.2）。克隆为函数体节点分配调用方自己的运行时槽位，把用到的
   静态槽位复制到调用方，并保留节点的源码位置信息；
2. 外部控制前驱非空时，为每个实参建一个 `GATE`（值输入为该实参），形参绑定到这个 `GATE` 上；
   否则形参直接绑定到实参；
3. 绑定即把克隆出的端口节点的全部值使用者改接到绑定对象（`replaceAllValueUses`），随后删除端口节点。

### 7.3 控制入口

入口目标按下面的优先级确定：

1. **可执行入口根**：被调图中能从其出口节点反向到达、种类不是 `DATA` / `PORT` / `DREF`、且所有输入
   都是 `DATA` 或 `PORT` 的节点（`SYNC` 与 `GATE` 也可以是入口根）；
2. 没有入口根时，取 §7.2 建立的参数 `GATE`；
3. 仍然没有时，取克隆出的入口节点（它若是端口，则取其绑定对象）。

然后按目标数量决定入口形式：

- 恰有一个目标：它就是 `ctrlEntry`，外部控制前驱追加为它的控制输入；
- 多个目标：新建一个以外部控制前驱为控制输入的 `SYNC` 作为 `ctrlEntry`，每个目标等待这个 `SYNC`。

参数 `GATE` 的控制输入是外部控制前驱（单目标时）或上述 `SYNC`（多目标时），保证参数先于读取它们的
入口节点就绪；自身就是 `ctrlEntry` 的那个 `GATE` 除外。

### 7.4 值出口、控制出口与会合

- **值出口**依次尝试克隆出的返回节点、输出节点、出口节点：候选若是端口则取其绑定对象，再穿过没有
  控制输入的 `GATE`（`resolveForwardedValueRef`），第一个拥有数据槽位的节点即 `valueExit`。
  全部落空时返回无效结果，调用节点保持原样。
- **控制出口**取克隆出的出口节点（没有则取返回节点，再没有则取输出节点），同样先换成绑定对象；
  该节点不存活时退回 `valueExit`。

随后在需要时插入会合用的 `GATE`：

- 外部控制前驱非空且没有可执行入口根时：如果返回值就是已经等候这些前驱的参数 `GATE`，直接复用；
  否则新建 `GATE`，值输入为 `valueExit`，控制输入为外部控制前驱加
  `ctrlExit`；`ctrlEntry`、`ctrlExit`、`valueExit` 都指向它；
- 解析出的控制出口与值出口不是同一节点时：如果 `ctrlExit` 已经是以 `valueExit` 为唯一值输入的
  `GATE`，直接复用它作为值出口；否则新建 `GATE`，值输入为 `valueExit`，控制输入为 `ctrlExit`。
  复用避免产生 `GATE(v, ctrl=GATE(v, ...))`，同时保留原有的完成条件。转发不同值的门控不适用此规则，
  上述无入口根的情形仍须先接好调用方的控制前驱；
  `valueExit` 与 `ctrlExit` 都指向它。

因此返回的结果中，值与控制完成点分离的情形总是被一个 `GATE` 收拢：`valueExit` 与 `ctrlExit`
指向同一节点。调用方仍应分别使用结果中的两个字段，而不是自行假设。

### 7.5 使用者重定向

最后三步把调用节点从图中换下：

1. `retargetBranchArmAnchors`：调用节点若是某个 `BRCH` 的臂首，臂首改为 `ctrlEntry`；若是臂尾，
   臂尾改为 `ctrlExit`；
2. `replaceAllValueUses(funcNodeId, valueExit)`：值使用者（普通与 `with` 输入）以及 draft 的输出/返回
   锚点改指 `valueExit`；`replaceAllCtrlUses(funcNodeId, ctrlExit)`：控制使用者以及 draft 的入口/出口
   锚点改指 `ctrlExit`；
3. 删除原 `FUNC` 节点。

---

## 8. 第五阶段：预算与提交

### 8.1 增长与次数上限

内联受以下上限约束（`include/camel/passes/opt/config.h`、`src/passes/opt/inline/engine.cpp`）：

- `OptimizeRewriteConfig::growthBudgetNodes`（默认 `kDefaultGrowthBudgetNodes`，可由环境变量
  `CAMEL_OPT_GROWTH_BUDGET` 覆盖）：特化与内联在整个程序上可复制的节点总数。每次内联按被调图中
  除 `DATA/PORT` 外的节点数计入；
- `InlineRewriteConfig::maxOwnerNodes`（默认 `kMaxInlineOwnerNodes`）：内联后调用方图的节点数上限；
- `InlineRewriteConfig::duplicationBudgetNodes`：非小子图的无环被调图有多个调用点时，
  全部内联所重复的代码量上限（见 §6.1）；
- `kRuntimeRewriteApplyBudget`：单次会话内实际应用内联的次数上限；
- `kMaxSessions`：一次 pass 运行中会话数的上限。

前两项在 §6.1 的判定中最后检查，所以被它们拦下的调用是引擎原本会内联的调用。

触及上限时不报错：相应调用保持为调用，程序语义不变，只是展开得更少。引擎最后一轮检查过而未内联的
直接调用及其原因（`InlineDecline`）记录在 `InlineReport` 中，作为 pass fact `kInlineReportFactKey`
挂在 `Context` 上，供后续遍解释某个调用为何仍是调用。

### 8.2 提交时的收口与结构检查

`session.commit()` 在编码前对闭包内每个 draft 调用 `GraphDraft::dissolveUnorderedGates`：没有控制输入
的 `GATE` 只是转发它的值，其值使用者改为直接读取该值，`GATE` 被删除。作为分支臂首或臂尾的 `GATE`
不删除；它若没有控制输入，则改为等待所属的 `BRCH`。

编码完成后，对重新编码出的每张运行时图执行结构检查（`validateRuntimeGraphPayload` 与
`validateRuntimeGraphCalls`），检查不通过时抛出异常、提交失败。检查项与契约见
[`17_graph_rewrite_api_contracts.md`](17_graph_rewrite_api_contracts.md)。

会话的根图是程序当前的运行时根图时，提交把新根图安装到 `Context` 上；否则只返回改写后的根图，
正在运行的程序不受影响。

---

## 9. 涉及的 Graph API 分层图

### 9.1 Pass 层（`InlineRewritePass` 与重写引擎）

- 只表达“要做什么”：
  - 配置策略与预算（`InlineRewriteConfig`、`OptimizeRewriteConfig`）；
  - 组织会话与轮次（§5）；
  - 选择目标（§6.1，策略含义见 §1.1）；
  - 调 `inlineCallInOrder`，在拼接前后补齐次序（§6.2）。

### 9.2 Session 层（`RuntimeGraphDraftSession`）

- 事务包装（`include/camel/runtime/draft_session.h`）：
  - `edit / rootDraft / tryDraft`：按需取得某张运行时图的 draft
  - `collectReachableRuntimeGraphs`：枚举从根图可达的运行时图
  - `findSpecialization / rememberSpecialization`：会话内的特化缓存
  - `commit`：重新编码可达闭包并替换运行时根图

### 9.3 Draft 层（`GraphDraft`）

- 一张运行时图的可编辑形式（`include/camel/runtime/draft.h`）：
  - `decode` / `encode`：与不可变的 `GCGraph` 互相转换；
  - 节点 id 在 draft 生命周期内稳定，删除节点不影响其他节点的 id；
  - 节点与边的编辑：`addNode`、`eraseNode`、`setCtrlInputs`、`appendInput`、`replaceInput`、
    `replaceAllValueUses`、`replaceAllCtrlUses`、`retargetBranchArmAnchors` 等；
  - 图级锚点（入口、出口、输出、返回）与端口表；
  - 跨图登记表：`addDependency`、`addSubGraph`、`addStaticGraphRef`。

### 9.4 克隆与拼接层

- `cloneRuntimeGraphIntoDraft` / `cloneDraftGraphIntoDraft`（`include/camel/runtime/draft_clone.h`）：
  把一张图的全部节点追加进调用方 draft，返回 `DraftGraphCloneResult`（源节点到克隆节点的映射，
  以及克隆后的端口与锚点）。
- `inlineCallableInDraft`（`include/camel/runtime/draft_inline.h`）：在克隆之上完成端口绑定、
  入口/出口解析、使用者重定向与删除调用节点。
- `inlineCallInOrder`（`include/camel/passes/opt/inline.h`）：在拼接之上保持调用原有的次序。
  需要把被调函数体带进某张图的变换都应经由它，而不是直接调用拼接原语。

各层的调用契约见 [`17_graph_rewrite_api_contracts.md`](17_graph_rewrite_api_contracts.md)。

---

## 10. 当前实现的关键不变量（建议代码评审必查）

1. **图引用在闭包内闭合**
   draft 中每个图引用（`FUNC` 的被调图、登记表、静态槽位里的函数值）指向的图都必须在会话的可达闭包内，
   提交时才能改写到新图上。

2. **只经由 draft 改写**
   `GCGraph` 在会话期间只读；所有编辑落在 `session.edit` 返回的 draft 上，由 `commit()` 一次性生效。

3. **函数体不离开分支臂**
   调用位于分支臂内时，函数体等待调用的控制前驱以及把调用放进臂内的实参；调用是臂首/臂尾时，
   锚点迁移到 `ctrlEntry` / `ctrlExit`。

4. **次序不丢失**
   调用隐含的先后次序（晚于实参的有序来源、使用者晚于调用）在拼接后由显式控制边保持；
   pass 通过 `inlineCallInOrder` 而不是直接通过 `inlineCallableInDraft` 内联。

5. **DraftInlineResult 合法性**
   内联成功时 `valueExit` 是拥有数据槽位的节点（必要时为会合用的 `GATE`）；提交时每个 `GATE`
   必须同时有值输入与控制输入。

6. **会话收口一致**
   仅在有改动时 `commit()`；否则沿用原图，避免伪改写产物。

---

## 11. 已知工程取舍

- **“小”是静态启发式**：判定只看节点数、调用点数、返回类型与重复代码量，没有调用频次/热度参与。
- **每次内联后重新开始一轮**：候选列表、闭包与调用计数都只在一轮内有效，换取判定始终基于当前的图。
- **单次会话不追新增 FUNC**：内联带进来的调用留给下一次会话，换取可收敛与可解释性。
- **递归图不进入环外调用方**：递归分量内部可以压缩辅助图，但递归环本身保持封装在自己的图里。
- **入口根优先**：控制入口优先对齐到函数体自身的可执行入口根，没有入口根时才回退到参数门控。
- **预算触顶不报错**：相应调用保持为调用，仅记录警告日志；被留下的调用及原因可从 `InlineReport` 读取。

---

## 12. 建议的后续演进方向

1. 引入“目标收益估计”（调用频次/节点增长率）驱动 selective inline，补充现有的静态判定。
2. 对 `DraftInlineResult` 增加结构化统计（克隆节点数、导入依赖数）以提升可观测性。
