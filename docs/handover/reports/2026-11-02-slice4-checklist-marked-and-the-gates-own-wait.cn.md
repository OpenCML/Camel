# 片 4 的清单，照抄一遍并逐条标"今天做了/没做"，以及它在哪一步停住

**依据的两页**：`docs/handover/reports/2026-11-02-slice4-keep-to-gate-checklist.cn.md`（`7207e3802` 写下，`f3fd12fb6` 补上 §5.1）。本页做三件事：把清单抄成"今天的状态"、回答 `f3fd12fb6` 点出的那个缺口、写出这一笔**在哪一步停住**和停住的原文。

**结论先说**：清单的 §1/§2/§4 都做过一遍（`git grep -c ':op/keep'` 从 17 降到 4，全套编过，`operator_semantics` 4/4 绿），**但 §2 第 2 行那个改法在提交时被图的不变量拒了**，所以整笔已回退，树上没有这一笔的代码。停住的位置与原文见 §3。

## 1. 清单原文与今天的标记

### 1.1 要删的 8 处（原页 §1）

| # | 位置 | 删掉什么 | 今天 |
|---|---|---|---|
| 1 | `include/camel/core/operator_table.h:153` | `inline constexpr OperId Keep = 22;` | 改过，已回退 |
| 2 | `include/camel/core/operator_table.h:127-151` | `:op/keep` 的契约注释 | 改过，已回退 |
| 3 | `include/camel/core/operator_table.h:187` | `kWellKnownOperatorUris` 的一项，`kWellKnownCount` 23 → 22 | 改过，已回退 |
| 4 | `src/builtin/executors/builtin.cpp:170` | `{"op/keep", __keep__}` 登记 | 改过，已回退 |
| 5 | `src/builtin/executors/builtin.cpp:643` | `key != "op/keep"` 的纯性豁免 | 改过，已回退 |
| 6 | `src/builtin/operators/ops.h:19-33` | `__keep__` 的声明与注释 | 改过，已回退 |
| 7 | `src/passes/test/semantics.cpp:151-157` | `printTraits(":op/keep")` 一段 | 改过，已回退 |
| 8 | `test/plans/feat/modules/operator_semantics.plan.toml:33-34` | 两行 `SEMANTICS traits :op/keep …` | 改过，已回退 |

**回退前的一次读数**（这些是"改法对不对"的证据，不是落地的读数）：全套编译 `BUILD_EXIT=0`，
`test/plans/feat/modules/operator_semantics.plan.toml` `4/4 pass`（两行删掉后计划仍然自洽），
`git grep -c ':op/keep'` 17 → 4。

### 1.2 要改的 4 处产生侧（原页 §2）

| # | 位置 | 改成 | 今天 |
|---|---|---|---|
| 1 | `src/passes/opt/generic/rewrite.h:180-218` | 契约原文改成"同步前驱 + 唯一数据端口" | 改过，已回退 |
| 2 | `src/passes/opt/generic/rewrite.cpp:292-302` | `addGateNode` + 唯一数据端口 `passed` + 同步前驱 `source` | **改过、编过、被图的不变量拒了 —— 见 §3；已回退** |
| 3 | `src/passes/opt/generic/generic.cpp:731` | 注释改说 `GATE` | 改过，已回退 |
| 4 | `src/passes/opt/generic/generic.cpp:1498` | 注释改说 `GATE` | 改过，已回退 |

**一处原文更正**：原页 §2 第 2 行写的 `draft.setSyncPredicate(node, source)` 在树上不存在；
做同一件事的是 `GraphDraft::setSyncPredecessor`（`include/camel/runtime/draft.h:682`，
"refuses a self edge. A node has at most one"）。这一条不影响结论，只是名字。

### 1.3 三个测试装置（原页 §3）

| 装置 | 改成读什么 | 今天 |
|---|---|---|
| `src/passes/test/keepnode.cpp` | `GCNodeKind::Gate` 且 `syncInputsOf` 非空：`KEEPNODE gate=1 …` | 改过，已回退 |
| `src/passes/test/keepshape.cpp` | 同上：`gate=1 sync_pred=1` 取代 `uri=:op/keep` | 改过，已回退 |
| `src/passes/test/semantics.cpp` | 整段删 | 改过，已回退 |

### 1.4 期望值（原页 §4）

`effects_kept.plan.toml`、`keep_node.plan.toml`、`operator_semantics.plan.toml`、`check.mjs` ——
**没做完**：`operator_semantics` 的两行删了（绿），`keep_node`/`effects_kept` 的期望值还没改完就
撞上 §3，回退时一并回去了。

### 1.5 验收（原页 §5）

1. `git grep ':op/keep'` 0 命中 —— **没做到**（回退后仍是 17）
2. 四后端读数 —— **没做**
3. `std::cml` 往返 —— **没做**
4. 全量 fail 0 —— **没做**（这一笔没有落地）
5. `OperId 22` 不复用 —— **做了，但随回退回去了**

### 1.6 原页 §6「这一笔不做的事」

`writeControlLink`/`appendControlEdge` 不动、`EffectLivenessFacts` 字段不动、不给 `GATE` 新增算子身份
—— **三条都没碰**。

## 2. 原页 §5.1 的缺口：`GATE` 自己的等待，今天由谁接

`f3fd12fb6` 的标题就是这句话：*The checklist stops short of the GATE's own wait: that read is a
different question from a credential*。原页 §5.1 因此把 `graph.cpp:561` 的
`facts.hasWaitEdge = !graph->ctrlInputsOf(ref).empty();` 留在原地（它问的是"这个 GATE 有没有等待"，
不是"这个效应有没有凭据"），这一笔不碰。

**今天是两条互不相识的路在接这个等待**：

- 值这条路（`keepThroughValue`，`src/passes/opt/generic/rewrite.cpp:288-300`）造的是 `:op/keep`，
  `setNormInputs(node, {source, passed})` —— **它身上没有等待**：两个入边都是数据边，"谁读过它"
  是它的算子身份（`mayFault`/`externalEffect` 的保守声明），不是边。
- 等待这条路只能由**读它的那个节点**来接：`replaceNode` 的 `inheritedWait`
  （`rewrite.cpp:367-384`，被 `handOverSyncWait` 交下去）和 `bypassValueGate` 的
  `appendControlDependency(value, user)`（`gates.cpp:94`）。也就是说今天"`:op/keep` 所保活的那个节点"
  依赖的是**别人的**链位，而不是自己身上的一条边。

**换成 `GATE` 之后这两条路本该合成一条**：`GATE` 的等待是它自己的同步前驱，而
`dissolveUnorderedGates` 的第一道门就是 `!syncInputsOf(id).empty()`
（`src/runtime/draft.cpp:2072`）—— 有同步前驱的 `GATE` 永远不会被溶解。这正是 §3 撞墙的地方：
**这条边写不进去**。

## 3. 停住的位置：`sync edge … starts at a node no sync block wrote`

按原页 §2 第 2 行改完之后，全套编过（`BUILD_EXIT=0`），但三份计划里的 dce/fold 用例全红，原文：

```
error[RuntimeDiag::PassInternalError]: Pass 'std::opt::dce' failed with an internal error: Runtime graph 'main' breaks its edge invariants (1 violation(s)):
  graph 'main': sync edge 1 -> 5 starts at a node no `sync` block wrote (kind 11, flags 0, data type 'int').
```

（`kind 11` 是 `GCNodeKind::Oper`，即那个被保活的效应节点本身；`1 -> 5` 是新 `GATE` 的同步前驱边。
同一句话在 `std::opt`、`std::opt::fold`、四个后端上逐字复现。）

**这不是期望值没改，是图不变量拒了这个形状**：`checkSyncEdges` 要求同步边的起点是"某个 `sync`
块写下的节点"，而 `keepThroughValue` 存在的全部理由就是**那个效应没有链位**（`rewrite.h:195`
"Where a `sync` block gives the effect a place in its chain the older mechanism…"）。原页 §2 把 `x`
写成同步前驱，等于要一个"没有链位的节点"去当链上的前驱——两句话不能同时成立。

我方还试了一条退路：`setSyncPredecessor` 拒绝时把 `GATE` 擦掉、让读者直接读 `x`
（这样至少不丢读），但那样 `x` 就回到"只有数据读、没有链位"的原状，`dissolveUnorderedGates`
下一次提交又把它当成纯转发的前驱擦掉——就是这一片要修的那个缺陷。所以退路也不成立，整笔回退。

**要作者答的一句**：`keepThroughValue` 这条"没有链位"的路，`x` 该不该被允许当同步前驱？
- 若**该**：`checkSyncEdges` 的"起点必须是 sync 块写下的节点"要放宽成"起点可以是任何可观测节点"，
  并且说明这条边与 `edges.cn.md` 5.3 的 "GATE = 一个数据端口 + 一个同步前驱" 是同一个意思；
- 若**不该**：那么 `keepThroughValue` 需要的不是 `GATE` 而是别的东西，原页 §2 的整个改法要重写。

**我方默认建议**：走"该"，但**不**放宽 `checkSyncEdges` 的全文，只加一条明确的例外——
同步边起点是 `GATE` 所保活的那个节点、且该节点 `carriesOrdering(…, true)` 为真时允许；
理由是这条边的用途与 `sync` 块写的边不同（它不是"同一块里上一句"，而是"我读你，所以我等你"），
把它写进同一条不变量会掩盖两种意思。**这一条我没有擅自做**：它改的是 `edges.cn.md` 5.3 的模型，
按本轮第 5 条应当停下来问。

## 4. 与"一条一笔"的关系

这一笔是按"一条一笔"试的：§1/§2/§3/§4 一起改，因为原页第 5 行写"这一笔不许分两次落"，而清单的
三项互相锁——先删算子是编不过，先换产生侧是十一条期望值同时红。§3 停住之后没有中间态可留，
所以**没有留下任何一笔代码提交**，只留这一页。