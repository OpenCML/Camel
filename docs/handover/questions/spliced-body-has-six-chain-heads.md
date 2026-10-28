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

`docs/design/effects/edges.cn.md` 5.7 写的是"结果代替原调用接回同步链，头尾都接"，那是以**一个体、一条链**为前提的。

**而在拼接之前直接量被调方的图，体里一条 sync 边都没有**（`std::macro std::jgir`，未跑内联）：

```
main              nodes=76  syncEdges=19
check             nodes=51  syncEdges=9
conv_input        nodes=9   syncEdges=0
conv_kernel       nodes=9   syncEdges=0
conv_bias         nodes=9   syncEdges=0
pools             nodes=13  syncEdges=0
embed             nodes=8   syncEdges=0
xent              nodes=3   syncEdges=0
grad(conv_input)  nodes=17  syncEdges=0   （六个 grad 图同样为 0）
```

被内联的体不是"六段链"，而是**纯表达式图**，没有链可言。所以那六个"段头"**在调用点这一侧，不在体里**：它们 `region=0`、各带一个块内 sync 后继、`ctrlIn=0`、`orderIn=0`，是内联过程中被标成语句、却没有 sync 前驱的节点。删链头节点时这些头被清空，正好落成前一次探针数到的十一次清空，`main` 图的可达从 364/386 掉到 56/321——上游语句于是整段不执行。

这也解释了上的第二次实测为什么只能接 `control`／`order` 前驱：**体里没有边可读**，没有顺序可继承，能接的只有别的种类，接到别处就把 `var` 语句的顺序打乱了。

## 要问的一句

**被内联的函数体是纯表达式（无链），它在调用者的 `sync` 块里是否占据一个语句位置？**

也就是 `5.7` 的头尾接法在"体无链"时退化成什么：

- 让体的**结果节点**顶替调用点的链位（前驱 → 结果 → 后继），体这次计算随结果一起被算；还是
- 这次计算**根本不算语句**，只保证结果在使用之前算好？

这是拼接与链的语义决定，我不动代码。