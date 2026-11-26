# 十五行“变严还是被拒”：七行是缺陷，八行是记录过期

判定对象：第 2 类（“用例不再能编译”，按 `meta.diagnostics[0]` 口径 130 行）里，除 `Range` 根因簇（67 行，归 `D-1123-41`）之外的 15 行。
取数：`D:\Temp\t122\fail\**\meta.json`（tip `065bfb1690` 落盘）。判定只读：没开 run、没改用例与实现。

## 口径：15 行是怎么点出来的

```
LoopBodyTypeMismatch          6  = var_read_before_region ×5（default/nvm/fvm/jit/opt_nvm）+ mm.gc_poison 副本 ×1
EmptyArrayWithoutType         5  = compile.literal_context/context.empty_array ×4（四后端）+ linear.loops/loops.grad_loop_collect
BranchReturnTypeMismatch      1  = showcase.d4_mcts/mcts.selfplay.curve
MatchCaseTypeMismatch         1  = consistency.narrowing/narrowing.shadowed_not_narrowed
DeclaredTypeMismatch          1  = compile.native_declarations/native.para_unordered_reduce_callback_arity
NoMatchingFunction（非 Range） 1  = consistency.narrowing/narrowing.functions（`f == g`）
⇒ 6 + 5 + 1 + 1 + 1 + 1 = 15
```

**另记一笔**：非 `Range` 的 `NoMatchingFunction` 落盘其实有 4 行，多出来的三条是
`consistency.entering/entering.err_{numeric,arg_reverse,other_payload}.nvm`，它们的失败是**位置过期**（期望 `.cml:4:14:`、实际 `6:14`），
与下面 G6 同形 —— 计类时别漏进别的类。

## 缺陷 7 行：两处，都在 `buildStreamLoop`

### G1（6 行）合法程序被拒

设计页站在用户这边：`docs/design-principles.cn.md:277`「**循环是一个带状态的表达式。** 状态写在循环头上，循环体的值是下一轮的状态」、
`:278`（多状态用逗号分开、循环的值是它们组成的元组）、`:290`（状态的类型取初值的类型）、
`:208`（3.5：`var` 一律化成 SSA，循环体里给外面的 `var` 赋值**自动成为循环携带的状态**）。
设计要求用户返回的是**头里声明的那几个状态**；`z` 是携带状态、不在头上，设计页没有一句要求用户把它一起返回。

实现的两层读数（不是推断）：

```
· 普通 while 路径自己填携带分量：builder_control.cpp:3388-3389（declaredStates == 1 时 components[0] = returned）
  ＋ :3433-3440（携带分量从 loopStateWrites_ 取，不需要用户写）
· for … in <range> 走 buildStreamLoop（:3951），:3983-3984 把含携带状态的 stateTypes 整体当 stateType，
  :4148 直接 intoState(loopStateValueOf(...), stateType) —— 没有那道填充 ⇒ 要求循环体交出 (int, int)
· 同文件 :3847-3854 的注释自己写着：携带的 var 是 "the loop's own state and no value of the program's"
```

⇒ 判**缺陷**。补实现的位置：`src/compile/src/gir/builder_control.cpp` 的 `buildStreamLoop`（`:3951-4192`）里照 `:3433-3440` 填携带分量，
`:4148` 只按头里声明的状态比；`:3983-3984` 的 `stateType` 只用于机器元组内部。

引入这一族的那一笔是 `132a5bee4d`（"A write inside a loop body is the loop's carried state"）：正文写了理由（设计 3.5、携带状态是内部量、
`let n = loop …` 的类型不变，并附两组反向验证），但**没有**说 `for … in <range>` 那条流路径没有跟着改。

### G3（1 行）内部状态元组漏进诊断

`showcase.d4_mcts/mcts.selfplay.curve` 是 `kind="behavior"`、`expect_exit=0`、要打印两行结果的行为用例，
诊断却印出期望类型 `(int.., bool, float)` —— 这是 `buildStreamLoop` 的机器元组（`:3980-3982` `machineTypes{stream, Bool()} + stateTypes`），
**任何 Camel 程序都写不出这个类型**。

设计页对“`return` 能不能站在循环体里”没有写（3.10 没这条），这一处要作者定；但**无论怎么定，把机器元组印给用户都是错的**。
补实现的位置：循环体 arm 的退出期望类型（`:4116-4180` 的 `ExpectedValueScope`、`visitExitNode` → `expectedExitTypeHere()` `:4222/:4234`、定义 `:936`）。
引入这一族的是 `3b66128e21`（"feat(stream): a `for` over a stream is the loop the graph already has"），正文没提这件事。

## 记录过期 8 行

### G2（5 行）空数组作循环初值：有意变严，改用例

`docs/design-principles.cn.md:374`（5.1）：「**空数组字面量 `[]` 的元素类型只取自它所在的位置**：没有这样的位置
（**作循环初值**、作泛型形参 `T[...]` 的实参）是编译错误，要写类型（`let e: f32[] = []`、`show::<f32>([])`）。`void` 不是元素类型。」

两个用例的注释仍写“空数组从 append 推元素类型”，与设计页正面冲突 ⇒ 设计页为准，改用例。
语料已有 8 处先例：`loop acc = [] as int[]`（`test/cases/loops/compiled.cml:35`、`loop_collect_multi.cml:6`、`test/cases/modules/onnx/loop_collect/*.cml`）。
`grad_loop_collect` 的 `expect_stdout_exact` 不用改（状态类型变了、打印值不变）。

另记两条实现侧问题（不改本组判定，但要记成缺陷）：
- 诊断文案错位：报在 `[]` 自己那一处、名字写循环变量、建议「Write the type: `let squares: int[] = []`」——**这条建议对循环状态无效**（该写 `loop acc = [] as int[]`），位置也应指循环初值。
- 同一写法在**数组形**循环里仍放行：`test/cases/loops/open_rank_round_rank2.cml:10` 在尖端上是绿的 ⇒ “作循环初值一律要写类型”还没落全（流形拒、数组形放）。

变严的来源：`826c8f0c64` 首次引入该诊断与文案，正文讲的是 `T?`/`T!` 的装入，没单独交代这一处收紧；设计页那句是 `12522984c5`（裁定并进原则页）。

### G4 / G6（各 1 行）纯位置漂移

```
consistency.narrowing/narrowing.shadowed_not_narrowed：plan 钉 `.cml:4:`，实际 6:18
consistency.narrowing/narrowing.functions：          plan 钉 `.cml:8:`，实际 12:18
```
语义判定本身都对（设计 3.7:222 分支臂类型、3.8:231 遮蔽只对 `let` 与参数生效；5.2:383 函数值没有身份、不能比较）。
漂移来自 `9622bd21c9` 与 `eb20efab7b` 两次语料/格式改写 —— `eb20efab7b` 的正文只声明“一个 formatter pass，**no program's meaning depends on it**”，
**没有**交代“钉住 `.cml:LINE:` 的期望要一起重录”，而那一类期望恰恰依赖格式。

### G5（1 行）不属本组

`compile.native_declarations/native.para_unordered_reduce_callback_arity` 的第一条诊断其实是 `Range` 那簇
（`.cml:11:5` 的 `let xs: int[] = 1 .. 5` 报 `This value has type 'Range'`），用例要测的“三参数回调被拒”根本没跑到
⇒ **从这 15 行里划给 `D-1123-41`**；那笔修完后把第 11 行换成不经 `Range` 的写法（如 `[1, 2, 3, 4]`），本组的本意才测得到。

## 判错方向的代价

```
G1/G3 若当“记录过期”去改用例 ⇒ 会写出一个按设计本不该写的 `(i + 1, z)`（把内部状态的错当规范），且 showcase 那条永远修不好；
G2 若当“缺陷”补实现（恢复 append 推元素类型）⇒ 等于改设计 5.1:374，那是裁定，不是补实现。
```

## 处置

- **G1 + G3** 开一条修复车道（同一处代码、7 行、含一个 showcase 行为用例）；修完 `for … in <range>` 这一族都受益。
- **G2 / G4 / G6** 并进重录那格（G4/G6 是纯行号；G2 要改两行程序文本为 `[] as T[]`）。
- **G5** 只做一件事：划走，归 `D-1123-41`。