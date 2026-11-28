# Figure 1 的三张图：`opt::inline` 版与 `std::cxx::inline` 版，逐字节

**状态：初读，非定稿（preliminary, not final）。** 本页只有一次运行的读数，机器当时很忙，且只覆盖
Figure 1 这一支程序；正文里每个数字都可以按 §4 的命令重取。

- **尖端**：车道工作树 `D:\Projects\Camel-wt\misc`，detached HEAD `b25dcda7a2b3`（主机检出
  `/home/zjwei/camel-work/misc`）。本轮没有改任何被跟踪文件；跑时工作树有 4 个**未跟踪**的探针文件，
  所以 `build-info.json` 的 `dirty` 是 4，`[provenance]` 行照抄了这个 4。
- **机器**：主机 `zjwei`（Linux，令牌池 24 个 worker）。本次运行的
  `[provenance] tip (dirty: 4 files) | binary built 2026-11-28T15:08:37+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 2`。
- **负载**：跑前 `[load] 71.75 54.38 42.30 (1m/5m/15m)`，跑后 `[load] 77.28 57.91 43.99`，池子
  `pool: size=24 inUse=14`。**主机很忙**，所以本页不写任何墙钟结论；图的**字节数**与**节点数**不随
  负载变（同一二进制、同一命令、dot 里没有时间戳），下面比的正是这两样。
- **日期**：2026-11-28。

## §1 口径：照计划自己声明的那一条比，不自造

`test/plans/feat/opt/figure1_pipeline.plan.toml` 是 Figure 1 的仪器，它自己声明的比较是最后两条：

```toml
[[tests]]
name = "figure1_pipeline.algebra.by_cpp_inline"
case = "../../../cases/opt/cxx_isolation/paper_figure1.cml"
args = ["${case}", "std::cxx::inline", "opt::cse", "opt::algebra", "std::gir", "std::nop"]

[[tests]]
name = "figure1_pipeline.algebra.by_camel_inline"
case = "../../../cases/opt/cxx_isolation/paper_figure1.cml"
args = ["${case}", "opt::inline", "opt::cse", "opt::algebra", "std::gir", "std::nop"]
compare_to = "figure1_pipeline.algebra.by_cpp_inline"
compare_fields = ["exit_code", "stdout_normalized"]
```

据此，本页的口径是：**同一份程序**（`test/cases/opt/cxx_isolation/paper_figure1.cml`，
`inc(x) * 3 + (x + 1) * 4` 印出 `21`）、**同样的下游遍且同序**、`std::gir std::nop` 收尾，两条读数的
区别只有第一步是用 `opt::inline` 还是 `std::cxx::inline`。

两处比计划更严：计划比 `stdout_normalized`，本页比**原始字节**（字节相同 ⇒ 归一化后必定相同）；
不同时打印第一处差异的行号与两行原文，所以差异会点名到某一节点行或边行，而不是"图不一样"。

三张图 = 管线的三步：① `inline` 之后、② `+ opt::cse` 之后、③ `+ opt::cse + opt::algebra` 之后。
另加一步 `before`（**不过任何内联器**，`std::gir std::nop`），它是起点、不是三张图之一。

## §2 四条读数

| 步 | 遍列表 | 字节 Camel / C++ | 相同 | nodes | edges | direct_calls |
|---|---|---|---|---|---|---|
| `before`（起点，无内联器） | `std::gir std::nop` | 4976 | —（只有一条命令） | 19 | 16 | 2 |
| ① `inline` | `opt::inline` 或 `std::cxx::inline`，`std::gir std::nop` | 7067 / 7067 | 是 | 28 / 28 | 26 / 26 | 0 / 0 |
| ② `inline_cse` | 同上 `+ opt::cse` | 6155 / 6155 | 是 | 24 / 24 | 22 / 22 | 0 / 0 |
| ③ `inline_cse_algebra` | 同上 `+ opt::cse + opt::algebra` | 4431 / 4431 | 是 | 18 / 18 | 14 / 14 | 0 / 0 |

`nodes`/`edges`/`direct_calls` 来自同一批遍之后加一个 `std::stats` 的 `GRAPH_STATS` 行，不是从 dot 里数的。

这张表里最要紧的一行不是"相同"，是起点：**4976 → 7067**，即 `opt::inline` 这一步确实动了图
（19 → 28 个节点、`direct_calls` 2 → 0）。所以③对①的逐字节相同，不是"内联器什么都没干、拿同一张图
跟自己比"那种空判。

## §3 判词原文（程序自己打的那几行，逐字）

```
fig1.program=test/cases/opt/cxx_isolation/paper_figure1.cml
fig1.camel=opt::inline fig1.cpp=std::cxx::inline
fig1.stage=before inliner=none bytes=4976 nodes=19 edges=16 direct_calls=2
fig1.stage=inline rc_camel=0 rc_cpp=0 bytes_camel=7067 bytes_cpp=7067 same=True
fig1.stage=inline camel nodes=28 edges=26 direct_calls=0
fig1.stage=inline cpp   nodes=28 edges=26 direct_calls=0
fig1.inline_changed_graph=True (before=4976 inline=7067)
fig1.stage=inline_cse rc_camel=0 rc_cpp=0 bytes_camel=6155 bytes_cpp=6155 same=True
fig1.stage=inline_cse camel nodes=24 edges=22 direct_calls=0
fig1.stage=inline_cse cpp   nodes=24 edges=22 direct_calls=0
fig1.stage=inline_cse_algebra rc_camel=0 rc_cpp=0 bytes_camel=4431 bytes_cpp=4431 same=True
fig1.stage=inline_cse_algebra camel nodes=18 edges=14 direct_calls=0
fig1.stage=inline_cse_algebra cpp   nodes=18 edges=14 direct_calls=0
figure1_two_inliners: 3 of 3 graphs identical
```

运行器那一侧：

```
[PLAN] probe.fig1-two-inliners: probe.fig1_two_inliners
[PASS] probe.fig1_two_inliners.three_graphs                    3.11 s
[SUMMARY] total: 1, pass: 1, fail: 0
```

## §4 怎么复算

探针当时是两个未跟踪文件：`test/plans/probe/fig1-two-inliners.plan.toml`（一个条目，`tool = "python"`，
`expect_stdout_contains = ["figure1_two_inliners: 3 of 3 graphs identical"]`）与
`test/plans/probe/fig1-two-inliners.py`（驱动：对每一步各跑两条命令、比字节，再各跑一次 `std::stats`
取节点数，最后打判词行）。**交回时已从工作树删除**（`test/plans/probe/` 是二十来条车道共用的目录，
留着会让别人误把它当目录靶子跑），原件归档在 `D:\Temp\misc-c4-and-figure1-probes\`。

页里不缺可复算的东西：§1 的遍列表 + §2 的字节数 + §5 的两行 `cmp` 就是全部口径；驱动没有别的花招，
重写它只需把 §2 的每行跑两次、比字节。§3 那 14 行是驱动当时的原话，取自运行日志
（`test/results/fetched/pass/probe.fig1_two_inliners/probe.fig1_two_inliners.three_graphs/stdout.txt`，
`.gitignore` 覆盖）。

命令（本轮实际用的那一行，`-Jobs 2` 是本车道探针的常态宽度）：

```powershell
& "D:\Projects\Camel-wt\misc\internal\tools\camel-remote.ps1" `
  -Path "D:\Projects\Camel-wt\misc" `
  -Plan "test/plans/probe/fig1-two-inliners.plan.toml" `
  -FetchFile "pass/probe.fig1_two_inliners/probe.fig1_two_inliners.three_graphs/stdout.txt" `
  -FromThisRun -Jobs 2 -QueueOnTreeLock
```

`-FetchFile` 把条目自己的 stdout 取回 `test/results/fetched/…`（`.gitignore` 覆盖），§3 的 14 行就是
那个文件。

不用探针、只想要一次 `cmp` 也可以（在主机上、二进制的 `CAMEL_HOME` 已设好时）：

```bash
camel test/cases/opt/cxx_isolation/paper_figure1.cml opt::inline opt::cse opt::algebra std::gir std::nop > a.dot
camel test/cases/opt/cxx_isolation/paper_figure1.cml std::cxx::inline opt::cse opt::algebra std::gir std::nop > b.dot
cmp a.dot b.dot && echo IDENTICAL   # 本轮 4431 字节 / 4431 字节
```

## §5 这一页回答了什么、没回答什么

**回答**：Figure 1 的三张图，`opt::inline` 版与 `std::cxx::inline` 版**逐字节相同**（7067 / 6155 /
4431 三对），并且这一对是在"内联这一步确实动了图"的前提下比的（4976 → 7067，`direct_calls` 2 → 0）。

**没回答**（不写成"大致相同"）：

- 没做**逐节点对齐**：字节相同是强于同构的证据，但本页没有把两边的节点 id 一一对上，也没有说
  "哪几个节点是同几个"；也没在别的程序上比（递归、闭包、growth budget 那几支在
  `internal/plans/2026-11-28-opt-inline.cn.md` §3 的 corpus 里，本页没测）。
- 没测 `growth_budget` 的边界（0、超出预算），也没测"内联器什么都不选"的程序。
- 本页的 `nodes`/`edges` 是 `std::stats` 的 `GRAPH_STATS` 计数（全程序所有图的 `Nxx` 之和），不是某一
  张图；与 `internal/reports/2026-11-28-figure1-dumps/counts.txt` 里按图逐个数的 19/28/24/18 对得上，
  但这属于同一口径的两次读数，不是两种独立方法的互证。