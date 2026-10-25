# `yield` 是关键字还是上下文关键字：交接说明第 7 项与 H248 计划的验收互相冲突

<<<<<<< HEAD
> 说明（协调者）：这条实现已并入 develop（`yield` 成为关键字、语句与 AST 节点，文法表与生成物同步，H248 计划里的 t9 反例自我一致）；页里引用的行号以并入时为准。本页问的仍是规格：交接说明把它列为上下文关键字，而计划要求它是普通关键字。


=======
>>>>>>> handover/h579-yield
## 是什么

`yield` 的语法收口（`docs/handover/README.cn.md:207` 的清单第 2 项，`README.cn.md:209`：流的 `yield`）已经落地：词法里加了关键字、`retStmt` 读第三个选项、AST 里一直有、却没人产生的 `ExitType::Yield` 有了产生者、编译器对写 `yield` 的函数体按名字拒绝。**但两处规格对"它是不是普通关键字"说法相反**，本页要的是这一处的裁定（语义本身不缺口，见下）。

## 逐字证据

- 交接说明第 7 项（`docs/handover/README.cn.md:217`）把它列进**上下文关键字**：给出最终的关键字表与上下文关键字表（`rule`、`fact`、`when`、`uses`、`pure`、`yield`、`tag`、`on` 等**只在特定位置是关键字的词**），分组写清。
- H248 计划的验收（`docs/design/io-concurrency-streams/plan.cn.md:307`）要求它是**普通关键字**：`yield` 成为关键字之后 t9 **必须**变成语法错误（这是「加了关键字」的负向验收）。t9 就是 `let yield = 1`（同文件 `:299-306`，原文记着它当时 `RUN_EXIT=0`、stdout `1`）。
- 我按后者实现（更具体、且带负向验收），主机逐字读数：
  - `test/cases/compile/stream/yield_keyword.cml`（即 t9）：`…:8:9: [Error]: 'yield' is a keyword and cannot be used as a name. Choose another name, for example 'yield_'. (name=SyntaxDiag::KeywordAsIdentifier, code=0x02…)`
  - `docs/language/02_lexical.md` §2.4 的控制行现在是 `… `return` `throw` `yield` `sync` …`（中英同步）；`node scripts/check-grammar.mjs --check` 逐字通过，它双向核对这张表与 `token_kinds.def`。
- 若按第 7 项办（上下文关键字），`let yield = 1` 必须**仍然合法**，t9 那条负向验收作废，`KeywordAsIdentifier` 不能对它报。
- 今天的用量：`.cml` 里把 `yield` 当名字写的地方 **0 处**（除本次新增用例；`git grep '\byield\b' -- '*.cml'` 余下三处是英文注释里的 "yield a gradient" 之类）。所以两种裁定的兼容代价都只在规格与表，不在用户程序。

## 已有裁定

- `docs/handover/reviews/questions-2026-10-23.md:394` 已定 `yield` 的语义 (a)–(i)：一轮里可以多次、恢复点在 `yield`、任意层嵌套、`match` 臂与 `sync` 块都可以、故障发生在产生那个元素的那次拉取处、产出的流值线性、不做委托、`return` 结束流、空流合法。
- 生成器状态机属 H248 片 E（`plan.cn.md:282-307`），不在本次语法收口里；`test/cases/compile/stream/type_signature.cml` 的注释也把"产生流值"留给另一片。本次只交语法、AST 与"按名字拒绝"。
- 参考里**目前只有一张关键字表**（§2.4）：`resource`、`tag`、`on`、`unordered` 这类上下文关键字根本没进参考，所以"上下文关键字表"是第 7 项要新建的东西，本页不代它决定。

## 选项与代价

1. **`yield` 是关键字（本次按此实现）**：t9 负向验收成立；插件表把它与 `return`/`throw` 同列控制关键字；代价是它不能再当名字（今天 0 处）。
2. **`yield` 是上下文关键字**：`let yield = 1` 继续合法；代价是 `plan.cn.md:307` 的验收作废、`KeywordAsIdentifier` 不能用、解析要改成"语句位置 + 在函数体内"判定，第 7 项的上下文表要把它连同每个允许位置逐条写清。

## 卡住什么

不卡本次语法收口（按 1 实现，每条都有逐字读数）。若裁定为 2，回退量很小（关键字表一行 + 一条负向用例 + `retStmt` 的第三个选项改成上下文判定）；若裁定为 1，第 7 项的插件表要把 `yield` 放进"关键字"行，而不是"上下文关键字"行。