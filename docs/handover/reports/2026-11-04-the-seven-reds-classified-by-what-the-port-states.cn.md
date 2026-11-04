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

## 4. 结论

| 类 | 条 | 用例 | 要修哪里 |
| --- | --- | --- | --- |
| **只有甲** | 2 | `linear_gradient_both_legs`、`matmul_t.law_read_through_vjp` | 律里把 `sum_to(..., a)` 换成 `sum_to_stated(..., shape(a))`（三个 `ct_of_a` + 三个 `ct_of_b` 重载随形状分派改写）。**不新增算子** |
| **甲 + 乙** | 5 | `erf_gradient_both_legs`、`erfc_gradient_both_legs`、`transpose_gradient_both_legs`、`mean_axis1_keepdims_gradient_reads_the_row_length`、`first_step.tensor_vjp_answers_the_step` | 两半都要：值（另一个操作数的值，`split`/残差那一侧）与形状（`x: float[..]`，开秩；这一半才是孪生算子唯一可能的理由） |

**所以孪生算子（`mul_stated`/`matmul_stated`）不是这七条的第一步**：两条 matmul 走律里的 `sum_to_stated(..., shape(a))`，而五条 `mul_tensor_t` 单独补形状也关不掉（另一操作数的值仍然缺），单独补值也关不掉（形状仍然是 `float[..]`）——这与本轮更早的一次实测一致：把参数的值塞进闭包，`residuals` 涨了，七条**一条没翻**。

## 5. 下一步（未做，等批）

1. **便宜的那一步先跑**：改 `stdlib/rulesets/transpose.cml` 的 3 个 `ct_of_a` + 3 个 `ct_of_b` 重载，把 `sum_to` 换成 `sum_to_stated` 并把 `shape(a)`/`shape(b)` 交进来，`matmul_t` 的答案改成把这两个形状当实参传。预计关掉上表"只有甲"那两条，风险在三个形状分派重载与向量车道的两条既有 `known_fail`（`matmul_t.vector_lhs_law`/`vector_rhs_law`，不许动）。**本轮没跑**：这不是一行，六个重载都要动，且改的是律而不是 C++，我先要一句"就按这条走"。
2. 五条 `mul_tensor_t` 的形状那一半（乙）**要等**：它要么等"切线自己的形状从哪来"（`vjp` 的零余切那一半，见 `2026-11-04-who-carries-the-shape-as-an-operand.cn.md` 第 4 节），要么等孪生算子。值那一半（甲）是 `split` 的残差问题，与上面那条独立的。