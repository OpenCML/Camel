# `red-table.mjs` —— 「`develop` 上现在有哪些红」那张表的生成器

```
node internal/tools/red-table.mjs                    # 重写 internal/reports/<当天日期>-develop-reds.cn.md
node internal/tools/red-table.mjs --print            # 打印到 stdout，不写文件
node internal/tools/red-table.mjs --develop <sha> --out <file>
node internal/tools/red-table.mjs --tsv <file> [--jobs 8] [--load 84.5,87.2,90.1] [--tier fast]
node internal/tools/red-table.mjs --diff <a.tsv> <b.tsv>
```

**它只读记录，不跑测试、不构建、不推远端、不碰 `develop`。** 跑完只在当前工作树里写一个页码（或 `--out` / `--tsv` 指的文件）。
`--tsv` 与 `--diff` 都在写页码**之前**返回：前者不再顺手重写 `.cn.md`，后者一个记录都不读。

## 为什么需要它

闸门只测「这次合并碰到的计划」。一个很久没被任何合并碰到的计划里的红，可以一直红下去而没人知道 ——
所以「有哪些红」不是任何一次读数的副产品，需要一张从记录里数出来的表，并且每天重数一遍。

## 它读什么

| 记录 | 位置 | 用它做什么 |
|---|---|---|
| 闸门读数 | `.git/camel-readings/<develop12>-<plans>-<hash>-v1.jsonl` + `.where` | 每行一条用例的 `verdict`。`.where` 写着这份读数测的是**哪个 develop 提交**、测了哪些计划、什么时候 —— `是`/`否` 只能从这里来 |
| 闸门放行记录 | `.git/camel-passed/<sha>.json` | 每次合并的闸门自己的记录（车道、计划集、哪些用例动了） |
| 跑样本 | `test/results/log/<run>/{run.json,fail/,xfail/}` | `run.json` 给那次跑的**提交**与**是否脏树**、选中了哪些计划；`fail/<plan>/<case>/meta.json` 与 `stderr.txt` 给失败原文；`xfail/` 是 `known_fail` 标记 |
| 计划树 | `test/plans/**/*.plan.toml` | 覆盖率的**分母**：一共 420 个计划 |
| 补充原文 | `internal/reports/data/*.tsv` | 闸门读数只存 `expected exit 0, got exit 1`，不存 stderr。车道自己量过的计划可以把「用例 → 报错原文」留成一张这样的表 |

`--develop` 缺省是 `git rev-parse origin/develop`。

## 它怎么判

- **一行＝一条红**：某个「计划/用例」**最近一次**记录判它 `fail`。后来的记录说它绿（那次跑样本选中了这个计划而它不在 `fail/` 下），它就不在表里。
- **故意红的减掉**：路径在 `test/tools/selftest/` 或 `test/plans/debug/` 下的、`selftest.*`/`probe.*` 的、以及 `known_fail` 标记（`xfail/`）的，单独列在第四节，不进表。这与上一轮盘点「466 − 35 = 431」是同一个减法。
- **`是`/`否`**：只由**恰好测在一个具名 develop 提交上**的读数给出。闸门在**合并之前**测那个合并的第一父，所以 `develop` 的尖端要等下一笔合并才被读到 —— 页首把「尖端」和「表锚在哪个提交、落后几笔」都印出来，避免把仪器的年龄当成被观察者的年龄（`C-1124-69`）。
- **归类**：`真缺陷`（有诊断、不变式或断言点名了没达成的期望，且没有 backlog 行认领）、`债`（有 backlog 行，表里只指 id）、`环境缺件`（缺卡/缺库/缺读数/超时）、`夹具或探针`（第四节）、`说不清`（记录里只有一句退出码，或那一行不点名任何拒绝）。

## 跑一次多久

实测（工作站，2026-11-24，三次）：**6.0s / 6.6s / 6.8s**。远低于 60 秒，所以不需要分批或缓存。

时间几乎全在读小文件上：`420` 个计划文件的头几行、`64` 份闸门读数（含 `.where`）、`24` 份放行记录、`325` 个 `run.json`、以及 `1313` 条失败用例的 `meta.json`+`stderr.txt`（约三千个几千字节的小文件）。记录越多它越慢，但按现在这个量级，一天一次完全不用管。

## 谁在什么时候跑它

两条一起用，一条给及时性、一条给下限：

1. **合并位每次合并推进 `develop` 之后顺手跑一次**（`merge-lane.ps1` 收尾那一行）。
   理由：这张表的「锚点」是**最新一个被读数测过的 develop 提交**，而读数正是合并位写的 —— 合并位跑它，锚点就刚好落在刚被量的那个提交上，「是」/「否」的覆盖面最大，且代价只有 6 秒。
2. **每天一次的计划任务兜底**（建议 08:05，工作站本机 `schtasks`）。
   理由：一天没有合并落地时，第 1 条不会触发，页码会停在昨天；兜底那一次把「生成时刻」推进到今天，并把记录的新鲜度重新数出来。它只读记录，不占构建槽、不占测试槽，可以和任何车道并行。

**不要**把它放进闸门当判据：它读的是已经发生的事，拒不了任何一笔合并。

## 表头：这张表是在什么条件下取的

`--tsv` 写出的表以一串 `#` 行开头（所以 `cut`/`awk` 与旧的两列读法都还能用），行本身是
`plan<TAB>case<TAB>why<TAB>record<TAB>observed-at<TAB>class`：

```
# red-table v1
# generated-at 2026-11-24T17:26:35.248Z
# tip d6503a197c…            ← [provenance] tip：生成时读的是哪个提交（--develop / origin/develop）
# anchor 7866e1dea64d…       ← 「还成立吗」那一列锚在哪个 develop 提交
# jobs mixed 4,8,28          ← 或 8；或 unknown
# jobs-source run.json of the rows' newest records
# load unknown               ← 或 84.5,87.2,90.1
# load-source unknown
# tier fast
# source 159 readings, 325 runs, 1913 rows
# rows 1913
```

- **`jobs`** 从每行最近那条记录的 `run.json` 读。一份表若混了几个宽度，写 `mixed 4,8,28`，**不**挑其中一个当代表 —— 那会让读的人以为这一整份是在一个宽度下取的。
- **`load`** 任何记录里都没有（`run.json` 只有 `jobs`，没有宿主负载），所以只能由取表的人给：`--load 84.5,87.2,90.1`，或 `--load-from <那次运行的日志>`（读末行 `[load] a b c (1m/5m/15m)`）。没给就是 `unknown`。
- **`unknown` 不是 0**：它说的是「这份表没有声明」，比较遇到它就拒绝（见下）。这与页首那句「未知不是缺陷，假装已知才是」是同一条规矩，只是这次的单位是表的条件而不是某一行的成立与否。

## 比较两份表：`--diff`

```
node internal/tools/red-table.mjs --diff <a.tsv> <b.tsv>
```

退出码：**0** 可比且没有 new/changed/removed；**1** 可比且有差异（逐条列名）；**2** **不可直接比**或读不了。
被拒时输出**只有拒绝理由，没有任何 new/fixed/removed 行** —— 「条件不同」不是一个有一百行的 diff。

拒绝的判据（实现在 `internal/tools/red-table-context.mjs`）：

- 任一份没有 `# red-table v1` 表头（旧的两列 `*-reds.tsv`、一份 `[FAIL]` 日志）⇒ 它没说自己是在什么 `jobs`/`load` 下取的，**是一次观察，不是基线**；
- `jobs` 不同，或任一份是 `unknown`/`mixed` ⇒ 宽度会改变用例集合：没启动的用例在 diff 里读作 `removed`；
- `load` 任一份 `unknown`，或一份静载（1m ≤ 8）一份带载 ⇒ 超时余量判据在带载时测不到，两种机器是两件仪器；
- `tier` 不同 ⇒ 层决定存在哪些用例。

两个显式开关 `--allow-unknown-jobs` / `--allow-unknown-load` 让人**说出来**再比；默认不猜。
`tip` **不**作为拒绝理由（基线与被测树本来就该是不同提交），但会打印。

名字按整段读：TSV 按制表符切；`[FAIL]` 行取标记后的整段、只在「两格以上空格」处切掉末尾的耗时列。
**不许用 `\S+`** —— 它在名字的第一个空格处停下，名字带空格的用例于是变成 `removed`，而它其实跑了（`C-1124-186`）。

## 自测

```
node internal/tools/red-table-context.test.mjs      # 18 项，含 CLI 的 --tsv/--diff
```

覆盖两条规矩：表头写上 `tip`/`jobs`/`load`，不同 `jobs`/`load` 的表被拒而不是逐条列 new；名字带空格的用例改档后是 `changed`，不是 `removed`。

## `--recheck-red`：把每条红单独跑一次（设计，未实现）

目的：把「整层读数」和「单跑读数」并排放，让同一个用例的「整层红、单独绿」由工具判，而不是由人猜。

输入必须是一份**带表头**的 `--tsv` 表（`jobs`/`load` 已声明）；缺表头就拒绝，因为「整层读数」那一栏没有条件可写。

逐条：用同一个树、同一个 `tier`、`-Jobs 1` 跑该用例所在的计划。运行器今天没有单用例开关，单用例形式要走 `--selection-cases`（一份只列这一条用例的记录）并让 `camel-remote.ps1` 把它传下去；在那之前，单位是**计划**（一条计划里有多条红时，只能一起重跑）。

输出三栏：

```
用例 | 整层（verdict, jobs, load） | 单跑（verdict, jobs=1, load） | 结论
```

结论的判法：

| 单跑 | 整层失败原文 | 结论 |
|---|---|---|
| 红，原文同 | — | **真红** |
| 红，原文不同 | — | **真红（换了红法）**（单独一个值，不折进「真红」） |
| 绿 | 命中资源判据（`below the 4x margin`、`CPU budget`、`timed out`）或整层 `load` > 8 | **负载型** |
| 绿 | 其他 | **并发型** |
| 没跑起来（exit 2、用例不存在、没构建） | — | **未复现**（不是绿） |

三条纪律：

1. **单跑绿从不写成 `fixed`**：它是一次观察，只把这行从「整层红」里拿掉，不把它从树上拿掉。
2. 判「真红」要比**归一之后的**失败原文：promotion-3 那次 196 条原文差异，归一临时目录名与实测数字后只剩 11 条。不归一，每条红都会被判成「换了红法」。
3. 代价按计划算：1913 条红分布在 139 个计划上，最坏是 139 次串行宿主运行。所以要有 `--only-plan <p>`、`--limit N`、按 `class` 过滤，并且**必须打印「没重跑的还有多少条」**。没有这个数，一份只重跑了三行的表会被读成整层结论。

## 交回时说什么

页首五行就是答案的形状：覆盖了多少计划、多少计划一条读数都没有、几条能答「是」/「否」、锚在哪个提交、尖端落后几笔。**「未知」不是缺陷，假装已知才是** —— 表里每一行的 `是/否/未知` 都要能追到一条具名记录。

带条件交回时再多两行：`--tsv` 那条汇总行（`jobs=`/`load=` 与它们的来源）和 `--diff` 的结论行（`comparable` 或 `not directly comparable` 与理由）。