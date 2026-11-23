# 视图答不出什么：`onnx` 与 `tiles` 停下来的 11 处

2026-11-24 ｜ 来自 `lane/3a-onnx-tiles`（阶段 3a，目录 `src/stdlib/onnx`、`src/stdlib/tiles`）｜ 对象：`kernel/graph/graph_view.h`

## 这一页是什么

这一页记的是 **`GraphView` 的缺口**，不是 `onnx`/`tiles` 欠的活。作者的原话：「读不了视图的（需要视图没有的能力）不硬改，列成清单交回，那是视图的缺口。」

计数（`git grep -cE '\bGCGraph\b|\bGraphDraft\b' -- src/stdlib/onnx src/stdlib/tiles`）：改前 `onnx 84 / tiles 74`；本轮改后 `onnx 72 / tiles 74`。**`tiles` 一处没减，原因就是下面第 1 至第 4 条**——那四条覆盖了 `tiles` 的 74 处里几乎每一处。凡是视图现有能力能等价表达的，本轮都改掉了（8 处）。

第 ④ 项分三档，不许混：

- **可以** —— 写清怎么替；
- **部分** —— 写清**哪一半**能替、哪一半不能；
- **不能** —— 写出**缺的那个方法名**。

**当前总账：可以 1 ／ 部分 2 ／ 不能 5**（8 条）。上一版的读数是 `可以 0 ／ 部分 2 ／ 不能 6`；差值来自另一条车道把 `C-1123-59` 的四项视图能力落进了 `origin/develop`（`3185345334`），其中两项正好落在本页的缺口上，见第四节。

本页的行号是**本车道基线 `98b56b3d5d` 的读数**；第四节谈的四项按 `origin/develop` 的 `3185345334` 读。

本轮那 8 处改动**已在构建槽上取过读数**（`feat/modules/onnx`＋`feat/modules/tiles`，268 例逐条比对，改前/改后**完全相同**：pass 233 / fail 27 / skip 6 / known 2；其中 `tiles` 109 例一例未动）。所以下面这张清单是「能改的都改完、读数确认没动」之后剩下的。

---

## 一、八条缺口

### 1. 节点与区域的属性（`attrsOf` / `RegionRecord` 的 tag set）

**① 位置**：`tiles/src/placement.cpp:27`（`functionGraphOf`）、`:41`（`bodyAttrs`）、`:47`（`factRegion`）、`:175`（`placementOf`）、`:348`（`GpuScopes::covers`）；`tiles/src/compiler.cpp:75`（`checkAttributeFates`）；`onnx/exporter.cpp:741`（`recordFates`，读 `nodeAttrs`/`nodePermissions`）。

**② 缺的能力**：节点属性 `attrsOf(NodeRef) -> AttrSetId`、节点权限、以及区域的 tag set（`GCRegion::attrs`）。`RegionRecord` 明确写了「its tag set (a fact, not a place)」——视图刻意不交；节点侧的读标签入口在 `graph_view.h` 末尾只注明「arrives as an addition」，今天没有。

**③ 今天为何需要**：Tiles 的放置查询读的是**写在节点或最近一层区域上的 `tiles.placement` 标签**——`placementOf` 问节点自己的标签，`factRegion` 沿 `region->parent` 上溯找最近一条带该键的区域标签，`bodyAttrs` 读区域 0 的标签。`checkAttributeFates` 与 `recordFates` 更直接：把属性集逐条过 fate 表，没有 fate 就拒绝。

**④ 三分**：**不能**（**仍是本车道的主诉：四項新能力一项都没碰它**）。

要的是两个方法，消费者各一处：

- `attrsOf(NodeRef) -> AttrSetId` —— 消费者 `tiles/src/placement.cpp#placementOf`：它先看节点自己的 `tiles.placement` 标签，没有再沿区域上溯；
- `RegionRecord::attrs`（或 `regionAttrs(RegionId)`）—— 消费者 `tiles/src/placement.cpp#factRegion`：它沿 `region->parent` 找**最近一条带该键的区域标签**，`RegionRecord` 今天五个字段里没有 tag set，所以这半截根本没有入口。

这条也是 `tiles 74 → 74` 的**唯一主要原因**：放置/编译两侧都要读标签，而标签不在视图上。

### 2. 区域抽取所需的存储细节

**① 位置**：`tiles/src/region_extract.cpp:29`（`typeOf` 读 `dataIndex`/`staticSlots`/`staticDataType`）、`:41`（`loopInternals`）、`:67`（`copyLoopNodes`）、`:119`（`makeLoopRegions`）、`:146`（`extractRegion`）。

**② 缺的能力**：原始 16 种 `GCNodeKind`（CAST/COPY/FILL/ACCS 不被折叠）、`GCNode::flags`、`nodePayloadBytes`（把算子/LOOP 的 payload 原样搬走）、`ctrlInputsOf`、`regionPorts` 与 `GCRegion::params`。（**常量槽那一半已经不再是缺口**，见 ④。）

**③ 今天为何需要**：抽取是**把一整个区域复制成一张新草稿**——每个成员节点按 `.kind/.flags/.payload/.permissions/.origin` 逐字节搬过去，输入三组（norm/with/ctrl）逐条重映射，区域按 `regionPorts`/`params` 复制。这不是读一个谓词，是复制一段存储。

**④ 三分**：**部分**（比上一版好了一半）。

- **新补齐的一半：常量槽可以替。** `origin/develop` 的视图有了 `staticIndexOf(NodeRef)` 与 `staticSlotCount()`（`C-1123-59` 的第 ④ 项），于是 `typeOf` 里那一支「`dataIndex < 0` → `staticSlots()[-dataIndex]`」可以写成「`staticIndexOf(ref)` 有值 → 那份常量」，配上 `constantOf`/`typeOf` 就够了；读者不再需要 `dataIndex` 的**编号语义**。
- 仍是**不能**的一半：`nodePayloadBytes`（视图不交 payload 字节，`graph_view.h` 明说「no payload bytes leave this face」）、原始 kind（四种被折进 `Oper`，而这里要按 `Cast`/`Fill`/`Accs` 分支）、`flags`、`ctrlInputsOf`。这些**不该**由只读面加方法（那正是视图拒绝的东西）——这一半的等价物在 3b 的编辑入口（`GraphTransaction::edit()` 的区域复制）。

### 3. `uses()` 反向索引、`syncEdges()` 全表、`outputNodeRef`

**① 位置**：`tiles/src/graph_regions.cpp:128`（`chainMembers` 建在 `syncEdges()` 上）、`:161`（读 `uses().sync`）、`:515`/`:525`（按 `nodeCount()` 建 `RegionNode` 表）、`:647`（`outputNodeRef`）、`:670`（`nodeOrigin`）。

**② 缺的能力**：正向的 `users()`（视图有，但按组），**反向**的使用索引 `uses()`（一次答「谁读过我」，含 data/sync/ctrl 三种）、`syncEdges()`（整张图的 sync 边表）、`outputNodeRef()`（图的输出节点，与 `exit()` 不是同一个）。

**③ 今天为何需要**：`chainMembers` 要先知道一个 `sync` 块里哪些节点在链上（按 `syncEdges` 的 user/pred 建并查集）；`region` 划分要按「谁读谁」连边，控制边决定同一臂内的语句顺序，sync 边决定环形；`outputNodeRef` 标记区域里哪个节点是结果。

**④ 三分**：**部分**。能替的一半：`users(ref, Normal|Context)` 可替 `normUsersOf`/`withUsersOf`，`predecessors(ref, Sync)` 可替 `syncPredsOf` 的逐节点读，`originOf` 可替 `nodeOrigin`。不能替的一半：(a) `syncEdges()` 是**全表扫描**，视图只按节点答，改成「遍历 `nodes()` 再问每个节点的 sync 前驱」会漏掉 JOIN/DREF 上的边（`contains` 对它们答 false）；(b) `uses()` 是反向索引，视图只答 `users()`（正向），要自己建表就失去索引意义；(c) `outputNodeRef()`：视图只有 `exit()`。缺的方法：`outputOf()`（若输出与 exit 确是两个概念），以及一个「按边种类答全表」的入口。

### 4. 程序侧：图→Program 的解析、可达图枚举、草稿写句柄

**① 位置**：`tiles/src/compiler.cpp:60`/`:67`/`:146`（`Frame` 与 `active` 持有 `GCGraph *`）、`:362`（`call` 的 `home` 指针）、`:1375`（`const_cast` 回 `GCGraph *`）；`tiles/src/region_outline.cpp:317`/`:1012`（`session.collectReachableRuntimeGraphs()`）、`:436`、`:465`、`:512`、`:678`；`tiles/src/graph_values.cpp:69`/`:100` 与 `graph_values.h:14`/`:18`（`GraphDraft &`）；`tiles/src/region_outline.cpp:105`–`:218`（`finish`/`order`/`upload` 都写草稿）。

**② 缺的能力**：(a) 「程序里有哪些图」与「这张图属于哪个 Program」；(b) `GraphTransaction`/`GraphDraft` 的**写**入口。

**③ 今天为何需要**：编译与抽取都从「程序的可达图集合」开始，逐图切区域、再逐区域抽成草稿；解析 callee 必须知道当前图属于哪个 Program（`runtimeProgram`），因为 `Program::calleeOf(graph, id)` 是按图指针查的。写草稿是抽取的产物。

**④ 三分**：**不能**。缺的是两侧：`Program` 侧要接受 `const Graph *`（`graphs()` 交出 `vector<const Graph *>`，`calleeOf` 收 `const Graph *`），视图本身不答「谁持有我」；写侧不在只读面上，等价物是 3b 的编辑原语（`GraphTransaction::edit()`）。

### 5. `programOfRuntimeGraph(GCGraph *)`

**① 位置**：`onnx/exporter.cpp:186`（`programOf`）、`:317`（`Evaluator::programOf`）；同一形状在 `onnx/passes/export.cpp:359`（`programOfGraph`）。

**② 缺的能力**：`core::context::Context::programOfRuntimeGraph` 只收 `const GCGraph *`，没有收 `const graph::Graph *` 的重载，也没有「这张图属哪个 Program」的视图查询。

**③ 今天为何需要**：导出每遇到一个 callee 都要拿到持有它的 Program，才能 `program.calleeOf(...)`、才能遍历可达图做递归/效应检查。

**④ 三分**：**不能**。缺 `Context::programOf(const graph::Graph *)`（内部按同一张表查）。视图侧无可替：`CalleeRef::graph` 只答「我调到谁」，答不出「我属于谁」。

### 6. `ctrlInputsOf`（`runsBefore`）

**① 位置**：`onnx/exporter.cpp:549`（`runsBefore`）；`:565` 处 `for (const auto preds : {g->normInputsOf(ref), g->withInputsOf(ref), g->ctrlInputsOf(ref)})`。

**② 缺的能力**：控制输入边（输入表的第三段 run）。

**③ 今天为何需要**：判断「同一条臂/同一个块里，这次赋值是否一定在读之前」——变量读写能不能导出，全押在这条顺序上。

**④ 三分**：**不能**。视图删掉了控制输入，理由是「控制输入不是边，它说的是区域（加上 sync 边）」。但 `runsBefore` 问的正是**区域内语句的先后**：`regionOf` 只答「同不同一个区域」，`predecessors(Order)` 答的是 order 边，都不是 `ctrlInputsOf`。缺的方法：`predecessors(ref, EdgeKind::Wait)`，或者一条明说「这张图里 `from` 是否先于 `to`」的查询——**视图必须自己给出这条规则**，否则每个读者各写一份，正是这条面要消除的东西。

### 7. `hasNodePayload()`——一个循环依赖，不只是一个缺方法

**① 位置**：`onnx/exporter.cpp:590`、`:1043`、`:1070`、`:1108`、`:1134`、`:5211`（六处图遍历的入口）。

**② 缺的能力**：`hasPayload()`——「这张图挂上 payload 了没有」。

**③ 今天为何需要**：遍历程序里的可达图时，有些图（未编码的、只作名字存在的）没有 payload，读它的 `kind` 会读到空。今天每一条遍历都用 `!g->hasNodePayload()` 当**闸门**：没挂就跳过，不报错。

**④ 三分**：**不能**，而且是最难看的一条：**视图读 `kind`/`operatorOf`/`originOf` 本身就要在 payload 之上**，所以「先问它挂没挂 payload，再读」在视图上已经晚了——问的时候视图已经在用 payload 了。缺的方法：`hasPayload()`。

**这一裁已下（2026-11-24，语言作者，经协调者转达）**：「没有任何节点的图是**空图**，不是**拒绝**」。依据：空图是合法的中间状态（刚建出来、还没填），而「拒绝」会让每个调用方必须分两种情况处理；空图仍可遍历，只是遍历出 0 个节点——这与「有 3 个节点」在读数上不同，与「读不到」也不同。所以不存在「无 payload 该怎么办」的分叉：视图照常答 `kindOf`（没有节点可问），`nodes()` 交出 0 个。六处遍历**因此可以迁移**，但迁移是另一次改动（本轮未做）。

### 8. `name()`

**① 位置**：`onnx/exporter.cpp:4824`（`checkArity`）、`:4885`（`checkExamples`）、`:733`（`unsupported`）；`tiles/src/graph_regions.cpp:724`（`segment.graph->graph()->name()`，报告里印图名）、`tiles/src/placement.cpp` 的放置报告同形。

**② 缺的能力**：视图上的图名字（上一版写作 `Graph::name()`）。

**③ 今天为何需要**：诊断要说清是**哪个函数**——`checkArity` 报「'foo' takes 2 arguments but 1 example was given」，`unsupported` 报「in 'foo$tspec0': ...」（后缀是特化标记，要削掉），报告里按图名分行。

**④ 三分**：**可以**（本轮能力补上了，`C-1123-59` 第 ③ 项）。

`origin/develop` 的 `GraphView` 已有 `virtual std::string_view name() const`，落法是把这三处收参数的形参从 `GCGraph *` 换成 `const GraphView &`，把 `graph->name()` 换成 `g.name()`；`name()` 对草稿答空（`graph_view.h` 的注释写明了），而这三处拿到的都是冻结图，空值也不会出现。旁证：这条最早由 `internal/questions/2026-11-23-does-a-graph-view-name-the-graph.cn.md` 提出（`lane/view-surface`，建议「先只答冻结图」）；本页补的是它的**第二、第三个消费者**——`onnx` 的诊断与 `tiles` 的报告，二者都不是「跳过规则那一侧」那种用法，而是**必须把名字印出来**。

---

## 二、三处「集合无法保证」——视图与旧图语义不等价的证据

这一节不是「还没做完的活」，是**三处证明**：把「按编号走」换成「按视图迭代」时，视图枚举出来的节点**不是**旧图枚举出来的那一批。三处都**没有改**，理由写在每一条里。

### E1. `onnx/passes/export.cpp:394` `graphOriginOf`

**① 位置**：`graphOriginOf`（`for (auto it = g->nodes()...)`，取第一个 `origin != kInvalidOriginId` 的节点）。

**② 不等价在哪**：旧 `GCGraph::nodes()` **只跳 `GCNodeKind::Dead`**（`graph.h:1213-1219`），JOIN 与 DREF 都会交出来；视图的 `nodes()` 跳 Dead、JOIN、DREF 三者（`graph_view.h:324-331` 明说不许把 JOIN 放进 `nodes()`）。所以「第一个带 origin 的节点」可能是旧图的 JOIN、而在视图上根本不被枚举。

**③ 今天为何需要它**：拿不到图级 origin 时，用「第一个有源位置的节点」当诊断位置。

**④ 三分**：**不能**（不靠新方法解决）。要等价只能在**编号**上走（`nodeSlotCount()` + 旧 `nodes()` 的语义），而编号在视图上正是被去掉的那一半。若作者认为「诊断位置允许落到 JOIN 的源位置之外」，那这条可以改成视图迭代并**明说读数的差**；今天按「证不出相同」停手。

### E2. `onnx/passes/export.cpp:272` `runtimeBranchOver`

**① 位置**：`runtimeBranchOver` 对每个 BRCH 的每条臂调 `camel::execute::collectRuntimeBranchArmRegion(g, brch, arm)`，取 `topoIndices` 的 **size** 挑**最内层**的臂。

**② 不等价在哪**：成员判断可以等价（`isAncestorOf(arm, regionOf(ref))`），但 **size 的择优不可**：`collectRuntimeBranchArmRegion` 交的是该臂的拓扑序（含嵌套区域的影响），`regionNodes(arm).size()` 是另一回事，两者在嵌套臂上会给出不同的「最小」。

**③ 今天为何需要它**：分支条件是不是常量（能不能在导出期定臂），取决于哪个臂真正包住这个条件节点。

**④ 三分**：**部分**。能替的一半：找到「这个 BRCH 的哪条臂包住节点」= `isAncestorOf(arm, regionOf(node))`；不能替的一半：`size` 的选择键（拓扑序大小）视图没有。缺的方法：一条「区域里节点的拓扑序/传递大小」的查询，或 `collectRuntimeBranchArmRegion` 收到 `const GraphView &`。

### E3. `tiles/src/region_extract.cpp:41` `loopInternals`

**① 位置**：`loopInternals`：按 `for (ref = 0; ref < source.nodeCount(); ++ref)` 枚举，再按 `nodeRegion(ref)` 沿 `region->parent` 上溯，判定是否落在 LOOP 的条件/体区域。

**② 不等价在哪**：`nodeCount()` 枚举**含 JOIN/DREF**；视图的 `nodes()` 不含。若某个 loop 区域里恰好有 JOIN（或将来有），旧代码把它收进 `nodes`（并在 `copyLoopNodes` 的 `require` 上炸），视图则根本不枚举它。**「视图上同一个集合」证不出**——这取决于「loop 区域里能不能有 JOIN」，而这不是视图承诺过的事。

**③ 今天为何需要它**：把 LOOP 的条件/体区域里的节点整批复制进新草稿。

**④ 三分**：**不能**。`regionsOwnedBy` + `regionOf` + `isAncestorOf` 能表达「哪些节点在区域内」，但只能在视图枚举出的节点上问；旧代码枚举的**是编号空间**。两者相等当且仅当「区域里没有 JOIN/DREF」，而这正是需要证明、今天证不出的那一句。

**这一条是本页措辞上最要紧的一条**：它说的是「**证不出相同**」，不是「看起来相同」。三处里只有它同时踩了两件事——按编号枚举、且集合依赖于 JOIN 是否占位。

---

## 三、谁认领了，谁没认领

`C-1123-59` 记的是**四项**视图能力（`internal/backlog.md`），`D-1123-96` 把它们派给 `lane/graph-view-doc-caps`，读数已交。四项与本页六条的对照：

| `C-1123-59` 的四项 | 本页哪一条 | 结果 |
|---|---|---|
| ③ 图的 `name()` | 第 8 条 | **可以**（已落地） |
| ④ `dataIndex` 索引本身 → `staticIndexOf`/`staticSlotCount` | 第 2 条的一半 | **可以**（已落地） |
| ① 「是不是真正的 OPER 记录」 | 无（本页用 `operatorOf != kNoOper` 已经表达） | 与本页无关 |
| ② `kernelArgumentsOf` | 无（`onnx`/`tiles` 都不读算子的实参表） | 与本页无关 |

**所以六条 `不能` 里只有一条被这四项覆盖。剩下四条，今天没有认领者：**

- 第 1 条：`attrsOf(NodeRef)` 与 `RegionRecord::attrs` —— **本车道主诉，覆盖 `tiles` 的 74 处里绝大部分**；
- 第 4 条：`Program` 侧收 `const Graph *`（`graphs()`/`calleeOf`）、以及草稿的编辑入口（写侧，属 3b）；
- 第 5 条：`Context::programOf(const graph::Graph *)`；
- 第 6 条：控制输入那条顺序（`predecessors(ref, EdgeKind::Wait)` 或一条「谁先于谁」的查询）。

第 7 条（`hasPayload()`）也已裁定怎么答（空图，不是拒绝），但它仍是一个**待加的方法**，同样没有认领者。

**这一节是这张清单最要紧的部分**：只说「不能」，这张页会读成「做不到」；说清「没人认领」，它才是**派工的输入**。

---

## 四、给视图车道的一句话

八条里，1、4、5、7 是**加方法**（`attrsOf`/`RegionRecord::attrs`、`Program` 侧收 `const Graph *`、`programOf(const Graph *)`、`hasPayload()`）；2、3、6 是**加读法**（payload/原始 kind 的复制入口、反向使用索引与全表边、控制顺序）。第 8 条已经从这张清单里结清（`name()` 已答）。第 7 条那一裁（空图不是拒绝）只解决了「怎么答」，没有解决「谁加、谁迁」。