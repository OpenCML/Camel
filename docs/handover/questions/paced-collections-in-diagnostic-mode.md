# 诊断模式的集合回收要不要限速

Author: 协调者（合并闸门），2026-10-24。Status: open — 需要负责人裁定。测量由 `handover/h379-prl-arms` 在 `handover/h379-prl-arms`（`b7a1312a1`）给出，全部数字可在提交信息与 `test/plans/feat/mm/gc_collections.plan.toml` 的头部复核。

## 事实

`std::prl` 走 taskflow，集合回收由 `src/passes/sched/taskflow/taskflow.cpp#TaskflowGcGate::collect` 限速：下一次不早于上一次耗时的两倍开始（`capUntilNs_.store(end + 2 * (end - begin))`）。后果是 `gc_collections.plan.toml` 的 24 个 `_prl` 臂**作为程序测试不空**（算子调用 6 555、分配 5 772、stdout 与 fvm/jit 一致），**作为"回收压力"守卫却是空的**：

| 运行 | CPU | 墙钟 | 集合数 | gate 限速 | 分配器 major |
|---|---|---|---|---|---|
| `_prl`（限速，改前） | 0.02 s | 0.14 s | 14 | capped 3525/3532，in_flow 0 | 13 requested 13 deferred 1947 |
| `nvm` | 10.26 s | — | — | 无 gate | 4235 |
| `fvm` | 8.96 s | — | — | 无 gate | 3538 |
| `jit` | 8.91 s | — | — | 无 gate | 3538 |

所以那 24 个臂过去**没有在压回收**，问题出在调度器的安全点策略，而不是收集器够不着。

## 已做的修（施工方的选择）

`std::prl` 的这次运行可以**要求压力被一比一服务**（一次运行一条开关，现在写成 `std::prl` 的传参；原稿是 `CAMEL_GC_STRESS_UNPACED=1`，按"不得新增 `CAMEL_*` 环境变量"改为传参）；其余运行一律保持限速。改后那 22 条压力臂：`capped 0`、集合 630–10 596、in_flow 78–1 014、堆 major 3 535，输出六行不变（md5 `a532a9f75dfd`）；`nvm`/`fvm`/`jit` 不受影响。

**不限速的代价（实测）**：`gc_nested_reduce.cml --arg reps=12` 在"服务所有诊断运行"下是 **97.61 s CPU / 792 672 次 in-flow 集合**，对照 `nvm` **75.04 s**；`std::tf` 的四个臂（`feat/mm/gc_elder_exhaustion.plan.toml`）的预算是按**限速**标定的。

## 选项与代价

1. **保持现修的形态**：默认限速，只有要求压力的那次运行按一比一服务（`gc_collections` 的 22 个压力臂）。代价：一条传参；收益：压力臂真的在压，其他运行的成本不变。
2. **诊断模式一律不限速**：代价是上面那组数字——所有 `std::prl` 运行慢 30% 左右、in-flow 集合多一个数量级，`gc_elder_exhaustion` 的四个臂要重标；收益是"诊断"这个名字更直白。
3. **只在集合便宜时服务**（按堆大小或分配量自适应）：代价是新策略、新测量、新守卫；收益是两边都要。

## 建议

**选 1**，并把"这一条运行要求了压力"写进计划头（已写）。选 2 的代价与收益不成比例：诊断的意义是**看见**回收压力，不是让每一次 `std::prl` 都变慢；`gc_elder_exhaustion` 的预算专门按限速标定，说明两种运行本来就该分开。