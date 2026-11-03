# 一条计划的判定取决于当时的负载

**日期**：2026-11-02。**计划**：`runner.backend_matrix`（`test/plans/feat/consistency/runner.plan.toml#runner.backend_matrix`，
执行 `test/tools/selftest/run.mjs`）。**状态**：同一个 sha 上读数不一致，**这是 flaky，不是回归**。

## 1. 它自己的判据依赖负载

这条计划的时间敏感写在它自己的注释里（`runner.plan.toml:16-19`）：

> The deciding reading is this self-test's own CPU time, not the wall clock: the runner's margin
> rule reads the CPU of every test's process tree. Six runs of this case inside the fast layer at
> -Jobs 8 on 2026-10-24 read **12.41-12.69 s of CPU against 22.0-23.5 s of wall** -- a 2.3% spread
> in the figure that decides, where the wall clock spread 6.5% in the same six runs.

也就是说：**判定依据是 CPU 时间的余量规则**，而这条规则的输入在一台有别人占用的机器上会被拉偏。

## 2. 同一个 sha 上两种读数

| sha | 运行 | 结果 |
| --- | --- | --- |
| `8c0128efd` | 全量 fast（-Jobs 8） | `total: 8514, pass: 8450, fail: 44` —— **该计划绿** |
| `09f849b66` | 全量 fast（同一 sha 上的一次实验） | 44 条红里**没有它** —— 绿 |
| `09f849b66` | 单跑该计划 ×2（临时分支，`-Jobs 8`） | `total: 1, pass: 0, fail: 1` **×2 红** |
| `438925953` | 单跑该计划 ×2（`-Jobs 8`） | 第一次 `pass: 0, fail: 1`（红），**第二次 `pass: 1, fail: 0`（绿）** |

**最有力的一行是最后一行：同一个提交、同一台机器、相邻两次，一红一绿。** 它的 stdout 在红的时候也只有
`runner self-test ok`、stderr 为空 —— **红的是退出码，不是断言文案**，这与"余量规则判定失败"吻合。

## 3. 我方的默认（不是"放宽到永远绿"）

1. **把"余量"改成"上限 + 明确的抖动带宽"**：只要求 CPU 时间**不超过**某个上限，并把它自己观测到的
   2.3% 抖动写进带宽里，而不是要求两次读数落在某个区间内。
2. **或者换成不依赖负载的判据**：例如"CPU 时间单调不减"（同一 fixture 上，多跑几次只能变多不能变少），
   它检验的是"计数没丢"，而这正是这条计划本来想守的东西。
3. **"放宽到永远绿"不在此列**：那等于把一条曾经能分辨的仪器变成装饰（`8c0128efd` 上它是绿的，
   说明它曾经分辨出过东西）。

**已推进到：判死"这是回归"的说法，并给出两条不依赖负载的改法。**