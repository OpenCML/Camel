# 生成器状态机（H248 片 E）：它要什么，今天缺哪一层，两条路各要多少

> 说明（协调者）：提出问题时的代码在 `handover/h601-generators`（基点 `cd44250a5`，develop 已把 `yield` 那半批回退，页里的探针读数就是在那个状态下量的）；作者已裁定"状态机与语法同一批做完再合入"，本页给的是"这一批的真实体量与两条路"。


> 说明（H601 车道）：这不是一条待裁定的语义问题，是一次没做完的实施的交接。语义已由
> `docs/handover/reviews/questions-2026-10-23.md:394` 的 (a)–(i) 定死，本页只记：要什么、今天缺哪一层、
> 两条实现路线的成本与风险。语法半批在 `handover/h579-yield`（不在 develop 上，`cd44250a5` 是它的回退）。

## 是什么

作者要求把 `yield` 的语法与生成器的状态机**同一批**合入，不留"按名字拒绝"
（`questions-2026-10-24-evening.md` §`2026-10-26-yield-keyword-or-context-keyword` 第 2 条）。语法那一半
（关键字、`retStmt` 第三选项、`ExitType::Yield` 的产生者、编辑器文法、诊断目录）已经写好，可整批取用；
**没做的是状态机**：把函数体里的 `yield` 变成一个流值，消费者每拉一次就走一轮、停在那个 `yield`。

把 `yield` 编译成流，今天缺三层，缺一层都拿不到一条能跑的正向读数：

1. **可恢复的执行**。"拉一次、停在 `yield`、下次从那里继续"今天在仓库里没有任何对应物：三个后端都只有
   「跑到结束」的循环驱动（`include/camel/execute/loop_driver.h:179` `driveLoopRounds` 的
   `for (size_t round = 0;; ++round)`），NodeVM 是一次平坦的 plan 走完（`src/passes/sched/nodevm/nodevm.cpp:301`）。
   流类型的处理在**类型层之外零命中**：`src/passes/**`（三个后端）与 `src/execute/**`（循环驱动、宏求值器、
   函数调用）对 `TypeCode::Stream|StreamType` 的命中数都是 **0**。
2. **流的运行期表示**。`include/camel/core/type/composite/stream.h` 自己写着 "A value of this type does not
   exist yet: producing one (a generator, a source) is another slice"；`TypeCode::Stream`（`base.h:104`）
   只有类型、存储种类（`storage.h:33`）与打印（`text.cpp:43/436/571`），没有值。
3. **一个消费者**。片 F（`plan.cn.md:309`）的 `for x in s` 也没有，`__iter__`
   （`src/builtin/modules/globals.cpp:1993`）只认数组、字符串、映射、张量。**计划的 E 验收自己就写着
   `for p in pairs(3)`（`plan.cn.md:307`），所以 E 单独不可观测**：E 与 F 的可观测部分是同一件事，必须同批。

库侧同样为零：`stdlib` 里 `unfold`、`tee`、`scan`、`batch`、`window` 的声明各 **0** 条，而 `unfold` 是片 E
自己点名要的（`plan.cn.md:284`）。

## 逐字证据

- 设计原则 5.4（`docs/design-principles.cn.md:361`）："**生成器**：函数体里写 `yield` 的函数产生一个流，
  它的返回类型必须在函数头部写成流类型（`func pairs(n: int): (int, int).. { … yield (i, j) … }`）；
  简单的生成器可以用库函数 `unfold(初始状态, 步进函数)`"。
- 语义裁定（`questions-2026-10-23.md:394`）："(a) **一轮里可以 `yield` 多次**——恢复点就是 `yield` 所在的
  位置（Python、C# 的生成器都是这样），状态机记"区域栈 + 每层位置 + 停在哪个 `yield`"；收集槽的"一轮一个值"
  是它的特例。(b) 任意层嵌套可以。(c) `match` 的臂可以；`sync` 块里也可以——`yield` 之后的语句只在消费者再
  拉时执行（惰性），块里效应的先后照 3.4。(d) 同一资源上按元素先后。(e) 故障：在产生那个元素的那次拉取处
  故障……(f) 产出的流值线性……(g) 不做委托……(h) `return` 结束流，不带值。(i) 空流合法。"
- 片 E（`plan.cn.md:282-307`）："词法加 `yield`；头部**必须写流类型**；**区域里的循环、分支都可以 `yield`**；
  **编译成状态机**；`unfold(初始状态, 步进函数)` 作库函数"；验收 "`for p in pairs(3)` 恰得 3 个元素；
  `yield` 成为关键字之后 t9 **必须**变成语法错误；生成器里的 `loop` 与 `if` 各 `yield` 一次的用例"。
- 计划的实现约束（`plan.cn.md:480`）："不为流新造结点种类（`for` 走 `__iter__` 重载，`yield` 走状态机，
  复用 `LOOP`/`BRCH`）"。
- 已有的两处落点：`include/camel/parse/ast/stmt.h:31` `enum class ExitType { Yield, Raise, Return, Throw };`
  （AST 一直有，此前无人产生）；`handover/h579-yield:src/compile/gct/builder.cpp:2017-2028` 是那条必须消失的
  拒绝（`SemanticDiag::FeatureNotSupported` 的 `commit("generators")`，`builder.cpp:2026`）。
- 语法半批**不需要改写就能在回退后的 develop 上取用**：在 `cd44250a5` 上 `git cherry-pick -n 99284ba13
  51780dcef a5943253f` 三方干净，20 个文件全部落下（只有 `docs/reference/camel-for-ai.md` 走了一次自动合并，
  无冲突）。所以语法那半批随时可以随状态机一起合入。

## 今天的读数（主机实测，2026-10-26，`origin/develop` = `cd44250a5`）

| 探针（`test/cases/compile/_genprobe/*.cml`，已删） | exit | 逐字输出 |
|---|---|---|
| t3：`func pairs(n: int): (int, int).. { loop i = 0 while i < n do { yield (i, i + 1); i + 1 } }` | 1 | `t3_pairs.cml:8:9: [Error]: Unresolved reference: 'yield'. …(SemanticDiag::UnresolvedReference, code=0x0300000B)` |
| t9：`func main(): int sync { let yield = 1; println(yield); return 0 }` | **0** | `1` |
| `for x in count(3) do println(x)` | 1 | `count.cml:14:5: [Error]: Unexpected 'for'; expected '}'. …(SyntaxDiag::UnexpectedToken, code=0x0200000A)` |
| `let s = count(3)`（`count` 的体里有 `yield`） | 1 | `count_let.cml:7:9: [Error]: Unresolved reference: 'yield'. …(0x0300000B)` |
| `func wrong(x: int): int { yield x }`（头不是流类型） | 1 | `head_not_stream.cml:8:5: [Error]: Unresolved reference: 'yield'. …(0x0300000B)` |
| `unfold(0, (i: int): int => i + 1)` | 1 | `unfold.cml:6:13: [Error]: Unresolved reference: 'unfold'. …(0x0300000B)` |
| `func pass(s: int..): int.. { return s }`（只有类型，没有产生者） | **0** | `1` |
| `loop acc = 0 for x in [1, 2, 3] do acc + x`（片 F 要扩展的那种走法） | **0** | `6` |
| `let s: int.. = consume`（把函数赋给流类型） | 1 | `…:10:5: [Error]: This value has type 'void', which is not the declared type 'int..'. …(SemanticDiag::DeclaredTypeMismatch, code=0x03000110)` |

四条要点：**(1)** `T..` 与 `int..` 在参数位、结果位都能解析、能类型检查（片 B 是活的），t3 的错误停在
`yield` 上而**不在 `..` 上**——`plan.cn.md:296` 记的那条"错误停在 `..`"已经过期。**(2)** `yield` 今天仍是
普通标识符（t9 exit 0、stdout `1`）。**(3)** 头不是流类型时没有任何具名诊断，只有 `UnresolvedReference`。
**(4)** 计划的验收写 `for p in pairs(3)`，而 **`for` 在 Camel 里不是语句形式**（探针逐字
`Unexpected 'for'`）；真正的走法是 `loop acc = 0 for x in s do …`，片 F 要扩展的是它。

## 选项与代价

**路线一：可恢复的执行（NodeVM 的帧 + plan 位置保存）。** `yield` 节点在执行时把解释器状态存进一个堆对象，
下次从那里继续。要存的量小：plan 位置 `i`（`nodevm.cpp:301`）、臂跳过状态（`tillNode`/`skipNode`/`joinNode`/
`enclosingArms`，`nodevm.cpp:277-283`）、`resumedLoop`，加上帧本身——循环状态已经在帧槽里
（`ExecLoop::stateSlot`，`exec_plan.h:75`），区域栈与"上次停在哪个 `yield`"就是这些量，正合裁定 (a) 的描述。
好处是**不需要做任何源码展平**：`loop`、`if`、`match`、`sync` 的嵌套照现有的区域机制跑，任意嵌套 (b) 自然成立。
代价：新造流的运行期表示与一个流值对象（含帧的 GC 根）；NodeVM 要有第二个入口；**字节码（`std::fvm`/`std::jit`）
与 `std::prl` 各自要一份等价的可恢复执行**，否则流程序在后端上不一致——这是这条路线最大的未知量，不是几十行。
约 800–1200 行、触及 GC，至少要 4–6 次远端构建（每次约 10 分钟）。

**路线二：展平为"步进函数"（编译器变换，图里就是状态机）。** 把生成器的体线性化成基本段（在 `yield`
处切开），`pc` 与所有活过 `yield` 的局部成为状态分量，图变成"给定 (pc, 状态) 算出 (element, 下一个 pc, 下一个
状态)"的普通无环图。好处：**图是普通图，四个后端一行不改**；坏处：这是一次真正的编译器变换——要自己做 CFG
展平、做**变量提升**（只在一个 `if` 臂里声明的局部在所有路径上都要有状态分量与类型）、把嵌套 `loop` 的
状态与位置一起提升、并保证 `sync` 块里效应先后。它是 `plan.cn.md:480` "复用 `LOOP`/`BRCH`" 那条约束的自然读法，
但工作量比路线一大，而且做错不会崩，只会静默给错值。

**两条路都要一起做的（否则做完了也看不见任何东西）：** 片 F 的最小消费者——`loop … for x in <流>` 走
`__iter__` 的一个流重载 + 循环驱动的一种流形式（`loop_driver.h:179` 的 `host.iterable()` 之外再加一种
"每轮拉一次、拉空即结束"）；头部的"必须是流类型"具名诊断（今天只有 `UnresolvedReference`）；`yield_lowering.cml`
改成正向用例（`count(3)` 得 0、1、2）。诊断码 `FeatureNotSupported`（`0x03000040`）**不随之删除**：它在
`src/compile/gct/builder.cpp` 的使用者：本页读数时（`cd44250a5`）是 **17** 处（`:983`、`:991`、`:1658`、
`:1684`、`:3015`、`:3679`、`:5137`、`:5144`、`:5151`、`:5158`、`:5165`、`:5172`、`:5486`、`:5513`、`:5634`、
`:5645`、`:5656`）；**今天的 `origin/develop`（`9d407fa5a`）是 15 处**（`:983`、`:991`、`:1658`、`:1684`、
`:5113`、`:5120`、`:5127`、`:5134`、`:5141`、`:5148`、`:5462`、`:5489`、`:5610`、`:5621`、`:5632`——`:3015` 与
`:3679` 两处被语法收口那批去掉了）。删的只是 `yield` 那一处；它仍在语法收口第 5 条的删除名单上，属另一批。

**两条路不是同一层次的选项。** 路线一作为**终态不成立**：只在 NodeVM 做可恢复执行，会让同一个流程序在
`std::nvm` 上跑、在 `std::fvm`/`std::jit`/`std::prl` 上跑不了，与设计原则 9.10"四个后端结果相同"冲突，
等于换一种形式的后端分歧；它只适合当探路。**终态是路线二**：状态机在图里，图是普通图，四个后端一行不改。
路线二里唯一的新机器是"循环每轮拉一次"——`yield` 的值就是 `LOOP` 的收集槽（`runtime/loop.h:20-28`），
"惰性的收集槽"正是计划自己对它的描述（`plan.cn.md:120`）。
`src/compile/gct/builder.cpp` 的使用者：本页读数时（`cd44250a5`）是 **17** 处（`:983`、`:991`、`:1658`、
`:1684`、`:3015`、`:3679`、`:5137`、`:5144`、`:5151`、`:5158`、`:5165`、`:5172`、`:5486`、`:5513`、`:5634`、
`:5645`、`:5656`）；**今天的 `origin/develop`（`9d407fa5a`）是 15 处**（`:983`、`:991`、`:1658`、`:1684`、
`:5113`、`:5120`、`:5127`、`:5134`、`:5141`、`:5148`、`:5462`、`:5489`、`:5610`、`:5621`、`:5632`——`:3015` 与
`:3679` 两处被语法收口那批去掉了）。删的只是 `yield` 那一处；它仍在语法收口第 5 条的删除名单上，属另一批。