# 论文侧读数刷新（task-435）

两轮读数，**各挂各的尖端**，不混写：

| 轮 | 读数 | measured_tip（全长） | 取的时刻 |
|---|---|---|---|
| 一 | 评测 B 21 对 | `beae6726f90c2cb6ef702974d956abb748dc1ded` | 2026-11-28 16:10:22 (+08:00) |
| 二 | Figure 1 三对（体积 + 图计数） | `d0539fba99433c6a9208584e854665ade60fba14` | 2026-11-28 16:14:05 (+08:00) |

树：一次性 detached 工作树 `D:\Temp\camel-readings-435`（跑前 `dirty 0`；第二轮因探针未跟踪而 `dirty: 1`，探针本身即读数、未入库）。
二进制：`built 2026-11-28T16:11:17` / `16:11:58` / `16:12:25`（第一轮三次 run 各自 build）；`16:15:31`（第二轮）。
`tier fast, jobs 2`，宿主 `[load]` 19.85–28.97 (1m)（其中约 3 核非 Camel：qemu/sidekiq/llm_server）——**本页没有一处结论依赖时间**。

---

## 1. 评测 B：19 条 `compare_to` + 2 条 `same_text` = 21 对，全部相同（tip `beae6726f9`）

| 计划 | 条目 | `compare_to` | 读数 |
|---|---|---|---|
| `test/plans/feat/opt/cxx_isolation.plan.toml` | 27 | 11 | `[SUMMARY] total: 27, pass: 27, fail: 0`，`RUN_EXIT=0` |
| `test/plans/feat/opt/camel_passes.plan.toml` | 49 | 7 | `[SUMMARY] total: 49, pass: 49, fail: 0`，`RUN_EXIT=0` |
| `test/plans/feat/opt/figure1_pipeline.plan.toml` | 6 | 1 | `[SUMMARY] total: 6, pass: 6, fail: 0`，`RUN_EXIT=0` |

⇒ **计划声明的比较字段，两侧全部相同**：19 条 `compare_to` 全 PASS，另加两条 `same_text` 条目，共 **21 对**。

**字段构成（实测，按条目点名）**：`compare_fields` 一共 19 条 ——
`camel_passes` 里 **1 条三项**（`["exit_code","stdout_normalized","stderr_normalized"]`）、**6 条两项**；
`cxx_isolation` **11 条全部两项**；`figure1_pipeline` **1 条两项**。
合计 **1 条三项 + 18 条两项**。

**口径更正（写清以免正文写错）**：这 21 对里**没有一对**是在 `std::jgir canonical=true` 下比的，也
**没有一对**按三字段比 —— `canonical=true` 在 `cxx_isolation` 出现 29 处，但全在**非配对**条目（单独打印
`std::jgir` 与 golden / verify 脚本比）。因此本页能给的读数是"**计划声明字段两侧相同**"，不能给
"`jgir canonical` 下三字段逐字节相同"。

## 2. Figure 1 三对：体积与图计数（tip `d0539fba99`）

同一份程序 `test/cases/opt/cxx_isolation/paper_figure1.cml`（`inc(x) * 3 + (x + 1) * 4`，印 `21`），
同样下游且同序，两条入口只差第一步（`opt::inline` / `std::cxx::inline`），`std::stats` 读计数、`std::gir` 收尾。

| 步 | 遍列表 | 体积 字节（Camel / C++） | nodes | edges | opers | direct_calls | indirect_calls |
|---|---|---|---|---|---|---|---|
| `before`（起点，不过内联器） | `std::stats std::gir std::nop` | **5214**（单侧） | 19 | 16 | 6 | 2 | 1 |
| ① `inline` | [opt::inline] 或 [std::cxx::inline] | **7431 / 7431** | 28 / 28 | 26 / 26 | 12 / 12 | 0 / 0 | 1 / 1 |
| ② `inline_cse` | `+ opt::cse` | **6463 / 6463** | 24 / 24 | 22 / 22 | 10 / 10 | 0 / 0 | 1 / 1 |
| ③ `inline_cse_algebra` | `+ opt::cse + opt::algebra` | **4627 / 4627** | 18 / 18 | 14 / 14 | 6 / 6 | 0 / 0 | 1 / 1 |

- **两侧逐项相同**：体积三对相同（7431/6463/4627），计数三对相同。
- **起点这一行（`before`，不过内联器）**：体积 **5214**、nodes **19**、edges **16**、opers **6**、`direct_calls` **2**、`indirect_calls` **1**，单侧（该步没有第二条入口）。
- ⇒ 一句话：**起点 5214 / 内联 7431：nodes 19→28、`direct_calls` 2→0、edges 16→26、opers 6→12**。与旧页比：**nodes/edges/direct_calls 一个没变**（19/16/2），**体积 4976 → 5214（+238）** 同样是渲染变长，不是图变了。
- **起点这一行（efore，不过内联器）**：体积 **5214**、nodes **19**、edges **16**、opers **6**、direct_calls **2**、
  indirect_calls **1**，单侧（该步没有第二条入口）。⇒ 一句话：**起点 5214 / 内联 7431：nodes 19→28、direct_calls 2→0、
  edges 16→26、opers 6→12**。与旧页比：**nodes/edges/direct_calls 三个数一个没变**（19/16/2），
  **体积 4976 → 5214（+238）** 同样是渲染变长，不是图变了。
- **这张表的 `opers`、`indirect_calls` 两列是本轮首次取到的量**，旧页（igure1-two-inliners.md，tip 25dcda7a2b3）没有
  这两个字段 —— 它只有 direct_calls。所以 opers 12/10/6 与 indirect_calls 1/1 **不能与旧页混排**：它们不是
  "旧值 → 新值"，是"本页第一次有值"。
- **与旧页（tip `b25dcda7a2b3`）的差，逐项说清**：旧页 `7067 / 6155 / 4431`。
  **体积变了**：`+364 / +308 / +196` 字节；而 **nodes、edges 一个没变**（19→28、28→24、24→18 与 16→26、26→22、22→14）。
  ⇒ 差在**渲染**，不在图：节点与边相同、`opers` 计数相同，dot 文本变长是行内容变长（属性/字段），
  不是多了节点。旧体积不可沿用，正文里若写 7067/6155/4431 应改为本行的三个数。
- 旧页的 `direct_calls 2 → 0`（起点 19 节点、2 直接调用）在本 tip 仍成立（三对均 `direct_calls=0`）；
  本 tip 的 `GRAPH_STATS` 多给了 `indirect_calls`，三对均为 **1**。

**体积的取法（读数的一部分）**：条目 stdout = `GRAPH_STATS {…}` 一行 + dot 文本；体积 = **去掉 `GRAPH_STATS` 行后
余下各行以 `\n` 连接的长度**（`dot.length`）。不是整段 stdout 的字节数，也不含 `std::stats` 行。

### 探针原文（只存在于一次性树 `D:\Temp\camel-readings-435`，**未入库**，随树删除）

```toml
id = "probe.figure1.readings"
order = 900
tier = "fast"
tags = ["probe", "figure1", "reading"]

# PROBE, NOT A TEST. Entries fail on purpose: a passing entry keeps no stdout, and the stdout is the
# reading. The verify script parses the stdout and throws the numbers back, so the run log carries them.

[[tests]]
name = "probe.fig1.inline.camel"
case = "../../../cases/opt/cxx_isolation/paper_figure1.cml"
args = ["${case}", "opt::inline", "std::stats", "std::gir", "std::nop"]
expect_exit = 0
timeout_ms = 30000
verify_script = """
const all = input.normalized.stdout
const lines = all.split('\n')
const stats = lines.filter(l => l.startsWith('GRAPH_STATS'))
const dot = lines.filter(l => !l.startsWith('GRAPH_STATS')).join('\n')
throw new Error('PROBE ' + JSON.stringify({key: 'inline.camel', bytes: dot.length, stats: stats.join('|')}))
"""
# …同形的另五条：inline.cpp（第一步换 std::cxx::inline）、inline_cse.{camel,cpp}（+ opt::cse）、
#   inline_cse_algebra.{camel,cpp}（+ opt::cse + opt::algebra）。argv 与 verify 脚本逐字同形，只改名字、遍列表、key。
```

运行读数（原文，六行）：
```
PROBE {"key":"inline.camel","bytes":7431,"stats":"GRAPH_STATS {\"graphs\":4,\"nodes\":28,\"edges\":26,\"opers\":12,\"direct_calls\":0,\"indirect_calls\":1,\"closures\":0,\"branches\":0,\"loops\":0,\"bytes\":1088,…}"}
PROBE {"key":"inline.cpp","bytes":7431,"stats":"…\"nodes\":28,\"edges\":26,\"opers\":12,\"direct_calls\":0,\"indirect_calls\":1…"}
PROBE {"key":"inline_cse.camel","bytes":6463,"stats":"…\"nodes\":24,\"edges\":22,\"opers\":10,\"direct_calls\":0,\"indirect_calls\":1…"}
PROBE {"key":"inline_cse.cpp","bytes":6463,"stats":"…\"nodes\":24,\"edges\":22,\"opers\":10,\"direct_calls\":0,\"indirect_calls\":1…"}
PROBE {"key":"inline_cse_algebra.camel","bytes":4627,"stats":"…\"nodes\":18,\"edges\":14,\"opers\":6,\"direct_calls\":0,\"indirect_calls\":1…"}
PROBE {"key":"inline_cse_algebra.cpp","bytes":4627,"stats":"…\"nodes\":18,\"edges\":14,\"opers\":6,\"direct_calls\":0,\"indirect_calls\":1…"}
[SUMMARY] total: 6, pass: 0, fail: 6   （六条全部是探针，故意红 —— 见下）

**为什么这六条红、为什么这是合规的**：计划 runner **只在条目失败时**把 stdout 留在运行目录（PASS 条目
"nothing was written"，-FetchFile 取不到任何 pass 产物），而本页要的正是 stdout（dot 文本 + GRAPH_STATS）。
所以探针**故意让 verify 脚本抛错**，把量到的数字放进错误消息里 —— **红的产物就是上面六行 PROBE 原文**，
不是缺陷、不是回归；探针只存在于一次性读数树 D:\Temp\camel-readings-435，**从未进入仓库**，跑完随树删除，
正文里也不引用它的 PASS/FAIL 状态。读到本页的人若在别处看到"探针红"，请按本条理解。
```

## 3. "6/6 对图相同、四对两侧都不内联、两对两侧各展开一个普通调用（nodes +9 / edges +10 / operators +6）"

**该句在当前仓库里无可追溯出处，无法重取。** 搜法与结果（tip `d0539fba99` 上执行）：

- `git grep -l "7067" -- internal docs test` → 6 个文件：`internal/backlog.md`、
  `internal/reports/2026-11-26-the-date-is-a-record.cn.md`、`internal/reports/2026-11-28-c4-and-figure1/figure1-two-inliners.md`、
  `internal/reviews/questions-2026-10-29.md`、`test/plans/feat/parse/golden/ast_hashes.txt`、一份 perf 结果。
  其中唯一与 Figure 1 有关的是 `figure1-two-inliners.md` —— 它是**三步 × 两条入口 = 3 对**（7067/6155/4431），
  计数用 `direct_calls`（2 → 0），**没有 `operators` 这个量**。
- `git grep -l "nodes +9"` / `"edges +10"` / `"operators +6"` → **零命中**。
- `test/cases/opt/cxx_isolation/` 下只有 `paper_figure1.cml` 一份该程序的用例，**没有六个成对的用例集**。

**后果**：本页不产出那六个 program 的读数，也不拿 3 对冒充 6 对；若正文要这句，需要有那份仪器
（六份 case 名、两条入口、`operators` 的定义）重测，或从正文撤回。此项处置不在本页。

## 4. 这台机器上此后尖端走到了哪里

第一轮 `beae6726f9`（16:10:22）→ 第二轮 `d0539fba99`（16:14:05）→ 交回时再取一次：`11b21d79071f40b5c935d442be17cdae05a41cac`（16:13:15）
/ 之后继续推进。**本页读数只对各自那两行 sha 成立**；越过即需重跑同样 run（每条约 40 s，含增量 build）。

