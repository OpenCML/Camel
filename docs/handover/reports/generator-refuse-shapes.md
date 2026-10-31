# 生成器的 `refuse` 还会命中哪些形状

> **`not built yet` 这种注释会自己过期；过期之后它比没有注释更糟，因为它会让人以为功能不存在。**

> **删一个 `refuse` 之前，先证明它拦下的形状已经能跑；否则删的是错误检查，不是限制。**

> **先跑红再跑绿的删除，是唯一能证明"删的是限制、不是检查"的做法。**

> **删掉检查之后错误换了名字（`FeatureNotSupported` → `UnresolvedReference`），说明删的是检查不是限制；把检查放回去，并把这一跑写成负例。**

> **把"还没做"钉成负例，等于把缺口写成语言规则；缺口要么被修掉，要么被记在待办里，不能出现在验收里。**

> **"先证明能跑，再删拒绝"的顺序不能倒：先删拒绝只能得到一个更响的错，先证明能跑才能得到一个更弱的拒绝清单。**

日期：2026-10-31　读数树：`lane-gen`（`5803e9039`）

**这一页只列形状，不删任何代码。** 本机不构建（Windows 上构建会蓝屏），所以每一行都是**静态读代码 + 对照已有用例**得来，标"未跑"的触发程序没有实际跑过。

## 先答两句

**① 状态机已经建好，`:2164` 那段注释从 `493d3953e` 起过期。**

- 注释写于 `e65fc0f69`（2026-10-26 03:33，"the refusal that keeps it from running as a `return`"），当时状态机确实是 0 行。
- 状态机在 `493d3953e`（2026-10-30 06:57，"Lower a generator into a state machine of pulled steps"）落进同一文件：`visitGeneratorBody` 就是它下面的那台机器，`:2160` 的"which is not built yet"与同文件第 5648 行起的实现直接矛盾。
- **行号也漂了**：裁定写 `:5704`，今天是 `:5659`（`refuse` lambda）/ `:5667`（第一个调用）；`:2164` 没动。

**② 今天只有一条用例真的撞在这组 `refuse` 上**：`test/plans/feat/compile/stream.plan.toml:528` 的 `stream.yield_lowering_refused`（`yield_lowering.cml`，`loop` 带 `i`、`s` 两个状态）。其余的 `generators_*` 全是绿的正例。

## 形状表（四栏）

| 形状（一句话） | 怎么触发（最小程序 / 已有用例） | 今天有别的路径能处理它吗 | 要补什么才能删掉这条 `refuse` |
|---|---|---|---|
| 函数头没写流类型，体里有 `yield` | `func f(): int { yield 1 }`（未跑） | 没有。这正是"头必须写 `T..`"的检查处（`5.4`）；但类型层可能先报别的错，需确认 | 先查类型层是否已有"yield 要求流返回类型"的诊断；若没有，把 `5667` 换成正经类型诊断，让头推出流类型 |
| `yield` 没有值 | `func g(): int.. sync { yield }`（未跑） | 没有 | 定 `yield` 无值是否合法（0 元流？）；不合法就给类型诊断 |
| `yield a, b` 多个值 | `func g(): (int, int).. sync { yield 1, 2 }`（未跑） | **有**：`yield (1, 2)` 元组今天能跑（`generators.cml` 的 `quad()`） | 把多值按元组收成一条，或在机器里按元组元素展开 |
| `yield` 包在块语句里 | **已有用例** `stream.generator_yield_in_block`（`{ yield 1 }` 后 `yield 2`） | **有**（已删并转绿：`117/117/0`，`01a46940c`） | 已做：块单独一个 arm，块后接着原来的 arm |
| `loop` 没有单个初值 | `loop while c do { yield 1 }`（未跑） | 没有（`init == nullptr` 直接拒） | 支持 0 状态循环：不建状态单元，只保留条件/体/回边四个点 |
| `loop` 带两个及以上状态 | **已有用例** `yield_lowering.cml`（`loop i = 0, s = 0 … { yield (i, s); (i + 1, s + i) }`） | **有**：把两值装进一个元组单元 `loop st = (0, 0) while … do { let (i, s) = st; yield (i, s); (i + 1, s + i) }`（未跑，按单状态路径推断） | 把多状态拆成元组状态；或给机器加多状态的 arm 布局 |
| `for` 循环里有 `yield` | **已有试跑**：`loop n = 0 for x in xs do { yield x; n + 1 }`（**表里原来的 `for x in xs { … }` 根本不过解析器**：`Unexpected 'for'; expected '}'`；语言里只有 `loop s = init for x in xs do …` 这一种写法） | **没有**：`124/120/4` 红，`generators_for_yield.cml:8:9` `FeatureNotSupported`；按位置状态（序列单元 + 位置单元 + 绑定单元，条件 `idx < len(seq)`、步进 `idx + 1`）写了一版，**挂住**（四个后端都无输出、被超时杀掉），已整体收回，`refuse` 还在 | 先查那版为什么挂（arm 布局/条件与步进的接线），再谈删拒绝 |
| `loop` 初值依赖当前点 | `var c = 0; loop i = c while i < 3 do { yield i; i + 1 }`（未跑） | 没有（`!hoistable(init)` 直接拒） | 允许读单元的初值：把初值求值放进第一个 arm，而不是提到循环前 |
| 循环状态名与体内单元重名 | **已有用例** `stream.generator_loop_shared_cell`（`var i = 0` 后写 `loop i = 0 while i < 3 do { yield i; i + 1 }`，循环后再 `yield i`） | **有**（已转正：`120/120/0`，四后端输出 `123`，`a1182780f`） | 已做：循环状态复用体内那个单元（一个名字一个单元），并把"读在声明前"的检查限定在体自己的语句表上 |
| `match` 臂绑定名字，臂里有 `yield` | `func g(x: int): int.. sync { match x { case n => { yield n } } }`（未跑） | 没有（`Ref`/`EnumPattern` 绑定被拒：绑定活在臂的图里，活不过 `yield`） | 把绑定提升成单元（与"局部跨 `yield`"是同一套：`addTypedCell`） |
| 循环体最后一条语句不产值 | `loop i = 0 while i < 3 do { yield i; var x = 1 }`（未跑） | 没有（`last == nullptr` 拒：下一轮没有值可写回状态） | 让"最后一条是 `var` 声明"也算产值（读那个单元），或让 `var` 在这里写成赋值 |
| `:2164`（`visitExitStmt` 看到 `yield`） | **没找到能命中的程序**（全部函数体在 `:5426` 被路由到生成器体；嵌套函数/lambda 各自路由并以 `:5667` 拒） | 无（按静态读法疑似死路径） | 删之前先找到一条能命中的程序；找不到就是死代码，可连同 `:2160` 注释一起删 |

## 两个数

- **形状 11 条**（表的前 11 行；`loop` 的四个子形状分开数）。
- **余下 2 条**：`for` + `yield`、双状态 `loop`（`yield_lowering_refused` 是作者留着待翻转的标记，不算）。
- **`yield a, b` 已删并转绿**：先红（`112/108/4`，四个后端都是 `FeatureNotSupported('generators')`）→ 删分支、用 `TupleDataFactory` 装元组 → 绿（`112/112/0`），`139d3a62d`。
- **块里的 `yield` 已删并转绿**：先红（`117/113/4`，四个后端都是 `FeatureNotSupported('generators')`，位置 `generators_yield_in_block.cml:7:7`）→ 给裸块加一个 arm、拉取停在块内、块后继续 → 绿（`117/117/0`，四后端四条 `[PASS]`），`01a46940c`。
- **状态名重名先红了、负例也钉了，但作者裁定不许钉负例**（2026-10-31）：第一次删法回来 `UnresolvedReference('i')`；把 `refuse` 放回、写成负例是错的处理——**负例等于把缺口写成语言规则**。改正：循环状态复用体内那个单元（一个名字一个单元），并把"读在声明前"的检查限定在体自己的语句表上（块里的语句位置不是体里的位置，之前的误报就来自这里）。`stream.generator_loop_shared_cell` 翻成正例：`120/120/0`，四后端输出 `123`（0、1、2，再是循环后的 3，与同一段逻辑写成普通循环逐步打印的结果相同），`a1182780f`。
- **全树负例扫描**：`git grep -n 'FeatureNotSupported' test/plans test/cases` 里"以负例把缺口钉住"的只有 `stream.yield_lowering_refused`（作者留着待翻转），其余命中都是 `expect_output_not_contains`（要求不出现），不是负例钉缺口。

## 这张表不承诺什么

- "有别的路径"是按**语义等价的最小改写** + 已有绿例推断的；状态名重名这条先被实测推翻（`UnresolvedReference`），后按作者裁定改正并转正。
- `:2164` 的"疑似死"同样只是静态读法：删它之前要么找到一条能命中的程序，要么接受"删掉并留一条它曾在哪里被命中的记录"。