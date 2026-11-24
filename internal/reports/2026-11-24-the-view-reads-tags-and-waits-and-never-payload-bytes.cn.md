# 图视图的三处缺口：标签、等待、载荷字节——所补能力与所留缺口

2026-11-24 ｜ 车道 `lane/view-gaps` ｜ 基线 `origin/develop@5162f14ccc`
｜ 裁定 `D-1123-127`（缺口清单）、`D-1123-129`（顺序）、`D-1123-130`（顺序更正为 A→B→C）

下一页车道照这张表派工：**① 每一行是一个缺口、它卡住的读者、补上的能力与落点；② 没补的那几行写明为什么它不是视图的缺口，以及该归谁。**

## 0 一句话

`GCGraph`/`GraphDraft` 的提及数在各目录往 0 走，靠的不是把读者搬走，是**把读者要读的答案搬到视图上**。这一轮搬了三样：**节点的 tag set 与权限列、区域的 tag set（A）**、**等待与反向边、整张边表（C）**；**载荷字节（B）判为不是视图的内容**——视图只交「常量值 + 静态区下标 + 类型 + 已解析的记录」，字节与原始 kind 归 3b 的编辑原语（§3）。

## 1 缺口 → 能力 → 落点

| # | 缺口 | 卡住的读者（基线行号） | 补的能力（API） | 落点 | 本轮 |
|---|---|---|---|---|---|
| A1 | 节点的 tag set | `tiles/src/placement.cpp:198`、`:251`；`tiles/src/compiler.cpp:84`；`onnx/exporter.cpp:748` | `GraphView::attrsOf(NodeRef) -> attr::AttrSetId` | `src/kernel/graph/graph_view.h`；两个 adapter | 补了；`placement.cpp`、`graph_regions.cpp` 已搬 |
| A2 | 区域的 tag set（沿 `parent` 上溯） | `tiles/src/placement.cpp:42`、`:48`、`:206`、`:246` | `RegionRecord::attrs`（`GCRegion::attrs` 随记录一起拷） | 同上 | 补了；`placement.cpp` 已搬 |
| A3 | 节点权限列 + `#strict` 下的生效值 | `tiles/src/graph_regions.cpp:232`、`:666`；`region_extract.cpp:81`、`:213` | `permissionsOf`、`effectivePermissionsOf(ref, policy)`，规则本体是 `core::tag::permissionsInEffect(tags, permissions, policy)` | `graph_view.h` + `camel/core/tags.h` / `tags.cpp` | 补了；规则只写一处（`#strict` 一个含义） |
| A4 | 区域链（`nodeRegion` + `region->parent`） | `placement.cpp:204`、`:283` | 已有：`regionOf` + `region(r).parent` | — | 顺带确认可用，`placement.cpp` 已改问视图 |
| C1 | 「谁先于谁」：控制边与同步边的联合 | `onnx/exporter.cpp:561`、`:665`；`tiles/src/compiler.cpp:582`；`graph_regions.cpp:161-163`、`:586-589` | `EdgeKind::Wait`：`predecessors/successors(ref, Wait)` 是 `waitInputsOf`/`waitUsersOf` 的同一个联合（一对两种写法都点到的去重） | `graph_view.h`；两个 adapter | 补了；`graph_regions.cpp` 的两处已搬 |
| C2 | 反向 `uses()` | `tiles/src/region_outline.cpp:741`（`uses().norm`） | `successors(ref, EdgeKind)`（`Data`/`Sync`/`Order`/`Wait`）；`users(ref, PortGroup)` 本来就在 | `graph_view.h`；两个 adapter | 补了；`region_outline.cpp` 已搬 |
| C3 | 整张边表（不按节点问） | `graph_regions.cpp:130`（`syncEdges()` 走全表） | `edges(EdgeKind) -> span<const Edge>`，`Edge{pred,user}`；`Sync`/`Order` 直接投影两张侧表，`Data`/`Wait` 由适配器走节点建表 | 同上 | 补了；`chainMembers` 已搬 |
| B1 | 载荷字节（`nodePayloadBytes`/`nodeBodyAs`） | `transforms/jvp.cpp`、`split.cpp:1372`、`transpose/walk.cpp:2226`、`rule_expand.cpp:1170-1177`、`pullback.cpp`、`std/cpp/function_emitter.cpp`、`std/fvm/compile.cpp:621` | **不补**（§3）：3b 的编辑原语 / 读者持有被投递的载荷 | — | 判为不是视图的缺口 |
| B2 | 原始 `GCNodeKind` | 同上，及 `gir/gir_metadata.cpp` 的 `recordType` | **不补**：视图的词汇是九个 kind，原始 kind 是表示的标签 | — | 同上 |
| B3 | `dataIndex` + `staticSlots` | `jvp.cpp:1557-1561`、`vmap.cpp:592-594`、`gir/gir_metadata.cpp:21`、`:33` | **已答**：`constantOf` / `staticIndexOf` / `staticSlotCount`（A 轮用它搬了 `placement.cpp` 的 `functionGraphOf`） | 已在 `graph_view.h` | 本轮证明够用；`transforms` 那两处留给后续车道 |
| B4 | 帧布局查询 | `std/fvm/compile.cpp`、`std/cpp` 的解码体 | **不补**：帧布局是执行计划的一半，与 `execPlanOf` 同一条裁定 | — | 同上 |
| D | 含 JOIN 的按编号枚举 / 草稿重编号 | 主卡 `transforms` | 不在本车道 | — | 未做（题面点名不做） |
| E | 图与程序归属、「两端」 | `tiles` 的 `calleeGraphOf`/`GpuScopes`、`tools` 的 `GCGraph*` 作为 map 键、`gir` 的入口形参 | 不在本车道 | — | 未做（题面点名不做）；残余见 §4 |

## 2 读数

口径就是阶段 3a 那个命令（`git grep -cE`，每文件计数求和）：

```
git grep -cE '\bGCGraph\b|\bGraphDraft\b' <rev> -- <目录>
```

注意 `\b` 的边界：`GCGraphOrderEdge`、`GCGraphSyncEdge`、`GCGraphBuildAccess` **不计**（它们不是那个类型名）。本轮因此不吃「把 `GCGraphSyncEdge` 循环改掉」这种便宜。

| 目录 | 基线 `5162f14ccc` | 本轮 | Δ |
|---|---|---|---|
| `src/tools` | 58 | 54 | −4 |
| `src/stdlib/std/gir` | 63 | 60 | −3 |
| `src/stdlib/tiles` | **74** | **63** | **−11** |
| `src/stdlib/transforms` | 124 | 124 | 0 |
| `src/stdlib/autodiff` | 41 | 41 | 0 |
| 全仓 | 3228 | 3221 | −7 |

`tiles` 的 −11 是 `placement.h`/`placement.cpp` 的查询改成 `const graph::GraphView &`（`placementOf`、`bodyPlacementOf`、`hasPlacementFact`、`holdsStaticFunction` 四个签名），加上 `graph_regions.cpp` 里 `chainMembers`/`inChain` 整块删掉——等待问出来以后，那张按 sync 边预建的集合没有读者了。**剩下 63 行里没有一行是 A/C 缺的**，全部是 B（载荷/kind）或 E（`GpuScopes` 的图身份、`Program::graphs()` 的形参）。

全仓只降 7 是因为这一轮在自己身上加了 11 行：`graph_view.h`、两个 adapter、`core/tags.h` 的注释都要点名它们替换掉的 `GCGraph`/`GraphDraft` 成员，否则下一个读的人不知道旧拼法是什么。视图解释自己替换了什么，这一行算在账上，不减。

`tools` 的 −4 与 `gir` 的 −3 里，只有两类真搬运：`gir/dot/graphviz.cpp` 的两条 order/sync 边循环改问 `predecessors(Order/Sync)`，`tiles` 的 `region_outline.cpp` 反向 `uses().norm` 改问 `users(Normal)`；其余是入口处多余的表示类型（`program->root()` 的 `auto`）与三处注释（它们引用的读数现在在视图上：`originOf`、`exit`）。**这两处不必粉饰：它们是 E 类缺口，不是这一轮补的能力。**

三个路径都是实的，命令原地可复现：`src/tools`（debugger + camel-cli）、`src/stdlib/std/gir`（`src/stdlib/std/gir/dot/graphviz.cpp` 等 7 个文件）、`src/stdlib/tiles`。**`src/stdlib/std/onnx` 不存在**（onnx 在 `src/stdlib/onnx`，本页口径下 84），所以 onnx 的数本页不给，它属于 `lane/3a-onnx-tiles`。

### 2.1 A 的判据不是 `tiles` 的总数，是那几处调用点能不能迁

`lane/3a-onnx-tiles` 报的 A 类在 `tiles` 是 13 个位置（`nodeAttrs` 4、区域 tag set 5、`effectivePermissions` 5）。本轮的读数：**只有 `placement.cpp` 那一组能整体迁走，其余每一处都坐在一个同时读载荷/原始 kind/`flags` 的函数里**，逐处核过：

| A 的调用点 | 所在函数还读什么 | 结论 |
|---|---|---|
| `tiles/src/placement.cpp:198`、`:251`、`:42`、`:48`、`:206`、`:246` | 无（只读 tag set、区域链、`staticSlots`/`dataIndex`→`constantOf`） | **迁了**（4 个签名 + 5 处调用点，−11 行） |
| `tiles/src/compiler.cpp:84`（`checkAttributeFates`） | `g.node(id)->kind`、`nodeBodyAs<GCOperBody>(id)->uri()` | 不迁：kind 与载荷是 B |
| `tiles/src/region_extract.cpp:81`、`:213` | `source.node(ref)->kind`、`->flags`、`nodePayloadBytes`、`nodeOrigin` | 不迁：整条 `DraftNodeInit` 是 B 的写 |
| `tiles/src/graph_regions.cpp:232`、`:666` | `graph.node(inputs[i])->dataType()`（B-3 可换）与 kernel IR | 半迁：换掉 `dataType` 不动 `GCGraph` 这个名字（函数形参是它），所以计数不动 |
| `onnx/exporter.cpp:748-749`（`recordFates`） | `nodeAttrs`、`nodePermissions`、`nodeOrigin`、`name`——**都已在视图上**，只差 `operUriOf(g, ref)` | 已就绪，属 onnx 那条车道的账，本轮不动 |

所以：**补对 A 的效果是「`placement.cpp` 整组迁走」，不是「`tiles` 里那 13 处一起消失」**——其余 12 处被 B 钉在同一函数里，而 B 的答复是 §3 的 B-2。这一条可验：`git show` 本轮 diff，`tiles` 的 −11 行全部落在 `placement.*` 与 `chainMembers` 那一块。

## 3 B 的判断：B-2，不是 B-1

**B-1（视图交一份只读载荷视图：字节范围 + kind + 常量槽索引 + 帧布局）与 `graph_view.h` 明说的边界冲突，因此不做。** 依据三条，都是本仓已经写下的：

1. `graph_view.h` 的文件头与尾注都写着「no byte of it leaves this face」，并把 `nodePayloadBytes`/`nodeBodyAs` 列在「不交」一侧。视图存在的理由是「每个谓词只写一次」；交字节会让每个读者都变成布局的读者，也就是第二个图模型。
2. 用字节的那些读者在做什么：`split.cpp:1372` 与 `transpose/walk.cpp:2226` 把 `DraftNodeHeader` 的 `kind`/`dataIndex`/`runtimeFlags` 抄进 `DraftNodeInit`——**那是在写一张新图**；`rule_expand.cpp:1170` 的 `nodeBodyAs<GCOperBody>` 是在按载荷里的 URI 造算子节点——**也是写**。写是 3b 的编辑原语。
3. 帧布局（B4）是执行计划的一半。作者 11-23 已经裁过 `fvm/version_state.cpp:37` 的 `execPlanOf`「不是视图缺口，是事实框架自己的答案」；帧布局同一个形状，不能一边说计划归事实层、一边把计划的布局塞进视图。

**B-3 已经在视图上，而且够用**：`constantOf`（常量值，用 `typeOf` 解码）、`staticIndexOf`（负半段 = 静态区下标）、`staticSlotCount`（越界判据）。本轮用它搬掉了 `placement.cpp` 的 `functionGraphOf`（原来读 `staticSlots()` + `-dataIndex`），改后行为逐字相同（`[VW]` 与 tiles 的 placement 用例钉着）。**这是 B-1 唯一真正缺的那一半的证据：要的是索引与值，不是字节。**

所以留给下一批的形状是 B-2：**载荷归 3b 的编辑原语；视图永不交。** 那句话对读者的意思是「持有被投递的载荷」——构造/重写一张图的一方本来就有它，读的一方不该有。**若作者要 B-1，这一页的 §3 就是要被推翻的那一条**；推翻后 A/C 不需回退。

## 4 谁还卡着，卡在哪

- `src/stdlib/transforms`（124，一行未动）：`jvp.cpp`/`vmap.cpp` 的 `staticSlots()`+`dataIndex` 可以用 B-3 搬；`split.cpp`/`transpose`/`rule_expand`/`pullback` 是 B-2（写一张图）。**这是下一批最大的一块，按 B-2 派工即可。**
- `src/stdlib/std/fvm`、`std/cpp`（99、84）：B-2 的解码体。
- `src/stdlib/tiles`（63）：`GpuScopes`/`calleeGraphOf` 要的是「图的身份」与 `Program` 的形参（E）；`compiler.cpp` 的求值器读 kind + 载荷（B）。
- `src/stdlib/std/gir`（60）：`gir_canonical.cpp` 的 `payloadJson`/`recordType` 是 B-2；其余是 map 以图指针为键（E）。
- `src/tools`（54）：debugger 的索引与观察者接口都以 `GCGraph*` 为身份（E）。
- `src/stdlib/onnx`（84）：`exporter.cpp:748` 的 A1 现在可搬（`attrsOf`+`permissionsOf`+`originOf` 都在），本轮没动它因为它不在五个目录的判据里；`exporter.cpp:561`/`:665` 的 C1 同。

## 5 不是缺口的那几类（和缺口分开记账）

和 `fvm/version_state.cpp:37` 的 `execPlanOf` 一样，下面这些**看起来像视图的洞，其实不是**，把它们算进缺口会把账算错：

1. **`src/stdlib/autodiff` 的 41 行（另一条车道在同目录报 40）：没有一行是「读」。** 全是图的身份——被存下来、被返回、当 `unordered_map` 的键——加上 4 行 `ruleGraphs`。身份不是视图能答的东西（视图答的是内容），所以这个目录**不该出现在任何「视图缺口」的表里**。本轮它一行没动，也不该动。
2. **`execPlanOf(graph)`（`fvm/version_state.cpp:37`）**：执行计划是事实层自己的答案，作者 11-23 已裁。同类的是**帧布局**（本页 B4）。
3. **`GCGraph::view()` 造适配器时没装 callee 解析器** —— 这一条**不属于 A/B/C**，它是「适配器与所有者之间少一根线」，不是「视图缺一个读法」，修法完全不同。核过的事实：
   - `src/kernel/graph/gc_graph_view.cpp:41-46`：`GCGraph::view()` 是 `new FrozenGraphAdapter(*this)`，**用的是单参数构造**，`calleeGraphOf_` 为空；
   - `src/kernel/graph/gc_graph_view.cpp:506-534`：`FrozenGraphAdapter::calleeOf` 对 FUNC 节点先问 `graph_.calleeIdOf(ref)`，拿到一个有效 `GraphId` 就 `GUARD(calleeGraphOf_ != nullptr)`（**518 行**）；
   - `GUARD` 在 Release 里是活的（`camel/utils/assert.h:95`，报 `Camel Invariant Violation` 并 abort），不是被 `NDEBUG` 抹掉的 `ASSERT`；
   - `src/kernel/graph/callee_graph_of.h:18-20` 自己写着：没有解析器的适配器「不是错的，是更窄」，它答不了按值的 callee，`calleeOf` 在那里**拒绝**；
   - 唯一的调用者 `src/kernel/facts/facts.cpp:988`（`arrangeable` 事实）拿到的正是这种窄适配器：`facts.cpp:451` 的 `GraphAccess::view` 是 `&graph.view()`。
   **所以「`view().calleeOf` 一用就 GUARD」是真的**，而且是**一句也没有传递者**的那种真：`GCGraph` 不持有 `Program`，`Program` 才有 `graph(GraphId)`。修法是让 `Program` 收 `const Graph *`（E 的那一半），或者让事实层的 `GraphAccess` 自己带上解析器——**两条都不是视图的洞**。这一条值得单独派工，它在 `transforms` 里是最容易回收的一块。

## 6 闸门读数

**① 构建（题面原命令，`-Plan` 为空即「只构建」）：**

```
pwsh internal\tools\camel-remote.ps1 -Path D:\Projects\Camel-wt\viewgaps -Jobs 8 -NoGpuWatch
→ BUILD_EXIT=0, BUILD_SECONDS=20.9, [DONE] Release build completed
```

**② 本车道钉住的那个计划（5 条全绿）：**

```
-Plan "test/plans/feat/opt/graph_faces.plan.toml"
→ RUN_EXIT=0, [tier] fast: 1 plan file(s), 5 declared test entries, 5 pass
```

`graph_faces.view_ports` 里新增的 `[VW]` 行在跑：`attrs`/`perms`/`wait`/`users` 逐节点、`edges`/`regions` 逐图与表示自己的答案逐项比，`[VW] agree=1`。

**③ 全快速层（`smoke,feat`）：基线本身是红的，本车道没有新红。**

```
-Plan "smoke,feat"
基线 5162f14ccc（干净树）：total 9895, pass 8458, fail 1384, skip 23, known failures 30
本车道（22 个文件 + 本页）：total 9895, pass 8458, fail 1384, skip 23, known failures 30
失败用例名逐条求交：base=1384、mine=1384、Compare-Object 条目 = 0
```

即 **1384 条红在 `5162f14ccc` 上就有**（与本轮无关），而两边的失败集合**完全相同**——本轮一条新红也没有、也没有把哪条红变绿。为了让这条读数不受「工作树脏」影响，基线是用 `5162f14ccc` 的独立检出（临时工作树 `D:\Projects\Camel-wt\viewgaps-base`，分支 `scratch/viewgaps-base`）量的，与本车道互不覆盖。

同一形状的窄读数（`feat/compile,feat/examples,feat/modules/tiles`，2922 条）：基线 `fail 270`、本车道 `fail 270`，名字集合 `Compare-Object` 条目 = 0。

**④ 静态检查器没跑成，原因是读数本身：** `-Check all` 在**未提交**的树上拒绝执行（「the base and this tree are the same commit: there was no change to measure」），提交之后这条车道没有再跑一次 `-Check`——**所以「检查器干净」这一条我没读到**，不当作已验。

## 7 没有做的事

未动 `develop`、未推 `develop`、未删分支与 tag、未 `--amend`、未改 `internal/collab/**`。D 与 E 未做（题面点名不做）。`fvm/version_state.cpp:37` 的 `execPlanOf` 未加视图能力。`git stash` 未再使用（本仓 stash 是全仓共享的，本轮改动的暂存与恢复都走 patch 文件与 WIP 提交）。