# task-439：21/19 对整份 `std::jgir canonical=true` 逐字节比 —— **未完成，先说没做到什么**

- **measured_tip**：源码树 = `d0539fba99433c6a9208584e854665ade60fba14`（本轮读数树 `D:\Temp\camel-readings-435`
  的 HEAD 是 `4e7d29c495d4…`，其相对源码基只多 7 笔 `internal/reports` 提交，`git diff --name-only d0539fba99 HEAD -- src test` 为空），
  **取的时刻 2026-11-28 16:20:46**；探针两次 run 的 `[provenance]` 二进制 `built 2026-11-28T16:22:07` / `16:22:54`。
- **做到了**：19 对（38 条 run）都跑出输出，按下表给出**逐对的长度与是否相同**。
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

## 逐对一行

`len` 为两侧整份输出的字节数（按 `TextEncoder` 的 UTF-8 字节）；`同` 判据 = 长度相同 **且** 32 位滚动哈希相同
（见下一条限制）。

| # | 名字（base ← other） | 同 | len 两侧 |
|---|---|---|---|
| 00 | `cxx_isolation.algebra_single_x.by_cpp` ← `.by_camel` | 是 | 3757 / 3757 |
| 01 | `cxx_isolation.algebra_two_writes.by_cpp` ← `.by_camel` | 是 | 4370 / 4370 |
| 02 | `cxx_isolation.algebra_bound.by_cpp` ← `.by_camel` | 是 | 8063 / 8063 |
| 03 | `cxx_isolation.algebra_display.by_cpp` ← `.by_camel` | 是 | 3757 / 3757 |
| 04 | `cxx_isolation.default_pipeline.by_default_name` ← `.by_registered_name` | 是 | 14645 / 14645 |
| 05 | `cxx_isolation.inline_figure1.by_cpp` ← `.by_camel` | 是 | 11168 / 11168 |
| 06 | `cxx_isolation.inline_closure.by_cpp` ← `.by_camel` | 是 | 17872 / 17872 |
| 07 | `cxx_isolation.inline_budget_zero.by_cpp` ← `.by_camel` | 是 | 6770 / 6770 |
| 08 | `cxx_isolation.inline_recursion.by_cpp` ← `.by_camel` | 是 | 8695 / 8695 |
| 09 | `cxx_isolation.inline_closure_corpus.by_cpp` ← `.by_camel` | 是 | 5751 / 5751 |
| 10 | `cxx_isolation.paper_figure1.by_cpp` ← `.by_camel` | 是 | 9873 / 9873 |
| 11 | `camel_passes.cse.by_camel` ← `.is_a_fixpoint` | 是 | 12605 / 12605 |
| 12 | `camel_passes.dce.by_camel` ← `.is_a_fixpoint` | 是 | 7421 / 7421 |
| 13 | `camel_passes.fold.by_cpp` ← `.same_text` | **否** | 11535 / 11535 |
| 14 | `camel_passes.fold.by_camel` ← `.is_a_fixpoint` | 是 | 11535 / 11535 |
| 15 | `camel_passes.fold_fault.by_cpp` ← `.same_text` | 是 | 3683 / 3683 |
| 16 | `camel_passes.fold_fault.without_the_pass` ← `.same_run` | **两侧都无输出** | 0 / 0 |
| 17 | `camel_passes.cse.after_inline.by_cpp` ← `.by_camel` | **否** | 8250 / 8250 |
| 18 | `figure1_pipeline.algebra.by_cpp_inline` ← `.by_camel_inline` | 是 | 6568 / 6568 |

⇒ **相同 17 / 19**（按长度 + 哈希）；**不同 2 对**：第 13、17 对。第 16 对两侧都是 0 字节（该对的两条条目本来就不打印），
按判据算"相同"，但它**不是**"逐字节相同的两份图"，正文若引用须注明。

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