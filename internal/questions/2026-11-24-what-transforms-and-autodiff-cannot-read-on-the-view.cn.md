# 视图答不出什么：`transforms` 与 `autodiff` 停下来的 28 处

2026-11-24 ｜ 来自 `lane/3a-transforms-autodiff`（阶段 3a，目录 `src/stdlib/transforms`、`src/stdlib/autodiff`）｜ 对象：`kernel/graph/graph_view.h`

## 这一页是什么

这一页记的是 **`GraphView` 的缺口**，不是这两个目录欠的活。作者的原话：「读不了视图的（需要视图没有的能力）不硬改，列成清单交回，那是视图的缺口。」本车道此前没有页，缺口只埋在 `9980a9c1c4` 的提交正文里；这一页是那条正文的完整版，并且按**能力**归类，不按用例罗列。

判据（作者 `D-1123-127` / `D-1123-130`）与两次读数，命令原文：

```
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- src/stdlib/transforms
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- src/stdlib/autodiff
```

| 目录 | 本车道起点 `c0d640b313` | 本轮之后 | 差 |
| --- | --- | --- | --- |
| `src/stdlib/transforms` | **118** | **112** | −6 |
| `src/stdlib/autodiff` | **40** | **40** | 0 |

`develop`（`9cbf544ebc`）上是 124 / 41；118 / 40 是上一轮 `9980a9c1c4` 之后本车道的读数。

**`autodiff` 一行没降，而且在这个目录里一行也降不了** —— 理由见第四节，那是本页最要紧的一条。`transforms` 降的 6 行里，3 行是签名（`refuseAt` 两处、`regionClosure` 一处），1 行是 `jvp.cpp` 里一句仍然指着一个已迁走的读的注释，2 行是另外两句同类注释。**没有一行是靠删注释凑出来的**：每一句改过的注释，它指的那次读在同一棵提交里已经搬到视图上了。

本页的行号是**本轮提交之后的读数**；`c0d640b313` 只是本页开头那张表的起点。

---

## 一、先把计数拆开：152 行里只有 28 行是 A/B/C

判据数的是**行**，但那 152 行里绝大多数不是「读」，是**身份**：一个图被装进结构体、被返回、被当 map 的键、被 `const_cast` 交出去。视图是**身份之上的门面**，它答不出「这个图是谁」，也不该答。这一轮把 152 行逐行归了类，一张表：

| 类 | `transforms` | `autodiff` | 是什么 |
| --- | --- | --- | --- |
| **A** 属性 / 权限 / 区域 tag set | 0 | 0 | 见第二节：这一类在本车道**一行都不占** |
| **B** 载荷字节 / 原始 kind / `dataIndex`+`staticSlots` / 帧布局 | 19 | 0 | 第三节 |
| **C** 控制与顺序边 / 反向 `uses()` / 按边种类全表（含「编号当序」） | 9 | 0 | 第四节 |
| **P** 程序侧：`Program::graph(GraphId)`、`calleeIdOf`、`ruleGraphs()` | 13 | 4 | 第五节 |
| **W** 写侧：正在被写的草稿 | 13 | 0 | 第五节（3b） |
| **I** 图的身份：被存、被返回、被当键、被 `const_cast` | 45 | 36 | 第五节（3c） |
| 注释与图级读数的引用（`liftedFrom`、`closureType`、`callTangentArgsOf`…，其中 1 行是代码） | 13 | 0 | 第五节 |
| 合计 | **112** | **40** | |

所以判据「往 0 走」在这两个目录里的天花板不是 0。**B 19 + C 9 = 28 行是视图今天能靠补 A/B/C 拿掉的；剩下 84 行（transforms）和 40 行（autodiff）要的是 3b 的写侧入口和 3c 的身份合一。**

按**处**数（一处是一个读者位置，与行数无关）：「28 处」= A 2 + B 18 + C 8。B 的 18 处里只有 14 处占计数里的行，另外 4 处拿着局部引用（上一轮这一格是 22 处 / 8 处，少掉的 4 处是静态常量那一家，见第六节（二））；C 的 8 处里只有 4 处占行。**所以「行」和「处」在这里是两个数，判据只数行 —— 这也是为什么判据降到 0 不等于读者都上了视图。**

---

## 二、A 类：这 2 处读者，一行都不在计数里

A 是 `onnx`/`tiles` 的主诉（`attrsOf(NodeRef)`、`RegionRecord::attrs`、节点权限），在那里它让 `tiles` 的 74 处一处没动。**在这两个目录里它只有 2 个消费者，而这两个消费者都不按名字提 `GCGraph`/`GraphDraft`** —— 它们拿着一个 `GraphDraft &` 的**局部引用**：

- `src/stdlib/autodiff/pullback.cpp:1246`（`markFusionGroup`）：把一条属性并到目标节点已有的属性上，`camel::core::attr::unite(target().nodeAttrs(node), attrs)`。要的是 `attrsOf(NodeRef) -> AttrSetId`。
- `src/stdlib/autodiff/pullback.cpp:4352`（把前向节点交给一个算子规则）：`.attrs = forward().nodeAttrs(node)`。同一问。（紧邻的 `:4351` 读的是 `nodeOrigin`，那一半视图已经答了 —— 就是本轮 `refuseAt` 换上的 `originOf`。）

`transforms` 一侧对属性只有**写**（`init.attrs = header->attrs`，`jvp.cpp:2684`、`split.cpp:1378`、`transpose/walk.cpp:2232`），而那是草稿自己的 header 字段，不是图的属性列。区域 tag set 在两个目录里**没有消费者**。

**所以：视图车道补 A 不会让这两个数字动一格。** 这不是说 A 不重要，是说它在这两个目录里没有杠杆 —— 把补 A 的力气花在 B 和 C 上，或者花在 `tiles`/`onnx` 上。

---

## 三、B 类：19 行，是这里唯一的真缺口大宗

**① 计数里的 19 行，14 处位置**（下面每一行都会因为 B 补齐而从 112 里消失）：

| 位置 | 读的是什么 | 为什么视图今天不行 |
| --- | --- | --- |
| `jvp.cpp:69` `nodeOf` | 返回 `const rt::GCNode *` | 视图不交记录（`graph_view.h`：「no payload bytes leave this face」） |
| `jvp.cpp:80` `nodeBodyOrNull` | 返回 `const Body *` | 同上 |
| `split.cpp:27` / `:31` / `:45` | `GCBrchBody::join`：哪条分支的 Join 是这个 | JOIN 在视图里不存在（`graph_view.h` 文件头第五条），两个配对的载荷一起被删 |
| `split.cpp:118` `ungated` | 把一条 GATE 链走到底 | `camel/runtime/graph.h#valueBehindGates` 是**模板**，约束要求 reader 交出 `const GCNode *` 才读得到 `kind`。视图够不上这个约束 |
| `split.cpp:299` `exitHalvesOf` | `withInputsOf`（已换：`inputs(ref, Context)`，两处）+ `node()->dataType()`（已换：`typeOf`）+ `ungated`（没有） | 只剩 `ungated` 一处：它被 `graph.h#valueBehindGates` 的模板约束连坐，不是这个函数自己的读 |
| `split.cpp:432` `pairTangentReadOf` | `nodeOperatorOf`、`memberReadOf` | 载荷 |
| `split.cpp:479` `residualsOf` | `ungated` + `memberReadOf` | 载荷 |
| `split.cpp:1660` `loopBehindExit` | `memberReadOf`、原始 `kind == Loop` | 载荷 |
| `split.cpp:1722` `exitShapeOf` | `fillPayloadOf`、原始 `Join` | 载荷 |
| `split.cpp:1801` `answersAPair` + `split.h:164` | 经 `exitShapeOf`/`ungated` | 同上 |
| `split_internal.h:1059` + `split_loop_cut.cpp:99` `readLoopCut` | `memberReadOf`、`region->result` | 载荷 |
| `module.cpp:103` `shapeOf` | `node->withBegin - node->inBegin` = 一个节点占几个 norm 输入，以及 `std::to_string((int)node->kind)` | **帧布局**（`inBegin`/`withBegin` 是输入表的偏移）和原始 kind 的数字 |
| `node_operator.h:279` `nodeCaseOf` | 记录的 `kind`/`dataIndex` | 原始 kind |
| `node_operator.h:19`、`:333`（注释） | 同一个记录 | 同上 |

**② 不在计数里的 4 处 B 位置**（同样读不动，只是它们拿着局部引用，不按名字提类型）：

- `src/stdlib/autodiff/pullback.cpp:925-949` `operAccessPlaceOf` —— `payloadOf(node)` 再按 `GCOperBody` 解出 URI，然后才 `dataIndex`。
- `src/stdlib/transforms/split.cpp:1372`、`src/stdlib/transforms/jvp.cpp:2676-2678`、`src/stdlib/transforms/transpose/walk.cpp:2226` —— 把一个 JOIN 的 header 原样搬进新草稿（`init.dataIndex = header->dataIndex; init.kind = GCNodeKind::Join; …`）。三处都是**写侧**，且要 `Join` 这个 kind。

同一个静态常量读，上一轮还是这里的四条（`rule_expand.cpp:1170-1179`、`vmap.cpp:594-597`、`jvp.cpp:1562-1568`、`transpose/walkmachinery.cpp:317-330`），本轮四个都换上了视图，所以这一格从 8 处降到 4 处 —— 经过见第六节（二）。

**③ 静态常量那一族：五处，全部换完了。** 这五处（`rule_expand.cpp:1170`、`vmap.cpp:594`、`jvp.cpp:1562`、`walkmachinery.cpp:317`、`pullback.cpp:264`）问的是「这个节点是不是常量、值是什么、类型是什么」，而 `C-1123-59` 落进 `origin/develop` 的 `staticIndexOf`/`staticSlotCount`/`constantOf` 正好答这三问。`pullback.cpp#staticValue` 在上一轮换掉：

```cpp
std::optional<slot_t> staticValue(const rt::GraphTransaction &transaction, gc_node_ref_t node) {
    return transaction.graph().view().constantOf(node);
}
```

其余四处本轮换掉，形状相同，都是「先问 `constantOf`，再 `typeOf`」，于是 `dataIndex`、`staticSlots()`、`node->dataType()` 三个读一起消失：

```cpp
// rule_expand.cpp（同一个交换，返回条件是「不是这张图的常量」）
const std::optional<slot_t> constant = body->view().constantOf(ref);
if (!constant.has_value()) { /* 同一句 refusal */ }
copied[ref] = draft.materializeStaticValue(
    *constant, const_cast<camel::core::type::Type *>(body->view().typeOf(ref)),
    rt::kGCNodeFlagConstant);
```

`const_cast` 不是这里放宽了什么：`materializeStaticValue` 的参数是 `Type *`，而 `typeOf` 答 `const Type *` —— 类型是 interned 的，`dataType()` 交出来的也是同一个对象，这条转换在本仓库里是既有写法（`split.cpp:1145` 的 `liftedMemberOf(const_cast<Type *>(lifted->typeAt(index)))`）。

**换它们不改判据**（它们拿的是局部引用，不按名字提类型），改的是「读者在不在视图上」。`constantOf` 的合格条件与旧代码逐字相同（`FrozenGraphAdapter::constantOf`：有记录、kind 是 `Data`、`dataIndex < 0`、下标在静态区里），所以这不是放宽 —— 旧代码那三个条件正是它自己重抄了一遍。

---

## 四、C 类：8 处，两种形状

**①「编号当序」（4 处，占了计数里 7 行 + 2 行注释）**

| 位置 | 为什么还在编号上走 |
| --- | --- |
| `node_order.cpp:41` `dependencyOrderOf`（+`node_order.h:51`、`node_order.cpp:18` 的 using） | Kahn 走 `0..nodeCount()`，因为 **JOIN 是序里的一项**：它是分支的合并点，下游要复制它；`nodes()` 会把它丢掉（`C-1123-17`）。同样地 `contains()` 把 Dead 也丢了，而这一遍要的正是「记录在、但视图没有」 |
| `node_order.cpp:116` + `node_order.h:73` `reverseDependencyOrderOf` | 就是上一条反过来 |
| `split.cpp:72` `walkRegion` | 按编号遍历一个区域：`nodeRegion(ref)` 对 JOIN/占位符也答（`gc_graph_view.cpp:340` 的 `regionOf` 注释说了为什么必须答），而 `regionNodes()` 只交活节点。成员数、`outside` 的算法都建立在这个全量上 |
| `transpose/walk.cpp:145` `cotangentWalkOrder` | 走的就是 `reverseDependencyOrderOf` 那条序，同一个原因 |

**缺的那个能力**：一条「这个位置是不是一条记录」的查询，**包含 JOIN 与占位符、排除 tombstone**。`contains()` 三个都排除，`kindOf()` 对 Dead 也答 `Data`，两个都不是。再往上，是一条**含 JOIN 的节点枚举**。`node_order.h:26` 那行注释（`GraphDraft::keepsNodeIds`）也是同一件事的第三面：**视图不说自己的编号是不是草稿重编过的**。

**②「按边种类全表」（4 处，计数里 0 行）**

- `src/stdlib/transforms/split_internal.h:381-382`：`!source->ctrlInputsOf(ref).empty() || !source->orderPredsOf(ref).empty() || !source->syncPredsOf(ref).empty()` —— 拒绝任何站在效果边上的节点。视图有 `predecessors(ref, Sync)`，**没有 ctrl，也没有 order**（`graph_view.h` 末尾：「the successors of an order edge and of a sync edge … arrive as additions」）。
- `src/stdlib/autodiff/pullback.cpp:543` `hasOrdering`：`ctrlInputsOf` + `syncInputsOf`。
- `src/stdlib/autodiff/pullback.cpp:585` `pruneUnreachable`：同两个读。
- `src/stdlib/autodiff/pullback.cpp:1000` `orderingOf`：同两个读，再加 `forwardedInputOf`。

这四处**一行都不在判据里**（它们拿的是 `draft.` / `source->` 的局部引用），所以 C 类看起来只有 9 行；实际上 C 挡住的读者比 B 少一半、但挡住的位置一个不少。

---

## 五、剩下 124 行：不是 A/B/C，视图也不该答

**① 身份 `I`（transforms 45 行 / autodiff 36 行）** —— 一个 `GCGraph *` 被装进结构体、被返回、被当 `unordered_map` 的键、被 `const_cast`，或者被拿去当另一个构造的入参。视图是身份**之上**的门面：它答「图里有什么」，不答「这是哪张图」。这一半要的是 3c（两个表示合一、`GCGraph` 与 `GraphDraft` 一起消失），不是补方法。

这一类里最大的一坨在 `autodiff`：`Engine` 的 `plans_` / `closurePlans_` / `armOwners_` / `recursions_` / `implBodies_` 全是按图指针索引的 map（`pullback.cpp:1463-1478`），`PullbackPlan` 里存着 `GCGraph *graph`，`gradient` / `planFor` / `planForClosure` 收和还的都只是指针。**把 `GCGraph` 换成 `graph::Graph` 会改类型名、不改判据**（判据认得 `GCGraph`，不认得 `Graph`）—— 所以那也不是本车道的路。

**② 写侧 `W`（13 行）** —— `fillTuple`、`staticZeroOf`、`zeroOfSpaceOf`、`statedExtentsNodeOf`、`buildLinearClosure`：参数是 `GraphDraft &`，函数体在往里加节点、写区域。3b 的编辑入口（`GraphTransaction::edit()`）的事。

**③ 程序侧 `P`（transforms 13 行 / autodiff 4 行）** —— `calleeGraphOf` 一族（`split.cpp:349`、`vmap.cpp:186`、`split_internal.h:106`）走 `graph.calleeIdOf(ref)` 再 `program->graph(id)`；`rules.cpp:15`/`:19` 读 `graph->ruleGraphs()` 再 `program->graph(id)`。视图的 `calleeOf` 已经答得更好（`CalleeRef{Kind::Value, graph}`，`gc_graph_view.cpp:408`），**但有个条件**：

> `GCGraph::view()` 造适配器时**没有传** `calleeGraphOf`（`gc_graph_view.cpp:40-45`），所以今天在 `view().calleeOf(ref)` 上问一个真的 FUNC 节点会 GUARD 失败。要回答这一问，调用方得自己造 `FrozenGraphAdapter(graph, resolver)`（`callee_graph_of.h`）。

这是**一个真缺口，而且是一条没人认领的缺口**：`Program` 侧收 `const Graph *`、或者 `view()` 自己带上解析器。补上它，`split.cpp` 的 `calleeGraphOf`/`answersAPairAt`/`tangentArgumentsOf`/`pairTangentReadOf` 四组签名（计数里 13 行）一起松绑 —— **这是本车道最大的一笔可回收，比 A/B/C 任何一类都大。**

**④ 注释与图级读数的引用 13 行**（12 行注释 + 1 行代码：`split.cpp:1894` 的 `addCapturePorts` 签名）—— `GCGraph::liftedFrom`（5 行：`lift.h:162`、`jvp_operator.cpp:194`、`transpose.h:312`、`transpose_operator.cpp:566`/`:619`：一个图级标志，视图不答）、`GCGraph::closureType`（3 行：`jvp.cpp:906`、`split.cpp:1904` 的注释和 `split.cpp:1894` 的签名 —— **闭包类型不在视图上**：视图说「captures 的类型就是 `ports(Capture)` 的类型」，但一个「布局只在函数值里」的图（就地写的 lambda）说的就是这个 NULL，视图表达不了那个区别，而从 `ports(Capture)` 推出的非空元组会把这段区别抹掉）、`GCGraph::closureNodes`（`split.cpp:1875`）、`GCGraph::callTangentArgsOf`（`split_internal.h:775`）、`nodeCount` 的编号（`node_operator.h:108`）、`GraphDraft::keepsNodeIds`（`node_order.h:26`，已算在 C 里）、`GCGraph` 拒跑一个没有 `with` 入参的转置 CALL（`transpose/walk.cpp:3227`）、一个图的「静态函数图」（`transpose/signature.cpp:295`）。

---

## 六、已换上的读，逐处对得上

### （一）`4809017f6c` 的 6 行

| 行 | 换成了什么 |
| --- | --- |
| `split.cpp:13-16` + `split_internal.h:75-79` `refuseAt` | 参数从 `const rt::GCGraph *source` 变成 `const GraphView &source`；`source->nodeCount()`→`nodeSlotCount()`、`source->nodeOrigin(ref)`→`originOf(ref)`（`FrozenGraphAdapter::originOf` 就是 `graph_.nodeOrigin(ref)`，一字节不差）。26 处调用点改成 `source->view()` / `half.source->view()` / `function->view()` |
| `split_loop_cut.cpp:54-57` `regionClosure` | 同上：`regionPorts`→`regionParameters`（就是 `graph_.regionPorts(id)`）、`nodeRegion`→`regionOf`（对 JOIN/占位符也答）、`node(input)==nullptr`→`regionOf(input)==kNoRegion`（**条件完全相同**：`regionOf` 在 `ref >= nodeSlotCount() \|\| node(stored)==nullptr` 时答 `kNoRegion`） |
| `jvp.cpp:870`、`:890`、`:905-908`（注释） | 三行不再提表示：`:870` 指的 `withPorts`、`:890` 指的 `closureNodes`、以及 `:905-908` 那段原本并列写的两个读数里的 `closureNodes` —— 三处指的读在同一棵提交里换成了 `ports(Context)`/`ports(Capture)`。同一段里的 `GCGraph::closureType` **留着**：视图答不了它（第三节 ③、第五节 ④） |

顺带换掉、**不改判据**的读（读者搬走了，数字没动）：

- `jvp.cpp:807`/`:853`/`:912-913` —— `withPorts()`/`normPorts()`/`closureNodes()` → `view().ports(Context|Normal|Capture)`。
- `vmap.cpp:276-278` —— `closureNodes()`/`normPorts()` → 同上两个。

### （二）本轮的五个读者（判据不动，仍是 112 / 40）

这四个静态常量读 + `exitHalvesOf` 的两处，全部在**局部引用**上，所以它们从来不在判据那 152 行里：换掉之后 `git grep -cE '\bGCGraph\b|\bGraphDraft\b'` 仍是 **112 / 40**。这是判据的已知盲区（第一节末尾）。**换完之后，这一页里再没有「视图已经答得出、只是还没换」的位置** —— 剩下的每一处都要视图先长出能力（A/B/C），或者要 3b/3c。

| 位置 | 旧读 | 新读 |
| --- | --- | --- |
| `rule_expand.cpp` `expandRuleRightSide` | `node->dataIndex`、`staticSlots[index]`、`node->dataType()` | `body->view().constantOf(ref)`、`typeOf(ref)`；那个 `staticSlots` 局部在换掉之后没有别的用途，一并删掉 |
| `vmap.cpp` `buildVmapGraph` 的 `Data` 臂 | 同上 | `function->view().constantOf(ref)`、`typeOf(ref)` |
| `jvp.cpp` `liftGraph` 的 `Data` 臂 | 同上，且每次把整张静态区**拷成 `std::vector<slot_t>`** | `function.view().constantOf(ref)`、`typeOf(ref)` —— 那份拷贝也一起没了 |
| `transpose/walkmachinery.cpp` `constantValueOf` | 同上 | `ctx.function->view().constantOf(ref)`、`typeOf(ref)` |
| `split.cpp` `exitHalvesOf` | `withInputsOf` ×2、`node(member)->dataType()` | `view().inputs(ref, PortGroup::Context)` ×2（`FrozenGraphAdapter::inputs` 的 `Context` 臂就是 `graph_.withInputsOf`）、`view().typeOf(member)` |

`constantOf` 的合格条件与旧代码那三个条件逐字相同（`FrozenGraphAdapter::constantOf`：有记录、`kind == Data`、`dataIndex < 0`、下标在静态区里），`typeOf` 就是 `node == nullptr ? nullptr : node->dataType()` —— 两处都不是「换了个更宽松的问法」，是把重抄的条件交还给视图。**残留的一处**：`exitHalvesOf` 仍按 `ungated` 收 `const rt::GCGraph &`（`valueBehindGates` 是模板，约束要求 `const GCNode *`），它所以还在判据里。

---

## 七、给视图车道的一句话

三句话，按价值排：

1. **`Program` 侧收 `const Graph *`（或者 `view()` 自带 callee 解析器）** —— 回收 `transforms` 的 13 行，是本车道最大的一笔，而且**今天没人认领**。
2. **`{节点类型, 数据输入}` 之外的三种边** —— `predecessors(ref, Ctrl)`、`predecessors(ref, Order)`，或一条 `predecessorsByKind`。回收 C 类 4 处读者的写法，不改判据。
3. **「这个位置是一条记录」** —— 含 JOIN、含占位符、排除 tombstone。回收 C 类那 4 处「编号当序」（计数里 7 行）。

B 类那 19 行**不要**指望视图加方法解决：`graph_view.h` 明说「no payload bytes leave this face」，而这里要的是**复制一段存储**（`rule_expand`/`jvp` 的常量复制、`transpose` 的 JOIN 写回）。那一半的等价物在 3b 的编辑入口。

A 类在这个目录里**没有杠杆**（第二节），补它不会让这两个数字动一格。