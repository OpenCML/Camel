# `static loop` 序列：两参数 `range` 的位置、元组那一轮 `x` 的读法、以及"复用常量折叠"能做到哪一步

（对 `docs/handover/reviews/questions-2026-10-24-evening.md` §static-loop-sequence-forms 的回报。裁定原文：
"按第 2 条补实现，不收窄措辞"，范围是那一个判据，四条附加要求里"四个探针程序入库成用例（现在是负例的，
补完后翻成正例）""求值复用常量折叠，不另写一个求值器"。本 lane 的实现在
`handover/h475-static-seq-forms`。）

## 结论（已实现，全部以主机 `88zjwei` 上的构建逐字读出）

判据落在一个函数上：`Builder::staticLoopSequenceOf`（基线 `src/compile/gir/builder_static.cpp:704`，
拒绝点 `Builder::visitStaticLoopNode` 同一文件 `:781`）。`in` 后的表达式由编译期常量经可自由排布的运算
组成时被求值再展开；求不出来仍是 `SemanticDiag::StaticLoopSequenceNotKnown`，消息一字未改。

四个探针（`notes/h471-probes/` 的原文）在补实现前后的逐字读数：

| 探针 | 补实现前 | 补实现后 |
|---|---|---|
| `probe_interval.cml`（`0..3`） | `3` | `3` |
| `probe_array.cml`（`[1, 2, 3]`） | `error[SemanticDiag::StaticLoopSequenceNotKnown]: … ': '[1, 2, 3]' is not.` | `6` |
| `probe_range.cml`（`range(0, 3)`） | `… ': 'range(0, 3)' is not.` | `error[SemanticDiag::RangeTwoArgumentsRemoved]: The two-parameter 'range(a, b)' is gone; write the interval 'a..b'.` |
| `probe_tuple.cml`（`for x in ts do acc + x`） | `… ': 'ts' is not.` | `error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'x'.`（`4:53`，`x` 上） |

`range(a, b, step)` 三个实参都是常量时：`range(0, 9, 3)` → `9`；负步长 `range(5, 0, -2)` → `9`；
`let a = 0 / let b = 10` 再 `range(a, b, 3)` → `18`。元组那一轮的名字用在类型位置上：`for x in ts do
acc + tag::<x>()` 对 `(int, float, string)` 是 `6`，空元组是 `0`（`test/cases/compile/static_expansion/
static_loop_sequence_tuple.cml`）。常量数组（`let xs = [1, 2, 3]`）按元素走，字符串元素按自己的文本当
`t.[s]` 的键。

## 交回的问题

1. **两参数 `range(a, b)` 在 `in` 后面要不要接受？** 协调件把"四个探针翻成正例"写成一条要求，而 h471 的
   `probe_range.cml` 写的正是 `range(0, 3)`；裁定正文写的却是 `range(a, b, step)`（三个实参）。我按已裁定的
   "两参数 `range` 删除"（设计原则 4.2 与 2026-10-24 的 `h248-generator-and-composition`：单位步长只写 `a..b`，
   `SemanticDiag::RangeTwoArgumentsRemoved`）实现，所以该探针在序列位置也报这条，没有翻成正例——否则语言里
   会出现"当值是错、当序列是对的"的第二种写法，与"一件事一种写法"相反。若作者要序列位置单独接受两参数，
   改动很小（`builder_static.cpp` 里那一个 `size() - 1 == 2` 的分支），请裁定。
2. **元组那一轮 `x` 的读法确认。** 裁定写"每轮 `x` 是该位置的类型"，所以 `probe_tuple.cml` 的 `acc + x`
   （把 `x` 当值）不可能成正例：它在补实现后报 `UnresolvedReference: 'x'`，而"按类型用"的正例是 `tag::<x>()`。
   入库的是两个用例：`static_loop_sequence_tuple.cml`（正例，`x` 当类型）与
   `static_loop_sequence_tuple_value.cml`（负例，`acc + x` 逐字保留探针原文）。若作者其实想要 `for x in ts`
   绑定元素的值（那样与普通 `loop` 的区别只剩"展开"），请裁定；两种读法不能同时成立。
3. **"求值复用常量折叠"的边界。** 既有常量折叠今天**给不出**那个 `int[]`：`Builder::evaluateCallAtBuildTime`
   先把"无函数体的声明"挡掉（`src/compile/gir/builder_when.cpp:1722`，`range` 正是这种），再用
   `isConstantKind` 挡掉复合类型的结果（同文件 `:1857`，只有 Int32/Int64/Float32/Float64/Bool/String）。
   所以本 lane 的复用是：实参（区间两端、`range` 的三个实参）走同一个读法——字面量、绑在常量上的名字、
   否则交给 `Builder::constantValueOrNull`（`compileTimeExpression`，即既有折叠）——并把整数枚举成**同一段**
   代码（`walkIntegers`，区间是步长 1 的那一次调用），没有第二个求值器。若作者要折叠本身返回 `int[]`，
   那是放开共享折叠的结果种类闸门，属另一处改动（会影响 `when` 断言与展开的折叠），请裁定是否要做。

## 附：这一批的两处已知后果（不阻塞）

- `for i in 0..n`（`n` 是参数）仍报 `StaticLoopSequenceNotKnown`，逐字未变：
  `The sequence of a 'static loop' must be known when the function is compiled: '0..n' is not.`（`4:12`，
  下划线整条 `static loop …`）。
- 三个名字（`for n, F, tags in …`）仍是语法错误，逐字未变：`error[SyntaxDiag::MissingToken]: Expected 'in'
  before ','.`（`4:40`）。