# "代码里不写任务编号"这条，管不管**用户可见文本**？

**问谁**：语言作者（负责人）
**问什么**：您那条规则（`docs/handover/reviews/questions-2026-10-24-evening.md`《顺序边的读法：返工》）的原文是"**代码里不写任务编号**：仓库要公开（'提交不写任务编号'同样适用于**注释**）"。我们按这条把 `src`/`include`/`modules`/`tools`/`scripts` 里的任务编号清干净了（226 处，225 处已处理：改写文档引用 15、只去编号 190、删历史 20）。**剩下三类都在"用户可见文本"里**，规则没明说，我们没敢动——请您定它们是否同样要处理，以及**该怎么指那些裁定**。

## 剩下的三类（逐字）

1. **一条诊断文本**：`src/compile/gct/builder.cpp:2532` 的用户可见消息里有 **`representation of H17`**；它被 `test/plans/feat/modules/tensor/subscript.plan.toml:59` 用 `expect_output_contains = [..., "H17"]` **钉住**，并在 `diagnostics/registry.tsv` 同一行、生成的 `src/messages/semantic.cpp` 与镜像 `docs/diagnostics.md` 里成对出现。⇒ 去编号必须**同批**改这四处（含一条测试期望值）。
2. **两条 help 文本**（都在 registry 的 help 字段、生成到 `src/messages/semantic.cpp:1508/1516`）：**`h248-generator-and-composition`** 与 **`h426-interval-step-form`**——它们看起来是**裁定小节名**（`docs/handover/reviews/` 里的小节标题），删掉编号就找不到对应裁定。
3. **一个路径**：`scripts/migrate-bare-tensor.mjs:861` 里写着 `test/cases/compile/_h551probe`（探针产物路径；施工方在量"除了它还有谁引用、该路径是否在树里存在"，若只有它自己引用就重命名去掉编号，被别处按名字引用就报回来）。

## 需要您定的

- **甲**：用户可见文本**同样**去编号 ⇒ 请给"指裁定"的写法（例如**日期＋小节标题**、或指向 `docs/design-principles.cn.md` 的某小节、或干脆不指、只陈述现在的规则）。第 1 条的新消息文本请一并给（或授权我们写成自述式，例如说清"这是哪种表示"而不再提任务号）。
- **乙**：用户可见文本**保留**现状（用户看不到仓库内部编号的意义，且删了会削弱可追溯性）⇒ 那么第 1、2 条都不动，我们把这条写进"已决定的风格"里，以后不再问。
- **丙**：**只**处理第 1 条（诊断文本是产品输出、必须自述），**保留**第 2 条（help 指向裁定小节名，属内部可追溯性）。

## 现状（不自行决定）

- 第 1、2 条**原样保留**（连带 `registry.tsv`、生成的 `semantic.cpp`、`docs/diagnostics.md` 与那条测试期望值都不动）。
- 注释里的任务编号（含**小写** `h450`/`h251`/`h465` 这类）按"注释"处理：去掉编号、句子仍成立、指向仍清楚（"ruling 2026-10-24 evening, item 2"这种写法），已在施工中。
- 排除项（**不是**任务编号，逐条有理由）：`modules/process/operators.cpp:100` 的 `TH32CS_SNAPTHREAD`（Win32 常量里的 `H32`）；`test/cases/compile/_h551probe` 这类路径（见第 3 条）。
- 本批过后，`src`/`include`/`modules`/`tools`/`scripts` 的注释里任务编号**归零**，且以后的提交不再新增。