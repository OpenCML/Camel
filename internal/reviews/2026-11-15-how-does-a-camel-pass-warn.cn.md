# 一个用 Camel 写的 pass，怎么报告一条不致命的诊断？

**状态**：待裁。本页从 `opt::cse` 的一条红上发现，问题本身比那条红值钱。

## 1. 发现

`opt::cse`（`stdlib/opt.cml`，用 `graph` 模块写的遍）在合并掉一个持位节点之后，应当把它的
位置交给后继；C++ 侧同位置做这件事（`placeLostStatements`）。为了让 Camel 侧也能写出那条边，
本轮给模块加了 `set_sync_predecessor(t, n, pred)`（`stdlib/graph.cml`、`modules/graph/txn.cpp`）。

排错时需要一个最小的问题：**先在 pass 里面说一句话**，看看它去哪了。结论：

1. **`println` 在 pass 里不进程序输出。** 在 `lost_place` 里打的 `println('PROBE ...')` 一条都没出现
   （pass 的输出被管线接走，程序 stdout 只剩 `std::gir` 之类翻译遍的产物）。
2. **唯一能落到 stderr 的通道是「让程序失败」**：Camel 没有 `throw`（`docs/design-principles.cn.md`
   225：`error('Kind', 'msg')` 是一个值），所以只能
   `return error('ProbeLeaf', 'graphs=6 withlost=1')`，它以
   `RuntimeDiag::UnhandledError: ... ProbeLeaf: graphs=6 withlost=1` 的形式出现。
3. **警告通道只在 C++ 侧**：`raisePassWarning`（`src/execute/pass.cpp:922`）的调用者是
   `modules/graph/txn.cpp:345` 那个 kernel（`eval` 把求值故障当警告报出来），`graph` 模块**没有**
   把这条出口暴露给 Camel。

于是：**任何「我想说一句、但不想让程序失败」的诊断，在用 Camel 写的 pass 里表达不出来。**
这一轮就是被它逼到拿 `UnhandledError`（一个致命错误）当调试通道用的——那不是它该有的用法：
一个诊断把整个管线打断，读者会以为程序坏了。

## 2. 问题

一个用 Camel 写的遍（`opt::fold`、`opt::cse`、`opt::dce`，以及任何以后用模块写的遍），
**怎么报告一条不致命的诊断**？

今天的三条路都不对：

| 想做的事 | 今天能用的 | 为什么不行 |
|---|---|---|
| 说一句话，程序照跑 | `println` | 不进程序输出，读不到 |
| 说一句话，程序照跑 | —— | 没有别的出口 |
| 说一句话，程序失败 | `error('Kind', 'msg')` | 诊断变成致命错误，管线中断 |

## 3. 默认（本页写死）

**先给 `graph` 模块加一条 `warn(t, msg)`，与 `raisePassWarning` 同一个出口**
（`RuntimeDiag` 里选一条「遍的警告」kind，位置取 `t` 所编辑的图），**本轮只在 `cse` 的探针里用它，
不推广**：等 `warn` 的形状被一两次实际使用确认之后，再决定要不要给 `Txn` 之外的入口
（例如给遍一个不依赖事务的 `warn(msg)`）。

理由：

1. 这不是新机制，是把已有的出口（`raisePassWarning`）接到 Camel 侧；C++ 侧今天就是这么报
   `ConstantExpressionFaults` 的，语义有先例。
2. 它不需要改语言（没有 `throw` 是设计原则的决定，本页不碰它）：`warn` 是一个普通的模块函数，
   像 `set_sync_predecessor` 一样。
3. **不推广**是因为还不知道形状：一条遍的警告要不要带节点（位置）、要不要在事务里、要不要去重，
   都要一两次真实使用才定得下来。

## 4. 缺口从哪来（为什么值得记）

本轮的红是「`opt::cse` 与 `std::opt::cse` 的产物差一条 `SYNC` 边」。排错时**最省事的一步是
「在判据里打一行」**，而这一步在 Camel 写的遍里做不到；于是每一轮排错都要
「改判据 → 让 pass 失败 → 读 stderr → 改回来」，一次构建一轮。**这条缺口会把每一次排错都变贵。**
它比那条红本身更值得修。
## 决策者答复（2026-11-15）

同意加 `warn`，与 `raisePassWarning` 同一个出口；形状定为 `warn(t, n, msg)`，带一个节点作位置（诊断要能指到图上的地方），不带节点的写法暂不提供。

另一半也要改：遍里的 `println` 不能悄悄消失（原则 3.1"效应不静默消失"）。遍的标准输出被管线接走，是因为翻译类的遍（`std::gir` 之类）用它交产物；改写类的遍没有这个用途，它的控制台输出转到进程的标准错误，前缀写明是哪个遍。归第 2 路，排在优先级一之后。
