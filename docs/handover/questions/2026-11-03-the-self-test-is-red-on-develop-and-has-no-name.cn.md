# 自检在 develop 上是红的：`runner.backend_matrix` 里 7 个用例失败，这一红该由谁摘？

日期：2026-11-03　状态：**已有名字，未修；默认"单独立项修"，不标 `known_fail`**

## 问什么

`test/tools/selftest/run.mjs` 是 runner 自己的自检。跑它的计划只有一条：
`test/plans/feat/consistency/runner.plan.toml` 的 `runner.backend_matrix`。它在 `origin/develop`
（`4b52f071e`）上是红的——外层 `[SUMMARY] total: 1, pass: 0, fail: 1`，自检内部
`[SUMMARY] total: 36, pass: 28, fail: 7`。这一红与 2026-11-03 那两笔（0 计划目标的拒绝、
`--list` 同改）逐行无关：把这两笔从树上摘掉再跑，16 条问题**逐行相同**。

也就是说：**闸门里有一条一直在红的用例，而它没有名字**。这一红归谁、什么时候修、在修好之前
"闸门绿"该怎么读？

## 读数

都在 88 上，`cd ~/camel-work/stdlibref && node test/tools/selftest/run.mjs`（退出码 1）。
下面是自检自己打印的 16 条问题（原样，长行截断）：

```
sweep fast: runner exit 1, expected 0
sweep fast: unexpected summary ([SUMMARY] total: 13, pass: 0, fail: 13)
sweep fast sampled 0 copies (risky 0, plain 0), expected 3 + 1
sweep all: runner exit 1, expected 0
sweep all: unexpected summary ([SUMMARY] total: 18, pass: 0, fail: 18)
sweep milestone: runner exit 1, expected 0
sweep milestone: unexpected summary ([SUMMARY] total: 5, pass: 0, fail: 5)
sweep collision: runner exit 1, expected 0
sweep collision: unexpected summary ([SUMMARY] total: 4, pass: 0, fail: 4)
sweep collision ran: FAIL poison:selftest.sweep.collision.alpha:shapes.nvm, … (4 条)
sweep reuse: runner exit 1, expected 0
sweep reuse: unexpected summary ([SUMMARY] total: 4, pass: 0, fail: 4)
sweep reuse ran: FAIL poison:selftest.sweep.reuse.source:shapes.nvm, … (4 条)
plan-selection: 1 plan file(s) no case path selects, so --changed can never run them:
    test/plans/feat/compile/rules_angle_args.plan.toml (its cases are under test/cases/modules/autodiff/)
benchmark failure: the payload failure reason is missing
benchmark failure: the diagnostic of the failed iteration is missing
```

分成三组，三组的病不一样：

1. **五个 sweep 夹具整体 `PASS=0`**（`fast`/`all`/`milestone`/`collision`/`reuse`）：不只是
   `poison` 那几条期望失败的对不上，连应当通过的样本也全红——这更像"夹具根本没跑起来"或
   "sweep 展开没产出样本"（`sweep fast sampled 0 copies … expected 3 + 1` 正指向后者），
   而不是某一条断言选错。
2. **`--changed` 的洞**：`feat/compile/rules_angle_args.plan.toml` 没有任何 case 路径能选到它，
   改了它的用例永远不会跑它。这是 `test/plan-selection.json` 缺一条规则，与 runner 本身无关。
3. **benchmark 失败载荷**：少 `reason`、少失败轮次的诊断两处。

## 三条路

| 路 | 代价 |
| --- | --- |
| 1. 单独立项修（先把五个 sweep 夹具单独跑一次，确认"没跑起来"还是"断言选错"） | 半天到一个 lane；难点是读懂 `poison` 标记的语义，但修好后 runner 自检才真的在护着东西 |
| 2. 把 `runner.backend_matrix` 标 `known_fail` | 闸门立刻可读；但这是"打了一条还红的用例摘标记"，而 sweep 与 benchmark 两处正是"读数不对"这一类回归的守门人 |
| 3. 只命名（本页 + 看板一行），闸门继续红 | 每次全量闸门都红，"绿"的定义被稀释；好处是没人再问"这条红是谁的" |

## 默认

**走路 1**，本页只负责命名与读数：不动测试、不标 `known_fail`。接这一笔的人第一件事是把五个
sweep 夹具单独跑一次——若它们"根本没跑起来"，那是夹具/sweep 展开的缺陷（一天内能修）；
若只是 `poison` 的期望对不上，那是语义问题，要回到设计原则里找 `poison` 该不该进摘要。