# 乘积律读另一个操作数：广播形状上答错，已回退

**结论**：把 `mul_tensor_t` 改成"每一侧读它没有在给余切的那个操作数"，在**广播**乘积上会答错形状和错数
（实测：`d/dx` 答 `[6.0]`、`d/dw` 答 `[1.0, 2.0, 3.0]`，正确是 `[2.0, 2.0, 2.0]` 与 `[6.0]`）。
一个错数比一个按名拒绝严重，所以那一处**已回退**，`stdlib/rulesets/transpose.cml#mul_tensor_t` 回到
`sum_to(ct * w, x), sum_to(ct * x, w)`，6 条 `known_fail` 回到原位。

**判决依据**：`sum_to(t, like)` 的第二实参是**归约目标形状**，不是随便一个"和它一样大的东西"。所以它
只能是被给余切的那个操作数本身。乘积两侧形状相等时两者可以互换，一旦广播就断——而"形状相等"在开秩类型
`float[..]` 上是**看不见**的（两侧类型一模一样），所以没有任何写法能既答对等形状、又对广播安全。

## 0. 被回退的那一行

```
mul_tensor(x, w) => (ct: T[..]) => (sum_to(ct * w, w), sum_to(ct * x, x))
```

它为什么看着像对的：`x` 那一侧的 `sum_to` 读 `x`（切向，没有值）所以拒绝；改成读 `w` 就把"读一个没有
值的东西"换成了"读一个残差"。在**逐元素**乘积（两侧同形状）上这确实对，因为两侧形状是同一个。

## 1. 三条量测（先手算）

`x = [1.0, 2.0, 3.0]`、`w = [2.0]`，乘积形状 `(3,)`、`sum = 12.0`。手算：
`d/dx = broadcast(w) = [2.0, 2.0, 2.0]`（numel 3）、`d/dw = sum(ct * x) = [6.0]`（numel 1）。

**1a. 库层腿（新写法，`vjp` 走 `stdlib/rulesets`）**，开秩与写死形状各一遍：

```
open  value=12.0 d/dx numel=1 d/dx=[6.0] d/dw numel=3 d/dw=[1.0, 2.0, 3.0]
stated value=12.0 d/dx numel=1 d/dx=[6.0] d/dw numel=3 d/dw=[1.0, 2.0, 3.0]
```

`float[..]`／`float[..]` 与 `float[3]`／`float[1]` 报的是同一对错数：两个位置都拿"另一个操作数"当了目标
形状，于是 `d/dx` 被归约成 `w` 的形状（把 `ct * w` 求和成 6.0），`d/dw` 反倒是 `x` 的全长。

**1b. 引擎腿（`grad`，`modules/autodiff` 自己的 pullback）**，同样两组：

```
engine d/dx numel=3 d/dx=[2.0, 2.0, 2.0]
engine d/dw numel=1 d/dw=[6.0]
```

与手算逐位相同。引擎之所以对，是因为 `modules/tensor/ops/vjp.cpp#accumulateOperand` 拿**操作数自己**
当 `sum_to` 的目标（它手上有前值，库层的转置没有）。

**1c. 今天有没有用例覆盖广播的 `multiply` 转置**：没有。`test/plans/feat/compile/` 下没有任何条目把
`float[1]`／秩 0 的 `w` 与 `float[3]` 的 `x` 一起送进乘积的转置；`generics_rank_zero.plan.toml` 测的是
`sum_to` 本身的两个方向，不是乘积律。所以这个错数**不会**被现成的钟抓到，它是靠这次专门探针抓到的。

## 2. 回退后的状态

- 律回到 `sum_to(ct * w, x), sum_to(ct * x, w)`，注释保持原样（它已经把"第二半是归约到操作数形状"写清）。
- 6 条 `known_fail` 回到原位，pin 仍是 `skip=cotangent-needs-a-value uri=tensor:multiply`；`known_fail_note`
  里补上了这次量测和结论（错数比拒绝严重，所以这一律不动）。
- 读数（宿主 88zjwei，2026-11-04，`-Tier fast -Jobs 4`，1 m load 15.5–23.3）：
  - 回退后、加新用例前：`feat/compile/transforms` `111 = 105 / 2 / 4`；`feat/compile`
    `2249 = 2239 / 2 / 8`（`719fc2baf` 的基线是 `2238/2/9`，差的 1 条 known 是并入的 `bbfc003ec`
    回掉的 `transpose_hand_closed_lambda`）。
  - 加六条新用例之后（最终）：`feat/compile/transforms` **`122 = 115 / 2 / 5`**；`feat/compile`
    **`2260 = 2249 / 2 / 9`**。总数 2249→2260 正是新增的 11 个条目（5 条绿 × nvm+fvm + 1 条开秩
    known_fail × nvm）；`fail` 仍是 **2**（一直是基线那两条 `transpose_loop_escape.*`），
    `known failures` **仍是 9**——六条新用例一条都没进已知失败列表。
  - `test/plans/feat/transforms`：`29 = 21 / 0 / 8`，退出码 0，与加用例前逐字相同（红数没增）。
- `transpose(pick)`（`transforms.plan.toml#transpose_refuses_a_parameter_value_it_cannot_be_handed`）
  仍答 `([0.0, 1.0, 0.0])`；`mean_axis1_keepdims_gradient_reads_the_row_length` 仍答
  `mean_axis1 keepdims value=120.0 cotangent=[[3.3333333333333335, 3.3333333333333335, 3.3333333333333335], [6.666666666666667, 6.666666666666667, 6.666666666666667]]`。

## 2.1 补上的六条用例（乙只做一半：加固定形状的，删不动开秩的）

派工的裁决是"固定形状是今天能对，开秩是今天拒，两者都要有；把开秩那条删掉就是把未来的钟拆了"。
所以开秩那 6 条 `known_fail` 一个字没动，另加六条。

| 用例 | 类型 | 今天 | 读数（同一程序里的两腿） |
| --- | --- | --- | --- |
| `exp_gradient_of_a_stated_shape` | `float[2]` | PASS（nvm+fvm） | `exp stated engine d=[2.718281828459045, 7.38905609893065]`、`exp stated library value=10.107337927389695 d=[2.718281828459045, 7.38905609893065]` |
| `relu_gradient_of_a_stated_shape` | `float[4]` | PASS（nvm+fvm） | `relu stated engine d=[0.0, 0.0, 1.0, 1.0]`、`relu stated library value=2.75 d=[0.0, 0.0, 1.0, 1.0]` |
| `sigmoid_gradient_of_a_stated_shape` | `float[2]` | PASS（nvm+fvm） | `sigmoid stated engine d=[0.25, 0.10499358540350662]`、`sigmoid stated library value=1.3807970779778822 d=[0.25, 0.10499358540350662]` |
| `tanh_gradient_of_a_stated_shape` | `float[2]` | PASS（nvm+fvm） | `tanh stated engine d=[0.18070663892364858, 0.07065082485316443]`、`tanh stated library value=-0.05887932643095051 d=[0.18070663892364858, 0.07065082485316443]` |
| `broadcast_product_gradient_of_a_stated_shape` | `float[3]`×`float[1]` | PASS（nvm+fvm） | `engine d/dx numel=3 d/dx=[2.0, 2.0, 2.0]`、`engine d/dw numel=1 d/dw=[6.0]`、`library value=12.0 d/dx numel=3 d/dx=[2.0, 2.0, 2.0] d/dw numel=1 d/dw=[6.0]` |
| `broadcast_product_gradient_of_an_open_rank` | `float[..]`×`float[..]` | XFAIL（pin 拒绝） | 期望是 `value=12.0 / [2,2,2] / [6.0]`；今天停在 `skip=cotangent-needs-a-value uri:tensor:multiply position=0` |

每条用例的注释里都先写手算，再写读数。四个激活的输入都是自己选的、注释里写明为什么选它——不是照抄历史
读数：`relu` 的 `[-1.5, 0.0, 2.5, 0.25]` 同时含负、正和**恰好 0**，所以"0 处取哪一侧"分得开
（`[0.0, 0.0, 1.0, 1.0]` 对 `[0.0, 1.0, 1.0, 1.0]`）；`sigmoid` 的 `[0.0, 2.0]` 给 `[0.25, 0.10499358540350662]`，
两个分量不相等且都离 0 远（0 附近导数近似，错律分不开）；`tanh` 的 `[1.5, -2.0]` 给
`[0.18070663892364858, 0.07065082485316443]`，同理。每条都**只用一个程序印两条腿**并由 plan 同时 pin，
任一侧规则改动都会在同一程序的两行之间显出来，而不是在两条用例之间。

### 方法论：能"答对"的用例比 `known_fail` 更硬

**广播那条写死形状的用例今天不是按名拒绝，是答对**——`shapeCarrierOf` 认写死的类型（`float[3]`），
形状有了、载体就有了，所以库层能算出 `[2,2,2]` 与 `[6.0]` 并和引擎逐位相同。

**因此钟是绿的，比 `known_fail` 更硬：改坏立刻红，而不是"哪天通了才红"。** 那条错数（读另一个操作数）
一变回来，这条绿用例当场答成 `d/dx numel=1 d/dx=[6.0]`、`d/dw numel=3 d/dw=[1.0, 2.0, 3.0]`；而 `known_fail`
在同样的情况下只是"从拒绝变成错数"，还要有人盯着已知失败列表才发现。**下一步再遇到"某个写法看着能通、
但只在更强的前提下成立"时，先找那个最强前提下今天已经能答对的输入，把钟做成绿的。**

**`ast_hashes.txt` 确认**：`test/plans/feat/parse/golden/ast_hashes.txt` 共 304 条，`compile/` 与
`transforms/` 的条目各 0 条（只覆盖 `test/cases/parse/**` 与 probes），所以新增
`test/cases/compile/transforms/*.cml` 不需要动它，本次也没动。

## 3. 这条律要通，缺的是什么

缺的还是"被给余切那个操作数的形状"，两条路都不通：

1. **从类型取（`extent_of`）**：`float[..]` 不写死形状，`extent_of` 在实例化处就被拒
   （`extent_of_needs_a_type_that_states_its_shape`）；6 条用例的参数全是开秩。把长度当宽化端口交出来
   也一样，宽化端口交出来的就是 `extent_of` 的答案。
2. **把长度当实参交给 `sum_to`**：`sum_to` 的第二实参要的是一个**值**，所以需要一个
   `sum_to_stated(t, at: int[])` 孪生。`git grep sum_to_stated` 在整棵树和所有分支里都是 0——它不存在，
   新加它要动 `modules/tensor`（本车道按派工不碰）。

所以 6 条要么等 `modules/tensor` 那个孪生算子，要么按 `lane-statetypes` 的路把形状写进类型（乙：那 6 条的
参数类型得从 `float[..]` 变成写死形状）。**律层没有第三条路**，而"读另一个操作数"是错的——见第 1 节。

## 4. 并入的闭包判决（不是本车道改的）

`719fc2baf` 上这 6 条停在更前面一步（`position=0`，走查还在为残差那一侧磨律）。`bbfc003ec`
（`lane-fwdclosure`）之后才轮到律自己那一处。本分支先并入它（合并提交 `77a7384ab`，冲突两处：
`transpose.cml` import 列表里 `element_at`→`pick_at`；`idx_arr_t` 保留本车道的四端口 stated 写法）。
`modules/transforms/**`、`modules/tensor/**`、`modules/autodiff/**`、`src/runtime/**`、`scripts/**` 一行未改。
## 5. 两条要记住的结论（一条给下一个碰乘积律的人，一条给嵌套形状）

**（一）开秩类型上"两侧形状相等"是看不见的。** `float[..]` 与 `float[..]` 是同一个类型，所以律里没有任何
写法能分辨"这两个形状一样"和"这两个形状会被广播成一样"。于是：

- 想读被给余切那个操作数的形状 → 转置图里它是切向，`float[..]` 不写形状，拒绝（今天的行为）；
- 想读另一个操作数的形状 → 等形状时对，广播时答错（本次实测的错数）。

**两条不能同时满足**，这不是措辞问题而是形状来源问题。引擎之所以两种都对，是因为
`modules/tensor/ops/vjp.cpp#accumulateOperand` 手上有前值、拿操作数自己当 `sum_to` 的目标；**转置图里没有
前值**。要同时满足，只有两条路：让形状进类型（乙），或者让算子接受"形状当实参"（`sum_to_stated`）。

**（二）`rule-ranks` 卡在律左半边的头，不在右侧。** `lane-statetypes` 量到的
`skip=rule-ranks uri=tensor:multiply rule=mul_tensor_t ranks=(1,1) argument types (float[3][..], float[])`，
原因是律左半边 `func mul_tensor::<T>(x: T[..], w: T[..])` 要求两个操作数**同一个元素类型**：`float[3][..]`
的元素是 `float[3]`、`float[]` 的元素是 `float`，同一个 `T` 绑不上，律在绑定阶段就没成——**右侧 `sum_to`
写成什么都不影响这一步**。要吃嵌套形状，得先让头按操作数分开（`mul_tensor::<T, S>(x: T[..], w: S[..])` 或
按位置分开的端口），那是律的形状问题，不是归约目标的问题。所以 `lane-statetypes` 那两条
`mean_axis_transpose` **不能靠改这条律的右侧解决**。

本车道在这条律上能做的已经到底：右侧的写法只有一种是对的（读被给余切那个操作数），它今天对着开秩拒绝。