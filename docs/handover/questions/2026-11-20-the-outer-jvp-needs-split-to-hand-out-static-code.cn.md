# 外层 `jvp` 要 `split` 交出静态可读的答案代码，那一步在 0.2.45 的范围里吗

**问句（一句）**：乙-1「变换递归进闭包」的唯一堵点是**外层 `jvp` 的 lift 不肯 lift 一个被调者代码不可读的调用**，而那个被调者正是 `split` 的产物；要让 lift 能进闭包，`split` 必须先交出**静态可读的答案代码**（今天的产物是「每次调用重造、其体在运行时才成型」的闭包）——**这是甲-1 的设计改动，不是乙能单独落的一步。请问它是不是 0.2.45 的范围？**

**默认建议（本轮按此开工）**：**不是**。它是甲-1 的产物形态改动，与 `grad(grad(f))` 属于同一件「需要 `split` 换产物形态」的事；0.2.45 **记下它**，二阶导的目标形状降到「**内层 `grad` 全绿 + 外层拒绝可见**」，`grad(grad(f))` 不进 P\*。

## 一、实测：内层四条读数齐全，外层一条都没有

`test/cases/transforms/nested/nest_named.cml`（`with <x: float> func dcube() = grad(cube)(x)`，再 `grad(dcube)(2.0)`），
`--trace transforms`，`std::nvm`，2026-11-20 03:13 本车道实测，逐字：

```
REINTERP first_with_as_param function=cube with_in=1 params_in=0 with_out=0 params_out=1 graph=359
REINTERP jvp function=cube__first_with_as_param graph=360 liftedFrom=359 type=(float, float) => (float, float) pure nodes=3 kinds=3/2 exit=Tuple{chain,tangent} tangent=computed source=ruleset rules=jvp_mul
REINTERP jvp built graph=360 nodes=8 exit=7 regions=1 function=cube__first_with_as_param
REINTERP jvp callees=0
REINTERP split function=jvp_of_cube__first_with_as_param forward=372 backward=373 type=(float) => (float, (float) => float pure) pure residuals=2
TRANSPOSE function=split_of_jvp_of_cube__first_with_as_param_backward graph=374 type=(float) => (float,) nodes=5 arms=0 rhs=-
TRANSPOSE op signature=(f: (params..) => result) => (Tangent<result>) => (cotangents..) resolved=(f: (float) => float pure) => (float) => (float,) transposed=(float) => (float,) graph=(float) => (float,) same=yes
REINTERP first_with_as_param function=dcube with_in=1 params_in=0 with_out=0 params_out=1 graph=405
```

**内层 `grad(cube)` 全绿**：`first_with_as_param` → `jvp` → `split` → `transpose` 每一步都留了读数。
**紧接着。`first_with_as_param(dcube)` 也成功了（`graph=405`），然后外层那次 `jvp` 一行 REINTERP 都没有。**

⇒ **⇒ 拒绝来自 `jvp` 的 lift，且是那条不带 cause、不带位置的拒绝**（`refusedOrigin == kInvalidOriginId`，`modules/transforms/jvp_operator.cpp:188`）：

```
stdlib/vjp.cml:175:25: [Error]: An error has occurred: transforms: jvp has no forward transform for this function.
The runtime has no diagnostic of its own for this fault: every fault falls back...
```

四种写法落在同一句上（2026-11-20 03:23 同一计划实测）：具名 `with` 函数（`nest_named`）、lambda（`nest_lambda`）、
Hessian（`hessian`，四个元素无一产出）、扰动混淆（`confusion`）。

## 二、这不是新缺陷：`compositions.jvp_vjp_f` 已经钉住了同一堵墙

`test/plans/feat/transforms/compositions.plan.toml:83`（`known_fail_reason`，原文）：

> the case's own call text is fixed, and the composition now reaches the transform layer: `jvp` refuses a call of the
> pullback value `stdlib/vjp.cml#vjp` answers (`jvp has no forward transform for this function`), because that value is
> `transpose(lin)` and the body producing it calls `split(jvp(f))`, a function value computed at run time

同一文件 `:84` 是 `known_fail_msg = "jvp has no forward transform for this function"`，`:85` 的长注记又写着
「`jvp` lifts a call of a function value only when the callee is evident」（`be43289aa`、`f0dfafba6`）。

⇒ **⇒ 乙-1 今天重测的，是这条早已记录、早已钉住的限制。** 我用四种写法把它重测了一遍，读数与它逐字吻合。

## 三、成因分工：堵点在乙的面，成因在甲的面

| | 归属 | 内容 |
|---|---|---|
| **堵点代码** | **乙（`modules/transforms/jvp.cpp`）** | lift 只看被调者的代码是否可读；不可读就拒绝，且不写 cause |
| **成因** | **甲 / `grad-callform`（`modules/transforms/split*`、`stdlib/vjp.cml`）** | `split` 的产物是「每次调用重造、体在运行时成型」的闭包（`docs/design/program-transforms/nested-transforms.cn.md` 2.2 的原话：「今天出问题的，是闭包**代码本身**要到运行时才由 `split` 造出来，而且每次调用都重造」） |
| **`grad(grad(f))` 的形状** | 作者 `fb484ca03` | `with` 参数写法是目标形状；普通参数的二阶保持为拒绝（9.9 不变） |

⇒ **⇒ 只在乙里改 lift 做不到：lift 要么相信一个代码不可读的被调者（那就是「跑通却给出错值」），要么拒绝（今天这样）。** 第三条路是 `split` 交出静态可读的答案代码。

## 四、这对「四种读数」意味着什么

```
① `x·x` 在 2.0 的二阶导 = 2.0：**一阶 4.0 有，二阶没有** —— 二阶要外层 `grad`，停在本页这堵墙
② `x³`  在 2.0 的二阶导 = 12：**一阶 12.0 有，二阶没有** —— 同上
③ Hessian：四个元素**无一产出**，整条停在这堵墙
④ 四后端一致：**今天无法测** —— 没有一个二阶读数是跑得出来的
   （`tf` 与 `prl` 是同一个后端，2026-11-20 裁定；第四项写 `prl`）
```

⇒ **⇒ 「二阶导受 9.9 约束」这一条的现代表述是**：内层 `grad` 在四条写法里全绿（`first_with_as_param` → `jvp` → `split` → `transpose` 都留了读数）；外层拒绝**可见且逐字**；把外层变成绿的唯一前提是 `split` 换产物形态。
⇒ **⇒ 普通参数写法（`grad(grad(square))(2.0)`）不是被这堵墙挡的**：它更早就被 `first_with_as_param` 按 9.9 拒绝，作者 `fb484ca03` 已裁定保持为拒绝。两条已转成 negative 用例并 PASS。

## 五、要作者裁的只有一句

`split` 交出静态可读的答案代码，是不是 0.2.45 的范围？

- **甲（默认建议）**：不是。记进 0.2.45 的已知限制，`compositions.jvp_vjp_f` 的 `known_fail` 保留原样（它的注记已经把这件事写清了），二阶导目标形状降到「内层全绿 + 外层拒绝可见」。
- **乙**：是。那么它要先于乙-1 排期，且需要甲的面（`split`/`transpose`）先动，乙才能接着做 lift。