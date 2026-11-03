# 乘积律读另一个操作数：6 条回到原数

**结论**：`stdlib/rulesets/transpose.cml#mul_tensor_t` 的每一侧改成读**它没有在给余切**的那个操作数。
6 条挂着的读数逐位回到历史值，`known_fail` 全消。分支 `lane-multensor`，提交 `52d4ff75b`（规则与标记）、
`77a7384ab`（并入 `lane-fwdclosure` 的闭包判决）。

## 0. 这条律改成了什么

```
mul_tensor(x, w) => (ct: T[..]) => (sum_to(ct * w, w), sum_to(ct * x, x))
```

原来是 `(sum_to(ct * w, x), sum_to(ct * x, w))`：`x` 那一份的 `sum_to` 第二实参是 `x` 自己，`w` 那一份是
`w` 自己。`sum_to` 的第二实参只被读形状，而"正在被给余切的那个操作数"在转置图里是**切向**——设计原则 4.3
说转置只拿到余切、拿不到前值，所以那一侧读的是一个没有值的东西。新写法每一侧读的是另一个操作数：值是调用方
确实带着的值，形状也在同一个值上，这就是作者 2026-11-03 17:35 那条（律只能读已知操作数，原值信息由有值的
那一半交出来），也就是下标族 `jvp_idx_arr` 的 `element_at(dxs, extent_pair(i, len(xs)))` 同一招。

它对**逐元素**乘积成立，因为 `tensor:multiply` 就是逐元素的：引擎自己的孪生
`modules/tensor/ops/elementwise_ops.cpp#multiplyVjp` 只写 `multiply(dy, rhs)` 累加进 `lhs`，自己不做任何
归约，归约是 `modules/tensor/ops/vjp.cpp#accumulateOperand` 按操作数的形状补的。所以"乘积的形状"和"另一个
操作数的形状"是同一个形状，谁都能写出来。

**没有读被给余切那个操作数的值，没有加零载体，`sum_to` 仍然读形状。**

## 1. 六条读数（逐位，先手算后对）

宿主 88zjwei（32 核），2026-11-04，`-Tier fast -Jobs 4`，跑完 1 m load 13.4–17.3（宿主一直不静，读数按同
负载对比）。

| 用例 | 读数 |
| --- | --- |
| `relu_gradient_through_the_library` | `relu library value=3.0 dL/dx=[0.0, 0.0, 1.0, 0.0]` |
| `exp_gradient_through_the_library` | `exp library value=10.107337927389695 d=[2.718281828459045, 7.38905609893065]` |
| `sigmoid_gradient_through_the_library` | `sigmoid library value=0.8914007525718497 d=[0.2350037122015945, 0.19661193324148185]` |
| `tanh_gradient_through_the_library` | `tanh library value=-0.2994769986957551 d=[0.7864477329659274, 0.41997434161402614]` |
| `transpose_array_product.answers_both_operands` | `value = 20.0`、`d/dxs numel = 3`、`d/dxs = 2.0 3.0 4.0`、`d/dw numel = 3`、`d/dw = 1.0 2.0 3.0` |
| `jvp_vjp_array_product.one_point_two_ends` | `value = 20.0`、`forward  <jvp tangent, ct> = 6.0`、`backward <dxs, d/dxs> + <dws, d/dw> = 6.0` |

手算（`exp`）：`exp(1.0) = 2.718281828459045`、`exp(2.0) = 7.38905609893065`，`total = 10.107337927389695`；
`d = exp(x)`。`sigmoid`：`σ(x)(1-σ(x))`；`tanh`：`1-tanh²(x)`；`relu`：掩码 `x > 0`。
数组乘积：`xs = (1,2,3)`、`w = (2,3,4)`，`value = 20`、`d/dxs = w`、`d/dw = xs`；
`dxs = (1,-1,2)`、`dws = (0,1,-1)` 时 `forward = sum(dxs*w) + sum(xs*dws) = 7 - 1 = 6`。

转置侧现在报的是走完的那一半（`TRANSPOSE` 行里没有 `skip=`）：

```
TRANSPOSE function=split_of_jvp_of_total_backward graph=354 type=(float) => (float[],) nodes=12 arms=0 rhs=sum_stated_t,mul_tensor_t
TRANSPOSE function=split_of_jvp_of_energy_backward graph=356 type=(float) => (float[..], float[..]) nodes=11 arms=0 rhs=sum_stated_t,add_tensor_t,mul_tensor_t
```

## 2. 七条判据

1. **六条逐位读数**：见第 1 节。
2. **`feat/compile` 基线 `719fc2baf` 是 `2249 = 2238/2/9`**（本车道自测，接线后同样的 `-Tier fast -Jobs 4`，
   1 m load 17.65）。改后：**`2249 = 2245 pass / 2 fail / 2 known failures`**（load 13.43）。红没增（还是那
   两条 `transpose_loop_escape.*`），known 从 9 降到 2。
   逐条对账：9 − 2 = 7，其中 **6 条是本次提交**，另 1 条是并入的 `bbfc003ec`（`transpose_hand_closed_lambda`
   在 `34b659107` 那棵树上是红的，它在 `bbfc003ec` 里回到原数，见本树第 4 条）。
3. **`feat/compile/transforms`**：改前（合并树 `77a7384ab`，本轮实测）**`111 = 105 / 2 / 4`**，改后
   **`111 = 109 / 2 / 0`**。两条红同样是 `transpose_loop_escape.two_values_of_two_kinds` 和
   `.second_state_of_the_same_loop`，基线就有。
4. **`test/plans/feat/transforms`**：**`29 = 21 pass / 0 fail / 8 known failures`**，退出码 0。8 条 known 全是
   与本律无关的既有标记（`compositions.grad_vmap_f`、`compositions.vmap_grad_f`、`compositions.jvp_vjp_f`、
   `compositions.grad_vjp_g_zero`，各 nvm/fvm 两条）。`bbfc003ec` 提交信息里的同目录读数是 `29 = 21/0/8`，
   一致：本次没有动到这一目录。
5. **其它 `mul_tensor` 形状没变坏**：`transpose_product_fixed_shape.matches_an_open_rank`（两个 `float[3]`）仍
   答 `value = 20.0`、`d/dxs = 2.0 3.0 4.0`、`d/dw = 1.0 2.0 3.0`；
   `transforms.plan.toml#transpose_refuses_a_parameter_value_it_cannot_be_handed`（`transpose(pick)`,
   `pick(x: float[3]) = x[1]`）仍答 `([0.0, 1.0, 0.0])`；`mean_axis1_keepdims_gradient_reads_the_row_length`
   仍是自己的数。三条都在上表的整轮里 PASS。
6. **新用例先红后绿**：没有新增用例——6 条是既有条目，改前在合并树上逐条实测为
   `skip=cotangent-needs-a-value uri=tensor:multiply`（四个库用例 `position=1`，两个数组乘积 `position=0`），
   整轮 `111 = 105/2/4`；改后 `109/2/0`。红先、绿后，两轮都是同一台宿主、同一 `-Tier fast -Jobs 4`。
7. **`git diff -w` 判定**：`git diff -w --stat HEAD~1 HEAD` 与 `git diff --stat HEAD~1 HEAD` 数值完全相同
   （4 个文件，49 插入 / 50 删除），没有只由空白造成的差异；规则文件的两处 hunk 各自连续，没有搬动旧行，
   改动就是"删掉读自己的那份形状、换成读另一个操作数"加配套注释。

## 3. 与派工里那条路的对账

派工说"长度从**类型**取（`extent_of`）"。**这条路对本 6 条没有可读的长度**，两条都试过：

1. `extent_of::<X>()` 对开秩类型没有答案：`float[..]` 不写死形状，实例化处就被拒（
   `transpose_needs_parameter_shape`、`extent_of_needs_a_type_that_states_its_shape` 守着的正是这一侧）。
   6 条用例的参数全是 `float[..]`／`float[]`。把它当宽化端口交出来（`*_from_type` 那一族的写法）也一样，
   宽化端口交出来的就是 `extent_of` 的答案。
2. 就算类型写了形状，`sum_to` 的第二实参要的是一个**值**；要让律"只读长度"得有一个
   `sum_to_stated(t, at: int[])` 这样的孪生算子。`git grep sum_to_stated` 在整棵树和所有分支里都是 0——
   它不存在，新加它要动 `modules/tensor`，本车道按派工不碰。

所以"长度当已知实参读"落在**另一个操作数**上：它在被给余切的那一侧是残差（调用方带着的值），形状就在这个
值上。这就是 `docs/handover/questions/2026-11-04-who-states-the-extents-a-product-law-reads.cn.md` 第 4 节
"甲"的可实现形态，那一页已补第 5 节记这次落地。

## 4. 合入的闭包判决（不是本车道改的）

`719fc2baf` 上这 6 条停在更前面一步：走查还会为残差那一侧磨律，拒绝位置是 `position=0`。`bbfc003ec`
（`lane-fwdclosure`，"a closure member holds no cotangent"）之后才轮到律自己那一处。所以本分支先并入
`lane-fwdclosure`，合并提交 `77a7384ab`，冲突只有两处：

- `transpose.cml` 的 import 列表：合并后的 `tensor.cml` 里 `element_at` 已改名 `pick_at`，本车道新增的
  `mean_axis`、`take` 两个名字保留。
- `idx_arr_t`：`bbfc003ec` 保留了合并基的两操作数写法，本车道此前已把它换成四端口的 stated 写法；保留
  stated 写法，注释里 `element_at_t` 改成 `pick_at_t`。

`modules/transforms/**`、`modules/tensor/**`、`modules/autodiff/**`、`src/runtime/**`、`scripts/**` 本车道
一行未改。