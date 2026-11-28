# Figure 1 的三张图：`opt::inline` 版与 `std::cxx::inline` 版，逐字节

**状态：可引用读数（citable reading）——限于这一个程序、这一台主机、这一个尖端。**
本页的数字全部来自**一次**运行（2026-11-28 18:12 +08:00，尖端 `c41814ea5e`），驱动随读数一起
自报身份，命令、`[load]` 行、两侧 SHA-256 都在下面，可按 §4 原样重取。**不是终稿**：只覆盖
Figure 1 这一支程序（见 §5 的"没回答"）。

- **尖端**：`c41814ea5eb2356045f0edaab2498f17a66c20b6`（= 当时的 `origin/integration`），
  车道工作树 `D:\Projects\Camel-wt\paper-readings`，分支 `lane/paper-readings`，主机构出
  `/home/zjwei/camel-work/paper-readings`。`[provenance] tip c41814ea5eb2356045f0edaab2498f17a66c20b6
  | binary built 2026-11-28T18:10:04+08:00 | tree /home/zjwei/camel-work/paper-readings | tier fast | jobs 4`。
- **驱动**：一个一次性探针脚本，**不在本页的提交里、也不进 integration**（探针纪律：活一次、用完删）。
  它在运行时自报 `driver.self_sha256=69f8fb22c99cc03182f37a294f53fa0131f7b7d73ac3439bf6791b749badb1da`
  ——**"主机上跑的是哪一份"由此回答**，而不是靠仓库里有没有这个文件。复算方法见 §4：命令、输入、
  以及"怎么解析这台机器上的输出"都在文字里，不需要那份 `.py` 也能重做。
- **负载**：跑前 `[load] 31.45 41.71 42.81 (1m/5m/15m)`，跑后 `[load] 29.77 41.01 42.57`；
  运行器自己打了 `WARNING: 1m load 29.77 > 8 —— readings taken now are not comparable to
  quiet-host readings`。**所以本页不写任何墙钟结论**；图的字节数与计数不随负载变。
- **机器**：主机 `zjwei`（Linux，令牌池 24 个 worker），`-Jobs 4`。
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
比的是 SHA-256，任何一处字节差异都会让两侧哈希分开，不是"看上去一样"。

三张图 = 管线的三步：① `inline` 之后、② `+ opt::cse` 之后、③ `+ opt::cse + opt::algebra` 之后。
另加一步 `before`（**不过任何内联器**），它是起点、不是三张图之一。

## §2 四条读数（本轮，尖端 `c41814ea5e`）

| 步 | 遍列表（两侧只差第一步） | camel 字节 | cpp 字节 | 两侧相同 | 两侧 SHA-256（相同则一个值） |
|---|---|---|---|---|---|
| `before`（起点，无内联器） | `std::gir std::nop` | 4499 | —（只有一条命令） | — | `b38054da0936f18bdc7d228ebe37676385205160ff026a01b793993f6505e8a2` |
| ① `inline` | `opt::inline` / `std::cxx::inline` | 6338 | 6338 | **是** | `e87c42e24890a1d50f8cf110ec907ec8d1e09c25f8824adfb589b9f073d086e1` |
| ② `inline_cse` | 同上 `+ opt::cse` | 5538 | 5538 | **是** | `61f5c74fb9facba04fd373f62e8c31ed2ad88372bfa2e3cbeebc17693811ff53` |
| ③ `inline_cse_algebra` | 同上 `+ opt::cse + opt::algebra` | 4038 | 4038 | **是** | `61619c59ff32d1e76a6e46c89642b0518d6980bf64322aad9f78ed8f886dd8a3` |

`nodes` / `edges` / `direct_calls` / `graphs` 取自同一条命令里的 `std::stats`（`GRAPH_STATS` 行，
本页把这一行从 dot 文本里剔掉，所以"字节"是 dot 本身）：

| 步 | camel nodes / edges / direct_calls | cpp nodes / edges / direct_calls | graphs（两侧） |
|---|---|---|---|
| `before` | 19 / 16 / 2 | — | 4 |
| ① `inline` | 28 / 26 / 0 | 28 / 26 / 0 | 4 / 4 |
| ② `inline_cse` | 24 / 22 / 0 | 24 / 22 / 0 | 4 / 4 |
| ③ `inline_cse_algebra` | 18 / 14 / 0 | 18 / 14 / 0 | 4 / 4 |

`GRAPH_STATS` 这一版记录的键是（驱动把收到的键名打出来了，不是我猜的）：
`branches,bytes,closures,cycle_dropped_sessions,direct_calls,edges,graph_names,graphs,indirect_calls,loops,macro,nodes,opers,specializations`。

这张表里最要紧的一行不是"相同"，是起点：**4499 → 6338**，即内联这一步确实动了图
（19 → 28 个节点、`direct_calls` 2 → 0）。所以①对起点的不同、②③对①的逐字节相同，不是
"内联器什么都没干、拿同一张图跟自己比"那种空判。

## §3 驱动打出来的原文（本轮，逐字；驱动故意以 1 结束，读数就是它的 stdout）

四条条目各打印自己那一步（`-Jobs 4`，`[SUMMARY] total: 4, pass: 0, fail: 4`，**故意红**：
这些条目没有断言可下，读数本身就是输出）：

```
driver.self_sha256=69f8fb22c99cc03182f37a294f53fa0131f7b7d73ac3439bf6791b749badb1da
case=test/cases/opt/cxx_isolation/paper_figure1.cml stage=before
cmd[camel]=…/out/latest/bin/camel test/cases/opt/cxx_isolation/paper_figure1.cml std::stats std::gir std::nop
route=camel rc=0 bytes=4499 sha256=b38054da0936f18bdc7d228ebe37676385205160ff026a01b793993f6505e8a2
stats[camel]=nodes:19 edges:16 direct_calls:2 graphs:4 bytes:716
```
```
case=… stage=inline
cmd[camel]=… paper_figure1.cml opt::inline std::stats std::gir std::nop
route=camel rc=0 bytes=6338 sha256=e87c42e24890a1d50f8cf110ec907ec8d1e09c25f8824adfb589b9f073d086e1
stats[camel]=nodes:28 edges:26 direct_calls:0 graphs:4 bytes:1088
cmd[cpp]=… paper_figure1.cml std::cxx::inline std::stats std::gir std::nop
route=cpp rc=0 bytes=6338 sha256=e87c42e24890a1d50f8cf110ec907ec8d1e09c25f8824adfb589b9f073d086e1
stats[cpp]=nodes:28 edges:26 direct_calls:0 graphs:4 bytes:1088
identical=True
```
```
case=… stage=inline_cse
cmd[camel]=… paper_figure1.cml opt::inline opt::cse std::stats std::gir std::nop
route=camel rc=0 bytes=5538 sha256=61f5c74fb9facba04fd373f62e8c31ed2ad88372bfa2e3cbeebc17693811ff53
stats[camel]=nodes:24 edges:22 direct_calls:0 graphs:4 bytes:928
cmd[cpp]=… paper_figure1.cml std::cxx::inline opt::cse std::stats std::gir std::nop
route=cpp rc=0 bytes=5538 sha256=61f5c74fb9facba04fd373f62e8c31ed2ad88372bfa2e3cbeebc17693811ff53
stats[cpp]=nodes:24 edges:22 direct_calls:0 graphs:4 bytes:928
identical=True
```
```
case=… stage=inline_cse_algebra
cmd[camel]=… paper_figure1.cml opt::inline opt::cse opt::algebra std::stats std::gir std::nop
route=camel rc=0 bytes=4038 sha256=61619c59ff32d1e76a6e46c89642b0518d6980bf64322aad9f78ed8f886dd8a3
stats[camel]=nodes:18 edges:14 direct_calls:0 graphs:4 bytes:664
cmd[cpp]=… paper_figure1.cml std::cxx::inline opt::cse opt::algebra std::stats std::gir std::nop
route=cpp rc=0 bytes=4038 sha256=61619c59ff32d1e76a6e46c89642b0518d6980bf64322aad9f78ed8f886dd8a3
stats[cpp]=nodes:18 edges:14 direct_calls:0 graphs:4 bytes:664
identical=True
```

## §4 怎么复算（探针是临时枝上的一次性脚本，**不随本页提交**）

复算需要的只有三段文字：**命令**、**输入**、**怎么解析这台机器的输出**。

命令（本轮实际执行的那一条：把驱动推到主机的工作树上，然后在那边按 stage 跑四次）：

```powershell
& "D:\Projects\Camel-wt\witness\internal\tools\camel-remote.ps1" `
  -Path "D:\Projects\Camel-wt\paper-readings" -NoBuild -QueueOnTreeLock -NoGpuWatch
```

```bash
# 在主机 /home/zjwei/camel-work/paper-readings 上，CAMEL_HOME 已设好；
# <probe.py> 是临时枝上那个脚本（它第一行会打印自己的 sha256，见页头）：
for stage in before inline inline_cse inline_cse_algebra; do
  python3 <probe.py> "$(command -v camel)" \
      test/cases/opt/cxx_isolation/paper_figure1.cml "$stage"
done
```

给要自己写这个探针的人，四个要点（这几条都是这一轮踩出来的，不是常识）：

1. **读数要自己打印成 stdout**：条目若没有断言可下，就让它故意以非零结束——只有**失败条目**的
   stdout 会被回显；通过条目不打印任何东西。
2. **`std::jgir` 的 stdout 是 JSON 加上程序自己的输出**（这里是 `collapse dh=22.0` 那类行），
   所以用 `json.JSONDecoder().raw_decode(text)` 取第一段，`json.loads` 会报 `Extra data`。
3. **`std::stats` 要放在 `std::gir` 之前**：它统计的是随后被打印的那张图；把 `GRAPH_STATS` 那一行
   剔掉剩下的字节数就是 dot 的字节。
4. **回显会被截断**（`omitted N line(s) above -- full output: …`）：完整输出在宿主的
   `test/results/log/<run>/fail/<条目>/stdout.txt`，直接 `ssh <host> cat` 取回来，**不要为了看全再跑一遍**。

另一个坑：把驱动留成**未跟踪**文件时，远端同步（`[camel-remote] … 0 changed file(s), 2 untracked`）
不会把它带过去，主机上跑的其实是别的东西，而读数里看不出这件事。所以驱动必须在**同一棵被推送的树**里
（临时枝上提交，用完删枝），并且第一行打自己的 SHA-256——**"主机上跑的是哪一份"由读数自己回答**。

单侧复算（在主机上、`CAMEL_HOME` 已设好时；`std::stats` 在前，取计数，再把
`GRAPH_STATS` 那一行剔掉就是 dot 的字节）：

```bash
camel test/cases/opt/cxx_isolation/paper_figure1.cml opt::inline opt::cse opt::algebra std::stats std::gir std::nop
camel test/cases/opt/cxx_isolation/paper_figure1.cml std::cxx::inline opt::cse opt::algebra std::stats std::gir std::nop
```

两行去掉 `GRAPH_STATS …` 后应当逐字节相同（本轮 4038 / 4038 字节、
`61619c59…` 一个哈希）；其余三步照 §2 表里的遍列表替换第一步即可。

## §5 这一页回答了什么、没回答什么

**回答**：Figure 1 的三张图，`opt::inline` 版与 `std::cxx::inline` 版**逐字节相同**
（6338 / 5538 / 4038 三对，两侧 SHA-256 各自相等），并且这一对是在"内联这一步确实动了图"的前提下
比的（4499 → 6338，`direct_calls` 2 → 0，节点 19 → 28）。

**没回答**（不写成"大致相同"）：

- 没做**逐节点对齐**：字节相同是强于同构的证据，但本页没有把两边的节点 id 一一对上，也没有说
  "哪几个节点是同几个"；也没在别的程序上比（递归、闭包、growth budget 那几支在
  `internal/plans/2026-11-28-opt-inline.cn.md` §3 的 corpus 里，本页没测）。
- 没测 `growth_budget` 的边界（0、超出预算），也没测"内联器什么都不选"的程序。
- 本页的 `nodes`/`edges` 是 `std::stats` 的 `GRAPH_STATS` 计数（全程序四个图的合计），不是某一
  张图；驱动只是把记录里的键原样打印出来，没有按图拆开。
- **旧草稿的字节数不再引用**：本页此前写过 `4976 / 7067 / 6155 / 4431`，那是另一次运行、另一个
  驱动（当时留成未跟踪文件、交回时已删）的计数，与本轮 `4499 / 6338 / 5538 / 4038` **不可混用**；
  两个驱动各自的"字节"口径没有被证明相同，所以本页只把本轮的数字当读数。两轮**一致**的是
  节点数 `19 / 28 / 24 / 18` 与结论（三对逐字节相同）——那是两个驱动各自算出来又对上了的部分。