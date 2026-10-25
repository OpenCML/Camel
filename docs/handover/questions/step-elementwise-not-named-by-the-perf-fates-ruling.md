# `perf.step_budget` 的守卫不止 `step.count`：`step.elementwise` 既没被裁定点名，也不是测量（2026-10-25，h500 lane）

本文件只记事实与读数，不改裁定、不改代码语义。

## 一、事实

裁定（`docs/handover/reviews/questions-2026-10-24-evening.md` 的 `release-gate-inventory-eight-perf-plans-unclassified`
段，逐字；该段目前只在 `fsh-dev` 的 `b7e5f1baa` 上，`develop` 的同一文件里还没有）：

> | `step_budget`、`reduce_layout` | **拆文件**：只断言计数与性质的守卫（`step.count`、`reduce_layout.count`）移到各自的计划文件进
> `SLOW_PLANS`；测量那半留在原文件进 `FORBIDDEN`（发布前测量） |

裁定所据的清单（`docs/handover/questions/release-gate-inventory-eight-perf-plans-unclassified.md`，§四第二项）把
`step_budget` 写成"**各有一条**'只断言计数与性质、不断言时间'的守卫……**外加一条测量**"。实测不是：

```
$ node -e "import('./test/tools/toml-lite.mjs').then(m=>console.log(m.parseTomlFile('test/plans/perf/step_budget.plan.toml').tests.map(t=>t.name)))"
[ 'step.count', 'step.elementwise', 'step.budget' ]
```

计划文件自己的头注释（改动前）把前两条都叫作守卫、都写明不断言时间：

- `step.count`："the guard on the graph, not on the clock"
- `step.elementwise`："the guard on the kernels, also a count … **no duration is asserted**"
- `step.budget`："the budget itself … The absolute numbers are recorded, not asserted"

即 `step_budget` 是"两条守卫 + 一条测量"，`reduce_layout` 才是"一条守卫（`reduce_layout.count`）+ 一条测量"
（其头注释逐字："`reduce_layout.count` The guard … `reduce_layout.ops` The measurement"）。裁定表里两个文件名各
配一个守卫名，正是照那份不准确的清单写的。

## 二、本 lane 的读法与落地

**读法（按"只断言计数与性质"这条判据，而不是按括号里的两个名字）**：`step.count` 与 `step.elementwise` 都移进新文件
`test/plans/perf/step_counts.plan.toml`（`SLOW_PLANS`），`step.budget` 留在 `test/plans/perf/step_budget.plan.toml`
（`FORBIDDEN`）。理由：判据是"只断言计数与性质"；按字面只移 `step.count` 会让一条**计数守卫**落到 `FORBIDDEN`、
从此没有任何层跑它——那是本次改动里唯一会减少实际断言覆盖的选项。

**另一种读法（字面）**：只移 `step.count`，`step.elementwise` 与 `step.budget` 一起留在原文件。
代价：`step.elementwise` 不再被任何层执行（它的 CPU 读数 0.42 s，比 `step.count` 的 0.57 s 还便宜）。

回退成本：两种读法之差是 `step_counts.plan.toml` 里一个 `[[tests]]` 表——把它移回
`test/plans/perf/step_budget.plan.toml` 即可，两侧都不需要改别的文件（新文件的 `id`/`order`/头注释各一处）。

## 三、读数（Linux 主机，32 线程）

拆出的两半各自单跑一次的结果见本 lane 报告；`step.count` 与 `step.elementwise` 同文件同 case
（`test/cases/perf/step_budget.py`），两个 `args` 分别是 `count` 与 `elementwise`。