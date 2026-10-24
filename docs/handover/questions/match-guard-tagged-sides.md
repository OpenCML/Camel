# `T?` / `T!` 的 `match` 在守卫下还能不能"恰好两个分支"：要一条裁定

Author: 车道 `handover/h422-match-guards`（`D:\Projects\Camel-wt\h422-match-guards`），2026-10-24。Status: open — 需要语言作者确认下面对 `docs/language/06_expressions.md` 的读法（或改哪一条），以及"同侧第二条无守卫 `case`"要报什么。测量在 Linux 主机（`88zjwei`）上、分支 `handover/h422-match-guards` 与 `origin/fsh-dev` 的合并基（`578d43f3b`）同一构建窗口内完成，读数走 `camel-remote.ps1` 与计划运行器。

## 数字先

- 守卫（`case p when c => ...`）按裁定（`docs/handover/reviews/questions-2026-10-23.md`，小节 `### when-placement-and-match-guards` 第 1 条）实现：**守卫为假就试下一个 `case`**；守卫在该 `case` 的作用域里求值，可以读模式绑定的名字；`case null` 也只是其中一条。落地后 `std::nvm` / `std::fvm` / `std::jit` 三个后端的读数逐字相同。
- 这直接推翻了 `docs/language/06_expressions.md:351`（"被匹配的值是 `T?` 或 `T!` 时，`match` **恰好有两个分支**"）与 `:378`（"缺分支、**多分支**……是 `SemanticDiag::TaggedMatchCases`"）在守卫下的字面读法：`case null when c` 为假时，值必须由**写在它后面的那条 `case`** 接住，所以一个 `T?` 的 `match` 里可以出现**两条 `case null`**、或 `case null` 后接 `case v` 再接 `case v`。
- 我落地时采用的读法（下面"我落地的读法"一节逐条列出）让下列 18 行输入在三个后端上输出同一串：`test/cases/compile/match_guard/first_match_wins.cml`（`12.0 -1.0 2.0 -2.0 0.0 big five tiny small 2 3 101 1 0 1 0 1 2`），其中 `nullable(null)` 走 `case null when false => 100`（守卫为假）落到 `case null => 101`，`nullable(20)` 走 `case v when v > 10`（读绑定的 `v`）。
- `test/plans/feat/compile/match_guard.plan.toml` 的 5 个用例 + `test/cases/parse/constructs/match_guard.cml` 全绿；没有改动 `docs/language/**` 与 `docs/design-principles*.md`（作者页）。

## 我落地的读法（要确认的就是它）

1. **每一侧都要有一条不带守卫的 `case`**（`T?` 的空值侧与非空值侧、`T!` 的错误侧与成功侧），它是这一侧的"开口"；只有它算这一侧被覆盖。带守卫的同侧 `case` 写在它**前面**，是"守卫为假就落到开口"的那些条。
2. **同侧开口之后又写了一条同侧 `case`**（无论有没有守卫），我认为它永远走不到，按 `SemanticDiag::TaggedMatchCases` 报错，文案 `a case of one side follows the case that takes that side`。
3. **缺一侧的开口**时保持原文案：`'case null' is missing` / `'case error e' is missing` / `the case for the value is missing ('case v' or 'case _')`。`test/cases/compile/match_guard/tagged_missing_null.cml` 就是这条（`case null when false` + `case v`）。
4. **顺序**：`case v` 写在 `case null` 前面也接受（两侧互斥，开口与守卫各自决定）；落地时的链是"按书写顺序，每条测自己那一侧"。
5. `case _` / `case v` 在 `T?` 上仍然指**非空**那一侧（合 `:353`），`case v` 取出的值类型是 `T`。

## 要作者回答的三点

- **(a)** `:351` / `:378` 要不要按守卫改写？我的建议：把"恰好两个分支"改成"两侧各恰好一条**不带守卫**的 `case`"（即上面第 1 条），并把 `:378` 的"多分支"限定为"同一侧出现第二条不带守卫的 `case`"。若作者要的是"守卫下改报别的诊断（新码）"，我改成新码。
- **(b)** 同侧开口之后的 `case` 是**错误**（我现在的选择，`TaggedMatchCases`）还是**警告**（像 `MatchCaseAfterWildcard` 那样保留、图上仍建臂）？现在的选择会让"`case null, case null when c`"这种把守卫写在后面的写法变红。
- **(c)** 需不需要一条"守卫为假落到同侧开口"的**新诊断**提示（例如"这条 `case` 的守卫为假时不落到任何 `case`"）？现在没有，缺开口直接报 `TaggedMatchCases`。

在这条裁定下来之前，我按上面的读法落地，并把 (a) 的文档改动留给作者页（我不动 `docs/language/**`）。