# 诊断改法的文本印法与 code action：三处画法和两处形状要作者确认

Author: 车道 `handover/h440-diag-fix-surface`（`D:\Projects\Camel-wt\h440-diag-fix-surface`），2026-10-24。Status: open — 裁定（`docs/handover/reviews/questions-2026-10-23.md`，小节 `## diagnostic-fixes-json-and-cause-vocabulary（H252 诊断改法）`）定的是"印出来、发给编辑器、用性质测、金标不记文本"，没有定下**怎么画**。我按下面的选择落地并把它们逐字锁进了测试；作者若要另一种画法，改的是一处渲染函数与两个 golden。

## 数字先

- 14 个标 `edit` 的码（`lexical` 4 + `syntax` 10）都有用例；`test/cases/compile/diag_fixes/proof.mjs` 现在断言一条**性质**：注册表标 `edit` 的码集合与"有用例产出改法、应用后重编译诊断消失"的码集合**双向相等**（14 = 14），另加"同上码的两种情况"（`0x01000001` 的 `077` 无改法 / `09` 有改法）。
- 文本格式：`text` 下每条带改法的诊断多出 `help: <fix 标题>` 与它下面的带标记源码行；`short` 不变（测试运行器解析它）。已重录的 golden 覆盖 8 条带改法用例的**逐字**输出。
- LSP：`textDocument/diagnostic` 的 `data.fixes` 给出 `title` + `edits[{range, newText}]`；`textDocument/codeAction` 每条改法一个 quick fix，应用后重编译该诊断消失（`test/cases/tools/lsp/code_actions.test.mjs`）。
- AST 金标：被拒文件与带警告文件现在只记诊断的**码与位置**（`Diagnostics::dumpIdentity`），改一条 help 文本不再动 `test/plans/feat/parse/golden/ast_hashes.txt`。

## 我落地的画法（要确认的就是这五条）

1. **一次替换（既写又删）只印"替换后的那一行"**。`09` → `9` 的改法印出 `    let a = 9`，`+` 画在写下的 `9` 下，被删掉的那个字符不另画一行。rustc 在有些情形下会再印一行原来的样子、用 `-` 标出被删的文本。我选择只印结果，理由：裁定说的是"给出替换后的那一行"，而删掉了什么由 `help:` 的标题说（"write the number without its leading zero"）。
2. **一个 fix 有多个 edit 且都在同一行时，每个 edit 各印一组**。`WhenAfterBody`（`SyntaxDiag::WhenAfterBody`）的改法是把条件从句首搬到 `when` 那一段：现在印两行同一个行号，一行 `+`（把 `when true ` 插进去后的样子）、一行 `-`（原来的样子，标出被删的那段）。rustc 会把整个 suggestion 合成一行画。合成需要把两个 edit 的位移合到同一张画布上，我没有做。
3. **标记用 `+` / `-`，颜色用绿（`1;32`）/ 红（`1;31`）**，行标与 `|` 沿用诊断自身代码片段的宽度与蓝色。这就是 rustc 的样子。
4. **LSP 里编辑叫 `newText`，CLI 的 JSON 里叫 `replacement`**。裁定同意 CLI 的 JSON 形状取 `{range, replacement}`；LSP 的 `TextEdit` 按协议必须是 `{range, newText}`，所以同一处改动有两个拼法，各随自己的协议，代码里两处相邻并都注明了。若作者希望 CLI 的 JSON 也改成 `newText`（一处拼法），那要动的是 `toJson` 与已发布的 `docs/cli.md` 例子。
5. **放不下去的改法整个不给，而不是给一半**。一个 edit 的 range 不是已解析的 `CharRange`（或超出文件的行）时，该 fix 不进 `data.fixes`，文本那边只印 `help: <标题>` 不印源码行。理由：编辑器会照着应用，画错位置比不给更糟。现在 14 个码的 edit 全是 `CharRange`，这条只在将来有人用 `TokenRange` 建 `fix` 时才会触发（`Diagnostic::fetchRange` 现在也会解析 fix 的 edit range，见同一改动）。

## 另一处要作者点头的

6. **快照不记严重度**。`Diagnostics::dumpIdentity` 记的是 `D 0x<码> <行>:<列>-<行>:<列>` 加每个次要位置的同形，**不记** `severity`、`message`、`suggestion`、`notes`、`fix` 的标题与替换文本。警告的严重度已经在码里（`0x01100000` 这种带 `1` 的码段），而 `-Werror` 把警告提升成错误是命令行的事、不该动解析器的快照。若作者要求把严重度也记进快照（那会让 `-Werror` 的运行敏感），我加。