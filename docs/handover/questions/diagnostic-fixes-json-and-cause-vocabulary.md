# 诊断改法（H252）：`data.fixes` 的形状、`none:` 的原因词表，以及三处"没给改法"的边界

## 落下来的（需要作者确认或改写）

1. **JSON 字段**：`--error-format json`（即 `--output-format json`）的 `data` 里新增 `fixes`：

   ```json
   "fixes":[{"title":"insert ','","edits":[{"range":{"start":{"line":2,"character":15},"end":{"line":2,"character":15}},"replacement":","}]}]
   ```

   一个 `fix` 可应用即成立，一个 `edit` = "把 `range` 覆盖的文本换成 `replacement`"；空 range 是插入，空 `replacement` 是删除。名字照 LSP 的 code action / rustc 的 suggestion 取。`text` / `short` 两种文本格式**故意不动**：改法是给工具的，`help:` 是给人的。

2. **"没有改法"的原因词表**（`registry.tsv` 第 7 列，`none: <cause>[: <句子>]`）：`runtime`（运行时才报，源码里没有可改的文本）、`choice`（改法是作者的取舍：写哪个名字、哪个类型、哪个值）、`context`（改法是已知的，但不是一段文本：要改第二处、另一个文件，或要重构）、`internal`（报的是编译器自己）。今天 260 条里 14 条 `edit`、246 条 `none`。

3. **同一条码可以有时有改法、有时没有**：`LexicalDiag::InvalidNumberFormat` 对 `09` 给改法（只能读成十进制 9），对 `077` 不给（在入口参数与转换文本里读十进制 77，别处读八进制 63，改法要在两种意思里挑一个）。`registry.tsv` 的列说的是"这条码**可以**给改法"，不是"每次都必然给"。

4. **未闭合字符串的插入点**：放在字符串**开始那一行的行尾**，不是词法器读到的地方（文件尾）。词法器把未闭合字符串一路读到 EOF，在 EOF 补引号能过编译，但会把后面所有代码吞进字符串——一个"不用问就能应用"的改法不应该悄悄改变程序的意思。rustc 同样补在行尾。

5. **LSP 不带 `fixes`**：语言服务器自己从语义模型构造 `data`，只有 `name`（和 `suggestion`/`notes`）。要不要把改法也发给编辑器，是另一件事（需要先把 `DiagFix` 送进 `TextDocumentDiagnostic`）。

6. **一个顺手发现的耦合（不是本车道引入的）**：`test/plans/feat/parse/golden/ast_hashes.txt` 的哈希里包含"前端对文件说了什么"——被拒文件只看诊断文本（`tools/parsecheck/snapshot.cpp` 的注释与 `parseText(...).dump`）。于是给任何在解析语料里出现的诊断改 `help`，都会让 16 行哈希变化（本次：给 `MissingToken`、`UnexpectedToken` 补 help，改了 16 行，仍 283 条，无增无减）。快照按文档流程 `check.py snapshot --update` 重录，没有放宽。要不要把"被拒文件的哈希只看诊断的 code 与位置、不看 help"，是作者的决定：那是改检查，不是改语义，本车道不动它。

## 开放问题（请作者定）

- **Q1**：原因词表要不要收成两个（`runtime` 与 `choice`）？`context` 与 `internal` 是我的分法。
- **Q2**：一条诊断有改法时，`help:` 与 `fixes` 语义重复（同一句话两种说法）。要不要让 `help:` 在有改法时省略，或让文本格式也打印改法？
- **Q3**：判定"这条码某次没有改法"要不要落到测试里？今天只锁了 `077` 与 `09` 这一对（`test/cases/compile/diag_fixes/proof.mjs`）。

## 证据

`test/plans/feat/compile/diag_fixes.plan.toml`（红前：`camel syntax_missing_comma.cml --error-format json` 只有 `suggestion`，没有位置也没有替换文本）；`docs/cli.md`"JSON diagnostics"一节；`docs/technical/30_diagnostics_reporting.md` 第 2 节的 `data` 字段表。裁定若下，请写进 `docs/handover/reviews/questions-2026-10-23.md`（`questions/` 下的文件会被清掉）。