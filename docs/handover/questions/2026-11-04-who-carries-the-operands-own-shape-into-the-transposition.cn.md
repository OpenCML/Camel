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

## 读数五：按可达性逐条丢，上限是 22 → 18，而且剩下的正好全是 `split` 那 18 条

"余切最终落不到任何参与参数就整条丢掉"这条路，先只做分析（不动代码）：在同一个入口上对每条命中问两件事——
**谁在读这个值**（源图上它的消费节点），以及**这条需求能不能落到参与微分的参数上**（余切顺着 `normInputs`
往上追，追到参与参数才算有着落；`Func`/`Loop`/`Brch` 与区域里的节点一律算"有着落"，所以"没着落"是证明不是猜）。
另问一句：这个端口本身是不是答案位置（`selfBinds`）。22 条命中的分布：

| 条数 | 谁在问 | 读这个值的节点 | 需求是什么 | 能不能丢 |
| --- | --- | --- | --- | --- |
| **18** | 五张 `split` 后一半 | **`Func`（kind 10）把它当一条直接调用的第 0 个实参** | 见下方读数六：转置这条调用要给被调成员一个数组的值；16 条里这个端口本身就是答案位置（`selfBinds=1`） | **不能丢**——它不是余切需求，"余切落不到参与参数"这句话对它没有适用面 |
| **2** | `__g_242__`（手写闭包 `(dx) => 2.0 * x * dx`） | `op/mul_d`，目标 `2.0*x`，`binds=0` | 规则要给 `2.0*x` 写余切，要 `dx` 的值；那笔余切落不到任何参与参数 | **空转，可丢** |
| **2** | `autodiff::mul_tangent<float>` | `op/mul_d`，`selfBinds=1` | 真需求，但闭包为空（`closureCount=0`），今天本来就取不到 | 取不到；不影响结果 |

按"逐条按可达性丢"落地（只丢 `binds=0` 的贡献，其余一个字不动）验证这条路的**上限**：

```
[SUMMARY] total: 87, pass: 84, fail: 2, known failures: 1
命中 22 → 18（丢掉 2 条 __g_242__ 的空转贡献；另 4 条贡献在 autodiff::div_tangent 里本来就不问值）
transpose_hand_closed_lambda: cut ct1= (6.0)  cut ct3= (18.0)  hand ct1= (6.0)  hand ct3= (18.0)
```

**结论，也是本页最该留的一句：空转的那 2 条恰好都不在 `split` 后面；`split` 后面那 18 条一条都不是空转。**
所以判据 ①′（`split` 后面那 20 条降到 0）**同样到不了 0**——这条路的上限是 18。
（那 18 条的取值来源见下一条读数：**不是**"函数值的闭包捕获"——本页初稿这么写过，读数六把它改正了。）

## 读数六：那 18 条的消费节点是**一次调用的实参**，不是闭包捕获（本页初稿说错了，这里是更正）

三条，各一句：

1. **谁建的**：`jvp` 在 `modules/transforms/jvp.cpp#buildJvpGraph` 的 `Func` 分支里把一条调用**提升成"每个实参送
   `(值, 切线)` 两格"**（`arguments.push_back(half); arguments.push_back(tangentOf[input]);`，注释原文
   "a call is lifted as a call: the lifted callee takes pairs"），这个 `Func` 节点是那时建的；`split` 造后一半时
   由 `modules/transforms/split.cpp#makeCopy` 的 `Func` 分支整条复制过去（`draft.setNormInputs(made, inputs)`）。
2. **那一格在切点之前是谁的值**：**切线本身**。它不是"从参数端口读出来的原值"，也不是跨切点搬过去的残差——
   `jvp` 建它时递的是 `tangentOf[input]`，即提升签名里**切线那一格**；而切线那一格在后一半里本来就是它自己的
   形参。读数原文：`REINTERP split function=jvp_of_g forward=271 backward=272 … residuals=5`、
   `TRANSPOSE function=rulesets.jvp::index_into<float> graph=300 type=(int, float[], float) => (float[],)`。
3. **为什么落在切点之后**：**是 `makeCopy` 复制过去的，不是 `split` 新造的**。而且它没有"跨切点"，所以捕获表
   一个字都不用改：`makeCopy` 的端口分支把 `n+0` 映到 `portOf[n+0]`，就是后一半自己的切线端口。后一半也必须
   留着这条调用——`lin` 的体本来就是拿切线算（`lin(dxs) = dxs[0] + dxs[1]`），它不是"切点之前的常量"。

于是转置侧拿它做什么：`transpose.cpp#buildTransposeMember` 的 `Func` 分支要给被调成员的每个值形参递一个值
（`forward(inputs[argumentAt + i])` 与 `calleeNeeds` 那一轮），而 `index_into` 的转置成员签名是
`(int, float[], float) => (float[],)`——它要**数组那个值**（形状）；这条调用递过去的数组却是 `dxs`。按位置取值
交出去的是残差 0，也就是原值 `xs`。同型、同形状、`sum_to` 只要形状，所以今天对得上。

**归类**：不是"`split` 新造、可以挪到前一半"，也不是"复制了一个捕获了切点之前值的函数值、要改捕获表"——
是"**必须被复制的调用，它的实参就是切线**"。所以它落回"身份 vs 位置"：转置成员要的那个数组值，今天从
"闭包第 `at` 格"取到的是**原值**，而这条调用的实参是**切线**；两者同型同形状，只有"该是谁"不同。

（读数六全程只读代码与既有日志：`modules/transforms/` 一个字没动，两个探针都只跑在别处已撤回的树上。）

## 三条路，各带代价

- **甲：切点带身份。** `split` 把每个残差"是谁的值"记下来（`Residual::parameter` 已经在手边，
  `split.cpp#residualsOf` 也已经据此把参数的值排进各自己的槽），`transpose` 按身份取。代价：这就是 14:55 否掉的
  `34788b554` 方向（"私下约定只是从字段换成了位置"），要作者先收回那一条。
- **乙：律不再向前向要这个值。** `sum_to(ct * other, operand)` 的第二实参改由余切/类型给出形状，或让 `jvp` 的前一半
  把切线的形状也当值算出来。代价：`mul_tensor_t` 这一类定律要重写，`jvp` 前一半多算，是规则集/契约的改动。
- **丙：保持现状（默认建议）。** 那段继续存在。可达性分析给的是硬上限：**22 → 18**，`split` 那 18 条一条都丢不掉
  （它们不是余切需求，是"一条调用的实参就是切线"），另 2 条本来就取不到。代价已经量清：20 条用例名与每行
  `ref/at` 在车道报告里，将来谁想动它不必重测。

## 默认建议：丙，暂不动，等裁定

甲乙都要改设计边界（一个是裁定本身，一个是规则集契约），而丙的全部代价这一轮已经量清。所以本车道**不提交任何
语义改动**：三条候选一条量成 27 红、一条量成空操作、一条（按可达性逐条丢）量成 22 → 18 且剩下的动不了。判据 ①
（及其更正 ①′）与 ② 今天**不能同时成立**——这是本页要留下的那句话，而且现在知道卡在哪：**不是余切，是一条
把切线数组当实参传给提升后被调函数的调用**。

## 那 2 条不在 `split` 后面的命中，各自从哪取值

- **`transpose_hand_closed_lambda.same_answer_as_the_cut`，`function=__g_242__`**：用例正文是 `(dx) => 2.0 * x * dx`
  手写的闭包，`split` 没参与。期望输出 `hand ct1= (6.0) / hand ct3= (18.0)` 今天**正是靠这条按位置取值**才成立：
  它要的是 `dx` 的值去写"落给 `x` 的那一笔"（那笔不出去，但走遍要写），闭包里的 `x` 与 `dx` 同型，于是被交出去。
  用例 desc 原文说 `34788b554` 之后"a closure a caller wrote records nothing"会让它拒答——身份路修不了它，位置路才能。
  可达性分析进一步说：**这笔需求是空转的**（`binds=0`），所以它也可以丢——按可达性丢完，这一条仍逐字答
  `hand ct1= (6.0) / hand ct3= (18.0)`。两件事不矛盾：它今天靠位置取值**成立**，但不是**非靠不可**。
- **`autodiff::mul_tangent<float>`**：库里的规则函数直接被转置，`closureCount=0`，闭包里没有可选成员，
  按位置取值**取不到**，落到 `TRANSPOSE-NEEDS`；这一条今天不影响结果（用例绿），而可达性分析说它是**真需求**
  （`selfBinds=1`）——这两句话合起来说明"取不到"和"不需要"是两件事。

## 取证位置

- 按位置取值那段：`modules/transforms/transpose.cpp#buildTransposeMember`（`forward` 里 `closurePorts[slot]` 那一支）
- 后一半端口表：`modules/transforms/split.cpp#buildSplitGraph`（后一半的端口循环）
- 闭包端口：`modules/transforms/split.cpp#addResidualCaptures`
- 残差的编号与排序：`modules/transforms/split.cpp#residualsOf`
- 定律：`stdlib/rulesets/transpose.cml#mul_tensor_t`
- 读节点复制：`modules/transforms/split.cpp#makeCopy`
- `Func` 节点谁建的（提升一条调用为 `(值, 切线)` 两格实参）：`modules/transforms/jvp.cpp#buildJvpGraph`
- 转置侧为什么要实参的值：`modules/transforms/transpose.cpp#buildTransposeMember`（`Func` 分支的成员实参一轮与
  `calleeNeeds` 一轮；两条拒绝 `call-value-missing` 与 `call-value-computed-by-the-body` 也在那里）