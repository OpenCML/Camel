# 这一站动了两个测试文件：动的是程序和一个断言，期望的数一个没改

这一站（端口次序，`lane-clport` 第 3 站）除了代码，还动了两个测例文件。两个都容易被读成"把题目改松，
让它过"，所以各写一遍来历与算法。

## 1. `test/cases/transforms/split/split_two_params.cml`：动的是**程序**，不是期望

**期望一个字节都没动**：`test/plans/feat/transforms/split.plan.toml` 相对 `1cad9b785` 是空的
（`git diff 1cad9b785 HEAD -- test/plans/feat/transforms/split.plan.toml` 无输出），那条用例仍要读

```
value= 18.0   dfdx= 12.0   dfdy= 9.0
```

原来用例把抬升后的四个实参写成**交错**的 `(x, dx, y, dy)`：

```cml
let (vx, tx) = lifted(3.0, 1.0, 2.0, 0.0)
let (vy, ty) = lifted(3.0, 0.0, 2.0, 1.0)
```

而 `50ecaf5e3` 把 `jvpSignature` 定成了 **grouped**：先所有参数的值，再所有参数的切向，所以
`jvp(f)` 的四个格是 `(x, y, dx, dy)`。于是那一行读起来是 `x=3, y=1, dx=2, dy=0`：

- 值 = `x*x*y` = 3·3·**1** = **9**（要 18）→ 这就是它唯一红的那一句 `value= 18.0`；
- 切向 = `2xy·dx + x²·dy` = 2·3·1·**2** + 9·0 = **12**（恰好还是要的 12）；
- 第二行 `(3.0, 0.0, 2.0, 1.0)` → `x=3, y=0, dx=2, dy=1`，切向 = 0 + 9·1 = **9**（也恰好是要的 9）。

也就是说：布局换了之后，**错的是值，两个导数因为数值凑巧仍然对**，所以这条用例只在 `value=` 那一句红。
它从 `50ecaf5e3` 起就红着，不是这一站弄红的。

改法是把实参按新布局写对（`(x, y, dx, dy)`），期望不动：

```cml
let (vx, tx) = lifted(3.0, 2.0, 1.0, 0.0)
let (vy, ty) = lifted(3.0, 2.0, 0.0, 1.0)
```

读数即用例自己的算术：值 = 3·3·2 = **18**；切向一 = 2·3·2·1 + 9·0 = **12**；切向二 = 0 + 9·1 = **9**。
用例注释里写的"x = 3, y = 2, 18 / 12 / 9"从此对得上。

## 2. `grad_array_with_int.fvm`：换掉的是一句**已经不该出现**的诊断

旧断言（`50ecaf5e3` 之前的那道墙）：

```toml
expect_exit = 1
expect_output_contains = ["Invalid call to 'split'"]
```

新断言：

```toml
expect_exit = 0
expect_output_contains = ["6.0", "([2.0, 3.0], ())"]
```

"让它变绿、不要改期望"指的是**那句 `Invalid call to 'split'` 不该再出现**，不是"把断言删掉"。新断言比旧的
更严：它钉住三个数，而旧断言只钉了一句诊断。

新期望的来历（`test/cases/compile/transforms/grad_array_with_int.cml`）：

- `mixed(xs, n) = xs[n] * xs[0]`，调用 `vjp(mixed)([3.0, 2.0], 1)`，种子 `1.0`。
  取 `n = 1`：`xs[1] * xs[0]` = 2.0 · 3.0 = **6.0**，这就是第一个 `println(value)`。
- `d/dxs[0] = xs[1] = 2.0`，`d/dxs[1] = xs[0] = 3.0` → `xs` 的余切是 `[2.0, 3.0]`。
- `n` 是 `int`，`Tangent<int>` 是 `()`，而 `Vjp<F>` 的 pullback 对**每个参数**答一格
  （`(..Tangent<ParamsOf<F>>)`）。所以答案的第二格是 `()` 这个空成员，整个答案打印成 `([2.0, 3.0], ())`。
  旧程序在这里打印 `(float[],)`——少一格——所以它撞的是 `ReturnTypeMismatch`，不是错数。

`6.0` / `([2.0, 3.0], ())` 这两个串就是这一站把它转绿时实测出来的原文，不是照着输出抄出来的宽松匹配：
两个串各自对应一个参数、一个算子（`mul` 与 `idx_arr`），换掉任一侧的映射都会让它们变。

## 3. 新加的用例（不是改动）

`test/cases/compile/transforms/grad_array_int_parameter_first.cml` +
`transforms.plan.toml#grad_array_int_parameter_first`：`f(n: int, xs: float[]) = xs[n]` 在
`(1, [3.0, 2.0])` 处，读 `2.0` / `()` / `[0.0, 1.0]`。`()` 在**前面**，所以那一半的端口 0 是参数 1，
这是"端口序号 ≠ 参数序号"唯一的形状。

## 4. 读数

- `feat/compile/transforms` 80 = 77 过 / 2 红 / 1 known（红：`transpose_loop_escape.two_values_of_two_kinds`、
  `second_state_of_the_same_loop`，与 develop 同）。
- `feat/transforms/split` 19/19（`split_two_params` 自从 `50ecaf5e3` 起红的那一句，这一站才转绿）。
- `feat/modules/remat` 6/6；`split.plan.toml` 与 `remat.plan.toml` 相对 `1cad9b785` 零字节改动。