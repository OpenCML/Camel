# 求导那一格：覆盖、三条"同一程序两条路"的读数、一处待开缺陷、一处口径

给论文渲染求导那一格用。本页不含提交信息；每个数字下面都是用例真的打出来的那几行。

## 1 覆盖：十条全部有双路（`+` 已补）

十条求导规则里，**10 条现在都有"手写规则 vs 引擎 / `with_grad`"的数值用例**：`sub`、`mul`、`div`、
`neg`（`broadcast_difference_through_vjp.cml`、`broadcast_product_gradient_{engine,library}.cml`、
`divide_gradient_both_legs.cml`、`neg_gradient_both_legs.cml`）与四个 `_t` 各自的那条同族用例。

**上一轮唯一的缺口是 `+` 那一对（`jvp_add` / `add_t`）**：它只有库侧数字
（`broadcast_add_subtract_gradient.cml` 的 `dL/db=[5.0, 7.0, 9.0]`，以及该对 `pair` 的
`ct=1.0 da=[…] db=[…]`），**没有引擎腿**。这一轮补上了：`add_gradient_both_legs.cml`
（计划条目 `add_gradient_both_legs`）把库腿（`vjp`，`jvp_add` 前向 + `add_t` 反向）与引擎腿
（`with_grad`）钉在同一串数字上，读数是下一节的第三段。

⇒ 写这一格时写 **"10/10 都有双路，`+` 由 `add_gradient_both_legs` 补上"**；上一轮的"缺口在 `+`"
是**已经关掉的历史**，不要再写成现状。

## 2 三条读数（同一程序两条路，两侧精确 `0.0`）

**`pow`**（`pow_forward_both_legs.cml`：`jvp` 前向 / `with_grad` 引擎 / 库 `vjp`）

```
pow p value=9.0 tangent=6.0
pow p engine value=9.0 tangent=6.0
pow p: forward=6.0 engine=6.0 same=true library=6.0 same_library=true
pow s value=27.0 tangent=56.66253179403897
pow s: forward=56.66253179403897 library=56.66253179403897 same=true
```

`x ^ 2.0` 在 3.0 那一组，三条路都给 `9.0` / `6.0`；`x ^ x` 在 3.0 那一组只有前向与库两条路
（引擎在这一组**拒**，见第 4 节），两条都给 `56.66253179403897` —— 一个把指数项丢掉的规则会在这里
答成**值** `27.0`，这一组就是为分开这两个数而存在的。

**`idx_take`**（`idx_take_forward_both_legs.cml`：`jvp` 前向 / `with_grad` 引擎）

```
idx_take forward value=[3.0, 1.0] tangent=[3.0, 1.0]
idx_take engine value=4.0 gradient=[1.0, 0.0, 1.0]
idx_take: forward=4.0 engine=4.0 same=true
```

两条路的"货币"不同，桥是切线本身：`xs[[2, 0]]` 的前向切线是**按写下的顺序**取到的两个位置
（`[3.0, 1.0]`，即 `dxs[2]` 再 `dxs[0]`），引擎给的是整个数组的梯度（`[1.0, 0.0, 1.0]`，位置 1 没被读）；
标量沿 `dxs` 的导数两侧都是 `4.0`。

**`add`**（`add_gradient_both_legs.cml`：库 `vjp` / 引擎 `with_grad`）

```
add library value=36.25 dL/db=[3.0, 6.0, 10.0]
add engine value=36.25 dL/db=[3.0, 6.0, 10.0]
add max_abs_diff library_vs_engine=0.000000000000e+00
```

`e = x + b`、`loss = sum(e * e)`，`x=[1.0, 2.0, 3.0]`、`b=[0.5, 1.0, 2.0]`：`loss = 36.25`，
两条腿都答 `dL/db = [3.0, 6.0, 10.0] = 2e`，逐位相同。三种错法各自会答成
`[0.0, 0.0, 0.0]`（只给一个操作数余切）、`[1.5, 3.0, 5.0]`（把 `e` 当余切）、或取负（翻号）。

三条的容差都不是新发明的：`pow` / `idx_take` 是 `abs(...) == 0.0`（`neg_gradient_both_legs.cml` 的写法），
`add` 是 `max_abs_diff ... {:.12e}`（`broadcast_difference_through_vjp.cml` 的写法）；所有数在二进制
浮点里都精确，所以 `0.0` 就是 0.0。

## 3 一处待开缺陷

**`with_grad` 对非常数指数放弃，且不给诊断。** 把 `s(x) = x ^ x` 交给引擎那条路
（`with_grad(s_with)<3.0>()`）时，run 直接以这条错误结束，程序正常的那几行输出之后没有别的答案：

```
():?:?: [Error]: Pass 'std::run' failed with an internal error: Derivative of ':op/pow_d' requires a constant exponent. The pass gave up without a diagnostic
```

"放弃且不给诊断"本身是判成坏行为的那个老毛病（`REFUSED_UNLOCATED` 一类）：一处规则不覆盖的写法，
读者既拿不到值，也拿不到指针。**这条没有钉成用例**，钉了就是把坏行为锁进期望；它是**读数**，
要不要开卡由决策者定。

## 4 口径：求导的入口名（写在这里一次）

**入口是 `jvp`（`transforms`）+ `import rulesets.jvp`；`with_grad` 在 `autodiff` 上、返回 `(value, gradient)`；
`std::jvp` 不存在。**

（这是一句口径，不是缺陷：仓里没有 `std::jvp` 这个东西，见 `git grep "std::jvp"` 零命中。）