# 覆盖表：每条规则、每个没有规则的文件，各选中多少用例

2026-11-24 · 基线 `origin/develop` = `e66efb7a7b` · 层 `--tier fast` · 表 `test/selection/coverage-table.json`
（`node scripts/coverage-table.mjs` 生成，`--check` 只读核对）

`--changed` 只回答"选中哪些计划"。计划是一个目录的用例，所以那个答案永远比改动宽：`src/compile/src/` 下改一个文件，
打印出来是四个目录加一个计划文件，而"四个目录"背后是多少用例、其中多少在 fast 层会真的起，规则文件里没有写。
这张表把它写下来：每条规则一行、每个没有规则的文件一行，各自带 **要跑的用例数**。

## 一、怎么算出来的（为什么数可信）

- **数的来源是运行自己的装载器**：`test/tools/run-tests.mjs#loadPlans`（本次为它加了一个 `export`，注释里写了为什么）。
  `[[sweeps]]` 副本、后端展开、层过滤、companion 闭合都在里面；本脚本对**每一个不同的选择**只调它一次，不另写一套
  "这个计划里有哪些用例"。
- **文件到规则的匹配也是 runner 的**：`test/runner.mjs#plansForChanges`，它打印的原因行点名了规则路径
  （`rule src/compile/src/ -> feat/compile`），表就从这一行读规则名。
- **只有两种读法**：`dir`（规则路径是目录）、`file`（规则路径是文件）、`none`（没有任何规则选中它）。
  目录规则按**文件的目录**解析，所以同一目录的文件合成一行；一个文件恰好出现在一行里（脚本自己核对这个和）。
- 表**不写 HEAD**：写了它就没法在承载它的提交上逐字节重生（提交让 HEAD 动）。它记三个输入的摘要——
  选择文件、文件清单、每个计划文件的内容——读的人可以自己判断这张表还描不描述他们的树。
- **表不修改任何计划**，只写表本身。

## 二、头行数字（本提交的树）

| 读数 | 数 |
|---|---|
| 文件（tracked + git 会加入的未跟踪，去掉表自己） | 8591 |
| 行 | 2929：目录 641 / 文件 837 / 无规则 1451 |
| 无规则行 | 1451 = **45 被规则的 `except` 排除** + **1406 没有任何规则** |
| 其中不被 selftest 允许表点名的 | **1047** |
| 计划文件 | 420 |
| 整个 fast 层（放宽一次要跑的） | **9916** 个用例 |

一句话：**这 1451 行里的任何一行，单独改动都起 9916 个用例。**
因为 `plansForChanges` 对"没有规则"返回 null，`--changed` 把 null 读成"什么都没匹配上"→ 放宽到整层。
那 45 个被 `except` 排除的文件**也一样**：`except` 只让它在混合改动里不选，单独改它还是放宽。
实测：只改 `test/cases/perf/fib_recursive.cml` 的列表打印
`falling back to the whole fast layer (386 plan file(s))`——"perf 层不该被普通改动选中"在单独改它时并不成立。

## 三、最宽的三行

同一套目标（同样的目录/文件）的多行合成一条读数，括号里是行数与文件数。

| 用例数 | 行 | 目标 |
|---|---|---|
| **9885** | `test/plans/feat/` 下 126 个**非计划文件**（`docs_ai_reference/fixture` 14、`docs_links` 14、`golden/` 90、`headers` 8），126 行 | `test/plans/feat` 整个目录 |
| **7611** | `src/stdlib/test/`，60 个文件，1 行 | smoke、feat/compile 2689、feat/modules 2085、feat/opt 1636、feat/linear 869、feat/mm 320、两个点名的 runtime 计划各 1 |
| **5766** | `src/kernel/` 与 `src/stdlib/std/builtin/` 的兜底规则，96 个文件，14 行 | smoke、feat/compile、feat/trans、feat/linear、feat/modules |

第四宽是**单个计划文件** `test/plans/feat/mm/gc_poison_sweep.plan.toml`：**4860**。
点名的计划文件整份跑，它的 `[[sweeps]]` 副本把别的计划的用例也带了进来——改一个计划文件比改一整个目录还贵。

第一行是自我规则（`"self": true`，`test/plans/`）的一个漏洞，表把它照原样记下来：
非计划文件"选最近的持有计划的目录"，但 `docs_ai_reference/` 这类目录的计划文件是**兄弟**
（`test/plans/feat/docs_ai_reference.plan.toml`），不在目录里；走到 `test/plans/feat/` 就停了，
而 `feat/` 顶层本身有 10 个计划文件（`docs_links.plan.toml`、`showcase_s2.plan.toml`……），
于是**改一个 fixture 就起 feat 整棵树**。`test/plans/feat/golden/` 的 90 个黄金同理。

## 四、改 `src/compile/src/` 到底多贵

表里 `src/compile/src/**` 的任何一行（61 个文件，4 行）：

| 用例 | 目标 | 怎么来的 |
|---|---|---|
| 2689 | `test/plans/feat/compile` | 目录目标，按层过滤 |
| 1292 | `test/plans/feat/consistency` | 目录目标，按层过滤 |
| 113 | `test/plans/feat/trans` | 目录目标，按层过滤 |
| 10 | `test/plans/smoke` | 目录目标，按层过滤 |
| 1 | `feat/runtime/draft_node_owner.plan.toml` | 点名的计划文件，**整份跑** |
| **4105** | | |

派工单写的 4109 是计划页那棵树上的读数；在本提交的树上重算是 4105（差 4 个用例，两棵树之间计划条目动过）。
结构没变：**四个整层 + 一个点名文件**，只是 fast 层把目录目标过滤到了 4105，而不是把四份计划的全部条目都跑。

对比一下宽度不同的两条规则：

- `src/compile/include/camel/compile/**`（头文件）→ **2813**：少了 feat/consistency 的 1292。
- `src/stdlib/test/` → **7611**；`src/kernel/` 兜底 → **5766**。
  也就是说改 `src/stdlib/test/fib.cml` 比改编译器内部还贵：它同时是 modules/opt/linear/mm 的输入。

## 五、1046（本页加入后 1047）个没有规则的文件：分类

分类是**从路径推的类别**，不是替它们补 allowlist：表里每一行带 `group`，不匹配任何类别的记为 `unclassified`（今天 0 个）。

| 组 | 文件 | selftest 允许表 | 是什么 |
|---|---|---|---|
| review-page | 541 | 否 | `internal/reviews/` 的评审/问答页 |
| report-page | 365 | 否 | `internal/reports/` 的报告页（含本页；写本页前 364） |
| benchmark | 163 | 是 | `benchmarks/`，手工 perf 层 |
| showcase | 87 | 是 | `showcase/`，手工 perf 层 |
| marketing | 80 | 是 | `promo/` |
| archived-page | 76 | 否 | `internal/archive/` 的退场页 |
| perf-layer | 39 | 是（规则的 `except`） | `test/cases/perf/` 17 + `test/plans/perf/` 22 |
| plan-page | 25 | 否 | `internal/plans/` 的工作计划页 |
| question-page | 21 | 否 | `internal/questions/` |
| repo-metadata | 9 | 是 | LICENSE / ABOUT / ZEN / 配置文件等 |
| ci-config | 8 | 是 | `.github/` |
| collab-page | 8 | 否 | `internal/collab/` 的协作页 |
| packaging-tree | 8 | **否** | `packaging/pypi/`：允许表写的是 `^pypi/`，管不到它 |
| test-scaffolding | 6 | 是 | `test/.gitignore`、`test/README.md`、perf 基线等 |
| gc-stress-plan | 4 | 是（`except`） | `test/plans/feat/mm/gc*.plan.toml` |
| editor-config | 3 | 是 | `.vscode/` |
| git-hook | 2 | 是 | `.husky/` |
| internal-page | 2 | 否 | `internal/` 根下的两页 |
| gpu-stress-plan | 1 | 是（`except`） | `test/plans/gpu/tiles_regions.plan.toml` |
| progen-plan | 1 | 是（`except`） | `test/plans/feat/opt/progen.plan.toml` |
| release-notes | 1 | **否** | `CHANGELOG.md`：允许表认的是 `changes.log` |
| vendored | 1 | 是 | `third_party/` |
| **合计** | **1451** | 404 在允许表里 | **1047 不在**（本页是其中之一） |

三条"空了"的类别，是这项读数里最值得说的部分：

- **docs：0。** `docs/` 有规则（docs_links / portability），没规则的是 `internal/` 的页面。
- **build/CI：0（允许表之外）。** `CMakeLists.txt`、`cmake/`、`.github/` 都有规则或归类。
- **源码：0。** `src/` 下没有一个文件没有规则。这不表示每个源文件都有用例，只表示**每一个**至少被一条规则
  选中某些计划；"有规则"和"有属于自己的用例"是两件事，本表只记前者。

派工单说 1059。本文量到的是 1046（本页加入前）：同一个判据（`plansForChanges(file) === null`
且不在允许表里），在 `352eeb02a5` 的导出树上量到 1047，在 `e66efb7a7b` 的树上量到 1046。
1059 是第三种树状态，我没有复现出来，也没有把 1046 向 1059 凑——数随树走，这正是表必须重新生成、
不能引用一个写死的数的原因。差额里能确定的是：写页/读数也在这一类里（那条分支的 2 页、19 条读数）。

表自己不在表里（它不能包含自己）：`test/selection/coverage-table.json` 是 `test/selection/` 下唯一的文件，
改它照样会放宽到整层，而这张表看不见它自己。

## 六、可重复性

同一条命令跑两次，两次的输出**逐字节相同**；`--check` 不写文件，只重新生成并比较：

```
node scripts/coverage-table.mjs            # 写表
node scripts/coverage-table.mjs --check    # 只读核对
coverage-table: identical sha256:a21ce12421f749c2fe130d076f898899e3b4d54bbad3ae741b7f5b2bf25e6966
```

摘要由生成器算，只取决于表的内容；输入摘要（`inputs.selection/files/plans`）只取决于树。
`--check` 在不一致时会打印第一处不同的行号与两边的原文，并以 1 退出。

`--print` 打印头行数字、最宽的几行和分类；`--explain <路径>` 打印一条路径的选择，并按目标拆开用例数
（"四个整层"是哪四个、各多少），页面里的表就是用它取的。

## 七、这张表没有决定的事

- **"选中"不是"执行到"。** 目录目标的用例是超集：改 `src/compile/src/gir/builder.cpp` 选中的 4105 个用例里，
  真正读了那个文件的可能只有一部分。表回答"起多少"，不回答"哪些真的覆盖了"。
- **用例级收窄没有合进来。** `lane/precise-select`（`352eeb02a5`）把选择单位改成了用例，
  `case:<文件>` 会把**用例文件**的改动窄到点名它的用例；那部分合并后，`test/cases/**` 的读数会变小，
  `src/**` 的读数不变（源码规则仍是 `source:<目录>`）。
- **自我规则对非计划文件的"最近目录"是漏洞**（第一节最宽的那 9885）。修它要么改 `runner.mjs`
  的 `nearestPlanDirectory`，要么给 `test/plans/feat/golden/` 这类目录加规则；表只记录现状。
- **45 个 `except` 文件单独改动仍放宽**。让 `except` 真的选 0（而不是 null）是策略决定，会改变
  `--changed` 的语义；本表不改语义，只写出这个事实。
- **`internal/` 的 1047 页要不要有规则**（例如 docs_links 本来就读 `.md`）是项目决定。数字是发现，
  加 allowlist 把数改小不是本任务的一部分。
- **只有 fast 层。** `--tier slow|milestone|all` 的数不同；点名的计划文件在所有层都整份跑。
- 表里的分组类别（`group`）与 selftest 的允许表是两份东西：表只说明某一类"已经在允许表里"，
  权威仍是 `test/tools/selftest/run.mjs#RULELESS_ALLOWED`；允许表改了而这里没改，数会漂。