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

## 读数（本机不构建；主机 88zjwei，树 `~/camel-work/arity`，tier fast）

| 计划 | 读数 |
| --- | --- |
| `feat/compile/transforms` | 80 = **77 通过 / 2 失败 / 1 已知失败**（两条红的正是 #4、#5） |
| `feat/compile/rulesets` | 67 = **67 通过 / 0 失败** |
| `feat/transforms/split` | 19 = **19 通过 / 0 失败** |
| `feat/compile/rules_angle_args` | 6 = **0 通过 / 6 失败**，前四个数字已对（30.0 / 5.0 / 1.5 / −0.25），第五个等本页 |