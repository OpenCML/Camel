# 文档守则在 2026-10-24 那批裁定之后变红：54 条片段 + 8 条引用

## 事实（同一命令 `-Plan test/plans/feat/docs_links.plan.toml`，同一主机）
- 合并前（`34d5531df`）：**total 10, pass 10, fail 0, RUN_EXIT=0**。
- 合并 `origin/fsh-dev` 的那批（`when` 一律写在体之前、文档树随语言项目重组、验证命题与 `refine_accuracy` 承诺等）之后：同一计划 **fail 2**
  （`docs.links.citations`、`docs.snippets_compile`）。所以这 54 + 8 条是那一批带进来的，不是存量。
- 逐字节选：
  - `docs/design-principles.cn.md:283: does not compile: error[SyntaxDiag::MissingToken]: Expected '(' before '::'.`（357 同）
  - `docs/language/06_expressions.md:591: does not compile: error[SyntaxDiag::UnexpectedToken]: Unexpected '=>'; expected '}'.`
  - `docs/language/07_statements.md:112: does not compile: error[SemanticDiag::FeatureNotSupported]: The feature 'default parameter values' is not supported yet.`
  - `docs/language/04_execution_model.md:232: output differs from the text block: line 3: page says "  --> <file>:4:15", program printed "  --> D:<file>:4:15"`
  - `docs/design/public-repo/README.cn.md:65: 'docs/design.md': no such file`
- 54 条按目录：`docs/language` 21、`docs/language/en` 21、`docs/tutorial` 6、`docs/`（顶层）4、`docs/design/rewrite` 2。

## 已经处理的（本操作自己的文件）
- `docs/design/rewrite/plan-verification.cn.md`：两条仓库外探针片段按约定标注 `<!-- camel-snippet: fragment: … -->`；指向已删问题文件的 5 条引用改指
  `docs/handover/reviews/questions-2026-10-23.md` 的对应小节。
- `test/cases/modules/rules/check_accuracy_library.cml`：指向已删问题文件的引用改指同一份 reviews 的 `§accuracy-check-sampling`。

## 剩下 52 条片段 + 1 条引用都在作者的页面里
`docs/language/**`、`docs/tutorial/**`、`docs/design-principles.*`、`docs/design/public-repo/README.cn.md`。本项目的硬规矩是**协调者不改这些既有文件**，所以没有动。

## 请作者裁定
1. 这 52 条是**页面该跟着改**（新语法/新位置落地后逐页更新），还是**守则的收录范围该改**（书里刻意展示旧写法或示意片段的页面需要一个"这一页不编译"的声明）？——这决定我是逐页派车道，还是等你给页面的分级。
2. `docs/design/public-repo/README.cn.md:65` 引用的 `docs/design.md` 是"公开的精简版设计原则"：它是**要新建的文件**（引用先写下了），还是应当改指现存文件？
3. 要不要把 `docs.snippets_compile` 分成"新增/改动页面必须绿"与"存量页面清单"两级，让存量债务不阻塞每次合并？同意我就按你的裁决落地。

## 出处
- 计划与条目：`test/plans/feat/docs_links.plan.toml`
- 检查脚本与标注约定：`scripts/check-doc-snippets.mjs` 头部注释（`fragment` / `counter-example` / `compile-only` / `graph-dump`）
- 裁定批次：`docs/handover/reviews/questions-2026-10-23.md`（2026-10-24 一批）
