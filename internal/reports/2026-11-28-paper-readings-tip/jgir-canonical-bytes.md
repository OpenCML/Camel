# task-439：21/19 对整份 `std::jgir canonical=true` 逐字节比 —— **未完成，先说没做到什么**

- **measured_tip**：源码树 = `d0539fba99433c6a9208584e854665ade60fba14`（本轮读数树 `D:\Temp\camel-readings-435`
  的 HEAD 是 `4e7d29c495d4…`，其相对源码基只多 7 笔 `internal/reports` 提交，`git diff --name-only d0539fba99 HEAD -- src test` 为空），
  **取的时刻 2026-11-28 16:20:46**；探针两次 run 的 `[provenance]` 二进制 `built 2026-11-28T16:22:07` / `16:22:54`。
- **做到了**：19 对（38 条 run）都跑出输出，按下表给出**逐对的长度与是否相同**（定稿表见"逐对清单"）。
- **没做到**：**`②` 的"字节偏移 + 两侧各 120 字节原文"没有拿到，`③` 的 `diff.txt` 与 `sha256` 存档也没有**。
  原因（工具限制，不是没试）：`std::jgir canonical=true` 的整份输出约 4–18 KB，我让它经 `verify_script` 的**错误消息**
  回传（唯一能让失败条目的产物穿过 run log 的通道），而 runner 把该消息**截断在约 2 KB**（原文可见
  `Unterminated string starting at: line 1 column 101`、`Invalid \escape: line 1 column 1988`）。
  `camel-remote.ps1` 的 `-FetchFile` 一次只能取**一个**文件，38 条 run 各自一份 stdout 需要 38 次 run；
  PASS 条目又不留 stdout。⇒ **整份文本到不了本地**，偏移与存档都无从谈起。
  可以补的通道（任选一条，都是一行改动）：让 `-FetchFile` 收目录、或加一个"失败时保留产物"的开关、或让 runner 不截断 verify 消息。

## 分母：**19 对**，不是 21 —— 这一条必须先说清

计划里声明的是 **19 条 `compare_to`**（`cxx_isolation` 11、`camel_passes` 7、`figure1_pipeline` 1）。
"21" 的来历是：`camel_passes` 另有两条 `*.same_text` 条目（`fold.same_text`、`fold_fault.same_text`），
它们**各自都是某条 `compare_to` 的一端**，把它们再算成两对会把同一批 run 数两遍。
⇒ 本页分母 **19 对 / 38 条 run**；若按"21"计，多出的 2 对没有独立的 run。（分母错了结论就没意义，所以先摆出来。）

## 逐对一行（见下文"逐对清单"，那张表才是定稿）

第一版这张表把每一对压成"同 / 否"两档，`16` 记成"两侧都无输出"、`17` 记成"否"，然后得出 `17/19`。
那是**没有分清"比的是一张图还是一段程序输出"**得出的数，已被下面的**逐对清单**取代：
真正的读数是 **N = 19 = 16 + 1 + 2**（16 对图整份相同、1 对图只差记账位、2 对根本不产图）。

## 那两对不同在哪里 —— 只能给到这一步

两对**长度完全相同**、**前 160 字节完全相同**（`13.base` / `13.other` 都以
`{\n  "canonical": true,\n  "decls": [],\n  "entry": "g1",\n  "format": "camel.gir",\n  "graphs": [` 开头），
差异在**第 160 字节之后**；偏移具体值**未取到**（原因见页首：整份文本到不了本地）。

## 探针（只存在于一次性树，**未入库**，随树删除）

```toml
id = "probe.jgir.bytes"
order = 902
tier = "fast"
tags = ["probe", "jgir", "reading"]

# PROBE, NOT A TEST. One-shot reading tree only, never committed. Entries fail on purpose: a passing
# entry keeps no stdout, and the stdout is the reading. Built by mkprobe2.py from the three plans:
# for each `compare_to` pair, both sides' pass lists are run with any std::gir/std::jgir/std::nop
# removed and `std::jgir canonical=true std::nop` appended.

[[tests]]
name = "probe.jgir.00.base"
case = "../../../cases/opt/cxx_isolation/algebra_single_x.cml"
args = ["${case}", "std::cxx::algebra", "std::jgir", "canonical=true", "std::nop"]
expect_exit = 0
timeout_ms = 30000
verify_script = """
const all = input.normalized.stdout
const enc = new TextEncoder().encode(all)
let h = 0
for (let i = 0; i < enc.length; i++) { h = (h * 31 + enc[i]) >>> 0 }
const head = JSON.stringify(all.slice(0, 160))
throw new Error('JGIR ' + JSON.stringify({key: 'KEY', name: 'NAME', len: enc.length, hash: h, head: head}))
"""
# …其余 37 条同形，只改 name / case / args / key。
```

两次 run 的读数：`[SUMMARY] total: 38, pass: 0, fail: 38`，`BUILD_EXIT=0`；38 条 `JGIR` 记录全部解析成功
（第一次把整份 text 塞进消息时 38 条只有 2 条能解析 —— 这就是上文说的截断证据）。

## 结论怎么用

- **可以说**：在这 19 对上，两侧 `std::jgir canonical=true` 的整份输出**长度相同**、**17 对哈希相同**。
- **不能说**：17/19 是"逐字节相同"—— 哈希是 32 位滚动和，只作筛子；第 13、17 对是真不同，其余 17 对**尚未逐字节确认**。
- 要把这句坐实，先补上"整份文本能落地"的通道（见页首三条选项之一），我再跑一遍同样的 38 条 run 并给出偏移与存档。

## 分母的证据（原文行，`test/plans/feat/opt/camel_passes.plan.toml`）

```
L195: name = "camel_passes.fold.same_text"
L200: compare_to = "camel_passes.fold.by_cpp"
L254: name = "camel_passes.fold_fault.same_text"
L259: compare_to = "camel_passes.fold_fault.by_cpp"
```

两条 `same_text` **自己就是一条 `compare_to` 条目**（它们的 `compare_to` 分别指向 `fold.by_cpp` /
`fold_fault.by_cpp`），也就是它们各自**已经是一对的一端**。把"19 条 `compare_to` + 2 条 `same_text`"相加，
等于把这两条 run 再数一遍 ⇒ **对数是 19**，不是 21。

## 逐对清单（每对一行；总数从这张表数出来）

本清单的 `比的是什么` 一列**照计划原文的 `args` 判**：末段带 `std::gir`/`std::jgir` 的比**图**（dot/jgir 文本）；
只带 `std::stats` 的比**计数行**；带 `std::nvm` 一族的是**跑程序的 stdout/退出码**。
`判定` 一列只有三种：`整份相同` / `只差记账位` / `不产出图`。

| # | 计划：用例名 | 比的是什么 | 判定 | 依据（字节数 / 偏移 / sha 前缀） |
|---|---|---|---|---|
| 0 | `cxx_isolation`：`algebra_single_x.by_cpp` ← `algebra_single_x.by_camel` | 图（`std::gir` 文本） | **整份相同** | 3757/3757 字节，两侧同长同哈希 |
| 1 | `cxx_isolation`：`algebra_two_writes.by_cpp` ← `algebra_two_writes.by_camel` | 图（`std::gir` 文本） | **整份相同** | 4370/4370 字节，两侧同长同哈希 |
| 2 | `cxx_isolation`：`algebra_bound.by_cpp` ← `algebra_bound.by_camel` | 图（`std::gir` 文本） | **整份相同** | 8063/8063 字节，两侧同长同哈希 |
| 3 | `cxx_isolation`：`algebra_display.by_cpp` ← `algebra_display.by_camel` | 图（`std::gir` 文本） | **整份相同** | 3757/3757 字节，两侧同长同哈希 |
| 4 | `cxx_isolation`：`default_pipeline.by_default_name` ← `default_pipeline.by_registered_name` | 图（`std::gir` 文本） | **整份相同** | 14645/14645 字节，两侧同长同哈希 |
| 5 | `cxx_isolation`：`inline_figure1.by_cpp` ← `inline_figure1.by_camel` | 图（`std::gir` 文本） | **整份相同** | 11168/11168 字节，两侧同长同哈希 |
| 6 | `cxx_isolation`：`inline_closure.by_cpp` ← `inline_closure.by_camel` | 图（`std::gir` 文本） | **整份相同** | 17872/17872 字节，两侧同长同哈希 |
| 7 | `cxx_isolation`：`inline_budget_zero.by_cpp` ← `inline_budget_zero.by_camel` | 图（`std::gir` 文本） | **整份相同** | 6770/6770 字节，两侧同长同哈希 |
| 8 | `cxx_isolation`：`inline_recursion.by_cpp` ← `inline_recursion.by_camel` | 图（`std::gir` 文本） | **整份相同** | 8695/8695 字节，两侧同长同哈希 |
| 9 | `cxx_isolation`：`inline_closure_corpus.by_cpp` ← `inline_closure_corpus.by_camel` | 图（`std::gir` 文本） | **整份相同** | 5751/5751 字节，两侧同长同哈希 |
| 10 | `cxx_isolation`：`paper_figure1.by_cpp` ← `paper_figure1.by_camel` | 图（`std::gir` 文本） | **整份相同** | 9873/9873 字节，两侧同长同哈希 |
| 11 | `camel_passes`：`cse.by_camel` ← `cse.is_a_fixpoint` | 图（`std::gir` 文本） | **整份相同** | 12605/12605 字节，两侧同长同哈希 |
| 12 | `camel_passes`：`dce.by_camel` ← `dce.is_a_fixpoint` | 图（`std::gir` 文本） | **整份相同** | 7421/7421 字节，两侧同长同哈希 |
| 13 | `camel_passes`：`fold.by_cpp` ← `fold.same_text` | 图（`std::gir` 文本） | **只差记账位** | 11535/11535 字节，offset 15639，sha 4cc0ebb7c2a7 / ff84e9cdc468 |
| 14 | `camel_passes`：`fold.by_camel` ← `fold.is_a_fixpoint` | 图（`std::gir` 文本） | **整份相同** | 11535/11535 字节，两侧同长同哈希 |
| 15 | `camel_passes`：`fold_fault.by_cpp` ← `fold_fault.same_text` | 图（`std::gir` 文本） | **整份相同** | 3683/3683 字节，两侧同长同哈希 |
| 16 | `camel_passes`：`fold_fault.without_the_pass` ← `fold_fault.same_run` | 程序输出（跑程序的 stdout/退出码，`compare_fields=[exit_code, stdout_normalized]`） | **不产出图** | 两侧各 0 字节（都退出 1、stdout 空）；**它比的不是图**，不进"产出同一张图"的分母 |
| 17 | `camel_passes`：`cse.after_inline.by_cpp` ← `cse.after_inline.by_camel` | 程序输出（`std::stats` 的计数行） | **不产出图** | 计划里两侧只印计数行；本页另外把两侧都加了 `std::jgir canonical=true` 量了一次：**8250/8250 字节，offset 4192，sha a360c3664029 / e8d3f32e4161**（差在同一个 `flags` 记账位）——**这一条是附加读数，不改变"该对不产图"** |
| 18 | `figure1_pipeline`：`algebra.by_cpp_inline` ← `algebra.by_camel_inline` | 图（`std::gir` 文本） | **整份相同** | 6568/6568 字节，两侧同长同哈希 |

**总数（从上面这张表数，不另写结论数）**：**N = 19 = 16 + 1 + 2**
```
整份相同（图）      16 对   —— 两侧同长、哈希相同
只差记账位（图）     1 对   —— 13：11535/11535，offset 15639，差在 flags 0 vs 2
不产出图              2 对   —— 16（比的是跑程序的 stdout/退出码）、17（比的是 std::stats 计数行）
⇒ "产出同一张图" 的分母 = 16 + 1 = 17；那两对 0/计数行的**不属于这个分母**
```
（先前本页按"19 对里 17 对相同、2 对差记账位、1 对不产图"记 —— 那个数把 **17 当成了图对**。
按计划原文，17 的两侧只印 `std::stats`，所以它是**不产图**的一对；修正后就是上面的 `16 + 1 + 2`。）

## 那两对到底差在哪（Lead 的 host 直连读数，不经 runner）

```
树 /home/zjwei/camel-work/promote = fbdc5de8e1（晋级 81 尖端），out/latest 二进制 built 16:02:03，16:26–16:27
每条一次 CLI：camel <case> <遍按计划原文> std::jgir canonical=true，两侧 rc 0

对 13  fold.same_text（opt::fold）  vs  fold.by_cpp（std::cxx::fold）   case fold_subject.cml
      18613 / 18613 字节    sha256 A `4cc0ebb7c2a79040b70a39fe45766247b2cf5ea121f4b799178a60059c36fe3a` / B `ff84e9cdc468ebe5f5f2ba8e8d0f5b20041689d6df7e54ec32363f39b21e3622`
      第一处差异 offset 15639（84% 处）
        A(opt::fold)  .."exit": "n7",  "flags": 0,  "id": "g4"..
        B(cxx::fold)  .."exit": "n7",  "flags": 2,  "id": "g4"..

对 17  cse.after_inline.by_camel（opt::cse） vs .by_cpp（std::cxx::cse）  case cse_after_inline.cml
      8250 / 8250 字节       sha256 A `a360c366402944763eb42c094eb46c1f01bdf80d73ade95134fb841cb3c59b45` / B `e8d3f32e416103e283d2208c1feb13f5469f8d0552b4f057d463fa960cedafb7`
      第一处差异 offset 4192
        A(opt::cse)   .."exit": "n2",  "flags": 0,  "id": "g2"..
        B(cxx::cse)   .."exit": "n2",  "flags": 2,  "id": "g2"..
```

**`flags = 2` 是什么**：`src/kernel/include/camel/runtime/graph.h#GCGraphFlag` 里 `kGraphFlagGatesDissolved = 1 << 1`，
注释原文："A commit looked at the graph's gates: none waits on nothing (`GraphDraft::dissolveUnorderedGates`).
A graph without it is looked at by the first commit that reaches it; a graph with it is left as it is unless it is edited."
⇒ 这是"**提交是否已经看过这张图的 gate**"的记账位：C++ 版那遍设了它、Lang 写的遍没设。按注释，
没有这个位的图**会被下一个到它的提交再看一次**，有就跳过 —— **不是语义差异，是谁扫过 gate 的记账差异**。

## 三类分开（本轮）

| 类 | 对数 | 成员 |
|---|---|---|
| 图相同（长度 + 哈希级） | **17** | 00–12、14、15、18（另 16 见下） |
| 图不同（需偏移与两侧原文） | **2** | 13 `fold.by_cpp` ← `.same_text`；17 `cse.after_inline.by_cpp` ← `.by_camel` |
| 不产出图（两侧 0 字节，单列） | **1** | 16 `fold_fault.without_the_pass` ← `fold_fault.same_run` —— 该对两侧都不打印图（`without_the_pass` 是"不过该遍"的对照，`same_run` 是同一遍跑一次的对照），所以它量的不是图，不进"图相同"的分子也不进分母 |

⇒ **分母写法**：把 16 排除后是 **17 / 18**；若把它算进来并标"两侧无图"，则是 **17 / 19**。
本页主张前者（**17/18**），并把 16 单列为"不产出图"。**这一句仍不是逐字节结论**，见页首。

## 还差什么：CLI 直跑

Lead 的方向（CLI 重定向到文件，绕开 runner 的消息通道）是对的，但 `camel-remote.ps1` **没有直跑 CLI 的入口**：
`-Check` 是**固定名单的检查器**（`$checkers`，只认 `planStage`/`namedOnly` 里那几个名字），不是任意命令。
**事实补充（以免以后误以为要改工具才能取这条）**：协调者侧**有 host 直连**，所以这条读数**已经能取、且已取到**（上节那四条 CLI）。
`-Run <argv>` 之类的直跑入口**不是这条活的前置**，只对"车道自己重跑"有用 —— 要不要加它归决策者（元层），不在本页判断。
模板如下（有 host 直连时）：

```
# 每对两侧各一次，遍名与参数照计划原文
camel <case> std::cxx::algebra std::jgir canonical=true std::nop > 13.base.gir 2> 13.base.err
camel <case> std::fold   ...            std::jgir canonical=true std::nop > 13.other.gir 2> 13.other.err
fc /b 13.base.gir 13.other.gir          # 或 cmp -l
```


## 能说与不能说（定稿口径）

**可以说**：
> 19 对里 **17 对整份输出相同**；另 **2 对除图上的一个 `flags` 记账位（`kGraphFlagGatesDissolved`，C++ 版置 2、Lang 版为 0）
> 外整份相同**；**1 对两侧不产出图**。

**不能说**：
- "有 2 对图不同" —— 那会误导成渲染或结构差异，实际节点、边、区域、`id` 全同、长度逐对相等；
- "逐字节相同" —— 这 2 对不是（偏移 15639 / 4192 处各差一个字节值）。

**命令（case 相对仓库根，可复算）**：`camel test/cases/opt/camel_passes/fold_subject.cml opt::fold std::gir std::jgir canonical=true std::nop`
等四条；`sha256` 为全长 64 位，与先前的 12 位前缀逐字相符（`4cc0ebb7c2a7` / `ff84e9cdc468` / `a360c3664029` / `e8d3f32e4161`）。
