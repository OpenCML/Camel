# 一个 stated 归约的 extents，由谁在编译期说出来

**状态**：两半都落了并测绿。`at` 说得出来就说 extents（第 3 节），说不出来的秩 0 那半靠律的端口（第 4 节，8/8 SIGSEGV → 8/8 PASS）。

问句是一句：**`mean_axis_stated` 的结果秩，靠把 `at` 变成编译期常量来拿到，还是靠让余切在进入律的时候拿到那个秩？**

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

红的两个正是**今天绿的两条**：`stated_mean_axis_gradient`（登记过）与秩 0 那条。所以拒绝不是"更诚实"，是**拿编译器的缺陷去驳回一个合法的程序**：extents 就在操作数的类型里，只是没写在 `at` 里。这与"走不到的状态必须说这是编译器缺陷，不是程序的错"直接冲突，因此**采纳第 3、4 节，不采纳拒绝**。

## 3. 落地一：说得出就说 extents，说不出就说元素类型与一个未定的秩

`modules/tensor/ops/reduction_ops.cpp#meanAxisStatedInfer`：`at` 是编译期常量时按 `reduceShape` 算出真形状（`at[0]` 是轴、`at[1]` 是标志、`at[2:]` 是操作数自己的 extents），并顺带在编译期比一次"声明的 extents 个数与操作数秩"；`at` 是运行值时回答操作数的元素类型 + 未定的秩（`T[..]`），**并在注释里写明为什么这是它能说的最诚实的类型**：秩 0 的答案在数组类型下由 `modules/tensor/ops/support.h#wrap` 装成秩零数组，值不与类型矛盾。

配套：`modules/tensor/module.cpp#kDeclaredOperators` 把 `mean_axis_stated` 从"声明式"里删掉（`sum_stated`、`take_stated` 留着——一个永远答一个数、一个永远至少答读到的那些行，声明的结果就是结果），`stdlib/tensor.cml` 里那行声明退成说明，`stdlib/signatures/tensor.cml` 记录它算出来的签名。机制与 `lane-statetypes` 对轴归约做的一样。

钉子：`test/cases/compile/transforms/stated_mean_axis_declares_the_extents.cml` + `test/plans/feat/compile/stated_extents.plan.toml` 的 `stated_mean_axis_declares_the_extents`。它读 `std::gir`：那个节点的 `value_type="float"` 恰好一处，且**没有**通向 `float` 的 `CAST`。改之前这里是 `float[..]` 加一个 `CAST`——正是让 `vjp` 在 `mean_axis` 上 `jvp has no forward transform` 的那次隐式转换。

`at` 拿不到常量时**没有诊断**（它安静地回答 `T[..]`）；有诊断的是两种说得清的情况，原文：

```
mean_axis_stated expects `at` = `[axis, keepdims, d0, ..., d(rank-1)]`: the axis, the flag and the
operand's N extent(s), and the list has M entries
```
```
mean_axis_stated states N extent(s) and the operand has rank R: `at` is
`[axis, keepdims, d0, ..., d(rank-1)]`
```

## 4. 落地二：秩 0 那半，杠杆是律的端口，不是 walk 的判据

**先说我原来的判断，再说它被量翻的地方。** 我原先写的是"改 `ruleCotangent` 的判据：端口是数组 + 传入余切来自无秩空间"。照改、照跑：**8/8 还是 SIGSEGV**。插桩否掉了这条：

```
[probe] rule=mean_axis_stated_t pos=0 ctValid=1 kind=1 ctType=float[..] port=float[..] valueType=float[] noRank=0
[probe]   node 2 kind=1 type=bool    idx=3
[probe]   node 3 kind=1 type=float[] idx=4      <- ct 就是这个 Port
[probe]   node 8 kind=11 type=float[..] idx=7   <- mean_axis_stated 本身
[probe] ct ref=3
```

`kind=1` 是 `Port`：**余切自己就是一个 `float[..]` 类型的入口**，不是"类型说数组、值没秩"能分辨的东西——链路上每一个类型都跟着那个谎走（`cotangentTypeOf` = `tangentSpaceOf(节点类型)`，而节点类型是 `at` 为运行值时算出来的 `T[..]`）。`addCastNode(port)` 在源类型已经是 `float[..]` 时，`ArrayType::castSlotFrom` 走 `assignableFrom` 分支，**什么都不做**。所以"在 walk 里装箱"这条路，判据写成什么样都够不着值。

**真正的杠杆是律的端口**，`stdlib/rulesets/transpose.cml#mean_axis_stated_t`：

```diff
-    mean_axis_stated(t, at) => (ct: float[..]) => mean_axis_cotangent(ct, at)
+    mean_axis_stated(t, at) => (ct: T) => mean_axis_cotangent(ct, at)
```

律把余切声明成**元素**，于是 `mean_axis_cotangent(ct: T[..], …)` 的实参成为"值进入数组位置"的那个入口，语言自己的 `T` -> `T[..]` 转换（`ArrayType::castSlotFrom` / `Builder::coerceTo`）就发生了——`numel(ct)` 答 1。**这正是 `sum_stated_t` 端口写 `(ct: T)` 所以同情形不炸的差别**（`docs/handover/questions/2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer.cn.md` 量到的），只是它落在"律的端口"而不是"walk 的判据"上。

量出来的读数（同一棵树，2026-11-04 06:5x–07:0x -05:00，load 15.5–18.2）：

| 情形 | 手算 | 实测 |
|---|---|---|
| `mean_axis([1,2,3,4], 0, false)`，秩 0 答案，8 条相同程序 | `dL/dx = [0.25] × 4` | **8/8 PASS**（改前 8/8 SIGSEGV，`rc=139`，`numelKernel`） |
| `mean_axis(x[2,3], 1, true)`，余切 `[2]`（`numel(ct)` 必须是 2 而不是 1） | 每位置 `1/3` | `[[1/3×3],[1/3×3]]`，**不是 1/6** |
| `stated_extents` 全计划（含 `keepdims=true` 那条） | —— | **8/8 PASS** |

**代价与归属**：这一行在 `stdlib/rulesets/**`，是本轮被焊死的文件（`lane-axistated` 在那条船上），所以它单独占一笔提交，可以按 hunk 取、也可以整笔丢掉；丢掉时把 `stated_mean_axis_element_gradient` 那条用例的登记一起回退即可（那口钟是它带绿的）。闸门读数在这一行落地前后**完全一样**：`feat/compile` `2245/2232/2/11`、`feat/compile/transforms` `105/98/2/5`、`feat/transforms` `29/21/0/8`、`feat/transforms/split` `19/19`、`feat/modules/stdlib_registry` `13/13`。

**（乙）让 `jvp_mean_axis` 把 extents 写成编译期常量。** 做不到：extents 是 `shape(xs)`，是原值。除非语言给"运行期形状常量"一个类型（`lane-extentof` 那族的 `extent_of`）。

**（丙）`mean_axis_stated` 拒绝运行期的 `at`。** 第 2 节已测：2 红，且是把编译器缺陷算到程序头上——**不必了**，第 4 节已经解开。

**留下的一个真问题**：`at` 是运行值时 `mean_axis_stated` 的类型仍说 `T[..]`，所以"这个节点的答案有没有秩"在类型上**永远问不出来**。今天靠律的端口绕过去了（律知道自己的右值要什么），但这意味着**每一条接 `*_stated` 的反向律都得自己声明成元素**，否则同样的 SIGSEGV 会在下一个 family 上重演。要不要让"stated 归约的答案秩"成为一个能问的类型，是作者的问题；我这一轮只能把它记在这里。