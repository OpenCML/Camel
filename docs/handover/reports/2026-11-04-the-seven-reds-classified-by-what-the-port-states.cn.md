# 七条红的甲/乙实测：缺的是别人的**值**，不是自己的形状

**本页只回答一句话**：那七条红里，**被回答的那一位**的切线类型是不是陈述过的？如果是，律为什么不直接读 `extent_of`（或 `statedExtentsNodeOf` / `array.h#statedExtentsOf`）拿到形状？

树 `D:\Projects\Camel-wt\zerocarrier`（基线 `d7f15f2b3` + 本车道四个提交），宿主 `88zjwei`，宿主时钟 2026-11-04 00:5x–01:2x（`-05:00`，本机 13:5x–14:2x），1 m load 21.5–26.3。**结论先说**：七条里**没有一条**的拒绝是"被回答那一位的形状陈述了却拿不到"。七条报出来的那一次失败，读的都是**别的操作数的值**，而那个操作数的类型全都陈述过（甲，但缺的是**值**不是类型）；其中五条另有一半是**真·乙**（被回答那一位是开秩 `float[..]`）。

## 1. 探针（临时，已撤）

在 `modules/transforms/rule_expand.cpp#expandRuleRightSide` 里加了一段只写 `/tmp/zc_probe.txt` 的打印，位置正好是"形状答不出、值也拿不到"的那一处拒绝：

- `PROBE-ENTER position=<n> law=<左式的算子里程碑> types=<instance.typeText> ports=[<左式各操作数的类型>] ops=[<答案里出现的算子>]`；
- `PROBE pos=<i> law=<...> arg=<第几个操作数> capture=<ref> kind=<节点种类> captureType=<捕获端口的类型> portType=<rulePortsOf 给的左式端口类型> extents=<stated|open> readers=[<谁读这个捕获：种类:算子>]`。

`ports` 就是 `rulePortsOf(instance)`（左式的操作数类型，也就是 `statedShapeOf` 读的那一处），`captureType` 是答案闭包里那个捕获端口的类型，`readers` 是答案体里读它的节点（`11:` = 算子，`10:` = `Func`，`1:` = `Port`）。

一次构建（`-Plan test/plans/feat/compile/transforms.plan.toml`，`build 527.3 s`，`[SUMMARY] total: 170, pass: 151, fail: 5, known failures: 14`，**与无探针时逐条相同**），然后七个用例逐个手跑（`camel <case> nvm`），跑完 `git checkout --` 撤掉探针，树回到四个提交的状态。探针没有改动任何行为：同一次运行里那份 plan 仍是那五条红，一条不多。

## 2. 逐条实测

| 用例 | 律与实例类型 | OUT 的 position（被回答的操作数） | 该位置答案要读的两个操作数（原话） | 甲还是乙 |
| --- | --- | --- | --- | --- |
| `erf_gradient_both_legs` | `mul_tensor_t`，`types=float, float[..], float`，`ports=[float[..] float]` | `0` → `x: float[..]` | `PROBE pos=0 arg=1 captureType=float portType=float extents=open readers=[11:tensor:multiply]`（要 `w` 的**值**，`w` 是**数**）；`PROBE pos=1 arg=0 captureType=float[..] extents=open readers=[11:tensor:shape]`（要 `x` 的**形状**，开秩） | 报出来的那一次是**甲**（`w: float` 陈述过，值没带）；另有**乙**（`x: float[..]`） |
| `erfc_gradient_both_legs` | 同上（`tensor:erfc`） | `0` → `x: float[..]` | 同上两行，逐字相同 | 同上：甲 + 乙 |
| `transpose_gradient_both_legs` | `mul_tensor_t`，`types=float, float[..], float[2, 2]`，`ports=[float[..] float[2, 2]]` | `0` → `x: float[..]` | `PROBE pos=0 arg=1 captureType=float[2, 2] portType=float[2, 2] extents=stated readers=[11:tensor:multiply]`（要 `w` 的**值**，类型**陈述了**）；`pos=1 arg=0 captureType=float[..] readers=[11:tensor:shape]` | 甲 + 乙 |
| `mean_axis1_keepdims_gradient_reads_the_row_length` | `mul_tensor_t`，`types=float, float[..], float[2, 1]`，`ports=[float[..] float[2, 1]]` | `0` → `x: float[..]` | `PROBE pos=0 arg=1 captureType=float[2, 1] extents=stated readers=[11:tensor:multiply]`；`pos=1 arg=0 captureType=float[..] readers=[11:tensor:shape]` | 甲 + 乙 |
| `linear_gradient_both_legs` | `matmul_t`（左式 `matmul(a, b)`，`law=tensor:matmul`） | `0` → `a` | `PROBE pos=0 law=tensor:matmul arg=0 captureType=float[2, 2] portType=float[2, 2] extents=stated readers=[10:-]`——**类型陈述了**（`float[2,2]`），读它的是 `Func`（答案调 `ct_of_a(ct, a, b)`，`a` 是那个调用的**实参**），不是 `tensor:shape` | **甲**（只有甲） |
| `matmul_t.law_read_through_vjp` | 同上 | `0` → `a` | 同上逐字相同 | **甲**（只有甲） |
| `first_step.tensor_vjp_answers_the_step` | `mul_tensor_t`，`types=float, float[..], float[2, 2]`（两次实例化） | `0` → `x: float[..]` | `PROBE pos=0 arg=1 captureType=float[2, 2] extents=stated readers=[11:tensor:multiply]`（要 `w` 的**值**）；`pos=1 arg=0 captureType=float[..] readers=[11:tensor:shape]`（要 `x` 的**形状**） | 甲 + 乙 |

（`position=18446744073709551615` 是 `kWholeAnswer`：那条律的答案只有一个名字，不按位置拆。）

## 3. 表读出来的三件事

1. **报出来的那一次失败，七条全是甲——但缺的是"值"，不是"类型"。** 每一条的 `extents=stated`（`float`、`float[2,2]`、`float[2,1]`），律却拿不到：`erf`/`erfc`/`transpose`/`mean_axis1`/`first_step` 要 `w` 的**值**（`ct * w` 里的另一个因子），`linear`/`matmul_t` 要 `a` 的**值**（`sum_to(matmul(ct, transpose(b)), a)` 把 `a` 整个交了下去）。所以"律怎么拿到自己切线的**类型**"这条修法对这七条**不成立**——类型本来就在手上；缺的是那个操作数的值，或者律把它当值用了。
2. **五条 `mul_tensor_t` 还有第二半，是货真价实的乙。** 它的另一次展开（`pos=1`）答案写 `sum_to_stated(ct * x, shape(w))`，被回答的那一位是 `x: float[..]`——**开秩**，`ports[0]` 也只写着 `float[..]`，`statedShapeOf` 给不出 extents。这一半在任何调用点都无法陈述：`grad` 返回的切线是运行时长度的数组（`types=float, float[..], float[2,2]` 里那个 `float[..]` 就是它）。
3. **两条 matmul 的不同在"律把形状当值用了"。** `ct_of_a` 写的是 `sum_to(matmul(ct, transpose(b)), a)`：`sum_to` 的第二个操作数按文档只被读形状，但它在答案体里是一个 **`Func` 节点**（调用 `sum_to` 的实参），而 `answerTheShapeOf` 只认 `tensor:shape` 这个读者，所以那一处**明明陈述了 extents 也答不出**。这一族的修法在**律**里，不在 C++、也不是新算子：把它写成 `sum_to_stated(matmul(ct, transpose(b)), sa)` 并把 `sa = shape(a)` 交进答案（`sum_stated`、`mean_axis_stated` 已经在用这条机制），`shape(a)` 是 `tensor:shape` 读的，`a` 的端口又陈述了 `float[2,2]` → `answerTheShapeOf` 直接给出 `[2, 2]`，不问任何值。

## 4. 结论（第 5 节是实测更正：律里那一步走不通）

| 类 | 条 | 用例 | 要修哪里 |
| --- | --- | --- | --- |
| **只有甲** | 2 | `linear_gradient_both_legs`、`matmul_t.law_read_through_vjp` | 律里把 `sum_to(..., a)` 换成 `sum_to_stated(..., shape(a))`（三个 `ct_of_a` + 三个 `ct_of_b` 重载随形状分派改写）。**不新增算子** |
| **甲 + 乙** | 5 | `erf_gradient_both_legs`、`erfc_gradient_both_legs`、`transpose_gradient_both_legs`、`mean_axis1_keepdims_gradient_reads_the_row_length`、`first_step.tensor_vjp_answers_the_step` | 两半都要：值（另一个操作数的值，`split`/残差那一侧）与形状（`x: float[..]`，开秩；这一半才是孪生算子唯一可能的理由） |

**所以孪生算子（`mul_stated`/`matmul_stated`）不是这七条的第一步**：两条 matmul 走律里的 `sum_to_stated(..., shape(a))`，而五条 `mul_tensor_t` 单独补形状也关不掉（另一操作数的值仍然缺），单独补值也关不掉（形状仍然是 `float[..]`）——这与本轮更早的一次实测一致：把参数的值塞进闭包，`residuals` 涨了，七条**一条没翻**。

## 5. 律里那一步的实测：走不通（2026-11-04 01:5x）

批准的那一步（`ct_of_a`/`ct_of_b` 改陈述形状）**做过、量过、撤了**，结论是**走不通，而且走不通的原因把这一族指到了同一个地方**：

1. **先把"身份归约"删掉**：四个车道（矩阵×矩阵、行×矩阵、矩阵×矩阵右、矩阵×列）里的 `sum_to(x, 操作数)` 都是身份（矩阵乘不广播），删掉之后律里不再读那个操作数——**编译不过**：`SemanticDiag::ReturnNeedsConversion`，"the value `matmul(ct, transpose(b))` has type `float[..]`, which is less exact than `float[?, ?]`"。也就是说**`sum_to(..., a)` 不是为了归约，是为了把返回类型钉成 `a` 的秩**，那个操作数在表达式里正是取它的类型。
2. **把返回类型放宽成 `float[..]` 再试**：编译通过（这一点值得记：**律可以完全不陈述形状**），但读数没动：
   `TRANSPOSE function=split_of_jvp_of_total_plain_backward skip=cotangent-needs-a-value uri=tensor:matmul position=0`——**还是同一句**。
3. **为什么没动**：位置 0 的余切是 `ct · bᵀ`，它需要的是 **`b` 的值本身**，不是 `b` 的形状。删掉 `sum_to` 只去掉了"读 `a` 的形状"这一半，另一半"读 `b` 的值"还在，而 `b` 的值**没有一个端口带进来**。这就是 `cotangent-needs-a-value` 的原话：the rule's right side reads a value the caller does not carry。
4. **所以这一族（7 条红 + 三条 `matmul_t` 基线红）的根因是同一个，而且不是形状、不是算子、不是律的写法**：**转置成员没有拿到**被转置那一步的**原值**（`dL/da = ct·bᵀ` 里的 `b`；`ct * w` 里的 `w`）。`vector_lhs_law`(`dL/dv = ct·wᵀ`)、`vector_rhs_law`(`dL/dx = ct·uᵀ`)、`matmul_t` 的矩阵车道全是这一个病；`rank_three_is_refused_by_name` 是它的下游（连律都选不出来，钉的是 `node=6`，实测已是 `node=7`）。
5. **落点**：`split` 交给成员的闭合端口与参数（及被转置那一步的操作数）之间的对应——与本轮第 4 项同一处，见 `2026-11-04-who-carries-the-shape-as-an-operand.cn.md` 第 4 节。**律里没有便宜的那一步**；这一步要动 `split`/`transpose` 的成员端口，不是律，也不是算子。

本轮对 `stdlib/rulesets/transpose.cml` 的改动**已全部撤掉**（`git checkout --`，工作区干净），宿主 `out/latest/stdlib` 也推回了提交版；试验只留下了上面这几行读数。

## 6. 三对齐打印：闭合成员／参数／取值键（2026-11-04 02:4x，`knownValue` 拒绝处）

在 `modules/transforms/transpose.cpp#knownValue` 的拒绝处临时打印三样东西——**闭合的成员**（`closureOf`，及每个成员建成了哪个端口）、**参数**（`paramCount`、`portOfParam`、`normPorts` 与 `portParameter`）、**取值键**（`forward` 被问的那个 `ref`，连同它的节点种类与类型）。一次构建（`build 411.6 s`）、跑完 `git checkout --` 撤掉；带打印那次 plan 仍是 `total: 170, pass: 151, fail: 5, known failures: 14`，与不带逐条相同。

**`linear_gradient_both_legs`（`matmul`，`float[2,2]×float[2,2]`），原话：**

```
ALIGN key=3 kind=1 type=float[2, 2] closureMember=0 paramPort=2 withPort=0
  closureOf(3): 0[k=1,t=float[2, 2],p=0] 1[k=1,t=float[2, 2],p=1] 2[k=1,t=float[2, 2],p=2]
  portOfParam(2): 0->4294967295 1->4294967295
  paramCount=2 nodeCount=10
  normPorts(2): 0:ref3[at=0] 1:ref4[at=1]
  closureNodes(3): 0[ported] 1[ported] 2[ported]
```

**`erf_gradient_both_legs`（`mul`，`float[..]×float`），原话：**

```
ALIGN key=9 kind=11 type=float[..] closureMember=0 paramPort=1 withPort=0
  closureOf(2): 0[k=1,t=float[4],p=0] 1[k=1,t=float[4],p=1]
  portOfParam(1): 0->4294967295
  paramCount=1 nodeCount=12
  normPorts(1): 0:ref2[at=0]
  closureNodes(2): 0[ported] 1[ported]
```

**三样对不上，而且是这一处**：

1. **取值键是"参数端口"。** `linear` 的键 `ref=3`，`normPorts(2): 0:ref3[at=0]` —— **它就是参数 0 的端口**，不是某个"值节点"。
2. **参数没有端口。** `portOfParam(2): 0->invalid 1->invalid` —— 两个参数**一个端口都没建**。这就是 `forward` 在 `portOfParam[at]` 处返回 `invalid` 的直接原因（`transpose.cpp#forward` 的 Port 分支）。
3. **闭合里有值，只是没和这个键连上。** `closureNodes(3): 0[ported] 1[ported] 2[ported]` —— 三个成员**都**建了端口（`p=0,1,2`，类型都是 `float[2,2]`）。**值在闭合里，键指向参数端口，两者之间没有一条边**。`erf` 同型：闭合两个成员都 ported，键 `ref=9` 是算子节点（`kind=11`），它的输入链尽头仍是"没有端口的参数端口"。

**所以判据的答案是"对不齐"，且对不齐的位置就是 `portOfParam` 空着**：`transpose.cpp#forward` 的注释写着"participating 的参数没有端口，值由调用的槽位（`valueOf`）送来"，而 `valueOf` 走的正是 `knownValue → forward`，于是那句话自我指涉：**要值的律问 `forward`，`forward` 说要问调用槽位，调用槽位又回到 `forward`。** 这一圈闭合不了的地方，就是 `portOfParam` 与 `closurePortOf` 没有接上的那一处。

## 7. 下一步

1. **接上那一处**：让"被转置那一步的操作数"要么在 `portOfParam` 里有端口，要么在 `closureOf` 里以同一 `ref` 出现（今天的打印说明它两边都没有）。这一处改完，10 条应该一起动（同一根因），届时重测并贴并排读数。
2. `matmul_t.rank_three_is_refused_by_name` **已单独修好**：它今天红在两处，第二处是钉子里的节点号——见 §8。
3. 五条 `mul_tensor_t` 的形状那一半（乙）：等第 1 条落地后重测；若仍红，再拿开秩 `float[..]`（`grad` 返回的切线是运行时长度的数组，**任何调用点都无法陈述**）去找作者裁。

## 8. 三行表：`showcase_s2.plan.toml` 的节点号（本次唯一改的期望值）

| 文件 | 旧值 | 新值 | 为什么 |
| --- | --- | --- | --- |
| `test/plans/feat/showcase_s2.plan.toml`（`matmul_t.rank_three_is_refused_by_name` 的 `expect_stderr_contains`，及同条 `desc` 与上方两行注释） | `node=6 position=0 ranks=(3, 2)` | `node=7 position=0 ranks=(3, 2)` | **节点号是下游数的**：它是被转置图里那个 `tensor:matmul` 节点的序号，由这一趟走法数出来，不是我们挑的。今天的树读 `node=7`（发布侧那条写的是 `node=6`，来自他们自己的树）；两句原文一并记在这里——`node=6` 是我这条车道动手之前别人读到的，`node=7` 是今天这棵树的实测。**没有把它改成"任意号"，`position=0 ranks=(3, 2)` 与两种流水（stdout 的整句、stderr 的 `reason=` 行）都没动。** |

读数：改前 `showcase_s2` 五条红（`first_step` / `matmul_t.law_read_through_vjp` / `vector_lhs` / `vector_rhs` / `rank_three`）；改后 `[SUMMARY] total: 7, pass: 3, fail: 4`，**`matmul_t.rank_three_is_refused_by_name` 转绿**，其余四条一字未动。也就是说这条基线红**原本只是钉子错**，不是一个行为缺陷。