# `with_grad` 记为"不是 stub、缺一条用例"，还是你那边有别的发现？

**状态：`lane-a3` 按清单核了一遍，读数是下面两行。请作者确认结案口径。**

## 读数

一个程序，两个变换，同一个 `loss`：

```cml
with <w: float>
func loss(x: float): float { return w * x + w * x }

let g  = loss->grad        // println(g<2.0>(3.0))   -> 6.0
let wg = loss->with_grad   // println(wg<2.0>(3.0))  -> (12.0, 6.0)
```

`w = 2, x = 3` 时 `w*x + w*x` 的值是 **12**，对 `w` 的导数是 `x + x` = **6**：

- `grad` 给 **`6.0`**；
- `with_grad` 给 **`(12.0, 6.0)`** —— 第一个分量是值 12，第二个分量是 `grad` 给的那个 6。

⇒ **与 `grad` 一致，且多给了它承诺的那个值。**

## 它是什么

`modules/autodiff/operators.cpp`：`with_grad` 与 `with_grad_aux` 注册为 **macro 算子**，规则是
`GradResolver(true, false)` / `GradResolver(true, true)`；kernel 是

```cpp
slot_t valueAndGradKernel(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    return gradFunction(norm, ctx, /*withValue=*/true, /*aux=*/false);
}
```

即 **`grad` 用的同一个 `gradFunction`，只把 `withValue` 打开**（`grad` 传 `false`）。没有第二条实现路径，也没有"占了名字但返回空/返回错值"的分支。

## 结论与建议

- **不是 stub**。有两种"能编过但结果不对"的形态，这里都不是：它既没有空实现，也没有与 `grad` 不同的语义。
- **缺的是一条锁住这两行的用例**，不是缺实现。已在本车道补上：`test/cases/modules/autodiff/with_grad_matches_grad.cml`
  + `test/plans/feat/modules/autodiff_value_with.plan.toml` 的 `value_with.same_as_grad`，断言是**交叉核对**而不是抄两个数字：
  `with_grad` 的第二个分量必须等于 `grad` 在同一参数下返回的值，第一个分量必须是 `loss` 在该参数下的值。
  把 `wg<2.0>` 改成 `wg<3.0>` 后该用例变红（`with_grad's value is 18, and w*x + w*x at w = 2, x = 3 is 12`），说明它分辨得出来。

## 需要作者回答的一句

**按"不是 stub、用例已补"结案，可以吗？** 若清单里"stub"指的是别的形态（例如某个具体 `has_aux` 分支、或某个算子族下的 `with_grad`），请指一下位置，我按那里的读数重量。