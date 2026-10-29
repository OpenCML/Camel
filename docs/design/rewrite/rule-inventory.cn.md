# 标准规则库清点（`stdlib/rules.cml`）

状态：清点稿。本页是**快照**，不是第二份真相：`stdlib/rules.cml` 的声明是唯一的真相，本表的数字与分类可以用一条命令重新数出来（第 4 节）。规则的改动只改那个文件；本表随之一并更新。

## 1. 有多少条

- **37 条 `rule` 声明，全部 `export`**。`std::opt` 的规则库就是这 37 条，没有别的来源：C++ 侧不再注册规则（`modules/rewrite/module.cpp` 只注册引擎与内核；`src/passes/opt/generic/algebra.cpp` 剩下的 `neutral_only` 是文档化的参考模式，不是库的一部分）。
- 标准库里另有 10 条 `rule`（`stdlib/autodiff.cml` 的 `add_jvp` … `neg_t`），那是求导的规则集，不属于 `std::opt`。
- 测试用例里的 `rule` 是各自用例的规则，不进库。

## 2. 按注册点分组

规则集是普通的 `Rule[]`，遍是「把规则集喂给引擎」：

| 注册点（`rules.cml`） | 条数 | 谁跑它 |
|---|---|---|
| `algebra` | 31 | `rules::simplify`（注册为 `std::opt::algebra`，`std::opt` 默认流水线的一步） |
| `accuracy_laws` | 3 | `rules::simplify` 同一次调用；也单独作为 `rules::accuracy` |
| `distribution` | 2 | `rules::saturate_all`（`std::opt::saturate` 的含义） |
| `fusion_laws` | 1 | `rules::fusion`（选择加入） |

于是：`std::opt` 默认一步跑 **34** 条（`algebra` + `accuracy_laws`，一个方向到不动点）；饱和那一步跑 **33** 条（`algebra` + `distribution`，两个方向，再取最省写法）；四个入口合起来覆盖全部 **37** 条。

## 3. 按许可分组，以及每条规则的触发条件与两侧

「许可」是规则声明的 `#requires(...)`；它决定这条规则在什么前提下成立，也决定判据比较什么（第 4 节）。「两侧」是规则匹配的图与它改写成的图；`x`、`a`、`b`、`c` 是节点，`const c` 是常量节点的值，`T[..]` 是逐元素。

### 3.1 无许可：23 条（要求逐位相同）

| 规则 | 触发条件 | 两侧 |
|---|---|---|
| `mul_one` | `is_real` | `x * 1 => x` |
| `div_one` | `is_real` | `x / 1 => x` |
| `sub_zero` | `is_real` | `x - 0 => x` |
| `neg_neg` | `is_real` | `-(-x) => x` |
| `add_self` | `is_real` | `x + x => x * 2`（2 的倍乘对浮点精确，含无穷、NaN、次正规数） |
| `int_add_zero` | `is_integer` | `x + 0 => x` |
| `int_mul_zero` | `is_integer` | `x * 0 => 0 as T` |
| `int_sub_self` | `is_integer` | `x - x => 0 as T` |
| `int_combine` | `is_integer`，`const c1, c2` | `x * c1 + x * c2 => x * (c1 + c2)` |
| `int_fold` | `is_integer`，`const c1, c2` | `(x * c1) * c2 => x * (c1 * c2)` |
| `int_fold_add` | `is_integer`，`const c1, c2` | `(x + c1) + c2 => x + (c1 + c2)` |
| `int_combine_one` | `is_integer`，`const c` | `x * c + x => x * (c + 1)` |
| `int_distribute` | `is_integer`，双向 `<=>` | `a * (b + c) <=> a * b + a * c` |
| `float_add_neg_zero` | `is_float` | `x + -0.0 => x`（`+0.0` 不是单位元） |
| `float_double` | `is_float` 且 `exact_doubling(c)`（有限整数，`1 <= |c| <= 2^(p-1)`） | `x * c + x * c => x * (c + c)` |
| `array_mul_one` | `is_real`，`T[..]` | `x * 1 => x` |
| `array_div_one` | `is_float`，`T[..]` | `x / 1 => x`（整数数组的商是浮点数组，所以只有浮点这一族） |
| `array_sub_zero` | `is_real`，`T[..]` | `x - 0 => x` |
| `array_neg_neg` | `is_real`，`T[..]` | `-(-x) => x` |
| `array_add_self` | `is_real`，`T[..]` | `x + x => x * 2` |
| `array_add_neg_zero` | `is_float`，`T[..]` | `x + -0.0 => x` |
| `array_double` | `is_float` 且 `exact_doubling(c)`，`T[..]` | `x * c + x * c => x * (c + c)` |
| `array_sigmoid_chain` | `f32[..]`，无许可 | `1.0 / (1.0 + exp(-x)) => sigmoid(x)`（定义相同、同一批 IEEE 运算，逐位不变） |

### 3.2 `#requires(reassociate)`：5 条（有限值上实数相等）

| 规则 | 触发条件 | 两侧 |
|---|---|---|
| `float_combine` | `combine_same_sign(c1, c2)`：有限、非零、同号、和不溢出 | `x * c1 + x * c2 => x * (c1 + c2)` |
| `float_combine_one` | `combine_one_same_sign(c)`：`c > 0` | `x * c + x => x * (c + 1.0)` |
| `array_combine` | 同 `float_combine`，`T[..]` | 同 `float_combine`，逐元素 |
| `array_combine_one` | 同 `float_combine_one`，`T[..]` | 同 `float_combine_one`，逐元素 |
| `array_distribute` | `is_integer`，双向 `<=>`，`T[..]` | `a * (b + c) <=> a * b + a * c` |

### 3.3 `#requires(reassociate, no_infs)`：2 条

| 规则 | 触发条件 | 两侧 |
|---|---|---|
| `float_fold` | `fold_coefficients(c1, c2)`：有限、非零、积有限且非零 | `(x * c1) * c2 => x * (c1 * c2)` |
| `array_fold` | 同上，`T[..]` | 同上，逐元素 |

### 3.4 `#requires(reassociate, no_infs, no_signed_zeros)`：4 条

| 规则 | 触发条件 | 两侧 |
|---|---|---|
| `float_combine_mixed` | `combine_mixed_sign(c1, c2)`：有限、非零、异号、和不等于零 | `x * c1 + x * c2 => x * (c1 + c2)` |
| `float_combine_one_mixed` | `combine_one_mixed_sign(c)`：`c < 0` 且 `c + 1 != 0` | `x * c + x => x * (c + 1.0)` |
| `array_combine_mixed` | 同 `float_combine_mixed`，`T[..]` | 同上，逐元素 |
| `array_combine_one_mixed` | 同 `float_combine_one_mixed`，`T[..]` | 同上，逐元素 |

### 3.5 `#requires(refine_accuracy)`：3 条（两侧的位不同，命题是误差界）

| 规则 | 触发条件 | 两侧 |
|---|---|---|
| `array_log_softmax_of_softmax` | `f32[..]`，`const axis` | `log(softmax<axis>(x)) => log_softmax<axis>(x)` |
| `float_log1p` | `hasScalarKernel`（`f32`、`float`） | `log(1.0 + x) => log1p(x)` |
| `float_expm1` | `hasScalarKernel` | `exp(x) - 1.0 => expm1(x)` |

## 4. 这 37 条被验到什么程度

两种判据，落在两处：

- `check`（`rewrite` 模块）读规则自己的两侧，在样本上比较。它的翻译层只覆盖整数标量，所以浮点规则报 `sampled`，理由是 `beyond translation coverage`：那不是比较。
- 差分执行（`test/plans/feat/modules/rules_differential.plan.toml`，用例 `test/cases/modules/rules/differential_equivalence.cml`）不读两侧：它为每条规则写一个包含左侧的见证程序，只把这条规则施加到见证上，然后在同一解释器上按有限定义域跑改写前与改写后，比较两次读数。**覆盖 34 条**（3.1–3.4 全部，加 `array_sigmoid_chain`）；**不覆盖 3.5 的 3 条**，因为它们的命题是误差界而不是相等（`check` 按更高精度的参照逐样本量它们）。判据按许可分岔：3.1 逐位比较（`+0.0` 与 `-0.0` 是两个读数），3.2–3.4 按实数相等比较（相对差 2^-20 以内），并按许可排除 NaN、无穷、以及改写前就已经离开许可定义域的点。

有限定义域对每个类型给出两端、回绕点、次正规端、两个零、两个无穷与 NaN。它是**枚举的有限集**，不是整个类型：一条规则在集外的某个值上失效，这里报不出来。

重新数：

```powershell
(Select-String -Path stdlib\rules.cml -Pattern '^\s*export\s+rule\s' ).Count
```