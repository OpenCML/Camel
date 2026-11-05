# 同步边的封口判据：问标记，还是问"这个节点是不是语句"

## 现象

`src/runtime/graph_edges.cpp#EdgeChecker::checkSyncEdges` 有两条判据在同一处：

- 现在树上的（integ2）：两条边端都要带 `kGCNodeFlagSyncStatement`（`isWaitingCarrier`），
  由写链的人设置；
- `lane-m4b2` 换上的：两条边端都要能被 `src/runtime/graph.cpp#isChainStatement` 从节点本身
  推出来，标记不算数（按"能推出来的事实不要存"的裁定）。

换上的那一版在合并时把 30 条 `feat/compile` 用例从绿变红，全部是同一条读数：

```
Pass 'std::opt' failed with an internal error: Runtime graph '__root__' breaks its edge
invariants: sync edge 6 -> 8 starts at a node that is not a statement of a chain
(kind 7, flags 4, data type 'int')
```

`flags 4` 就是那个标记：**节点带着标记，但推导判据说它不是链上的语句**。也就是说两套判据
今天互相矛盾，而这个矛盾不是新引入的缺陷——它是"标记还在写、推导判据已经上线"这个中途状态
本身。

合成的工作树回到了标记判据，30 条用例随之回绿；`isTheBlocksOwnValue`
（`src/runtime/graph.cpp#isTheBlocksOwnValue`）的定义留在树上，谁问谁读，只是封口处不问它。

## 两条路的代价

**甲：留标记判据，等标记的写点全部消失。**
代价是那个不变量今天只是"被相信"而不是"被检查"——写链的人忘了给标记，封口检查看不出来。
好处是零成本，而且和 `kGCNodeFlagSyncStatement` 的退场顺序一致（表达式声明、分配、draft
都还在写它）。

**乙：现在就换成推导判据，并把写标记的地方一起删掉。**
这是 `lane-m4b2` 想做的事，但它只换了判据、没换写点，于是"带标记但推不出来"的图被判红。
要落地得一次做完：`isChainStatement` 的推导要覆盖 `std::opt` 造出来的每一种链端（今天
`kind 7 / flags 4` 那一族它覆盖不到），同时删掉所有 `markSyncStatement` 调用点。
代价是一次跨 pass 的迁移，收益是封口检查开始真的检查。

**丙：两条都问一半。** 维护上最贵，两边都不背责任，不推荐。

## 我方默认建议

**甲**，并按 2026-11-04 的做法把它写死在那行注释里：封口处问标记，推导判据只做 census
（`CAMEL_LOG_INFO_S("SyncCensus", ...)` 那一支），直到写标记的最后一个调用点消失。
到那时把判据换成 `isChainStatement` 并删标记，是同一笔提交的两半，不再分两次做。

## 要作者答的一句

**"封口检查允许在标记还在写的时候只问标记吗？"** —— 是（甲，默认），
还是"不，宁可由它红着，也要现在就把推导判据和写点一起换掉"（乙）。