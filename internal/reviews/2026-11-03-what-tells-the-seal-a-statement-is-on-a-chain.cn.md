# 一页一问：封口检查怎么知道"这个节点在链上有位置"？

## 实测事实

`src/runtime/graph_edges.cpp` 的封口检查拒绝两种图：

```
graph 'main': sync edge 12 -> 17 starts at a node no `sync` block wrote (kind 6, flags 0, data type 'int')
graph 'timed': sync edge 5 -> 7 joins two blocks: 5 is in the block of region 0 and 7 in the block of region 2
```

今天它靠 `node->flags & kGCNodeFlagSyncStatement` 这个**标记**判断"这个节点是语句"。标记要退役（能推导的不该存），于是逐处查了 10 处写标记的地方：

| 事实 | 结果 |
|---|---|
| 边**都在** | 每写一次标记，下一行就写出链边（`builder_helpers.h:678` `linkNodes(LinkType::Sync, from, to)`） |
| 区域**不是**载体 | `GCRegionKind` 七个成员（Root/Arm/LoopCond/LoopBody/Inline/Block/Dead）里没有"sync 块"；`GCRegion` 也没有这个字段 |
| 唯一"在不在 sync 块里"的既有查询 | `builder_diagnostics.cpp#inUnusedSyncBlock`，**沿边图走到一个 `Gate`** |
| fork 那一半 | 已经能从边表算（`checkSyncChainForks` 用 `graph_.syncEdges()` 建 followers），**不需要标记** |
| "两端都是语句"这一条 | **只能读标记**，因为校验对象就是那张边表 —— 用边证边是自指 |

两种替换写法各自的读数（都已回滚）：

- 谓词改"同块"（`controlBlockOf(pred)==controlBlockOf(user)`）⇒ `fail 707`：**链合法地跨进嵌套区域**（5 在 region 0 的块、7 在 region 2 的块）。
- 谓词去掉（只删写标记）⇒ `fail 212`：`starts at a node no sync block wrote`。

## 问题

要删掉这个标记，"这个节点在链上有位置"需要一个**非自指的凭据**。可选：

- **(甲)** 建图侧写一个**链号 + 句序号**（两个整数释放到节点上，封口只读、不与边互证）；
- **(乙)** 承认封口这条检查无法逐个节点推导，改成**只渲染不拒**（与 census 同形）；
- **(丙)** 保留标记（**与"能推导的不该存"冲突，除非承认它就是不能推导**）。

## 我方默认

**按 (甲)**：链号与句序号由建图侧在建图时写下（那一处手上就握着前驱/后继），封口只读这两个数。

理由：**(乙) 是撤检查**（今晚已有一条"撤检查比留一个不生效的守卫好"，但那要求证明它不可推导）；而**(甲) 让"在链上有位置"成为一个图事实，正是"能推导的不该存"的方向**。

**在裁定前不动这 7 处**（已完成的 2 处在车道上，读数不变）。