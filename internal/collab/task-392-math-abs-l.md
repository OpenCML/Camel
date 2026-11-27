# task-392 · 把 `math:abs_l` 翻译成图算子，让第 17 章的定律真的发火

**来源**：`D-1128-48` ①（2026-11-28）。**任务板上限 256 已满，本卡先落在 `internal/collab/`，起活时再建板上的卡并 `reassign`。**

## 为什么

`docs/tutorial/17_teaching_a_law` 这一章要教的是"定律发火"，但它演示的定律 `abs_of_nonnegative` **从未在这个语言上发过一次火**：

```
refuted abs_of_nonnegative: proposition equality, evidence none,
  reason the rule never fired on its own left side in 64 tries,
  the rule never fired on its own left side,
  coverage beyond translation coverage: the operator 'math:abs_l' is not translated
```

原因不是讲义写错，是 `math:abs_l` 没被翻译成图算子 ⇒ 定律的左端永远匹配不上。讲义现在的输出块已按决策者要求恢复成"发火"的形状，缺口由 pending 清单记着（`check-doc-snippets.mjs` 的 `PENDING_ALL`，key `12ee3b07b5c68a43`，cn/en 各一条，理由 `math:abs_l is not translated, so the law cannot fire`）。

## 目标

把 `math:abs_l` 翻译过去，让那章演示的定律真的发火；发火后 pending 会自己报 stale，把两条删掉，缺口闭合。

## 判据（缺一不可）

1. 第 17 章那段程序**打印出 `fired`**（贴原文；讲义输出块写的就是它）；
2. `node scripts/check-doc-snippets.mjs --default-roots` **报出那两条 pending 为 stale**（key `12ee3b07b5c68a43`，cn/en 各一条）—— 报出即可，**删 pending 由协调者另派一笔**；
3. 规则库/`math` 相关计划与 `test/plans/feat/**` 的**新增红 0**（对 `newestRedTable()` 逐名比，红名逐个贴）；
4. `docs_links` 计划不因这次改动变红（pending 两条 stale 提示除外）；
5. `[provenance] tip` + `binary built` + `numstat`，且 `BUILD_EXIT=0` 与读数**同一次**运行。

## 禁

- 不改 `check-doc-snippets.mjs`；不改 `.plan.toml` 的断言；不把讲义改成"发不起来"。
- 不为让检查变绿改语义（语义与设计原则冲突时以原则为准，记缺陷）。

## 落点（先报再动手）

规则库与算子翻译所在模块（`src/stdlib/math*` 与规则库）。**第一件事是读代码报"落点 + 打算怎么翻"给协调者**，再动手。

## 排期

**不占 CC 格**，排在 CC 之后的第一个空格（`D-1128-48` ②）。