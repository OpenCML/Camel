# 规则检查器的反例文本还在打印 `-nan`（第二处，`modules/rewrite`，本车道不许动）

## 是什么

`-nan` 在 develop 上有两处。第一处是张量打印（`src/core/rtdata/array.cpp` `formatTensorElement`），
已按设计原则 7.7 的"NaN 的文本不带符号"修掉（`withoutNaNSign`，见同批提交）。

第二处在 **`modules/rewrite`**：规则检查器渲染反例值时把浮点直接交给 fmt，
没有走 `camel::utils::withoutNaNSign`：

```
modules/rewrite/module.cpp:2850   text += std::format("{}", static_cast<float>(tensorElement(t, i)));   // 张量样本元素
modules/rewrite/module.cpp:2853   text += std::format("{}", tensorElement(t, i));                      // 张量样本元素
modules/rewrite/module.cpp:2887   default: return std::format("{}", s.asDouble);                       // 标量样本
```

`include/camel/utils/number_text.h:665-672` 把这件事写成规则："**Every path that formats a number through a
library (the interpreter hands floats to fmt) applies this first, so no path can write `-nan`, with or
without a format specification.**" 这三行是那条规则的反例；`float_format.h`、`complex_format.h` 都照做了。

## 逐字证据

`test/plans/feat/modules/rules.plan.toml:193`（`rules.a_reassociate_rule_is_compared_in_the_higher_precision`，
`cases/modules/rules/check_real_equality.cml`）今天要求并且今天**为绿**（本次跑 `rules` 计划 0 红）：

```
refuted overflow_to_nan: proposition real equality + IEEE on special values, evidence counterexample,
the two sides differ at (1.0000000150474662e+30): -nan and 0 (a special value is not the same bit for bit)
```

同一条断言在翻转线 `handover/h465-flip-integration`（`c9be5d619`）上一字未改，仍是 `-nan` —— 那条线的
张量打印已经改成数组形式（`[nan, inf, -inf, -0.0, 0.0]`），但这处反例文本没跟着动。

## 既有裁定

- `docs/design-principles.cn.md:530`：任何 NaN 都写作 `nan`，无论符号位如何、无论是否带格式说明、**在哪个后端**。
- `docs/handover/reviews/H15-H20-H21.md:66`：经 fmt 打出 `-nan` 是缺陷，改为 `nan`。
- `include/camel/utils/number_text.h:665-672`：上面那条"没有一条路径可以写 `-nan`"。

## 卡住什么

本车道的允许改动是 `src/**`、`include/**`、`test/cases/**`、`test/plans/**`、`docs/handover/questions/**`；
`modules/**` 不在其中，所以这里只定位、不修。修法很小（三处 `withoutNaNSign`，`rules.plan.toml:193` 的
`-nan` → `nan`，`modules/rewrite/module.cpp` 里 `-0` 那一处的文本不受影响），但需要另一个车道或协调者开口子。