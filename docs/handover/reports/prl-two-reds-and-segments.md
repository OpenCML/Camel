# prl 的两条红：它们现在在哪，以及分段怎么读

**结论：红只出现在 `lane-eff-rule` 的中间图上；那条车道的建图改动未落地，`develop` 上两条都是
PASS，没有可修的红。** 这一段记下来，是为了将来有人重建那条线时知道该盯哪两条。

**并且 08:30 那句“问题在 `prl` 的分段”要按下面的打印修正**：`sort_rebinding` 的 `main` 是 1 段
0 链 37 个算子 inline，`gpu_fallback` 的 `main` 是 1 段 0 链 14 个算子 inline——这两个程序的图上
只有一条全序，**根本没有可切的东西**。所以不是“它分不出段”，而是这两个程序只排得出一条全序；
08:49 说的“没守住同步链和顺序边”是行为缺陷，与分段无关。

## 两条用例与出处

| 用例 | 计划 / 程序 | 当时的现象 |
|---|---|---|
| `gpu_preference.no_device.prl` | `test/plans/feat/modules/tiles.plan.toml` / `test/cases/modules/tiles/gpu_fallback.cml` | 打印顺序与 `nvm`/`fvm`/`jit` 不同 |
| `new_values.sort_rebinding.prl` | `test/plans/feat/consistency/new_values.plan.toml` / `test/cases/consistency/new_values/sort_rebinding.cml` | 零输出（期望 81 字节，实得 0，`exit 0`） |

- `origin/fsh-dev@766e3f72f`（08:49）：“`prl` 那两条（`gpu_preference.no_device` 的打印顺序错、
  `sort_rebinding` 零输出）都是 `prl` 没有守住同步链和顺序边，属于行为缺陷，不能重录。”
- `origin/fsh-dev@ea5845184`（08:29，即 08:30 的裁定）：“`prl` 却一行没跑，问题在 `prl` 的分段或
  调度。打出分段结果，看是‘分不出段’还是‘段没有被调度’。”

## 读数（`origin/develop` = `2e07e43cc`，主机，`-Jobs 1`）

| 计划 | total | pass | fail | skip |
|---|---|---|---|---|
| `feat/consistency/new_values.plan.toml` | 51 | 51 | 0 | 0 |
| `feat/modules/tiles.plan.toml` | 101 | 96 | 0 | 5 |
| `feat/para`（`core` + `handoff`） | 63 | 63 | 0 | 0 |
| `smoke` fast | 10 | 10 | 0 | 0 |
| `feat/linear/loops.plan.toml` | 144 | 144 | 0 | 0 |
| `feat/mm/gc_collections.plan.toml`（milestone，含 22 条 `_prl` 臂） | 73 | 73 | 0 | 0 |
| `gpu` fast | 8 | 8 | 0 | 0 |
| `feat/linear` fast | 874 | 872 | 0 | 0（known 2） |
| 全量闸门（`-Plan all -Tier fast -Jobs 8`） | 7898 | 7889 | 0 | 7（known 2） |

`lane-eff-rule@4d63c8fe9` 上同样的两个计划：151 total / 146 pass / 0 fail / 5 skip，两条均 PASS。

## 根因归属：读边，不是分不出段

- 分段已经读同步边（`src/passes/sched/taskflow/taskflow.cpp#buildStructuredPlan`，块可达性把
  `uses().sync(ref)` 与 `ctrlOutputsOf` 并列，`taskflow.cpp:897`），顺序边在
  `taskflow.cpp:2443`；拓扑序三种边都读（`src/execute/exec_plan.cpp:643`）。
- `buildStructuredPlan` 抛 `it has a loop`（`taskflow.cpp:747`）只覆盖循环，是 prl 唯一的“整图退成
  逐节点”路径，与这两条不是一回事：一个带循环的函数本来就不进并行计划，而这两条的程序没有循环。
- “分不出段”与“退成逐节点”是两件事，分界线是 `GraphPlan#refusal`：计划现在把拒绝的原因记在图上，
  `segments=true` 在没有任何段时先打原因（没有段 = 这张图不按段跑），有段时才打段。以前这条原因只在
  `diag=true` 时写一行，其余时候看不见。

## 现在怎么读分段：`std::prl segments=true`

声明在 `stdlib/std.cml` 的 `taskflow` 的 `with` 块；实现是
`src/passes/sched/taskflow/taskflow.cpp#TaskflowExecSchedPass::printSegments`。打开后按计划打印：
总段数/链数/算子数，每段的条目数与链数、它是内联还是跑成任务图、它挂在哪个段的哪一项之后，每条链的
条目数与链间依赖，最后逐个算子打印它落在哪一段。

`sort_rebinding`（`std::prl segments=true`）：

```
[taskflow] segments 'main': 1 segment(s), 0 chain(s), 37 operator(s)
[taskflow] segments 'main' segment 0 (top): 44 item(s), 0 chain(s), inline
[taskflow] segments 'main' oper :var/new #2: segment 0, inline
[taskflow] segments 'main' oper :io/println #19: segment 0, inline
```

`gpu_fallback`（`CUDA_VISIBLE_DEVICES=-1 std::prl segments=true`）：

```
[taskflow] segments 'main': 1 segment(s), 0 chain(s), 14 operator(s)
[taskflow] segments 'main' segment 0 (top): 17 item(s), 0 chain(s), inline
[taskflow] segments 'main' oper tiles:available #0: segment 0, inline
[taskflow] segments 'main' oper :io/println #1: segment 0, inline
```

两条都是**一段、零链、内联**：整张图是一个全序，没有可并发的段，也就没有“切了段却没被调度”的
空间。这就是 `prl` 守住同步链与顺序边的正面证据——不是“没红过”。

## 对照：有并行结构的程序不是一段

为了排除“这个参数永远只会打出 1 段”，把 `test/cases/para/core/` 下的程序扫了一遍（`-t 8`，只列有
链的图）。`prl` 照样切得出多段多链：

```
== concurrent_calls.cml   'both':        1 segment(s),  3 chain(s), 1 operator(s)
== concurrent_recursion.cml 'tree':      3 segment(s),  3 chain(s), 4 operator(s)
== merge_sort.cml         'mergesort':   3 segment(s),  5 chain(s), 15 operator(s)
== merge_sort_small.cml   'mergesort':   3 segment(s), 11 chain(s), 9 operator(s)
== concurrent_alloc.cml   'mergesort':   3 segment(s),  5 chain(s), 9 operator(s)
== sync_order_with_tasks.cml 'independent': 1 segment(s), 3 chain(s), 1 operator(s)
```

`merge_sort.cml` 的 `mergesort` 一段原文：

```
[taskflow] segments 'mergesort': 3 segment(s), 5 chain(s), 15 operator(s)
[taskflow] segments 'mergesort' segment 0 (top): 10 item(s), 0 chain(s), inline
[taskflow] segments 'mergesort' segment 1 (arm of #6): 1 item(s), 0 chain(s), inline, after segment 0 item 3
[taskflow] segments 'mergesort' segment 2 (arm of #6): 8 item(s), 5 chain(s), as tasks, after segment 0 item 3
[taskflow] segments 'mergesort' segment 2 chain 0: 2 item(s)
[taskflow] segments 'mergesort' segment 2 chain 3: 2 item(s)
[taskflow] segments 'mergesort' segment 2 chain 0 -> 1
[taskflow] segments 'mergesort' segment 2 chain 1 -> 4
[taskflow] segments 'mergesort' oper :struct/len_arr #8: segment 2 chain 0
[taskflow] segments 'mergesort' oper array:merge_sorted_l #23: segment 2 chain 4
```

即：两个切片的臂各成一条链、合并等它们（`chain 1 -> 4`），段 2 跑成任务图。所以“1 段 0 链”是这两个
程序的形状，不是这个参数的退化。

## 没做

没有提交任何“修红”的改动（两条在 develop 上不复现，也没有放宽任何断言、没有动金标）。