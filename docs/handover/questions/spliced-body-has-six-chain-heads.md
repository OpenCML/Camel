# 内联体有六个段头时，`5.7` 那条链该接哪一个

## 先更正一个分母

`runner.backend_matrix` **占一个 `fail` 位**。全量 `total 7475` 的真实分母是"6 条已知红 ＋ 1 条在 `native_state_checkpoints`、`device_region.nvm`、`runner.backend_matrix` 之间浮动的"（三者都会在负载下自红或自绿）。此前"名单与基线逐条相同"的说法是错的，在此更正；下面所有读数都按这个口径看。

## 我量到的：一次拼接，体里六个段头

`test/cases/modules/autodiff/nn_grad_check.cml` 在 `std::macro std::opt growth_budget=512 std::nvm` 下，`src/runtime/draft_inline.cpp#devirtualizeStaticCallInDraft` 每次拼接打一行（只打印，不改语义）：

```
call=6  chainPlace=1 bodyHasChain=0 heads=0 tails=0 syncIn=1 syncOut=1 ctrlPreds=1 orderIn=0
call=11 chainPlace=1 bodyHasChain=0 heads=6 tails=6 syncIn=0 syncOut=1 ctrlPreds=1 orderIn=0
call=16 chainPlace=1 bodyHasChain=0 heads=0 tails=0 syncIn=1 syncOut=1 ctrlPreds=1 orderIn=0
call=21 chainPlace=1 bodyHasChain=0 heads=6 tails=6 syncIn=0 syncOut=1 ctrlPreds=1 orderIn=0
call=26 chainPlace=1 bodyHasChain=0 heads=0 tails=0 syncIn=1 syncOut=1 ctrlPreds=1 orderIn=0
call=31 chainPlace=1 bodyHasChain=0 heads=6 tails=6 syncIn=0 syncOut=1 ctrlPreds=1 orderIn=0
call=36 chainPlace=1 bodyHasChain=0 heads=0 tails=0 syncIn=1 syncOut=1 ctrlPreds=1 orderIn=0
call=41 chainPlace=1 bodyHasChain=0 heads=6 tails=6 syncIn=0 syncOut=1 ctrlPreds=1 orderIn=0
call=54 chainPlace=1 bodyHasChain=0 heads=6 tails=6 syncIn=0 syncOut=1 ctrlPreds=1 orderIn=0
```

在意的五次 `check(...)` 拼接是 `call=11/21/31/41/54`：**`bodyHasChain=0`，`heads=6`，`tails=6`** —— 被拼入的体里六条语句**各自成段**，没连成一条链。它们同时是 `syncIn=0`（调用点自己是链头），`ctrlPreds=1`。

`bodyHasChain=1` 的拼接在这条命令里**一次都没有出现**，所以"由 `bodyHasChain` 那一支把体链接回调用点链位"的改动在这条用例上**永远不触发**——这解释了前一次改动为什么一点效果都没有。

`heads=6` 时拼接走 `src/runtime/draft_inline.cpp#spliceChainAnchor` 那一支：只给体造一个锚点门、把调用点的位置交给它，体的六段没有一段接到调用者的链上。

## 随后两次实测

| 改法 | 读数 |
|---|---|
| 体链头接回调用点的链位（只在 `bodyHasChain` 为真时） | `fail 8`，`nn_grad_check` 仍 `got 1`；原因就是上面那句：分支不触发 |
| `src/runtime/draft.cpp#takeStatementPlace` 里，`preds` 为空时从同区 control 前驱接管链位（并给该前驱打语句标，不打会直接报边不变量违例） | `total 7475, pass 7458, fail 7`，但 **`nn_grad_check` 从 `got 1` 掉到 `got 0`**（打印一条都没有）、`var_between_statements` 六条全绿、`static_branches.simplify` 仍红 ⇒ **更差，已回退** |

## 这属于 `5.7` 没写的一种形状

`docs/design/effects/edges.cn.md` 5.7 写的是"结果代替原调用接回同步链，头尾都接"，那是以**一个体、一条链**为前提的。探针给的形状是**一个体、六条链**（六个段头）。删链头节点时每段的头被清空，正好落成前一次探针数到的十一次清空，`main` 图的可达从 364/386 掉到 56/321——上游语句于是整段不执行。

## 要问的一句

一个内联体里有六条各自成段的语句时，`5.7` 那条链应当接哪一个：

- 六个段头**全接到调用点的链位上**，让体在调用者的块里变回一条链；还是
- 保持六段，由调用点分别与它们相接（那是另一种形状，`5.7` 没有写）？

这是拼接与链的语义决定，我不动代码。