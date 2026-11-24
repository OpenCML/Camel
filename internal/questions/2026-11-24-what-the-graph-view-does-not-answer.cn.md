# 视图答不出什么：`onnx` 与 `tiles` 停下来的地方，按能力归类

2026-11-24 ｜ 来自 `lane/3a-onnx-tiles`（阶段 3a，目录 `src/stdlib/onnx`、`src/stdlib/tiles`）｜ 对象：`kernel/graph/graph_view.h`

## 0. 这一页是什么，读数是哪三个

这一页记的是 **`GraphView` 的缺口**，不是 `onnx`/`tiles` 欠的活。作者的原话：「读不了视图的（需要视图没有的能力）不硬改，列成清单交回，那是视图的缺口。」

判据命令（**路径是 `src/stdlib/onnx`、`src/stdlib/tiles`**。判据原文写的 `src/stdlib/std/onnx`、`src/stdlib/std/tiles` 在 `develop` 与本车道都不存在，按原文跑是 `exit 1`、无输出，见 §8）：

```
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- src/stdlib/onnx
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- src/stdlib/tiles
```

| 读数 | `onnx` | `tiles` |
|---|---|---|
| `develop`（判据的 84 / 74 就在这一侧） | **84** | **74** |
| 本车道本轮开工时（`14c791d1c6`） | 72 | 74 |
| **本轮改后** | **70** | **71** |

本轮动了 6 行（3 个函数），§8 逐条写清为什么是这三个、为什么不是别的。

**这一版把缺口按能力重新归了口：A / B / C 三样是 `lane/view-gaps` 正在补的，D 至 H 五样不是它补的、也没人认领。** 上一版按用例列了 8 条 11 处，读得出「哪儿不能改」，读不出「缺的是哪一样、谁在补、谁没补」；这一版换掉的是这个。

**本地行号一律是本车道本轮改后的读数**（`D:\Projects\Camel-wt\3a-onnx`，`lane/3a-onnx-tiles`）。

## 默认：这一侧希望视图车道先做哪一样

**默认建议：先做 A，其次 D。** 理由是可量：A 一条挡住 `tiles` 的 13 处直接消费，加下游 30 多行被它钉死的签名——它是 `tiles` 从 71 往下走的唯一门槛；B 与 C 挡的处数少，而且 B 那六处还压在 E/H 下面（§3），单补 `hasPayload()` 一行都掉不下来。D 只有 1 处、却挡住 `onnx` 的 6 行，而且它是三个已认领项之外**唯一还没人认领、又能被视图自己解决**的一条（E 视图答不了，F 属 3b）。若这一侧按默认开工，下一步就是拿着 §5 的那两句去问 D 该由谁加。

---

## 1. 总账

「位置」= 一处要该能力的地方（一个调用点，不是一个用例）。同一个能力被一个文件里的三个函数各要一次，就记 3 处。

| 类 | 缺的能力 | `onnx` | `tiles` | 本轮能迁吗 | 谁在补 |
|---|---|---|---|---|---|
| **A** | `attrsOf(NodeRef) -> AttrSetId`、区域的 tag set、节点权限 | 1 | 13 | **不能** | `lane/view-gaps`（在补） |
| **B** | `hasPayload()`，以及被折叠那四种 kind 自己的载荷字节 | 10 | 4 | **不能**（§3 说明：不是语义问题） | `lane/view-gaps`（在补，裁已下） |
| **C** | `predecessors(ref, EdgeKind::Wait)`：同一条臂/块里谁先于谁 | 2 | 5 | **不能** | `lane/view-gaps`（在补） |
| **D** | 静态区的**类型**（`staticDataType()->typeAt(i)`）与静态槽的整片读取 | 6 行悬在 1 处 | 1 | **不能** | **没人**（本页新提） |
| **E** | `Program` 侧收 `const Graph *`（`calleeOf`/`graphs()`/`programOf`/`collectReachableRuntimeGraphs`） | 最大一类 | 最大一类 | **不能** | **没人**（视图侧无解） |
| **F** | 写侧：`GraphDraft`/`GraphTransaction` 的编辑入口 | 0 | 12 | **不能** | 3b 的编辑原语 |
| **G** | 反向索引 `uses()`、全表 `syncEdges()` | 0 | 2 | **不能** | **没人** |
| **H** | 原始 16 种 kind、`GCNode::flags` | 若干 | 若干 | **部分** | **没人** |

**一句话答作者的问题：`tiles` 71 处几乎每一处都落在 A 上（13 处直接读标签/权限，其余落到它的下游），`onnx` 70 处的主干落在 E 上。A/B/C 三样补齐后，这两个数不会同时归零——D、E、F、G 各挡一段，而 E 那一段视图本身答不了。**

---

## 2. A — 节点与区域的属性（`lane/view-gaps` 在补）

**缺三样**：`attrsOf(NodeRef) -> AttrSetId`；`RegionRecord` 的 tag set（`regionAttrs(RegionId)`，或把这半截叫别的名字——`RegionRecord` 今天五个字段里没有它，注释明说 tag set「is a fact, not a place」）；节点权限（`effectivePermissions` 要的那张表）。

**14 处，按三种消费形状分：**

1. **`nodeAttrs(NodeRef)` 直接读一处节点的标签** —— 4 处：
   - `onnx/exporter.cpp:744`（`recordFates`，把属性集逐条过 fate 表，没有 fate 就拒绝）
   - `tiles/compiler.cpp:84`（`checkAttributeFates`，同形）
   - `tiles/placement.cpp:198`（`placementOf`：先问节点自己的 `tiles.placement`）
   - `tiles/placement.cpp:251`（`hasPlacementFact`：扫全图问有没有这条键）
2. **区域的 tag set，含沿 `region->parent` 上溯** —— 5 处，全在 `tiles/placement.cpp`：
   - `:42`（`bodyAttrs`，读区域 0 的标签）
   - `:48`（`factRegion`，沿 `region->parent` 找**最近一条带该键的区域标签**）
   - `:206`（`placementOf` 的区域内层）
   - `:246`（`hasPlacementFact` 扫区域）
   - `:295`（`placementReport` 按区域印 `region.attrs`）
3. **`core::tag::effectivePermissions(graph, id, policy)`** —— 5 处：
   - `tiles/compiler.cpp:801`、`tiles/graph_regions.cpp:233`、`:668`、`tiles/region_extract.cpp:81`、`:213`

第 3 种形状值得单说：`effectivePermissions` 是 `core/tags` 里的免费函数，**它自己就是一个读者**，形参是 `const GCGraph &`。所以 A 补齐不等于这 5 处能改——要么它收 `const GraphView &`，要么每个调用方自己把节点标签和上溯拼一遍（后者正是这条面要消除的东西）。

**为什么这一条是 `tiles` 的主因**：放置查询（`placementOf`/`bodyPlacementOf`/`factRegion`/`hasPlacementFact`）与属性 fate 检查，全部建在「写在节点或最近一层区域上的标签」上。标签不在视图上，这 13 处就一个也动不了，而它们又被 `graph_regions.cpp` 的 `eligibility`/`planResidency`/`analyzeRegions` 一排函数调用，于是那些函数也必须继续收 `const rt::GCGraph &`——**一个能力缺口把 30 多行的签名钉死**。

---

## 3. B — 载荷（`lane/view-gaps` 在补；裁已下，但这一轮迁不动）

作者已裁（2026-11-24，经协调者转达）：「没有任何节点的图是**空图**，不是**拒绝**」。所以不存在「无 payload 该怎么办」的分叉：视图照常答 `kindOf`，`nodes()` 交出 0 个。

**`hasNodePayload()` 那六处（`onnx/exporter.cpp:590`/`:1043`/`:1070`/`:1108`/`:1134`/`:5215`）本轮一处也没迁，理由是硬的，不是语义的：**

那六处不是六个孤立的闸门，是**六个图遍历的入口**，每个循环体里同时要：

- `program.calleeOf(*g, ref)`（E 类：`Program` 收 `const GCGraph *`）——六处里五处有；
- `(*it)->kind`，即 `GCNode` 记录本身（H 类）——`nodes()` 迭代器交的是 `const GCNode *`；
- 一处还要 `g->staticArea()`（D 类，`refuseEffectsOf` 的静态函数常量分支）。

`hasNodePayload()` 只是这些函数里**最浅**的一层。把闸门按裁定换成「遍历 0 个节点」之后，`GCGraph` 仍然在这一行出现（`Program::calleeOf` 收的就是它），**grep 不会掉一行**。所以这六处的正确记法是：**B 是表层的缺口，E/H 是底下的缺口，六处跟着 E/H 一起迁，不单独迁第二遍**。

另外两处 B 与六处形状不同：

- `tiles/region_extract.cpp:92`、`:224`：`source.nodePayloadBytes(ref)` —— 把算子的 payload **逐字节搬进新草稿**。`graph_view.h` 明说「no payload bytes leave this face」，这半截的等价物在 3b 的 `GraphTransaction::edit()`（F 类）。
- `GCFillBody::slots()` / `GCAccsBody`（6 处：`onnx/exporter.cpp:885`/`:891`/`:3114`、`onnx/passes/export.cpp:180`、`tiles/compiler.cpp:664`/`:675`）：视图把 FILL/ACCS/CAST/COPY 折进 `Oper`，而 `operatorOf` 对它们答 `kNoOper`——**被折叠那四种自己的载荷在视图上没有名字**。这是 B 类里唯一「加了 `hasPayload()` 也不够」的一半。

---

## 4. C — 控制/顺序边：`predecessors(ref, EdgeKind::Wait)`（`lane/view-gaps` 在补）

`graph_view.h` 的 `EdgeKind` 只有 `Data`/`Sync`/`Order`，文件头自己写了「This face has three edge kinds and no `Wait`」。而 `ctrlInputsOf` 问的正是**同一臂/同一块内语句的先后**。

**7 处读侧：**

- `onnx/exporter.cpp:561`（`runsBefore`：回溯走 `norm ∪ with ∪ ctrl`）——变量读写能不能导出，全押在这条顺序上；
- `onnx/exporter.cpp:665`（`refuseEffectsOf` 的同一形状并集）；
- `tiles/compiler.cpp:582`（`for (auto dep : g->ctrlInputsOf(id))`）；
- `tiles/graph_regions.cpp:162`、`:564`、`:589`（区域的 arm/顺序判定：`ctrlInputsOf` 与 `ctrlOutputsOf` 一起决定区域能不能成立）；
- `tiles/region_extract.cpp:108`（复制 loop 节点时把 ctrl 一组逐条重映射）。

`region_outline.cpp:190`/`:889`/`:902` 也读 `ctrlInputsOf`，但读的是**草稿**的（`draft.ctrlInputsOf`），属 F 类写侧，不算 C。

**这一条为什么必须由视图给规则**：`regionOf` 只答「同不同一个区域」，`predecessors(Order)` 答的是 order 边，都不是「谁先于谁」。各读者自己写一份，正是这条面要消除的东西。

---

## 5. D — 静态区（**不是** A/B/C，本页新提，没人认领）

上一版说「常量槽那一半已经不再是缺口」，**这一句只对了一半，本版改掉**。

视图有了 `staticIndexOf(ref)`、`staticSlotCount()`、`constantOf(ref)`——**值**这一半确实够了。但两处读者要的不是值，是**类型**：

- `onnx/passes/export.cpp:96` `constantOf` 自己：

  ```cpp
  const auto index     = static_cast<size_t>(-n->dataIndex);
  core::type::Type *ty = g->staticDataType()->typeAt(index);   // ← 视图没有这一句的替身
  return Example{g->staticArea()->get<slot_t>(index), ty ? ty : n->dataType()};
  ```

  它一个挡住 6 行：`:96`、`:138`、`:153`、`:159`、`:165`、`:216`（`constantArgAt`/`constantArg`/`constantWithArg`/`readFunction`/`readRequest` 全部建在它上面）。

- `tiles/region_extract.cpp:29` `typeOf`：`if (auto *type = node->dataType()) return type;` 之后有一支 `staticDataType()->typeAt(-dataIndex)`。

**为什么视图答不了**：视图的 `typeOf`（`gc_graph_view.cpp:176-179`）只答 `node->dataType()`，也就是记录自己的 `typeId`；**记录里没有类型的那种节点，视图答 `nullptr`**，而上面两支正是为它们写的。静态区的形状（`TupleType *staticDataType()`，`typeAt(index)`）在视图上没有入口——`staticSlotCount()` 只给个数。

**这一条我留了一个读数没取**：那种「记录里没有类型、类型只在静态区」的 DATA 节点今天到底有没有、有多少，本页没有测。所以 D 的措辞是「视图没有这个入口，而代码里有一支为它存在」，不是「一定在跑」。要么视图加一条「静态槽 `i` 的类型」，要么把 `constantOf` 的定义搬进适配器（它本来就持有静态区）；两条都还没人认领。

---

## 6. E / F / G / H — 也不是 A/B/C

### E. `Program` 侧收 `const Graph *` —— 行数最大的一类，视图侧无解

`Program::calleeOf(const GCGraph *, ref)`、`Context::programOfRuntimeGraph(const GCGraph *)`、`RuntimeGraphDraftSession::collectReachableRuntimeGraphs()` 全收表示。视图不答「谁持有我」——`CalleeRef::graph` 只答「我调到谁」。

`onnx` 侧：`programOf`(`:186`)、`Evaluator::programOf`(`:317`)、`callReaches`(`:1037`)、`findTreeRecursion`(`:1063`)、`findRecursiveCall`(`:1101`)、`reachableStats`(`:1127`)、`refuseUnendedUnrolling`(`:5184`)、`passes/export.cpp` 的 `programOfGraph`(`:359`)/`calleeGraphOf`(`:368`)/`calleeNameOf`(`:384`)/`requestIsReachable`(`:452`)。
`tiles` 侧：`placement.cpp` 的 `functionGraphOf`/`calleeGraphOf`/`GpuScopes`（3 处）、`graph_regions.cpp` 的 `passesOrdinaryArgument`/`eligibility`、`compiler.cpp` 的 `Frame`/`active`/`const_cast`、`region_outline.cpp` 的 `collectReachableRuntimeGraphs` 两处。

**这一类我没做逐行归属**（要读 30 多个函数体才能定），所以只报机制与位置清单，不报行数——不比把估数写成实测。

### F. 写侧（3b 的 `GraphTransaction::edit()`）

`tiles/graph_values.h:14`/`:18`、`graph_values.cpp:69`/`:100`、`region_extract.cpp:67`/`:119`、`region_outline.cpp:105`/`:152`/`:172`/`:202`/`:218`、加 `draft.ctrlInputsOf` 三处。12 行悬在这一个入口上：抽取的产物是草稿，草稿不是只读面。

### G. 反向索引与全表边

`graph_regions.cpp:129` 的 `chainMembers` 要 `syncEdges()` 全表建并查集；`region_outline.cpp:743` 要 `graph->uses().norm(input)`。视图只答 `users()`（正向、按组），要自己建反向表就失去索引意义；`syncEdges()` 全表改成「遍历 `nodes()` 再问每个节点的 sync 前驱」会漏 JOIN/DREF 上的边（`contains` 对它们答 false）。

### H. 原始 kind 与 `flags`

视图的九种 kind 把 CAST/COPY/FILL/ACCS 折进 `Oper`，把 JOIN 折进 `Brch`，DREF/DEAD 不出现。`tiles/region_extract.cpp:73-76`、`:89` 要按 `Cast`/`Fill`/`Accs` 分支、要 `node->flags`——这一半是 3b 的编辑入口，**不该**由只读面加方法。`onnx` 侧受它影响的是 `nodes()` 迭代器交 `const GCNode *` 的六处（§3）。

---

## 7. 三处「集合无法保证」，仍是证据，不是活

这一节不是缺口清单，是**三处证明**：把「按编号走」换成「按视图迭代」时，视图枚举出的节点**不是**旧图枚举的那一批。三处都没改。

- **E1. `onnx/passes/export.cpp:394` `graphOriginOf`。** 旧 `GCGraph::nodes()` 只跳 `Dead`，视图 `nodes()` 跳 Dead/JOIN/DREF，所以「第一个带 origin 的节点」可能是旧图的 JOIN。**本版新发现第二个理由**：它先查 `source->debugMap().graphOrigin(g->id())`，而 `GraphId` 正是视图明说不答的东西（`graph_view.h` 的 `CalleeRef` 注释）。所以这一处**两重**不可替。
- **E2. `onnx/passes/export.cpp:272` `runtimeBranchOver`。** 成员判断可等价（`isAncestorOf`），但**择优键不可**：它比 `collectRuntimeBranchArmRegion` 交的拓扑序大小，`regionNodes(arm).size()` 是另一回事。
- **E3. `tiles/src/region_extract.cpp:41` `loopInternals`。** 旧代码按 `nodeCount()` 枚举**编号空间**（含 JOIN/DREF），视图只枚举节点。两者相等当且仅当「loop 区域里没有 JOIN/DREF」，而这正是证不出的那一句——**「证不出相同」不等于「看起来相同」**。

---

## 8. 本轮动了什么，为什么是这三个

6 行，3 个函数，判据从「72 / 74」到「70 / 71」：

| 位置 | 改法 | 为什么等价 |
|---|---|---|
| `onnx/exporter.cpp:4826` `checkArity` | 收 `const GraphView &`；`withPorts()/normPorts()` → `ports(Context)/ports(Normal)`，`name()` → `name()` | 端口三组就是 `ports` 的三个组；`name()` 对冻结图答同一个串 |
| `onnx/exporter.cpp:4889` `checkExamples` | 收 `const GraphView &`；`funcType()`/`name()` | 两个入口都在视图上 |
| `tiles/graph_regions.cpp:455` `parameterIsDevice` | 收 `const GraphView &`；`node(port)->dataType()` → `typeOf(port)` | `typeOf` 答的就是记录的 `dataType()` |
| `tiles/graph_regions.cpp:475` `returnsToHost` | 收 `const GraphView &`；`funcType()` | 同 |
| `tiles/region_outline.cpp:270` `standingIn` | 收 `const GraphView &`；`regionCount()`/`region(id)->owner` | `RegionRecord::owner` 就是 `GCRegion::owner`；视图的 `region(id)` 对不存在的 id 答默认记录而非 `nullptr`，所以**先问边界**再取 `owner`，与旧代码 `region < regionCount() ? ... : nullptr` 同一判定 |

**为什么只动这三个**：`tiles` 剩下的 71 行里，签名级的（`placement.h:9`、`regions.h:8`、`residency.h:1`、`graph_values.h:2`）是**被函数体钉住的**——函数体里一句 `nodeAttrs` 或 `nodeBodyAs` 就让签名必须留着 `const GCGraph &`，改签名不改体编译不过、改了体又没有能力，这正是 A/B/H 的价值。`onnx` 剩下 70 行里没有第三个同形的短函数：`constantOf` 撞 D、`runsBefore` 撞 C、六个闸门撞 E/H、`programOf` 撞 E。

**判据命令的路径**：判据原文的两条写的是 `src/stdlib/std/onnx`、`src/stdlib/std/tiles`；这两个目录在 `develop`（`git ls-tree develop src/stdlib/std/` 只有 builtin/cml/cpp/cxx/exec/fvm/gc/gir/macro/nop/nvm/prl/profile/rulesets/selftest/stats/tns）和本车道都不存在，按原文跑是 `exit 1`、无输出。本页与交回一律用 `src/stdlib/onnx`、`src/stdlib/tiles`，数值 84/74 是对得上的（`develop` 侧 84 与 74 精确重现）。

---

## 9. 谁认领了，谁没认领

| 类 | 认领者 | 状态 |
|---|---|---|
| A（`attrsOf` + 区域 tag set + 节点权限） | `lane/view-gaps` | 在补，**未落地**（该分支尚不领先 `develop`） |
| B（`hasPayload()`） | `lane/view-gaps` | 在补；裁已下（空图不是拒绝） |
| C（`predecessors(ref, EdgeKind::Wait)`） | `lane/view-gaps` | 在补 |
| **D（静态区的类型 / 整片读取）** | **没有** | 本页新提 |
| **E（`Program` 侧收 `const Graph *`）** | **没有** | 视图侧无解，是 `Program` 的活 |
| **F（写侧编辑入口）** | 3b | 属阶段 3b |
| **G（`uses()` 反向、`syncEdges()` 全表）** | **没有** | |
| **H（原始 kind、`flags`）** | 3b | 区域抽取的存储复制 |

**这一节是这张清单最要紧的部分**：只说「不能」，这张页会读成「做不到」；说清「缺哪一类、谁在补、谁没补」，它才是**派工的输入**。A/B/C 补齐之后 `tiles` 会掉一大块、`onnx` 的 70 不会归零——D/E 各挡一段，而 E 视图本身答不了。

---

## 10. 本轮读数：取过什么，没取什么

**取过的**：`feat/modules/onnx` ＋ `feat/modules/tiles` 两个计划，**在同一台构建槽上跑了两次**——一次在本轮改动上，一次把三个源文件退回 `HEAD`——268 例按**名字与判决逐条比对**（`Compare-Object` 269 行，无差异）。两次都是 pass 233 / fail 27 / skip 6 / known 2（含 1 例超时），其中 `onnx` 13 红、`tiles` 14 红，与上一轮记录的两份分布（13 / 14）一致；那 27 红是 `develop` 自己的，改前改后同一批。两次构建的 footprint 都是 `7580f3c78f7c`，即同一份源码。

**没取过的，三处，明说**：

- 第 1 版 `feat/modules/onnx` 的红里有 1 例超时（`resnetbn_train_step_export`，300 s 上限），本轮没有追它是否与 `develop` 的记录一致——它在改前改后两次里都红且都超时，判决没动，超时的**原因**没查。
- D 类「记录里没有类型、类型只在静态区的 DATA 节点今天有没有」没测（§5）。
- E 类没有逐行归属，只给位置清单（§6）。
## 决策者答（2026-11-24）

默认同意：`lane/view-gaps` 按 A → B → C 做（已派），D 接在 C 之后进同一条车道。其余各类的去处：

- **D 是视图的正确性缺口，不只是少一个入口**：一个节点的类型只有一个答案，视图的 `typeOf` 对"类型在静态区"的节点答 `nullptr` 就是答错了。修法是 `typeOf` 自己把静态区那一支收进来（读者不再各写一支 `staticDataType()->typeAt(-dataIndex)`），先补上本页没取的那个读数：这种节点今天有多少（为 0 也要写出来，那时这两支是死代码，删掉）。
- **G 就是 C 的一部分**（反向 `uses`、按边种类的全表），归 `lane/view-gaps`，不另开。
- **F 是 3b**（写图经编辑会话的入口），不进视图车道；等 3b 的 `GraphTransaction::edit()` 落地后，这 12 行随 3b 迁。
- **E 不是视图的缺口，是 `Program`/`Context` 的接口按表示作答**：`calleeOf`、`programOfRuntimeGraph`、`collectReachableRuntimeGraphs` 收 `const GCGraph *`。它们该按图的身份作答（视图已有的 `CalleeRef::graph` 那种句柄），读者给句柄、不给表示指针。这是一条单独的车道，排在 `view-gaps` 之后，判据是这三个入口不再收 `GCGraph *`、本页 E 节列的位置改成传句柄。
- **H**（原始 kind 与 `flags`）随 `kinds-to-oper` 走：四种 kind 删掉后再看还剩哪些读原始 kind 的地方。
