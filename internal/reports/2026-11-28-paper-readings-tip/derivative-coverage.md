# 求导那一格：覆盖差集、两条"同一程序两条路"的读数、一处口径

给论文渲染求导那一格用。**三块的数一字未改**，来源是求导那格交回的读数；本页不含提交信息。

## 1 覆盖：十条里 8 条有、唯一缺口是 `+`

十条求导规则里，**8 条已有"手写规则 vs 引擎 / `with_grad`"的数值用例**
（`sub`、`mul`、`div`、`neg` 及四个 `_t`）。

**唯一缺口是 `+` 那一对（`jvp_add` / `add_t`）**：`+` 只有库侧数字
（`broadcast_add_subtract_gradient.cml` 的 `dL/db=[5.0, 7.0, 9.0]`，以及该对 `pair`
的 `ct=1.0 da=[…] db=[…]`），**没有引擎腿**。

⇒ 写这一格时写 **"8/10 有，缺 `+`"**，**不要写"全部有"**。

## 2 两条读数（同一程序两条路，两侧精确 `0.0`）

**`pow`**

```
pow forward          value=9.0   tangent=6.0
pow with_grad        value=9.0   grad=6.0
pow forward-vs-with_grad  same=true  diff=0.0
```

**`idx_take`**

```
idx_take forward     value=[3.0, 1.0]   tangent=[3.0, 1.0]
idx_take with_grad   value=4.0          grad=[1.0, 0.0, 1.0]
idx_take tangent-sum=4.0   with_grad-along-dxs=4.0   same=true   diff=0.0
```

两条都是**同一程序走两条路**（前向 `tangent` / `with_grad` 的 `grad`），两侧差**精确 `0.0`**。

## 3 口径：求导的入口名（写在这里一次）

**入口是 `jvp`（`transforms`）+ `import rulesets.jvp`；`with_grad` 在 `autodiff` 上、返回 `(value, gradient)`；
`std::jvp` 不存在。**

（这是一句口径，不是缺陷：仓里没有 `std::jvp` 这个东西，见 `git grep "std::jvp"` 零命中。）