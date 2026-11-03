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
- 读数：`feat/compile/transforms` 回到 `111 = 105 / 2 / 4`；`feat/compile` 回到 `2249 = 2239 / 2 / 8`
  （`719fc2baf` 的基线是 `2238/2/9`，差的 1 条 known 是并入的 `bbfc003ec` 回掉的
  `transpose_hand_closed_lambda`）。
- `transpose(pick)`（`transforms.plan.toml#transpose_refuses_a_parameter_value_it_cannot_be_handed`）
  仍答 `([0.0, 1.0, 0.0])`；`mean_axis1_keepdims_gradient_reads_the_row_length` 仍答
  `mean_axis1 keepdims value=120.0 cotangent=[[3.3333333333333335, 3.3333333333333335, 3.3333333333333335], [6.666666666666667, 6.666666666666667, 6.666666666666667]]`。

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