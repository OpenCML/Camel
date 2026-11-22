# `liveNodeCount()` 不是活集合

## 事实

`GCGraph::liveNodeCount()`（`src/runtime/graph.cpp:2591`）数的是**`kind != GCNodeKind::Dead` 的节点**：

```cpp
gc_cnt_t GCGraph::liveNodeCount() const {
    gc_cnt_t live = 0;
    ... live += chunk.nodes()[i].kind != GCNodeKind::Dead; ...
    return live;
}
```

它唯一的实质使用者是**编码器**（`src/compile/gir/graph_encode.cpp:415` 一带，`prepared.liveNodes` / payload 的
大小），问的是「这张图要装几个节点」。**清扫留在图里的死节点（没人读的 `Func`、孤儿 `Gate`）按定义本来
就不在它里面**——它按 kind 过滤，不按可达性过滤。

## 为什么不能拿它答「读者还活着吗」

一个节点的读者可能是**清扫收不走的孤儿**（本目录另一页记的那条：`screen.cpp:239-258` 的 switch 缺
`Func/Call/Loop`）。这样的读者 `kind != Dead`，于是 `liveNodeCount` 的世界里它「活着」；而按原则 3.1
它既不运行也不产生值，程序永远看不到它。**用 `liveNodeCount` 判断「某个读者是否还活着」，答案恒为
「活着」**——这正是上一轮把 `dead=1` 变成 `dead=0`、边界检查拒图的那个错的另一个版本。

两个问题是两件事：

| 问的 | 谁来答 |
| --- | --- |
| 这张图要装几个节点（payload 大小） | `GCGraph::liveNodeCount()`，按 kind 过滤 |
| 哪些节点还能被程序看到（读它等于有人读） | `runtime::liveNodesOf(draft)`，从根向后可达 |

## 作者的裁定原文点名了它

裁定说「`compile::gir::liveDraftNodes` 与 `liveNodeCount()` 改为调用它，然后删掉自己的走法」。
**前半句已落地**（`d36a2803d`：`liveDraftNodes` 现在只有三行，自己那 76 行走法删了）；
**后半句在事实层面不成立**：`liveNodeCount` 回答的不是同一个问题，改成调 `liveNodesOf` 会让编码器按
可达性而不是按 kind 算 payload 大小，两边对不上。所以这一处出入必须让作者看到，而不是我替他合并。

## 默认建议（按此开工，不另等）

**`liveNodeCount` 保持现状**（它回答 payload 大小），**活集合只由 `runtime::liveNodesOf` 回答**；
**冻结图侧的活集合等 3.4 的根集合落地时写在它旁边**（`src/runtime/graph.cpp`，与 draft 那份同一段代码
的两个面），而不是复用 `liveNodeCount`。

## 附：3.4 的根集合与现有走法的关系

原则 3.4 说同步块里的效应、可能出故障的运算、点名的值都是块的根。现有 `liveNodesOf` **已经从根沿
`sync` 输入向后走**（`src/runtime/graph.cpp:1723-1726`，注释引 `docs/design/effects/edges.cn.md` 1.5：
「sync 边携带 used」），所以**`sync` 链上的写本来就在活集合里**，接上判据之后也不会被删——这正是作者
第 2 条要的读数。真正缺的是**区域内部**的根（一条臂或一个循环体里没人读的效应），那要区域端口与臂的
入口一起当根；它属于 3.4 那一步，不是本页的范围。
## 决策者答复（2026-11-15）

你对，我上一页把它点错了：`liveNodeCount()` 回答的是要装几个节点，不是程序能看到哪些节点，不能改成调 `liveNodesOf`。保持它，但**改名**为 `storedNodeCount()`（或同义的名字），免得下一个人再把它当活集合用。冻结图侧的活集合按默认建议写在 `liveNodesOf` 旁边，并且**只写一份算法**：从根沿输入向后走的那段代码，对草稿与冻结图各有一个只读视图（`draft_writes.cpp` 已有 `FrozenGraphView` 这种视图），走法本身不复制。

## 后续（已落地）

作者裁定「保持它、但改名」：`GCGraph::liveNodeCount()` 现在叫 **`storedNodeCount()`**
（`include/camel/runtime/graph.h:895`），`GraphRead` 接口上的同名方法一并改名（`graph_read.h:88`），
注释里写明「它回答要装几个节点，不是程序能看到哪些节点」。活集合只由 `camel::runtime::liveNodesOf` 回答，
冻结图侧的那份与本页建议一致，已经补上（`2e5504b8e`，与草稿同一份走法、两个视图）。
