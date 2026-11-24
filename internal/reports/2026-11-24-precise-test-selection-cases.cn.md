# 精确测试选择，第二步：选中的是**用例**，而且能重放

第一步（`2026-11-24-precise-test-selection-readings.cn.md`）把"最近 30 次合并选中多少"记成了读数，
并留下结论：仓库里没有覆盖表，所以只改源码的改动**无法**窄到计划目录以下；另有 13 笔合并没有任何读数。
这一步把那句话变成机制：选择的对象从计划换成用例，每条用例带一个原因，运行收下这份名单，
记录能用它自己的输入重放出同一份名单。

## 一、改了什么

| 位置 | 改动 |
|---|---|
| `test/runner.mjs` | `--changed` 的每条选择带上原因：`case:<文件>`（改的是某个用例的输入）、`plan:<目标>`（改的是一份计划/黄金/`also`/文件规则点名的计划）、`source:<目录>`（源码目录镜像到计划，即诚实的残留）。用例文件的改动把计划**窄到点名该文件的用例**；其余两种整份保留。 |
| `test/tools/run-tests.mjs` | 新 `--list-cases`（和真跑同一套 loadPlans/sweep/层/companion 闭合，只打印不启动）与新 `--selection-cases <文件>`：只起名单里的用例，名单里有不会启动的名字就退出 2，而不是静默少跑。 |
| `scripts/selection-cases.mjs` | 引擎与记录：`--print` 打印名单与摘要，`--write` 写记录，`--verify` 用记录自己的 `files` 重放并逐字段比较。 |
| `test/plan-selection.json`、`docs/test-plans.md` | 规则文件与文档改成"规则点名计划，运行跑用例"；并写明命名计划不再整份跑。 |
| `test/selection/readings/*.json` | 19 条记录，共 135 KB。 |

层（`--tier fast`）现在按**用例自己**的 tier 判：一份被点名的计划不再因为"它是命名目标"而跑它的慢用例
（旧规则原文是"a named plan runs in full whatever `--tier` says"）。日层照旧跑慢用例。

## 二、一笔具体改动的前/后

`e1433cfe1912`（11-24 07:03，改了 `test/plans/feat/compile/higher_order_clause.plan.toml` 与
`test/plans/feat/mm/gc_poison_sweep.plan.toml` 两份计划文件）：

```
旧：plan-cases: 4863           ← store 里那次的读数 4863 个用例 / 210 s（374ebfbceb45-2-9b384355）
新：plan-cases: 4863
    cases:      241           ← --tier fast 下这批计划真正要跑的用例
    case-digest: sha256:9993ade9abc6b300844feb621483be73594d157420a0e80dc2d709719063606b
```

我的重放把旧的 4863 一分不差地重现（同一棵树上"计划级展开"的定义），新列表 241 是同一批计划在 fast 层
的用例——旧读数里那 4,622 个慢用例，是"命名计划整份跑"买来的，不是这次改动碰到的。

改到**用例文件**时窄得更明显：`test/cases/modules/tiles/argmax_axis_scope.cml` 145 → 9；
`test/cases/compile/enum/shapes.cml` 40 → 13（5 个 `compile.enum` + 8 个前端的 AST 快照，后者是通过
`refs` 闭合进来的 companion）。

## 三、13 笔没有读数的合并，逐笔

第一步报告表 18–30 行（11-24 01:18–02:54，`4b528b928fc0` … `38b7aec1bf00`）。每笔一条**选择读数**，
`verdict` 字段为 `null` 并写明"没有判读，也没有从选择里编一个"——选择读数不是判读。

| 合并 | 文件 | 计划 | 旧展开 | 新列表 | 原因 |
|---|---|---|---|---|---|
| `4b528b928fc0` | 9 | 15 | 9018 | 9018 | plan=9018 |
| `973868aec7` | 5 | 1 | 21 | 21 | plan=21 |
| `d2c3f10d6f` | 8 | 12 | 7432 | 7432 | plan=7432 |
| `71785e357f` | 5 | 4 | 34 | 34 | plan=34 |
| `1347235053` | 1 | 1 | 50 | 50 | plan=50 |
| `48f1025769` | 28 | 26 | 9043 | 9043 | plan=9043 |
| `809033ed49` | 2 | 1 | 21 | 21 | plan=21 |
| `c2f2519150` | 4 | 3 | 13 | 13 | plan=13 |
| `10824724c3` | 3 | 2 | 31 | 31 | plan=31 |
| `faf8841267` | 5 | 7 | 296 | 296 | plan=296 |
| `338a75a889` | 78 | 16 | 9894 | 9894 | plan=9894 |
| `712ba6f670` | 10 | 10 | 6197 | 6197 | plan=6197 |
| `38b7aec1bf` | 1 | 2 | 9894 | 9894 | 无规则 → 放宽（`widened=true`） |

**13 笔一笔都没减。** 原因不是这一步没做，而是它们没有一笔改到用例文件：它们的用例 100% 来自
`plan:` 原因——规则直接点名一份计划，那计划里哪个用例会被碰，规则本身没有说。这正是第一步"仓库里
没有覆盖表"的结论；现在每笔都有 `cause_counts` 为证，而不是推断。窄得下来的都在改到用例的那一类
（上面 §二）。

## 四、可重复性：同一输入，两次同一份名单

`e1433cfe19`，同一 `--base/--head/--files` 跑两次：

```
hash1=C848C73CDCC43A9901138E4E897DF0154ED7F5A03C1346DCD6FD36CF60113411
hash2=C848C73CDCC43A9901138E4E897DF0154ED7F5A03C1346DCD6FD36CF60113411
```

`--verify` 对 19 条记录全部报 `selection-cases: identical`（19/19）。另一个方向：本来已有读数的 6 笔
被重放做交叉核对——`d8c6789e69` 38=38、`e1433cfe19` 4863=4863、`6c56589f63` 31=31、`0fc19d90d6` 31=31
完全一致；`abed9ea896` 9041（记录 9038）、`82408f6fc8` 2529（记录 2528）差几个用例，因为重放跑在
**今天的**计划树上，而记录是那时的树——记录里 `tree` 字段写的就是这一点。

## 五、闸门

`pwsh -File internal/tools/camel-remote.ps1 -Path <worktree> -Jobs 8 -NoGpuWatch`：exit 0，build 34 s，
`RUN_EXIT=0`，`TREE_STATE=clean`（跑前跑后）；这次改动自身的选择是 36 个用例。

## 六、这一步没做的

- 读数存储（`scripts/merge-readings.mjs`）仍以**计划集**为键。用例集是计划集的细分，同一个计划集两次
  改动现在会得到不同的名单，拿子集去比全集就会报"移除"。下一步应把基准键换成用例集（记录里已有
  `case_digest` 可直接当键）。
- selftest 的规则覆盖检查在今天的树上报 1059 个"没有规则"的文件（其中 365 个是 `internal/reports/`
  的旧页，`CHANGELOG.md` 也在内），与本次改动无关；我只为自己新增的这一类加了 allowlist 条目
  （`test/selection/`），没有替别人的红做决定。