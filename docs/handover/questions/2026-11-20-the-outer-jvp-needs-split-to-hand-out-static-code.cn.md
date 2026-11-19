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

## 五、作者的裁定（已到，2026-11-20 05:55，`36f91f6a4` 第 2 条 / `ea05b6826`）

**问句已答**：这条路在 0.2.45 的范围**里**，而且不是降级、是改表示层。原文：

> `lane-nested-transforms` 的活是「`split` 把反向一半建成**静态图** + `jvp` 对代码已知的闭包做前向」，与 `grad-form` 合力

> second order goes by the planned shape, **split builds the backward half as a static graph**

⇒ **⇒ 所以这页从「要不要做」变成「做在哪里」。** 第六节是读码之后的答案：**图已经是静态的，缺的是读法**，
而且补读法的那一侧正好落在 `modules/transforms/`（本车道的面），`split` 侧可以保持现状。

被否掉的那条（我原先的默认）：~~记进 0.2.45 的已知限制、二阶导目标形状降到「内层全绿 + 外层拒绝可见」~~。

## 六、`split` 今天交付什么：图已经是静态的，缺的是「读法」（2026-11-20 03:52 读码写入）

作者 `36f91f6a4` 第 2 条把这条车道重新定向到「`split` 把反向一半建成**静态图**」。读完 `modules/transforms/split.cpp`（4120 行）之后，这条的要求**比它听上去小**：图已经是静态的，缺的是 `jvp` 不去看它。

**① 今天就是静态的部分**

| 东西 | 位置 | 形态 |
|---|---|---|
| 前向/反向两半的**图** | `split.cpp:3445 buildSplitGraph`、`:1767 splitHalvesWith`、`:2439 cutLoopGraph`、`:2908 cutBranchGraph`、`:1963 buildHalfBranch` | 在 `GraphDraftGroup` 里 `reserve` 出 `split_of_<f>_backward` 这样的 `GraphId`，**一次建好、发布进 program**，之后是普通的 `GCGraph` |
| 反向一半的签名 | `split.cpp:263 splitOperatorType` | `(残差…) => (cotangent) => (cotangents…)`，残差**不进 `split` 的结果类型** |
| 残差清单 | `split.cpp:1217 residualsOf`（按出口 pair 的 result 半边 / tangent 半边划前后） | 静态算出来的 `std::vector<Residual>` |
| 反向图的形参 | `split.cpp:1173 addResidualCaptures` | 每个残差一个 closure port |

**② 运行期才造的，只有那个「壳」**

`split.cpp:1202 buildLinearClosure` 的注释是这件事的原文：

> A FILL of the `FunctionClosure` kind -- ... the static input is a function value naming the backward
> graph, whose closure layout is the one this build read from that graph, and the `with` inputs are
> the residual values in slot order. ... the values do not exist before the forward half runs, so
> `lin` is built where they do, and what it captures is stated by no signature.

⇒ **⇒ 每个调用点造的是一个 `FILL(FunctionClosure)` 值**：静态源是 `Function(backwardId)`，`with` 是这次调用的残差。**代码（反向图）不是每次重造的；重造的是「把静态图和这次的值绑在一起」的那个值。**

这比设计稿 2.2 那句「闭包代码本身要到运行时才由 `split` 造出来，而且每次调用都重造」更精确：**代码已经是静态的**；今天每次调用重造的是闭包实例。

**③ 缺的一块：`jvp` 只认 `Func` 节点**

```
src/runtime/graph.cpp:3300  GraphId GCGraph::calleeIdOf(gc_node_ref_t ref) {
                                if (!n || n->kind != GCNodeKind::Func || !n->hasPayload()) return kInvalidGraphId;
                                ... }
modules/transforms/jvp.cpp:1037  const GraphId calleeId = function.calleeIdOf(ref);
                                 if (!rt::isValid(calleeId)) { return false; }   // ⇒ 空拒绝
```

`FunctionClosure` 在仓里被 **28 个文件**读到（`split.cpp`、`rule_expand.cpp`、`autodiff/pullback.cpp`、
`execute/exec_plan.cpp`、`defunctionalize.cpp`、三个 VM、C++/JSON 发射器……），
**但在 `modules/transforms/jvp.cpp`、`jvp_operator.cpp`、`transpose.cpp`、`vmap.cpp` 里出现 0 次。**

⇒ **⇒ 所以「代码已知」在图里是成立的，在 `jvp` 的读法里不成立。** `split` 交出的闭包值，`jvp` 的 lift 根本不认识这种节点，
`calleeIdOf` 对它答 `kInvalidGraphId`，lift 在那里 `return false`，于是 `jvp has no forward transform for this function`，不带 cause、不带位置。

**④ 这条对新目标意味着什么**

「`split` 把反向一半建成静态图」这一步，**在 `split` 这边基本已经到了**；要走完，缺的是**读的一侧**：

```
· 让 `jvp` 的 lift 认识 `FILL(FunctionClosure)`：静态源 → `Function` → `GraphId` → 与 `Func` 节点同一条 lift 路径
· 残差（`with` 输入）成为这次 lift 的参数，与今天 `liftFuncValue` 处理被调者 `with` 列表的那条路一致
· `transpose` 同理（它今天也不认识 `FunctionClosure`），否则反向一半转置不出来
```

⇒ **⇒ 这不是「换产物形态」，是「让两个变换认识已经存在的产物形态」。** 与 `grad-form` 合力时，分工可以是：`split` 侧保持现状（图已静态），
`modules/transforms/` 侧补 `FunctionClosure` 的读法 —— 这正好落在本车道的面里。

⇒ 附带读数：`c74e59759`（`grad-form`）把 `grad` 的调用写法恢复成 `grad(f)<w, ...ws>(...xs)` 之后，
**`grad(grad(square))<2.0>()` 不再撞 9.9 的那条 `first_with_as_param` 拒绝** —— 内层 `grad` 现在交出的是 `<w: float>() => float`，
有 `with` 参数可移。六条 nest 用例因此**全部落到同一堵墙**上，`test/plans/feat/transforms/nested.plan.toml` 已按此改写（六条 `known_fail`、两条 negative）。