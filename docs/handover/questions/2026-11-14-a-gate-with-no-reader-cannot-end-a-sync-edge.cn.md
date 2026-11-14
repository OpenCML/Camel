# 一个既没有值读者、也没有同步后继的 `GATE`，怎么结束一条同步边？

## 问什么

作者任务书 R1 的保活规则是：一次改写取走某个节点最后一条读取时，**可能出故障的计算交给 `GATE`**
（数据口读原值，同步前驱是那个节点），不用 `:op/keep`，也不借用控制边。这条规则在本批里落不下去，
卡在一个很具体的地方：

`src/runtime/graph_edges.cpp#canEndSyncEdge`只认两种 `GATE`——有同步后继的，或者值走到本块出口的
（`reachesOwnBlockValue` 跟随 `norm`/`with` 使用者）。而 `fold` 把内层分支唯一的读取取走之后，
那条折叠出来的**值已经没有人读**了：既没有值读者，也没有同步后继，于是 `setSyncPredecessor` 立不起来，
`keepThroughValue` 只能把刚建好的 `GATE` 再删掉，保活落空。

于是现状是：`:op/keep` 已全删，保活暂时仍靠**控制边残留**（被剪掉的分支把它等过的节点继承给接手它位置的代码，
`CTRL` 边算作一次使用）。这与 R1 的裁定冲突——裁定说不要借用控制边；但把那条边删掉，故障就静默消失。
要作者定的是：**一个没有值读者的 `GATE` 该不该、以及怎样结束一条同步边**；这一层属于 R3 的效应/同步语义，
不该由车道自己发明第三种机制。

## 证据（原文照录）

程序 `test/cases/opt/effects/fold_keeps_inner_fault.cml`：内层条件是 `1 / z > 0`（`z = 0`），
内层两支都是 `3`，`fold` 把外层的比较折成 `false`、取走内层分支唯一的读取。

| 树 | 命令 | 读数 |
| --- | --- | --- |
| 基线 `bd3396fea`（`/home/zjwei/camel-work/gates`） | `camel fold_keeps_inner_fault.cml std::opt::fold std::nvm` | `error[RuntimeDiag::DivisionByZero] ... :18:24`，**exit 1**，故障在 |
| 删掉控制边残留之后 | 同一命令 | 打印 `4`，**exit 4**，**故障消失**（回归） |

第二行就是本页的由来：残留那条 `CTRL` 边一直在替保活干活——`orderedSources` 在结构节点（`BRCH`/`JOIN`）处停下，
永不看进分支内部，所以残留一删，内层条件那条除法就彻底没人读了。**控制边残留不是可以顺手删掉的垃圾，
它是当前唯一的保活。**

两次「走进结构去找那条读取」的尝试，两次都比保留残留更差：

| 尝试 | 结果 |
| --- | --- |
| 走进分支臂（`JOIN`/`BRCH` 的 `with` 口） | 20 棵图红：`GATE 1332 reads OPER 383 (tensor:multiply), which lies in an arm of BRCH 377 that may not be taken` |
| 只走结构自己的输入（`norm`/`ctrl`） | `static_branches.simplify` 丢一个 effect：输出少一行 `before call` |

## 现状（本批交付的形状）

- `:op/keep` 已全删，`git grep ':op/keep' -- src include stdlib modules` 零命中。
- 保活暂时仍靠那条残留 `CTRL` 边；`pruneBranch` 的控制边残留**按原样保留**（本批的第二笔提交把这一半撤回了）。
- `test/plans/feat/opt/effects_kept.plan.toml` 里那两条结构性读数（画里没有 `CTRL`、运行里没有内层三个节点）
  以 open 记录、**未断言**；断言的是行为：`fold_keeps_inner_fault` ×4 后端 12 条绿，口径是
  「exit 1 + `RuntimeDiag::DivisionByZero`」，不咬定经不经过 `GATE`。
- 本批读数：`feat/opt` + `modules/operator_semantics` 1194/1194、exit 0。

## 默认建议（按它开工，不等回复）

**按现状保留**（故障不丢、`:op/keep` 已删、残留边不动），把「无读者的 `GATE` 如何结束同步边」
留作设计问题，由作者在 R3 的效应/同步语义里定。**不要**再由车道自己发明第三种机制
（既不是 `:op/keep` 那种算子，也不是借用控制边）：前两次尝试都证明，猜出来的机制会以
「跨臂读取」或「丢一个 effect」的形式付出代价，而这两种代价都比一条待裁的残留边更贵。