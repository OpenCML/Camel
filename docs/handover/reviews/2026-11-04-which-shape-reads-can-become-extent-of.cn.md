# 律里那几处取形状的地方，哪些能换成 `extent_of`？

## 问什么

作者的 2026-11-03 19:35 裁定第 ③ 步要 `extent_of`，理由是"让转置在不读值的前提下拿到长度"。那么
`stdlib/rulesets/transpose.cml` 与 `stdlib/rulesets/jvp.cml` 里**已经**在读形状的那些地方，哪些可以
换成它、哪些不能？剩下唯一一处还能换的地方（`mul_tensor_t`），换不换是作者的决定：两条路今天都自洽，
代价一个是"零载体"、一个是"开秩的程序变红"。

## 读数（2026-11-04，宿主 `88zjwei`，树 `lane-extentof`，load 16.4/19.0/18.6）

`extent_of::<A>()` 今天读的是**类型实参**，一个类型参数，答 `int[]`
（`src/builtin/operators/struct.cpp#extentOfResolver`，声明在 `stdlib/prelude.cml`）。
`shape(xs)` 读的是**值**（`stdlib/tensor.cml#shape`，`t: T[..]`）。两者的分界就是这一列能不能换的根据。

| 位置 | 它读的是什么 | 能换吗 | 为什么 |
| --- | --- | --- | --- |
| `transpose.cml#idx_arr_t` | 类型：`at: A` 端口（走查把操作数类型的 extents 物化） | **已经是** | 本车道换的；`zeros_of(ct, extent_product(at))` |
| `transpose.cml#sum_from_type_t` | 同上 | **已经是** | `spread_over(ct, at)`；同一路线的三条之一 |
| `transpose.cml#take_from_type_t` | 同上 | **已经是** | `take_cotangent_from_type(ct, indices, at)` |
| `transpose.cml#mean_axis_from_type_t` | 同上 | **已经是** | `mean_axis_cotangent_from_type(ct, at)` |
| `transpose.cml#sum_stated_t` / `#take_stated_t` / `#mean_axis_stated_t` | 值：`at` 是前向半**写出来**的 `int[]`（`jvp` 的切点递过去的已知操作数） | **不能** | `at` 是值不是类型；换成 `extent_of` 等于把这个已知操作数删掉，切点那条路就没东西可递了 |
| `transpose.cml#mul_tensor_t`（`sum_to(ct * w, x)`、`sum_to(ct * x, w)`） | 值：`sum_to(t, like)` 的第二个参数是**值**，形状从它读 | **今天不能**（见下） | ①`sum_to` 只吃值，`sum_to_shape(t, s)` 不存在；②`X`/`W` 是律自己的类型参数，走查把它绑成开秩时 `extent_of::<X>()` 会按名拒绝——今天答得出来的乘积转置会变红 |
| `transpose.cml#add_tensor_t` / `#sub_t` / `#mul_t` / `#div_t` / `#neg_t` 等 | 不读形状 | 无关 | 逐元素，cotangent 直接过 |
| `jvp.cml#jvp_sum`（`sum_stated(dxs, shape(xs))`） | 值：原值 `xs` 的形状 | **不能** | `xs: T[..]` 陈述不出（设计原则 5.4）；而且 jvp 路上原值本来就在手——"形状从原值（残差）读"正是裁定 ② 的那一半 |
| `jvp.cml#jvp_mean_axis`（`stated_axis_shape(axis, keepdims, shape(xs))`） | 同上 | **不能** | 同上；`mean_axis_stated` 的第三个参数是值，换掉要连算子一起改 |
| `jvp.cml#jvp_take`（`stated_rows(indices, shape(t))`） | 同上 | **不能** | 同上 |
| `jvp.cml#jvp_idx_arr`（`extent_pair(i, len(xs))`） | 值：`len(xs)` | **不能** | `xs: T[]` 一个维度、没长度；`element_at` 的第二个参数是 `[position, length]` 这样一个**值** |

一句话的分界：**值是值的、类型是类型的**。`extent_of` 只在"那份形状除了类型没有别的来源"时才用得
上——今天正是四条 `*_from_type` 律所在的位置；`shape(...)`/`len(...)`/`sum_to(_, x)` 所在的位置都有值，
换过去只会把能答的题变成拒绝。

这一点有用例钉着：`test/plans/feat/compile/transforms.plan.toml` 的 `extent_of_is_not_shape` 在
`float[2, 3]` 上断言 `shape(t) == extent_of::<float[2, 3]>()` 为 `true`（等价的那一半），在 `float[..]` 上
断言 `shape(t)` 仍答 `[2]`（不等价的那一半），旁边 `extent_of_refuses_an_open_rank_type` 钉同一位置上
`extent_of` 的拒绝。

## 唯一还开着的一处：`mul_tensor_t`

`mul_tensor_t` 的右半边要"把 cotangent 归约回操作数的形状"，今天用 `sum_to(ct * w, x)`——读 `x` 的值。
可是**裸 `transpose` 里操作数未必有值**（`transpose(pick)` 的参数就是没有值的那个人）。今天走查的兜底是
把类型的 extents 物化成 `tensor:zeros` 递过去（`modules/transforms/transpose.cpp#shapeFromTypeOf`），
那仍然是零载体，与 17:55「不要零载体」相抵。

**这一处的债单独成一页**：`docs/handover/questions/2026-11-04-does-a-product-transpose-still-need-a-zero-carrier.cn.md`
——`sum_to` 的第二参数是值不是形状（所以"多一个端口"不是 `sum_to_shape` 的替代，三条路收敛成两条）、
两条路的代价、以及"今天没有程序走得到这处"的探针读数。**本页不改代码，默认 A：这一轮不动。**

## 谁在用 `extent_of`

今天只有两处：`src/builtin/operators/struct.cpp#extentOfResolver`（语言侧，程序可以写）与走查的
`modules/transforms/transpose.cpp#statedExtentsNodeOf`（把它的答案物化成 `int[]` 递给律）。两者都问
`include/camel/core/type/composite/array.h#statedExtentsOf`，所以"哪些类型有形状"只有一个答案。