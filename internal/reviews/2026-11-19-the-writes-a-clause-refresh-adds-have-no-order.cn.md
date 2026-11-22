# 刷新声明出的写没有序边（交给 effect order）
2026-11-19 ｜ 来自 `lane-wide` ｜ 对象：`EffectOrder`，`modules/tiles/src/region_outline.cpp:60-187`

## ① 现在的次序

`effects.finish(draft)` 在每张图各自重写完的那一刻被调（`:920`）；`declareBodiesOverDrafts(session)`
在所有图重写完之后才跑（`:930`）。刷新「值改名字的图的 clause」那段（`holdsWidened` + 上界
`copy->nodeCount()`，`:336-399`）在 `finish` **之后** ⇒ 调用点此时才把 `write stdout` 写进声明，
写边早已写完 ⇒ 新声明出的写没有任何边。

## ② 要的次序，以及为什么不能塞进 `finish`

刷新之后再走一遍 order，只对新声明出的写补边。不能塞进 `finish`：`finish` 是「这张图写完了」，而刷新读
`session.prepared()` —— 整个程序的编码；一张图的 clause 变宽会让它的调用方也变宽（`__root__` 这个遍根本
不重写、`finish` 对它从未跑过），还要一轮一轮直到不再变宽。

## ③ 证据（原样）

`gpu_scope.write_outside_a_sync_block_runs_through_the_pass`（`gpu_scope_unused_effect.cml`）：
「'tiles::compile' committed a graph whose conflicting effects are not ordered: node 2 of graph
'scaled' (:io/println), in one region of graph 'scaled', has no order against node 6 of graph
'scaled' (tiles:retain_preferred), and at least one of the two writes (write stdout / write unknown)」。

对照：上界退回 `draft.nodeCount()`（刷新第 1 轮就停），同一用例一个 `0x0400` 码都没有；`feat/modules/tiles`
18 红 → 1 红，`gpu-plans` 28 红 → 15 ⇒ 冲突是刷新真的声明出写之后才有的。
## ④ 危险点（只有你们能判）

`finish` 之上那句注释是「The graph is finished: …」。刷新在它之后又对同一张图开草稿、删掉一个值节点并写入
新节点（`:393-397`）。请判：这会不会把图重新变成「没完成」；`order()` 里以 `draft.alive()` 为前提的边会不会
指到被这次 `eraseNode` 删掉的 id 上。

## 默认建议（写死）

**默认建议**：按「**会**把图重新变成没完成」处理 —— 刷新之后再走一遍 order、**只对新声明出的写补边**
（不塞进 `finish`，理由见 §②）；并且 `order()` 里所有以 `draft.alive()` 为前提的边在 `:393-397` 那次
`eraseNode` 之后**逐条重查**，指向已删 id 的边丢掉，不许留悬空边。
**判据**：`gpu_scope.write_outside_a_sync_block_runs_through_the_pass` 不再出现任何 `0x0400` 码；
`-Plan feat/modules/tiles` 与 `gpu-plans` 的红数不涨。上界保持 `copy->nodeCount()` ——
**不许**退回 `draft.nodeCount()` 换绿：§③ 已经量明那 18→1 是靠「刷新第 1 轮就停」换来的假绿，
而冲突正是刷新真的声明出写之后才有的。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

建议答：两页（the-writes-a-clause-refresh-adds-have-no-order、the-call-site-refresh-in-the-tiles-pass）的默认都对。刷新之后再走一遍 order，只给新声明出的写补顺序边；`eraseNode` 之后丢掉指向已删节点的边；上界不许退回 `draft.nodeCount()`；内层扫描先看 `code()`，不要假设静态值一定是函数值。依据：图编辑设计稿第 8 节"编辑整个程序时改了某个被调函数的签名，草稿里调用它的地方必须同一次改好"；红线 4"不能让它们变成无序"；红线 6"函数体里访问的、图没有绑定的资源，都写在签名的效应里"。这套 clause 刷新在阶段 3b 由事实框架取代，可排到 tiles 迁到编辑会话时一起做。
