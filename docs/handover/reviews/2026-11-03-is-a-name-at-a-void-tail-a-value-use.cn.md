# 一个 `void` 函数体尾部的名字，是"被丢掉的值"还是"被当成值使用"？

## 问什么

`func g(): void { let step = len }` 修好之前是一句 **internal error**（`still contains a live DREF node`），而只差一层形状的
`func g(): void { len }` 今天给的是正经诊断 `FunctionNameAsValue`（"`len` 有 4 个签名，不是函数值"）。**同一个尾位置，
"先 `let` 再当值"是内部错误、"直接写名字"是诊断，说明这个位置对"名字"的裁定从未写下来过。** 我按"`void` 体丢掉
最后一个语句的值，丢掉一个名字不等于使用它"修了（见下 §2 的读数），于是 `func g(): void { len }` 也不再报诊断。
**这一处要不要保留诊断，是语义裁定，不是实现选择。**

## 1 三种写法今天（修前 / 修后）的读数

修前（`origin/develop 0c7e5f20a`，干净构建）：

| 程序 | 修前 | 修后 |
|---|---|---|
| `func g(): void { let step = len }` | internal error（live DREF，draft ref 0） | **编译通过，exit 0，无输出** |
| `func g(): void { len }` | `FunctionNameAsValue`（"`len` 有 4 个签名"） | **编译通过，exit 0，无输出**（本页要裁的那一处） |
| `func g(): int { let step = len }` | internal error（同一句） | `FunctionNameAsValue`，位置在 `let` 那一行 |
| `func main(): int sync { { len }; return 0 }` | `FunctionNameAsValue` | `FunctionNameAsValue`（未动） |
| `func main(): int sync { { let s = len }; return 0 }` | 编译通过 | 编译通过（未动） |

最后两行之间的不一致（同样"值没人要"的块，直接写名字报错、`let` 绑定不报错）在我改之前就存在，我没有动它。

## 2 修法：丢掉的值不是值使用

缺陷的准确形状不是"只有 `void` 函数"（`func g(): int { let step = len }` 同样崩），而是**函数体的最后一个语句的值被交给
图当出口**：`void` 只是"合法程序不必写 `return` 就走到这个位置"的那种写法。`let step = len` 里的 `len` 有 4 个签名，
`let` 绑的是**名字**（设计原则 §2.1：函数值 = 图 + 捕获的值；一个名字不是值），于是那张 `NREF` 语句的值是一个 `DREF` 节点，
而 `void` 出口把它当成了自己的值 —— `graph_encode.cpp#prepareGraph` 的活 DREF 检查就炸了。修法见 §5。

## 三条路与代价

- **甲（默认）：`void` 体的尾位置是"丢掉值的位置"，名字在那里不算值使用。** 代价：`func g(): void { len }` 丢掉一条
  诊断（现库内无任何用例钉它，`feat/compile/surface` 全绿）；好处是与"`void` 体丢掉最后一个语句的值"（`acceptsAsResult`：
  "A function declared `void` discards whatever its body ends with"）和 `func g(): void { 1 + 2 }` 今天就不报错这一事实一致。
- **乙：`void` 体的尾位置算值位置，两种写法都给 `FunctionNameAsValue`。** 代价：`let step = len`（一个没人调用的函数值）
  变成错误；而 `scripts/stdlib-reference.mjs` 生成的 doc example 正是 `func __doc_example(): void { let __doc_example = split }`
  这一形状 —— 一条签名之差的库函数就会把一个文档示例判红，等于要求作者先裁定"未使用的函数值该不该报错"。
- **丙：只让 `void` 尾位置的"直接名字"继续报错、`let` 绑定不报。** 代价：把一个形状差当成两个规则写进代码，
  下一个人分不清哪条是有意的。

## 默认

**按甲开工**（已实现）：`void` 体的尾位置保持"丢掉值"，名字在那里不产生节点、也不产生诊断；`func g(): int { let step = len }`
这种**值真的被用**的尾位置仍按乙给诊断（`'step' is used as a value here, but it names an operator with 4 signatures`）。
`surface.dropped_function_name` 与 `surface.function_name_at_tail` 两条用例分别钉住这两侧。

## 4 这一处能碰到多少份文档示例

一份都没有修之前是**一份**：`stdlib/signatures/transforms.cml:66` 的 `@example let step = split`。`scripts/stdlib-reference.mjs#exampleSource`
把每个 `@example` 都包进 `func __doc_example(...): void`，所以只要示例的最后一个语句是 `let x = <一个没有单一函数值的名字>`，它就站在
这个位置上 —— 修前它是 `internal error: Compile graph '__doc_example' still contains a live DREF node at draft ref 0`，
修后编译通过（这一份还让它的 `@example-fragment` 标记变成陈旧：原来那句"示例是片段"是靠编译器崩掉才对上的）。

按形状数则是 **28 份**：1 份 `let step = split`，加上 `stdlib/rulesets/jvp.cml` 里 27 份 `@example dtod` / `ftod` / … 这样的单名字示例
（它们今天能编译只因为那几个转换算子各自只有一个签名；多一个签名就落到同一个位置）。整个 566 份可编译示例都在这个包装里，
所以"一个签名之差"是这个位置的普遍性，不是这一份的偶然。

配套的计数表（`scripts/stdlib-reference.mjs` 的 `EXAMPLE_STOPS` / `STALE_FRAGMENTS`）**这一笔没有动**，因为它在 develop 上本来就是红的：
同一棵树、修前的二进制跑 `--verify-docs` 已经报 `EXAMPLE_STOPS['a semantic error'] records 39, and the run found 37` 与
"1 example class(es) the run found and the script does not record"；而 `doc-examples` 那条车道上还有 4 份 stdlib 文件的改动没有并进来
（`graph.cml` / `onnx.cml` / `rewrite.cml` / `signatures/transforms.cml`），换表号的人和它同一批改，我在这里改谁的数字都对不上。
**这笔修法带来的两条新事实交给那张表的下一次改版**：`an internal error` 这个类现在是空的（最后一份就是 `split` 的示例），
`STALE_FRAGMENTS` 0 → 1（`split` 的标记该删，而那一行在别人的文件里）。

## 5 给以后一句

**"某位置是不是值位置"要按位置的类型裁定，不要按位置上写了什么裁定** —— 同一个尾位置，直接写名字报错、写 `let` 崩内部错误，
就是因为它按"最后一个语句长什么样"分支。

## 6 证据

- 缺陷与修法：`src/compile/gir/builder.cpp#visitExecNode`（尾值）、`src/compile/gir/graph_encode.cpp#prepareGraph`（活 DREF 守卫）、
  `src/compile/gir/builder.cpp#visitNRefNode`（`let` 绑名字）、`src/compile/gir/builder_calls.cpp#functionValueOf`（只有单一签名才成函数值）。
- 用例：`test/cases/compile/surface/dropped_function_name.cml`、`test/cases/compile/surface/function_name_at_tail.cml`；
  计划 `test/plans/feat/compile/surface.plan.toml` 的同名两条。
- 探针：`D:\Temp\dref-probe*\`（修前 / 修后同一批 17 个变体）。

## 7 裁定之后（作者裁定 `290f338d3`，2026-11-03；本页 §1 的最后两行由此收掉）

裁定是：**规则只有一条 —— "丢掉值的位置不算值使用"，不按形状写成两条（丙不取）**。于是 §1 里标"未动"的那一行也照这条
规则收掉，它不再是"未动"：

| 程序 | 修前 | 现在 |
|---|---|---|
| `func g(): void { 1 + 2 }` | 编译通过 | 编译通过 |
| `func g(): void { len }` | 编译通过 | 编译通过 |
| `func g(): int { let step = len }` | `FunctionNameAsValue`，位置在 `let` | 同左（值真被用的位置，未变） |
| `func main(): int sync { { len }; return 0 }` | `FunctionNameAsValue`（2:27，`len`） | **编译通过，exit 0，无输出** |
| `func main(): int sync { { let s = len }; return 0 }` | 编译通过 | 编译通过（未变） |

实现：`src/compile/gir/builder.cpp#visitExecNode` 现在先问一个"这个位置丢不丢值"（`valueDropped`、`: void` 尾位置、
`visitInPosition` 传下来的 `void` 位置 —— 与 `branchTakesNoValue` 问分支的是同一个问题），丢的时候一律走
`src/compile/gir/builder.cpp#droppedValueAt`，也就是 `08622b019` 的 `void` 尾走的那一处，**没有第二份**；值真被用的位置仍走
`namedValueOf`。用例：`dropped_value_statement.cml`（§1 能编译的四行，加两层嵌套的同一位置）、
`function_name_bound_at_a_used_tail.cml`（第三行，4:17 逐字钉住）。`feat/compile/surface` 24/24。