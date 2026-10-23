# 问题：`perf.para` 要不要变成可门禁（H167，note ⟨f14c90d4⟩）

## 问题
性能闸门只挑了两个计划（`perf.startup` 2.1 s、`perf.fib` 约 11 s）；`perf.para` 在闸门里被标成
不可门禁（六条用例一次 178.7 s，`asian_option_mc.nvm` 一条 67.5 s，`merge_sort` 常驻 3.8 GB / 11.9 GB），
所以并行一侧只靠里程碑手动跑。是接受现状，还是把一个便宜的并行子集立成可进闸门的计划？

## 选项与代价
| 选项 | 代价 |
|---|---|
| A. 保持现状（里程碑手动跑） | 零改动；`std::prl` 一侧的回归在两次里程碑之间无人看见——正是 H167 立闸门要修的那类空白 |
| B. 把便宜的子集立成单独的计划（报告点名 `monte_carlo_pi.prl`，1.61 s） | 要改计划与用例（H167 的任务禁止改 plan/tier/case，须新开一项）；闸门多一个目标，且墙钟类用例只在负载带内可比 |
| C. 接受整条 `perf.para` 进闸门 | 一次 178.7 s（报告按重复次数记作 9–15 分钟一次的门），且它是共享主机上负载最敏感的一项，假回归概率最高 |

## 倾向
选 **B**：H167 已经把 10 个计划按成本分过类并写进闸门的数据（`gateable`、`refusal`、`cost`），
挑已经量过的小项即可，比 C 便宜、比 A 有信号。**判断在负责人**（报告点名"留给作者裁定"）；
计划怎么拆这一半决策者可以先定。

## 出处
- note：`git notes --ref=handover show f14c90d4`，报告《H167》§7「边界与未决」第 5 条。
- 现状：`scripts/perf-gate.mjs` 的 `PLANS` 表——`perf.para` 是 `gateable: false`、
  `refusal: 'expensive'`，没有预算，只有超时；口径写在 [`docs/test-plans.md`](../../test-plans.md)
  的「Performance Measurement」一节，基线在 `test/perf-baseline.json`。
  发布门禁 `scripts/release-gate.mjs` 是另一件事，它的 slow 层按 H154 §3 点名计划文件，同样不含
  `perf.para`。