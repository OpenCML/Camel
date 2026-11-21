# 一本「只许降」的账本，遇到一次合法合并带进来的新住户：账本该涨吗？

**日期**：2026-11-20
**提出者**：账本车道（`lane-ledger`，基线 `origin/develop` = `1eaa3d780`）
**状态**：等作者裁定。**默认建议（写死）：允许涨，但每一次涨必须具名到文件与理由，并且写在同一笔提交里；「没有写明理由的涨」仍然红。**
**限制**：本页只有 `git` 读操作、`check.mjs` 与 `check-doc-links.mjs` 三条只读命令，加一次 `check.mjs --record --force`。没有改 `check.mjs`、没有改 `runtime.graph_draft_ledger.plan.toml`；构建与计划读数在提交里，本页只写它们的结果。

## 1. 要作者回答的一句

**`runtime.graph_draft_ledger` 的 `files` 从 43 涨到 46，与「账本只许降」怎么并存？**

## 2. 让它是问句的读数

**账本是一张成员名单，不是一条只降的棘轮。** `test/cases/runtime/graph_draft_ledger/check.mjs` 比的是三张列表
（`files` = 路径、`constructions`／`decode` = `file#symbol`），整词 `-w` 才算、注释与字符串先被 blank 掉；
`references` 那个**计数**已被你 2026-11-03 13:30 的裁定删除（"一个能在行为不变的情况下漂移的数，什么都不保护"）。

**今天 `origin/develop` = `1eaa3d780` 上的读数**（`node test/cases/runtime/graph_draft_ledger/check.mjs`，exit 1）：

```
[LEDGER] files 47 member(s), constructions 1 member(s), decode 3 member(s), compared with the list member by member
[LEDGER] zero files ok: modules/graph/handle.h, modules/graph/handle.cpp, modules/graph/read.cpp,
                        modules/rewrite/fact.cpp, modules/rewrite/module.cpp name the type nowhere
[LEDGER] RED entered files: src/passes/test/draft_erase_unread.cpp
[LEDGER] RED entered files: src/passes/test/rules.cpp
[LEDGER] RED entered files: src/passes/test/undo_selfcheck.cpp
[LEDGER] RED entered files: src/runtime/draft_undo.cpp
[LEDGER] RED the list is test\cases\runtime\graph_draft_ledger\baseline.json: 4 entered, 0 left
```

**四处都不是同一笔，而四笔都晚于上次记账**（`c31c57609`，2026-11-18 23:31，`files` 40 → 43）：

| 处 | 哪一笔带进来 | 日期 | 判 |
| --- | --- | --- | --- |
| `src/passes/test/rules.cpp` | `1f617b8d8` | 11-19 17:33 | **拼写**：7 处全是 `shapeAfter(...)` 调用点的 lambda 形参 |
| `src/runtime/draft_undo.cpp` | `84f7008e6` | 11-19 19:53 | **住户**：17 处类外成员定义 + 8 处助手形参 |
| `src/passes/test/undo_selfcheck.cpp` | `84f7008e6` | 11-19 19:53 | **住户**：被检的 undo/自检 API 本体 |
| `src/passes/test/draft_erase_unread.cpp` | `a698b933c` | 11-20 14:00 | **住户**：2 个匿名助手形参（改模板/内联＝§62 判过的"数掉下去、东西没动"） |

本笔的做法是账页从 §62 起的两条腿：**住户记账（`--record --force`）、拼写归位（改 `auto &`）**。
`rules.cpp` 的 7 处 lambda 形参改 `auto &` 之后整词命中 7 → 0，它离开名单；另外三处是住户，
按 §62／§63 的规矩记账。⇒ **`files` 43 → 46。**

**⇒ 所以这是一次"合法合并带进来的住户"**：三处里有**三处都是 11-18 之后新建的文件**（`draft_undo.cpp`、
`undo_selfcheck.cpp`、`draft_erase_unread.cpp`），它们不是"有人为了过账而新写了一个名字"，
而是**那三笔工作本来就得写这个名字**。

## 3. 两条路各带什么代价

**（甲）账本允许涨，但涨必须具名。** 代价：基线数字会涨，读者必须相信"涨的那一行旁边写了理由"；
好处：合法合并不必为了过账去删代码或改名。

**（乙）账本严格只许降。** 那么今天这四处红只能靠**改结构**过：把 `draft_undo.cpp` 的 17 个成员定义
搬回 `draft.cpp`（~700 行、跨文件移动）、把 `undo_selfcheck.cpp` 的探针改到不收草稿、把
`draft_erase_unread.cpp` 的两个助手内联掉。代价：三处都是**为了过账而改结构**，
而 `draft_undo.cpp` 那一处**在 C++ 里根本做不到**——类外定义成员必须写类名。
⇒ **（乙）的实际结果是「账本永远红」或者「账本被改成看不见这三处」**，两者都比（甲）坏。

**（丙）把 `files` 从账本里拿掉，只留 `constructions`／`decode`。** 代价：`files` 是**唯一**能回答
"这个文件还连着草稿吗"的那一格（`constructions` 只数造图、`decode` 只数读入口），
拿掉以后一个只读草稿、不造不读入口的新文件就完全不入账。**不建议。**

## 4. 默认建议（本页写死，作者可推翻）

**（甲），形状写死如下：**

> **涨是允许的，但涨的每一行都必须在同一笔提交里具名到文件与理由；没有理由的涨仍然是红。**

具体的三条落地口径：

1. **提交说明点名那一笔**：涨的是哪个文件、由哪一笔带进来、为什么删不掉（本笔的
   `docs/handover/reviews/graph-store-step2-b-ledger.cn.md` §64 就是形状）；
2. **`--force` 是唯一入口**：`check.mjs --record` 拒绝写涨了的成员，只有 `--force` 才写，
   于是 `baseline.json` 的 diff 本身就是"这次涨了哪几行"的清单，审阅者可以逐行拒绝；
3. **"拼写"与"住户"必须分开处置**：同一笔里凡能改拼写的都必须改掉再记（本笔的 `rules.cpp`），
   **只允许把改不干净的那几处记账**。

**⚠️ 一条必须先说清的口径冲突（本页自己量出来的）**：如果把（甲）写成"**涨必须与『新增文件』同时出现**"，
那么 §62 那一笔**会被判红**——它涨的 `modules/transforms/rule_expand.cpp` 是**已有文件**，
涨的原因是"新写出的名字，住户本来就在"（`auto &draft = transaction.edit()` 在 `c6bbd67f5^` 上就有）。
⇒ **所以判据不能写成"必须同时有新增文件"**，只能写成"**新写出的名字，且那个住户可证**"。
本笔三处恰好都是新文件，那是巧合，不是判据。

**另一条必须写进判据、否则这一格会反复回归的口径**：

> **别拿 `git grep -n 'GraphDraft' -- src/` 的命中数当判据。** 账本只看**成员进出**（整词 `-w`、
> 注释与字符串被 blank 掉、`constructions`／`decode` 带符号）。`1eaa3d780` 上这个数是 **538 行**，
> 其中 `RuntimeGraphDraftSession` 占 **169 次**、注释另有一批；**改 `auto &` 会让这个数降、
> 同时让账本想涨——两者方向都不一致。**

## 5. 附：本笔的 `[SUMMARY]`（逐项写，不要只给合计）

| 计划 | 读数 | 时刻 / 来源 |
| --- | --- | --- |
| `feat/runtime/graph_draft_ledger` | `[SUMMARY] total: 2, pass: 2, fail: 0`；`[PASS] runtime.graph_draft_ledger.never_rises`；`[LEDGER] files 46 member(s)` / `ok: every member is on the list` | 88 主机，`-Jobs 8`，本笔 |
| `feat/opt/savepoint` | `[SUMMARY] total: 2, pass: 2, fail: 0`（改前同一条：`total: 2, pass: 2, fail: 0`） | 88 主机，`-Jobs 8`，本笔 / 改前 |
| `feat/compile` | `[SUMMARY] total: 2662, pass: 2646, fail: 0, known failures: 16` | 88 主机，`-Jobs 8`，本笔 |

**两个不同树上的 `[SUMMARY]` 不许相加**：这三行是同一次传输里同一棵树的运行，行内数字各算各的。
`skip`、`known`、`fail`、`pass`、`total` 分开写：`feat/compile` 的 `known failures: 16` 不是 `pass`。