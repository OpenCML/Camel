# 一条用例不登记进计划，但也不丢掉

**2026-11-02，`lane-m4b`。工程口径，配一条待观察的用例。**

## 口径

**一条用例在其观测被证明可用之前不登记进计划。** 这不是删用例，是把一个已知不可用的观测移出计划、另案跟踪。

`grad_of_an_array_parameter` 现在处于这个状态：它的观测（见 `2026-11-02-the-value-chain-after-the-port-fix.cn.md`）指向值链，而那件已移交。用例本体随本提交进分支，**登记只差一行**。

## 登记那一行（值链修好后加进 `test/plans/feat/compile/transforms.plan.toml`）

```diff
diff --git a/test/plans/feat/compile/transforms.plan.toml b/test/plans/feat/compile/transforms.plan.toml index 6b335f053..c5a2beda5 100644 --- a/test/plans/feat/compile/transforms.plan.toml +++ b/test/plans/feat/compile/transforms.plan.toml @@ -749,3 +749,24 @@ expect_exit = 0  expect_stdout_contains = ["3.0", "1.0", "-1.0", "0.25", "-1.0", "TRANSPOSE function=autodiff::div_tangent<float> graph="]  normalizers = ["strip_ansi", "normalize_newlines", "trim_trailing_space"]  timeout_ms = 20000 + +# The cotangent of a function of an array. `g(xs) = xs[1] * xs[0]` at `[3.0, 2.0]` pulls back to +# `[2.0, 3.0]`: `dg/dxs[0] = xs[1] = 2` and `dg/dxs[1] = xs[0] = 3`. An array's cotangent is an +# array, so the transposed graph answers `float[]` and not a tuple of floats -- which is the shape +# this entry exists to refuse: an answer tuple built from the callee's parameter count, or a call +# that passes the `int` index as if it were a parameter a cotangent goes to, is what the graph's +# edge invariants catch. The read is through `vjp` because `grad` is declared over `(x: float) => +# float` today (making it generic is another lane); the cotangent is the same one `grad` would read. +[[tests]] +name = "grad_of_an_array_parameter" +desc = "The cotangent of an array parameter is an array: `vjp(g)([3.0, 2.0])` for `g(xs) = xs[1] * xs[0]` reads `6.0` and then the pullback at seed 1 reads `[2.0, 3.0]` -- `xs[1]` and `xs[0]`, the two partials. The body's element read is `:op/idx_arr`, whose transpose rule writes the cotangent into an array of zeros shaped like `xs` and reads the `int` index as a value rather than as a place a cotangent goes. The reading is taken through `vjp` and not through `grad`, whose declaration in `stdlib/vjp.cml` covers `(x: float) => float` alone today; the array is reachable one step lower and the cotangent is the same one. Neither a refusal of this shape nor a graph the edge invariants reject may appear: `skip=` is absent and so is `breaks its edge invariants`, both of which the transform answered before the answer was built from the cotangent spaces. `[2.0, 3.0]` is on no forward line, so it is the pullback and not a value the case could print without differentiating." +kind = "behavior" +case = "../../../cases/compile/transforms/grad_of_an_array_parameter.cml" +args = ["${case}", "${backend}"] +backends = ["nvm"] +backends_agree = ["exit_code", "stdout"] +expect_exit = 0 +expect_stdout_contains = ["6.0", "[2.0, 3.0]"] +expect_output_not_contains = ["skip=", "breaks its edge invariants"] +normalizers = ["strip_ansi", "normalize_newlines", "trim_trailing_space"] +timeout_ms = 20000
```

加完之后 `feat/compile/transforms` 从 `49` 条变 `50` 条，而那条期望值是 **`[2.0, 3.0]`，它是对的、不改**。

## 为什么不用 `known_fail`

没修好的东西不进"这是对的"那一栏。`known_fail` 记的是"我们知道它红且接受"，这条不是——它是"观测手段还没到位"。