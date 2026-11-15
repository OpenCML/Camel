# 压测收集要不要按墙钟配速？

- 日期：2026-11-15
- 车道：`lane-prlcount`（工作树 `D:\Projects\Camel-wt\prlcount`，基线 `f135bbb16`，`origin/develop`）
- 状态：**等回答**（默认建议见文末，写死了「现在不动」）
- 相关：原则 10.5；`docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md`

## 问题

调度器在每次收集之后按墙钟给下一次收集配速：
`src/passes/sched/taskflow/taskflow.cpp#TaskflowGcGate::collect` 记下这次收集的耗时，并把「下一次可以开始」
推到 `capUntilNs_ = now + 2 × 这次耗时`。这段时间里到的轮询一律跳过，只计 `cappedPolls`，不收集。

这件事在普通运行里是对的：收集是回收的机会，早一点晚一点不影响语义，而「收集完了立刻再收一次」只是在
烧时间。**但 `--gc-stress` 要的不是机会，是工作量。** `test/plans/feat/mm/gc_collections.plan.toml` 的头
已经写明这一点，并给它的 prl 臂加了 `gc_stress=true`（`std::prl gc_stress=true`，也就是让压测收集不被配速）。
`test/plans/feat/compile/ref.plan.toml` 里量计数盒子的那几条 prl 臂**没有**这个参数，于是压测要求的收集被配速
吃掉了一大半：本用例每轮 12 次轮询里 **9 次**被跳过，`pending_polls=12 collections=6 capped=9`。

## 量到的（同一二进制、同一用例、同一组旗标）

`test/cases/compile/ref/ref_boxes_made_per_call_ten.cml`，`--gc-stress alloc=7,safepoint=3,kind=both
--gc-check foreign,verify=both --gc generations`，`std::prl`：

| 收集配速 | 轮数 | `collections` / `capped` | `live` 读数 |
|---|---|---|---|
| 开（今天 `ref.plan.toml` 的形状） | 15 | 6 / 9（14 轮）、12 / 6（1 轮） | `0` ×13、`3` ×1、`6` ×1 |
| 关（`std::prl gc_stress=true`） | 15 | 15 / 0 | `3` ×14、`4` ×1 |

**关掉配速只是把抽签收紧，不是把它定住。** 读数的不确定度来自「每次收集落在哪个程序点」，不是「收集跑了几次」：
15/15 轮里 `pending_polls` 与 `collections` 完全相同的那些轮，`live` 仍然读出 0、3、6 三个值
（`docs/handover/reports/2026-11-15-prl-counted-boxes-return/H-prl-pollcounts.tsv`）。所以这一条**不是**这条
车道那道红的修法，它只是同一片地上的另一块石头。

## 关掉它要付什么

- **波及面**：`gc-stress` 出现在 20 个 plan 文件里（`feat/mm`、`feat/para`、`feat/linear`、`feat/modules`、
  `feat/consistency`、`feat/compile`、`feat/tools`、`gpu`）。关掉配速会改其中**所有 `std::prl` 臂**的收集次数，
  于是它们的读数、耗时与「第几次收集看见什么」全都变。核对这些需要 `feat/mm` 与 `feat/para` 的全网格，
  而这两组正是 `--changed` 不一定选中的那类计划。
- **判据面**：压测收集一对一服务之后，一次压测运行会跑完它要求的每一次收集。这会让「压测」更接近它的名字，
  也会让一些今天靠配速偶然躲过去的读数撞上面（`gc_collections` 之外没人量过）。

## 默认建议（写死）

**在第 1 项（`var` → SSA）落地之前不动它。**

理由不是「改了会红」，而是**改了会让两边的红互相掩盖**：第 1 项是一次语义大改，它的红会以「图不一样了 →
回收看见的东西不一样 → 读数不一样」的形状出现；如果同一个时间窗口里又把收集配速改了，同一格红就有两个都说得通
的原因，谁也分不清是 SSA 改写带来的，还是收集次数带来的。收集配速是**仪器**的一部分，仪器在测量期间不换。

顺序写在这里，等第 1 项落地后由点名的人执行：先在第 1 项的树上重跑本页那两组 15 轮（得到「语义改完之后，
今天这份配速下的分布」），再单独一笔关掉压测配置的配速，跑 `feat/mm` + `feat/para` 全网格，两次读数叠在一张表上
比较。在那之前，`ref.plan.toml` 的 prl 臂保持现状：它们的红是计数依附于回收时机的红，不是配速造成的红。
## 决策者答复（2026-11-15）

同意默认：现在不动配速。读数的不确定来自收集点落在哪里，不来自收集次数；计数做准之后，计数盒子的读数不再经过收集器，这件事与配速无关了。配速本身是收集器的调优，普通运行下合理；`--gc-stress` 要不要绕过配速，等计数做准之后，以 `feat/mm` 的收集用例为准再看，不为计数盒子去改。
