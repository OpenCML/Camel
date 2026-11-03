# 2026-11-04 按位置取值今天还有谁在读：9 条规则内核，删了 14 条红

> 作者 2026-11-03 17:35（`ae4d2d9b5`）已经裁定第 1、2 项是**同一个修法**、修在**规则层**，并写明改完之后删哪两处
> （"`transpose.cpp` 里按位置取值的那一段、`wantedValues`/`TRANSPOSE-NEEDS` 删掉"）。所以这里不是问句：
> **本页只记录规则层改完之前还有谁在读**，作为那条车道的验收清单——改完之后，下面 14 条红必须一起翻绿。

## 结论

`lane-bycontent`（`f5eaeb0fa`）把"哪些被调函数答二元组"改成按内容判断之后，按位置取值**仍然有 9 个读者**
（9 条用例各 1 次），删掉它新增 **11 条** `feat/compile/transforms` 红、**14 条** `feat/compile` 红，
正好是裁定里说"要保持绿"的那批用例。

## 那段代码在哪

`modules/transforms/transpose.cpp#buildTransposeMember`，`forward` 闭包的 `Port` 分支、`if (!bundlesResiduals)`
那一格：guard 是 `forwardOf[ref] == kInvalidNodeRef && at < paramCount`，取值是 **`closurePorts[slot]`，而 `slot = at`**，
唯一的护栏是 `camel::core::type::typeEquals(slotMember->dataType(), askType)`。

**判据是 `at`（闭包槽号 = 参数下标）；类型只是护栏，不是证据。** 两个 `float[]` 的残差类型相同，所以
"第 `at` 格是参数 `at` 的原值"没有一处被检查。这个身份其实已经存在：`modules/transforms/split.h#Residual`
的 `parameter` 记着"这个残差是第几个参数"，`residualsOf` 也把参数端口排在残差表最前——**只是没交给
`transpose`**，它能拿到的只有 `closurePorts` 这个数组（裁定否掉了"把身份交过去"这条路：不要身份、字段或
位置约定，任何一种都不要）。

## 三种口径的命中数（命令 + 数；三种不是同一批，别相加）

树 `D:\Projects\Camel-wt\posgone` @ `f5eaeb0fa`（`lane-posgone`，从 `lane-bycontent` tip 起），主机
`~/camel-work/posgone`；`2026-11-04 01:37–01:47 +08:00`；主机 **load 13.9–32.3 / 32 核**（读数不与空载可比）。
插桩 `D:\Temp\posprobe.patch`（`POSPROBE-HIT`，marker 文件 `/tmp/camel-posprobe.on`），已回退，不在提交里。

| 口径 | 怎么量 | 命中 |
| --- | --- | --- |
| ① `from=call-capture` | 单 case、单进程，逐个跑全部 96 个 `test/cases/compile/transforms/*.cml`（`std::nvm`） | **9 行，9 个 case 各 1 行** |
| ② 用例命中 | 同上，按 case 数 | **18 个 case 命中、47 行 HIT**（真实构建 28 行、探针草稿 19 行）；裁定点名那 10 条 **10/10 全在** |
| ③ 整份计划 | `-Plan test/plans/feat/compile/transforms.plan.toml -Tier fast -Jobs 4` | **13 行**（真实构建 11 行、探针草稿 2 行） |

三者差在哪：①只数真实构建里的**调用点捕获**；②多出 `from=-` 那 38 行（律展开自己问的，`sum_t`/`mul_t`
要操作数原值建形状）；③只覆盖计划里 87 个 `case=` 条目，`matmul_transpose_law` / `transpose_array_product` /
`transpose_product_fixed_shape` **不在 transforms 计划里**（在 `feat/compile` 别的计划），所以③比②少 34 行。

①那 9 条用例：`grad_array_int_parameter_first`、`grad_array_no_int`、`grad_array_one_element`、
`grad_array_product`、`grad_array_sum`、`grad_array_two_elements_first`、`grad_array_with_int`、
`grad_of_a_division`、`grad_of_an_array_parameter`。形状统一：`fn=split_of_jvp_of_*_backward`、`i=0`、
`at`=某个参数下标、`slotName=closure<at>`、`askType == slotType`。

## 为什么按内容改完仍有这 9 条（本页最要紧的一句）

> 那 9 条的调用者全是**规则内核**（`rulesets.jvp::index_into<float>`、`autodiff::div_tangent<float>`，
> 带 `tangentArgs`），`split.cpp#answersAPair` **先问签名侧**（`splitHalvesWith`），内核在那一问就返回 false。

**这就是"按内容"改完仍有 9 条的原因**：bycontent 换的是"**哪些被调函数**答二元组"，这 9 条问的是
"**哪一次求值需要操作数的原值**"——两个判据，前者永远走不到后者。

这也正好对上 17:35 裁定"改规则"那段的描述：`jvp_idx_arr` 展开后，切向那一半里有一个 `index_into(dxs, i)`，
转置它时 `idx_arr_t` 写的是 `zeros_like(xs)`，要读 `xs`，而这里的 `xs` 是**线性操作数**，于是只好按位置
从闭包里摸出原值来顶。裁定的修法（线性版本多收一个已知长度实参，转置规则只读那个已知实参）就是对着
这 9 条去的。

## 删了会怎样（真删 21 行 + 重编；与探针 drop 实验逐条相同）

同一棵树、同一批目标，删前/删后：

| 目标 | 删前 | 删后 |
| --- | --- | --- |
| `feat/compile/transforms` | **91 = 88/2/1**（load 14.2） | **91 = 77/13/1**（+11 红，load 15.8） |
| `feat/transforms/split` | **19 = 19/0/0** | **19 = 19/0/0**（不变，load 15.7） |
| `feat/compile` | **2222 = 2217/2/3**（load 14.4–17.8） | **2222 = 2203/16/3**（+14 红，load 15.5–16.6） |

口径：本车道 `transforms` 是 **91**、`compile` 是 **2222**（任务书里 `origin/develop` 是 102 / 2233——这条
车道没带那 11 条新律）。跨树只有**增量**可比。

**两种坏法**：需要值却取不到时，成员把这一格写成**捕获端口**（`transposeSignature` 从 `wantedValues` 加端口），
调用点仍按原签名传参 → 运行期 `norm-arity mismatch`；或者走 `skip=call-value-missing` 直接拒绝
（`TRANSPOSE-NEEDS function=… param=N` 是前一站）。"取不到"不能用"少给一个值"表达，只能靠拒绝或补端口。

## 14 条新增红 ← 读的是什么

| 红 | 谁问的（探针 `ask`） | 读的是 | 删后报什么 |
| --- | --- | --- | --- |
| `grad_array_no_int.fvm` | `from=call-capture`，`index_into` 的转置要 param0 的值 | 参数 0（`xs`）的**原值**，`zeros_like` 要形状 | `vjp.cml: … the parameter at position 1 reads a value that…` |
| `grad_array_one_element.nvm` | 同上，`at=0` `closure0` | 参数 0 原值 | 同上 |
| `grad_array_two_elements_first.nvm` | 同上 | 参数 0 原值 | 同上 |
| `grad_array_product.nvm` | 同上，`closure=5` | 参数 0 原值 | 同上 |
| `grad_array_sum.nvm` | 同上，`closure=5` | 参数 0 原值 | 同上 |
| `grad_of_an_array_parameter.nvm` | 同上，`closure=5` | 参数 0 原值 | 同上 |
| `grad_array_with_int.fvm` | 同上，`mixed_backward` `at=0` | 参数 0 原值 | 同上 |
| `grad_array_int_parameter_first.nvm` | 同上，`read_at_backward` `at=1` | 参数 1（`xs`）原值 | 同上（position 1） |
| `grad_of_a_division.nvm` | 同上，`___g_325___backward` `at=0` `askType=float` | 那一格的**原值** | 同上（position 4） |
| `relu_gradient_through_the_library.nvm` | `from=-`，`ask=rule:sum_t\|node:9\|pos:0`，`target=oper:tensor:multiply` | `total` 的输入**原值**（`spread_like` 要形状） | `norm-arity mismatch … target 'transpose_of_split_of_jvp_of_total_backward' has 2 norm ports` |
| `transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` | `from=-`，`ask=rule:mul_t\|node:4\|pos:0`，`target=port0(p=0,part)` | 问的是参与微分的 `dx`，取到的是**闭包 0（`x`）**——值也取错 | `norm-arity mismatch … target 'transpose_of___g_317__' has 2 norm ports` |
| `transpose_array_product.answers_both_operands.nvm` | `from=-`，`ask=rule:sum_t\|node:8\|pos:0`，`target=oper:tensor:add`，`at=1 closure1` | 参数 1 的**原值**（形状） | `a parameter holds no cotangent: nothing uses its value` |
| `jvp_vjp_array_product.one_point_two_ends.nvm` | 同上（`energy_backward`） | 参数 1 原值 | 同上 |
| `transpose_product_fixed_shape.matches_an_open_rank.nvm` | 同上，`askType=float[3]` | 参数 1 原值 | 同上 |

**分类，裁定覆盖到全部 14 条，没有第三种读法：**

- **9 条** = `index_into` / `div_tangent` —— 裁定点名的形状（`jvp_idx_arr` 的线性版本多收已知长度）。
- **5 条** = `sum_t` / `mul_t` 读操作数自己的值或形状 —— 裁定"同类规则一起查"那一桶（`div_t`、`sum_t`、
  `take_t`、`mean_axis_t`）。

## 为什么 bycontent 碰到它们不是"漏了"

bycontent 是"换判据"，不是"少改一处"：`answersAPair` 与 `liftedFrom` 在**每一张有图可问的图**上答得一样
（那一笔提交的原话），差别只在"手写同形函数"上。这 9 条的问题不在"谁是二元组"，而在"这一次求值有没有
操作数的原值"——换判据不会让一个内核变成二元组，也不会让一次求值多出一个已知实参。要它消失只能改规则。

## 五条判据（干净树、无探针、交付提交 `110324021`）

1. `grad_array_no_int`：`6.0` / `([2.0, 3.0])`，exit 0 —— 逐字。
2. `transpose_hand_closed_lambda`：`cut ct1= (6.0)` / `cut ct3= (18.0)` / `hand ct1= (6.0)` / `hand ct3= (18.0)`，
   exit 0 —— 四行齐。
3. `transpose_loop_escape.two_values_of_two_kinds` 仍拒绝：`error[RuntimeDiag::PassInternalError]: Pass 'std::nvm'
   failed with an internal error: Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args,
   target graph 'transpose_of_read_outer' has 2 norm ports.`
4. `transpose_loop_escape.second_state_of_the_same_loop` 仍拒绝：`error[RuntimeDiag::RuntimeError]: transforms:
   split does not build this function (this loop is on the chain the exit reads and neither half has a value for it:
   the cut copies a loop where the exit reads its two components through one projection each, and here the half
   would have to copy a body region).`
5. 交付树重测：`91 = 88/2/1`（load 22.5）、`19 = 19/0/0`（27.8）、`2222 = 2217/2/3`（28.6–32.3）；
   提交后最后一次 `-NoBuild`：`tip 11032402141c37a717f369d920783020de3d388b`、`91 = 88/2/1`、load 19.35。

## 口径（今晚已经被"两个口径相加/串用"坑过一次）

`origin/develop`（`97e25ccee`）上今天的 `from=call-capture` 条数**本轮没有重量**：develop 侧的 9 条是
`2026-11-04-what-the-positional-read-is-a-copy-of` 那页在 `c69a110fa` 上量的，我在 bycontent 树上量到 9 条，
**两边是同一批用例，但"同一批"是比对用例名得出的，不是同一次运行**。

②里 47 行有 19 行是探针草稿的重复，**不是 18 个 case 之外的新读者**；拿 47 当"读者数"会高估。

## 没查到的

- **裁定要求两处一起删**（按位置取值那一段 + `wantedValues`/`TRANSPOSE-NEEDS`），本页只量了前者。
  另删 `wantedValues` 不会让这 9 条变绿（需求是真的），只会让它们不再被记成捕获端口、在同一处换成另一种
  拒绝；**没有重量**。
- `camel-remote.ps1` 的工作机环境变量到不了被测进程，探针只能靠 marker 文件；主机上探针文件已删，
  过程日志在 `D:\Temp\posgone-*.log`，不入 git。