# `loop` 的两套 `for`：`for i in ...` 与 `static loop ... for (i) in ...`

Author: 车道 `handover/h435-learnability`（`D:\Projects\Camel-wt\h435-learnability`），2026-10-24。Status: open — 需要一句裁定：`static loop` 的 `for (名) in <?>` 里 `?` 允许什么；以及普通 `loop` 不带括号、`static loop` 带括号这一对区分是有意的还是遗留。测量在 Linux 主机（`88zjwei`）上、基线 `handover/h425-ai-reference` 的构建（`build-info.json` 的 revision 为 `2e6438492`，dirty=false）上完成。

## 数字先

六条写法，全部实测（`camel <file>`）：

| 写法 | 读数 |
|---|---|
| `loop acc = 0 for i in 0..5 do acc + i` | `10` |
| `loop acc = 0, n = 1 for i in 0..4 do (acc + n, n + 1)` | `10 5` |
| `loop acc = 0 while acc < 5 do acc + 1` | `5` |
| `loop acc = 0 for i in 0..10 while acc < 6 do acc + i` | `6` |
| `static loop acc = 0 for i in 0..5 do acc + i` | `SyntaxDiag::MissingToken`: `Expected '(' before 'i'.` |
| `static loop acc = 0 for (i) in 0..5 do acc + i` | `SyntaxDiag::UnexpectedToken`: `Unexpected '0'; expected a name.` |

- 前四行是同一族：普通 `loop` 的 `for` 后面是**裸名字**，`0..5` 这种区间字面量可以跟；`for` 与 `while` 可各自省略，多个状态用逗号分隔，表达式取最后一个状态。
- 后两行说明 **`static loop` 要括号**：不给括号是 `MissingToken: Expected '('`；给了括号以后 `in` 后面那个位置**不接受区间字面量**（`0` 被当成名字位置）。
- 语言作者页面 `docs/language/07_statements.md` 写的是 `static loop acc = 0 for (j) in indices ts do acc + ts.[j]`——括号里后面跟的是 `indices ts`（一个应用，不是区间字面量），与上表最后一行"必须不是区间字面量"的读数相容，但没有说明 `?` 的完整范围。
- 参考文档（本车道产物）§5 的 precedence 区（由 `src/parse/syntax/` 的产生式生成）里，普通 `loop` 的产生式是 `loopFor : 'for' identDef 'in' logicalOrExpr`，`identDef` 是一个名字——与上表前四行一致。

## 我这一轮的做法（只写实测过的那一套）

`docs/reference/camel-for-ai.md` §6 新增了 `loop` 的条目：给出状态、`for <name> in <range>`、`while <condition>`、取值为最后一个状态，并明确 `for` 后是裸名字、`for i in 0..5 { ... }` 是 `SyntaxDiag::UnexpectedToken`（这正是原文 §11 教过的写法，同一轮一并改掉）。§6 的例子加了 `println(loop acc = 0 for i in 0..5 do acc + i, loop n = 3 while n > 0 do n - 1)`。

**`static loop` 没有写进参考文档**：它的括号形态和它到底接受什么（只能是名字？可以是 `indices ts` 这样的应用？区间字面量为什么不行？）没有实测到能写清的程度，写进去就是猜。参考文档只在普通形态上给读者一条能编译的路。

## 待定

1. `static loop` 的 `for (名) in <?>` 里 `?` 允许什么表达式？若答案是"名字或应用，不接受区间字面量"，那么 `Unexpected '0'; expected a name.` 应当换成能指出这一点的消息（今天这条会把人引向"少写了括号"，而括号已经在）。
2. 普通 `loop ... for i in ...`（裸名字）与 `static loop ... for (i) in ...`（括号）是有意的语法区分，还是 `static` 加进来时的遗留？若是前者，两份文档都应把两者并列写清；若是后者，值得统一。
3. `static loop` 的迭代变量是不是编译期常量（可用来做 `ts.[i]` 这种静态下标）？若是，参考文档需要一个可编译的例子，本车道没有实测到。

## 我没有做

没改 `docs/language/**`、解析器、`src/parse/syntax/`；没在参考文档里写 `static loop`；没有为 `static loop` 造一个我没编译过的例子。