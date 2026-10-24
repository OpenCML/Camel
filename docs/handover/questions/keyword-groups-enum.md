# 关键字分组：`enum` 在 token 表里是 `Reserved`，在语言参考 2.4 里是"类型与转换"

Author: 车道 `handover/h425-ai-reference`（`D:\Projects\Camel-wt\h425-ai-reference`），2026-10-24。Status: open — 需要一处一行的裁定（改 `token_kinds.def` 的分组，还是改 `docs/language/02_lexical.md` 2.4 那行），并要求确认"`Reserved` 只表示'文法接受、语言未实现'"这一读法。测量在 Linux 主机（`88zjwei`）上、`origin/develop`（`69f183330`）加本车道改动同一构建窗口内完成，读数走 `camel-remote.ps1` 与计划运行器。

## 数字先

- 两边列的关键字**集合完全相同**：40 个（`CAMEL_KEYWORD` 行 40 条；2.4 的表 11+15+4+3+7 = 40）。`scripts/check-grammar.mjs` 逐字只比集合（"the keyword table (2.4) lists exactly the keywords of the token table"），**不比分组**，所以这一处现在没有任何检查看得见。
- 分组**只有一处不同**：`enum`。`include/camel/parse/syntax/token_kinds.def:33` 逐字 `CAMEL_KEYWORD(KwEnum, "enum", Reserved)`；`docs/language/02_lexical.md` 2.4 把它放在"类型与转换"那行（`as const constas enum`），"保留，尚未实现"那行是 `is of try catch finally typeof keyof` 7 个词。
- `enum` **已经实现**：`test/cases/format/match_and_type_arguments.cml:6` 逐字 `type Shape = enum {`，语义写在 `docs/language/03_data_model.md` 3.5（构造、`match`、穷尽性、图上表示、各后端支持），标准库与示例也在用。所以 token 表的 `Reserved` 是**语言加上 enum 时没跟着动**的旧值。
- 交叉引用是 token 表自己写下的：`token_kinds.def:5` 逐字 `Group is the row of the keyword table in the language reference (2.4)`，`include/camel/parse/syntax/token.h:63` 逐字 `// Declaration, Control, Type, Literal, Reserved`。即**表声称 2.4 是它的来源**，而 2.4 说 `enum` 不是保留词。
- 影响面：`group` 目前只在 `src/parse/syntax/lexer.cpp:161` 被填进 `KeywordInfo`，代码里唯一的读者是 `tools/parsecheck/fuzz.cpp:144`（遍历关键字），没有语义分支、没有诊断消息、没有测试断言它。改这一个词是**一行、无行为风险**的改动。

## 我这一轮的做法（不改两边，也不留假话）

本车道的产物 `docs/reference/camel-for-ai.md` 第 4 章的关键字表由 `scripts/camel-for-ai.mjs` 生成。上一版它照 `token_kinds.def` 的分组渲染，于是那张表把 `enum` 放在 `Reserved`，而同一份文件的第 6、11、12 章又在用 `enum`——**自相矛盾**，而且正落在"给 AI 读、必须可信"的那份文件里。

现在的做法：**词与分组以 2.4 为准**（token 表自己指认的来源），算符与分隔符仍取自 token 表，并在生成的区域里追一行由两个来源**算出来**的差异说明，逐字：

`The token table include/camel/parse/syntax/token_kinds.def groups enum (token table: Reserved; reference: Type and conversion). This file follows the reference.`

这一行不是写死的：它由"表里每个词在两边分组不同"推出，`token_kinds.def` 一分组改对，它自己就消失。这样本车道既没有把错误传播给读者，也没有在参考里偷偷改语义。

## 两条路（都需要一句裁定）

- **(a) 改 token 表**：`token_kinds.def:33` 的 `Reserved` 改成 `Type`。一行，与 2.4 和 `token.h` 的注释一致，检查侧没有任何断言会因此变红；本车道生成的那行差异说明随之消失（我这边会看到它消失，不需要另改文档）。风险：如果 `Reserved` 在别的车道里有"文法接受但未实现"以外的含义（我现在只看到 fuzz 遍历），需要先确认。
- **(b) 改 2.4**：把 `enum` 挪回"保留，尚未实现"。这与 3.5、测试用例、标准库的现状相反，我不建议。

建议 (a)。按 AGENTS.md，"未决项与未覆盖项问作者、不擅自决定"，所以本车道没有动 `token_kinds.def`，也没有动 `docs/language/**`。

## 我没有做

没改 `token_kinds.def`、`docs/language/02_lexical.md`、`docs/language/03_data_model.md`、`scripts/check-grammar.mjs`；没给 `check-grammar.mjs` 加分组比较（那会立刻变红，而红的原因是上面这条待裁定）；没动 token 表里任何别的词的分组。