# 那 11 条"每次都红"的用例，一条一条对

**问句**：`internal/reports/2026-11-24-test-suite-inventory.cn.md` §③ 里"13 次全快层跑每次都红"的
11 条，今天为什么红，该修还是该记成既知的红？

## 口径

- **读数来自构建机（linux-x64）2026-11-24 的一次实跑**：`e66efb7a7b`（干净），本次分支未改产品代码，
  `camel-remote.ps1 -Plan <这 11 条所在的 8 个计划> -Jobs 8`，`[SUMMARY] total: 343, pass: 294,
  fail: 48, skip: 1`，CPU 时间在录制时读得到（`[CPU] 13/13` 是同一台机器上另一条命令）。
- **那 13 次记录是 Windows 开发机上跑的**（2026-11-02 … 11-12），所以"每次都红"是那个平台的读数，
  不是这棵树的读数。这条差别是下面每一行的关键。
- **今天的树本身是红的**，与这 11 条无关：那 343 条里 48 条红，绝大多数是**一条还没落地的 lane**
  （`C-1123-61`，`lane/op-overload-merge`：`Range` 的 `to_array`/`map`/`filter` 还没进编译器，
  `src/stdlib/random.cml:114` 里就留着这笔债的注释）加上 11-24 那次图编号改名带来的金标漂移。
  所以下文的"绿"是"这棵树在 Linux 上绿"，不是"全树绿"。
- 因此这份记录**不写"某一层是绿的"**：它只写这一条用例今天在构建机上是 `PASS` 还是 `FAIL`，以及
  它与 batch-1 报告的红表是不是同一条。在大树复绿之前，"层是绿的"这句话不成立；能报的只有
  "本层红表与上一份红表逐条比，新增红 N 条"——红数不是判据，红表才是。

## 11 条

| # | 用例 | 今天在构建机上 | 为什么红 | 做了什么 |
|---|---|---|---|---|
| 1 | `modules.rules` / `rules.accuracy_check_compares_error` | **PASS** 124 ms | Windows/macOS 上 `rules` 库的参考值是 `long double`（`modules/rewrite/module.cpp:1832`），MSVC 与 Apple arm64 的 `long double` 就是 `double`，参考值自己那一 ulp 被算成误差，规则被 `refuted`：`refuted accurate_log1p … error 7.28033e-16 exceeds the original's 0 (ratio 1.63938)` | 已修：`9d01307b9e`（2026-11-22，"accuracy: the reference is a wide type of its own, and the checks read declared bounds"）——参考值换成自己的宽类型，断言读声明的 ulp 界而不是逐字比值。本分支未改 |
| 2 | `modules.rules` / `rules.accuracy_library_passes_the_check` | **PASS** 143 ms | 同上（同一页的另一个读数） | 同上 |
| 3 | `modules.rules` / `rules.every_exported_rule_enters_the_check` | **PASS** 2.66 s | 同上：它断言"没有一条规则被 refuted"，Windows 上那四条会 refute | 同上 |
| 4 | `tools.load_class` / `load_class.shared_place` | **PASS** 5.70 s（Linux 上跑） | 它要读子进程的 CPU 时间（`/proc/self/stat`），Windows 上没有这个文件，那个"必须红一次"的夹具变成绿，于是断言"紧的那个界能让一条跑挂"失败 | 已按树里现成的机制声明：`ab856b6622`（2026-11-22）加了 `skip_unless = ["node", "test/tools/have-child-cpu.mjs"]`，Windows 上是 skip 不是红。本分支未改 |
| 5 | `tools.load_class` / `load_class.explain_under_load` | **PASS** 4.42 s（Linux 上跑） | 竞态夹具在 Windows 上跑不满 8 轮：`race-harness.mjs --rounds 8 --load 8` 的读数在 Windows 上是红的（13/13），可**仓库里没留下它那次的原话**——`test/results` 只在负责人机器上 | 没改，也没标：它的原因（竞态夹具在 Windows 上的读数）没有一句可以引用的诊断，标 `known_fail` 就得自己编一句。它已随本批移进慢层（每一条都超 2 s），所以它不再红快层 |
| 6 | `exemplars` / `08_structured_control_flow` | **FAIL** 260 ms | 期望文件落在图编号后面，不是行为被拒：`stdout differs from expected/dump.agir.dot at line 50: expected: "            label=\"__g_49__\";" got: "            label=\"__g_59__\";"` | **记为既知的红**：`known_fail`（owner = 11-24 那次图编号改名 `14f77c0739`），`known_fail_msg` 钉住"第 50 行、`dump.agir.dot`"这一行，所以同一份金标的另一种断裂（少一个节点、变一条边）是另一句消息，仍然红。结束它的是重录：`node internal/exemplars/check.mjs --update 08_structured_control_flow` |
| 7 | `tools.debugger` / `debugger.dap_fusion_group` | **PASS** 202 ms | Windows 上那次 60.79 s 的红没有可引用的诊断留在仓库里；同一份计划里 `debugger.dap_session` 今天在 Linux 上也红（`FAILED: a var shows the current content of its cell, after the assignment / expected "var int = 15" actual "int = <not evaluated>"`），那是另一条 | 没改：Linux 上 0.2 s 绿，Windows 的红只能等一次带原文的记录。它已随本批移进慢层 |
| 8 | `docs.links` / `docs.snippets_compile` | **FAIL** 8.18 s | 文档写得比编译器早：12 条 snippet 在 `scripts/check-doc-snippets.mjs` 的 `PENDING` 名单上，理由同一句——`range values: `C-1123-61` (lane `lane/op-overload-merge`) has not landed, so a `Range` still shares no `map`/`filter`/…`。末行 `12 stale snippet problem(s) in 191 reader-facing document(s) (353 snippet(s) checked, 3.4 s)` | 没标：这一条用例罩着 353 条 snippet，`known_fail` 会把别处新断的 snippet 一起咽下去（`test/plans/feat/compile/native_declarations.plan.toml:1087` 已经写过同一个理由："a `known_fail` would be worse, because the same mark would hide the errors that come later"）。它红的墙由工具自己印出来，等 `C-1123-61` 落地并删掉那 12 条 `PENDING` |
| 9 | `modules.unicode_paths` / `unicode.paths` | **SKIP**（`on-windows.mjs` 只在 Windows 上跑） | 它是**产品缺陷**，不是测试缺陷：Windows 上非 ASCII 用户名/PATH/CAMEL_HOME/键名要走 UTF-8，13 次里每次都红 | 没改：这条只有 Windows 跑，Linux 上是 skip，所以它不红这棵树；修它要动产品的进程边界，本分支不许碰 |
| 10 | `feat.tools.exit_reporting` / `runner.exit_reporting_names_the_signal` | **PASS** 2.65 s（Linux 上跑） | Windows 上没有 SIGSEGV 可发（Node 抛 `kill ENOSYS`），那条 `got signal SIGSEGV (rc=139, …)` 和 summary 的 `killed by a signal` 在 Windows 上不是任何东西的读数 | 已按树里现成的机制声明：`25f2b1d588`（"test: the signal-reporting entry says it needs POSIX signals"）加了 `skip_unless = ["node", "test/tools/have-posix-signals.mjs"]`。本分支未改 |
| 11 | `modules.tensor` / `erfc_grad.f64` | **PASS** 77 ms | f64 的十进制钉到 17 位，MSVC/Apple 的 `erfc` 末位差 1 ulp：`expected "0.15729920705028513" got "0.1572992070502851"` | 已修：`9d01307b9e` 的同一次改动把这条的断言改成"按组合声明的 8 ulp 界比较"，`neg_erf` 那行仍然逐位（0.0 是语言承诺的精确值）。本分支未改 |

## 落下来的

- **4 条已由 `9d01307b9e` 修掉**（1、2、3、11）：那是 2026-11-22 的一次改动，晚于 13 次记录的截止日，
  所以记录里它们还是"每次都红"。今天在 Linux 上全绿。
- **3 条已由树自己的平台机制声明**（4、10，加上 `unicode.paths` 反过来只跑 Windows）：`skip_unless`
  + 探针工具，是这棵树对"这一条在这个平台上不是任何东西的读数"的写法；`known_fail` 不分平台，
  标上去会把 Linux 上的真红一起藏起来。
- **1 条记成既知的红**（6），带着可以引用的诊断和结束它的命令。
- **3 条只记录、没动手**（5、7、9）：两条是 Windows 上红但仓库里没有原话（5、7，它们的红要靠一次
  带原文的记录才说得清），一条是产品缺陷且只在 Windows 上跑（9）。
- **没有一条被删掉，没有一条断言被放宽**；`known_fail` 只加在一条上，且 `known_fail_msg` 钉的是
  金标里那一行，不是"这个用例失败了"这种能吞下任何红的消息。