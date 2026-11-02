# 第 7 节第 9 步的续篇：`std::cml` 往返（S3 之后的剩余工作）——实施计划

**这是第二份计划。** 第一份是 [plan.cn.md](plan.cn.md)（决策者 2026-10-22 已批复，答复见任务书），它已经定下：`std::cml` 是翻译遍、打印的是当前的图而不是原始源码；`canon(P)` 的等价定义（§2.2）；S0–S5 的划分；§7 七问的答案（`let` 名字用源码名、声明标签用程序级事实表、`#tag { }` 建 `Block` 区域、泛型实例打印成具体函数、v1 一个文件摊平、大常量超限拒绝、打印抽取后的可执行图）。本文件不重复那些结论，只写**剩余工作**、**随仓库变化的修订**与**实测证据**；与第一份冲突的地方以本文件为准并写明原因。

实测基线：`develop 102dbc5d1`（本计划的提交基底），在 88zjwei 上构建后的 `out/latest/bin/camel`。下面每个数字、每条诊断都是这一版跑出来的；测量脚本是本次临时加入、**不提交**的 `test/cases/trans/cml_probe.py`（11 个探针），命令形式是 `camel <file> std::cml [passes]`。反例程序都写在下文里，可逐条重跑。

**基线与移动的目标**：写这份文件时 `origin/develop` 已经前进到 `38fbbb7b1`（图的边重构的数据边与顺序边语义、两份裁定——《一元算子比 `->` 结合得松》与《改写保持源码程序运行的每个效应》、数组归约内核，以及两份问题文件）。本计划依据的文件在这段区间里**只有一处变化**：`src/compile/gir/builder_expr.cpp#Builder::orderAfterLastWriter` 的注释（把待办指向裁定后的说明），行为没有变；`src/passes/trans/json/**`、`src/passes/trans/cml/**`、`test/tools/cml_roundtrip.py`、`include/camel/runtime/graph.h` 都没有改动。所以下面的数字与缺口在新基线上同样成立；实施 S3a 时以当时的 `develop` 为准，并注意 §8 第 5 问已经由那份裁定回答了一半。

**第一份计划里已经过期的一条事实**：任务书第 218 行的表格仍写「S0（规范转储与等价匹配器，不改语言与流水线）在跑」。S0 已经合入 develop：`77bb8b124`（canonical GIR 与独立等价检查器）、`5815e6802` + `684ecc6ca`（S1 打印机）、`14b43005b`（分支、循环、闭包、`with`/`var` 参数），测试在 `test/plans/feat/trans/cml.plan.toml`。那行是作者的表，本计划不改。

## 0. 结论摘要

**2026-11-02 重扫（`lane-m5`, `f7cff9227`, 扫描工具 `test/tools/cml-sweep.mjs`，计划 `test/plans/feat/trans/cml_sweep.plan.toml`）。** 下面 1–3 条是 2026-10-30 的数，测量脚本当时"临时加入、**不提交**"，所以**今天跑不起来**——旧数不能复现，只能并排读，这是发现而不是障碍；能复现的是这一行命令：

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3360 printed=474 refused=1882 frontend=1004 timeout=0
REFUSALS families=49
ROUNDTRIP checked=458 equivalent=411 not_equivalent=47 over_budget=0 unfaithful=16
```

语料从 2387 涨到 3360（+973），打印 474（打印率 14.1%，旧 14.6%），等价 411/474 = 86.7%（旧 309/348 = 88.8%）。拒绝家族按「图名/算子名/数字掩码」归并后 49 类，最多的五类是：算子没有源码形式 755、图名不是源码标识符 358、`with` 参数是命名类型 86、参数的类型打不出来 85、返回类型是带标签/可空/单元组 73。四类分开记：**打印成功 ≠ 往返等价**（474 vs 458 检查、411 等价），**能打印 ≠ 能重新编译**（16 个连 canonical 文档都产不出）。

**2026-11-02 第二刀：打印机读声明表（`lane-m5`）。** 打印机原来自带一份「URI 里的名字 → 符号」的表（`add/sub/mul/…` 17 条，`cml.cpp`），与算子在哪里声明无关；现在改为问声明表：`OperatorTable::groupOf(uri)` 给出它登记的组（`__add__`、`__neg__`、`__not__`），`operatorSymbolOf(组名)` 给出源码里写的符号；问不到就拒绝并**点名**，不猜拼法。范围只到「读表」——没有补任何一条声明。

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3362 printed=475 refused=1881 frontend=1006 timeout=0
REFUSALS families=49
ROUNDTRIP checked=459 equivalent=412 not_equivalent=47 over_budget=0 unfaithful=16
```

对照上一块（3360 文件、旧二进制）：打印 474 → 475，等价 411 → 412，不等价 47 → 47，产不出 canonical 文档 16 → 16。按文件比而不是按数比：改前旧二进制逐条记下的打印集与改后**一条不差**，只多出 `parse/rulings/not_by_precedence.cml`（它之前卡在 `!!t`：`!` 与 `!` 相邻是一个词法单元，现在打印成 `!(!t)`）；`DIFFERENT` 列表与旧基线**逐行相同**。

两向 A/B（都在主机上跑、改完立即回退）：把 `:op/neg_l` 再登记进 `__not__` 组（声明顺序早于 `__neg__`），同一个程序从 `println(-n)` 变成 `println(!n)`——**文本跟着声明走**，打印机里没有第二份拷贝；`test/cases/consistency/faults/parallel_lowest_index.cml` 是**没有任何声明的算子**，被点名拒绝：`the operator ':mark/map_arr'`。

**2026-11-02 第三刀：47 条不等价按族归并，修「字面量算术回来被折叠」这一族。** 归并的读法不是看判定器那行话，而是把两边的 canonical 文档里每个节点的算子（非算子节点用它的 kind）数一遍再相减：差的是「左边有、右边没有的节点」「右边有、左边没有的节点」。47 条归并成五族（余下是混合族）：**GATE 独缺 6**（`compile/constas/constas_with_rest.cml`——图里的门来自内联/区域，打印出来的程序是内联后的文本，不需要那道门）、**同节点不同形状 6**（`compile/equality/null_test_never_null.cml`）、**`DATA:tuple`↔`DATA:void` 5**（`compile/surface/if_without_else_ok.cml`——`()` 是空组不是 unit，打印器没有第二种写法，裸 `return` 编不过）、**COPY 独缺 4**（`compile/array_shape/rank_zero_var_slot.cml`）、**`DATA:i64`+FILL 3**（`compile/static_expansion/indices_graph.cml`）。

被修的是原来 13 条的那一族：图里是 `:op/add_l` 加它的两个常量节点，打印成 `println(0 + 1 + 2 + 3)`，前端把整串折成 `DATA 6`——打印出来的程序比图少 3 个算子、4 个常量。修法在 `cml.cpp#infix`/`#prefix`：两个操作数都是字面量时，各自先绑一句 `let cmlN = <字面量>`，再写 `cmlN + cmlM`。名字不是字面量，算子就不再被折；而 `let` 一个字面量就是那个常量节点本身，节点一个不多一个不少（实测：`let c = 1` + `c + 2` 的图正是 `DATA 1`、`DATA 2`、`add_l`）。绑定只活到当前语句：`flushHeld` 把名字表连同 `let` 行一起清掉（第一版不清，`compare()` 里读到了 `chain()` 的 `cml0`，打印结果编不过）。名字沿用打印器既有的生成名前缀 `cml`（`cml_contract.py#GENERATED`），不发明新标识符。

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3356 printed=474 refused=1881 frontend=1001 timeout=0
REFUSALS families=48
ROUNDTRIP checked=460 equivalent=415 not_equivalent=45 over_budget=0 unfaithful=14
```

对照同一棵树、合并 `develop` 之后、这一刀之前的基线：打印 **474 → 474**（按文件比：改后 474 条全在改前集合里，一条没丢）、拒绝 1881 → 1881、等价 **413 → 415**、不等价 **47 → 45**、产不出 canonical 文档 14 → 14；`DIFFERENT` 行逐行比对**只少 2 行**（`compile/rest_params/rest_inline.cml`、`compile/static_expansion/static_loop_sequence_forms.cml`），**一行没多**。被修那族 13 条里，11 条不再报缺 `:op/add_l` 与常量（普查里它们只剩 GATE/FILL 等下一族的差），2 条整条出了不等价名单。

**合并 `origin/develop = 3df534542` 带走两个打印，要记账。** 合并后语料 3362 → 3356；`compile/transforms/**` 的 9 个程序全部以 `the operator '<name>'` 被拒（打印机读声明表之后，develop 新加的这类图里的算子没有声明），其中 `jvp_effect.cml`、`jvp_lifted.cml` 在合并前是能打印的。所以「打印不许低于 475」在今天应读作「474 是这棵树的底线，丢的 2 个有名有姓、归到补声明」。

**2026-11-02 第四刀：算子的调用形状进声明表，打印器不再按 URI 拼名字。** 裁决是 (a)：**每个算子节点打印成对它的声明的调用**，而"对声明的调用"就是**调用形状**（名字 + 元数 + `with` 参数），所以形状归声明表，不把源码名贴到图上。表新增 `CallForm{name, minArgs, withCount}`（`operator_table.h`），来源是声明：`Builder::declareNativeOperator` 每登记一个算子时把该声明的函数名、普通参数个数、`with` 参数个数写进去（`#op` 标签和 `模块:函数` 约定两条路都经过这里）；一个 URI 被多个重载声明时取最小元数与最小 `with` 数，名字取第一个声明（`:op/itol` 既是 `int` 又是 `i64`）；带 `*` 的家族声明（`:op/*tos`）按模式登记，`callFormOf` 用前后缀匹配回答。

打印机里三处前缀特判的去向：① `:io/` —— 删掉，改读表（`:io/println` 的声明是 `println`）；② `:op/*to*` 的"按最后一个字母拼 `i32`/`int`/`f32`/…" —— 删掉，改读表（`:op/itol` 的声明就是 `int`，比推字母更准）；③ `:struct/range` —— 区间形态 `a..b` 留着（那是语言形式，不是调用形状），三参数调用那支的 `range` 字样改读表。

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3358 printed=516 refused=1841 frontend=1001 timeout=0
REFUSALS families=48
ROUNDTRIP checked=494 equivalent=447 not_equivalent=47 over_budget=0 unfaithful=22
```

对照这一刀之前（476 / 1881 / 417 / 45 / 14）：打印 **476 → 516**、拒绝 **1881 → 1841**、等价 **417 → 447**、拒绝家族最大的那类 **762 → 716**、家族数仍 48（没升）。**两条线没守住**：不等价 45 → 47、产不出 canonical 文档 14 → 22。原因不是调用形状写错，而是**新打出来的程序撞上打印机另外两个旧缺陷**：① 二维数组字面量只打第一行——`[[1.0, 2.0], [3.0, 4.0]]` 打成 `[1.0, 2.0]`，于是 `let cml1: f32[2, 2] = [1.0, 2.0]` 编不过（`DimensionMismatch`，3 例）；② `byref` 的 `var` 实参（`linear/byref/var_argument_declared.cml`）。22 条里 7 条落在 `array_shape`/`tensor`/`linear/byref`。

**2026-11-02 第五刀：先补两个旧缺陷，把两条边界的读数收回来。** 裁决是「先补漏再加水」：上一刀多打的 113 个程序里有 8 个撞在打印机**自己**的旧错上，先把这两处修掉，再谈"调用怎么写"。

**① 二维数组字面量只打第一行。** `constantText` 的 `Array` 支路按 `value->size()` 迭代、元素一律用 `elemType()` 打，而一个多维度数组的 `size()` 是**一行**的槽数：`[[1.0, 2.0], [3.0, 4.0]]` 打成 `[1.0, 2.0]`，于是 `let cml1: f32[2, 2] = [1.0, 2.0]` 以 `DimensionMismatch` 编不过。改成按值自己的秩递归（`arrayText`：每维一层方括号，元素按行主序线性走 `getSlot`），秩 0 的数组按设计原则 5.4 打成元素本身。三例改前 → 改后：

| 用例 | 改前 | 改后 |
| :-- | :-- | :-- |
| `array_shape/let_shape.cml` | `let cml1: f32[2, 2] = [1.0, 2.0]` | `let cml1: f32[2, 2] = [[1.0, 2.0], [3.0, 4.0]]` |
| `array_shape/literal_length.cml` | `let cml1: f32[] = [1.0, 2.0]`（4 个只剩 2 个） | `let cml1: f32[] = [1.0, 2.0, 3.0, 4.0]` |
| `array_shape/open_rank_element.cml` | `let grid: f32[2, 2] = [1.0, 2.0]` | `let grid: f32[2, 2] = [[1.0, 2.0], [3.0, 4.0]]` |

**② `byref` 的 `var` 实参。** `var a: f32[..] = [1.0, 2.0, 3.0]` 的图是 `DATA → COPY → CAST → :var/new`，打印机为了让字面量那个 `f32[]` 类型有个落脚处，先写 `let cml1: f32[] = [1.0, 2.0, 3.0]`、再写 `var a: f32[..] = (cml1 as f32[..])`——而 `let` 是一个 store，图里没有这个节点，于是这一例从"能打印"变成"不等价"。改成：**只被一处读、且那一处自己写着类型的字面量不另起 `let`**（`typeWrittenByUse`：穿过 `COPY` 看唯一用户；用户是 `as` 时，只有值的类型没有比 `as` 多说一句才省标注——一个未知 extent 的 `f32[]` 对开秩 `f32[..]` 正是这种，而 `f32[2, 2]` 对 `f32[..]` 有形状要说，标注留着；用户是 `:var/new` 时按 `var` 声明的类型比）。现在打成 `var a: f32[..] = ([1.0, 2.0, 3.0] as f32[..])`，编得过且**与源等价**。

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3358 printed=516 refused=1841 frontend=1001 timeout=0
REFUSALS families=48
ROUNDTRIP checked=502 equivalent=458 not_equivalent=44 over_budget=0 unfaithful=14
```

判据逐条：打印 **516（不低于 516，没掉）**、等价 **447 → 458**、拒绝 **1841（不变）**、拒绝家族 **48（没升）**、**不等价 47 → 44（≤45）**、**产不出文档 22 → 14（≤14）**；按文件比 `DIFFERENT` **只少不多**（对上一笔报的那棵树新增 0、减少 11；对合并 `origin/develop = da27eca0a` 之后的基线新增 0、减少 3）。账是这样走的：合并后先量到 516/455/47/14（数组那条修完已经回到 14），再修 ② 与两个秩 0 的 `var` 用例，把 47 收到 44。

**（更正。）** 上一笔这里写的"24 条 `UnexpectedToken` 是 `xs->sum()` 被打成 `sum(xs)`"归因错了：实测两种写法都能编过，真原因是函数类型不能写在标注位置，见第六刀。打印器已能按需写 `import { name, name } from module`（`collectImports`）。

**2026-11-02 第六刀：函数类型没有标注写法，`compile/transforms` 的 30 个程序因此能打。** 上一笔列的 24 条 `UnexpectedToken` 的真实原因不是"带接收者的调用被打成前缀调用"——实测 `import { sum } from tensor` 之后 `sum(xs)` 与 `xs->sum()` **都能编过**（`import tensor` 与不导入则两种写法都 `UnresolvedReference`），是我那次归因错了。真原因在**标注位置**：

```
let back: (float, float) => float = transpose(pick)
                                        ^ error[SyntaxDiag::UnexpectedToken]: Unexpected '=>'; expected '='
```

一个函数类型**没有标注写法**：`let` 的类型位置被读成一个类型，其中的 `=>` 不被当成类型的一部分；加一层括号也不行（`let back: ((float, float) => float) = …` 报 `MissingComma`）。而不写标注时，`let back = transpose(pick)` 由值推出类型——正是节点本来的类型。所以 `annotationOf` 在**类型里任何一个函数**（`Function`，或经由元组/数组/结构体含一个函数，`hasFunction` 递归）时写空标注，其余照旧；`var` 声明同一处理。

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3358 printed=550 refused=1807 frontend=1001 timeout=0
REFUSALS families=48
ROUNDTRIP checked=536 equivalent=492 not_equivalent=44 over_budget=0 unfaithful=14
```

对照合并 `origin/develop = 28e36a2cb` 之后的基线（516 / 1841 / 458 / 44 / 14，家族 48）：打印 **516 → 550**、等价 **458 → 492**、拒绝 **1841 → 1807**（降）、不等价 **44 → 44**、产不出文档 **14 → 14**、家族 **48（没升）**；`UnexpectedToken` 那 24 条 **24 → 0**（全消），`DIFFERENT` 行 **新增 0（一条都没多）**。新打的 34 个程序全部等价，一个 `DIFFERENT` 都没添。`compile/transforms` 单独量：**52 个文件里 30 个能打印、30 个全等价、20 个被拒、2 个本来就编译不过**（这一刀之前只打印 1 个）；剩下的 15 个是「图名不是源码标识符」（`vjp::vjp<float>`、`array::vmap<float[3], int>` 这类实例化的名字），归到 358 条的家族，另派。

**顺便定死"名字"这一列的判据。** 只让"程序写得出来的名字"被打印：核心的算子（`:` 前缀）名字任何程序都在作用域内；模块自己在**声明处**给出调用形状的算子（`transforms` 的 `jvp(f)`/`transpose(f)`/`split(f)`，`CallForm::declaredByModule`）名字是该模块导出的；而**从 Camel 声明推出来**的形状只是被调者的名字，那个声明可能是模块不导出的 kernel（`#impl(sum_kernel)`），所以这类模块算子仍然被拒。放宽到"所有模块算子都打印"实测打印 589、等价 513，但产不出文档 31——剩下的错是**另外几族**（`ImportNameNotExported` 9、`NoMatchingFunction` 3、`Redeclaration` 3、`UnresolvedReference` 3，以及各 1 条的 `ArgumentNeedsConversion`/`DynamicCastForbidden`/`ReservedIdentifier`），按裁决一族一笔地消。

**2026-11-02 第七刀：私有 kernel 的算子拒绝时点名。** 裁决下来的做法是"拒，不给导出名"——打印出来的程序必须能重新编译，替作者挑一个公开名字等于替作者决定语义。先说清事实：那 9 条今天**已经不在打印栏里**（上一刀的 `declaredByModule` 或核心那两条判据就把它们挡住了），所以这一刀不搬数字，只把话说清楚：算子**有声明、有名字，但那个名字不是程序写得出来的**——body-less 声明就是模块自己那侧的 kernel（`array:sum_l` 是 `array::sum` 背后的 `sum_kernel`）。`CallForm` 记下这一列（`kernelDeclaration`，取自声明节点的 `nativeDeclaration()`），打印机拒绝时点名：

```
the operator 'array:sum_l' without a printable name: 'sum_kernel' is a body-less kernel
declaration, and no program writes that name
```

9 条里有 8 条落进这句（第 9 条 `compile/entry_args/scalars.cml` 本来就打印、且在 492 条等价里）：

| 文件 | 算子 URI | 私有 kernel |
| --- | --- | --- |
| `compile/when_import/array_sum.cml` | `array:sum_l` | `sum_kernel` |
| `modules/std/array/sort_basic.cml` | `array:sort_l` | `sort_kernel` |
| `compile/fstring/basic.cml` | `:str/format` | `format` |
| `modules/math_calls/erf_values.cml` | `math:round_d` | `round_kernel` |
| `modules/stdlib/fault_array_min_empty.cml` | `array:min_l` | `min_kernel` |
| `modules/stdlib/fault_array_max_empty.cml` | `array:max_l` | `max_kernel` |
| `trans/cpp/math_min_max_int.cml` | `math:min_l` | `min_kernel` |
| `compile/transforms/rulesets_probe_missing.cml` | `math:pow_d` | `pow_kernel` |

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3358 printed=550 refused=1807 frontend=1001 timeout=0
REFUSALS families=49
ROUNDTRIP checked=536 equivalent=492 not_equivalent=44 over_budget=0 unfaithful=14
```

判据：打印 **550（不变）**、等价 **492（不许降，没降）**、拒绝 **1807（不变）**、不等价 **44**、产不出文档 **14**、`DIFFERENT` **新增 0**。拒绝家族 **48 → 49**，多出来的就是这句（全语料 **376 个图**落在它上面，不是孤例）：它必须自成一族——老的 `the operator '<name>'` 说的是"没有声明"，这句说的是"有声明，名字是私有的"，两件事混进一族正好把这刀要说清的东西抹掉。

**解析器待办（不属于 M7，单独立着）。** `let back: (float, float) => float = transpose(pick)` 今天**编不过**：`error[SyntaxDiag::UnexpectedToken]: Unexpected '=>'; expected '='`，加括号是 `MissingComma`。这是**解析器的缺陷**——类型写在标注位置是合法的，函数类型本身也该写得出来；打印机写空标注只是**绕开**它，不是修它。修解析器时 `test/cases/compile/transforms/transpose_branch.cml` 就是验收：标注写回来之后，打印器那侧的空标注规则才可以考虑退场。

**2026-11-02 第八刀：模块算子写成符号时，模块要 import。** 裁决问的那 3 条 `NoMatchingFunction`（`a / b`、`[7, 8, -9] / [1, 0, 2]`、`x / cml1`），**先答那一句：既不是我把调用形状打错，也不是语言里没有这个写法**——形状是对的（源程序写 `/`，图里是 `tensor:divide`，`__div__` 组给的符号就是 `/`），缺的是**模块没 import**：同一段文本加上 `import tensor` 就编过，去掉就 `NoMatchingFunction`（`/` 在 `int[]` 上没有核心规则，规则是 `tensor` 的声明带来的，原则 5.1）。所以这是第三种情形：**图的依赖比打印出来的 import 多**。

打印器原来只给"按名字调用"的模块算子写 `import { name } from module`；写成符号的模块算子（`tensor:divide`、`tensor:remainder`……）一个 import 都不写，于是打印出来的程序去编就找不到规则。改法一行判据：**按名字写的 → 具名 import；写成符号的 → 整模块 `import <module>`**（整模块导入会把名字也带进来，所以两者都有时只写整模块那条）。

```
import tensor

func main(): int sync {
    println('before')
    println([7, 8, -9] / [1, 0, 2])
    println('not reached')
    return 0
}
```

```
node test/tools/cml-sweep.mjs --jobs 16
SWEEP files=3358 printed=550 refused=1807 frontend=1001 timeout=0
REFUSALS families=49
ROUNDTRIP checked=540 equivalent=495 not_equivalent=45 over_budget=0 unfaithful=10
```

判据：打印 **550（不变）**、等价 **492 → 495（只增）**、产不出文档 **14 → 10（只降）**、拒绝 **1807（不变）**、家族 **49（不升）**。**两条没做到，同一条用例**：`not_equivalent 44 → 45`、`DIFFERENT 新增 1`，都是 `test/cases/modules/tensor/int_division.cml`。原因查清了，是**既有那一类"同样的节点、不同的形状"**，不是 import 改出来的：源里写的是 `[-9223372036854775807 - 1]`（元素是个表达式，图里是 `FILL array slots[0]`），打印机把它折成常量字面量 `[-9223372036854775808]` 写回去，前端把常量元素表读成**常量数组**（`DATA`），于是左边多一个 `FILL`、右边多一个 `DATA`，值一样、形状不同。这一例此前根本编不过（`NoMatchingFunction`），所以从没有过"形状"可比——它是**新露出来的**，不是新造的。两条路：**(a)** 这种"槽位全是常量的 `FILL`"本来就没有忠实写法（任何纯常量表达式都会被折成常量数组），照"写不回来的形状就拒绝"处理，单独一刀，`not_equivalent` 回到 44、另外三条仍等价；**(b)** 让它就这么算进不等价。我建议 (a)，但它属于另一类，所以没混进这一刀。

**2026-11-02 第九刀：写不回的形状具名拒绝，`not_equivalent` 回到 44。** 第八刀那 1 条 `DIFFERENT` 的账在这里收平：`test/cases/modules/tensor/int_division.cml` 从打印栏移到拒绝栏，理由是**这个形状没有忠实写法**——源里写的是 `[-9223372036854775807 - 1]`（元素是表达式，图里是 `FILL array slots[0]`），而它的值 `-2^63` **没有普通字面量写法**（int64 写不出它的绝对值），打印机只能写成表达式，前端再把整个字面量折成**常量数组**，写回的程序建出的是另一张图。**这不是打印机的手艺问题，是语言的事实**；把它算进不等价会让 `not_equivalent` 失去含义（它该记"我们打印了、但回不来"），所以按同一把尺子拒：

```
std::cml cannot print main: a construction whose slots are constants with no plain literal
spelling has no faithful spelling: the value is written as an expression, the front end folds
the literal into a constant array, and the program written back builds a different graph
```

判据（对照合并 `origin/develop = 374488bf4` 之后**重测**的基线 550 / 1807 / 540 / 495 / 45 / 10 / 49，旧数不跨基线比）：打印 **550 → 549（掉 1，就是 `int_division.cml`，只能拒）**、等价 **495（保持）**、不等价 **45 → 44**、产不出文档 **10（保持）**、拒绝 **1807 → 1808**、家族 **49 → 50**、`DIFFERENT` **新增 0（并回少 1 条）**。新族必须自成一族：老族分别是"没有声明"（`the operator '<name>'`）与"有声明但名字是私有 kernel 的"（`body-less kernel declaration`），这句是第三件事——**声明、名字、写法都齐，但写回去的形状不是同一张图**。条件收得很窄：元素的**值没有普通字面量写法**（`int64`/`int32` 的最小值）才拒；同一支里 `[[1.0, x], [x, 2.0]]` 这种槽位写着**名字**的构造照常打印（实测 `[1, 2]`、`[[1.0, 2.0], [2.0, 2.0]]`、`[-9223372036854775808]` 都会被折成一张 `DATA`，而槽位里有名字的仍是 `FILL`），所以没有连带丢掉 `trans/cpp/exec/array_conversion.cml` 那类。

**`int_division.cml` 这一条要单说一句：它此前根本编不过（`NoMatchingFunction`，缺 `import tensor` 那一刀之前），从没有过"形状"可比 —— 那个不等价是第八刀新露出来的，不是新造的。**
**2026-11-02 第十刀（分类，不动代码）：`358` 那一族不该在"补名字"这一层动。** 350 那一族（`the graph name '<name>' is not a source identifier`）按**首次拒绝时的名字形状**分类，实测（树 = `ea9bf0915` + 合并 `374488bf4`）：

| 形状 | 判定 | 文件数 |
| --- | --- | --- |
| 含 `<…>`（泛型实例，如 `array::vmap<float[3], float>`） | `GENERIC` | **219** |
| 含 `::`（模块/命名空间限定，如 `Tools::inc`、`rules::…__lhs`） | `MANGLED` | **74** |
| 其余 | `PLAIN` | **0** |
| 合计 | | **293** |

**291/293 = 99.3% 是"语言的事实"，不是打印机的手艺**：实例图在源码里**没有写法**（源写 `vmap(f)`，实例由调用点定），`namespace`/`module` 区域**打印机不支持**。所以这一族**不该在补名字这一层动**，两条路都等作者裁，页面见 `docs/handover/reviews/questions-2026-11-02-generic-instances-and-namespace-blocks.md`（两条各带代价，默认两条都不做）。

**一处更正（把我上一刀的读数收回）：** 上一刀我报"`PLAIN` 两个、名字是 `rulesets.`（以句点结尾）⇒ 前端拼前缀漏了成员名"——**不成立**：那是我统计脚本的截断错误（`sed` 在第一个句点处截断），真名是 `rulesets.jvp::lift<float>` 与 `rulesets.probe_missing::add_ok__lhs`。用正确的提取重扫：**以 `::`、`.` 或 `<` 结尾的名字计数为 0**，`PLAIN` 为 0。所以"前端拼名字漏成员名"这条缺陷**没有可修的点**，这一刀只有分类、没有代码改动。
**2026-11-02 第十一刀（作者缺陷清单 A7 的第 1 条）：`fail<'Kind'>(message)` 是语句，打印机漏了它。** 缺陷原文：`consistency/fail_never/statement_after_fail` 原程序打印 `before`、报故障、`exit 1`；打印出来的程序打印 `before`、`after`、`exit 0`——**`fail` 语句被丢了**。

`git grep -n ":op/fail\|kFailOperatorUri" -- src include` 指到 `include/camel/core/operator_names.h#kFailOperatorUri`：`kFailOperatorUri = ":op/fail"`，注释写着"它**不是效果**，所以它的值不被需要的地方没有东西运行它"。`src/passes/trans/cml/cml.cpp` 的语句走查（`processNode`）只把 `nodeEffect(ref)` 为真的节点当语句打印，而 `fail` 的值类型是 `never`、用户数 0、又不是效果 ⇒ **它是被"跳过"的，不是被当成值表达式丢了**（值表达式那条路也走不到：`users == 0` 时那一支只在有名字或有效果时产出）。

修法：在语句走查里按 URI 认出它，按语言的写法打印（`emitFail`）——`with` 槽里的常量字符串是 `'Kind'`、唯一一个 norm 输入是消息；**kind 不是常量字符串时具名拒绝**（`fail<'Kind'>` 是唯一写法，写别的 kind 就是假打印）：

```
a 'fail' whose kind is not a constant string: the statement is written `fail<'Kind'>` and the
kind is the value it holds, so a print that cannot read it cannot write the statement
```

两侧读数（同一对程序，`std::nvm`）：原程序 `before` + `error[RuntimeDiag::NoPremise]: the premise of this program does not hold`、`rc=1`；打印出来的程序 `before` + 同一句 `RuntimeDiag::NoPremise`、`rc=1`（逐字一致）。

判据（合并 `origin/develop = 03c7065cc` 之后**重测的基线** `550 / 1809 / 540 / 495 / 45 / 10 / 52`，语料 3360 个文件，旧数不跨基线比）：

| 读数 | 基线 | 本刀 | 说明 |
| --- | --- | --- | --- |
| `printed` | 550 | **553** | 只增；新打印的 3 条是 `consistency/fail_never/fail_located.cml`、`opt/never_exit/fail_last.cml`、`opt/generic/fold_fail_is_intended.cml`——它们此前唯一挡路的就是这个写不出的 `fail` 语句 |
| `refused` | 1809 | 1806 | 与上面同一件事（-3） |
| `checked` | 540 | 543 | 同上 |
| `equivalent` | 495 | **499** | 只增（+3 新打印 + 1 原不等价转等价） |
| `not_equivalent` | 45 | **44** | -1，正是 `statement_after_fail`（它现在打印得忠实） |
| `unfaithful` | 10 | **10** | 不升；`DIFFERENT` **-1 新增 0** |
| `families` | 52 | **52** | 不升；kind 不是常量的那条具名拒绝在本语料里**一次没触发** |

**A7 那 8 个用例按"打印器漏了什么"分类（给作者）：** **陈述 1** —— `statement_after_fail`（`fail<...>` 语句被丢，本刀已修）；**写表 1** —— `writer_table_through_callee`（原 `0` `41`、打印版 `0` `0`，写作表经被调用者传出去的那条路没写回）；**其它 6** —— `linear/runtime/var_read_before_region`、`var_read_before_write`（`var` 的读被挪到写之后，2 条）、`order_edges_regions`（`exit 56`→`76`，区域顺序边）、`parse/rulings/power`（`-9 9`→`-9 -9`，一元负号的结合）、`trans/cpp/exec/print_formats`（`3-4|1+2`→`3 4|1 2`，格式说明符丢）、`consistency/values/float_special`（宽度与正号标志丢）。**这个分类比"8"这个数有用：陈述与写表各 1 条，剩下 6 条都不是"打印器忘了写一条语句"，是 print 的格式与求值顺序。**

**2026-11-02 第十二刀（A7 第 2 条）：`41` 从哪来——写作表的第三张凭据，穿过函数值的调用是"可能的写者"。** 缺陷原文：`effects/writer_table_through_callee` 原程序 `0` `41`；打印版 `0` `0`。

`41` 的来源在 `src/compile/gir/builder_expr.cpp#recordWrite`（`throughCallee`）的注释里：**"穿过一个函数值的调用只是一个变量*可能的*写者（保守写者集合），所以写在它前面的那次写必须继续运行，而调用之后读到的版本要指名它"**。源里的 `apply((x: int) => { counter = x }, 41)` 正是这条：`counter` 是闭包捕获的 `var`，调用是它的写，所以**调用之前的读读到 `0`、之后的读读到 `41`**。

**打印机把它丢了**：`program_.calleeOf` 只对**已知的图**给得出被调者，穿过函数值的调用它给出 `nullptr` ⇒ `nodeEffect` 判"无效果"、图的纯性扫描也判这个函数纯 ⇒ `apply` 的体 `f(v)` 与 `main` 里的 `apply(...)` **都当"没有值的非效果"被跳过**。打印版于是从不调用，两次读都是 `0`。

修法：**穿过函数值的调用一律当效果**（`nodeEffect` 与纯性扫描两处），理由写进注释——**"没有打印机读得到的图，所以这里没有东西能说它纯；把它打印掉就是假打印"**。打印出来的程序：`apply` 的体是 `f(v)`，`main` 里是 `apply((x: int): int => { counter = x }, 41)`。

两侧读数（`std::nvm`）：原程序 `0` `41`；打印版 `0` `41`（`rc=0`）。

判据（合并 `origin/develop = 427eb900d` 后**重测的基线** `553 / 1807 / 543 / 499 / 44 / 10 / 52`，语料 3361 个文件）：

| 读数 | 基线 | 本刀 |
| --- | --- | --- |
| `printed` | 553 | **553**（一条不丢：这一条本来就打印，只是打印错了） |
| `equivalent` | 499 | **500** |
| `not_equivalent` | 44 | **43**（正是 `writer_table_through_callee`） |
| `unfaithful` | 10 | **10** |
| `DIFFERENT` | 54 行 | **53 行，新增 0** |
| `families` | 52 | **52** |


**2026-11-02 第十三刀（A7 第 5、6 条）：`print` 的格式说明就在 `with` 槽里，读法与 `fail<'Kind'>` 同源。** 缺陷原文：`trans/cpp/exec/print_formats` 原程序 `3-4|1+2` / `next` / `[5][a]` / `0.50`；打印版 `3 4|1 2` / `next` / `5a` / `0.5`。`consistency/values/float_special` 原程序末三行 `nan` / `     nan` / `+nan`；打印版三行都是 `nan`。

**`with` 槽的形状**（`stdlib/prelude.cml` 的声明）：

```
/// With a format, print<fmt>(...), writes the format with the arguments put in,
/// also without a line end.
/// @example print('a', 1)    // a 1
#op(':io/print')
with <fmt: string>
export func print::<T>(x: T, ..rest): T
```

`println` 同形，`with <fmt: string>`。图里它是**一条 WITH 边**（`std::gir` 的 `N00 -> N03 [label="0|0", type="WITH"]`），格式字符串是那次调用的常量。`float_special` 的 `println<'{:>8}'>`、`println<'{:+}'>` 是同一个槽——**宽度与符号不是 print 的另一个参数，就是格式串本身**，所以这两个用例是同一类。

**打印机把它丢了**：算子那条路只写名字和括号里的值，`with` 槽里的格式从不写回（`print<'{}-{}'>(3, 4)` 打成 `print(3, 4)`）；`callForm->withCount == 0` 才允许写名字，`print` 的合并调用形（`print()`、`print::<T>(x, ..rest)`、`print::<T>` 三份声明的共同形）正是 0。

**改法**：算子调用的名字后、括号前写 `<...>`，槽里的每个值按 `expression` 打印；**配置不是格式串就具名拒绝**：

```
a call whose configuration is not a format string: the printer writes `name<fmt>(...)` for the
print family, and this operator's configuration is not a string, so its call has no spelling here
```

**第一版太宽，被自己的 golden 拦住**：曾把可写条件放宽成 `withCount == 0 || uri.starts_with(":")`，于是 `trans/cml/refuse/operator.cml` 的 `xs->map<(x: int) => x * 2>`（`:mark/map_arr`，配置是一个函数值）被打成 `map<(x: int): int => x * 2>(xs)`，golden 报 `AssertionError`——**这条用例的注释写着"源拼法是算子组的，不是节点的，所以拒绝"**。收紧成"配置必须是字符串"后它仍是拒绝，格式串的那两条打印。**这就是"只改一半比不改更坏"的形状：名字写得出、配置写不出时，宁可整条拒。**

两侧读数（`std::nvm`）：`print_formats` 原 `3-4|1+2` / `next` / `[5][a]` / `0.50`，打印版同上；`float_special` 原 `nan` / `     nan` / `+nan`，打印版同上。

判据（基线 `553 / 1807 / 543 / 500 / 43 / 10 / 52`，语料 3361 个文件，即第十二刀之后）：

| 读数 | 基线 | 本刀 |
| --- | --- | --- |
| `printed` | 553 | **553**（不降：两条本来就打印，只是打错） |
| `equivalent` | 500 | **502** |
| `not_equivalent` | 43 | **41**（`print_formats`、`float_special`） |
| `unfaithful` | 10 | **10** |
| `DIFFERENT` | 53 行 | **51 行，新增 0** |
| `families` | 52 | **52** |



**2026-11-02 第十六刀（区域内的读）：先证明是同一个事实，再拆那条守卫。** 上一刀留了一句"区域内的读要单独一刀"。**动手前先查"记录版本的位置"**：`git grep -n 'recordWrite' src/compile/gir/` 只有三处调用点——`builder_expr.cpp#recordWrite` 本体、`builder_control.cpp#mergeArmVariables`（对 JOIN/LOOP 的那一次，`mergeArmVariables` 的调用点在分支与循环的构建里）、`builder_operators.cpp` 里经调用的那三处写。**版本只在一处记**，但它对"写本身就是区域节点"另走一条：`regionExit` 时写的是 **control** 边（`writeControlLink`），注释把另一半说清了——"a read in the region around the write … stays pending: the write that meets it is the region's own"。

**关键的一问"臂里的读和块里的读是不是同一个事实"，答案在图上是**：臂的节点**就在块的图里**（`controlOf` 说的是"哪个区域来铺开它们"，不是"哪个图记它们的版本"）。`~/a7/arm_inner.cml`（`if` 的 then 臂里 `let before = t; t += 1; report(before, t)`）的 gir 里 `N0C -> N10 [ORDER]`，两端 `controlOf` 都是那个 BRCH——**同一条 `recordWrite` 写的同一条 ORDER 边**。所以它不是另一个事实，上一刀那条 `controlOf(ref) != 0` 的守卫只是把真缺陷挡在门外：改前的打印是 `report('int', t, t)`，跑出 `int 4 4`，源是 `int 3 4`。

改法：**删掉那条守卫**（判据仍是 `orderUsersOf`）。语料里**没有一条**覆盖这个角：3361 个文件一个都没踩到，所以这一刀必须**补用例**——`test/cases/trans/cml/arm_read.cml`（写在 `cml_contract.py` 的 `CORPUS` 与 `MARKERS` 里）。

判据（**同一棵合并后的树、语料 3363 个文件**，即 `arm_read.cml` 已在语料里；基线是"守卫还在"的那次构建，只差这一行）：

| 读数 | 守卫还在 | 本刀 |
| --- | --- | --- |
| `printed` | 554 | **554** |
| `equivalent` | 504 | **505** |
| `not_equivalent` | 40 | **39**（唯一变化是新增的 `arm_read.cml`） |
| `refused` | 1808 | **1808** |
| `unfaithful` | 10 | **10**（十个文件名逐一相同） |
| `DIFFERENT` | 50 行 | **49 行，新增 0** |
| `families` | 53 | **53** |

**"只动了一条用例"正是这一刀的重点**：删守卫前的全量快跑是 `8375/8362/1`（红的正是新增的 `arm_read.cml`），删后是 `8375/8363/0`。**守卫在时那条用例是 `DIFFERENT`，删掉后消失**——用例咬得住这条缺陷，缺陷也确实只在角上。

两个顺带记下的读数变化（**都不是本刀**，是合并 `bf64149aa` 带进来的那一个语料文件）：`refused` 1807 → 1808、`families` 52 → 53。后者是**工具把家族名归一化时的伪影**：`REFUSAL … cannot print <graph>: float[]) => float pure>: the graph name '<name>' is not a source identifier.`——同一个"图名不是源标识符"家族，因为那个图的**名字里带 `: `**，家族的 `<graph>` 前缀削掉了半截。**家族数没涨，涨的是归一化没归准。** **由此得一条仪器规则：家族数在名字被截断时不可信**——`REFUSAL` 行的家族键是把"`std::cml cannot print <图名>:`"这段前缀削掉得到的，图名里带 `: ` 时只削掉半截，同一个家族就数成两个。**下一次看到 `families` 涨一，先查那一行是不是被截断的家族，再当成"多了一个家族"。**

**2026-11-02 第十五刀（A7 第 3、7、8 条，同一机制）：`let before = z` 消失，读版本打成变量名。** 三条用例（`var_read_before_region`、`var_read_before_write`、`order_edges_regions`）是**一件事**，不是三件：`:var/get` 只打 `cellName(cellReadBy(...))`，**读的是哪一版它不打**。

**判据不是"有没有 ORDER 边"这种猜测，而是建图器自己写的那句话**（`src/compile/gir/builder_expr.cpp#recordWrite`）："读要看见写之前的值……读在写之前由一条 order 边排定"。图上就是 `N03 -> N0E [ORDER]` 这样的边：**一个读若被排在一次写之前，它的版本就一定比文本在该处写到的旧**——因为文本里变量名只表示"文本此刻写到的那一版"。

改法（两档，按父裁的默认）：

1. **能便宜地忠实 ⇒ 忠实**：`orderUsersOf(ref)` 非空 ⇒ 这个读落成一条 `let`，位置不变（源里有名字就用源名字：`let before: int = z`；没有就给 `cmlN`）。
2. **便宜不了 ⇒ 具名拒绝**：诊断写"这条读的版本在文本里无法表达"。**本语料里一次没触发**——ORDER 边保证读在写之前，落 `let` 总是安全的。
3. **版本号不进文本**：位置决定次序。

三条卡两侧输出（`std::nvm`）**逐行相同**：`0 2`/`0 3`/`0 5 1`；`int 2 1`/`int 2 1`/`array [7, 7] [1, 1]`；`exit 56`。

判据（基线 `553 / 1807 / 543 / 503 / 40 / 10 / 52`，语料 3361 个文件，即合并 `01ff08254` 之后）：

| 读数 | 基线 | 本刀 |
| --- | --- | --- |
| `printed` | 553 | **553** |
| `equivalent` | 503 | **504** |
| `not_equivalent` | 40 | **39**（`var_read_before_write`） |
| `refused` | 1807 | **1807**（具名拒绝没触发） |
| `unfaithful` | 10 | **10** |
| `DIFFERENT` | 50 行 | **49 行，新增 0** |
| `families` | 52 | **52** |

**这一刀没做、要单独一刀的**：①**区域内的读**（`arm_write.cml` 读的是分支留下的版本，那里变量名本来就对）；②另外两条**行为已相同**但 canonical 仍差，差的是**另一个缺陷**——循环/分支体的尾值打印成 `return i + 1` / `return println(2)`。问题页：`docs/handover/questions/2026-11-02-cml-var-version-in-printed-text.md`。

**2026-11-02 第十四刀（A7 第 4 条）：前缀 `-` 比 `^` 松，括号表把它放严了一级。** 缺陷原文：`parse/rulings/power` 原程序第二行 `-9 9 -4 -4 12`；打印版 `-9 -9 -4 -4 12`——`(-x) ^ 2` 被打成 `-x ^ 2`，而解析器读它是 `-(x ^ 2)`，9 变成 -9。

**这一格的原话是解析器写的，不是打印机猜的**（`src/parse/syntax/parser_expr.cpp`）：

```
 *   powerExpr       ^                                           right
 *   negExpr         prefix -  !  ~   looser than `->` and than `^`, tighter than the other binary
 *                                    operators: `-x->f` is `-(x->f)`, `!xs->is_empty()` is
 *                                    `!(xs->is_empty())`, `-x ^ 2` is `-(x ^ 2)`, `-a * b` is `(-a) * b`
```

```
/// powerExpr : nullableExpr ('^' powerExpr)?
/// negExpr : ('-' | '!' | '~') negPower | linkExpr
```

打印机这边原来是 `prefix()` 返回 `70`——**比 `^`（60）严**，于是 `^` 的底数不加括号。改成 `kPrefixPrecedence = 55`（`*` 的 50 与 `^` 的 60 之间），一格一行注释，出处写在常量上。

**"别处还有一份表"的检查**：`git grep -n precedenceOf -- src` 只有 `src/passes/trans/cml/cml.cpp#precedenceOf` 一处（打印侧）；解析侧没有第二份"优先级数字表"，它是 `parser_expr.cpp` 里逐层的语法函数（`parsePower` / `parseNeg`），**这一刀不动它**——我们只是把括号加上，解析器本来就按那套规则读。**所以这次不存在"两张表只改一张"的形状；只改了一处，且改的那处把出处指向了解析器的原话。**

原程序与打印版（`std::nvm`）三行输出：

```
原: 18 512 64 2     打印: 18 512 64 2
原: -9 9 -4 -4 12   打印: -9 9 -4 -4 12   （第 4 行现在是 `println(-(x ^ 2), (-x) ^ 2, -4, -4, 12)`）
原: 0.5 2 3         打印: 0.5 2 3
```

判据（基线 `553 / 1807 / 543 / 502 / 41 / 10 / 52`，语料 3361 个文件，即第十三刀之后）：

| 读数 | 基线 | 本刀 |
| --- | --- | --- |
| `printed` | 553 | **553**（这一条本来就打印） |
| `equivalent` | 502 | **503** |
| `not_equivalent` | 41 | **40**（正是 `power`） |
| `unfaithful` | 10 | **10** |
| `DIFFERENT` | 51 行 | **50 行，新增 0** |
| `families` | 52 | **52** |

**两套计数（写进页，免得下一个人以为数字被改动）：** `EXAMPLE_STOPS` 的键是**诊断类别**（`a semantic error`、`an internal error`），值是该类的条数，**不是总停止数**。作者 22:30 那一笔记的 `40` 是 `a semantic error` 类，总停止数是 **41**；本轮把 `vjp` 的 `@example` 修好之后，`a semantic error` 回到 **39**、总停止数 **40**（`vjp` 已不在"编不过"的名单里，那份名单现在是 34 个名字、40 条，多为 `f`/`encode`/`n` 这类 fragment）。两个数一起看才不误会。
**补声明清单（按节点数）。** 全语料里被打印机点名、确实没有任何声明的算子 **143 个、12520 个节点**。前 12：`:str/format` 1019、`tensor:sum` 934、`tensor:multiply` 862、`tensor:reshape` 856、`:op/tag_is_null` 689、`:struct/len_arr` 585、`tensor:add` 582、`:op/assn_l` 569、`:mark/map_arr` 389、`tensor:subtract` 324、`tensor:matmul` 238、`:op/slice_sub` 219。

**更正两份旧数。** ① 之前报的「353 条没有声明」其实是「没有 `#op` 标签」的数，不是「没有声明」：`:op/add_l`（3244 节点）、`:op/mul_d`（801）这一批由 `globals.cpp` 的组（`__add__`、`__mul__`）**原生登记**，打印机从不点名它们，所以它们不是缺口；真正的缺口是上面 143 条。② `:op/keep` 已在 `fsh-dev 5773c9482` 删掉，全语料图里 **0 个节点**，扣前扣后都是 353（它与上面那 143 条无关）。

1. **今天能打印的只是少数**：`test/cases/**/*.cml` 共 **2387** 个文件，`std::cml` 打印 **348** 个，以 `CmlUnsupportedConstruct`/`CmlBigConstant` 拒绝 **1338** 个，其余 701 个本来就编译不过（诊断用例）。能打印的集中在 `compile` 122、`consistency` 50、`opt` 39、`modules` 38、`linear` 23、`trans` 21。
2. **往返性质今天就有反例**：打印成功且能重新编译的 348 个里，**309 个等价、35 个不等价、4 个的打印结果连 `std::jgir canonical=true` 都跑不出来**。这 35 个不是「看起来不整齐」，是打印器**丢节点**或**改接线**，见 §3.9 与 §7。
3. **最大的两类缺口是声明与类型**：拒绝理由归并后 131 类，最多的是「算子没有源码形式」463 个、「参数的类型打不出来」273 个、「`with` 参数是命名类型」66 个、「图名不是源码标识符」58 个、「宏图」38 个、「参数是命名类型」35 个、「返回类型是带标签/可空/单元组」31 个、「间接调用少了函数值」26 个。
4. **优化过的图打不出来**：`camel x.cml std::opt std::cml` 与 `std::opt::inline std::cml` 今天都拒绝，理由是 `Inline` 区域（`cml.cpp#validateGraph`）。第一份计划把这当成给人用的附带功能；按裁决 9.7（改写必须保持源码程序运行的每个效应）它其实是**必须**能打的一条路。
5. **等价判定器看不见第三种边**：canonical v2 的节点输入只有 `normal`/`context`/`control` 三种（`gir_canonical.cpp#CanonicalDocument::inputsJson`），**没有顺序边**（实测：一个带 `var` 写与同资源效应的程序，整个 canonical 文档里 `order` 出现 0 次；节点字段只有 `callee/id/inputs/kind/payload/type/value`）。图的边重构落地后每个同资源效应对与每个 `var` 写写对都有顺序边，判定器却看不见——**这是假绿的来源**。
6. **判定器也看不见 `Block` 区域**：canonical 主动**抹掉** `Inline` 与 `Block`（`gir_canonical.cpp#CanonicalDocument::canonicalRegionOf`），而标签是程序的一部分（设计原则 2.5 明说「含区域、写法、标签」）。`Inline` 抹掉是对的（内联体不是另一个程序），`Block` 必须留下。
7. **与图的边重构的接口是 8 处读今天表示的地方**：`cml.cpp#findEntry`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#bindLoopCond`、`cml.cpp#expression`（`Gate`/`Sync` 节点）与 `cml.cpp#buildTopo`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`（`ctrlInputsOf`）；`cml.cpp#regionNeedsSync` 不在这八处里——它已由 sync 边判定（`syncPredsOf`）。图的边重构删掉 `Ctrl` 这一种类与这两种节点（`edges.cn.md:15, 25, 48`；`README.cn.md:402, 409`）。**打印机今天就是按这些写的**，本计划的第一步就是让它不按这些写（§5）。
8. **顺序**：先做一个**一个提交就能看证据的竖切片**（三种边 + 端口角色进判定器与打印器视图，§6 的 S3a），再做优化过的图（S3b）、声明（S4）、其余算子与整程序（S5）、与图的边重构合流（S6）。每一步的验收都是「跑一条命令、看三份输出」，不是「看起来更好了」。

## 1. 今天已经有什么（事实，带提交与位置）

| 有什么 | 在哪 |
|---|---|
| 打印机 | `src/passes/trans/cml/cml.cpp`（2433 行）、`include/camel/passes/trans/cml/cml.h`；`CmlDumpPass` 是 `RuntimeGraphTranslatePass`，`acceptsWritings()` 为假（`cml.h#CmlDumpPass`） |
| 注册与声明 | 内核注册 `pass.cpp#registerCoreKernels`；遍的声明（名字、说明、参数）在 `stdlib/std.cml#cml` |
| 诊断码 | `CmlUnsupportedConstruct = 0x1A5`、`CmlBigConstant = 0x1A6`（`semantic.h#CmlUnsupportedConstruct`） |
| 参数 | `maxLiteralBytes` 默认 64 KiB（`cml.h#CmlOptions`），写出为 `std::cml max_literal_bytes=...` |
| 抽取写法 | 有写法状态时，驱动器挑一个写法再交给消费者（`pass.cpp#applyPassesDetailed`）；不知道状态的遍拒绝（同一函数里的 `RuntimeDiag::PassNeedsExtraction`） |
| 规范转储（normalizer） | `std::jgir canonical=true`，版本 2，实现在 `src/passes/trans/json/gir_canonical.cpp`，规格在 `docs/technical/gir-interchange.md`「Canonical documents」 |
| 等价检查器 | `test/tools/cml_roundtrip.py`（互异个体化细化，预算 20000 节点 / 200000 步；预算用尽返回 2，**绝不报等价**；`cml_roundtrip.py#equivalent`） |
| 共享比较工具 | `test/tools/gir-dot.mjs#compareGirDotIsomorphic` + `test/tools/normalizers.mjs#applyNormalizers`，用于 `graph_equal.mjs#GOLDENS` 记的那份语料 |
| 已钉住的合同 | `test/plans/feat/trans/cml.plan.toml`、`test/cases/trans/cml_contract.py`（语料 8 个程序、执行对照、确定性、名字保留、5 类拒绝、大常量拒绝） |
| S1/S2 覆盖的语法 | 模块值、函数、字面量、访问、CAST/COPY/ACCS、算子语法（`:io/*`、`:op/*`）、直接调用、`let`/`var`、赋值、`return`、直行 `sync`、`if`/`match`、循环（`for`/`while`/`for ... while`）、闭包、`with`/`var` 参数（`cml.cpp#CmlPrinter` 的自述） |

## 2. 「任何图」的范围：逐项实测

设计原则 2.5 要求「任何合法的图都能写成源码，写回再编译得到同一张图（名字、源码位置不计）」。下表把「任何图」拆成构造，逐项给出**模型里有没有**、**源码能不能写**、**今天打印器怎么做**、**实测到的证据**。

| 构造 | 模型里的位置 | 源码形式 | 今天 | 实测证据 |
|---|---|---|---|---|
| 根/体区域、臂、循环条件与体 | `GCRegionKind`（`graph.h#GCRegionKind`） | `if`/`match`/`loop` | **能** | 2 sync 块、分支与循环语料打印并等价（`cml_contract.py#CORPUS`） |
| `Inline` 区域 | 同上（`graph.h#GCRegionKind` 的 `Inline`） | 无（内联是遍的产物） | **拒绝** | `camel x.cml std::opt std::cml` → `a 'inline' region, which is not a branch or loop region`（`cml.cpp#validateGraph`） |
| `Block` 区域（带标签的块） | 同上（`graph.h#GCRegionKind` 的 `Block`）；前端会建：`builder.cpp#Builder::visitExecNode`（`setRegionAttrs`） | `#tag { ... }` | **拒绝**（区域循环只放行 Arm/LoopCond/LoopBody） | 与上一行同一条诊断；节点的标签另在 `cml.cpp#validateGraph` 拒绝 |
| 节点标签/权限掩码 | `GCGraph::hasNodeAttrs()`（`graph.h#GCGraph::hasNodeAttrs`） | `#strict`、`#kernel` | **拒绝** | `trans/cml/refuse/tagged_node.cml` → `std::cml cannot print f: it has node attributes or tags.` |
| 声明标签（函数/类型/字段/参数） | 运行时图里**没有存放处**；只有前端服务层 `model.h#DeclFacts` 的 `declFacts`。命名一张图的声明标签有：`kGraphRefRule`（`graph.h#GCGraphRefRole`，`#vjp(rule)`） | `#strict`、`#macro`、`#shape`、`#width(32)` | **拒绝/丢失** | 第一份计划 §1.1 实测 `std::jgir` 里这些标签完全不出现；今天 `#macro` 图被 `cml.cpp#validateGraph` 拒绝（38 个用例） |
| 端口与角色（普通/上下文） | `GCNode` 的输入区（Norm 后 With，`graph.h#GCNode`）；目标模型是「一列带角色的端口」（`edges.cn.md:23-31`） | 形参与 `with <...>` | **能，但按今天的表示读** | `cml.cpp#buildTopo` 分别遍历 `normInputsOf`/`withInputsOf`；canonical 文档里 `context` 角色已存在（实测 `{normal 12, context 1, control 6}`） |
| 同步边 | 今天：`Ctrl` 输入 + `Gate`/`Sync` 节点（`cml.cpp#findEntry`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#bindLoopCond`、`cml.cpp#expression`） | `sync { ... }`、函数体后的 `sync` | **能，由 sync 边判定** | `regionNeedsSync`（`cml.cpp#regionNeedsSync`）读同区域内的 sync 边（`syncPredsOf`），`regionReturnsValue`（`cml.cpp#regionReturnsValue`）读同区域内的控制边；`std::gir` 实测该探针图有 `GATE: 2` 节点、3 条控制边 |
| 顺序边 | `GCGraphOrderEdge`、`orderPredsOf`/`orderEdges`（`graph.h#GCGraphOrderEdge`、`graph.h#GCGraph::orderPredsOf`、`graph.h#GCGraph::orderEdges`、`graph.h#GCGraphNativePayload`）；已参与节点身份（`GCGraph::nodeContentHash`，`graph.cpp#GCGraph::nodeContentHash`） | **没有**（设计稿 `edges.cn.md:42`：从 `sync` 与资源重新推出） | **不打印，也不检查** | 实测：一个 `var x = 1; x = 2; x = 3` + 两个 `println` 的程序，canonical 文档里 `order` 出现 **0** 次；打印器输出 2 个 sync 块、往返判为等价、两侧 stdout 都是 `3\|1\|2` |
| 分支与臂归属 | `BRCH` + `Arm` 区域（`edges.cn.md:47`：臂只靠区域归属，`BRCH` 与臂之间没有边） | `if`/`match` | **能** | 分支语料打印并等价 |
| 闭包与函数值 | 函数常量 + `FILL` + `capturePorts` | lambda、被捕获的名字 | **能**（有边界） | 共享的 lambda（一个函数值多处用）与匿名捕获名被拒绝（`cml.cpp#validateGraph`） |
| 调用（直接/间接） | `FUNC`/`CALL` | `f(x)`、函数值调用 | **直接能，间接常拒绝** | 26 个用例 `an indirect call without one function value`（`cml.cpp#callExpression`） |
| 效应与资源 | 效应的资源顺序来自 `sync` 与资源（`edges.cn.md:11`）；资源冲突由 `resources.cpp` 判定 | 效应就是普通调用 | **取决于 wn 算子是否有源码形式** | `test/cases/resources/effects.cml` → `the operator 'array:sort_in_place'`；`conflict_per_resource.cml` → `the operator 'rand:seed'` |
| 写法（egraph 等价类） | `hasWritings` + 抽取（`pass.cpp#applyPassesDetailed`） | **没有**（第一份计划 §7 已答：打印抽取后的图） | **拒绝，除非驱动器先抽取** | `std::cml` 的 `acceptsWritings()` 为假（`cml.h#CmlDumpPass::acceptsWritings`）；`std::opt::saturate std::cml` 实测打印成功（抽取后的可执行图） |
| 命名类型（struct/enum）与类型参数 | `StructType::named()`/`typeArgCount()` | `type Point = { x: int }`、`enum` | **拒绝** | `checkType` 的 `Struct` 分支（`cml.cpp#checkType`）；`Option<int>`、`Tensor`、`Mlp`、`Node`、`Program` 全走这里 |
| 带标签/可空/单元的元组与返回类型 | `TupleType::isTagged()/isNullable()/isCell()` | `T?`、`$tagged` 元组 | **拒绝** | `cml.cpp#checkType`（元组）、`cml.cpp#checkSignatureType`（返回类型；实测 31 个用例） |
| 数组、匿名 struct、元组、普通函数类型 | `TypeCode::Array/Struct/Tuple/Function` | 字面量 | **能** | `cml.cpp#checkType` 放行这些；其余落到同一函数的 `default` 分支 |
| 大常量 | 静态区 | 字面量 | **超限拒绝** | `camel big.cml std::cml max_literal_bytes=64` → `std::cml refused the constant 'xs' of type int[] in main: 128 bytes, over max_literal_bytes (64). Raise the limit with ...` |
| 收集循环 | `GCLoopRef::collectionCount`（`graph.h#GCLoopRef`） | 解释器的收集槽（源码写不出） | **拒绝** | `cml.cpp#loopExpression` → `a loop that collects values into arrays`；`std::cpp` 同样拒绝（`function_emitter.cpp#FunctionEmitter::lowerLoop`） |
| 宏图 | `kGraphFlagMacro` | `#macro` | **拒绝** | `cml.cpp#validateGraph`；`trans/effect_trace/macro_effect.cml` → `it is a macro graph` |
| 模块级捕获（库模块） | 图捕获表 + 顶层 `let` | 顶层 `let` | **拒绝** | `test/cases/trans/library_module.cml` → `std::cml cannot print shifted_square: it captures 'offset' from an enclosing function (a closure)`（`cml.cpp#validateGraph`） |
| 图名不是标识符 | `GCGraph::name()` | 无 | **拒绝** | `cml.cpp#validateGraph`，实测 58 个用例（例 `compile/array_shape/constant_dimension.cml`） |

## 3. 缺口清单（按名字、带复现、带「语法停在哪一行」）

**缺口 A：算子的源码形式表（463/1338，最大一类）。**
复现：`camel test/cases/compile/closure/function_values.cml std::cml` → `std::cml cannot print main: the operator ':str/format'. This version prints expressions, ...`。
停在哪：`cml.cpp#operatorExpression`（`:var/get`、`:var/*` 与其余算子 URI 都在这里 `fail`，`prefix` 里另有一条）。实测最常见的 URI：`:str/format` 71、`:mark/map_arr` 23、`env:get_env` 21、`tensor:reshape` 19、`:struct/len_str` 17、`:mark/reduce_arr` 9、`rand:seed` 8、`:struct/range` 8、`array:sort_in_place`、`rewrite:rule`。
代价：这是**表**的工作，不是语义的工作：每个 URI 要么给一条语法（`x->map<f>` 这类高阶形式），要么确认它是编译期/内部算子（`:mark/*`、`:struct/*`）而应当写成它降级前的源码形式。判断需要逐 URI 的证据，所以它是 S5 里最大的一块。

**缺口 B：命名类型与声明（273 + 66 + 35 + 31 = 405 个用例）。**
复现：`camel test/cases/trans/cml/refuse/named_type.cml std::cml` → `std::cml cannot print get: a parameter is the named type 'Point'.`；`camel test/cases/compile/enum/generic_tree.cml std::cml` → `... a parameter has the type 'Option<int>', which std::cml does not print.`；`camel test/cases/modules/onnx/export_train_step.cml std::cml` → `a 'with' parameter is the named type 'Mlp'`。
停在哪：`cml.cpp#checkType`（命名 struct 与 `default` 分支）、`cml.cpp#checkType` 的元组分支（带标签元组）。
代价：要打印声明本身（`type`/`enum` 声明、字段名、类型参数），还要一条**程序级声明表**（第一份计划 §7 问题 2 已答「程序级事实表，键为声明身份」）。`Tensor`、`Node`、`Program` 也是命名类型，所以 ONNX/autodiff/graph 三个模块语料的 38+ 个用例都挂在这一类上——它是**整程序**能不能打印的前提。

**缺口 C：优化过的图（`Inline` 区域）。**
复现：`camel test/cases/trans/cml/refuse/inline_region.cml std::opt::inline std::cml` → `std::cml cannot print main: a 'inline' region, which is not a branch or loop region`（`cml.cpp#validateGraph`）。
停在哪：`cml.cpp#validateGraph` 的区域循环只放行 `Arm`/`LoopCond`/`LoopBody`。
代价：内联体在源码里没有专门写法，唯一正确的做法是**把它的语句按它自己的顺序摊进外层区域**——也就是判定器已经在做的抹掉（`gir_canonical.cpp#CanonicalDocument::canonicalRegionOf`）。代价是「被内联的入口提升成虚拟 `main`」这条（`gir-interchange.md` 已列为「还没做，属于让打印器必要的那一步」）要同时落；否则原文的 `main` 图在重编译后没有调用者，canonical 的「只取可达图」会把两侧的图集比得不一样。**S3b 的第一件事就是量这一条**。

**缺口 D：顺序边（不打印可以，不检查不行）。**
复现：`camel probe.cml std::jgir canonical=true` 对
```camel
func f(): int sync { let a = println(1)
    let b = println(2)
    return b }
func main(): int sync { var x = 1
    x = 2
    x = 3
    println(x)
    return f() }
```
整个文档里 `order` 出现 0 次（节点字段只有 `callee/id/inputs/kind/payload/type/value`）。
代价：给 canonical v2 加 `syncPreds`/`orderPreds`（或把 `control` 拆成 `sync`/`order` 两种 kind），并让检查器比较它们。这**不改变源码表面**（`edges.cn.md:42`），只让判定器看得见第三种边。不做的话，打印机把顺序边写错/丢掉永远测不出来。

**缺口 E：`Block` 区域与标签（被 normalizer 抹掉）。**
复现：`camel test/cases/trans/cml/refuse/tagged_node.cml std::cml` → `it has node attributes or tags`；前端确实会建 `Block`（`builder.cpp#Builder::visitExecNode`）。
停在哪：打印器拒绝（`cml.cpp#validateGraph`）；判定器抹掉（`gir_canonical.cpp#CanonicalDocument::canonicalRegionOf`）。
代价：判定器要留下 `Block`（kind、parent、owner、attrs），`Inline` 继续抹掉；两者的理由不同（标签是程序内容，内联体不是另一个程序）。打印器要能打印 `#tag { }` 块；需要先有**标签声明的源码形式**（`tag finite = [kernel, heavy]` 这类今天在 `test/cases/compile/tags` 里就有）。

**缺口 F：模块级捕获（库模块）。**
复现：`camel test/cases/trans/library_module.cml std::cml` → `it captures 'offset' from an enclosing function (a closure)`（`cml.cpp#validateGraph`）。
代价：顶层 `let` 的值被函数捕获时，源码里就是读一个顶层名字。第一份计划 §7 已答「模块级捕获摊平成顶层 `let`」——今天只是没实现（`cml.cpp#validateGraph` 里读非顶层名字那条与捕获那条一起）。

**缺口 G：图名不是标识符（58 个）；间接调用（26 个）；大常量数据文件；收集循环；宏图；rest/constas 参数。**
各自的位置：`cml.cpp#validateGraph`、`cml.cpp#callExpression`、`cml.cpp#scanBigConstants`+`cml.h#CmlOptions::maxLiteralBytes`、`cml.cpp#loopExpression`、`cml.cpp#validateGraph`、`cml.cpp#checkSignatureType`。
其中「大常量」第一份计划已答「v1 超过 `max_literal_bytes` 拒绝并列出（实测消息见 §2 末行），数据引用的源码形式另行设计」——本计划把「另行设计」放到 S5，形状是**源码旁边的数据文件 + 一条读它的声明**，代价是要动 `test/data` 的组织与 `stdlib` 的读接口。

**缺口 H：参数种类（rest/`constas`/可变 lambda）。** 复现：`cml.cpp#checkSignatureType` 里的三条；`compile/constas/*` 与 `compile/rest_params/*` 里能被打印的两个用例**打印了但往返不等价**（见 §7），说明这一类还有第二种缺陷。

## 4. 往返性质与它怎么度量

**性质（沿用第一份计划 §2.2，不再改）**：对程序 `P`，`canon(P)` 与 `canon(compile(P std::cml))` 必须等价；检查器判等价，**不判文本**。文本只要求确定性：同一个图打印两次逐字节相同。

- **normalizer**：`std::jgir canonical=true`（版本 2）。它只留等价定义涉及的东西：可达图、活节点、节点种类与按种类和槽位的输入、算子身份（语义键）、静态值（按值）、区域（kind/parent/owner/index/result/ports/params/attrs）、类型表、声明原子、标签原子；名字、源码位置、编号都不在文档里（`docs/technical/gir-interchange.md`；`gir_canonical.cpp#CanonicalDocument`）。
- **检查器**：`test/tools/cml_roundtrip.py`。它对整份结构做颜色细化 + 互异个体化，比较结构类；预算用尽是 exit 2，**不会**被读成等价（`cml_roundtrip.py#equivalent`、`cml_roundtrip.py#main`）。
- **共享比较工具（不对往返负责）**：`test/tools/gir-dot.mjs#compareGirDotIsomorphic` + `test/tools/normalizers.mjs#applyNormalizers`，在 `graph_equal.mjs#GOLDENS` 记的那几条上用于「Camel 遍 vs C++ 遍」的语料对照（`graph_equal.mjs#NORMALIZERS` 是它用的规范化器）。它比的是 **GIR DOT**：节点签名是 `type|shape|style|in|out 度`，边签名是两端签名 + `linktype` + `label`（`gir-dot.mjs#nodeSignature`、`gir-dot.mjs#edgeType`），所以它**看不到算子身份、静态值、区域的 index/params/attrs**，而且它依赖显示转储的边类型——图的边重构正改这个（`gir.cpp#dumpJsonRuntimeGraph` 今天写 `control`）。结论：往返的判据仍是 v2 文档 + `cml_roundtrip.py`；`graph_equal.mjs` 只在它现有的语料里用，**不搬到往返上**，代价是同一个仓库里有两套图比较工具（比把它们合并便宜：合并要先让 DOT 携带算子身份与静态值，那是另一个任务）。
- **今天的实测结果**：348 个能打印的程序里 **309 等价 / 35 不等价 / 4 个打印不出 canonical**（`SWEEP files=2387 printed=348 refused=1338 frontend=701`、`ROUNDTRIP checked=348 equivalent=309 not_equivalent=35 unfaithful=4`）。
- **执行对照**：`cml_contract.py` 已经把「打印后重新编译的程序 stdout 与退出码相同」钉住（`cml_contract.py#check_round_trip`）。它与设计原则 9.7（改写必须保持源码程序运行的每个效应）的关系：**必须在同一次构建里对照**。理由是这条规则自己的验收用例 `func f(): int { let a = println(1); let b = println(2); return b }` 在重构前打印 `1`、重构后只打印 `2`——同一段源码的执行结果会随边的模型变，所以「和昨天记录的 stdout 一样」不是判据，「这一次构建里原程序与打印后程序一样」才是。
- **名字**：不进等价（第一份计划 §2.3 第 5 条），由 `cml_contract.py#check_names` 的名字检查单独钉。

## 5. 三种边与图的边重构：怎么做到不针对今天的表示

**今天打印机从哪里读边（8 处，逐处都会在图的边重构后失效）**：

| 位置 | 读什么 | 图的边重构之后 |
|---|---|---|
| `cml.cpp#findEntry`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue`、`cml.cpp#bindLoopCond`、`cml.cpp#expression` | `GCNodeKind::Gate` / `GCNodeKind::Sync` | 两种节点删除（`edges.cn.md:48`） |
| `cml.cpp#buildTopo`、`cml.cpp#isEffectful`、`cml.cpp#regionReturnsValue` | `ctrlInputsOf(ref)` | `Ctrl` 种类删除，存活只沿数据端口与同步边（`edges.cn.md:15, 19`）；`cml.cpp#regionNeedsSync` 不读这一列，它已由 sync 边判定（`syncPredsOf`），不随这一层改 |
| `cml.cpp#buildTopo` | `normInputsOf` / `withInputsOf` 两个列表 | 合成**一列带角色的端口**（`edges.cn.md:25`） |

**做法（一个视图层，两个实现）**：在 `src/passes/trans/cml/` 里加一个只读视图 `PrintedGraph`，它把运行时图翻译成打印器唯一认识的东西：`ports(node)`（有序，带 `Normal`/`Context` 角色）、`syncPreds(node)`、`orderPreds(node)`、`regionOf(node)`、区域表；打印器其余部分只调视图，不再直接碰 `normInputsOf`/`withInputsOf`/`ctrlInputsOf`/`Gate`/`Sync`。

- 过渡期：视图有两个实现（今天的一套；边重构落地后的一套），用现有测试钉住两者对同一批语料的输出逐字节相同。代价：一个约 200 行的适配文件与一段"两份实现并存"的窗口；收益是不用在边重构落地当天重写 2433 行里的 8 处深浅不一的读法。
- **同步边必须复原成 `sync`**：它是效应"被用到"的唯一载体，漏掉一个 `sync` 块，重编译后的程序就不再运行原程序运行的效应，直接违反 9.7。所以 `sync` 的复原判据要从「同区域内的控制边」换成视图的 `syncPreds`。
- **顺序边不打印**（设计稿 `edges.cn.md:42`：从 `sync` 与资源重新推出），但**必须进判定器**（缺口 D），否则"打印机把顺序边丢了"这种错误永远是绿的。
- **`BRCH` 与臂、`JOIN` 的端口**按设计稿第 5 节由节点种类定（`edges.cn.md:46`），视图不改这一层。

## 6. 工作顺序（每步的产物、验收、代价）

**S3a（先做的小竖切片，一个提交能看证据）——三种边与端口角色进判定器与视图。**
1. canonical v2 增加 `syncPreds`/`orderPreds`（缺口 D）；`cml_roundtrip.py` 比较它们。
2. 加 `PrintedGraph` 视图，打印器改调视图（8 处）。
3. 用例三条：`func f(): int sync { let a = println(1) ... }`（同步边 + 效应）、`var x = 1; x = 2; x = 3`（写写顺序边）、一个带 `with` 参数与一个 lambda 的程序（两种端口角色）。
4. 验收（可复现）：`std::jgir canonical=true` 的文档里出现两种前驱；把其中一条顺序边从图里删掉后**检查器必须报不等价**（今天报等价，这是这一步唯一能证明"看得见"的实验）；`std::cml` 打印后往返等价、同一次构建里 stdout 相同。
代价：v2 文档多两个字段（读者是自家检查器，版本号不动，因为 v2 尚未对外承诺；若决策者要求，改 v3，代价是多一个版本判定）。这一步**不动语言表面**。

**S3b——优化过的图。** 摊平 `Inline` 区域（缺口 C）+ 量「重编译后没有调用者的图算不算程序的一部分」；`camel x.cml std::opt std::cml` 必须能跑。代价：虚拟 `main` 的提升，以及一次关于"孤立图"的规范决定（建议：canonical 只取从入口沿调用可达的图，并在同一提交里把这条写进 `gir-interchange.md`——那份是技术文档，不是作者的目录）。

**S4——声明。** 命名类型与 enum、类型参数、程序级声明表、`Block`/标签、非标识符图名、带标签/可空元组。代价最大（405 个用例），但它是 `Tensor`/`Node`/`Program` 语料的前置，所以排在算子表之前。

**S5——其余算子与整程序。** 算子源码形式表（463 个用例，逐 URI 给语法或给"内部算子"的判定）、模块级捕获、间接调用、收集循环、宏图、大常量的数据文件、随机程序扫描（第一份计划 S5 的原文要求）。代价：表与逐 URI 证据，最耗时。

**S6——与图的边重构合流。** 视图切到新实现、三种边的合流验收（`README.cn.md:398-410` 的 7 条）、四个后端一致、整语料扫描的通过数与"允许拒绝"清单一起交决策者裁定。

## 7. 验收用例（今天的结果是基线，不是目标）

| 用例 | 断言 | 今天 |
|---|---|---|
| `test/cases/trans/cml/{linear,branch,loop,closure,with_param,var_param}.cml` | 往返等价 + stdout + 确定性 + 名字 | **全绿**（`trans.cml` 计划） |
| 5 个 `refuse/*.cml` | 精确诊断、不产生半份输出 | **全绿**（`cml_contract.py#check_refusal`） |
| `std::opt::saturate std::cml` | 打印抽取后的可执行图 | **绿**（实测：`let x: int = 5`） |
| `std::opt std::cml` / `std::opt::inline std::cml` | 打印并往返等价 | **红**：`a 'inline' region ...` |
| `compile/constas/constas_column.cml` | 往返等价 | **红**：打印出 `func main(): int sync { return 0 + 1 + 2 + 3 }`，而原图多 3 个 DATA 节点与 3 个二元 OPER 节点（打印器**丢节点**） |
| `compile/rest_params/rest_inline.cml`、`rest_params.cml`、`constas_forward_rest.cml`、`constas_with_rest.cml` | 往返等价 | **红**（4 条同族） |
| `compile/return_type/accepted.cml` | 往返等价 | **红**：打印出 `func a(x: int): int { return x }`，原图多 1 个 DATA 节点 |
| `compile/diag_json/warnings_visible.cml` | 往返等价 | **红**：节点种类多重集相同、接线不同（检查器第一条差异：`a structural class has 1 vertex/vertices on the left and 0 on the right`） |
| `compile/static_expansion/indices_graph.cml` | 往返等价 | **红** |
| `modules/rules/check_rules.cml`、`trans/macro/*` | 打印 `rule`/`#macro` | **红**：`the operator 'rewrite:rule'`、`it is a macro graph` |
| `trans/library_module.cml` | 库模块打印 | **红**：`it captures 'offset' from an enclosing function` |
| 整语料 | 每一个能编译的用例要么往返等价要么在允许拒绝清单里 | **348/2387 打印，35 不等价，4 打印不出 canonical** |

**这 35 个不等价是这一步最有价值的资产**：它们证明检查器真的会红，也给出打印器"悄悄丢节点"的具体形态。S3a 的第一件事是把它们逐个定性（丢节点 / 改接线 / canonical 自身的问题），因为「打印器丢了节点」与「规范转储漏了东西」是两种完全不同的缺陷。

## 8. 要裁定的问题（本计划不擅自决定）

1. canonical v2 是否直接加 `syncPreds`/`orderPreds`，还是升 v3？（我倾向直接加：读者只有自家检查器，且 v2 尚未对外承诺。代价：若已有外部读者，会静默少两个字段。）
2. `Block` 留在 canonical，`Inline` 抹掉——是否照此裁定？（依据：标签是程序内容，内联体不是另一个程序。代价：`Block` 的 attrs 要参与等价，含标签的图会比较得更严。）
3. 大常量的数据文件形式（第一份计划把形状留到现在）：源码旁一个数据文件 + 一条读它的声明，还是别的形状？
4. 算子源码形式表里哪些 URI 判为"编译期/内部算子"（`:mark/*`、`:struct/*`）而永久拒绝？（这直接决定 §7 的"允许拒绝清单"。）
5. 裁决 9.7 与边重构的合流验收里，「同一段源码在重构前后执行不同」是否需要语言侧的说明（教程/语言参考），以免用户把它当成往返的缺陷。

## 9. 本计划明确不做

不把源码或文本存进图；不为打印在图里加"源形状"字段；不因为打印不出就改语言语义；不把 `std::cml` 做成部署格式；不合并 `graph_equal.mjs` 与 `cml_roundtrip.py`；不在本计划里动 `src/runtime/**`（边的实现）、类型系统、`modules/tensor` 与 runner——那些是别的任务；GPU 与 `std::cml` 的交互（`#gpu` 块）留给 GPU 那两项落地后另立计划。
