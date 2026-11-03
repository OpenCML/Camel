# 一个 stated 归约的 extents，由谁在编译期说出来

**状态**：能说的那一半已落并测绿（第 3 节）；**崩还在**，第 4 节列三条路各带代价，我方默认在末。

问句是一句：**`mean_axis_stated` 的结果秩，靠把 `at` 变成编译期常量来拿到，还是靠让 transpose 的 walk 按端口给余切装箱？**

## 1. 读数：`at` 在两处调用点都是运行值

在 `modules/tensor/ops/reduction_ops.cpp#meanAxisStatedInfer` 里临时打一行（`at` 取不取得到常量、操作数秩），树 `~/camel-work/statedsig`（宿主 88zjwei，2026-11-04 06:2x -05:00，load 15.54/16.18/17.62，`-Tier fast -Jobs 2`）：

```
[probe] mean_axis_stated at=runtime rank=0
[probe] mean_axis_stated at=runtime rank=0
```

两次就是两条路：`stdlib/rulesets/jvp.cml#jvp_mean_axis` 的正切调用，和 `rulesets.transpose` 反推时的那个节点。原因在 `jvp_mean_axis` 的右值本身：`mean_axis_stated(dxs, stated_axis_shape(axis, keepdims, shape(xs)))`——`axis` 与 `keepdims` 是节点操作数上的常量，**extents 是 `shape(xs)`，是原值的值**，`stated_axis_shape` 是普通的 Camel 函数调用，不是静态实参；`shape(xs)` 在 `x: float[]` 上也不可折。**这不是用例写得不够具体，是这个拼写存在的原因**：把运行期的 extents 送给反推律，本来就是这族算子的用途。

## 2. 拒绝这条路的代价（已测，未采纳）

按"`at` 必须是编译期常量，拿不到就明确拒绝"改完跑 `test/plans/feat/compile/stated_extents.plan.toml`：**total 7→6、pass 4、fail 2**，两条红都是同一句：

```
REINTERP jvp function=total skip=rule uri=tensor:mean_axis reason=Invalid call to
'mean_axis_stated' with argument types <> (float[..], int[]): mean_axis_stated states the extents
of its result in `at`, so `at` must be a compile-time constant
```

红的两个正是**今天绿的两条**：`stated_mean_axis_gradient`（登记过）与本页相关的秩 0 那条。所以拒绝不是"更诚实"，是**拿编译器的缺陷去驳回一个合法的程序**：extents 就在操作数的类型里，只是没写在 `at` 里。这与我这一轮的验收要求（"走不到的状态必须说这是编译器缺陷，不是程序的错"）直接冲突，因此**采纳下面第 3 节，不采纳拒绝**；诊断原文记在第 3 节末尾。

## 3. 落地：说得出就说 extents，说不出就说元素类型与一个未定的秩

`modules/tensor/ops/reduction_ops.cpp#meanAxisStatedInfer`：`at` 是编译期常量时按 `reduceShape` 算出真形状（`at[0]` 是轴、`at[1]` 是标志、`at[2:]` 是操作数自己的 extents），并顺带在编译期比一次"声明的 extents 个数与操作数秩"；`at` 是运行值时回答操作数的元素类型 + 未定的秩（`T[..]`），**并在注释里写明为什么这是它能说的最诚实的类型**：秩 0 的答案在数组类型下由 `modules/tensor/ops/support.h#wrap` 装成秩零数组，值不与类型矛盾；矛盾只在 transpose 的余切上——`modules/transforms/transpose.cpp#cotangentTypeOf` 用 `tangentSpaceOf(本节点类型)` 定余切的类型，而 `tangentSpaceOf(float[..])` 还是 `float[..]`，于是 `ruleCotangent` 那次装箱（它判的是"余切类型不是数组"）不触发，`numel(ct)` 把一个 double 当指针读。

配套：`modules/tensor/module.cpp#kDeclaredOperators` 把 `mean_axis_stated` 从"声明式"里删掉（`sum_stated`、`take_stated` 留着——一个永远答一个数、一个永远至少答读到的那些行，声明的结果就是结果），`stdlib/tensor.cml` 里那行声明退成说明，`stdlib/signatures/tensor.cml` 记录它算出来的签名。机制与 `lane-statetypes` 对轴归约做的一样。

钉子：`test/cases/compile/transforms/stated_mean_axis_declares_the_extents.cml` + `test/plans/feat/compile/stated_extents.plan.toml` 的 `stated_mean_axis_declares_the_extents`。它读 `std::gir`：那个节点的 `value_type="float"` 恰好一处，且**没有**通向 `float` 的 `CAST`。改之前这里是 `float[..]` 加一个 `CAST`——正是让 `vjp` 在 `mean_axis` 上 `jvp has no forward transform` 的那次隐式转换。

`at` 拿不到常量时**今天没有诊断**（它安静地回答 `T[..]`）；有诊断的是两种说得清的情况，原文：

```
mean_axis_stated expects `at` = `[axis, keepdims, d0, ..., d(rank-1)]`: the axis, the flag and the
operand's N extent(s), and the list has M entries
```
```
mean_axis_stated states N extent(s) and the operand has rank R: `at` is
`[axis, keepdims, d0, ..., d(rank-1)]`
```

## 4. 崩还在：三条路

**（甲，默认）让 walk 按端口装箱。** `ruleCotangent` 今天判"余切的类型不是数组才装箱"，而余切的类型是从本节点类型推来的（`cotangentTypeOf` = `tangentSpaceOf(节点类型)`），所以"类型说数组、槽里没有秩"这种情形它看不见。改判据为"端口是数组、且传入的余切来自一个没有秩的空间（`resultCotangentType`/`rankZeroType`）"即可；`mean_axis_stated_t` 一个字不用改。**代价**：`modules/transforms/transpose.cpp`，那里有两条车道在改（我这轮的允许范围不含它）。**收益**：`test/cases/compile/transforms/stated_mean_axis_element_gradient.cml` 从 8/8 SIGSEGV 到 0/8 并能登记进 `feat/compile/stated_extents`。默认：**这一条我留单，不越界改**。

改前改后各量一次这口钟（同一棵树、同一份 `tmp_probe` 计划、8 个相同条目）：

| | `test/plans/tmp_probe/rank0_repeat.plan.toml` |
|---|---|
| 改前（`lane-rank0` tip + `lane-statetypes`，06:17） | `total: 8, pass: 0, fail: 8, killed by a signal: 8`，每条形如 `got signal SIGSEGV (rc=139)`，崩在 `numelKernel` |
| 改后（本轮 4 文件 + 用例，06:32） | **`total: 8, pass: 0, fail: 8, killed by a signal: 8`——没动** |

也就是说：**这一半是必要的、不是充分的**。它让"写得出 extents 的调用"诚实了（第 3 节那条 gir 钉子），但走 `vjp`/`transpose` 的那条路上 `at` 是运行值，还得（甲）。

**（乙）让 `jvp_mean_axis` 把 extents 写成编译期常量。** 做不到：extents 是 `shape(xs)`，是原值。除非语言给"运行期形状常量"一个类型（`lane-extentof` 那族的 `extent_of`），代价不在这一族。

**（丙）`mean_axis_stated` 拒绝运行期的 `at`。** 第 2 节已测：2 红，且是把编译器缺陷算到程序头上。**不建议**，除非同时做（甲）——那时拒绝才是"不猜"，而不是"不给"。