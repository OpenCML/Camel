# `^` 的反向半读不了切向的值：那句守卫留不留，`(-3.0) ^ 2.0` 答 `-6.0` 还是 `NaN`？

## 问什么

`pow_tangent`（`stdlib/rulesets/jvp.cml#pow_tangent`）的右半边带一句守卫：

```
if dw == T(0) then T(0) else pow(x, w) * log(x) * dw
```

作者 2026-11-03 让加的，理由是 `x < 0` 时 `log(x)` 没有实值、`ln` 那一项会污染整条切向，而实际模型里
指数几乎总是常量（`dw == 0`），于是"常量指数不碰 `ln`"。`test/cases/compile/transforms/jvp_of_a_power.cml`
因此钉住两条：`p(-3.0)` 的前向读数是 `9.0` 和 **`-6.0`**，`s(-3)` 的指数分支是 `NaN`。

这条守卫和 `^` 的**反向**是互斥的，而且这一轮量到了：

- `^` 的 `vjp` 走到 `pow_tangent` 就停：`TRANSPOSE-NEEDS function=rulesets.jvp::pow_tangent<float> param=3`
  —— `param=3` 就是 `dw`，走法要**它的值**；然后是 `TRANSPOSE function=rulesets.jvp::pow_tangent<float>
  skip=condition`、`skip=call-no-transpose … callee=rulesets.jvp::pow_tangent<float>`。反向只拿到余切，
  拿不到切向的值，所以"一条在切向的值上分支的规则"转不过去。
- 把守卫去掉（探针实测）：`p(-3.0)` 的切向从 `-6.0` 变成 **`NaN`** —— 正好把上面那条已钉住的读数打红。
- 两条律这一轮都补齐了（`math:pow_d` 的 `jvp` 与转置，`stdlib/rulesets/jvp.cml#jvp_pow_d`、
  `stdlib/rulesets/transpose.cml#pow_d_t`，外加它指数分支用到的
  `stdlib/rulesets/transpose.cml#log_d_t`）。所以 `^` 这一格的阻塞**不是缺规则**：同样是 `pow`，
  换个拼法 `pow(x, 3.0)`（`stdlib/math.cml#pow`，算子 `math:pow_d`）走 `vjp` 已经给出 **12.0**，
  与引擎对 `w ^ 3.0` 在 `w = 2` 给的 **12.0** 同数。

也就是说：**同一个核、同一条数学，`pow` 的反向通了，`^` 的反向被守卫挡住。** 这个差要不要抹平、抹平
哪一头，只有作者能裁。

## 读数（可复核）

在 `545b24d920` 上、装好这一轮三条律的 stdlib 上量的（同一个二进制，只换 stdlib）：

- 引擎：`pow engine value=8.0 dL/dw=12.0`（`with <w>` 里 `w ^ 3.0`，`w = 2`）。
- 库层 `pow`：`pow library value=8.0 d/dx=12.0`（`vjp(x => pow(x, 3.0))`，`x = 2`）；非整数指数
  `pow fractional value=5.656854249492381 d/dx=7.0710678118654755`；指数分支
  `pow exponent value=8.0 d/dn=5.545177444479562`。
- 库层 `^`：`skip=condition`，即上面那三行；`vjp(p)(3.0)` 拿不到值。
- 去掉守卫后（探针，未入树）：`jvp_of_a_power.cml` 同一组读数跑成
  `9.0 6.0 9.0 nan 9.0 6.0 27.0 56.66253179403897 -0.037037037037037035 nan` —— `p(-3.0)` 那两格
  从 `9.0`/`-6.0` 变成 `9.0`/`nan`，即 `-6.0` 这一格被 `ln` 的 `NaN` 吃掉。
- 引擎跑 `pow(...)` 本来就不行：`autodiff: no derivative rule for operator 'math:pow_d' of type 'float'`
  —— 引擎只有 `:op/pow_d`（`^`）的规则，所以"库层与引擎同答"只能在 `^` 拼法上比数。

## 三条路，各带代价

**A. 守卫留着（现状）**
- 一行不改。`^` 的反向继续拒；`pow` 的反向已通并已钉数。
- 代价：D1 那张"引擎能微分、库层不能"的清单上，`^`（`:op/pow_d`）这一行留着；任何把 `^` 写在
  被微分函数体里的程序，走 `vjp`/`grad` 仍然是拒。`test/cases/compile/transforms/pow_gradient_refused_by_the_library.cml`
  现在钉的就是这一行读数，所以这条代价是**被一条用例盯着**的，不会悄悄变。
- 好处：`jvp_of_a_power.cml` 钉的三条读数（`-6.0`、`x^x` 的 `56.66253179403897`、`s(-3)` 的 `NaN`）
  一条都不动；模型里"常量指数"这条最常见的路径继续给对的数。

**B. 守卫去掉，`pow_tangent` 写成无分支**
- `x ^ w` 的切向恒为 `w * x^(w-1) * dx + x^w * log(x) * dw`，反向就能转过去。
- 代价：`x < 0` 且 `dw == 0` —— 也就是**常量指数**这条最常见的路径 —— 从 `-6.0` 变 `NaN`，
  `jvp_of_a_power.cml` 的 `-6.0` 那格要改成 `NaN`，而那正是 2026-11-03 加守卫要避免的读数。
  等号两边都变差：反射那一步（`d/dx`）本来是好的，被 `ln` 的 `NaN` 吃掉。
- 好处：`^` 的反向与 `pow` 的反向一致，D1 清单少一行。

**C. 让反向能读切向的值（`split`/`transpose` 的改动）**
- 一条新能力："被转置的规则可以读某个切向的值"——反向半把切向当残留带上，或改成"切向的守卫在
  前向半求值、结果当残留带过去"。
- 代价：这是 `modules/transforms/**` 的语义改动，本轮这条 lane 明令不许碰；而且"一条规则能不能读
  切向"是新语义，得作者先裁；带上切向的值也让反向多一份残留（每条被微分路径都多一格）。
- 好处：`^` 与 `pow` 都通，且不用放弃 `-6.0`。

## 默认（本轮按这个开工，不等回复）

**走 A，守卫留着。** 因为 A 的代价是**D1 清单上的一行**，且那一行已经被一条用例按读数钉住（谁改动
它就会红、就会说明改了什么）；B 的代价是**最常见路径上的一个错数**（`-6.0` → `NaN`），恰好是作者
2026-11-03 加守卫要避免的；C 是 `modules/transforms` 的语义新增，不在本轮可动范围，且要先有裁定。

作者若裁 B，`jvp_of_a_power.cml` 的 `-6.0` 与 `pow_tangent` 的 `@example` 两处跟着改，`pow_gradient_refused_by_the_library.cml`
可以整条删掉（那时它该绿了）；若裁 C，这一页连同 `pow_gradient_refused_by_the_library.cml` 一起
进 `reviews/`，并在 `modules/transforms` 加那一格残留。