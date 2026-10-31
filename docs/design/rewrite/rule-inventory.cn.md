# 标准规则库清点（`stdlib/rules.cml`）

状态：清点稿。本页是**快照**，不是第二份真相：`stdlib/rules.cml` 的声明是唯一的真相，本表的数字与分类可以用一条命令重新数出来（第 4 节）。规则的改动只改那个文件；本表随之一并更新。

## 1. 有多少条

- **37 条 `rule` 声明，全部 `export`**。`std::opt` 的规则库就是这 37 条，没有别的来源：C++ 侧不再注册规则（`modules/rewrite/module.cpp` 只注册引擎与内核；`src/passes/opt/generic/algebra.cpp` 剩下的 `neutral_only` 是文档化的参考模式，不是库的一部分）。
- 标准库里另有 10 条 `rule`，那是求导的规则集，不属于 `std::opt`：5 条前向规则在 `stdlib/rulesets/jvp.cml`（`jvp_add` … `jvp_neg`），5 条转置规则在 `stdlib/rulesets/transpose.cml`（`add_t` … `neg_t`）。规则属于哪一族由它声明在哪个模块决定；它是哪个原语的规则，由左边的运算符与元数决定。
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

三种判据，落在三处。它们不互相替代：前两种在样本上比读数，第三种对全部输入给判定。

- `check`（`rewrite` 模块）读规则自己的两侧，在样本上比较。它的翻译层只覆盖整数标量，所以浮点规则报 `sampled`，理由是 `beyond translation coverage`：那不是比较。
- 差分执行（`test/plans/feat/modules/rules_differential.plan.toml`，用例 `test/cases/modules/rules/differential_equivalence.cml`）不读两侧：它为每条规则写一个包含左侧的见证程序，只把这条规则施加到见证上，然后在同一解释器上按有限定义域跑改写前与改写后，比较两次读数。**覆盖 34 条**（3.1–3.4 全部，加 `array_sigmoid_chain`）；**不覆盖 3.5 的 3 条**，因为它们的命题是误差界而不是相等（`check` 按更高精度的参照逐样本量它们）。判据按许可分岔：3.1 逐位比较（`+0.0` 与 `-0.0` 是两个读数），3.2–3.4 按实数相等比较（相对差 2^-20 以内），并按许可排除 NaN、无穷、以及改写前就已经离开许可定义域的点。**它是常设的回归测试，不是证明。**
- SMT 证明（`scripts/rule-proofs.mjs`）：把每条规则的两侧翻译成一个 SMT 命题，问求解器它的否定是否不可满足，结论是 `proved` / `refuted`（附反例模型）/ `unknown`（附原因）。

有限定义域对每个类型给出两端、回绕点、次正规端、两个零、两个无穷与 NaN。它是**枚举的有限集**，不是整个类型：一条规则在集外的某个值上失效，这里报不出来。

### 4.1 许可就是前提

许可决定的是什么被证明，所以它被翻译成**前提**，而不是采样过滤器，也不是数值容差（设计原则 7.2：`reassociate`/`contract` 是有限值上实数相等加特殊值上逐位相等，`refine_accuracy` 是误差界）。规则自己的 `when` 也是前提，它的合取项从库里读：辅助函数（`combine_same_sign` 等）按定义展开，类型谓词（`is_real` 等）按实例求值。

| 许可 | SMT 前提 | 命题 |
|---|---|---|
| 无 `#requires` | 无 | 逐位相等：整数用 `QF_BV` 的 `=`（补码回绕，因而是一个环），浮点用 `fp.to_ieee_bv` 的相等（`+0.0` 与 `-0.0` 因此是两个读数） |
| `reassociate` | 算式在实数上读：每个操作数是它的精确实数值，没有一次舍入；这个模型的论域就是有限值，所以守卫里的 `finite_value(...)` 照常翻译并由模型满足（不是被手工丢掉） | 两侧作为实数相等 |
| `no_infs` | 没有中间值是无穷：中间的乘积或和在 R 里取，因此"去掉一次上溢"不算差异 | 同上 |
| `no_signed_zeros` | 零没有符号：R 里只有一个零，因此只差一个零的符号的规则不会被这条证伪 | 同上 |
| `refine_accuracy` | 误差界 `error(right) <= error(left) + bound`（以 ulp 计） | 不是等式，不生成查询 → `unknown` |

库里没用到的许可也逐个有行，理由是具体的，不是"跳过"：`contract`（融合乘加改变的是舍入路径，命题属于 `reassociate`）、`approx_math`（换掉的是一个核的值，两侧是两个函数）、`change_precision`（相对误差界，不是两侧相等）、`no_nans`（求解器的浮点理论只有一个 NaN，逐位视图已经一致）、`flush_subnormals`（要次正规格作前提，没有规则用它）、`nondeterministic`（值随调度变，只能采样，两种结论不许混）、`unordered_reduce`（授予归约顺序，不是数值假设）、`finite_math`（那四个名字的合写，库里逐个写）。这张表和每个名字的理由由 `scripts/rule-proofs.mjs` 打印；许可名的清单从 `src/core/operator_semantics.cpp` 读出，所以核心加一个许可而这里没有行，会被报成 `unmapped`。

### 4.2 三态：31 proved / 0 refuted / 6 unknown

实例集是 `int`、`i32`、`float`、`f32`（规则是泛型的，一条规则在每个实例上分别证明；`T[..]` 的规则按元素类型逐元素判，两侧的操作数形状不变）。

- **31 条 proved**：3.1 的 23 条里 20 条（去掉 `float_double`、`array_double`、`array_sigmoid_chain`），加 3.2 的 5 条、3.3 的 2 条、3.4 的 4 条。
- **6 条 unknown**，原因分三类：
  - `float_double`、`array_double`：守卫 `exact_doubling(c)` 给一个符号系数加了量级与整数性约束，命题要位展开"用这个符号系数乘两次"，Z3 在每条查询 40 秒内未判定（两个类型各一次）。这是浮点舍入模型本身的代价，不是翻译缺口。
  - `float_log1p`、`float_expm1`、`array_log_softmax_of_softmax`：许可 `refine_accuracy` 的命题是误差界，不是等式。
  - `array_sigmoid_chain`：两侧调 `exp`/`sigmoid`，超越函数没有 SMT 理论；另有数组形状。
- **0 条 refuted**：库里没有一条规则在它自己的许可下不成立。

`proved` 不声称的事，写在这里以免读多：带许可的浮点规则证明的是**实数相等**，设计原则 7.2 那条命题的另一半（特殊值上逐位相等）由差分执行与 `check` 量；求解器的浮点理论只有一个 NaN，所以 NaN 的载荷传播不在模型里；数组规则按元素判，形状不变是规则写法的结构事实，不是那次查询的一部分。

跑它的入口在仓库里，共三处，谁改规则谁就同时改到这三处：计划项 `rules.proofs_every_law_holds_under_its_licence`（`test/plans/feat/modules/rules_proofs.plan.toml`，以 `--require-solver --timeout 10000` 跑）、`scripts/release-gate.mjs` 的静态项「rule library: every rule holds under its own numeric licence」、以及 `test/plan-selection.json` 里由 `stdlib/rules.cml` 与 `scripts/rule-proofs.mjs` 触发同一个计划项的规则；在仓库根直接跑是 `node scripts/rule-proofs.mjs`。它要一个 SMT 求解器（`$CAMEL_Z3`、`PATH` 上的 `z3`、或 `$HOME/.local/bin/z3`）；没有求解器时它打印 `SKIPPED` 横幅并说明装什么，退出码 0——**静默通过是不允许的**，计划项用的形式带 `--require-solver`，把"没有求解器"变成失败。**这条失败说的是这台机器没有 z3，不是某条规则不成立**：横幅第一行就是 `no SMT solver was found` 与搜索过的四个位置，照着装或指 `CAMEL_Z3` 即可，不要去查规则。本机的 `camel-remote.ps1 -Check rule-proofs` 只是便利通道（该脚本在仓库外，只存在于这台机器），**不承重**；但它同样带 `--require-solver`，所以没有求解器时 `-Check all` 会红，原因同上。每次运行都先跑四条已知答案的地面查询（逐位视图分得开两个零、整数加法回绕、浮点模型会舍入），并做一次证伪自检：把 `int_combine` 的右侧故意改成 `x * (c1 - c2)`，它必须给出 `refuted` 与反例模型，未改的规则必须给出 `proved`。判定能力退化成"什么都证明"时，这两道闸先失败。

重新数：

```powershell
(Select-String -Path stdlib\rules.cml -Pattern '^\s*export\s+rule\s' ).Count
```