# 文档还在教已退役的规则

**这份账与代码红单分开：代码红了会被闸门挡住，文档红了不会。** 所以它需要单独一页。

## 被退役的规则

决策者 2026-11-15 裁定（`internal/reviews/q-2026-11-15-what-the-side-effect-warning-says-now.cn.md` 文末）：

> 按 11-15 定的新语义，**给 `var` 赋值不是效应**（原则 3.1「给 `var` 赋值不是效应」、3.5），所以这两条警告对 `var` 都不该出现。

由此：
- `IgnoredSideEffect` 删掉 `var` 那一支（`buildCall` 的 `hasVarArgument`），**捕获变量那一支保留**；
- `EffectNotExecuted` 只剩「一个外部写所在的区域没有运行」一个含义；
- 一次没人读的赋值改报 `UnusedAssignedValue`（按**值**报告，措辞不含「效应」「副作用」）。

**代码已按此改（`8b541bdba`）。文档没有全部跟上。**

## 还写着旧规则的地方

| 位置 | 现在写的是 | 应改为 |
|---|---|---|
| `docs/language/04_execution_model*.cn.md` / `.en.md` 的告警样例 | 用 `var` 赋值演示 `IgnoredSideEffect` / `EffectNotExecuted` | 用**真正的效应**（打印、外部读）演示；`var` 赋值改演示 `UnusedAssignedValue` |
| `test/tutorial_examples` 的 `06_let_to_var` | 章节正文教「`var` 赋值在 `sync` 外会被忽略」 | 同上 |
| `test/tutorial_examples` 的 `10_write_nobody_reads` | 同上 | 同上 |

## 判据

**一条都不许用「改期望」的方式消掉**。这些红不是实现错了，是**教材还在教一条已经不存在的规则**——
`feat/tutorial_examples` 的两条与 `docs/language/` 的样例，都要**改正文**，不是改断言。

## 为什么它不该由车道顺手做

改教程正文会改变**读者对语言的理解**，而教程是给第一次学 Camel 的人看的。
**顺手改正文的车道，会顺手把「为什么这条规则被废掉」也写错。**

所以：这条账归文档，不归实现线。**等作者定「新语义该怎么向初学者讲」，再动手。**
