# 事务退回它分配的一切——这条判据在丢弃路径上测得到吗？

> 状态：**已裁（c）：撤掉这条用例**（2026-10-31，`fsh-dev = f24411f32`）。留档于此。
> 相关：`docs/handover/reviews/questions-2026-10-31-when-is-a-region-known-not-to-hold.md`

## 一句话

**"事务退回它分配的一切"这条判据，在"抽出 → 判定不成立 → 丢弃"这条路径上按 GC 读数恒真且无信息——要不要把它改写成"`encode` 之后丢弃 + 回收"那一笔，还是另外给草稿自己的存储立一个读数？**

## 测到的三个数（实测，不是推断）

```
during  allocations=2 live=0
before  allocations=2 live=0
after   allocations=2 live=0
```

一个事务建了 8 个 port 和一个静态值，再销毁：**采集器的 `allocations` 从 2 到 2，一动不动。** 所以 `after == before` 成立，但**`during == before` 也成立**——**这条用例测到的是"什么都没发生"。**

## 为什么会这样（代码级理由）

`extractRegion`（`modules/tiles/src/region_extract.cpp:146-152`）只做两件事：

* `GraphTransaction::open(id)` —— `make_unique<GraphDraft>`，**普通堆**；
* 往草稿自己的向量里 `addNode` / `appendStaticSlot`（`src/runtime/draft.cpp:2131-2145`——`materializeStaticValue` 就是 `appendStaticSlot` + `addNode`，**静态值存在草稿的 `staticSlots_` 里**）。

**整条抽出路径一次 `autoSpace()` 分配都没有。** 草稿造的一切是它自己的几个 vector，"退回"就是 `~GraphDraft` 释放它们——**`autoSpace().stats()` 永远看不到。**

## 唯一会让读数动的一步

`region_outline.cpp:391` 的 **`encode`** 把草稿变成 `GCGraph`，**那才进 graph space**。但今天"判定不成立"发生在 `encode` **之后**（`:401 compileTypes` → `:407`），所以**凡是被丢弃的 region 都已经付过 `encode` 的分配**；而 `encode` 之后的丢弃，`after` 要靠 **`collectGraphSpace()`** 才回得去。

**那时测的是"图空间回收"，不是"事务退回"——两笔不同的账。**

## 三个可选（请挑一个）

* **(b) 换题目**：用 `encode` 之后丢弃 + `collectGraphSpace()` 让 `after == before`，题名写明**这是图空间回收**。这是真实、有价值的账，但它不证明"事务拥有它造出的一切"。
* **(a) 换读数**：给草稿自己的存储立一个读数（`nodeCount` / 静态槽位数），测析构之后归零。**但"堆地址可复用"证明不了析构**，所以读数要挑得能证伪。
* **(c) 撤销这条用例**，把结论留在账页：**"草稿不在 GC 空间分配，所以'丢弃前后 GC 读数相同'在丢弃路径上恒真且无信息。"**

## 附：这一条判据的由来

作者 2026-10-31：「**待命名的事务被丢弃时，它分配过的一切（槽位、静态值）都由事务的析构负责退回。事务拥有它造出的一切。**」用例：**抽出一块 region、判定它不成立、丢弃它，前后 GC 空间的读数相同。**

**"事务拥有它造出的一切"这句话是对的，也已经在代码里成立**（`RegionDraft::graph` 就是那个事务，丢弃即析构）。**问题只在"用 GC 读数证明它"这一步**：那笔分配不在 GC 空间里。