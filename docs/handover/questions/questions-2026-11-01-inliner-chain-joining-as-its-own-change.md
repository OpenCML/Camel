# 内联的接链修法要不要单独立项

> **问什么**：S1 想把 `builder_control.cpp:2864`（BRCH → 臂入口）这条 ctrl 边删掉，删了之后
> `positions.cml` 少两行输出、十来个节点掉出计划。缺的那一步已经找到：`inlineCallableInDraft`
> 的接链块对"语句调用"从不执行（`callHasChainPlace` 只认 sync/order，而今天的链写成 ctrl），
> 补上之后三条判据全绿——但它在闸门上引出 47–97 条失败。
> **堵着谁**：S1 的删边，和"liveness 从区域来"这半条裁定能不能落地。
> **为什么我不自己定**：修法本身是对的，代价却不是一处边能带走的；要不要现在付、按什么顺序付，是你的事。

## 一、修法与三条判据（实测，改在 `src/runtime/draft_inline.cpp#inlineCallableInDraft`）

门槛从 `callHasChainPlace && bodyChain.heads > 1` 改成 `bodyChain.heads > 1`（今天语料里每个语句调用
的 `callHasChainPlace` 都是 false，这段因此**从来没有执行过**），再加一条回退：一片如果读不到体内任何
语句、只读调用方传进来的值，就写在"调用等的那条语句"之后（`ctrlPreds.front()`）。

删掉 `builder_control.cpp:2864` 后，三条判据全部成立：

| 判据 | 读数 |
|---|---|
| `positions.cml` 仍 10 行、次序不变 | `7 0 ok 3 value 4 value -1` … `3 null 4 -1` / `one -` / `big` / `value 2`，与 golden 逐字一致 |
| 掉出的 10 个节点回来 | `__root__` 109/112/113/116/120、`main` 129/132/133/137/141 全部 `used=1 live=1`（129 得到 `syncIn=[120]`） |
| `older_effect_place` 不变 | `before / older / 1 1 / after` |

证据行（删边后、修前）：那 10 个节点 `raw=1 rkind=4 (Inline) rparent=0 rowner=-1 control=0`——**它们
所在的区域没有 owner**；`109 syncIn=[] users=[112]`、`120 syncIn=[] users=[]`，是挂在链外的一片。

## 二、代价（闸门，`-Tier fast -Jobs 8`）

| 放开的口径 | 结果 |
|---|---|
| 门槛全放开（每个多片体都接链） | **97 fail** |
| 只放开"完全没有 wait 的调用" | **47 fail** |

失败面（47 那一档）：`gir.gir_snapshot`、`gir.inline_gir_snapshot`、`gir.rgir_snapshot`、`boundaries.agir`、
`boundaries.argir`、`specialize_growing_shape.*`、`graph.null_then*`、`equal_branch.graph.*`、
`control_data.graph`、`nn_grad_check.spliced_body_chain_is_one_chain`、`var_between_statements.opt*`、
`if_without_else.graph`、`entering.positions.fold.nvm`、`entering.positions.inline_opt.nvm`，以及 ONNX 的
`the model holds no Loop`。

## 三、根因：每个程序的根图都会被重写

`main` 接进 `__root__` 的那次调用**没有 wait**（它是根图里唯一的语句，`ctrlPreds` 为空），也不在
sync 链上。所以这条修法一放开，**每个程序**的 `__root__` 都被改写：多出 sync 链边、计划里多出节点，
上面那些锁定的图与 ONNX 形状随之变化。这不是"删一处边"的附属改动，而是一次全语料的链改写。

## 两条路

- **甲（我方默认建议）**：**不删边**；接链修法单独立项，按"会重写每个程序的根图"来估代价——先跑一遍
  全量 golden 重整的预演，数清哪些图会变、变在哪个方向，再决定要不要合并进来。S1 这半条裁定先停在
  "executor 已经从区域取 liveness"（已落：`232542e69`、`822ecbd02`）。
- **乙**：现在就付，把修法和 golden 重整一起提交。代价是一次 47–97 条图的形状变化，且 `nn_grad_check`
  那条"拼接体的链是一条链"的用例要重写预期——它正是这个机制的守门人。

## 请作者答一句

**甲的"先预演、再决定"，还是乙的"现在就付"？** 若选甲，S1 的下一笔就停在读数上（`REGION … in-plan ==
members` + 552 已进计划 `entering.positions.plan_regions`），不再动这条边。