# 转置图需要前向那一半的中间值，谁携带它？

## 问什么

反传那一半（`transpose`）要走一遍前向图，把每个节点的余切推回去。有些规则右边需要**前向算出来的中间值**才能算余切——`mean_axis_cotangent` 要读 `xs`，`softmax` 的反传要读前向的 `y`。问题不是"读不到"，而是**没有任何东西携带它**：pullback 的约定里，转置出来的图只接收"参数 + 余切"，前向中间值一个都不在端口上。于是走到那里只有三种下场：拒绝（`skip=`）、多出一个没人填的端口（调用时被我方的 `normPorts.size()` 打死）、或者退回去重算整个区域。

## 量到的

同一因由在五处同时红着，读数各自逐字如下：

1. **`vjp_loop.rounds_are_undone_in_reverse`**（`feat/compile/transforms`，记 `[XFAIL]`）。切出来的两半里，`residuals=1 invariants=0`：闭包只有 tape 一个成员，而反传那半要的是"体自己算出来的值"——`index_into(dxs, 0)` 的数组参数是**重算的**，不是残差。拒绝逐字：`skip=call-value-missing param=0 node=23 arg=2`。
2. **`transpose_loop_escape.two_values_of_two_kinds`**（同上，基准红）。转置出来的图与函数签名对不上，读数逐字：
   `resolved=(f: (x: float) => float pure) => (float) => (float,) transposed=(float) => (float,) graph=(float, float) => (float,) same=no`
   ——多出来的那个端口就是"前向中间值"的位置，没人填；运行到调用点时是 `Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports`，**信号级失败**，不是诊断。
3. **`transpose_loop_escape.second_state_of_the_same_loop`**（同上，基准红）。一个循环有两个状态被出口读到，切分那一步就建不出来：`split does not build this function (… this loop is on the chain the exit reads and neither half has a value for it: the cut copies a loop where the exit reads its two components through one project…)`。
4. **`rules_angle_args.mean_axis_transpose`**（`feat/compile/rules_angle_args`）。`mean_axis_cotangent` 的右边读前向的 `xs`，`mean_axis_t` 的三处端口里第二、三处没人问余切。把它那三处数目的闸去掉之后，走完得到的图收 `(x, ct)`（`type=(float[..], float) => (float[..],)`），而函数值签名承诺 `(float) => (float[..],)`——同样多一个端口，同样在调用点被 `nargs.size() == normPorts.size()` 打死（此前记过 3 条 killed）。
5. **`rules_angle_args.{reshape,softmax}`**（同上一支）。这两条今天已经能打印出前四个数（`value=30.0 tangent=5.0`、`value=1.5 tangent=-0.25`），卡在第五个 `transpose=6.0`；当前逐字停在 `skip=rule uri=tensor:multiply reason=no-rule`。

第 5 条与前面四条**不是同一个成因**：`reason=no-rule` 说的是转置规则表里根本没有 `tensor:multiply` 的律（前向的 `jvp_mul_tensor` 是这一站才有的），缺的是一条律，不是携带。把它记在这里，是因为它挡在同一条用例的前面，容易被当成同一件事。`tensor:mean_axis` 那道 `skip=arity` 也同理：它数的是三个操作数，而 `int` 与 `bool` 没有切空间才是真正的原因，去掉它换来的不是绿，是那个多出来的端口。

## 什么不是原因

- **不是识别名字**：键在 `bda`…`c23186a41` 这一段已经是派生的（`float/1`、`mul_tensor/2`），四族换算一律有键。
- **不是闭包端口找不到参数原值**：那一项（参数原值作为残差进闭包）已经落地（`34788b554`），`vjp_loop` 那半 `invariants=0` 说明它**不读任何参数的值**，所以那一项修不好它——缺的是"这半自己算出来又读回去的值"。
- **不是残差数量不够**：`residuals=1` 里的那一个（tape，`ref 0`，类型 `(float, float[], float, float, float)[]`）确实在闭包里；缺的是循环**结果**，不是它的输入。

## 建议（默认）与那个岔口

**默认建议：把残差按端口带进转置图**，与已落地的"后一半经闭包端口读参数原值"同一条思路的延伸——闭包现在带的是参数与 tape，再往前一步就是"前向那一半读过的中间值，凡后一半要用的，都作为残差按端口带过去"，端口序号与需求的对应关系写在同一张表里，缺了就拒（而不是多出一个没人填的端口）。

岔口是**抄区域 vs 让残差携带值**：

- **抄区域**：反传那一半把循环/分支整块复制一遍，自己再算一次中间值。它不需要新的残差约定，但"整块复制"就是 `split` 今天已经在做、并且被 `second_state_of_the_same_loop` 证明做不干净的那件事（出口只读两个分量、入口只有一个值）。
- **让残差携带值**：前向那一半把值算出来，后一半按端口读。它要动的是 pullback 的端口约定（今天只收参数与余切），代价是**每一个** `split` 出来的转置图都要多声明端口，签名与图的对应关系成为必须守住的不变量。

两者选一，不能各做一半：只抄区域会让"多出来的端口"消失而把正确性押在复制是否完整上；只加残差而不动端口约定则仍然是"算得出、交不进去"。这条与
`2026-11-03-does-a-one-member-closure-prove-the-slot-identity.cn.md` 里"单成员闭包的类型校验证明不了身份"是同一个墙的两面——那一页问的是**怎么认出**一个端口是这个参数的值，这一页问的是**有没有**这样一个端口。

## 未测出

- 没有量过"抄区域"在真实用例上的代价（只在 `second_state_of_the_same_loop` 上看到它建不出来）。
- 没有量过"每个中间值都进残差"会把转置图的端口数推到多少：`transpose_many_parameters` 那一族可以给一个上界，但今天没有一条用例是"中间值多于参数"的形状。
- `rules_angle_args` 那两条要先等到 `tensor:multiply` 有转置律才可能走到这道墙前面，所以第 5 条的读数今天只到 `reason=no-rule`。