# 前端代码评审：词法/语法、AST 构建、格式化器、诊断基础设施

范围（只读源码，没有构建与运行；标"待验证"的是我没有确认的前提）：
- 词法与语法：`src/parse/syntax/`（`lexer.cpp` 全文，`parser.cpp`、`parser_expr.cpp`、`parser_impl.h` 的语句/声明/表达式部分，`report.cpp` 的错误整理），`include/camel/parse/syntax/token_kinds.def`。
- AST 构建：`src/parse/ast/builder.cpp`（5334 行，读了拒绝路径与最长的几个函数）、`src/compile/gct/builder.cpp` 里转交的拒绝。
- 格式化器：`tools/format/fmt.cpp`、`fmt.h`。
- 诊断：`diagnostics/registry.tsv`（577 行）及其生成物 `src/core/error/diagnostics/messages/*.cpp`，`diagnostics.cpp`、`render.cpp`、`builder.cpp`。
- 对照：`docs/language/02、06、07、08`、设计原则 4.1、4.2、4.4、9.11。

按严重程度排序：A 语言与实现不符、会误导使用者或静默接受错误；B 接受了的语法在后面被拒绝，或拒绝是伪装的语法错误；C 诊断措辞；D 基础设施；E 格式化器；F 结构与重复；G 缺的测试；附：`throw`、`?->`、`module` 删除时要动的清单。

---

## A. 语言与实现不符

### A1. 语言参考 6.12 的一元运算符优先级与解析器、设计原则相反
`docs/language/06_expressions.md` 6.12 表的 10a 行（英文版同）写：一元 `-` `!` `~`"比 `->` 与 `^` 松：`!xs->is_empty()` 是 `!(xs->is_empty())`，`-a->f` 是 `-(a->f)`"。设计原则 4.2 说的是相反的：一元运算符"优先级低于 `^`、高于 `->`"，`-x->f` 与 `!x->f` 都是先取负/取反再传入。解析器按原则实现：`parseLink` 的每个操作数都是 `parseNeg`（`parser_expr.cpp:329-339`），`parseNeg` 先吃前缀运算符（`:351-362`），文件头的优先级表也这样写（`:21-26`）。
- **场景**：读者按语言参考写 `!xs->is_empty()`，得到的是 `(!xs)->is_empty()`，对数组取 `!` 报类型错误，信息里没有任何线索说明自己被优先级骗了。
- **修**：改 6.12 的行（和 6.7 里若有的例子），中英文一起；`docs/language/02_lexical.md` 与 `04` 里凡写"先 `->` 后取负"的地方一并查。

### A2. 没有闭合的字符串会吞掉后面的代码，直到文件里最后一个转义引号
`lexer.cpp:704-741`（普通字符串）与 `:680-683`（`` `…` `` 格式化字符串）：找不到闭合引号时，"字符串止于它最后一个转义引号"（`'C:\dir\'` 是字符串，注释写着 *as it always did*）。循环一直扫到文件末尾，`fallbackEnd` 记的是**整个文件里最后一个** `\'`。
- **场景**：第 3 行忘了闭合引号 `let s = 'abc`，往下 200 行有一个别处的 `\'`；词法分析把第 3 行到那个 `\'` 之间的所有行当成一个字符串，没有任何词法错误，后面的解析错误出现在很远的地方，或者根本不出错（`let s = '…';` 恰好能解析）。
- **修**：删去这条回退。`'C:\dir\'` 可以写成 `'C:\\dir\\'`；真要保留，回退只能发生在**同一行**内，并给一条警告。同时删掉"as it always did"这类历史注释。

### A3. 语言参考 02 的续行操作符表列出了不存在的记号
`docs/language/02_lexical.md:14`（英文同）"下一行以只能是二元的运算符开头"的清单里有 `?..`、`is`、类型里的 `\`。词法分析器没有这三个记号（`?..` 与 `\` 不在 `makeCharClasses` 的标点集里，`\` 是 `kIllegal`，`lexer.cpp:59-61`），语法里也没有 `is`（`token_kinds.def` 无 `KwIs`）。`parser_expr.cpp:21` 的文件头注释同样写着"`postfix as T, is T`"，并把 `try` 列为 `dataExpr` 的一种（`:9`）。这些是已删除功能的残留，读者会据此去找并不存在的写法；`scripts/check-grammar.mjs` 校验的是生成的文法块和关键字表，不校验这一段手写文字。
- **修**：删去这三个；把这张续行表也交给 `check-grammar.mjs`，让它从 `parseLink`/`parseAnno` 等处读出"只能是二元的记号"，或者在脚本里列一份并逐项校验。

### A4. 数字：浮点接受前导零，整数拒绝，帮助文字说"没有前导零"
`lexer.cpp:504-520`：`digits '.' digits` 一律是 `Real`，所以 `007.5` 是合法的实数（注释 `:499` 明说）；`:566-575` 同样的前导零整数 `007`、`09` 被拒（`InvalidNumberFormat`）。`registry.tsv` 里这条诊断的帮助写的是"A number is decimal digits without a leading zero"，语言参考 2.5.2（`02_lexical.md:109`、`:136`）只说整数。三处不一致：到底允不允许前导零？
- **修**：定一条，全部按它来；若实数也不许前导零，`:504` 之前先判；若许，帮助文字与 02 要写明只针对整数。

### A5. `{ ...` 的结构体字面量只剩注释
`parseStmt` 的注释写"`{` 是结构体字面量，当它是 `{}`、`{ name: ...` 或 `{ ...`"（`parser.cpp` 的 `parseStmt` 头注释），而 `braceIsStruct`（`parser_impl.h:296-300`）只认 `name :` 与 `static for`。`{ ...rest }` 这种写法要么是已删除的功能（注释需要改），要么是缺的（待确认）；现在的行为是按块解析，`...` 之后给出一个与语句无关的错误。

---

## B. 接受了的语法在后面被拒绝，或拒绝是伪装的语法错误

下面这些写法，词法、语法、`syntax::Tree` 都接受，到 AST 或 GCT 构建时才以"不支持的构造"拒绝，而设计原则对其中几条已有明确语义：

| 写法 | 拒绝处 | 说明 |
|---|---|---|
| `t[a:b, c]`、`t[a:b, c:d]`（多维的范围或位置混合下标） | `src/compile/gct/builder.cpp:2536-2551`（`ConstructNotSupported`，"the slice operator takes one axis…"） | 设计原则 5.4 把它定义成核心语义（"`t[a:b, c]` 在第一维切片、在第二维取位置"）；这里是缺的功能，不是语法 |
| 带类型参数的 `fact`（`fact range::<T>(v: T): …`） | `src/parse/ast/builder.cpp:1470-1476`（`giveUp`，"…is not compiled"） | `docs/design/rewrite/v2.cn.md:121` 把它写成目标写法 |
| 生成器 `yield`、返回类型 `T..` 的函数体 | `src/compile/gct/builder.cpp:2048`（`FeatureNotSupported "generators"`） | 关键字、语句、AST 节点都有，语义没有 |
| 一条 `fact` 缺 `join`、缺 `case _`、case 不含变量 | `ast/builder.cpp:1492`、`:1524`、`:1773`（`giveUp`，再加 `FactCaseDefaultMissing`） | 是对声明形状的要求，本该是语法层的"期望什么" |

- **问题**：`ConstructNotSupported` 是个兜底的诊断码，消息说的是"这个构建不支持"而不是"语言规定什么"。读者无法判断是自己写错、语言不允许、还是实现还没做。
- **修**：已决定的语义补实现；语言不允许的在**语法层**拒绝，用语法诊断（期望什么）；还没做的才用 `FeatureNotSupported`，并且指向一个名字稳定的特性清单。`ConstructNotSupported` 本身最后删掉（现在剩 2 个调用点）。
- 另：`giveUp` 是 `visitFactDecl` 里的局部 lambda，返回 `Value()` 后外层继续构建（`ast/builder.cpp:1459-1465`），会不会产生后续级联需待验证。

---

## C. 诊断措辞

### C1. 消息仍在教已删除的写法
`diagnostics/registry.tsv` 与生成的 `messages/semantic.cpp`：
- `ThrowOutsideFallible`、`ThrowNotAnError`（`0x000070`、`0x000071`）整条消息就是 `throw`。
- 提到 `?->` 的帮助：`TaggedValueNotOpened`（"opened only by `??`, `!!`, `?->` and by match"）、`TaggedOperandMismatch`、`OperatorNotOverloadable`（运算符清单里的 `'?->'`）、`UnusedFallibleValue`（"Take the value apart with `!!`, `?->` or match"）、`UnhandledError`（runtime）。`->` 传递错误、`?->` 删除之后，这些帮助让读者去写一个会得到"未知记号"的写法。
- `InvalidAnnotation` 的帮助："`@name(args)` is no longer accepted (numeric permissions are tags, design principles 7.2)"——明说了已删除的拼写，且按设计原则 1 的"删除之后它就是普通的语法错误，不留指向新写法的诊断"也不该有这句。
- `InvalidNumberFormat` 的帮助举了 `077` 与 `0o77` 的对照，同样在讲被拒绝的拼写（可保留一句正面的规则：八进制写 `0o…`，不举反例）。
- **修**：帮助只写现在的规则与现在的写法；上面这些随删除 `throw`/`?->` 一起改，`Throw*` 两条删除（编号留空，已有"not reused"的惯例）。

### C2. 用户可见的帮助里引用内部记录
- 33 条消息的**帮助文字**引用"design principles N.M"（`LambdaParameterWithoutType`、`SliceStepZero`、`EnumWrittenWhereATypeIsExpected`、`RuleTagNotRequires`、`AsRightSideIsType`、`ReducedPrecisionUnsupported`……）。设计原则的编号会改（9.11 刚加），编译器输出里的引用就悄悄指错；终端用户也并不持有那份文档。
- `registry.tsv` 的注释满是历史：`# The value used to be 0x010000…`（`:8`）、`# …a rule used to accept…`（`:27`）、`# It used to be a warning…`（`:81`）、`# Renamed from AmbiguousOperatorCall when it became the report for every call`（`:135`）、`# Design principles log 2.10/2.11/2.12/2.13: 0xB0 and up`、`# the rule on rule_when_const_node`、"…retired: there is no `any` type"。这些是提交说明的内容。
- **修**：帮助里去掉括号引用，或换成稳定的锚点（诊断名 → 文档页的映射由生成器维护，编号变了生成物跟着变）；注释只留"这一段码的范围是什么"。

### C3. 语法错误的整理按 token 文本做二次判断
`report.cpp:367-742`（`reportSyntaxErrors`，约 376 行）不是把解析器记下的错误翻译成消息，而是再扫一遍括号（`BracketScan`），按 token 文本（`off == "else"`、`kStatementStart = {"let","var","return","func","type","import","export","module","}"}`，`:676`）做十来条启发式，每条前面写着编号。编号已经乱了：两个"7."、一个插在中间的"6b"，"8."的注释落在"6b"的代码上（`:648-678`）。
- **后果**：规则一多就互相遮蔽——`once(code, token)` 只按（诊断码，错误 token）去重，同一个 token 上第二个不同的问题被丢掉；`kStatementStart` 里的 `"module"` 在 `module` 删除之后是个永远不匹配的词，而缺少 `"namespace"`、`"using"`、`"with"`、`"yield"`、`"static"` 之类现在真能开始语句的词（`startsStmt`，`parser.cpp:331-352`）——两份"哪些词开始语句"的表。
- **修**：让解析器在 `error()` 的当时记录足够的上下文（期望集合已经有了，加上"正在解析的产生式"与开括号栈），`report.cpp` 只做翻译；"语句开头的词"只在 `startsStmt` 一处。

### C4. 词法错误里的 `KeywordAsIdentifier` 建议名
帮助写"Choose another name, for example `'{0}_'`"（registry 第 16 行）：`throw_`、`module_` 之类。无害，但建议的名字没有检查是不是已被占用或是不是合法标识符的后缀（`_` 结尾合法）；可保留。

---

## D. 诊断基础设施

- **二次方**：`Diagnostics::add`（`diagnostics.cpp:440-477`）对每条错误线性扫一遍已存的错误做去重，`checkLimits`（`:674-694`）再扫一遍计数。N 条错误 O(N²)；在"一个泛型函数每个实例报一遍"的程序里不小。用以（类型、码、消息、位置）为键的哈希集合，并分别维护各严重度的计数。
- **去重只对错误**：同一个警告从泛型体的每个实例各报一遍（`UnusedFallibleValue` 一类）。去重的注释只讲了错误的道理，对警告同样成立。
- **`discarded_`**：命令行关掉的诊断被移进一个成员，**以引用返回**（`:441-446`），锁在 return 之前就释放了；两个线程同时丢弃诊断时，一个线程的 `.fix(...)`、`.related(...)` 写进另一个线程正在移动的对象。把返回类型改成不依赖存储的临时对象。
- **`stage` 取自进程级全局**：`stageForDiagnostic`（`:280-297`）对 `Lexical/Syntax/Semantic` 之外的诊断读 `pipelineStageSlot()`——一个带互斥量的全局字符串；并行运行的两条流水线（测试驱动、语言服务器的多文档）互相覆盖。阶段应在建诊断的当时由调用者给出。
- **上限用异常**：`checkLimits` 从 `add` 里抛 `DiagnosticsTotalLimitExceededException`（`:674-694`），所以任何 `diagnostics.of(...).commit()` 的调用点都可能抛；`DiagnosticBuilder` 的使用者没有谁在文档里被告知。若是有意的"到上限就中止"，写进 `commit` 的文档并让 RAII 的使用点知道。
- `renderRich`（`render.cpp:718`，约 100 行）、`Diagnostic::toJson`（`diagnostics.cpp:305-392`，约 90 行）偏长，JSON 是手拼字符串（`escapeJson`、`hexNib`，`:406-438`）；用一个小的 JSON 写出器。

---

## E. 格式化器（`tools/format/fmt.cpp`，2233 行）

我没有找到会改变意义的规则：字符串换引号只在不含引号时转换、含两种引号时原样保留（`:70-93`，注释记着曾被格式检查抓到）；三引号字符串原样保留（`:62-68`）；一元前缀运算符与操作数之间补空格以避免 `!!`、`--`、`->`、`!=` 粘连（`:1604-1610`）；二元运算符一律两侧空格，`->` 与 `..` 不加（`:1615-1625`），与词法的"`<` 紧贴"规则不冲突。有待验证的与可以收拾的：
1. **`std::regex_replace(result, std::regex("^\\s*$"), "")`**（`:259-260`），意图是"去掉空行里的尾随空白"。ECMAScript 的 `^`/`$` 默认只匹配整个字符串的两端（`std::regex::multiline` 是 C++17 才有，libstdc++ 到较新版本才支持），所以这行在多数实现里要么什么也不做，要么在整个文件都是空白时把它清空；还有 libstdc++ 的 `std::regex` 在长输入上递归栈溢出的老问题。待验证；若是 no-op，删掉，用逐行处理。
2. **死代码与残留**：`CommentPreference { Hash, Slash }`（`fmt.h:33`）没人用；`preferSemis` 永远是 `false`（`fmt.cpp:232-252`）；`visitModuleDecl`（`:271-285`，还处理 `#gpu module m` 这样的标签）、`ThrowStmt` 对应的 `RetStmt`、`'?->'`（`:1615`）、`annoExpr` 注释里的 `'!'`（`:1629`，解析器明确说后缀 `!` 不在语言里，`parser_expr.cpp:392-394`）、`channel 2 = "a # line"`（`fmt.h` 的 `FmtToken` 注释，`#` 现在不是注释）。
3. **每个 `visit*` 上方的 ANTLR 风格文法注释**是语法规则的第三份拷贝（另两份：`parser*.cpp` 的头注释与 `docs/language/08`），其中已有过期的（上面第 2 点）。`check-grammar.mjs` 只校验 08 里生成的文法块。让格式化器的注释指向 08 的产生式名，而不是抄一遍。
4. `dispatch`（`:2073-2232`，160 行的宏列表）与 `visitProgram`（114 行）偏长；`fmt.cpp` 2233 行按声明/语句/表达式/类型拆开。
5. **没有"格式化后重新编译，图相同"的性质测试**：`test/plans/feat/format` 有用例，我没有看到对全部 `test/cases` 做"格式化→编译→图一致"的批量检查（红线 5 的往返测试针对的是图与源码，不是格式化器）。格式化器改了一个空白就可能改变续行语义（行首的 `+`、`-`、`(`、`[`、`@`），这条检查便宜且最有用。

---

## F. 结构与重复

- `Lexer::tokenizeImpl`（`lexer.cpp:407-962`，556 行）一个函数含数字、三种字符串、格式化字符串、标点、注释、非法字符。拆成 `scanNumber`、`scanString`、`scanFString`、`scanPunct`。
- 字符串的扫描有**两份**：词法分析器里内联的（`:614-753`）和 `skipQuoted`/`skipFString`/`scanInterpolation`（`:244-356`，注释自己说"it is what the lexer does, without the fallback"）。两份行为已经不同（回退）；`${…}` 里的字符串是否按同一规则结束，取决于哪一份。`:249` 与 `:310` 有一模一样的文档注释各出现一次。
- 保留字表有三份：`token_kinds.def`、`docs/language/02` §2.4（由 `check-grammar.mjs` 校验）、`editors/vscode/syntaxes/camel.tmLanguage.json`（`:284-299` 的 `module-declaration`、`:473` 的 `return|throw|yield`）。上下文关键字有一份单一来源（`scripts/context-keywords.mjs`，插件规则与它对账）是好做法；保留字的插件规则是否被校验，待验证（`check-grammar.mjs` 的校验清单里只明说了关键字表与上下文关键字）。
- `parser_rule.cpp:221` 的 `kClauses = {"join","meet","widen","narrow"}` 是第二份"事实尾子句"的词表（另一份在 `scripts/context-keywords.mjs`）；脚本说它校验"每个词仍被按名字读到"，但不能保证两份相同。
- `ParserImpl::recover`（`parser.cpp:259-292`）在非嵌套处遇到 `func/let/var/import/module/namespace` 就停（不论在不在块里），而 `import`、`module`、`namespace` 只能出现在顶层：在块里跳过错误文本时，一个出现在字符串之外的 `import` 单词把恢复点放在块中间，随后解析器把 `import` 当作块内语句，产生第二个错误（级联）。`recover(topLevel=false)` 里应只用 `startsStmt`。
- `ast/builder.cpp` 5334 行，最长：`visitRuleDecl` 493 行、`visitFactDecl` 397 行、`visitAnnoExpr` 220 行、`visitFuncDecl` 209 行。`visitRuleDecl`/`visitFactDecl` 与 `modules/rewrite` 的规则对象是同一件事的两半，拆到 `parse/ast/rule_builder.cpp`。
- `visitRetStmt`（`ast/builder.cpp:2434-2448`）的注释引用"design principles 3.9""5.4"，`ExitType::Throw` 在 `compile/gir/builder_control.cpp` 里还有一整套分支。

---

## G. 缺的测试

1. 未闭合字符串后面有 `\'` 的文件：词法错误，且不吞掉中间的行（A2）。
2. 优先级：`!xs->f`、`-x->f`、`-2 ^ 2` 的解析树（A1，也钉住文档）。
3. 续行表里每个记号的正例与反例（行首的 `.`、`??`、`->`、`<`、`+`、`-`、`(`、`[`、`@`），以及"表里没有的记号是语法错误"（A3）。
4. 块里的错误恢复不越出块（F 的 `recover`）。
5. 格式化→编译→图一致性，覆盖所有 `test/cases/**/*.cml`（E5）。
6. 每个被拒绝的"不支持"构造一个用例，钉住它是哪一类诊断（B）。
7. 诊断：2000 个相同错误的性能上限；并行流水线的 `stage` 不串（D）。
8. 诊断文字的健康检查：`registry.tsv` 里不得出现已删除的记号与"design principles"字样（C1、C2）——一个脚本即可，放进 `check-grammar.mjs` 的同类检查。

---

## 附：删除 `throw`、`?->`、`module` 时要动的地方

- **记号与关键字**：`token_kinds.def:37`（`KwModule`）、`:51`（`KwThrow`）、`:115`（`QuestionArrow`）；`lexer.cpp:921-923`（`?->` 的三字符分支）；`docs/language/02` 的关键字表与续行表。
- **解析器**：`parser.cpp` 的 `recover`（`:275`）、`startsDecl`（`:299`）、`startsStmt`（`:329`）、`parseDecl`（`:388`）、`parseStmt`（`:464`）、`parseModuleDecl`（`:605`）、`:715`、`:781`、`parseRetStmt` 的文法注释（`:1216`）；`parser_expr.cpp` 的 `parseLink`（`:329-339`）、文件头表（`:9`、`:22`）、`:392` 的注释；`report.cpp:676`。
- **AST 与构建**：`ast/builder.cpp`（`visitModuleDecl` 及其调用 `:440-459`、`:542`；`visitRetStmt` 的 `ExitType::Throw` `:2440`；`:3870` 的 `"?->"` 分支）、`ast/stmt.cpp`、`parse/parse.cpp`、`service/semantic/syntax_index.cpp`、`compile/gct/builder.cpp`、`compile/gir/builder_control.cpp`、`core/func.h` 的 `ExitType::Throw`、`include/camel/parse/syntax/node_kinds.def` 的 `ModuleDecl`。
- **诊断**：`ThrowOutsideFallible`、`ThrowNotAnError`、上面 C1 列出的五条帮助。
- **工具**：`tools/format/fmt.cpp`、`fmt.h`（`visitModuleDecl`、`RetStmt`、`'?->'`）、`editors/vscode/syntaxes/camel.tmLanguage.json`（`:284-299`、`:473`）、`test/cases/tools/grammar/{keywords,modules,operators}.cml`。
- 删除之后，`throw`/`module` 是普通标识符：`throw x` 会得到"意外的标识符"一类的语法错误，没有专门诊断，这正是设计原则 1 要的结果。

---

## 建议的处理顺序

1. A1（改文档）、A2（删除回退）、A3：不动语义，修改小。
2. C1、C2 与"附"里的删除一起做；加 G8 的脚本，防止再长回来。
3. B：逐项决定"实现"还是"语法层拒绝"。
4. D 的二次方与全局 `stage`；C3 的整理器重构（解析器多记上下文）。
5. E1（正则）、E5（批量往返测试）先于任何格式化器改动。
6. F 的拆分在测试钉住之后。
