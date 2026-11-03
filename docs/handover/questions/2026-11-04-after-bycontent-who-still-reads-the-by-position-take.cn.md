# 按位置取值删不掉：bycontent 改完之后 9 个读者还在，删了新增 14 条红

## 问什么

作者 16:55 的裁定说：「今天这条路径改完，第 1 项的『按位置找参数值』也应当一起消失，因为两者是同一个
思路：**后一半里不存在需要靠约定才能找到的值**。」`lane-bycontent`（`f5eaeb0fa`）把那条路径改完了，
本页是那句话的验收读数：**这段还该不该留？** 读数说：删了新增 14 条红，所以**默认留着**；要裁的是
"留着并写清约定"还是"把 `split` 已有的身份交给 `transpose`"。

## 它在哪，判据是什么

`modules/transforms/transpose.cpp#buildTransposeMember`，`forward` 闭包的 `Port` 分支里、
`if (!bundlesResiduals)` 那一格：guard 是 `forwardOf[ref] == kInvalidNodeRef && at < paramCount`，
取值是 **`closurePorts[slot]`，而 `slot = at`**，条件只有一句
`camel::core::type::typeEquals(slotMember->dataType(), askType)`。

所以**判据是 `at`（闭包槽号 = 参数下标），类型只是护栏**：两个 `float[]` 的残差类型相同，
"第 `at` 格是参数 `at` 的值"这件事没有任何一处被证明。这段还活着的唯一一句自述在
`modules/transforms/split.h#Residual`（`parameter` 记着"这个残差是第几个参数"），
但 `transpose` 拿不到它——它只有 `closurePorts` 这个数组。

## 三种口径的命中数（命令 + 数，别串口径）

树 `D:\Projects\Camel-wt\posgone` @ `f5eaeb0fa`（`lane-posgone`，从 `lane-bycontent` tip 起），
主机 `~/camel-work/posgone`；`2026-11-04 01:37–01:47 +08:00`；主机 **load 13.9–32.3 / 32 核**
（读数不与空载可比）。插桩用 `D:\Temp\posprobe.patch`（`POSPROBE-HIT`，marker 文件
`/tmp/camel-posprobe.on`），交付前已回退，不在提交里。

| 口径 | 怎么量 | 命中 |
| --- | --- | --- |
| ① `from=call-capture` | 单 case、单进程，逐个跑全部 96 个 `test/cases/compile/transforms/*.cml`（`std::nvm`） | **9 行，9 个 case 各 1 行** |
| ② 用例命中（父任务那 18 / 10） | 同上，按 case 数 | **18 个 case 命中，47 行 HIT**（`probe=0` 真实构建 28 行、`probe=1` 探针草稿 19 行）；表里那 10 条 **10/10 全在** |
| ③ 整份计划跑 | `-Plan test/plans/feat/compile/transforms.plan.toml -Tier fast -Jobs 4` | **13 行**（`probe=0` 11 行、`probe=1` 2 行） |

三种口径**不是同一批数**：①只数真实构建里的调用点捕获；②多出 `from=-`（律展开自己问的 38 行，
`sum_t`/`mul_t` 要操作数原值建形状）；③只覆盖计划列出的 87 个 `case=` 条目，
`matmul_transpose_law` / `transpose_array_product` / `transpose_product_fixed_shape` 这几条
**不在 transforms 计划里**（它们在 `feat/compile` 的别的计划），所以③比②少 34 行。

**`from=call-capture` 的 9 条：一个都没少。** 它们是
`grad_array_int_parameter_first`、`grad_array_no_int`、`grad_array_one_element`、`grad_array_product`、
`grad_array_sum`、`grad_array_two_elements_first`、`grad_array_with_int`、`grad_of_a_division`、
`grad_of_an_array_parameter`，形状统一：`fn=split_of_jvp_of_*_backward`、`i=0`、`at`=某个参数下标、
`slotName=closure<at>`、`askType == slotType`。

## 为什么 bycontent 没让这 9 条消失

那 9 条的调用者全是**规则内核**——`rulesets.jvp::index_into<float>`、`autodiff::div_tangent<float>`，
它们带 `tangentArgs`，签名是 `(值…) => (切线…)` 而不是 `(Params) => (value, tangent) 二元组`。
`modules/transforms/split.cpp#answersAPair` **先问签名侧**（`splitHalvesWith`），
内核在那一问就返回 false，所以 `answersAPair` 与 `liftedFrom` 的换判据**根本走不到这 9 条**：
bycontent 换的是"哪些被调函数答二元组"，不是"哪一次求值需要操作数原值"。
两者不是同一个判据，作者那句"应当一起消失"在这个方向上没有实现路径。

## 删了会怎样（真删：21 行删掉，重新编译）

同一个树、同一批目标，删前/删后：

| 目标 | 删前 | 删后 |
| --- | --- | --- |
| `feat/compile/transforms` | **91 = 88/2/1** | **91 = 77/13/1**（+11 红） |
| `feat/transforms/split` | **19 = 19/0/0** | **19 = 19/0/0**（不变） |
| `feat/compile` | **2222 = 2217/2/3** | **2222 = 2203/16/3**（+14 红） |

> 口径：本车道的 `transforms` 是 **91**，不是任务书里 `origin/develop` 的 **102**——
> 这条车道没带那 11 条新律（`feat/compile` 同理 2222 vs 2233）。跨树比数只有"增量"可比。

**14 条新增红，逐条：它读的是什么。**

| 红 | 谁问的（探针 `ask`） | 读的是 | 删后报什么 |
| --- | --- | --- | --- |
| `grad_array_no_int.fvm` | `from=call-capture`，`index_into` 的转置要 param0 的值 | 参数 0（`xs`）的**原值**，`zeros_like` 要它的形状 | `vjp.cml: transpose has no transpose for this function: … the parameter at position 1 reads a value that…` |
| `grad_array_one_element.nvm` | 同上，`at=0` `closure0` | 参数 0 原值 | 同上 |
| `grad_array_two_elements_first.nvm` | 同上 | 参数 0 原值 | 同上 |
| `grad_array_product.nvm` | 同上，`closure=5` | 参数 0 原值 | 同上 |
| `grad_array_sum.nvm` | 同上，`closure=5` | 参数 0 原值 | 同上 |
| `grad_of_an_array_parameter.nvm` | 同上，`closure=5` | 参数 0 原值 | 同上 |
| `grad_array_with_int.fvm` | 同上，`mixed_backward` `at=0` | 参数 0 原值 | 同上 |
| `grad_array_int_parameter_first.nvm` | 同上，`read_at_backward` `at=1` | 参数 1（`xs`）原值 | 同上（position 1） |
| `grad_of_a_division.nvm` | 同上，`___g_325___backward` `at=0` `askType=float` | 除数/被除数那一格的**原值** | 同上（position 4） |
| `relu_gradient_through_the_library.nvm` | `from=-`，`ask=rule:sum_t\|node:9\|pos:0`，`target=oper:tensor:multiply` | `total` 的输入**原值**（`spread_like` 要形状） | `norm-arity mismatch: caller 'main' node 7 has 1 args, target 'transpose_of_split_of_jvp_of_total_backward' has 2 norm ports` |
| `transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` | `from=-`，`ask=rule:mul_t\|node:4\|pos:0`，`target=port0(p=0,part)` | 问的是参与微分的 `dx` 那一格，取到的是**闭包 0（`x`）**——值也取错 | `norm-arity mismatch: caller 'main' node 21 … target 'transpose_of___g_317__' has 2 norm ports` |
| `transpose_array_product.answers_both_operands.nvm` | `from=-`，`ask=rule:sum_t\|node:8\|pos:0`，`target=oper:tensor:add`，`at=1` `closure1` | 参数 1 的**原值**（形状） | `a parameter holds no cotangent: nothing uses its value` |
| `jvp_vjp_array_product.one_point_two_ends.nvm` | 同上（`energy_backward`） | 参数 1 原值 | 同上 |
| `transpose_product_fixed_shape.matches_an_open_rank.nvm` | 同上，`askType=float[3]` | 参数 1 原值 | 同上 |

**两种坏法**：需要值但取不到时，成员把这一格写成**捕获端口**（`transposeSignature` 从
`wantedValues` 加端口），调用点仍按原签名传参 → 运行期 `norm-arity mismatch`；或者走
`skip=call-value-missing` 直接拒绝（`TRANSPOSE-NEEDS function=… param=N` 是前一站）。
"取不到"不能用"少给一个值"表达，只能靠拒绝或靠补一个端口——这与
`transpose_loop_escape` 那两条长红是同一机制。

## 五条判据读数（干净树、无探针、`f5eaeb0fa` 原文）

1. `grad_array_no_int`：`6.0` / `([2.0, 3.0])`，exit 0 —— **逐字**。
2. `transpose_hand_closed_lambda`：`cut ct1= (6.0)` / `cut ct3= (18.0)` / `hand ct1= (6.0)` / `hand ct3= (18.0)`，exit 0 —— 四行齐。
3. `transpose_loop_escape.two_values_of_two_kinds`（`transpose_loop_escape_reads_outer.cml`）仍是拒绝：
   `error[RuntimeDiag::PassInternalError]: Pass 'std::nvm' failed with an internal error: Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports.`
4. `transpose_loop_escape.second_state_of_the_same_loop` 仍是拒绝：
   `error[RuntimeDiag::RuntimeError]: transforms: split does not build this function (this loop is on the chain the exit reads and neither half has a value for it: the cut copies a loop where the exit reads its two components through one projection each, and here the half would have to copy a body region).`
5. 三个目标在交付树上重测：`91 = 88/2/1`（load 22.5）、`19 = 19/0/0`（27.8）、`2222 = 2217/2/3`（28.6–32.3）。

## 默认（本轮按它开工：不改代码）

**这段留着。** 依据是判据第 3 条：删了新增 14 条红，其中 11 条正是作者钉住的判据用例。

三条路，供作者选：

1. **留着并把约定写清**（默认）：在 `transpose.cpp` 与 `split.cpp` 的注解里写死"残差槽号 = 参数下标"，
   并写明"读取处只查类型，槽号靠 `residualsOf` 把参数端口排在残差表最前"。代价 0，
   代价是那条约定仍然是私下的、那个可判条件仍然不被检查。
2. **把 `split` 已有的身份交给 `transpose`**：`Residual::parameter` 已经记着"这个残差是第几个参数"，
   改由它找槽，`at` 就只是参数而不是槽号。代价：手写闭包没有这张表，
   `34788b554` 曾这么做、因手写闭包红了而回退；要么给手写闭包一条通用写法，要么承认两者不同源。
3. **让律不要值、只要形状**：把 `zeros_like(xs)` / `spread_like(xs, ·)` 换成从余切就能建的写法。
   代价：动 `stdlib/rulesets/transpose.cml`，而 `sigmoid`/`tanh` 在别的车道上，跨车道协调才动。

## 没查到的

- **`origin/develop`（97e25ccee）上今天的 `from=call-capture` 条数没在本轮重量**：②/①的 develop 侧
  参照是 `2026-11-04-what-the-positional-read-is-a-copy-of` 那页量的 9 条（`c69a110fa`），
  本页在 bycontent 树上量到 9 条，**两边同一批用例**，但"同一批"是比对用例名得出的，不是同一次运行。
- ②里 47 行有 19 行是探针草稿（`probe=1`）的重复，**这 19 行不是 18 个 case 之外的新读者**；
  拿 47 当"读者数"会高估。