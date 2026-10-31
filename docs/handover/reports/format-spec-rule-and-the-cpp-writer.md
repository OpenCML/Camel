# 格式说明的不对称在 develop 上已不存在，剩下的是 C++ 写出器

日期：2026-10-31　车道：`lane-fmt`

裁定的原文是：

> **「`${arr:.6f}` 在插值里被拒，而 `'{:.6f}'->format<xs>` 接受。插值和 `format` 必须接受同一套格式说明，这是规则统一的缺陷，修掉。」**

这一页先说前提：**这条不对称在 develop 上已经不存在了**，两条写法落到同一个调用、给出同一段文本；今天真正剩的缺口在另一端——`std::cpp` 写出的 C++ 程序里，数字数组的格式说明会被拒。前提的现状先记下来，比反复解释省事。

## 一、前提的现状：两个读数一样

同一个程序，`let fs: float[] = [0.7692, 0.7808]`：

```
插值   `out: ${fs:.6f}`           ->  out: [0.769200, 0.780800]   exit 0
format 'out: {:.6f}'->format<fs>  ->  out: [0.769200, 0.780800]   exit 0
```

**两边都接受，逐字节相同。** 作者自己撞上的那一段也一样：把
`showcase/d4-mcts/selfplay.cml` 的 `let curve = '{:.6f}'->format<losses>` 改成直接插值
`${losses:.6f}`，程序 exit 0，打印

```
mcts selfplay losses=[0.640625, 0.483687, 0.358262, 0.279323, 0.263602, 0.297754, 0.325618, 0.320763, 0.293756, 0.261926]
```

## 二、为什么一样：本来就是一个解析器

- `src/builtin/operators/str.cpp:235` —— `checkFormatPattern`，**唯一**判断一条格式说明是否被接受的代码。
- `src/builtin/executors/builtin.cpp:690/694` —— `:io/print`、`:io/println`（`formatWithArg`）与 `:str/format`（`formatNormArg`）登记的是**同一个**检查器。
- `src/compile/gir/builder_operators.cpp:3699` —— 编译期**唯一**的调用点。
- `src/parse/ast/builder.cpp:4736` —— `visitFStringData` 把 `${expr:spec}` 就地落成同一个 `:str/format` 调用（表达式进 `with`、pattern 进 `norm`），此后各阶段看不见"插值"这回事。

所以没有第二套解析器，也没有"让哪一处向哪一处看齐"可谈。裁定引用的读数来自更早的二进制：
`85a591b34`（2026-10-25，**act on every element when a specification meets a numeric array**）
把"规格作用于数字数组的每个元素"这半补上，同时给 `checkFormatPattern` 加了数组分支；
在此之前 `'{:.6f}'->format<xs>` 打印数组的无规格文本，插值同样。

## 三、92 对矩阵：旧写出器 vs 新写出器

13 个类型 × 每个 8 或 5 种规格（`{}`、`{:.6f}`、`{:d}`、`{:>8}`、`{:.2f}`、`{:x}`、`{:e}`、`{:08.2f}`），
每对写两遍（插值 + `format`），同一份 C++ 源分别用**旧** `format.h`（`HEAD` 那一版）和**新**
`format.h` 编出来跑，再与 `std::fvm` 的文本逐字节比：

```
[SUMMARY] pairs=92 front_reject=47 same=33 differs=0 untranslatable=12 one_side=0 checked_programs=66
          old_programs_fail=20 old_programs_wrong=6 old_pairs_fail=10 old_pairs_wrong=3 old_pairs_same=20
          new_header_failed=0
```

- **47 对**前端按类型同拒（`int[]` 配 `{:.6f}`、`f32` 配 `{:d}`、元组/结构配数值规格…）。它们在任何后端都一样，不因 C++ 而变。
- **12 对** `std::cpp` 根本不能翻译（`f16`/`bf16` 没有 C++ 字面量、未定型数组），修前修后都一样。
- 剩下 **33 对**是这次变的：修前 10 对在 C++ 里直接 `FormatFailed`，3 对**静默写错**（`{:>8}` 把整个数组的文本当字符串补齐，exit 0），20 对本来就对；修后 **33/33 逐字节相同**，失败 0、写错 0。
- 程序级：修前 20 个跑不起来、6 个悄悄写错，修后 **0 / 0**。

（同一套读数在更早的 102 对扫描里是 57 对前端同拒；多出的 10 对是我把类型名写成 `complex`/`tensor`
被当成未解析引用，属噪声。）

## 四、修的是哪一半

`include/camel/standalone/format.h`：`FormatArg` 多一种 `Kind::Array`，数组参数随身带上"按规格逐元素写"
的那份文本——`SpecPerElement` 只认 `int32`/`int64`/`float`/`double`，与 `isNumericElementType`
同口径；元素不是数字的数组（`string[]`、`bool[]`、`f32[][]`）仍走文本，规格照旧排整段文本，与解释器一致。

两行逐字节相同（同一个程序，`'{:.6f}'->format<fs>`）：

```
std::fvm  [0.769200, 0.780800]
std::cpp  [0.769200, 0.780800]
```

钉住它的用例是 `test/cases/trans/cpp/format_array.cml`，列进 `test/tools/cppexec/expect.txt`
（由 `feat/trans/cpp.agree_with_fvm` 翻译、编译、运行、与 `std::fvm` 逐字节比）。

## 五、`agree_with_fvm` 那一片红：`:var/after` 从来没有翻译

`feat/trans/cpp.agree_with_fvm` 是 `slow` 层，`-Plan all -Tier fast` 跑不到它。它在慢层里**是红的**——
`std::cpp` 直接拒绝整个程序：

```
:var/after … on a value of type 'var int', which has no translation in std::cpp yet
```

`test/cases/linear/nvm/var_params.cml` 与 `test/cases/trans/cpp/exec/variables.cml` 都在这个理由下被拒。
但整闸只跑 `-Tier fast`，这一片红从来没进过读数：**缺口不需要被藏起来，它只要待在没人跑的那一层就够了。**

`:var/after(cell, call)` 是"变量在被那次调用写过之后的版本"（`(var T, any) => var T`）：结果是那个单元本身，
第二个实参是那次调用、**不被读、可以是 `void`**。写出器补上它（`23298498c`）之后，生成的程序里那次调用
本来就是自己的一条语句，效果按图给的顺序跑，版本就是那个单元——与一次写留下的一样。

两个程序现在都编译、运行、与 `std::fvm` 逐字节相同：

```
std::fvm  12 33 / abc / 5 12 12
std::cpp  12 33 / abc / 5 12 12
```

整张 `feat/trans/cpp.plan.toml`（118 个程序）：`[SUMMARY] total: 11, pass: 11, fail: 0`。

这一条原来只在慢层（`tier = "slow"`），整闸看不到；现在挪进快层（`4c6ee7991`）。快闸总数
**8038 → 8039**——只多这一条，另十条本就在快层（计划顶上的 `tier = "fast"` 是默认值）。wall：
**178.6 s → 171.3 s**（安静时，那 37.7 s 藏在 8 路并行池的临界路径下面）；主机被别的车道编译压住时
是 **→ 282.3 s**，因为 118 次 `clang++` 会和 8 路并行抢核。同一个提交、同一台机器，两次读数差 111 s，
差的不是这条测试，是机器上另一个构建。

顺带一条：`test/tools/toml-lite.mjs` 的转义，**换序修不好**（`\\` 先塌成 `\` 之后，`\n` 那一遍仍会咬住
后面的 `n`），改成**一次左到右解码**；`"x\\ny"` 旧读法是 `\` 加一个真换行（进了 JS 就是行继续），新读法是
`\` 和 `n` 两个字符。

## 六、`std::cpp` 还缺哪些翻译：一次数清（3195 个程序）

`test/cases/**/*.cml` 全部 3195 个，每个跑一遍 `std::cpp`，把"VM 接受、`std::cpp` 拒"的程序按诊断里的
算子归类。

**先说尺子，因为它错过一次。** 最初那次普查只认两句话——`has no translation in std::cpp yet` 和
`std::cpp does not translate yet`——于是漏掉了第三种说法（`on values std::cpp does not translate **it
for** yet`，约 30 个），也把**类型层面**的拒（`converts a value of type 'f32' to 'f32[..]'`、
`returns a value of type 'Json'`、`'\'Device<f32[..]>\''` 这些）压成了"另有 11 个"。现在按**一句**
数：输出里有 `TranslationRefused` 就算被拒，一个程序一行。读数不是 683，是 **999 / 3195**（31%）。

| 算子 | 被拒程序数 |
| --- | ---: |
| （诊断没点名任何算子：类型层面） | 400 |
| `autodiff:grad` | 88 |
| `onnx:export_model` | 78 |
| `tensor:sum` | 45 |
| `tensor:ones` | 42 |
| `autodiff:with_grad` | 40 |
| `tensor:zeros` | 34 |
| `tensor:load_npy_f32` | 31 |
| `rand:uniform_array` | 24 |
| `tensor:multiply` / `tensor:shape` / `tensor:matmul` / `:op/slice_sub` / `:op/mem_blocks` | 15 / 12 / 10 / 10 / 10 |
| `tiles:inspect` / `tiles:source` / `tensor:add` / `rand:shuffle_l` | 8 / 7 / 7 / 7 |
| `env:get_cwd` / `tensor:full` / `array:sum_l` | 6 / 5 / 5 |

其余每个 ≤ 4 个，最长的一串是 `tiles:*`、`tensor:*` 和几个模块的 `fs:`/`io:`/`re:`。tensor 一族合计
约 250、autodiff 128、onnx 79、tiles 24、rand 35；核心算子（`:op/*`）现在只剩 `:op/slice_sub` 10
（rank≥2 的值）、`:op/idx_arr` 4、`:op/idx_take` 1、`:op/mem_blocks` 10（gpu 那条线）。
**4 个崩**（`rc=139`）已修成 0，见下。

这条读数线（旧尺子，只数点名算子的那两句话）是 **683 → 674 → 666 → 661 → 638 → 629 → 593**：
第一笔 `:op/set_index_in_place` 9 个，第二笔 `:op/set_field` / `:op/set_index` 5 个，第三笔
`:op/idx_take` 4 个 + `:struct/last_arr` 2 个，第四笔 `:op/fail` 32 个（23 个真翻译、9 个换桶），
第五到七笔是换桶那 9 个的三个算子（六之二），第八笔 `tensor:reshape`（六之三）。**这条线只用来比
趋势**；绝对值以 999 为准。

这张表把两种东西分开了：tensor / autodiff / onnx / tiles / rand 是**整个子系统**（bridge 不运数组与
张量，`std::cpp` 本来就不打算翻它们，要动是另一个决定）；`:op/*` 和 `:struct/*` 才是**写出器的洞**。

**表头那一格是个假的最大值。** 270 个程序被记在 `tensor:reshape` 名下，但按原文看是 7 句、且只有一句
是真关于 reshape 的：`f32[]` 182、`float[]` 80、`int[]` 5、`fN[][]` 1，其余 2 句在匿名函数和 `check`
里。更关键的是**它们不是同一个洞**：把 reshape 翻出来之后，只有 **6 个**真的翻译出来了，**264 个换了
桶**——`onnx:export_model` 78、`tensor:sum` 38、`autodiff:with_grad` 24、`autodiff:grad` 16、类型层面
15、`tensor:multiply` 10、`:op/slice_sub` 9、`tiles:*` 15……因为拒绝是按"图里第一个走到的算子"报的，
reshape 只是那条流水线上最先碰到的那个。**一个格子 270 个程序，真正属于这个格子的只有 2 个**
（修完之后仍停在 `tensor:reshape` 上的只有 2 条）。

**4 个崩**（`rc=139`）：`compile/contextual_empty_array/all_literal.cml`、
`contextual_empty_array/named_sibling.cml`、`compile/semantic/recursive_struct_array.cml`、
`recursive_struct_array_gc.cml`。四个都是"结构体通过数组装自己"（`type Node = { value: int, children: Node[] }`）。
`gdb` 的栈帧是没头的 `ProgramValues::type` 自己套自己：`type(Node)` 去算成员的 C++ 类型，成员是 `Node[]`，
`type(Node[])` 又要 `type(Node)` —— 名字还没登记就先走成员，走不完。这一笔（`12034b9e4`）把这种结构体的名字
在走成员**之前**登记进去（枚举那条路早就是这么写的），成员走完再把 layout 记上；别的类型走的还是原来那条路，
名字顺序也没动。另外 `array.h` 把 `alignof(T)` 的静态检查从类作用域挪进了 `allocate`：`Node` 里的
`rt::Array<Node>` 会在 `Node` 还没完整时点名它，而只有分配才需要尺寸与对齐。

四条读数：**139 → 0 / 0 / 1 / 0**。前两条、第四条翻译出来且与 `std::fvm` 逐字节相同（`0 2 0 7`、`9 1 0 0`、
`10 178 1024 / 4`）；第三条给的是诊断，不是崩：

```
error[RuntimeDiag::TranslationRefused]: Translation pass 'std::cpp' refused the graph: function 'main' uses the operator ':op/set_field' on a value of type 'Node', which has no translation in std::cpp yet.
  --> test/cases/compile/semantic/recursive_struct_array.cml:40:19
```

（`grown.children[0].value = 20` 是 `:op/set_field`，当时是另一个洞，5 个程序，下一笔就修掉了。）

顺手撞出**第 5 个崩**，这次在前端不在写出器：`type Node = { state: State, children: Node[] }` 里 `State`
写在 `Node` **后面**时，`std::fvm` **自己**段错误（`rc=139`）；`State` 写前面就正常，互相递归（`A` 里 `B[]`、
`B` 里 `A[]`）也必然先撞这一条。最短复现 6 行（`type A = { b: B }` / `type B = { n: int }` / `main` 里
`let a: A = { b: { n: 1 } }`），去掉那句 `let` 就只剩诊断——**"让这个值活着"才是复现的必要条件**。
`gdb` 的栈帧：`Type::assignableFrom` ← `Builder::visitStructInto` ← `visitDataInto` ← `visitDataDecl`，
不是自己套自己，是**拿一个空字段类型去问"能不能赋给"**。

根因：字段点名了后面才声明的类型 → 那个名字当场报 `UnresolvedTypeReference`、声明**什么都没声明**（设计原则
写的就是这句）——可那个"半成品类型"还留在解析它自己字段的那个作用域里；于是 `let a: A = {…}` 认出了它，读
它那个空字段，解引用空指针。修法（`5e3cb487a`）：`lookupType` 在**问任何作用域之前**先看这个名字的声明是不是
已经失败，失败就直接中止——报告还是声明那一条，不新增、也不拿半成品去建值。结构体通过数组装自己仍然要在
自己的字段里认出自己，那条作用域照旧。

两条读数：**139 → 1**（前向引用）、**139 → 1**（互相递归），给的都是同一句

```
error[SemanticDiag::UnresolvedTypeReference]: Unresolved type reference: 'B'.
  --> test/cases/compile/semantic/mutual_struct_array.cml:8:15
```

两条用例钉住：`compile/semantic/field_type_forward_reference.cml`（字段点名后声明的类型）、
`compile/semantic/mutual_struct_array.cml`（两个结构体通过数组互相点名）。

这一笔修的是洞里最大的**一条翻译**：`:op/set_index_in_place`（9 个），`xs[i] = v` 现在与
`acc = acc->append<v>` 同一个形状（`eae9f9f5b`）——变量把数组交出去，`rt::setAt` 按读的方式检查下标
（同一条故障、同一段报告、同一个位置），块被别的持有者共享时先复制，所以值还是值。九条程序与 `std::fvm`
逐字节相同，包括越界那一条：

```
std::fvm  [9, 2, 3]
std::cpp  [9, 2, 3]
```

接着修同一个族里的下一处：**`:op/set_field`**（5 个，比普查时多出来的那一个是以前崩的
`recursive_struct_array.cml`，现在按缺口报了）。也就是 `p.x = 9`、`m[0].x = 9` —— 赋值把"改了一处的
结构体"当**值**交出来，形状和上一笔的数组一模一样：**变量交出值、一处改变、块共享先复制**。所以它照抄
第一个的形状，没有另起一套：

```
对数组：rt::setAt(xs, i, v, at_x_y)          （上一笔的，复用）
对结构体：rt::setField(s, &Point::x, v)      （新，include/camel/standalone/record.h）
```

`set_field` 的字段是个**下标**，不是值——名字在类型里，所以规则表通过 `OperatorCall::fieldName` 去问，
写成成员指针；这一段第一版把 `normTypes`/`norm` 捕获进 lambda，而它们已经被 move 进 `NodeCall` 了，
所以永远拿到空类型、五条全拒。**"照抄形状"照抄的是语义，不是指针生命周期**，这是这一笔唯一的坑。

读数：**5 → 0**，普查 **674 → 666**，与 `std::fvm` 逐字节相同（五条都 `same`）：

```
std::fvm  { x: 9, y: 2 } { x: 1, y: 2 }
std::cpp  { x: 9, y: 2 } { x: 1, y: 2 }
```

第二列是**存之前读到的那个值**：`rt::setField` 返回新记录、`rt::setAt` 共享时先复制，所以
`let kept = p; p.x = 9` 之后 `kept` 还是 `{ x: 1, y: 2 }`。`test/cases/trans/cpp/field_store.cml`
把三处（字段、元素、元素的字段）各钉一遍，每一处都配一个存之前读的值。

再往下是普查里剩下的最小一对，**一处一笔**：`:op/idx_take`（4）和 `:struct/last_arr`（2）。
`idx_take` 是 `xs[positions]`——按一组位置取元素，`a6c21b186`；`last_arr` 是 `last(xs)`——最后一个
元素，空数组给"位置 -1"那条故障，`b0b03b861`。两个都照抄上一笔的形状：**只读不写、位置照读的那条
检查、越界就是读的那条故障、同一条站点**。`rt::take` 内部就调 `rt::at`，所以越界消息与位置原文一字
不差；`rt::last` 填的也是读填的那三个值（位置、"an array"、长度），所以空数组印出来的就是 `Index -1`
那一句。

读数：**4 → 0**、**2 → 0**，普查 **666 → 661**（第五个 `gc_born_old.cml` 换了桶：它现在卡在
`rand:uniform_array`，是模块的洞，不是写出器的）。六条里五条两边逐字节相同（包括两条故障用例）。

```
std::fvm  [40, 10, 30] [20, 20] [10, 20, 30, 40]
std::cpp  [40, 10, 30] [20, 20] [10, 20, 30, 40]
std::fvm  3 7 3     →   Index -1 is out of range for an array of length 0.
std::cpp  3 7 3     →   Index -1 is out of range for an array of length 0.
```

两条用例：`test/cases/trans/cpp/take_positions.cml`（一个置换、一个重复位置、一个定长 `f32[3]`）、
`test/cases/trans/cpp/last_element.cml`（三元素、单元素、空数组那条故障）。

最后是 `:op/fail` 那 32 个——我一直说它"不是一个翻译，是 `never` 贯穿三处"，**这个判断是错的**。真去
看拒绝原文，32 条是同一句、拦在同一个地方：**在问规则之前**，`lowerOperator` 先看结果的类型能不能写，
而 `never` 那时映射成 `void`——`void` 不是值，于是"结果类型没有翻译"。**不是三处贯穿，是一处**：`never`
这个类型在这儿没有值。把它做成真正的类型 `rt::Never`（`struct Never`，向每种类型都能转，但永远转不到，
因为唯一持有它的方式是结束程序），三处就一起通了：语句、`let` 的右值、分支的臂、参数、`never` 函数的
返回，全是"值站在要值的地方"。

报告那一半也有现成的：站点本来就有 `kind`/`message`/`location` 三个字段（为 `attempt` 的错误值留的），
所以 `fail` 的站点是 **ProgramFault 模板换个名字**——`d.name = "RuntimeDiag::" + 程序写的 kind`，
印出来就是 `error[RuntimeDiag::Stop]`，和解释器一字不差，`attempt` 抓到的错误值说的也是同一个 kind。

读数：**32 → 0**（这一行归零），普查 **661 → 638**。32 个里 **23 个真的翻译出来了**，另外 **9 个换了桶**
（它们早就过了这个算子，卡在模块的 `array:sum` / `array:element_*` 上）。`feat/trans/cpp` 里的
`fail_program.cml` 把五种站位各放一个 `fail`，最后留一个没人处理的，两边逐字节相同：

```
std::fvm  BadLimit / true / 7 / 1 / error[RuntimeDiag::Stopped]: the end ... rc=1
std::cpp  BadLimit / true / 7 / 1 / error[RuntimeDiag::Stopped]: the end ... rc=1
```

这就是判据要改的那一条：**32 个程序听起来是 32 个难点，实际是一个类型没有值。**

## 六之二、换桶的那 9 个：三个算子，一处一笔

`:op/fail` 修完之后，32 个里有 9 个**并没有真的翻译出来**，只是"不再卡在 `fail` 上"——它们早就走过
这个算子，停在前面的模块函数里。**这 9 个按原文分成三句，一句一个新算子**：

| 拒绝原文 | 程序数 | 谁在喊 |
| --- | --- | --- |
| `'array:sum_d' on a value of type 'float[]'` | 4 | `statistics::variance_of` |
| `'array:elements' on a value of type 'float[..]'` | 2（`sum_d` 修完变 4） | `array::flatten<float>` |
| `'array:sort_d' on a value of type 'float[]'` | 3 | `statistics::quantile<float>` |

三笔，一笔一个 sha，一笔一处：

1. `8fef6467e` **`array:sum_d`**——元素从 +0 加起、按摆放顺序、在 `double` 里累加（运行时的浮点归约
   就是这么累加的，`sumOf`）。只认 rank 1：这个后端的数组本来就是一条平铺的跑线。
2. `8cde63796` **`array:elements`**——行主序一条跑线。`rt::Array` 不留形状，所以"取元素"就是给它的那个
   值本身（`array_axis.cpp` 里 rank ≤ 1 也是原样返回）。**开区间 rank 也通**：`T[..]` 说的是长度未知，
   不是摆放方式不同。
3. `eb1eb5c4c` **`array:sort_d`**——`min`/`max` 的那个序（7.7）：NaN 在所有数之后，`-0` 在 `+0` 之前，
   其余照常；相同元素留在原位（稳定排序，所以 `sort(xs)` 与 `xs[argsort(xs)]` 是同一些位）。

读数：**638 → 629**（9 个全翻译出来了，一行不剩），n = 3194，崩溃 0。用例 `float_reduce.cml`：

```
std::fvm  7.0 / [-0.0, 0.0, 1.5, 2.5, 3.0] / 7.0 / [3.0, -0.0, 0.0, 1.5, 2.5] / [2.0] / 0.0
std::cpp  同上，逐字节相同
```

`-0.0` 排在 `+0.0` 前面这一条，就是这个序里最容易被漏掉的一半。

这 9 个里 8 个两边逐字节相同，`perf_fib_smoke.cml` 印的是耗时（每次都不一样），比不了；另外 4 个是
**bridged**（还要链 `libcamel`），本机默认 `libstdc++` 比 `libcamel.so` 要求的旧（`GLIBCXX_3.4.35/36`），
`run.py` 的链接行因此链不上——这是我手工补 `-L<conda>/lib -lstdc++` 之后才比出来的，**不是翻译的问题，
是这台机器的链接环境问题**。

判据进页：**一个洞归因到一处之后，换桶的那几个不算修好，只算没被它挡住——报数时要把"真的翻译了"与
"不卡在这了"分开。**

## 六之三、`tensor:reshape`：不是张量计算，是"这条平铺的跑线"换个说法

`reshape` 的语义是"同样的元素、行主序、换个形状"（`modules/tensor/kernels/layout.cpp`）。这个后端
**不留形状**（`rt::Array` 就是一条跑线），所以：

- **目标是一维**（`reshape<[3]>(xs)`）：值、打印出来的样子、能做的操作全都不变，**就是它自己**。这条
  能翻，而且 `xs->reshape<[n]>` 跟 `xs->reshape<[m]>` 的差别只有"元素数对不对"。
- 算子真正做的比较只剩**元素数**：目标若不是同一个数，运行时在**读任何东西之前**就报
  `TensorDimensionMismatch`，消息里带输入自己的形状（`[3]`）和目标（`[2]`）。这一半照抄了：
  `rt::reshape(xs, count, site)`，`count` 来自目标类型里写死的长度。
- **目标是多维**（`reshape<[2, 2]>`）：形状**是能被看见的**——`println` 印出来的是
  `[[1.0, 2.0], [3.0, 4.0]]`，不是 `[1.0, 2.0, 3.0, 4.0]`。这条**翻不了**，不是没写规则，是这个后端
  没有形状可印。所以规则只在目标一维时接，多维仍走 bridge。

一笔（`984b1ffac`）：`tensor:reshape` 那一格 **270 → 0**，真翻译出来 **6 个**，264 个换桶（分布见六）。
用例 `reshape_flat.cml` 三行相同、第四行是那个故障：

```
std::fvm  [1.0, 2.0, 3.0] / [4.0, 5.0, 6.0] / [1.0, 2.0, 3.0] / error[RuntimeDiag::TensorDimensionMismatch]: ... [3] -> [2].  rc=1
std::cpp  同上，逐字节相同
```

判据进页：**表头那一格按"被拒程序数"开工之前，先按"拒绝句数"看一眼——270 个程序可能只是两句话；
而且这两句话还可能是两条流水线的第一句话。** 这次的 270 是 7 句、1 个真洞、2 个真属于它的程序。

## 七、十条判据

> **裁定也可能基于过时的读数：拿到裁定先去 develop 上复现一次；不复现，就先把前提写下来，再谈要不要改。**

> **一个后端缺一条翻译，不会报错，只会把整个计划留在慢层；慢层没人跑，缺口就一直绿着。**

> **一个计划能抓"两个后端悄悄不一致"，它就该在快层；抓不到不一致的计划才配待在慢层。**

> **一个后端缺哪条翻译，用"被拒程序数"报，不用"我碰到的那两个"报；今晚两个是碰到的，剩下多少没人知道。**

> **普查的价值在分母：`674 / 3186` 这个数比"我碰到的那两个"有用得多。**

> **一个后端拒一条翻译是缺口，崩一次是缺陷；缺口按被拒程序数报，缺陷按崩溃数报，两者不在一个优先级上。**

> **同一个缺陷在写出器和前端各崩一次时，先修前端——写出器只影响一个后端，前端影响所有人。**

> **同一个缺口的第二个实例，先照抄第一个的形状，再谈抽象；照抄的时候只抄语义，不抄指针。**

> **一个洞按"程序数"报完，还要按"难点数"报一次：32 个程序可能只是三处贯穿。**

> **表头那一格按"被拒程序数"开工之前，先按"拒绝句数"看一眼——270 个程序可能只是两句话。**