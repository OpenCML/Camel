# 转置展开律时要"操作数自己的值和形状"，这件事该由哪一侧承载？

## 问什么

`modules/transforms/transpose.cpp#buildTransposeMember` 的 `forward` 里有一段"按位置取值"：一个参数没有端口时，
它把闭包里**第 `at` 个成员**当作"参数 `at` 自己的值"，并且**检查类型相等**才交出去。

2026-11-03 14:55 的裁定要求它消失：`transpose` 遇到闭包端口就经 `forward` 转发，不按位置去找参数的值；配套的
`TRANSPOSE-NEEDS` 拒绝路径删掉。要裁的一句是：

> **转置展开一条展开律时，需要"这个操作数自己的值和形状"（`sum_to` 的第二参数），这件事该由哪一侧承载——**
> **`split` 把值放进闭包并说明它是谁的（身份），还是律本身不再向前向要这个值？**

## 判据的更正（这一条是合并车道改的，原判据一并留在这里）

- **原判据**：`POSPROBE-HIT`（`transpose` 按位置取值入口）命中数 **20 → 0**。
- **更正为 ①′**：**`split` 后面那 20 条降到 0**；剩下 **2 条与 `split` 无关**，单列在本页并各自说明取值来源。
- **为什么改**：原判据到不了 0，不是标准放宽，是数出来的——那 2 条命中所在的函数根本不是 `split` 造出来的
  （`__g_242__` 是手写闭包，`autodiff::mul_tangent<float>` 的闭包是空的）。要记的正是"原判据不成立"。

## 读数一：同一个入口上，22 = 20 成功 + 2 取不到

插桩在 `transpose.cpp#buildTransposeMember` 的 `forward` 按位置取值那段入口（环境变量 `CAMEL_POSPROBE=1` 打开），
一整份 `test/plans/feat/compile/transforms.plan.toml` 上：

- **22 条命中，11 条用例**；
- 其中 **20 条按位置取值成功**（闭包里有同型成员）——这就是原判据里那个 20；
- 另 **2 条取不到**：`function=autodiff::mul_tangent<float>`，`closureCount=0`，落到 `TRANSPOSE-NEEDS`。

命中分布（`ref/at` 逐条留在车道报告里）：`split_of_jvp_of_g_backward` 10、`split_of_jvp_of_read_at_backward` 2、
`split_of_jvp_of_mixed_backward` 2、`split_of_jvp_of___g_250___backward` 2、`split_of_jvp_of_both_backward` 2、
`__g_242__` 2、`autodiff::mul_tangent<float>` 2。

## 读数二：那个 `ref` 是图自己的普通端口，不是 `makeCopy` 造的读节点

22 条全部同一个形状：

```
POSPROBE-HIT function=split_of_jvp_of_g_backward at=0 i=0 place=0 ref=5 normPort=1 closureNode=0 kind=1 params=1 closureCount=5 ask=float[]
POSPROBE-HIT function=split_of_jvp_of_read_at_backward at=1 i=0 place=0 ref=2 normPort=1 closureNode=0 kind=1 params=2 closureCount=2 ask=float[]
POSPROBE-HIT function=split_of_jvp_of___g_250___backward at=0 i=0 place=0 ref=3 normPort=1 closureNode=0 kind=1 params=1 closureCount=3 ask=float
POSPROBE-HIT function=__g_242__ at=0 i=0 place=0 ref=0 normPort=1 closureNode=0 kind=1 params=1 closureCount=1 ask=float
POSPROBE-HIT function=autodiff::mul_tangent<float> at=0 i=0 place=0 ref=0 normPort=1 closureNode=0 kind=1 params=4 closureCount=0 ask=float
```

`normPort=1`、`closureNode=0`、`kind=1`（`GCNodeKind::Port`）、`place=0`：**`ref` 是这张图 `normPorts()` 里的第 0 个
端口**，也就是 `split.cpp#buildSplitGraph` 建后一半端口表时 `addPortNode` + `appendNormPort` 出来的那两个节点，
**不经过 `makeCopy`**。

这三轮量到的是同一件事，取证位置各一处：

| 这一轮 | 取证 | 结论 |
| --- | --- | --- |
| 去掉这段，`feat/compile/transforms` 87 → 74/12/1 | `transpose.cpp#buildTransposeMember` | 这段今天被用着，不是死代码 |
| `makeCopy` 的 `portIndex` 分支插桩一次都没进 | `split.cpp#makeCopy` | 抱怨的 `ref` 不是 `split` 从源图复制的读节点 |
| 22 条全部 `normPort=1 / closureNode=0 / place=0` | `transpose.cpp#buildTransposeMember` + `split.cpp#buildSplitGraph` | 它是图自己的参数端口，由**端口表**那一段直接建出 |

所以"体里经参数端口读值"这条路的源头在**建签名/端口表**那段（`split.cpp#buildSplitGraph` 后一半的端口循环，
以及 `split.cpp#addResidualCaptures` 的闭包端口），不在 `makeCopy`。这一层三轮独立量到，可以当结论用。

## 读数三：照 14:55 那条改端口表，命中掉到 4，代价是 27 条红

改法：后一半端口表里，凡"该参数的值已被切点当残差带走"（`residualIndexOf` 里有它的值端口）就把 `portOf[i]`
指向 `projectResidual` 的闭包端口，体里读它就落到残差投影。同一棵树上：

```
[SUMMARY] total: 87, pass: 57, fail: 29, known failures: 1
命中 22 → 4（剩下 4 条全是不在 split 后面的那两个函数）
```

27 条红含：九条 `grad_*` 全灭、`vjp_api.*` 七条、`transpose_hand_closed_lambda.same_answer_as_the_cut`、
`transforms.library_grad_is_the_composition`、`grad_of_a_division`、`grad_where_the_tangent_ignores_the_operands`、
`transpose_loop_escape.*` 三条。第一条红的原文：

```
(transpose_of_split_of_jvp_of_g_backward):?:?: [Error]: An error has occurred: autodiff: a zero tangent array needs an array with a tangent.
```

**根因（这一轮最有价值的读数）**：后一半体里读那个切线端口的地方，**不是"经参数端口读参数原值"，是真的在读切线
自己的值**。`stdlib/rulesets/transpose.cml#mul_tensor_t` 的定律原文写着：an operand's cotangent is `ct * other`,
and the second half of the law is "then summed back to the shape the operand states"——右边是
**`sum_to(ct * other, operand)`**，第二个实参要的是**这个操作数自己的值**（`T[]` 的长度不在类型里，只在值里）。
把那个读改到残差投影上，`lin` 自己执行时算的就从 `xs*dxs` 变成 `xs*xs`。**红是对的，改法是错的。**

## 读数四："落在闭包端口上的余切不收集"是空操作

另一条候选：`transpose.cpp#buildTransposeMember` 里 `wantsCotangent` 加一句"目标是闭包端口就不收集余切"
（理由本来就在那句注释里：闭包里的值只被读、不当答案）。读数：

```
[SUMMARY] total: 87, pass: 84, fail: 2, known failures: 1    （与基线逐条相同）
命中仍 22（20 仍是成功）
```

因为那条要不到值的链，目标**不是闭包端口本身而是中间节点**（`2.0 * x * dx` 里的 `2.0 * x`），
`wantsCotangent` 只看一层看不到底；要做成要一条"余切最终落不到任何参与参数上就整条丢掉"的可达性分析。

## 三条路，各带代价

- **甲：切点带身份。** `split` 把每个残差"是谁的值"记下来（`Residual::parameter` 已经在手边，
  `split.cpp#residualsOf` 也已经据此把参数的值排进各自己的槽），`transpose` 按身份取。代价：这就是 14:55 否掉的
  `34788b554` 方向（"私下约定只是从字段换成了位置"），要作者先收回那一条。
- **乙：律不再向前向要这个值。** `sum_to(ct * other, operand)` 的第二实参改由余切/类型给出形状，或让 `jvp` 的前一半
  把切线的形状也当值算出来。代价：`mul_tensor_t` 这一类定律要重写，`jvp` 前一半多算，是规则集/契约的改动。
- **丙：保持现状（默认建议）。** 那段继续存在；`split` 后面最多降到 0 也停不下那 2 条，另 2 条永久依赖它。
  代价已经量清：20 条用例名与每行 `ref/at` 在车道报告里，将来谁想动它不必重测。

## 默认建议：丙，暂不动，等裁定

甲乙都要改设计边界（一个是裁定本身，一个是规则集契约），而丙的全部代价这一轮已经量清。所以本车道**不提交任何
语义改动**：两条候选一条量成 27 红、一条量成空操作，提交哪条都是没验证过的语义改动。判据 ① 与 ② 今天**不能同时
成立**——这是本页要留下的那句话。

## 那 2 条不在 `split` 后面的命中，各自从哪取值

- **`transpose_hand_closed_lambda.same_answer_as_the_cut`，`function=__g_242__`**：用例正文是 `(dx) => 2.0 * x * dx`
  手写的闭包，`split` 没参与。期望输出 `hand ct1= (6.0) / hand ct3= (18.0)` 今天**正是靠这条按位置取值**才成立：
  它要的是 `dx` 的值去写"落给 `x` 的那一笔"（那笔不出去，但走遍要写），闭包里的 `x` 与 `dx` 同型，于是被交出去。
  用例 desc 原文说 `34788b554` 之后"a closure a caller wrote records nothing"会让它拒答——身份路修不了它，位置路才能。
- **`autodiff::mul_tangent<float>`**：库里的规则函数直接被转置，`closureCount=0`，闭包里没有可选成员，
  按位置取值**取不到**，落到 `TRANSPOSE-NEEDS`；这一条今天不影响结果（用例绿）。

## 取证位置

- 按位置取值那段：`modules/transforms/transpose.cpp#buildTransposeMember`（`forward` 里 `closurePorts[slot]` 那一支）
- 后一半端口表：`modules/transforms/split.cpp#buildSplitGraph`（后一半的端口循环）
- 闭包端口：`modules/transforms/split.cpp#addResidualCaptures`
- 残差的编号与排序：`modules/transforms/split.cpp#residualsOf`
- 定律：`stdlib/rulesets/transpose.cml#mul_tensor_t`
- 读节点复制：`modules/transforms/split.cpp#makeCopy`