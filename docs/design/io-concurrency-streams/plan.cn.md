# 流 `T..`、`yield`、函数组合 —— 实施计划

依据：任务书 5.28、设计原则 5.4 与 4.2（`docs/design-principles.cn.md`）、`docs/design/io-concurrency-streams/README.cn.md` 第 3 节。体例照 `docs/design/graph-store/plan.cn.md`。

**本轮只交计划，没有改语言表面。** 所有「现状」条目都在主机上实测（见 1.1 与附录 A、附录 B），逐字输出抄在附录 A；计划里引用的符号在提交前由 `node scripts/check-doc-links.mjs --citations` 校验为 0 unverified。

---

## 0. 结论摘要

1. **`..` 今天有四种身份，没有一种是流**：（a）类型位置**只在 `[...]` 里**的维度包 `T[..]` / `T[..rest]`（已落地，实测 `let xs: f32[..] = [1.0, 2.0]` 编译并打印 `[1.0, 2.0]`）；（b）表达式里的**二元运算符**，`CompExpr` 层次（`ParserImpl::parseComp` 同时吃 `..` 与 `?..`）；（c）落图时 `CompExpr` 编译成编译器自有宏算子 `:macro/cmp`；（d）`?..` 是**保留记号**，报错不实现。任务书要的 `T..`（类型后缀）与 `f..g`（函数组合）都要在 (b) 这一层落地，并与 (a) 共存。**`T..` 是今天唯一没有先例的形状**。
2. **`f..g` 与 `?..` 今天都是硬错误，而且是同一个诊断**：`Builder::visitCompExpr` 对 `..` 报「function composition is not implemented」，对 `?..` 报「the operator is not implemented」，两处都是 `SemanticDiag::ConstructNotSupported`。诊断**不看操作数类型**：实测 `let xs = 1..5` 拿到的仍是「function composition is not implemented」——即「两边是数时是区间」这条读法今天连诊断都没有对应物。今天等价的区间写法是库函数 `range(start, end)`。
3. **`yield` 不是关键字**：实测 `let yield = 1; println(yield)` 编译并打印 `1`。`include/camel/parse/syntax/token_kinds.def` 的词法表里没有 `yield`；`src/parse/ast/stmt.cpp` 里出现的 `"yield"` 只是 `ExitStmt` 的打印名，与生成器无关。
4. **流类型不存在，而且是核心类型层面的缺口**：`TypeCode` 枚举（`include/camel/core/type/base.h#TypeCode`）没有流；`include/camel/core/type/composite/array.h#ArrayType` 的开秩形状 `[..]` 是**最接近**的邻居，但它是数组（有长度、可下标、可追加），不是按需产生的序列。写 `T..` 今天在**类型位置**是语法错误（实测 `let s: int.. = [1, 2, 3]` → `SyntaxDiag::UnexpectedToken`「Unexpected '..'; expected '='.」）；`as int..` 同样（「expected '}'.」）。
5. **`map`/`filter`/`reduce`/`foreach` 今天只作用于数组**，是 `:mark/*_arr` 一类**带 `with` 参数的算子**（`src/builtin/modules/globals.cpp#map_arr` 等，`stdlib/prelude.cml#map` 的声明是 `with <f: (item: T) => U> (collect: T[]) => U[]`）。流的对应物（`scan`/`batch`/`zip` 流版/`take` 流版）**一个都没有**：实测在 `stdlib/` 与 `src/builtin/modules/` 里 `scan`、`batch`、`unfold`、`tee`、`window` 的声明数都是 0。`fold` 今天只有两个**遍**叫这个名字（`stdlib/std.cml#fold`、`stdlib/opt.cml#fold`），不是流折叠；数组的折叠叫 `reduce`。
6. **驻留今天只是一个「推断 + 打印」，不插节点、不驱动执行**：`modules/tiles/src/residency.cpp#residencyReport` 按 `docs/design/gpu/plan.cn.md` 3.2 从图推出「每个值的块在哪、哪些值跨边界、调用搬不搬」，由 `modules/tiles/src/region_pass.cpp#RegionPass` 印成 `TILES_RESIDENCY`，注释明写 *"It reads the graph; it inserts no node yet."*。所以任务书第 4 条要的「涉及流的图可以驻留执行」今天**没有任何可复用的执行器**；可复用的是这套**推断的形状**（owner 表 + 跨边界计数 + 循环状态留在原地）。
7. **规则引擎今天表达不了 `map` 融合，有两道各自独立的门**：（a）**项读取器**只接受算子、常量、参数，并且**明确拒绝带上下文（`with`）参数的算子**（`src/runtime/rules.cpp#SideReader::term`：「has an operator with a context argument or an order dependency」），而 `map` 的 `f` 正是 `with` 参数——实测 `std::gir` 里两个 map 节点各有一条 `type="WITH"` 的入边（`Function(graph=dbl, tupleSlots=0) -> N0A`）；（b）**规则的形参表写不出函数类型**：`rule h248_map_comp(xs: int[], f: (int) => int, g: (int) => int)` 在 `)` 处报 `SyntaxDiag::MissingComma`。另外规则体里必须写 `map<inc>(xs)` 而不是 `map(xs, inc)`（实测 `SemanticDiag::NoMatchingFunction` 给了这句话），所以融合规则的**左式拼写**已经定死为尖括号形式（泛型规则写法也在同一层）。
8. **`for x in s` 今天只认数组、字符串、映射、张量**：`src/compile/gir/builder_control.cpp#LoopSequenceNotIterable` 的消息把四类逐一列出，**没有流**。扩展点是现成的：`src/builtin/modules/globals.cpp#__iter__` 是一个「每种可遍历类型一个重载」的算子组，张量模块与映射已经各加了一个。
9. **切片按可并行度分 5 组**：**第 1 组（互不依赖，可同时开工）** A（`f..g` + 删 `?..`）、B（`T..` 类型与两种读法）；**第 2 组（B 之后，四片可并行）** C（纯算子）、D（来源与去处）、E（`yield` 状态机）、F（`for` 遍历流）；**第 3 组** G（线性检查与 `tee`，等 B、C）；**第 4 组** H（两种调度与无限流 `take`，等 C/E/F 与 GPU/JAX）；**第 5 组** I（规则融合，等 A、C 与规则语言）。
10. **要问作者的语义空白已单独成文**（不给结论）。其中**必须先裁一条**：`docs/design/io-concurrency-streams/README.cn.md` 第 6 节第 5 条仍写「`Stream<T>` 是标准库类型（不是核心类型）」、且「拉取是这个流自己的资源上的副作用」，与设计原则 5.4 和同稿第 3.1 节（流是值、只有来源与去处是效应）**直接冲突**；设计原则优先，但稿子第 6 节没有随 3.1 一起改（见 1.10）。

---

## 1. 现状：逐条证据

### 1.1 方法与探针

**不在本机构建或测试**（会蓝屏）。全部测量在主机 `88zjwei` 上，走 `camel-remote.ps1`，工作树 `D:\Projects\Camel-wt\h396-h248-plan`，基线 `origin/develop` = `ad9ed160e`：

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 `
  -Path D:\Projects\Camel-wt\h396-h248-plan `
  -Plan test/plans/feat/compile/_h248probe.plan.toml -Tier fast -Jobs 8
# 退出码从运行器带回：RUN_EXIT=1（14 条探针全部按「今天失败」记录）
```

探针是**临时**的（`test/cases/compile/_h248probe/*.cml` 与 `test/plans/feat/compile/_h248probe*.plan.toml`），提交前已删除——它们不是本任务的交付物。每条探针的期望是空的：`verify_script` 打印捕获到的 `exit_code` 与输出后故意失败，所以运行器把**逐字的编译输出**印出来（`verification output: ...`）。**退出码读的是运行器的返回值，没有用 `echo $?` 一类的被引号包住的 shell 字符串测。** 探针源码与逐字输出见附录 A。

### 1.2 `..` 今天的四种身份

| 身份 | 位置 | 落点 | 今天 |
|---|---|---|---|
| 维度包（已落地） | 类型位置，**只在 `[...]` 里** | `ParserImpl::parseArrayDim`；`include/camel/core/type/composite/array.h#ArrayType::openRank` | `T[..]`、`T[3, ..rest]`；实测 `f32[..]` 可声明可打印 |
| 二元运算符 | 表达式 | `ParserImpl::parseComp`（同时吃 `..` 与 `?..`），AST 结点 `CompExpr` | 建图时报 `ConstructNotSupported` |
| 宏算子 | 落图以后 | `src/compile/gct/builder.cpp#MacroCmp`，`:macro/cmp`，`src/builtin/operators/macro.cpp#__cmp__` | 实现体是 `return NullSlot`（空实现）；因为 AST 先报错，这条路径今天到不了 |
| 保留记号 `?..` | 表达式 | 词法 `QuestionDotDot`；`Builder::visitCompExpr` 的独立分支 | 报 `ConstructNotSupported`，并在 AST 上额外建一个 `NotNullThen` 结点 |

要点：**`ParserImpl::parseComp` 是一个「零个或多个 `..`/`?..` 的列表」层次**（注释原文：`compExpr : annoExpr (('..' | '?..') annoExpr)*`），不是两层。任务书要求的「两边是函数时是组合，是数时是区间」因此是**同一个语法结点上的语义分派**，不需要新语法层次——但今天连"是数"这条分支的诊断文本都没有（见摘要第 2 条）。

`..` 还出现在另外两处**与流无关**的地方，改语法时不能误伤：`src/parse/syntax/parser_expr.cpp#ParserImpl` 的 rest/spread 读法（`...xs` 与 `..xs` 在数组/结构体字面量与形参里）、以及 `src/parse/syntax/parser.cpp` 的 `..` 分支。

### 1.3 `?..` 的保留记号与其诊断（任务书要求删除的那一份）

已经有用例与计划条目固定了它的现状：

- 用例 `test/cases/compile/not_supported/optional_comp.cml`（第 11 行 `let g = inc?..dbl`）；
- 计划条目 `test/plans/feat/compile/not_supported.plan.toml` 的 `not_supported.optional_comp`，`expect_diagnostic_name = "SemanticDiag::ConstructNotSupported"`、`expect_diagnostic_code = "0x0300001F"`、第 11 行第 16 列，`desc` 原文「`?..` is rejected at the operator; it used to be a warning and compiled as `..`.」；
- 诊断目录 `docs/diagnostics.md` 里 `0x0300001F` 的一行。

**所以「删除保留记号 `?..` 及其诊断」不是删一句话，是删一条有测试与目录登记的语言表面**：词法记号、`parseComp` 的一个分支、`visitCompExpr` 的一个分支、一个用例、一条计划条目，以及 `camel-format` 的对应用法（`tools/format/fmt.cpp#Formatter` 的带括号/不带括号处理）。`ConstructNotSupported` 这个**诊断码本身要留**——还有 6 条别的用例在用它（`is_operator.cml`、`non_null_assert.cml`、`spread_array.cml`、`spread_struct.cml`、`of_type.cml`、`wait_ignored.cml`、`strict_equal.cml`）。

### 1.4 没有 `yield`

- 词法表 `include/camel/parse/syntax/token_kinds.def` 里 `yield` **不是关键字**（`Kw*` 一列共 43 个，没有它）。
- 实测：`let yield = 1` 编译通过并打印 `1`（附录 A 的 t9）。它是普通标识符。
- 库里也没有 `unfold`（实测 0 处声明）。
- 打印侧有一个同名物但无关：`src/parse/ast/stmt.cpp` 的 `ExitStmt` 打印成 `"yield"`；`src/passes/trans/cpp/cpp_types.cpp` 的保留字表里有 `co_yield`（C++ 关键字表）。

### 1.5 没有流类型

- `include/camel/core/type/base.h#TypeCode` 共 16 个码（`Int32`…`Float16`、`Other`），**没有流**；0x00C（`any`）与 0x00D（联合）已退役，注释里写明「每一个值在编译之后都有确定的类型」。
- 最接近的邻居是**开秩数组**：`include/camel/core/type/composite/array.h#ArrayType` 的文档写明 `T[..]` / `T[..rest]` 是「开秩：`extents()` 是一个前缀，任何不短于它的秩都接受」。实测 `let xs: f32[..] = [1.0, 2.0]` 编译并打印 `[1.0, 2.0]`（附录 A 的 t14）。**设计原则 5.4 也正是拿 `T[..]` 来类比的**（「与 `T[..]`（后面还有不定数量的维度）同读」）——所以 `T..` 必须与 `T[..]` 在词法与类型上共存，这是本任务第一个要问作者的点（问题 1）。
- 类型位置的 `T..` 今天**语法错误**：`ParserImpl::parseArrayDim` 只在 `[` 之后认 `..`，`T` 之后直接跟 `..` 无人接收。实测两条：`let s: int.. = ...` → 「Unexpected '..'; expected '='.」；`xs as int..` → 「Unexpected '..'; expected '}'.」。
- **`T?..` 的拆分今天没有先例**：`T?` 是可空（`T?` 与 `T!` 是既有的可选/可错类型，设计原则 5.3 的 `match` 穷尽性把 `T?`、`T!` 与 `bool` 并列），`..` 是流后缀，两个后缀叠在一起时「先 `?` 后 `..`」还是「先 `..` 后 `?`」在今天的类型解析器里无从判断。
- `as T..` 的两种读法同样没有先例：`as` 的右式今天是一个**类型**（`ParserImpl` 的 `KwAs` 分支），而任务书要的是「`as T..` 后紧跟值时按流后缀读，写区间加括号」——即 `as` 的右式要有**由后续记号决定的两种读法**。

### 1.6 `map`/`filter`/`fold` 今天在哪

| 名字 | 今天在哪 | 形态 |
|---|---|---|
| `map` | `stdlib/prelude.cml#map`；`src/builtin/modules/globals.cpp#map_arr`（`:mark/map_arr`） | `with <f: (item: T) => U> (collect: T[]) => U[]`；**只接受 `TypeCode::Array`**（resolver 里 `norm[0]->code() != TypeCode::Array` 直接 `return nullopt`） |
| `filter` | `stdlib/prelude.cml#filter`；`globals.cpp` 的 `:mark/filter_arr` | 同上，退出类型必须是 `bool` |
| `reduce` | `stdlib/prelude.cml#reduce`（`:mark/reduce_arr`） | 数组的左折叠；**不是流折叠** |
| `foreach` | `stdlib/prelude.cml#foreach` | 数组的按序副作用 |
| `fold` | `stdlib/std.cml#fold`、`stdlib/opt.cml#fold` | **两个遍**，与折叠无关 |
| `zip` | `stdlib/prelude.cml#zip`（`:struct/zip`） | 数组的元组化；要求各参数都是数组 |
| `take` | `stdlib/array.cml#take`（数组）；`stdlib/signatures/tensor.cml#take`（张量按下标取行） | **没有流版** |
| `sum`/`max` | `stdlib/array.cml#sum`、`stdlib/array.cml#max` | 数组归约 |
| `range` | `stdlib/prelude.cml#range` | `range(start, end): int[]`——今天「区间」的等价写法 |
| `scan`/`batch`/`unfold`/`tee`/`window` | **无**（实测 0 处声明） | 全部是新东西 |

数组高阶算子有一个可复用的**形状**：`src/builtin/executors/builtin_semantics.cpp` 的 `OperatorSemantics::elementCalls`（`ElementShape::Fold`、`independent`）是「调度器有更快跑法」的那一项，流版的按需/逐元素执行应当在这个形状上加一套，而不是新造机制。`map`/`filter` 今日的语义**本来就声明成「元素之间没有顺序、按位置给结果」**（`stdlib/prelude.cml` 的 `// ---- higher-order functions ----` 段：*"`map` and `filter` apply f to the elements with no order among the calls"*）——这与流要的「每条边上元素保持先后」**不是同一条**，是缺口 1.6-c。

### 1.7 驻留执行今天能表达什么

- **推断**：`modules/tiles/src/residency.cpp#residencyReport`（`docs/design/gpu/plan.cn.md` 3.2）——每个值的块由产生它的节点决定；跨边界的值**一个方向只搬一次**（不按消费者数搬）；调用的参数跟随实参的块（同类之间不搬）；**循环的状态留在原地**（只有真正换类的值出现在一轮的搬运里）。
- **打印**：`modules/tiles/src/region_pass.cpp#RegionPass` 把放置报告与驻留报告一起印出来，`TILES_RESIDENCY` 是**一行一个 JSON 对象**（`{"graph","body","blocks","uploads","downloads","crossings","per_round","calls"}`）。没有放置事实的程序输出不变。
- **明确不做的事**：`modules/tiles/src/region_pass.cpp#RegionPass` 的注释原文 *"It reads the graph; it inserts no node yet."*。
- 所以任务书第 4 条「涉及流（消费或产生）的图可以驻留执行」要的可复用资产是**推断的形状**（owner 表、跨边界一次、循环状态留在原地、`per_round` 计数）与**报告的既有格式**（`TILES_*` 一行 JSON），不是执行器本身。
- **流的驻留与 GPU 的驻留是同一件事的两个侧面**：`docs/design-principles.cn.md` 5.4 的「图驻留、元素像流水线一样流过」与 GPU 的「段的 exec 节点返回 pending 设备值」共用「整图驻留 + 每条边一个有界缓冲」这层。这也是任务书把本计划排在 GPU 与 JAX 对齐之后的原因（第 4 节）。

### 1.8 规则引擎能不能表达 `map` 融合

`map(map(s, f), g) => map(s, f..g)` 这个验收要三道各自独立的门同时打开，今天**一道也没开**：

1. **左式的项**：`src/runtime/rules.cpp#SideReader::term` 的 `case GCNodeKind::Oper` 第一句就是
   *「an operator with a context argument or an order dependency」* 的拒绝——带 `with` 入边或控制入边的算子**根本不能作为模式项**。而 `map` 的 `f` 就是 `with` 入边：实测 `std::gir` 两个 map 节点各有一条 `type="WITH"` 的入边（源是 `Function(graph=dbl, ...)` 的 `DATA`），`std::jgir` 里它们是 `{"kind":"context", ...}` 的边。**这条拒绝是流与规则语言、图的边重构的交界**：图的边重构把控制边拆成同步边与顺序边、`With`/`Norm` 合为「一种数据边 + 端口角色」，那条拒绝的措辞与判据都要跟着改。
2. **规则的形参类型**：`rule` 的形参表今天写不出函数类型。实测 `export rule h248_map_comp(xs: int[], f: (int) => int, g: (int) => int)` 在 `)` 之前报 `SyntaxDiag::MissingComma`（第 8 行第 47 列）。泛型规则（`rule add_zero::<T>(x: T) when is_integer::<T>()`）会改的是同一个形参表。
3. **右式的函数值**：右边要写 `f..g`，即一个**由参数组合出来的函数值**。`SideReader::term` 的默认分支拒绝「不是算子、常量或参数」的项；函数的组合在图上要么是一个闭包（`Function` 常量 + `FILL`），要么是一个新算子。今天两者都不在允许清单里。

另有一条**已经定死的拼写**：规则体里的高阶算子必须写成尖括号形式。实测把 `map(xs, inc)` 写进规则得到 `SemanticDiag::NoMatchingFunction`，消息原文给了修法：「`'map' takes 'inc' in angle brackets, not in the parentheses: write map<inc>(xs) or xs->map<inc>`」。所以融合规则的左式是 `map<f>(map<g>(xs))` 的形状，计划与用例都按这个拼写写。

**今天有没有「先例」可抄**：`stdlib/rules.cml` 的 `rules::fusion` 段有一条真的融合规则 `tensor_sigmoid_chain`（`1.0/(1.0+exp(-x)) => sigmoid(x)`），它的注释把「不能写什么」列了一遍（第 690 行起）：右式只能写算子、常量与参数（引 `src/runtime/rules.cpp#SideReader::term`）；`exp` 后接 `sum` 需要先存在 `tensor:logsumexp` 算子；融合**区域**（属性/区域写入）规则形式没有。**同一段注释把本计划的第 3 道门也点出来了**——这正是可复用的话术与先例。

### 1.9 `for x in s` 今天

- 判据在 `src/compile/gir/builder_control.cpp#LoopSequenceNotIterable`：类型不是数组时，去 `src/builtin/modules/globals.cpp#__iter__` 这个算子组里按类型求一个重载；求不到就报错。消息把三类列全：数组、字符串（码点，每个是一个字符的字符串）、映射（按键序的 `(key, value)`），并主动提示区间写法 `a..b`。**流不在其中。**
- 扩展点是现成的：`__iter__` 的注释原文 *"A map adds its own overload (collection.cpp), and the tensor module adds one for tensors."*——流只要再加一个重载。
- 循环本身是**区域形式**（`include/camel/runtime/loop.h#LoopView`：条件与体是同一张图的两个控制区域，共帧）：`Loop` 的 Norm 输入是 `xs`，体区域第一个形参是元素。所以「遍历流」在 IR 层不是新结点，是 `__iter__` 的一个新重载 + 「流结束后循环结束」的语义。
- 收集槽（`collectionCount`）已经有模型与文档，但**没有源码形式**（`include/camel/runtime/loop.h#LoopView` 的注释：*"no source syntax exists for this yet; the design"*）。任务书 3 说 `yield` 是「现有循环收集槽的惰性版本」——**这是 `yield` 状态机最接近的既有物**，但它是图生产者才能给的，源码写不出来。

### 1.10 一处必须先记下来的文档冲突（不是代码缺陷）

`docs/design/io-concurrency-streams/README.cn.md` 第 6 节第 5 条今天仍写着：

- 「**`Stream<T>` 是标准库类型**（不是核心类型）：按需拉取、**拉取是这个流自己的资源上的副作用**；`for` 可以遍历它；`map`、`filter` 作用于流时惰性。」

而**同稿第 3.1 节**、**设计原则 5.4** 与任务书第 1 条都已经改成：写得是 `T..`、`Stream<T>` 的写法不保留、流是**值**、*"取代原来『`Stream<T>` 是句柄类型、`next(s)` 写 `uses s`』的写法：那会让纯的变换链也带上效应，挡住融合、并行与驻留执行"*。第 6 节没有跟着改。

另外一处**排版缺陷**：该稿的 `### 3.5 生成器的执行（决策者 2026-10-24）` 排在 `## 7 定稿时补上的闭环` **之后**，即第 3 节的这一小节跑到文件末尾去了；按顺序读会漏掉它，而它正是任务书第 4 条「生成器内部的预取条件」的出处。

**处理方式**：按设计原则的既有规则（它优先，冲突记为缺陷或待办），本计划以 5.4 与 3.1 为准，把这一条写进问题 1 请作者裁定并同时改稿。**在裁决之前不动该稿一个字。**

---

## 2. 缺口清单

「层」一列是缺口的落点；「代价」是**文件数的量级、构建次数、能不能并行**，不是工时。

| # | 今天是什么 | 裁定要求什么 | 缺哪一层 | 代价 |
|---|---|---|---|---|
| G1 | `T..` 在类型位置语法错误（`ParserImpl::parseArrayDim` 只在 `[` 里认 `..`） | `T..` 是类型的后缀 | 词法/语法 + 类型 | 3–5 文件；1 次构建；与 G2 可并行 |
| G2 | `as T..` 无两种读法；`as` 右式只能是类型 | `as T..` 后紧跟值时按流后缀读，写区间加括号 | 语法 + 类型 | 2–3 文件；1 次构建；与 G1 同一片 |
| G3 | `T?..` 的拆分无先例 | 类型位置拆成 `?` 与 `..` | 语法 + 类型 | 含在 G1/G2 片内 |
| G4 | `TypeCode` 没有流；`ArrayType::openRank` 是邻居 | 流是有类型码（或等价的核心 composite）的类型，值语义 | 类型 + IR + 各遍 | 6–10 文件（类型、`gir`/`jgir` 打印、`std::cml` 往返、`std::cpp`、调试器）；2–3 次构建；与 G1 串行（同一处类型解析） |
| G5 | 高阶算子只接受数组，且**声明成元素间无顺序** | `map`/`filter`/`scan`/`fold`/`batch`/`zip` 对**流**是**纯的**、惰性、元素级按需 | 运行期/库 + 类型（重载解析） | 5–8 文件 + `stdlib`；2 次构建；与 G6/G7 可并行 |
| G6 | 来源只有 `fs::read_text`/`read_chunk`/`read_bytes`（无 `read_lines`），无网络/模型流来源 | 来源与去处是**效应**；来源上的读取按顺序且在**被拉取时**发生 | 库（`fs`/`net`/`llm`）+ 效应系统 | 3–6 文件 + `stdlib`；1–2 次构建；与 G5 可并行 |
| G7 | `yield` 是普通标识符；词法无此关键字 | `yield` 生成器，**头部必须写流类型**，编译成状态机，区域里的循环与分支都可以 `yield`；`unfold` 作库函数 | 词法 + 语法 + 建图（控制区域 → 状态机）+ 库 | 8–12 文件（建图器、区域、状态机、`stdlib/unfold`）；3 次构建；**与 G5 可并行，但建图器与 G5 的路由有重叠，需在一开始约定接口** |
| G8 | `for x in s` 只认数组/字符串/映射/张量 | `for x in s` 遍历流 | 库（`__iter__` 重载）+ 建图 + 诊断文本 | 3–4 文件；1 次构建；依赖 G4 |
| G9 | 流值没有线性约束；`tee` 不存在 | 每条执行路径至多一个消费者，**图上检查**，第二次使用报错并给出两处使用与 `tee` 的写法 | 遍（图上检查）+ 类型 + 诊断 | 4–6 文件；2 次构建；依赖 G4；与 G10 可并行 |
| G10 | 无 `tee` 原语；无缓冲峰值报告 | `tee(s, n): (T.., T..)`，缓冲语义上不限、`n` 是提示、**不报错**；剖析报告峰值 | 库 + 运行期（剖析）+ 报告 | 3–5 文件；1–2 次构建；与 G9 可并行 |
| G11 | 只有一种执行方式（线性/字节码/并行），没有「逐元素 vs 驻留」的对照 | 同一程序两种调度**结果逐字相同** | 运行期/调度器 | 6–10 文件；3 次构建；**等 GPU 与 JAX 对齐**；依赖 G5/G7/G8 |
| G12 | `take(s, n)` 没有流版；无限流不存在 | 无限流上 `take(s, n)` **只计算 n 个元素** | 库 + 调度器 + 可观测性（计数） | 2–4 文件；1–2 次构建；依赖 G5/G11 |
| G13 | 规则项拒绝带 `with` 参数的算子；规则形参写不出函数类型 | `map(map(s,f),g)` 融合成 `map(s,f..g)` | 规则引擎（`modules/rewrite` + `src/runtime/rules.cpp`）+ 规则形参语法 | 5–8 文件 + `stdlib/rules.cml`；2–3 次构建；**等规则语言与图的边重构**；依赖 G5 |
| G14 | `f..g` 报 `ConstructNotSupported`；落图的 `:macro/cmp` 是空实现 | 先 `f` 后 `g`；两边是函数时是组合 | 语法（已有）+ AST + 建图 + 类型 | 3–5 文件；1–2 次构建；**独立，可立即开工** |
| G15 | `1..5` 拿到的是「function composition」诊断；区间靠 `range(a,b)` | 两边是数时是区间 | AST + 建图 + 类型 | 含在 G14 片内；**语义待作者裁定**（问题 3） |
| G16 | `?..` 保留记号 + 诊断 + 用例 + 目录登记 | **删除**保留记号 `?..` 及其诊断 | 词法 + 语法 + AST + 用例 + 计划条目 + 格式化器 | 5–7 文件；1 次构建；**独立，可立即开工** |

**缺口之间的真依赖**（其余都是可并行）：

```
G16 ─┐
G14 ─┴─ (第 1 组，独立)
G1/G2/G3 ── G4 ──┬── G5 ──┬── G9 ── G10
                 │        ├── G12
                 ├── G6   └── G13（还等规则语言、图的边重构）
                 ├── G7 ── G8
                 └── G8
G5/G7/G8 + GPU/JAX ── G11 ── G12
```

---

## 3. 切片与顺序

每组是一片**可独立提交、可独立跑用例**的增量；「红前用例」是**用户今天就能写、今天失败**的具体程序，逐字输出在附录 A。「验收」给的是**逐字命令与期望输出**（`$CAMEL_HOME` 指 `out/latest`，`PATH` 已含 `$CAMEL_HOME\bin`）。

### 第 1 组（互不依赖，可同时开工）

#### 片 A：`f..g` 组合与 `?..` 的删除

- **A1（`?..` 删除）**：`token_kinds.def` 的词法记号、`ParserImpl::parseComp` 的分支、`Builder::visitCompExpr` 的 `?..` 分支、用例 `test/cases/compile/not_supported/optional_comp.cml`、计划条目 `not_supported.optional_comp`、`tools/format/fmt.cpp#Formatter` 的对应处理。`ConstructNotSupported` **保留**（还有 7 条用例用它）。
  - 红前用例（附录 A 的 t10，**反例**：今天编译失败，这正是本片要改掉的现状）
    <!-- camel-snippet: counter-example: 反例：今天的 `?..` 报 ConstructNotSupported，逐字诊断见附录 A 的 t10 -->
    ```camel
    func inc(x: int): int { return x + 1 }
    func dbl(x: int): int { return x * 2 }
    func main(): int sync {
        let g = inc?..dbl
        println('never printed')
        println(g(3))
        return 0
    }
    ```
    今天：`...:11:16: [Error]: '?..' is not supported: the operator is not implemented. ... (name=SemanticDiag::ConstructNotSupported, code=0x0300001F)`，`RUN_EXIT=1`。
  - 验收：
    ```powershell
    camel test\cases\compile\not_supported\optional_comp.cml
    # 期望：不再是 ConstructNotSupported，而是 SyntaxDiag::UnexpectedToken（'?..' 不再是记号）
    git grep -n "QuestionDotDot"      # 期望：0 命中
    node scripts/diagnostics-catalog.mjs --check   # 期望：0 问题（0x0300001F 仍被其他用例使用）
    ```
- **A2（`f..g` 组合）**：`Builder::visitCompExpr` 的 `..` 分支改成建组合（不再是错误），落图不再是 `:macro/cmp`（其实现是 `return NullSlot`），类型上两边都是函数时结果是函数。
  - 红前用例（附录 A 的 t4，**反例**：今天编译失败）
    <!-- camel-snippet: counter-example: 反例：今天的 `f..g` 报 ConstructNotSupported，逐字诊断见附录 A 的 t4 -->
    ```camel
    func inc(x: int): int { return x + 1 }
    func dbl(x: int): int { return x * 2 }
    func main(): int sync {
        let g = inc..dbl
        println(g(3))
        return 0
    }
    ```
    今天：`...:11:16: [Error]: '..' is not supported: function composition is not implemented; write a lambda, e.g. '(x: int): int => g(f(x))'. ... (code=0x0300001F)`，`RUN_EXIT=1`。
  - 验收：
    ```powershell
    camel cases\...\t4_comp.cml          # 期望 stdout: 4   （= dbl(inc(3))，不是 inc(dbl(3)) 的 8）
    camel cases\...\t4_comp.cml std::gir # 期望：组合是函数类型的一个值，不再有 :macro/cmp
    ```
    另加：`xs->map<inc..dbl>` 与 `xs->map<inc>->map<dbl>` 结果相同（尖括号里放组合）。
- **A3（区间，待裁定）**：`1..5`。**本片先落地 G14 的"函数"分支并保留"数"分支的现状诊断，等问题 3 的裁决**——因为「闭/半开、与 `range(start, end)` 是否同物、是否与切片 `t[a:b]` 冲突」都没有依据，不许自己定。
  - 红前用例（附录 A 的 t13）：`let xs = 1..5` 今天报的仍是「function composition is not implemented」（诊断不看操作数类型）。
  - 验收（裁定为「区间」之后）：`let xs = (1..5)` 打印 `[1, 2, 3, 4]`（或裁定给的端点），且 `1..5` 不带括号时的读法由裁定固定并有用例。
- **独立理由**：不碰流类型、不碰 IR 结点种类、不碰驻留，是本计划里唯一能与其它片完全解耦的一片。

#### 片 B：`T..` 类型与 `as T..` 的两种读法

- **B1**：类型位置的后缀 `T..`（`ParserImpl` 的类型解析层次 + 类型构造）。**先只做类型**，不做值的产生——这样 B 能与 A 并行，且第 2 组的四片都要它。
- **B2**：表达式 `as T..` 的两种读法（后紧跟值 → 流后缀；写区间加括号）。与 A2/A3 在同一层（`as` 的右式），**接口要在开工前与 A 约定**：A 交出「`..` 在表达式里是什么」，B 交出「`..` 在类型里是什么」，`as T..` 是两者的交界。
- **B3**：`T?..` 的拆分；`Stream<T>` 的写法**不保留**（不新增、不解析、诊断指向 `T..`）。
- 红前用例（附录 A 的 t1、t2，**两个反例**：今天都是语法错误）
  <!-- camel-snippet: counter-example: 反例：类型位置的 `T..` 今天报 SyntaxDiag::UnexpectedToken，逐字诊断见附录 A 的 t1 -->
  ```camel
  func main(): int sync {
      let s: int.. = [1, 2, 3]
      println(0)
      return 0
  }
  ```
  <!-- camel-snippet: counter-example: 反例：表达式里的 `as T..` 今天同样报 SyntaxDiag::UnexpectedToken，逐字诊断见附录 A 的 t2 -->
  ```camel
  func main(): int sync {
      let xs = [1, 2, 3]
      let s = xs as int..
      println(0)
      return 0
  }
  ```
  今天：`t1` → `...:8:15: [Error]: Unexpected '..'; expected '='.  (name=SyntaxDiag::UnexpectedToken, code=0x0200000A)`；`t2` → `...:9:22: [Error]: Unexpected '..'; expected '}'.  (code=0x0200000A)`。都是 `RUN_EXIT=1`。
- 验收：
  ```powershell
  camel cases\...\t1_stream_type.cml   # 期望 exit 0
  camel cases\...\t2_as_stream.cml     # 期望 exit 0
  camel cases\...\t1_stream_type.cml std::jgir   # 期望 outputType 写成 `int..`
  # 与既有开秩数组共存（附录 A 的 t14）：今天已通过，改动后必须仍通过
  camel cases\...\t14_open_rank.cml    # 期望 stdout: [1.0, 2.0]
  camel cx.cml std::cml                # std::cml 往返：打印出的源码要能重新编译（2.5）
  ```
- **独立理由**：类型层的改动是第 2 组四片与第 3、4、5 组的共同前提，先做完它就能一次并行铺开。

### 第 2 组（B 之后，四片可并行）

#### 片 C：流上的纯算子

- `map`/`filter`/`scan`/`fold`/`batch`/`zip` 对流的重载。**`map`/`filter`/`scan`/`fold`/`batch`/`zip` 是纯的**（任务书第 2 条）：它们不产生效应，不在同步/顺序边上，图上与 `:mark/map_arr` 同类，走 `src/builtin/executors/builtin_semantics.cpp#OperatorSemantics` 的 `elementCalls` 形状。
- **必须同时解决的语义分叉**：数组版 `map`/`filter` 今天声明为「元素之间无顺序、按位置给结果」（`stdlib/prelude.cml` 的段注释）；流版要的是「每条边上元素保持先后」。这是**两条不同的声明**，不能靠同一个 `elementCalls` 混过去。
- 红前：今天写不出任何流程序，所以「红前用例」是 B 的 t1 —— C 的验收必须在 B 之后才有意义。
- 验收：`map`/`filter` 链在没有消费者时**不执行**（用一个会打印的 `f` 验证：只建链不拉取，stdout 为空）。

#### 片 D：来源与去处（效应）

- 来源：文件按行/按块（`stdlib/fs.cml` 今天只有 `read_text`/`read_chunk`/`read_bytes`，加流的按行/按块读）、网络、模型的流式输出、定时器；去处：写出。
- **来源的读取只在被拉取时发生**（3.5 的预取条件）；来源是资源，同一来源上的读取仍按顺序。
- 红前：无流，无法表达。
- 验收：`fs::read_lines` 一个**不存在的路径**在**不拉取**时**不报错**（效应没有发生），拉取时才报（这是「用到才执行」细化到元素的直接观测）。

#### 片 E：`yield` 生成器 → 状态机

- 词法加 `yield`；头部**必须写流类型**；**区域里的循环、分支都可以 `yield`**；编译成状态机；`unfold(初始状态, 步进函数)` 作库函数（`stdlib/unfold`）。
- **语义**（`reviews/questions-2026-10-23.md` 的 `h248-generator-and-composition`）：一轮里可以 `yield` 多次，**恢复点就是 `yield` 所在的位置**；任意层嵌套；`match` 的臂里可以；`sync` 块里也可以——`yield` 之后的语句只在消费者再拉时执行，块里效应的先后照 3.4；同一资源上按元素先后；**故障发生在产生那个元素的那次拉取处**（故障不是值），之前已交出的元素不受影响；产出的流线性，生成器函数可以多次调用、每次得到一个流；不做委托（写 `for x in inner do yield x`）；`return` 结束流、不带值；空流合法。
- **可错元素**：元素类型可错（`T!..`）时，`yield error(...)` 交出的就是这个错误元素，后面接 `return` 结束；没有"交出错误并结束"的单独语句形态。
- **状态机的形状**（路线二：流的值是步进函数加状态，`reviews/questions-2026-10-24-evening.md` 的「流有值」与 `2026-10-26-what-the-generator-state-machine-still-needs` 两节）：
  - 生成器 `func g(args): T.. { body }` 被调用时**只求实参**，函数体一句都不执行，返回的是「步进函数 + 初始状态」这个普通函数值；没有新的运行期表示、没有新的节点种类，四个后端一行不改。
  - `step(s): (T, S)?`：从 `s` 记录的恢复点接着跑，跑到下一个 `yield e` 时交出 `(e, s')`，`return` 或跑完时交出 `null`。
  - 状态 `S` 是一个结构体：恢复点编号 `pc`；**所有活过某个 `yield` 的局部变量**（含实参、`var`）；每个包含 `yield` 的循环的状态（循环状态、`for` 的序列与当前位置）。只活在两个 `yield` 之间的局部变量不进状态。
  - **含 `yield` 的循环**在步进函数里拆成"状态 + 分支"：循环头、循环体、回边各是一段，循环状态放在 `S` 里；不含 `yield` 的循环照旧是一个普通的 `LOOP`，整段放在某一段里。
  - 落地上只用 `BRCH`、`LOOP`、结构体与既有节点：机器是「一个 `LOOP`（条件是 `pc` 还没结束）里一串按 `pc` 的 `BRCH`」，每段末尾把新状态交给下一段；语言没有 `break`（3.10），所以结束是**条件**而不是跳转。写状态要注意：`return` 写在 `if`/`match` 的臂里只结束那个臂（`ExitInUnusedBranch`），所以每段的出口是**表达式的值**，`return` 只在步进函数的最外层。
  - 局部变量按"活过 `yield`"进 `S`：每段入口从状态里取出来（`let`/`var`），出口把新值写回状态。**没写先读的字段要有值**：这个语言没有"任意类型的默认值"，所以这一步用底类型 `never` 与核心内建 `fail<'Kind'>(message): never` 写（`m[k] ?? fail(...)`），两者落地之前不要把半个机器合入。
  - `for` 位置把生产者融进消费者的 `loop` 是**优化**：生产者与消费者同时可见（同一个函数里，或内联之后）时省掉闭包；看不见时走步进函数值。两条路的结果必须相同，差分用例两种都覆盖。
  - 步进函数体就是函数体：生成器头上写的 `sync` 就是步进函数体的 `sync`，按每次拉取生效；步进函数的资源集就是这个流的资源集。落地时删掉 `FeatureNotSupported("generators")` 与 `stream.yield_lowering_refused`，`test/cases/compile/stream/yield_lowering.cml` 转成正例（`count(3)` 产出 0、1、2）。除 `count(3)` 与 `pairs(n)`（双层循环）外，验收还要覆盖一轮多次 `yield`、`match` 臂里的 `yield`、`sync` 体里的打印顺序、拉到一半就停时后面的效应不发生、空流、可错元素的 `yield error(...)` 加 `return`；每个用例配一份"直接把元素写成数组"的等价程序，收集成数组后逐元素比较，四个后端都跑。
- **预取条件**（3.5）：生成器内部，元素之间只经状态相连；不依赖上一个元素的那部分可以提前算后面的元素，条件是「**纯且不会出故障**，并放在**有界缓冲**里」；**来源上的读取不预取**。
- 红前用例（附录 A 的 t3、t9）：
  ```camel
  // t3：头部写流类型 + yield
  func pairs(n: int): (int, int).. {
      var i = 0
      loop while i < n do
          yield (i, i + 1)
  }
  ```
  今天：**错误不再停在 `..` 上**——`...:7:31: Unexpected '..'` 那条读数已经过期：`yield` 已经是被读到的
  语句，错误停在它上面。`cd44250a5`（语法收口之前）逐字是 `t3_pairs.cml:8:9: [Error]: Unresolved reference:
  'yield'.`（`SemanticDiag::UnresolvedReference`，`0x0300000B`）；语法收口之后、片 E 落地之前，同一个位置上是
  `FeatureNotSupported: 'generators'`（`0x03000040`，`test/plans/feat/compile/stream.plan.toml` 的
  `stream.yield_lowering_refused`）。片 E 要做的正是把这个按名字的拒绝换成状态机。
  <!-- camel-snippet: counter-example: 语法收口之后 `yield` 是关键字，这条程序是片 E 的负向验收：它必须是语法错误 -->
  ```camel
  // t9：反例——`yield` 成为关键字之后，这条程序必须是语法错误
  func main(): int sync {
      let yield = 1
      println(yield)
      return 0
  }
  ```
  今天：`...:4:9: [Error]: 'yield' is a keyword and cannot be used as a name. Choose another name, for example 'yield_'.  (name=SyntaxDiag::KeywordAsIdentifier, code=0x02000006)`，`RUN_EXIT=1`——关键字落地之前它是 `RUN_EXIT=0`、stdout `1`。
- 验收：`loop n = 0 for p in pairs(3) do n + 1` 恰得 3 个元素——`for` 不是 Camel 的语句形式（探针逐字
  `Unexpected 'for'`），能写出来的消费者走法是 `loop … for x in <流> do …`（片 F）；`yield` 成为关键字之后 t9 **必须**变成语法错误（这是「加了关键字」的负向验收）；生成器里的 `loop` 与 `if` 各 `yield` 一次的用例。

#### 片 F：`for x in s`

- **落地方式已改**：流的消费不走 `__iter__` 的新重载，而在图上展开成已有的 `while`——`for x in s` 的每一轮是一次拉取，循环的状态是流加上程序写的状态，每一轮**先判断条件、再取元素**（和数组同一个顺序）：`c` 成立才调用步进函数，拉到空就结束，所以 `for line in read_lines(f) while n < 10` 不会读第 11 行。这条与下一段「`__iter__` 加一个流的重载」是同一个功能的另一条路，已被取代；`LoopSequenceNotIterable` 的消息确实把「流」补进了那几类（见 `diagnostics/registry.tsv`）。
- `src/builtin/modules/globals.cpp#__iter__` 加一个流的重载；`LoopSequenceNotIterable` 的消息文本要把「流」补进那四类里（今天的原文只列数组、字符串、映射、张量）。
- 红前用例（附录 A 的 t5，**反例**：今天编译失败）
  <!-- camel-snippet: counter-example: 反例：`for x in <int>` 今天报 LoopSequenceNotIterable，逐字诊断见附录 A 的 t5 -->
  ```camel
  func main(): int sync {
      let n = 3
      let a = loop acc = 0 for x in n do acc + x
      println(a)
      return 0
  }
  ```
今天：`...:9:35: [Error]: 'for' takes the elements of an array, a string, a map, a tensor or a stream; 'n' has type 'int'. ... (name=SemanticDiag::LoopSequenceNotIterable, code=0x03000120)`，`RUN_EXIT=1`。
- 验收：`for x in s` 遍历流；**流结束时循环结束**；无限流上的 `for` 靠 `while` 条件停（`docs/design/io-concurrency-streams/README.cn.md` 3.2）；诊断文本新增「流」并有用例固定。

### 第 3 组（等 B、C）

#### 片 G：线性检查与 `tee`

- **G9 线性检查**：流值每条**执行路径**至多一个消费者（**分支的不同臂各消费一次算一次**）；**在图上检查**；第二次使用是编译错误，**诊断给出两处使用与 `tee` 的写法**。来源上的读取只发生一次。
- **G10 `tee`**：**原语不是关键字**，签名 `(s: T.., n: int = …): (T.., T..)`；**共享缓冲语义上不限**，`n` 只是执行器的提示，**不报错**（执行器可以阻塞领先的一方或扩大缓冲）；**剖析报告 `tee` 缓冲的峰值**。报告的格式建议照既有先例 `TILES_RESIDENCY` 的一行 JSON（`modules/tiles/src/residency.cpp#residencyReport`），但**这一条要作者定**（问题 2）。
- 红前用例（附录 A 的 t6，**反例**：今天编译失败）
  <!-- camel-snippet: counter-example: 反例：今天的 `tee` 报 UnresolvedReference，逐字诊断见附录 A 的 t6 -->
  ```camel
  func main(): int sync {
      let s = [1, 2, 3]
      let (a, b) = tee(s, 8)
      println(a)
      return 0
  }
  ```
  今天：`...:9:18: [Error]: Unresolved reference: 'tee'. Make sure the name is declared and in scope. (name=SemanticDiag::UnresolvedReference, code=0x0300000B)`，`RUN_EXIT=1`。
- 验收（**任务书加的**）：
  ```camel
  let (a, b) = tee(s, 8); (sum(a), max(b))
  ```
  在**逐元素**与**驻留**两种调度下结果相同、**都不报错**。
  另加：同一流值消费两次（不写 `tee`）必须是编译错误，诊断里同时出现**两处使用**与 `tee` 的写法；`tee` 的结果两个流各自再 `tee` 一次的行为由问题 2 裁定。

### 第 4 组（等 C/E/F 与 GPU/JAX）

#### 片 H：两种调度与无限流上的 `take`

- **G11**：涉及流（消费或产生）的图可以**驻留执行**；同一程序逐元素整图执行与驻留流水线执行**结果逐字相同**，条件是每条边上元素保持先后、节点只按到达的输入计算（设计原则 5.4；Kahn 进程网络的确定性条件）。**生成器本身就是驻留的**（3.5）：每拉一次从上次停下的地方继续一轮，状态一直保留，图在整个流的生命期里驻留——所以「涉及流」的判据**包括产生流的图**。
- **不确定性的写法**：`merge`（按到达顺序）与按时间的窗口**在声明里写明不确定性**（任务书第 2 条）；这使得「两种调度结果逐字相同」的**比较范围**要先被划定（问题 2）。
- **G12**：无限流上 `take(s, n)` **只计算 n 个元素**。
- 今天可复用的只有 G7 的驻留**推断形状**与 `TILES_RESIDENCY` 报告（1.7）；执行器本身等 GPU 与 JAX 对齐。
- 红前：无流，无法表达；这一片的「红前证据」就是 1.7——距「驻留执行」最近的东西今天**不插一个节点、不驱动一次执行**。
- 验收：
  ```powershell
  camel t_stream.cml std::nvm        # 逐元素
  camel t_stream.cml <resident pass> # 驻留
  # 期望：两行的 stdout 逐字节相同（用同一个程序的两次运行对照，不是两次相似输出）
  ```
  `take(s, n)` 的「只计算 n 个」要有一个**可观测的计数**（一个每次调用都自增的 `with` 参数函数，或有名字的剖析事件），不能只看结果对不对——否则「算了 10^6 个再取前 5 个」也会通过。

### 第 5 组（等 A、C 与规则语言、图的边重构）

#### 片 I：`map` 融合

- `map(map(s, f), g)` 被规则**融合**成 `map(s, f..g)`。
- **三道门**（1.8）：`src/runtime/rules.cpp#SideReader::term` 对带 `with` 参数算子的拒绝；规则形参表要能写函数类型；右式要能写「参数组合出来的函数值」。
- 红前用例（附录 A 的 t8、t12）：
  ```camel
  // t8：规则的形参写不出函数类型
  export rule h248_map_comp(xs: int[], f: (int) => int, g: (int) => int) {
      map(map(xs, f), g) => map(xs, f..g)
  }
  ```
  今天：`...:8:47: [Error]: Expected ',' or ')' before '=>'. ... (name=SyntaxDiag::MissingComma, code=0x02000007)`。
  ```camel
  // t12：`map(xs, inc)` 的拼写本身就被拒
  export rule h248_map_identity(xs: int[]) {
      map(xs, inc) => xs
  }
  ```
  今天：`...:11:5: [Error]: No matching function found ...; 'map' takes 'inc' in angle brackets, not in the parentheses: write map<inc>(xs) or xs->map<inc>. ... (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)`。
  **左式的拼写因此定死为 `map<f>(map<g>(xs))`。**
- 验收：
  ```powershell
  camel t_fuse.cml "rules::fusion" std::gir
  # 期望：链上只剩一个 map，它的 with 参数是 inc..dbl（不是两个 map）
  camel t_fuse.cml std::nvm     # 期望：结果与融合前逐字相同（逐元素、全量）
  ```
- 同时给 `stdlib/rules.cml` 的 `rules::fusion` 段加这条规则（先例：`tensor_sigmoid_chain`，同段注释已经把「不能写什么」列过一遍）。

### 收尾片（验收示例）

任务书第 6 条的四个验收，做成 `test/cases/` 与 `test/plans/` 的正式用例（**不属于本计划这一轮的交付**，是各片的验收汇总）：

1. **数据加载器用 `yield` 写、训练循环 `for batch in loader`**（E + F）。
2. `map(map(s, f), g)` 融合成 `map(s, f..g)`（I）。
3. 同一流程序**逐元素与驻留两种调度结果逐字相同**（H）。
4. 无限流上 `take(s, n)` **只计算 n 个元素**（H）。

---

## 4. 与既有计划的关系

任务书 5.28 的排期是「**GPU 与 JAX 对齐之后、D9 之前**」。逐条说明它为什么是这个位置。

### 4.1 图的边重构（主线，正在另一条车道落地）

本计划与图的边重构的接触面**恰好就是第 1 道门与第 3 组**：

- `src/runtime/rules.cpp#SideReader::term` 今天读的是 `withInputsOf` 与 `ctrlInputsOf`，并在有其中之一时拒绝。图的边重构把「控制边」这一种类删掉，换成**同步边**与**顺序边**，并把 `With`/`Norm` 合成「一种数据边 + 端口角色」（任务书 5.21 第 1、2 条）。所以那句拒绝的**判据与措辞都要跟着改**——流的算子**必须**是纯的（无同步边、无顺序边），而带 `with` 参数这件事本身**不再**是「有上下文参数」的理由。
- 图的边重构第 4 条要求「规则匹配只看数据端口」。这正好解掉第 1 道门的一半：`map` 的 `f` 变成带「上下文」角色的一条**数据边**，而不是被拒绝的 `with` 入边。
- **建议**：片 I 的第 1 道门**在图的边重构落地后重测一次再动**，不要先按今天的 `withInputsOf`/`ctrlInputsOf` 写死在流的算子上——否则边重构一合，判据就错了。
- 片 G 的线性检查也要写在边重构的边模型上（「数据边 + 端口角色」），不是今天的 `LinkType::Ctrl` 上。

### 4.2 JAX 对齐与 GPU 驻留

- 流的「图驻留、元素流水线式流过」与 GPU 的「整图驻留 + 每条边一个有界缓冲」是**同一层执行模型**（`docs/design-principles.cn.md` 5.4 与 `docs/design/gpu/plan.cn.md` 3.2）。任务书把本计划排在 GPU 与 JAX 对齐之后，是因为**驻留执行器只有一份**：先由 GPU 那条车道把它做出来（`TILES_RESIDENCY` 从「读」变成「改写」），本计划的第 4 组才有一个能进的执行器，而不是新写一个第二套。
- 复用的**具体接口**：`modules/tiles/src/residency.cpp#residencyReport` 的 owner 表与跨边界一次计数、`per_round` 的循环状态判据、`TILES_RESIDENCY` 的报告格式。流的驻留应当把**同一张表**用在「元素沿边流动」上。
- **不建议**在本计划里另造一个流专用执行器：那会让「两种调度结果相同」变成两个实现互相对照，而不是同一语义的两种执行方式——而这正是 5.4 要避免的。

### 4.3 对齐 JAX 的程序变换

对齐 JAX 那一项是「遍即函数」，与流的关系是两点：

- **`scan` 与 `while_loop`**：对齐 JAX 那一项把 `scan`/`while_loop`/`cond` 对应到既有的 `LOOP`/`BRCH`。本计划的 `scan`（跨元素状态的折叠，`docs/design-principles.cn.md` 5.4 把「跨元素的状态（`scan`/`fold`）」列为驻留要定的第一件事）与它是**同一个算子**的流版与数组版，**接口要约定**，否则会出现两个 `scan`。
- **`vmap` 与流的元素级语义**：两者都是「把作用于单个东西的函数变成作用于一串东西」，但 `vmap` 按**维度名**对齐、结果是数组；流按**到达顺序**、结果是流。**不合并**，但要在文档里写明它们是两件事，避免以后有人拿 `vmap` 实现流。

### 4.4 规则语言第二版

- 片 I 的第 2、3 道门在规则语言的地界里：**规则形参可写函数类型**与**规则的泛型/条件**是同一个形参表；规则语言的「类型泛型规则」`rule add_zero::<T>(x: T) when is_integer::<T>()` 会改它。**本计划不自己改这个语法**，等规则语言落地后在它上面加函数类型。
- 规则语言的「算子性质与结合交换匹配」（`+`、`*` 的交换/结合来自算子声明）：流的 `map` 融合**不是**结合交换律，是**函数组合的结合律**（`map(map(s,f),g) = map(s,f..g)`），右式有一个真正的新函数值。所以它不能靠「算子的结合性质」白拿，需要规则语言的「右边调用 Camel 函数」与「多根模式」那一档能力（`docs/design/rewrite/v2.cn.md` 第 9 节）。
- **`stdlib/rules.cml` 是同一个文件**：本计划的融合规则与规则语言的重写会在同一个文件里相遇，**先合的那一方定写法**。建议本计划的融合规则在规则语言的规则写法范本落地后写（任务书 5.11 的验收就是「`stdlib/rules.cml` 按新写法重写」）。

### 4.5 D9（官方智能体示例）

任务书说本计划在 **D9 之前**，理由是 D9 要用流：

- `docs/design/showcase/README.cn.md` 的 D9 一行写明它需要「长耗时的网络调用、超时、竞速与取消、**流式接收模型输出**」；
- `docs/design/io-concurrency-streams/README.cn.md` 2.6 说 D9 的整个流程本身就是一张图；
- 所以 D9 依赖的是本计划的**片 D（来源，含模型的流式输出）**与**片 C（变换）**，**不依赖**片 H（驻留）与片 I（规则融合）。这意味着 **本计划可以部分交付就给 D9 让路**：第 1、2 组做完（`T..` + 纯算子 + 来源 + `yield` + `for`）就够 D9 用了，第 3、4、5 组可以跟在后面。

### 4.6 与该稿第 3 节的逐条对应

| `docs/design/io-concurrency-streams/README.cn.md` 第 3 节 | 本计划 | 备注 |
|---|---|---|
| 3.1 流类型（`T..` 是值；来源与去处是效应） | **片 B** + **片 D** | 与同稿第 6 节第 5 条冲突（1.10），等问题 1 |
| 3.2 遍历与变换（`for x in s`；`map`/`filter` 惰性；窗口、按键聚合、合并、批处理；背压） | **片 C** + **片 F** | 窗口与合并的**不确定性声明**在片 H |
| 3.3 编译（流水线融合成一个循环；彼此独立的阶段并行；跨流合并按到达顺序） | **片 C**（融合形状）+ **片 H**（调度）+ **片 I**（规则融合） | |
| 3.4 自定义的流与驻留的流水线（生成器即定义流；驻留执行；语义只有一个） | **片 E** + **片 H** | 要定的四件事里，「跨元素的状态」在片 C，「跨元素的效应顺序」与「缓冲与背压」在片 H，「按时间窗口的不确定性」在片 H 的声明里 |
| 3.5 生成器的执行（生成器本身就是驻留的；预取条件） | **片 E** | **该小节在文件里排在 `## 7` 之后**（1.10 的排版缺陷） |
| 第 6 节第 5 条（`Stream<T>` 是标准库类型；拉取是资源上的副作用） | **与 5.4 冲突，按 5.4** | 问题 1，不自己裁定 |

---

## 5. 作者问题

按项目既有体例单独成文，**不在这里给结论**：

1. 流类型 —— `T..` 的表面：与既有开秩数组 `T[..]` 的共存、`T?..` 的拆分、`as T..` 两种读法的判定、以及**流是核心类型还是标准库类型**（第 6 节第 5 条与 5.4 的冲突）。
2. 线性 —— 判据（循环体、`match` 的多臂、`tee` 的结果是否也线性）、`tee(s, n)` 的 `n` 默认值与非法值、缓冲峰值报告的**格式与阈值**、「两种调度结果逐字相同」的比较范围与 `take` 的计算次数怎么观测。
3. 生成器 —— `yield` 状态机的边界（故障路径、嵌套、委托、生成器里的效应顺序）、生成器值是否也线性、`f..g` 的类型规则（参数个数、`with` 参数、泛型、重载函数值、能否进 `map<>` 的尖括号）、以及区间读法（闭/半开、与 `range(start, end)` 的关系）。

---

## 6. 风险与不做

1. **最大的风险是「两种调度结果逐字相同」的判据见不到底**。按到达顺序合并与按时间开窗口**定义上**就不确定；如果「逐字相同」被理解成字节相等，那么任何含不确定算子的程序都无法验收。片 H 的第一件事必须是**划定比较范围**（哪些算子排除、以什么为单位比较），并且要作者裁定（问题 2）——否则会写出一个永远红的用例。
2. **`T..` 与 `T[..]` 的共存是语法风险，不是类型风险**。`f32[..]` 已经是一个合法类型并会打印成 `[..]`；`T..` 加进来之后，`T[..]..`、`T[..]?..`、`T[3, ..rest]..` 这类叠法必须有明确读法，否则会出现「同一个拼写两种读法」而**没有诊断**。片 B 的第一步是把这些叠法的**读法与诊断**一起定下来，不是先做最简的 `T..`。
3. **不写第二套驻留执行器**（4.2）。流与 GPU 共用「整图驻留 + 每条边一个有界缓冲」，重复实现会让验收退化成两个实现互相印证。
4. **不在片 I 里自己改规则语法**（4.4）。不自己定对带 `with` 参数算子的模式是否合法的判据（图的边重构会改），不自己加规则形参的函数类型（规则语言会改）。
5. **不做的事**：不实现语言表面（本轮只交计划）；不改 `docs/design/**` 已有的任何文件（含 1.10 记下的两处缺陷，只记录、等裁定）；不新增 `Stream<T>` 的写法；不把 `async`/`await` 引进来（该稿第 1 节的核心主张）；不为流新造结点种类（`for` 走 `__iter__` 重载，`yield` 走状态机，复用 `LOOP`/`BRCH`）。
6. **口径限制**：本计划的「现状」全部在 `ad9ed160e` 上实测（14 条探针 + 读源码）。图的边重构、JAX 对齐与规则语言落地后，`src/runtime/rules.cpp#SideReader::term` 的判据、`modules/tiles/src/region_pass.cpp#RegionPass` 的输出、`stdlib/rules.cml` 的规则写法都会变，**片 G/I 的现状条目要重测**才能开工。

---

## 附录 A：探针源码与逐字输出

探针目录 `test/cases/compile/_h248probe/`（**提交前已删除**，在这里存档以便复现）。运行方式见 1.1；`RUN_EXIT=1`（探针故意全部失败以打印输出）。

**t1 `t1_stream_type.cml`** —— `let s: int.. = [1, 2, 3]`：

```
(h248_t1_stream_type)/.../t1_stream_type.cml:8:15: [Error]: Unexpected '..'; expected '='.  (name=SyntaxDiag::UnexpectedToken, code=0x0200000A)
```

**t2 `t2_as_stream.cml`** —— `let s = xs as int..`：

```
(h248_t2_as_stream)/.../t2_as_stream.cml:9:22: [Error]: Unexpected '..'; expected '}'.  (name=SyntaxDiag::UnexpectedToken, code=0x0200000A)
```

**t3 `t3_yield.cml`** —— `func pairs(n: int): (int, int).. { ... yield (i, i + 1) }`：

```
(h248_t3_yield)/.../t3_yield.cml:7:31: [Error]: Unexpected '..'; expected a declaration or the end of the file.  (name=SyntaxDiag::UnexpectedToken, code=0x0200000A)
```

**t4 `t4_comp.cml`** —— `let g = inc..dbl`：

```
(h248_t4_comp)/.../t4_comp.cml:11:16: [Error]: '..' is not supported: function composition is not implemented; write a lambda, e.g. '(x: int): int => g(f(x))'. The grammar accepts this construct, but the compiler does not implement it. Remove it; nothing in the language replaces it yet. (name=SemanticDiag::ConstructNotSupported, code=0x0300001F)
```

**t5 `t5_for_stream.cml`** —— `loop acc = 0 for x in n do acc + x`（`n: int`）：

```
(h248_t5_for_stream)/.../t5_for_stream.cml:9:35: [Error]: 'for' takes the elements of an array, a string, a map, a tensor or a stream; 'n' has type 'int'. A loop's 'for x in xs' visits the elements of xs in order (design principles 3.10): the elements of an array; the code points of a string, each a one-character string; the (key, value) pairs of a map, in key order (11.3); and the sub-tensors of a tensor along its first dimension. 'range(a, b)' is the array of the integers from a to b. (name=SemanticDiag::LoopSequenceNotIterable, code=0x03000120)
```

**t6 `t6_tee.cml`** —— `let (a, b) = tee(s, 8)`：

```
(h248_t6_tee)/.../t6_tee.cml:9:18: [Error]: Unresolved reference: 'tee'. Make sure the name is declared and in scope. (name=SemanticDiag::UnresolvedReference, code=0x0300000B)
```

**t7 `t7_nested_map.cml`** —— `[1, 2, 3]->map<inc>->map<dbl>`，`EXIT=0`：

```
[4, 6, 8]
```

**t8 `t8_rule_comp.cml`** —— `export rule h248_map_comp(xs: int[], f: (int) => int, g: (int) => int) { ... }`：

```
(h248_t8_rule_comp)/.../t8_rule_comp.cml:8:47: [Error]: Expected ',' or ')' before '=>'. The items of a list (parameters, arguments, elements) are separated by ','. (name=SyntaxDiag::MissingComma, code=0x02000007)
```

**t9 `t9_yield_only.cml`** —— `let yield = 1; println(yield)`，`EXIT=0`：

```
1
```

**t10** —— 既有用例 `test/cases/compile/not_supported/optional_comp.cml`：

```
(optional_comp)/.../optional_comp.cml:11:16: [Error]: '?..' is not supported: the operator is not implemented. The grammar accepts this construct, but the compiler does not implement it. Remove it; nothing in the language replaces it yet. (name=SemanticDiag::ConstructNotSupported, code=0x0300001F)
```

**t11 / t11b** —— t7 的图：`std::jgir` 里两个 map 节点的 `f` 是 `context` 边，`std::gir` 里是 `WITH`：

```
{"kind":"context","source":"g1n1","target":"g1n3","targetIndex":0}   // map 的 f（inc）
{"kind":"context","source":"g1n0","target":"g1n4","targetIndex":0}   // map 的 f（dbl）
N06 [label="Function(graph=dbl, tupleSlots=0)", type="DATA", ...]
N06 -> N0A [label="0|0", type="WITH"];
N09 -> N0A [label="0|0", type="NORM"];
```

**t12 `t12_rule_map.cml`** —— `export rule h248_map_identity(xs: int[]) { map(xs, inc) => xs }`：

```
(h248_t12_rule_map)/.../t12_rule_map.cml:11:5: [Error]: No matching function found for argument types: <> (int[], (x: int) => int); 'map' takes 'inc' in angle brackets, not in the parentheses: write map<inc>(xs) or xs->map<inc>. Check the number and types of arguments to ensure they match one of the available overloaded function signatures. Available overloads: 
    <:mark/map_arr>: <f: (item: T) => U> (collect: T[]) => U[]
 (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

**t13 `t13_range.cml`** —— `let xs = 1..5`：

```
(h248_t13_range)/.../t13_range.cml:9:15: [Error]: '..' is not supported: function composition is not implemented; write a lambda, e.g. '(x: int): int => g(f(x))'. The grammar accepts this construct, but the compiler does not implement it. Remove it; nothing in the language replaces it yet. (name=SemanticDiag::ConstructNotSupported, code=0x0300001F)
```

**t14 `t14_open_rank.cml`** —— `let xs: f32[..] = [1.0, 2.0]`，`EXIT=0`（既有开秩数组，改动后必须保持）：

```
[1.0, 2.0]
```

---

## 附录 B：现状证据的位置索引

**行号只在需要时才用**（`node scripts/check-doc-links.mjs --citations` 把带行号的引用判为缺陷，因为行号随每次编辑移动）；下面用「符号」定位，行号是本次测量（`ad9ed160e`）的快照，仅供查证。

| 事实 | 符号 | 本次快照 |
|---|---|---|
| `..` / `?..` 的词法记号 | `include/camel/parse/syntax/token_kinds.def#QuestionDotDot` | 73（`DotDot`）、74（`Ellipsis`）、122（`QuestionDotDot`） |
| 表达式的 `..` 层次 | `src/parse/syntax/parser_expr.cpp#ParserImpl::parseComp` | 360–373 |
| 类型里的 `..` | `src/parse/syntax/parser_type.cpp#ParserImpl::parseArrayDim` | 155–188（`DotDot` 在 169） |
| `..` / `?..` 的 AST → 诊断 | `src/parse/ast/builder.cpp#Builder::visitCompExpr` | 3617–3665（`..` 3625–3635，`?..` 3636–3645） |
| `CompExpr` 落图 | `src/compile/gct/builder.cpp#MacroCmp` | 2944–2949 |
| 宏算子本体 | `src/builtin/operators/macro.cpp#__cmp__` | 12（`return NullSlot`） |
| 算子 id | `include/camel/core/operator_table.h#MacroCmp` | 117（`= 19`，`:macro/cmp`） |
| `TypeCode` 无流 | `include/camel/core/type/base.h#TypeCode` | 56–100 |
| 开秩数组 `T[..]` | `include/camel/core/type/composite/array.h#ArrayType::openRank` | 83–87、111–118 |
| 维度包落图 | `src/compile/gct/builder.cpp#DimPack` | 5095–5120、5775–5805 |
| `map` / `filter` 的类型解析 | `src/builtin/modules/globals.cpp#map_arr` | 2266–2348 |
| 高阶算子的语义形状 | `src/builtin/executors/builtin_semantics.cpp#ElementCalls` | 190、222 |
| 数组版签名 | `stdlib/prelude.cml#map` | 385（`zip`）、411–429（`map`/`filter`/`reduce`）、324（`range`） |
| `__iter__` 扩展点 | `src/builtin/modules/globals.cpp#__iter__` | 1956–1977 |
| `for` 的不可遍历诊断 | `src/compile/gir/builder_control.cpp#LoopSequenceNotIterable` | 2155–2188 |
| 循环区域模型 | `include/camel/runtime/loop.h#LoopView` | 4–40（收集槽 20–28、语义 30–34） |
| 规则项读取器的拒绝 | `src/runtime/rules.cpp#SideReader::term` | 107–148（`with`/控制 133–135、默认分支 146–147） |
| 规则编译错误的措辞 | `src/runtime/rules.cpp#SideReader::fail` | 67–71 |
| 融合规则的先例与「不能写什么」 | `stdlib/rules.cml#array_sigmoid_chain` | 373–374 |
| 驻留推断 | `modules/tiles/src/residency.cpp#residencyReport` | 81–180（`TILES_RESIDENCY` 在 177） |
| 驻留报告的接口 | `modules/tiles/include/tiles/internal/residency.h#residencyReport` | 23–25 |
| 「只读不插节点」 | `modules/tiles/src/region_pass.cpp#RegionPass` | 43–49 |
| 驻留的设计依据 | `docs/design/gpu/plan.cn.md` | `### 3.2 驻留推断`（297） |
| `?..` 的既有用例 | `test/cases/compile/not_supported/optional_comp.cml` | 11 |
| `?..` 的既有计划条目 | `test/plans/feat/compile/not_supported.plan.toml` | `not_supported.optional_comp`（67–80） |
| 诊断目录里的两个码 | `docs/diagnostics.md` | `0x0300001F`（70、1654）、`0x03000120`（114、3019） |
| `fs` 今天有什么 | `stdlib/fs.cml#read_text` | 24、36、41（无 `read_lines`） |
| D9 依赖流 | `docs/design/showcase/README.cn.md` | 41 |