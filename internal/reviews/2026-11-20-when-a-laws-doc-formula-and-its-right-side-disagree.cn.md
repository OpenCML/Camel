# 律的文档公式与右端表达式不一致时，以哪条为准则？

**日期**：2026-11-20
**提出者**：审计车道（`lane-law-doc`，基线 `origin/develop` = `f6fccdcf8`）
**状态**：等作者裁定。**默认建议：以数学为准**——文档对就改实现，实现对就改文档，两边都不对就按数学重写；并且每一条被判为「读数不分辨」的律补一条落在**非特殊点**上的用例。默认建议写在 §4，作者可推翻。
**来源与限制**：**纯读**。本页没有构建、没有跑任何计划（主机 `10.126.62.88:22` 不通，`npm run test` 一类一步也做不了）。逐字对照全部来自源码；标「手算」的数是按定义手算的，**没有跑过**。
**页头的一句归属**：本页要对照的第一条——`log_softmax` 那条「文档与实现不一致」——是**合并车道在 2026-11-20 读出的，不是本车道**。本车道在 `f6fccdcf8` 上**复现不了它**，理由在 §2.1 与表第 19 行。本页其余的 70 行是本车道的。

## 1. 判据：每条律回答三问

对 `stdlib/rulesets/jvp.cml` 里**每一条**律（71 条 `export rule`），逐条写：

1. **① 文档给的公式与实现给的表达式，逐字抄下来，看是否等价。** 若不等价，说清**在什么条件下等价**（像 `log_softmax` 那条只在 `p` 均匀时才等）。
2. **② 该律现有用例的输入是否落在那个等价的特殊点上**（全零、均匀、单位阵、单元素、常切线……）。**这一问是核心**：用例落在特殊点上，读数绿而它证明不了这条律。
3. **③ 若 ①② 都命中，写一行「这条律的读数不分辨文档与实现」。**

「逐字不等价」有两层，本页分开算：**数学**（两侧是不同的函数，或只在特殊点上同值）与**拼写**（两侧是同一个函数，只是算子名或实参写法不同）。表第 5 列只对**数学**那一层给条件；拼写那一层写在括号里，因为它不改变读数。

**关于第 5 列的一处约定**：① 未命中（两侧处处是同一个函数）的行，第 5 列写 **`不适用`**——那时没有任何输入能把两侧分开，也就没有「辨识力」可言。这是条件式答案，不是省事。

## 2. 表：71 条律逐条对照

`jvp.cml` 里 `export rule` 共 **71** 条。其中 **69** 条的 `///` 文档头直接压在律上；**2** 条（`jvp_reshape`、`jvp_permute`）的文档写在它们配对的函数（`reshape_jvp`、`permute_jvp`）上，律本身没有头——那两条的公式仍按配对的函数读，行号照计。所以**表行数 71**。

前五条（`jvp_add`…`jvp_neg`）的右端是 `stdlib/autodiff.cml` 的命名 helper 而**不是**文档里那个表达式：helpers 的体逐字就是文档的公式（`add_tangent` 是 `dx + dw`，`sub_tangent` 是 `dx - dw`，`mul_tangent` 是 `dx * w + x * dw`，`div_tangent` 是 `dx / w - x * dw / (w * w)`，`neg_tangent` 是 `-dx`），所以是「逐字不同、处处等价」，同一文件里那句「one law, one writing」就是这么安排的。

| # | 律 | 文档给的公式（逐字） | 实现（逐字） | 等价性 / 等价条件 | 用例是否落在特殊点上 |
| --- | --- | --- | --- | --- | --- |
| 1 | `jvp_add` | `d(x + w) = dx + dw` | `add_tangent(x, w, dx, dw)` | 等价（处处） | 不适用 |
| 2 | `jvp_sub` | `d(x - w) = dx - dw` | `sub_tangent(x, w, dx, dw)` | 等价（处处） | 不适用 |
| 3 | `jvp_mul` | `d(x * w) = dx * w + x * dw` | `mul_tangent(x, w, dx, dw)` | 等价（处处） | 不适用 |
| 4 | `jvp_div` | `d(x / w) = dx / w - x * dw / (w * w)` | `div_tangent(x, w, dx, dw)` | 等价（处处） | 不适用 |
| 5 | `jvp_neg` | `d(-x) = -dx` | `neg_tangent(x, dx)` | 等价（处处） | 不适用 |
| 6 | `jvp_stop_gradient` | `d(stop_gradient(x)) = 0` | `T(0.0)` | 等价（处处，标量域：数组这一个律不服务，注释已写明） | 不适用 |
| 7 | `jvp_matmul` | `d(a · b) = da · b + a · db` | `matmul_stated(da, b, shape(a), shape(b)) + matmul_stated(a, db, shape(a), shape(b))` | 等价（处处；`matmul_stated` 的值就是乘积） | 不适用 |
| 8 | `jvp_sum` | `d(sum(xs)) = sum(dxs)` | `sum_stated(dxs, shape(xs))` | 等价（处处；`sum_stated` 的值就是和） | 不适用 |
| 9 | `jvp_mean_axis` | `d(mean<axis, keepdims>(xs)) = mean<axis, keepdims>(dxs)` | `mean_axis_stated(dxs, axis, keepdims, shape(xs))` | 等价（处处） | 不适用 |
| 10 | `jvp_sum_axis` | `d(sum<axis, keepdims>(xs)) = sum<axis, keepdims>(dxs)` | `sum_axis_stated(dxs, axis, keepdims, shape(xs))` | 等价（处处） | 不适用 |
| 11 | `jvp_max_axis` | `d(max<axis, keepdims>(xs)) = max<axis, keepdims>(dxs)` where the lane's largest element is | `max_axis_stated(dxs, xs >= max<axis, true>(xs), axis, keepdims, shape(xs))`，而 `max_axis_stated` 的**值**是 `max<axis, keepdims>(t)`（掩码不读值） | **不等价**：右端是**切线的极值**，数学要的是**极值处的切线**（选择算子 S·dx）。只有切线的极值恰好落在极值位置上才同值；**常切线永远同值** | **命中**：本树没有一条用例读这条律的前向读数（详见 §3.1） |
| 12 | `jvp_min_axis` | `d(min<axis, keepdims>(xs)) = min<axis, keepdims>(dxs)` where the lane's smallest element is | `min_axis_stated(dxs, xs <= min<axis, true>(xs), axis, keepdims, shape(xs))`，值的语义同上 | **不等价**：同第 11 行，`min` 的版本 | **命中**：同第 11 行 |
| 13 | `jvp_mean` | `d(mean(xs)) = mean(dxs)` | `mean_stated(dxs, shape(xs))` | 等价（处处） | 不适用 |
| 14 | `jvp_reshape` | `d(reshape(x)) = reshape(dx)` | `reshape_jvp(dx, dims)`，其体是 `reshape<dims>(dx)` | 等价（处处） | 不适用 |
| 15 | `jvp_take` | `d(take(t, indices)) = take(dt, indices)` | `take_stated(dt, indices, shape(t))` | 等价（处处） | 不适用 |
| 16 | `jvp_subtract` | `d(x - w) = dx - dw` | `dx - dw` | 等价（处处） | 不适用 |
| 17 | `jvp_mul_tensor` | `d(x * w) = dx * w + x * dw` | `mul_stated(dx, w, shape(x), shape(w)) + mul_stated(x, dw, shape(x), shape(w))` | 等价（处处） | 不适用 |
| 18 | `jvp_softmax` | `d(softmax(x)) = y * (dx - sum(y * dx))` with `y = softmax(x)` over the same axis | `softmax<1>(x) * (dx - sum<1, true>(softmax<1>(x) * dx))` | **公式等价（处处）**；**轴不等价**：文档说「over the same axis」，实现写死 `<1>`。只在调用轴就是 1 时等价 | **命中**：唯一用例是 rank-2、`softmax<1>`，正好是律唯一能答的那个轴（§3.3） |
| 19 | `jvp_log_softmax` | `d(log_softmax(x)) = dx - sum(exp(y) * dx)` with `y = log_softmax(x)` | `dx - sum<0, true>(exp(log_softmax<0>(x)) * dx)` | 等价（**逐字相同**）；轴写死 `<0>`，文档自己把域限成 rank-1 | 不适用（① 未命中；合并车道在本树复现不了它，见 §2.1） |
| 20 | `jvp_relu` | `d(relu(x)) = dx` where `x > 0`, and zero where it is not | `where(x > 0.0, 1.0, 0.0) * dx` | 等价（处处，含 0 处取零这个选择） | 不适用 |
| 21 | `jvp_exp` | `d(exp(x)) = exp(x) * dx` | `exp(x) * dx` | 等价（处处） | 不适用 |
| 22 | `jvp_sigmoid` | `d(sigmoid(x)) = y * (1 - y) * dx` with `y = sigmoid(x)` | `sigmoid(x) * (1.0 - sigmoid(x)) * dx` | 等价（处处） | 不适用 |
| 23 | `jvp_tanh` | `d(tanh(x)) = (1 - y * y) * dx` with `y = tanh(x)` | `(1.0 - tanh(x) * tanh(x)) * dx` | 等价（处处） | 不适用 |
| 24 | `jvp_neg_tensor` | `d(-x) = -dx` | `-dx` | 等价（处处） | 不适用 |
| 25 | `jvp_abs` | `d\|x\| = sign(x) * dx`, the sign as the mask `where(x >= 0.0, 1.0, -1.0)` | `where(x >= 0.0, 1.0, -1.0) * dx` | 等价（处处，含 0 处取 +1 这个选择） | 不适用 |
| 26 | `jvp_sqrt` | `d(sqrt(x)) = dx * 0.5 / sqrt(x)` | `(dx * 0.5) / sqrt(x)` | 等价（处处；结合次序也是文档写的那个） | 不适用 |
| 27 | `jvp_log` | `d(log(x)) = dx / x` | `dx / x` | 等价（处处） | 不适用 |
| 28 | `jvp_rsqrt` | `d(rsqrt(x)) = -0.5 * y * (y * y) * dx` with `y = rsqrt(x)` | `(dx * (-0.5)) * (rsqrt(x) * (rsqrt(x) * rsqrt(x)))` | 代数等价（处处）；乘积的结合与左右次序不同，文档说这个次序就是律 | 不适用 |
| 29 | `jvp_maximum` | `d(maximum(x, w)) = mask * dx + (1 - mask) * dw` with `mask = where(x >= w, 1.0, 0.0)` | 同左式（`1.0 - where(x >= w, 1.0, 0.0)` 写第二项） | 等价（处处） | 不适用 |
| 30 | `jvp_minimum` | `d(minimum(x, w)) = mask * dx + (1 - mask) * dw` with `mask = where(x <= w, 1.0, 0.0)` | 同左式 | 等价（处处） | 不适用 |
| 31 | `jvp_divide` | `d(x / w) = dx / w - (x / w) * dw / w` | `dx / w - ((x / w) * dw) / w` | 等价（处处） | 不适用 |
| 32 | `jvp_idx_arr` | `d(xs[i]) = dxs[i]` | `pick_at(dxs, i, len(xs))` | 等价（处处） | 不适用 |
| 33 | `jvp_pick_at` | `d(pick_at(xs, position, length)) = pick_at(dxs, position, length)` | 同左式 | 等价（处处） | 不适用 |
| 34 | `jvp_pow` | `d(x ^ w) = w * x^(w-1) * dx + x^w * log(x) * dw` | `w * pow(x, w - T(1)) * dx + pow(x, w) * math_log(x) * dw` | 等价（处处，`x > 0`；负底数时指数项按文档是 NaN） | 不适用 |
| 35 | `jvp_pow_d` | `d(pow(x, w)) = w * x^(w-1) * dx + x^w * ln(x) * dw`, both branches | `y * pow(x, y - 1.0) * dx + pow(x, y) * math_log(x) * dy` | 等价（处处，同上） | 不适用 |
| 36 | `jvp_float_from_float` | `d(float(x)) = float(dx)` where `x` is a float | `float(dx)` | 等价（处处） | 不适用 |
| 37 | `jvp_float_from_int` | `d(float(n)) = 0` where `n` is an `int` | `0.0` | 等价（处处） | 不适用 |
| 38 | `jvp_float_from_i32` | `d(float(n)) = 0` where `n` is an `i32` | `0.0` | 等价（处处） | 不适用 |
| 39 | `jvp_float_from_string` | `d(float(s)) = 0` where `s` is a `string` | `0.0` | 等价（处处） | 不适用 |
| 40 | `jvp_f32_from_float` | `d(f32(x)) = f32(dx)` where `x` is a float | `f32(dx)` | 等价（处处） | 不适用 |
| 41 | `jvp_f32_from_int` | `d(f32(n)) = f32(0)` where `n` is an `int` | `f32(0)` | 等价（处处） | 不适用 |
| 42 | `jvp_f32_from_i32` | `d(f32(n)) = f32(0)` where `n` is an `i32` | `f32(0)` | 等价（处处） | 不适用 |
| 43 | `jvp_f32_from_string` | `d(f32(s)) = f32(0)` where `s` is a `string` | `f32(0)` | 等价（处处） | 不适用 |
| 44 | `jvp_f16_from_float` | `d(f16(x)) = f16(dx)` where `x` is a float | `f16(dx)` | 等价（处处） | 不适用 |
| 45 | `jvp_f16_from_int` | `d(f16(n)) = f16(0)` where `n` is an `int` | `f16(0)` | 等价（处处） | 不适用 |
| 46 | `jvp_f16_from_i32` | `d(f16(n)) = f16(0)` where `n` is an `i32` | `f16(0)` | 等价（处处） | 不适用 |
| 47 | `jvp_f16_from_string` | `d(f16(s)) = f16(0)` where `s` is a `string` | `f16(0)` | 等价（处处） | 不适用 |
| 48 | `jvp_bf16_from_float` | `d(bf16(x)) = bf16(dx)` where `x` is a float | `bf16(dx)` | 等价（处处） | 不适用 |
| 49 | `jvp_bf16_from_int` | `d(bf16(n)) = bf16(0)` where `n` is an `int` | `bf16(0)` | 等价（处处） | 不适用 |
| 50 | `jvp_bf16_from_i32` | `d(bf16(n)) = bf16(0)` where `n` is an `i32` | `bf16(0)` | 等价（处处） | 不适用 |
| 51 | `jvp_bf16_from_string` | `d(bf16(s)) = bf16(0)` where `s` is a `string` | `bf16(0)` | 等价（处处） | 不适用 |
| 52 | `jvp_erf` | `d(erf(x)) = 2/sqrt(pi) * exp(-x*x) * dx` | `mul_stated(mul_stated(dx, exp((x * x) * -1.0), …), full(shape(x), 1.1283791670955126), …)`，即 `(dx * exp(-x²)) * 2/sqrt(pi)` | 代数等价（处处）；两侧的乘积次序不同，文档说这个次序就是律 | 不适用 |
| 53 | `jvp_erfc` | `d(erfc(x)) = -2/sqrt(pi) * exp(-x*x) * dx` | 同第 52 行，常数取 `-1.1283791670955126` | 代数等价（处处） | 不适用 |
| 54 | `jvp_gelu` | `d(gelu(x)) = gelu'(x) * dx`，斜率取 tanh 形式 `0.5*(1+t) + 0.5*x*(1-t*t)*kScale*(1+3*kCubic*x*x)` | `dx * (0.5 * (1.0 + tanh(k*(x + c*x*x*x))) + 0.5 * x * (1.0 - tanh(...)*tanh(...)) * k * (1.0 + 3.0*c*x*x))` | 等价（处处） | 不适用 |
| 55 | `jvp_transpose` | `d(transpose(x)) = transpose(dx)` | `transpose(dx)` | 等价（处处） | 不适用 |
| 56 | `jvp_permute` | `d(permute<perm>(x)) = permute<perm>(dx)` | `permute_jvp(dx, perm)`，其体是 `permute<perm>(dx)` | 等价（处处） | 不适用 |
| 57 | `jvp_where` | `d(where(c, a, b)) = (where(c, da, db))` | `where(c, da, db)` | 等价（处处） | 不适用 |
| 58 | `jvp_max` | `d(max(xs)) = sum(where(xs >= max(xs), dxs, 0))` | `sum_stated(where(xs >= max(xs), dxs, 0.0), shape(xs))` | 等价（处处） | 不适用 |
| 59 | `jvp_min` | `d(min(xs)) = sum(where(xs <= min(xs), dxs, 0))` | `sum_stated(where(xs <= min(xs), dxs, 0.0), shape(xs))` | 等价（处处） | 不适用 |
| 60 | `jvp_linear` | `d(linear(x, w)) = x @ dw + dx @ w` | `matmul_stated(dx, w, shape(x), shape(w)) + matmul_stated(x, dw, shape(x), shape(w))` | 等价（处处） | 不适用 |
| 61 | `jvp_sum_stated` | `d(sum_stated(t, at)) = sum_stated(dt, at)` | `sum_stated(dt, at)` | 等价（处处） | 不适用 |
| 62 | `jvp_sum_axis_stated` | `d(sum<axis, keepdims>(t, extents)) = sum<axis, keepdims>(dt, extents)` | `sum_axis_stated(dt, axis, keepdims, extents)` | 数学等价（处处）；**拼写不一致**：文档的左端写成一个不存在的调用形 `sum<axis, keepdims>(t, extents)`（`sum` 只有一个数据操作数，且本律挂的算子名是 `sum_axis_stated`） | 不适用 |
| 63 | `jvp_mean_stated` | `d(mean_stated(t, extents)) = mean_stated(dt, extents)` | `mean_stated(dt, extents)` | 等价（处处） | 不适用 |
| 64 | `jvp_mean_axis_stated` | `d(mean<axis, keepdims>(t, extents)) = mean<axis, keepdims>(dt, extents)` | `mean_axis_stated(dt, axis, keepdims, extents)` | 数学等价（处处）；**拼写不一致**，同第 62 行 | 不适用 |
| 65 | `jvp_take_stated` | `d(take_stated(t, indices, extents)) = take_stated(dt, indices, extents)` | 同左式 | 等价（处处） | 不适用 |
| 66 | `jvp_mul_stated` | `d(x * w) = dx * w + x * dw`, each term's shapes stated | `mul_stated(dx, w, ax, aw) + mul_stated(x, dw, ax, aw)` | 等价（处处） | 不适用 |
| 67 | `jvp_matmul_stated` | `d(a · b) = da · b + a · db`, with the two extent lists the call carries | `matmul_stated(da, b, sa, sb) + matmul_stated(a, db, sa, sb)` | 等价（处处） | 不适用 |
| 68 | `jvp_max_axis_stated` | `d(max<axis, keepdims>(t, extents)) = max<axis, keepdims>(dt, extents)` where the lane's largest element is | `max_axis_stated(dt, mask, axis, keepdims, extents)`，而它的值是 `max<axis, keepdims>(dt)` | **不等价**：同第 11 行，低一层——右端是切线的极值，转置律却把它当选择算子 | **命中**：无用例读（§3.2） |
| 69 | `jvp_min_axis_stated` | `min`'s mirror of the law above, the same mask rule turned round | `min_axis_stated(dt, mask, axis, keepdims, extents)` | **不等价**：同第 68 行 | **命中**：无用例读（§3.2） |
| 70 | `jvp_add_tensor` | `d(x + w) = dx + dw` | `dx + dw` | 等价（处处） | 不适用 |
| 71 | `jvp_log_d` | `d(log(x)) = dx / x` | `dx / x` | 等价（处处） | 不适用 |

**行数与命中数**：表 **71 行**（69 条律自带 `///` 头 + 2 条的头在配对函数上）；**语义不一致 5 行**（11、12、18、68、69）；**拼写不一致 2 行**（62、64）；**「读数不分辨」5 行**（11、12、18、68、69）。其余 64 行两侧处处是同一个函数，第 5 列记 `不适用`。

### 2.1 第一条（`log_softmax`）为什么在本树上复现不了

合并车道的读法是：文档写 `d(log_softmax(x)) = dx - sum(exp(y) * dx)`（对），实现写 `dx - exp(y) * sum<0, true>(dx)`（只在 `p` 均匀时对）。**本树不是这样。**

1. `stdlib/rulesets/jvp.cml#jvp_log_softmax` 的右端逐字是 `(dx - sum<0, true>(exp(log_softmax<0>(x)) * dx))`——**与文档的公式逐字相同**，是 `dx - sum(p · dx)`，也就是 `J = I - 1 pᵀ` 作用在 `dx` 上（`p = exp(y)`）。这一条是对的。
2. 「`… - exp(y) * sum<0, true>(…)`」那个形状在树里确实有，但它在 `stdlib/rulesets/transpose.cml#log_softmax_t`：`ct - exp(log_softmax<0>(x)) * sum<0, true>(ct)`。它是**正确的**转置：`Jᵀ = I - p 1ᵀ`，所以 `(Jᵀ ct)_j = ct_j - p_j · Σ_i ct_i`。引擎自己的 `logSoftmaxVjp` 写的就是这个，`test/plans/feat/compile/transforms.plan.toml` 里那条钉住的数（`[0.7299082804888587, 0.2658145868356072, -0.9957228673244649]`）正是 `ct - 3p`。
3. 两条式子「差一个广播」，这正好是**转置关系**，不是一处错：前向是 `dx - 1·(pᵀdx)`，反向是 `ct - p·(1ᵀct)`。`transpose.cml#log_softmax_t` 的注释把这件事写出来了，还写明反过来的 `ct - sum(p * ct)` 是错的（`ct = 1` 时它答 0，因为 `sum(p) = 1`）。

⇒ 所以「文档对、实现错」在这一条上不成立；要么合并车道读的是 `transpose.cml` 那一行（并把它当成了前向律），要么读的是另一棵树。**那一条若要作为本类的第一条证据，需要合并车道给出它在哪一棵树上量到的。** 本页不把它算进 §2 的 5 个命中。

## 3. 五条「读数不分辨」的律：具体差在哪

### 3.1 `jvp_max_axis` / `jvp_min_axis`（表 11、12）

- **文档**：`d(max<axis, keepdims>(xs)) = max<axis, keepdims>(dxs)` *where the lane's largest element is*；正文补一句正解：「a max is a selection, and the tangent of a selection is the tangent of the element it chose」。
- **实现**：`max_axis_stated(dxs, mask, axis, keepdims, shape(xs))`。而 `max_axis_stated` 的**值**是 `max<axis, keepdims>(t)`——`stdlib/tensor.cml#max_axis_stated` 的文档说「the value is the axis form's own reduction」，`modules/tensor/ops/reduction_ops.cpp#extremumAxisStatedKernel` 的注释逐字是「`mask` is not read for the value」。⇒ **右端交出的数是切线的极值，不是极值处的切线。**
- **数学上的答案**是线性选择：`S · dx`，`S` 在每行 argmax 处取 1（并列时每个并列位置各一份，`modules/tensor/ops/reduction_ops.cpp#extremumAxisVjp` 的语义）。两侧只在「切线的极值恰好落在极值位置上」时同值，**常切线（全 1）永远同值**。
- **反方向是对的**，这使问题更难看见：`stdlib/rulesets/transpose.cml#max_axis_stated_t` 写 `where(mask, lane_spread(ct, …), 0.0)`，正是 `S` 的转置。于是**正反两半对同一个算子说了两件事**：右端当它是 `max`，转置律当它是选择。`vjp` 走的是后者，所以 `vjp`/`grad` 都对，`jvp` 错。
- **现有用例**：`test/cases/compile/transforms/stated_max_axis_gradient_tie.cml` 与 `stated_min_axis_gradient_negative_axis.cml` 都走 `vjp`（读的是 `max_axis_stated_t`），`test/cases/modules/autodiff/tensor_grad_check.cml` 的 `extrema()` 走 `grad`（反向）。**没有一条用例读这条律的前向读数**：按 `jvp(` 与 `max<`/`min<` 求交集，树里没有一条 `.cml` 同时在 lane 极值上做 `jvp`。
- **一条能分辨的新读数（手算，未跑）**：`x = [[1.0, 3.0], [0.5, 2.0]]`、`dx = [[1.0, 0.0], [0.0, 1.0]]`、`f(x) = max<1, false>(x)`。两行的最大值都在第 1 列，所以**正确切线是 `[0.0, 1.0]`**；今天的右端每行答切线的最大值，即 `[1.0, 1.0]`。若把 `dx` 写成全 1（`test/cases/compile/transforms/sum_axis_lane_derivative.cml` 对 lane sum 就是这么写的），两边都答 `[1.0, 1.0]`——**那就是这个特殊点**。

### 3.2 `jvp_max_axis_stated` / `jvp_min_axis_stated`（表 68、69）

同一条，低一层：文档写 `d(max<axis, keepdims>(t, extents)) = max<axis, keepdims>(dt, extents)` 并说「the mask is the call's selection, so passing it through is the whole law」；实现把 `mask` 原样递过去，而它的值是 `max<axis, keepdims>(dt)`。**文档与实现一致，两者都漏了同一件事**：`max_axis_stated` 的值函数根本不是线性选择，所以 `d(op(t)) = op(dt)` 不是它的导数。§3.1 的那条新读数同时覆盖这一行（把 stated 拼写写进被求导的函数即可）。

### 3.3 `jvp_softmax`（表 18）

- **公式那一半两侧一致，而且已被非退化切线钉住**：`test/cases/modules/autodiff/rules_angle_args_softmax.cml` 用 `x = [[1.0, 1.0]]`、`dx = [[1.0, 0.0]]`，手算切线 `-0.25`。把公式写成 `y * (dx - sum(dx))`（把 `y` 拿到归约外面）会答 `-1.0`，所以**这一半的公式分得清**，这里没有问题。
- **不一致的是轴**：文档第一行说 `y = softmax(x)` **over the same axis**，实现写死 `softmax<1>` 与 `sum<1, true>`。只在调用轴是 1 时等价。
- **文档那段「静默答错轴」在本树上是陈的**：`jvp.cml#jvp_softmax` 的注释说「**This law answers the wrong axis, silently**」并给出 `softmax axis 2 … tangent=13.500000`。而本树 `test/plans/feat/transforms/nested.plan.toml`（`nested.rule_writes_a_constant` 的 `desc`）写着「A law that DOES spell a `<>` value stays refused: `jvp_softmax` writes `<1>`, so `softmax<2>` is refused by name」，`internal/questions/2026-11-20-a-law-that-states-its-angle-answers-only-that-angle.cn.md` §2 也记着同一件事（是**拒绝**，不是错值）。同一段还说「The named form is written and reverted」，而 `jvp_reshape` / `jvp_permute` 在本树**已经把参数写进左端**。⇒ 这一行是**文档陈、实现对**，修法是改文档（除非采纳那一页的「轴作参数」）。**本车道没构建、没跑，这一句按树内书面读数记，未自量。**
- **用例为什么分辨不出**：唯一用例是 rank-2 上的 `softmax<1>`，**恰好是律唯一能答的那个轴**。它分不开「律写了调用的轴」与「律写死了 1」。

## 4. 默认建议（本页写死，作者可推翻）

**准则：以数学为准。** 逐条落到一句话：

1. **改实现**（文档说了数学、实现没做到）：`jvp_max_axis` 与 `jvp_min_axis`。右端应写成同一文件 `jvp_max` 已有的那种形状——把选择写成掩码上的和（`sum_axis_stated(where(mask, dxs, 0.0), axis, keepdims, shape(xs))` 一类），而不是把切线的极值当答案；`jvp_max_axis_stated` / `jvp_min_axis_stated` 一起定，它们现在是同一条错。`max_axis_stated` 的**值**不动：它是公开算子，`stdlib/tensor.cml#max_axis_stated` 记的就是「值 = 轴归约、掩码不读值」，`vjp` 那一半也依赖它。
2. **改文档**（实现说了数学、文档写成别的）：`jvp_softmax` 的轴那一段（改成「律只答 `<1>`，其它轴按名拒绝」）；`jvp_sum_axis_stated` / `jvp_mean_axis_stated` 的左端拼写（`sum_axis_stated(t, axis, keepdims, extents)`）。
3. **补用例**：每一条「读数不分辨」的律补一条落在**非特殊点**上的用例——lane 极值两条按 §3.1 的 `x` 与 `dx`（常切线那一版是反例，不作数），`softmax` 的轴那一条按 §3.3 的形状补一个非 1 轴（今天会以拒绝收场，正是要钉住的读数）。
4. **一条纪律**（本页想定下来）：**一条律的读数必须在「两式不同值」的点上取**。常数、全 1、单元素、单位阵只能证明「没崩」，不能证明「答对了」；`sum_axis_lane_derivative.cml` 的全 1 切线是现成的反例形状——同一形状搬到极值上就会让一条错的律变绿。

## 5. 要作者回答的一句

律的文档公式与右端表达式不一致时，按「以数学为准」逐条改（§4 的 1、2 两笔），并且把每条「读数不分辨」的律补上非特殊点的用例——这个准则和这三笔改动，认不认？

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

认"以数学为准"。依据 9.9"跑通却给出错值不允许"、7.1"定义图是规范"。`jvp_max_axis`/`jvp_min_axis` 的前向在 develop 上仍是错值：`jvp.cml:373-374` 交出的是"切线的极值"，不是"极值处的切线"。这是用户可见的错结果，按重构总方案第 5 节现在就做最小修复（按掩码选择），配页 §3.1 的非特殊点用例；`*_stated` 孪生随骨架删除（核心抽象第 5 节）。"读数必须取在两式不同值的点上"写进求导规则的检验纪律，红线 3 的随机检验本身就在非特殊点取样。
