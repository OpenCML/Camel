# 转置图需要前向那一半的中间值，谁携带它？

## 问什么

一条律要给某个操作数的切向量，可能得读**前向那一半在这一点上的值**（不是参数原值，不是常量，也不
是它自己算出来的那个数，而是链上某个**中间节点**的答案）。`transpose` 走出来的图今天只收
**余切**：每个操作数喂一个余切，图的可读入值就是这些余切。于是"律的右手边读了一个中间节点的前向值"
这件事**无处安放**：走法把它当成第 N+1 个可读入值，在转置图里多开一个端口，而函数值的签名是另一个
东西算的（`transposeSignature`），它只数余切 —— 两张表对不上，调用时死在
`nargs.size() == normPorts.size()`。

要作者裁的是：**残差（前向中间值）要不要按端口带进转置图**，还是转置只能对"律的右手边只读参数与
常量"的节点成立？两条路都自洽，代价不同，见 §默认。

## 五处症状（同一因，逐条实测原文）

| # | 用例 | 缺的东西 | 原文 |
| --- | --- | --- | --- |
| 1 | `rules_angle_args.mean_axis_transpose` | `mean_axis_cotangent` 读 `xs` 的前向值 | `TRANSPOSE function=loss_mean skip=arity uri=tensor:mean_axis operands=3` |
| 2 | 同上，把个数那道闸抬开之后 | 端口没人填 | `TRANSPOSE function=loss_mean graph=296 type=(float[..], float) => (float[..],)` 对函数值签名 `(float) => (float[..],)`；随后被信号打死 |
| 3 | `rules_angle_args.reshape` / `.softmax` | 同一个地方（前四个数字已对） | `value=30.0 tangent=5.0`、`value=1.5 tangent=-0.25`，第五个 `transpose=6.0` 缺席 |
| 4 | `transpose_loop_escape.two_values_of_two_kinds` | 循环体自己算出来又读回去的值 | `Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports`；`transposed=(float) => (float,) graph=(float, float) => (float,) same=no` |
| 5 | `transpose_loop_escape.second_state_of_the_same_loop` | 同上 | 与 #4 同一个 `same=no` |
| 6 | `vjp_loop.rounds_are_undone_in_reverse`（已知失败） | 体自己算出来的值（`split` 侧更早一步） | `split_of_jvp_of_g_backward skip=call-value-missing param=0 node=23` |

#2 与 #3 是同一次观测：#3 是同一堵墙在更外层算子上的样子（`reshape`/`softmax` 各自的外层节点先撞
别的墙，越过后停在同一个 `mean_axis`/`multiply` 一带）。#1、#2 是同一处的两个阶段：#1 是按个数拒，
抬开个数之后才是 #2 的签名不一致。**这些都不是"比对错"，是缺功能。**

## 已经量到的四件事

1. **转置的约定今天只有余切。** 走法把每个操作数的一个余切当作转置图的可读入值；函数值的签名由
   `transposeSignature` 从**正函数的签名**算，只数余切。两者本来一致 —— 直到律的右手边读了一个
   中间节点的前向值。
2. **律读*参数*或*常量*不需要残差。** `sum_t`、`mul_t`、`dtod_t` 都只读自己的余切，所以这些算子
   在转置里根本不需要前向值 —— 今天每一条能走通的转置律都是这个形状。（这条原文还点了
   `mul_tensor_t`：**那个律不在这个树里**——数组乘积的转置律是 `lane-mulsc` 正在写的那一半，
   落地之前不要把它当成已存在的事实来引用。）
3. **`mean_axis` 的律是反例。** `mean_axis_t` 要 `xs`（被归约的那个数组）的**值**才能把余切摊回去；
   `stdlib/rulesets/transpose.cml#add` 早就写着"这不是一条还要有人去找的律"。
4. **标量这条路本身是通的。** 另一条车道量的标量模型三方数完全一致（`dL/dw` = −0.75、`dL/db` =
   −0.5、`loss` = 0.125，库层×引擎、库层×PyTorch 的最大绝对差都是 **0.0**）。所以问题不在架构，
   在"转置该不该带前向值"这一条约定。

## 三件容易混成一件事的（分清）

- **#3/#4 `softmax<1>`、`mean<-1,true>`** —— 同一笔：axis/keepdims 是 `<>` 端口，`portsTakeOperands`
  与 `normTypesCount()` 的帐对不上（本支已修的那一处），越过它之后撞的就是本页这条。
- **`sum(w * w)` 那格** —— 独立的一笔：缺的是**数组乘法的转置律**（`mul_t` 只到 `is_float::<T>()`），
  即 `jvp_mul_tensor` 的伴随，另一条车道在做。它不解决残差，残差也不解决它。
- **捕获的数组常量那格** —— 不在规则表里：`modules/transforms/jvp.cpp` 造常量对的地方不给数组造，
  本支已改成"按类型（和值给的形状）造零切向量"。

## 不知道的

- 残差按端口带进来之后，**签名与调用点跟着改**是必然的：`transpose(loss_mean)` 的值要从 `(float) =>
  (float[..],)` 变成 `(float[..], float) => (float[..],)`。这会改掉每一个**调用点**的写法（`vjp` 走
  `split`，它的闭包已经带残差，`residuals_in_the_closure.nvm` 是绿的），所以受影响的只是"直接对非线性
  函数取 `transpose`"的地方。
- 与 #6 的关系：循环那半可以"把区域抄一份重算"（今天的选择，`vjp_loop` 的 `invariants=0`），也可以让
  残差携带值。**二者选一**，不能一半抄一半带 —— 那正是不一致生出的信号。
- 端口顺序：残差接在余切前还是后，要与 `split` 的闭包端口顺序一致（`transpose.cpp` 的
  `needs`/`knownValue` 那两张表要一起改）。

## 默认（按此往下做，不为了让它绿而抬开个数的闸）

1. **残差按端口进转置图**：走法遇到"律的右手边读了中间节点的前向值"时，把这个值登记成一个残差端口
   （顺序与 `split` 的闭包一致），`transposeSignature` 从同一张表算，两边不再各算一份。这样 #1~#5 有
   一条共同的路。
2. **在那之前不抬 `transpose.cpp` 的个数闸**：它今天挡的正是 #2 那种不一致，抬开只会把"有名拒绝"换成
   "信号打死"（实测：`total: 6, pass: 0, fail: 6, killed by a signal: 3`）。
3. **`vjp_loop` 那格与本页合并裁**：抄界区域 vs 残差携带值，二选一，裁完再动。

## 本树的实测（2026-11-03 19:35，lane-sigtable，`b15127a34`，探针 `D:\Temp\probe-sigtable\sig_call2.cml`）

**本页的两半，一半已经在了。** `sum_t`（`sum(xs) => (ct) => spread_like(xs, ct)`）读参数 `xs` 的
值，走法给出的图**已经带那个端口**，而且这个端口是 `transposeSignature` 从 `calleeNeeds` 那张表算
的（`buildTransposeMember` 先跑一遍 probe 求出 `found`，再拿它写签名、写端口）：

```
TRANSPOSE function=loss_sum graph=50 type=(float[..], float) => (float[..],) nodes=3 arms=0 rhs=dtod_t,sum_t
TRANSPOSE op signature=(f: (float) => float | (float, ...) => float) => (cotangent) => (cotangents..) resolved=(f: (xs: float[..]) => float pure) => (float) => (float[..],) transposed=(float) => (float[..],) graph=(float[..], float) => (float[..],) same=no
error[RuntimeDiag::PassInternalError]: Pass 'nvm' failed with an internal error: Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_loss_sum' has 2 norm ports.
```

**缺的是另一半：声明的答案类型。** `stdlib/types.cml#Transpose` 早就写着
`(..ParamsOf<F>, ct: Tangent<ResultOf<F>>) => (..Tangent<ParamsOf<F>>)`，但解析出来的**参数表只有
`ct`**：`..ParamsOf<F>` 在**形参位置**的那一半没有摊开，而答案位置的 `..Tangent<ParamsOf<F>>` 摊开了
（`(float[..],)`）。所以本页剩下的不是"残差要不要按端口进图"（它已经按端口进了），而是**形参位置的
拼接**：让 `..ParamsOf<F>` 把参数表摊到 `ct` 前面，`same=no` 才是 `same=yes`，调用点才跟着写
`transpose(f)(x, 1.0)`。

## 读数（本机不构建；主机 88zjwei，树 `~/camel-work/arity`，tier fast）

| 计划 | 读数 |
| --- | --- |
| `feat/compile/transforms` | 80 = **77 通过 / 2 失败 / 1 已知失败**（两条红的正是 #4、#5） |
| `feat/compile/rulesets` | 67 = **67 通过 / 0 失败** |
| `feat/transforms/split` | 19 = **19 通过 / 0 失败** |
| `feat/compile/rules_angle_args` | 6 = **0 通过 / 6 失败**，前四个数字已对（30.0 / 5.0 / 1.5 / −0.25），第五个等本页 |
## 代价：值端口的形状在类型层表达不出来（2026-11-03 20:20 实测）

裁定第 1 条（残差按端口进转置图）**已经实现了一半**：走法给出的图带那个端口，端口与签名都从
`TransposeState::calleeNeeds` 同一张表算。分叉在第三个算签名的地方 —— `transpose` 算子自己的答案
类型，它调的是不带表的那个重载：

```cpp
// modules/transforms/transpose_operator.cpp:46
FunctionType *transposed = transposeSignature(function);            // 没有 captures/needs
// modules/transforms/transpose.cpp:408（走法那一侧）
transposeSignature(function->funcType(), participates, nullptr, &capturesOf);
```

同一个函数于是算出两个形状：resolver 给 `(float) => (float[..],)`，build 给
`(float[..], float) => (float[..],)`，`same=no` 就是这么来的；调用点按 resolver 的那个写，运行时撞
`caller graph 'main' node 3 has 1 args, target graph 'transpose_of_loss_sum' has 2 norm ports`。
**这就是"两边各算一份"在今天的最后一份。**

### 它不能靠"把声明当契约执行"消掉，因为声明与 `vjp` 那条路冲突

`stdlib/types.cml#Transpose` 写的是**每个**形参都在 `ct` 前：

```
type F = (xs: float[..]) => float
is_same::<Transpose<F>, (xs: float[..], ct: float) => (float[..],)>()   // true
is_same::<Transpose<F>, (ct: float) => (float[..],)>()                  // false
```

探针 `D:\Temp\probe-sigtable\alias.cml`，`camel alias.cml nvm`。`..ParamsOf<F>` 在形参位置**是摊开
的**（所以这不是拼接层的缺陷，我先前那条判断要按这一条更正）。而 `stdlib/vjp.cml` 的
`(y, transpose(lin))` 把 `transpose(lin)` **原样**交给调用者，`pullback(1.0)` 只传余切 —— `lin` 是
`split` 的后一半，它的形参就是那些切向量，按声明执行之后 `pullback` 要多带这些切向量的值，而反向时
那些值不存在。**"全形参"与"`vjp` 不改"不能同时成立。**

### 影响面（数出来的，不是估的）

| 收法 | 要改的调用点 | 计划族 | `vjp` 那条路 |
| --- | --- | --- | --- |
| 全形参（照声明执行） | **63 处 / 28 个文件**（`let back = transpose(…)` 后 `back(…)` 34 处、`transpose(…)(…)` 29 处；按这两种形状数的）。分族：`feat/compile/transforms` 25 个用例文件 59 处、`feat/transforms/transpose` 1 个 2 处、`feat/compile/rules_angle_args` 1 个 1 处、`stdlib/signatures/transforms.cml` 的 `@example` 1 处 | `feat/compile/transforms`、`feat/compile/rules_angle_args`、`feat/transforms/transpose` 三族 | **必须同改**：`transpose(lin)` 也会多出端口，`pullback(1.0)`、`grad` 的每一处调用跟着动 |
| 只"体真的读到的那几个" | 只有真读到前向值的那些（`sum_t`、`mean_axis_t` 一类）；**今天数不出来**——要等图建出来才知道，已实测的只 `rules_angle_args_mean_transpose` 一处（它还停在更前面的 `tensor:multiply` 无律） | 最多一族 | 不动 |

### 两条路各卡在哪

1. **全形参**：类型层能表达，声明就是它；代价是 `vjp` 与 63 处调用点同时改，且 `grad` 的调用者要
   凭空造出"反向时不存在"的切向量值 —— 这条不是改调用点，是改 `vjp` 的形状。
2. **只真读到的**：`vjp` 不动、调用点只多一处；但**类型层看不见体**，`Transpose<F>` 无法表达它 ——
   要表达就得让算子的答案类型从**建好的图**取（图知道体读了什么），而 resolver 只拿到一个类型。
   这是一条新机制（算子答案类型由图定），不是今晚能顺手做的一笔。

### 默认建议

**走第 2 条**：`transpose` 的答案类型从**建好的图**取（resolver 与 build 同一个函数、同一张表，签名
不再各算一份），`Transpose<F>` 的声明随之改成"按体读到的那几个形参给值端口"并注明它是**下界**，
而不是"每个形参"。理由是读数把第 1 条的两样代价都摆出来了：63 处调用点 + `vjp` 的形状要一起改，
而第 2 条只多一处、且 `vjp` 的"两个形状是一"这句话仍然成立。**在这条裁定下来之前不落任何一种**：
今晚不许把残差塞进闭包，也不该为了少改调用点自己收窄。

顺带更正作者 10:50 那句的**影响面**：不是"只影响直接对非线性函数取 `transpose` 的地方" —— 照声明
执行，**每一条**直接 `transpose(f)` 的调用点都多一个值参数，线性的也一样（63 处里绝大多数是线性的
那几族）。

### 补一条实测：`split` 那半今天**没有**值端口（所以"全形参"必然改 `vjp`）

`test/cases/transforms/transpose/residuals_in_the_closure.cml` 跑一遍（`camel <case> nvm`，绿，
`back dx= 12.0 back dy= 9.0`）：

```
TRANSPOSE function=split_of_jvp_of_f_backward graph=250 type=(float) => (float, float) nodes=9 rhs=-
TRANSPOSE op ... resolved=(f: (float, float) => float pure) => (float) => (float, float) transposed=(float) => (float, float) graph=(float) => (float, float) same=yes
```

`lin` 是 `(float, float) => float`，`ParamsOf<lin>` 是两个 float；照声明执行，`transpose(lin)` 变成
`(float, float, float) => (float, float)`，而 `back` 只传了 1.0、`pullback` 也只传余切 —— 这就是
"全形参"要 `vjp` 同改的那条读数的原文。（顺带：这个用例的头注释说"两个前导参数按零传"，可源文件里
是 `back(1.0)`，注释与源码不一致，属于同一处旧说法的残留。）

## 本树五份计划读数（2026-11-03 20:50，lane-sigtable，`3d922ce3f`，主机 88zjwei，tier fast，`-Jobs 4`）

| 计划 | 读数 |
| --- | --- |
| `feat/compile/transforms` | **80 = 77 通过 / 2 失败 / 1 已知失败**（`transpose_loop_escape.two_values_of_two_kinds`、`transpose_loop_escape.second_state_of_the_same_loop`；已知失败 `vjp_loop.rounds_are_undone_in_reverse`） |
| `feat/transforms/split` | **19 = 19 / 0** |
| `feat/modules/remat` | **6 = 6 / 0** |
| `feat/compile/rulesets` | **67 = 67 / 0** |
| `feat/parse` | **115 = 115 / 0** |

**#4 与 #5 就是本页说的那份分叉，不是两条独立的路。** `two_values_of_two_kinds` 的原文：

```
TRANSPOSE function=read_outer graph=49 type=(float, float) => (float,) nodes=14 arms=0 rhs=add_t,mul_t
TRANSPOSE op ... transposed=(float) => (float,) graph=(float, float) => (float,) same=no
error: Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports.
```

`(float, float) => (float,)` 的图里那两个端口是**循环逃逸的残差**（`carried=2`），与 `sum_t` 那个
参数值端口是同一件事的两种来源；两个端口一条也没进 resolver 的答案类型。所以"把 resolver 那一份
从同一张表算"同时是 #4、#5 的第一步，不只为 `mean_axis`。
