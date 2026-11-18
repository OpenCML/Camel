# 刷新声明出的写没有序边（交给 effect order 的那一份）

日期：2026-11-19 ｜ 来自 `lane-wide` ｜ 对象：`EffectOrder`（`modules/tiles/src/region_outline.cpp:60-187`）

## ① 现在的次序

`effects.finish(draft)` 在**每张图各自重写完的那一刻**被调（`:920`，在单图重写的尾部）；
`declareBodiesOverDrafts(session)` 在**所有图重写完之后**才跑（`:930`）。刷新里那条「值改名字的图的
clause」（`holdsWidened` + 我改的 `copy->nodeCount()` 上界）就在 `:336-399`，**晚于每一张图的 `finish`**。
⇒ 调用点这时才把 `write stdout` 写进自己的声明，而写边早已写完 ⇒ **新声明出的写没有任何边**。

## ② 要的次序

刷新之后再跑一次 order（只对**新声明出的写**补边即可）。**为什么不能在 `finish` 里顺手做**：
`finish` 是「这张图重写完了」的时刻，而刷新要读的是 `session.prepared()` —— **整个程序**的编码，
且一张图的 clause 变宽会让它的**调用方**也要变宽（`__root__` 这样的图这个遍根本不重写，`finish`
对它从来没跑过），还要一轮一轮直到不再变宽。这与「一张图写完就收边」不是同一个时刻。

## ③ 证据（原样）

`gpu_scope.write_outside_a_sync_block_runs_through_the_pass`（`test/cases/modules/tiles/gpu_scope_unused_effect.cml`）：

```
'tiles::compile' committed a graph whose conflicting effects are not ordered:
node 2 of graph 'scaled' (:io/println), in one region of graph 'scaled',
has no order against node 6 of graph 'scaled' (tiles:retain_preferred),
and at least one of the two writes (write stdout / write unknown): no data edge,
no sync edge, no order edge and no region's GATE input makes one wait for the other
```

对照：把那一行刷新上界退回 `draft.nodeCount()`（刷新在第 1 轮就停），同一个用例**一个 `0x0400`
码都没有**；`feat/modules/tiles` 18 红 → 1 红，`gpu-plans` 28 红 → 15。⇒ 冲突是刷新**真的**声明出
写之后才出现的，不是原有的。

## ④ 危险点（只有你们能判的一处）

`finish` 上面那句注释是「The graph is finished: …」。刷新在 `finish` **之后**又对同一张图开草稿、
**删掉一个值节点**并写入新节点（`:393-397`）。请判：**这会不会把图重新变成「没完成」** ——
尤其 `order()` 里以 `draft.alive()` 为前提的那些边，会不会指到一个已被刷新的 `eraseNode` 删掉的 id 上。