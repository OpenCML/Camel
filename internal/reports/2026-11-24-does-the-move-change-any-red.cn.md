# 那笔移层改掉了几条红？——慢层 169 条与底树快层 1,384 条逐条比

**问句**：`a5cd47367c` 把 257 条用例移进慢层，它是"只改选择"还是也改了读数？这一页是那一笔的
验收：把**移层前后同一棵树的两份红表**逐条对上，三类分开报。

## 口径（两份读数怎么写出来的）

| | 移层前（before） | 移层后（after） |
|---|---|---|
| 命令 | `camel-remote.ps1 -Plan test/plans -Tier fast -Jobs 8` | `camel-remote.ps1 -Plan test/plans -Tier slow -Jobs 8` |
| 计划内容 | **底树**（63 个计划文件 `git checkout e66efb7a7b -- test/plans`） | 本分支（`a5cd47367c` 之后） |
| 产品代码 | 两份完全相同（同一 commit、同一二进制） | 同左 |
| 起起来的 | 387 个计划、**9,916 条** | 74 个计划、**560 条** |
| 结果 | `pass 8478, fail 1384, skip 24, known failures 30`，wall 264.8 s | `pass 295, fail 169, skip 95, known failures 1`，wall 419.5 s |
| 主机负载 | 1 m 11.6 → 16.1（读数含竞争，所以下面按**成因**分，不按条数比） | 1 m 10.9 |

两份逐条表就是这一页的证据，跟着提交走，下一个合并位可以直接读：

- `2026-11-24-the-slow-layer-population.tsv`：560 行，一行一条用例，列 `class / plan / case /
  base_fast_layer / slow_layer / cause_class / first_line_of_the_red`；
- `2026-11-24-the-base-fast-layer-reds.tsv`：1,384 行，列 `plan / case / cause_class / first_line`。

分类只在**移层后真的跑起来的 560 条**里做。一条用例在 before 里没出现，不是"它不红了"，而是它
当时就已经在慢层（`tier = "slow"`），底树快层从来没跑过它——这条纪律是整页的支点，否则"不红了"
与"没跑"会混成一个数。

## 三类

| class | 条数 | 它是什么意思 |
|---|---:|---|
| `red-in-both` | **123** | 移层前红、移层后红，同一句失败原文。**树的红，不是这笔移层造成的** |
| `red-now-was-already-slow` | **46** | 今天红，但底树快层没跑过它（当时已是 slow）。**这笔提交碰不到它** |
| `green-now-was-red` | **0** | 移层前红、现在不红——**0 条**，没有需要解释的"消失" |
| `red-now-was-green` | **0** | 移层后新出现的红——**0 条** |
| `green-in-both` | 275 | 两份读数里都绿 |
| `green-now-was-already-slow` | 116 | 当时已在慢层，今天绿 |

⇒ **169 = 123 + 46**，两类都是树本来就红的。**移层没有让任何一条由红转绿，也没有让任何一条由绿
转红**：`green-now-was-red` 与 `red-now-was-green` 都是 0，而这两个零是量出来的，不是推断的。

**"不红了"的那一类为什么是 0，以及它为什么本来最该看**：一条用例"不红了"可能是三种事——修了、
跳过了、或者现在**没有任何一层跑它**。第三种最容易被当成好消息。这里它是 0，为了让这 0 不
和"没跑"混：169 条全部是 `--tier slow` 起起来的（`[SUMMARY] total: 560` 就是这 560 条），74 个
文件全在 `SLOW_PLANS` 里，`--tier all` 也选它们（`test/plans/feat/gate --tier all` 起 13 条、
13 过），`planInventoryProblems()` 报 0 条问题。

## 那 169 条按它自己那句话分

| 成因 | 123 条（两边都红） | 46 条（当时已在慢层） |
|---|---:|---:|
| `range-takes-no-higher-order-function`（`(Range)` 上还没有高阶函数） | 39 | 23 |
| `golden-behind-the-recording`（金标/snapshot 落后于录制） | 32 | 0 |
| `exit-1-with-no-line-saying-why`（退出 1、没有一句说明） | 19 | 18 |
| `to_array-not-in-the-compiler`（`to_array` 还没进编译器） | 13 | 3 |
| `unclassified` | 11 | 0 |
| `parser-rejects-the-old-spelling`（`=>` 之前写返回类型） | 4 | 0 |
| `an-expected-line-is-missing` | 3 | 0 |
| `never-reached-its-verdict-line` | 2 | 2 |

⇒ 169 条里 **78 条是 `Range`/`to_array`**（也就是那条还没落地的 lane），**32 条是金标落后于录制**，
**37 条是"退出 1 却没有一句说明"**——最后一类要有人读原文才说得清，它也是最像"产品真坏了"的一类。

## 底树快层那 1,384 条（给 integration 当"上一份红表"）

9,916 条里 **1,384 条红 = 14%**。按成因：`range-takes-no-higher-order-function` 369、
`unclassified` 318、`exit-1-with-no-line-saying-why` 279、`to_array-not-in-the-compiler` 212、
`golden-behind-the-recording` 116、`an-expected-line-is-missing` 56、
`known-fail-mark-changed-its-mind` 6、`printed-an-error-it-forbids` 13、
`parser-rejects-the-old-spelling` 10、`never-reached-its-verdict-line` 5。
按计划最多的是 `consistency.attempt` 119、`consistency.faults` 100、`consistency.faults_optimized` 100、
`opt.gate_value` 60、`modules.stdlib_faults` 48、`examples` 42、`compile.rulesets` 35、
`modules.json_value` 33、`modules.autodiff` 31、`opt.splice_place` 29、`consistency.values` 28、
`para.core` 28、`modules.tensor` 27。

**这张表比 batch-1 报告更适合当"逐条比"的基线**：batch-1 的 13 次与 324 次记录只公布了 11 个名字
和按计划的红数，没有逐条红表，所以拿它做不了逐条对齐；这一份是同一棵底树、同一个二进制、整层
跑一遍的读数，谁拿到它都能在下一轮做 `comm -13`。

两条读这张表要知道的事：这次 before 跑的 provenance 是 `dirty: 63 files`（只有 63 个计划文件回到
底树内容，产品代码与本分支一致），所以它不是一次"干净的跑"；另有 52 行 `plan` 列是 `?`，因为
runner 在收尾的名单里印那些失败时不带日志路径（其中 6 行是 `known_fail` 标记失效——标记写的
消息与今天的失败对不上，这件事本身值得一条）。