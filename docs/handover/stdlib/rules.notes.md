# `rules` 重写说明

稿件：`rules.cml`。**未编译**。保留每一条现有的定律（不丢代数覆盖），合并重复，规则名与规则集、遍、调度组合子都改到目标写法。`#requires(...)` 许可原样保留。

## 总体

1. **遍**：`simplify`、`saturate_all`、`accuracy`、`fusion` 与五个调度组合子都是 `::<F>(f: F): F! when is_function::<F>()`（组合子答 `Run<F>!`）；选项在尖括号：`f->simplify`、`f->simplify<strict: true>`、`f->saturate_all<rounds: 8>`、`f->seq<[algebra, fusion_laws]>`。
2. **规则集是值**：新增导出的 `algebra`、`distribution`、`accuracy_laws`、`fusion_laws`（都是 `Rule[]`），遍与外部使用者读同一份清单；原来 `simplifyWith`、`saturateList`、`accuracy`、`fusion` 里各抄一份的清单合并。
3. **规则的形参**：`x: T` 就是 `Node<T>`（`questions-2026-10-24-evening.md` “is-a-fact-parameter…”），所以头部不变；`const` 形参是值。
4. **错误传递**：`?->` 改普通 `->`（同 `opt.notes.md` 的泛型形参风险）；组合子的 `Run<Program>` 改 `Run<F>`；`stage` 等助手里 lambda 的形参类型省略；`started` 删除，直接 `start(f) as Run<F>!`。
5. **camelCase 助手改 snake_case**（`finiteValue` → `finite_value` …）；`module rules` 行与 `import { Program }` 删除。
6. **`tensor` 一词消失**：数组形式的规则前缀 `array_`；对 `softmax`/`log_softmax`/`sigmoid` 的引用改自 `nn`；`exp`、`log` 等来自 `math`；不再 `import … from tensor`。
7. 每条导出的规则补了一行 `///` 说明（原来多数没有）。

## 规则：合并后的清单（旧 43 条 → 37 条，没有丢掉任何一条定律）

| 新名字 | 取代 | 说明 |
|---|---|---|
| `mul_one` `div_one` `sub_zero` `neg_neg` `add_self` | `int_mul_one`/`float_mul_one`、`int_div_one`/`float_div_one`、`int_sub_zero`/`float_sub_zero`、`int_neg_neg`/`float_neg_neg`、`int_add_self`/`float_add_self` | 整数与浮点同一条（`when is_real::<T>()`）：字面量取实例的类型，`x * 1` 对浮点就是 `x * 1.0`；这五条在两个族里写的是同一个事实，都是精确的 |
| `array_mul_one` `array_div_one` `array_sub_zero` `array_neg_neg` `array_add_self` | `tensor_mul_one` … `tensor_add_self` | 数组形式不能和标量合并：形参 `x: T[..]` 把 `T` 绑到元素类型，使一条规则对所有形状成立；若用一个绑整个数组类型的形参，每个形状一个实例。顺带整数数组也适用（原来只有浮点；这些律对整数同样精确，见 Q4） |
| `int_add_zero` `int_mul_zero` `int_sub_self` `int_combine` `int_fold` `int_fold_add` `int_combine_one` `int_distribute` | 同名 | 只对整数成立 |
| `float_add_neg_zero`、`array_add_neg_zero` | `float_add_minus_zero`、`tensor_add_minus_zero` | 只对浮点（`x + 0.0` 不是单位元，`-0.0 + 0.0` 是 `+0.0`）；改名去掉 “minus” |
| `float_combine` `float_combine_mixed` `float_fold` `float_combine_one` `float_combine_one_mixed` | 同名 | 许可与条件不同于整数版，所以不合并 |
| `float_double` | `float_double` 与 `float32_double` | 两条只差倍增界（`2^52` 对 `2^23`）：界是类型决定的，写成 `exact_limit::<T>()`（`match type`）后是同一条规则 |
| `array_combine` `array_combine_mixed` `array_fold` `array_combine_one` `array_combine_one_mixed` | `tensor_*` | 数组形式 |
| `array_double` | `tensor_double` | 旧稿的条件读 `exactDoubling32`，把所有数组元素类型都按 `f32` 的界处理（旧注释自己写了“这个调用把类型变量定在 f32”）；现在界随 `T`，对 `f64` 数组不再误用 `2^23` |
| `array_distribute` | `tensor_distribute` | 整数 |
| `array_log_softmax_of_softmax` | `tensor_log_softmax_of_softmax` | 泛型于浮点 `T`（旧稿只有 `f32`）；轴是 `with` 参数，模式写作 `log(x->softmax<axis>) => x->log_softmax<axis>` |
| `float_log1p` `float_expm1` | 同名 | |
| `array_sigmoid_chain` | `tensor_sigmoid_chain` | 泛型于浮点 `T`（旧稿只有 `f32`） |

对旧名字的引用：`test/cases/modules/rules/{check_every_rule,check_library,check_accuracy_library,check_real_equality,fusion_schedule,accuracy_schedule,verify_translate,verify_report_instance,typed_pass,bench,schedulers,when_rules,check_reduced_element_type,check_type_cannot_be_made}.cml`、`test/cases/opt/{algebra_rules,generic}/**`（含 `tables.mjs`、`pairs.mjs`）、`test/cases/compile/tags/strict_algebra.cml`、`test/cases/format/wrap_wide_rule.cml` 与它的 golden、`test/cases/feat/mm/writings/saturate_draw.cml`，以及 `test/plans/feat/modules/rules.plan.toml`、`test/plans/feat/compile/tag_permissions.plan.toml`。规则的 `explain` 输出里出现规则名的 golden 一并重录。

保留的限制：`array_distribute` 只对整数，因为浮点的乘对加分配会在乘积溢出而和不溢出时给出 NaN（原注释的理由原样）。

## 其他变化

| 项 | 处理 |
|---|---|
| `simplify`、`simplify_strict` | 合为 `with <strict: bool = false> simplify`；`simplify_strict` 删除 |
| `saturate_all`、`saturate_all_strict`、`saturate_only` | `saturate_all` 带 `rounds`、`strict`；`_strict` 删除；`saturate_only` 删除（就是 `std::opt::saturate`，原注释自己写了“这个遍在核心里的名字”） |
| `extract(q, 'nodes')` | `-> extract<nodes>`，新增导出的代价函数 `nodes`（`rewrite.notes.md` Q1） |
| 调度组合子的 `strict`、`rounds` | 带默认值（`rounds: 16`、`strict: false`）；`seq`/`first` 的每阶段 16 轮写成 `rounds` 参数（评审稿 “字面量 16 是隐藏的配置”） |
| `plus`、`stage`、`settled`、`untouched`、`finished`、`pair` | 保留，泛型于 `F` |

## 调用点

- 遍：`rules::simplify` 等出现在 21 个文件（`test/cases/modules/rules/`、`test/cases/opt/algebra_rules/measure.cml`、`test/cases/compile/stream/composition_uses.cml`、`test/cases/modules/onnx/export_minimal.cml`、`test/cases/modules/pass_not_found/shadowed_rules/main.cml`、`test/plans/feat/{linear/loops,compile/impl,modules/*}.plan.toml`），`simplify_strict` 9 个、`saturate_all` 5 个、`saturate_only` 3 个：`rules::simplify_strict` → `rules::simplify strict=true`（命令行）/`f->simplify<strict: true>`（源码）；`saturate_only` → `std::opt::saturate`。
- 组合子：`test/cases/modules/rules/schedulers.cml`、`test/cases/compile/with_defaults/defaults.cml`；`seq<[…], false>` 的位置 `strict` 改为具名。
- 文档：`docs/language/09_rules_and_facts.md`、`docs/design/rewrite/{README,v2}.cn.md`、`docs/tutorial/**` 里的规则示例。

## 开放问题

**Q1. 调度组合子的归属。** 它们是 Camel 函数，而 `rewrite` 是有 `.cmo` 的声明模块（函数不能有体），所以现在放在 `rules.cml`。组合子与规则库无关，放在这里是历史原因。推荐新开一个 Camel 模块 `schedule`（`f->schedule::seq<…>`），`rules` 只留规则、规则集与四个遍。

**Q2. 浮点倍增界的 16 位延伸。** `exact_limit` 给了 `f16`（`2^10`）与 `bf16`（`2^7`），公式是 `2^(p-1)`（`p` 为有效位）。原稿只有 `f64`、`f32`。16 位的两个由 `check` 决定能否入库；`trunc` 在 `math` 里没有 16 位版本，实例也许建不出来（安静跳过）。若不愿延伸，把 `case f16`/`case bf16` 两行删掉即可。

**Q3. 数组形式规则里的初等函数。** 数组上的 `exp(x)`、`log(x)` 现在是 `math` 对数组按元素施用（由 `map` 定义），规则左边的 `log(x->softmax<axis>)` 在图上是 `map<log>` 还是对元素函数的调用，取决于 `math` 数组重载的实现与引擎对 `map` 的模式读法。若是后者，`array_log_softmax_of_softmax` 与 `array_sigmoid_chain` 要写成 `x->map<log>` 的形式；`check` 一跑便知。

**Q4. 整数数组的五条共用律。** 见上表第二行：把原来只有浮点的数组律扩到整数。推荐接受（精确、与标量族一致），否则 `array_*` 五条的条件改回 `is_float::<T>()`。

**Q5. `weighted` 代价。** `std::opt::extract` 默认的 `cost: "weighted"`（除法比乘法贵、乘法比加法贵）没有 Camel 版本；本稿只给 `nodes`。推荐 `rules.cml` 补一个 `weighted`，权重取 C++ 现用的数。
