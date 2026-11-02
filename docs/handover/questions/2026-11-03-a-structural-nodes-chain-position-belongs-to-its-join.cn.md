# 结构节点的链位在它的 JOIN 之后，不在它自己身上

## 问什么

`src/runtime/draft_inline.cpp#inlineCallableInDraft` 里 `isBodyStatement` 决定"拼进来的函数体里哪些节点算语句"，链条的头尾都由它算出来。它今天读 `kGCNodeFlagSyncStatement` 标记（**构建器挂过链**）；作者 2026-11-02 04:50 裁定能推导的标记不该存，于是问：**换成节点形状（`src/runtime/graph.cpp#nodeIsChainStatement`）之后，"最后一条语句"由什么决定？** 我按"同控制区域内布局序最大的语句"试过两个变体，都炸在同一个断言上（§2）。这一页要定的是**先决条件**，不是那一行怎么改。

## 1 探针读数（原样）

`test/cases/consistency/faults/fault_in_untaken_arm.cml`，`std::opt`，第四处 splice（`main` 体拼进 `__root__`，`region=0`，`slotsBefore=2`）。`stmt` = `include/camel/runtime/draft.h#isChainStatement`，`mark` = `kGCNodeFlagSyncStatement`，`sin`/`sout` = 块内的同步前驱/后继：

```
  id=4  kind=11 stmt=1 mark=1 sin=0 sout=0   ← :io/println
  id=7  kind=11 stmt=1 mark=1 sin=0 sout=1
  id=11 kind=11 stmt=1 mark=1 sin=1 sout=0
  id=13 kind=11 stmt=1 mark=0 sin=0 sout=0   ← 是语句，没挂链
  id=14 kind=11 stmt=1 mark=1 sin=0 sout=1
  id=16 kind=13 stmt=0 mark=1 sin=1 sout=0   ← 带标记，按形状不是语句（GATE）
  id=19 kind=7  stmt=0 mark=0 sin=0 sout=0   ← JOIN，BRCH 20 的
  id=20 kind=6  stmt=1 mark=0 sin=0 sout=0   ← BRCH：按形状是语句，构建器没挂链
  id=27 kind=7  stmt=0 mark=0 sin=0 sout=0   ← JOIN，BRCH 28 的
  id=28 kind=6  stmt=1 mark=0 sin=0 sout=0   ← BRCH
```

同一张表里其余 19 行是纯值与常量（`stmt=0 mark=0 sin=0 sout=0`），从略。

按标记读：链头 = 有标记、块内无前驱且有人跟（`7`、`14`），链尾 = 有标记、块内有前驱且无人跟（`11`、`16`）⇒ `heads=2 tails=2` ⇒ `bodyHasChain=false` ⇒ 走 GATE 兜底，不合并也不交让 —— **今天是对的**。按形状读：链头仍是 `7`、`14`，链尾只剩 `11` ⇒ `heads=2 tails=1` ⇒ 进 `joinBodyPieces` ⇒ 它挑"最近读到的语句"当前驱，挑中 BRCH ⇒ 链边从 BRCH 出发。

## 2 两个判据的定义域不同，而且两个方向都差过

- 标记 = "构建器在这一处落下过链边"；形状 = "跑它是可观察的"。两者在同一条语句上不必同时成立：**id 13 是语句却没标记，id 16 有标记却不是语句**。
- 换成形状：`PassInternalError` —— `OPER 4 (:io/println) runs between BRCH 20 and its JOIN 19 and belongs to no arm`。再改成"头尾与前驱都按布局序"：`BRCH 28 runs between BRCH 20 and its JOIN 19 and belongs to no arm`。两处都指向 `docs/technical/17_graph_rewrite_api_contracts.md` 4.1 与 `docs/design/effects/edges.cn.md` 4.6。
- **布局序不是执行序**：臂的语句就排在 `BRCH` 与 `JOIN` 之间，"布局上在 20 之后"不等于"在 20 结束之后执行"。

## 3 三条路

- **（甲，默认）先定"结构节点（`BRCH`/`LOOP`/`CALL`）的链位由它的 `JOIN` 承载"**，再动那一行；`mark` 这一轮不动。理由：4.6 说同步边连的是同一块里"直接写着的相邻语句"，而结构节点的"下一条语句"是它 JOIN 之后那一条 —— 这条位置关系没人写清过，`joinBodyPieces` 就是在这里挑错的。
- **（乙）继续用标记，把这一处标为有条件例外并写清条件**：条件是"标记只落在构建器落了链边的语句上"，代价见附录。
- **（丙）同时改 `joinBodyPieces` 的前驱选择规则**（不挑结构节点，或挑 JOIN 的用户）。它仍建立在（甲）那条没写清的位置关系上，跳过（甲）就是在猜。

## 附录：退役不是一行的事

标记今天**还在 8 处写**：`src/compile/gir/builder_helpers.h#markSyncStatement`、`src/passes/opt/generic/rewrite.h#markSyncStatement`、`src/passes/opt/generic/generic.cpp#joinBlockFragments`（两处）、`include/camel/runtime/draft.h#handChainPlaceTo`、`modules/autodiff/pullback.cpp`、`modules/transforms/jvp.cpp`（两处）。**读的还有 7 处**：`src/runtime/draft_inline.cpp#inlineCallableInDraft`（两处）、`src/runtime/draft_regions.cpp#inheritRegion`、`src/execute/macro/rewrite.cpp#bridgeMacroRewriteControl`、`src/passes/opt/generic/rewrite.h#markSyncStatement`、`src/passes/opt/generic/generic.cpp#joinBlockFragments`、`src/passes/test/fault.cpp` 的一次性探针（`src/execute/pass.cpp#effectCredentialViolation` 里只剩注释）。多留一处，就多一处"构建器做过什么"的知识不进图 —— 这就是（乙）的代价。

## 更正

`docs/handover/questions/2026-11-03-does-a-mark-encode-shape-or-history.cn.md` 里"一个 `sync` 块里每条语句都被挂上链……不成立"的**证据**已撤回：同一晚 `println(1); println(2)` 的 `std::gir` 首导是 `N01 -> N03` 上 `ORDER` 与 `SYNC` 并见，**有** SYNC；那张"零 SYNC"的图是纯函数 `g(x) = x` 那次。是语句却没挂链的（本页 id 13）在别处 —— `main` 体拼进 `__root__` 的区域，不是 `sync` 块。该页"两个判据的定义域不同"仍然成立，证人换成本页的 id 13 与 id 16。