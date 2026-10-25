# 面向读者的页面里，哪些状态标注不能直接删掉

交接说明 5.32 第 0 条与 `docs/handover/reviews/questions-2026-10-24-evening.md` 的"负责人 2026-10-25：文档按目标写"要求删掉面向读者页面里的"目标""尚未实现""保留"这类标注。本页记下**没有删的那些**与理由：它们都不是"已经裁定的特性还没实现"，而是"这条规则还没定"或"这个能力还没有裁定"。判据是：删掉标注以后，剩下的话若变成一句关于目标语言能力的断言，而那个能力没有裁定，就不删（AGENTS.md：未决问题归语言作者）。

## Q1：切片步长

`docs/language/06_expressions.md` 与 `en/06_expressions.md`：

> 步长目前只支持 `s == 1`。步长不为 1 的切片是带步长的视图，需要 H17 的数组表示，所以现在是故障 `RuntimeDiag::InvalidArgument`（张量上是编译期错误）。

"目前"是状态标注；删掉它，剩下的话就成了"步长是 `s == 1`"——一句关于目标语言规则的断言。目标语言里步长切片是什么，要等数组表示（H17）落地后裁定。

## Q2：`constas` 的一串限制

`docs/language/07_statements.md` 与 `en/07_statements.md` 的"目前的限制 / Current restrictions"一条：删掉抬头以后，剩下的是"泛型函数不能加标注""`enum` 值还不能作为 `constas` 实参"等一串"不能"。这与 Q1 同理：它断言的是目标语言的一条限制，而这条限制的将来没有裁定。

## Q3：规则"还没定"的三处

`docs/language/01_introduction.md` 的"当前实现的行为，尚待确定"、`docs/language/04_execution_model.md` 的"行为尚未决定"、`docs/language/en/03_data_model.md` 的 `their meaning is not yet decided`。这是**规则的确定状态**，不是实现状态："按目标写"说的是已经裁定的目标语言，未决的规则写成已决会凭空造出一条规则。

## Q4：写未来后端的一句

`docs/language/07_statements.md`："以后的后端（如 Tiles）可以在有许可的节点上用别的算法。"这不是"目标语言"的一部分，删掉会把它写成现在的事实。

## Q5：顺带看到、未改的两类缺陷

- 面向读者的页面里点名内部任务号：`docs/language/02_lexical.md`（H89）、`docs/language/06_expressions.md` 与 `docs/language/en/06_expressions.md`（H17，各一处）、`docs/language/06_expressions.md`（"（2026-10-25 的裁定）"）。
- 已删除的章节留下的悬挂引用：`docs/reference/camel-for-ai.md` 曾指向 §13/§14；本次已修掉正文里的五处（含生成区域的关键字分组标签），其余页面若还有，请一并清。