# H247 规则的形式化验证 —— 实施计划

依据：任务书第 5.27 节；规则语言第二版设计稿 `docs/design/rewrite/v2.cn.md` 第 8 节与第 8.1 节（`check` 的覆盖、采样对照、SMT 证明的分工）；数值契约 `docs/design-principles.cn.md` 第 7.2–7.4 节；与 H232 的衔接已有定论：泛型规则的实例化，以及它的集合与谓词。体例照 `docs/design/graph-store/plan.cn.md`。

**只读调研，没有改代码。** 所有读数用 `origin/develop`（`18c98b856`）在 Linux 主机 `88zjwei` 上的构建实测：计划形式跑 `test/plans/feat/modules/rules.plan.toml`（全绿，121 通过 0 失败，`RUN_EXIT=0`，构建 51.0 s、计划 4.0 s），单条规则的一次性读数用仓库外的探针（`/tmp`，不进仓库）。

## 0. 结论摘要

1. **今天没有任何求解器接入**：`z3` 只出现在 `docs/design/rewrite/v2.cn.md` 与任务书里，`smt` 只在设计稿与示例的性能说明里，`solver` 在 `src`、`include`、`modules`、`tools`、`cmake`、`conanfile.txt` 里**零次**出现。
2. **今天的 `check` 是纯采样对照**，签名 `check(rules, samples, seed)`，返回一个 `string`。它把许可读成两件事——采样域的过滤器与一个数值容差——**没有一处把许可读成给判定器的假设**，这与 8.1 的"许可就是前提"对不上。
3. **今天的报告词汇是 `ok` / `FAIL` / `unchecked`**，不是任务书要求的 `proved` / `refuted` / `sampled`。三个字都要换，且"只采样"这一档今天没有名字。
4. **今天会对一条真的不成立的规则说"通过"。** 探针逐字：`x * 2 / 2 => x` 在 `int`（64 位）上是

   ```
   ok wide_mul_div: 200 samples agree, fired 8 of 8
   ```

   原因是 `modules/rewrite/module.cpp#pickInt` 的边界表只列到 `int32` 的两端，64 位溢出抽不到。这**正是** 8.1 说的"采样对照可能漏掉边界"，也是本任务要用它证明"能给出反例"的那条规则。
5. **泛型规则今天完全绕过 `check`**。探针逐字：

   ```
   unchecked generic_add_zero: a generic rule, whose instances are not checked here
   ```

   而 H232 的实例化机制已经落地（`test/plans/feat/modules/rules.plan.toml` 的 `rules.generic_law_is_instantiated_per_type_tuple` 等 6 条今天全绿），所以"每个实例分别证明"缺的只是 `check` 那一侧的桥，不是实例化本身。
6. **`stdlib/rules.cml` 今天是 69 条规则**：`int` 13、`i32` 13、`float` 16、`f32` 14、张量 15——也就是**标量 54 条、张量 15 条**；带许可的 21 条（`reassociate` 族 16、`refine_accuracy` 5），其中**那 16 条同时带 `when` 守卫**，所以今天一次值比较都没做（§1.1 末尾）。按任务书"先整数与浮点的标量算子"，第一步的翻译覆盖正好是那 54 条。
7. **`check` 今天只跑到 68 条，不是 69 条**：`check_library.cml` 列了 63 条、`check_accuracy_library.cml` 列了 5 条，第 69 条 `tensor_sigmoid_chain` 从未传给 `check`。任务书的验收是"**每条**规则都有标注"，所以 `69/69` 要在一开始就当硬要求（G17、S0）。
8. **七片**：S0 判定词汇与报告类型（无求解器）∥ S1 整数标量翻译器；S1 之后 S2 反例、S3 浮点与许可、S4 `when`/`fact` 前提、S5 泛型逐实例**四路并行**；S6 报告落地与 CI 收口。每一片都有一条今天就能跑、今天失败的红前用例（§3）。缺口清单 18 条（§2）。
9. **两个问题**交给作者：许可到前提的读法（三种许可今天读法不同，哪一种算 `proved`）与判定落在哪个面上（标注写在哪、谁发布判定、覆盖不到的怎么标）。

## 1. 现状：逐条证据

### 1.1 今天 `check` 的形状

| 位置 | 行 | 事实 |
|---|---|---|
| `modules/rewrite/module.cpp#checkKernel` | 2358–2688 | `check(rules, samples, seed)` 的全部实现：拿 `samples` 与 `seed` 两个整数，逐个规则跑两条路径 |
| `modules/rewrite/module.cpp#Sampler` | 1836–2004 | 采样器：`value` / `fixedInt` / `materialize` / `tensor`；张量按秩 1、2、3 轮换 |
| `modules/rewrite/module.cpp#pickInt` | 2006–2027 | 整数样本。`edges[]` 列到 `int32` 的最大值与最小值，**没有 64 位边界**；其余均匀抽 `±100000`（`const` 参数 `±64`） |
| `modules/rewrite/module.cpp#pickFloat` | 2029–2070+ | 浮点样本。特殊值表含 `0.0/±1.0/±2.0/0.5/3.0/0.1` 与 `1e-300`、`1e300`、`1e-310`（次正规）；`refine_accuracy` 另加上溢、下溢、最小次正规、近零各档 |
| `modules/rewrite/module.cpp#RefEvaluator` | 1483–1709 | `refine_accuracy` 用的高精度参照；**认识的算子是一张硬编码表**：`:op/add_f|_d`、`sub`、`mul`、`div`、`neg`，`math:sqrt/exp/log/log1p/expm1/abs`，`tensor:add/subtract/multiply/divide/maximum/minimum/neg/abs`；表外抛 `the check has no higher-precision reference for operator '…'` |
| `include/camel/core/operator_semantics.h#RewriteErrorBound` | 247–255 | 数值契约的两个数：`ulps`、`relative`，加一个 `checkable` |
| `src/core/operator_semantics.cpp#rewriteErrorBound` | 144–173 | 许可 → 容差的映射：`kReassociate` 加 `steps + 1` 个 ulp、`kChangePrecision` 加 `2^-8 * (steps + 1)` 的相对量、`kNonDeterministic` 置 `checkable = false`；`needs == kNoPermissions` 时按位比较（容差全 0） |

三条路径（**全都是采样或"能不能触发"级别的检查，没有一条是证明**）：

1. **值路径**：抽 `samples` 组值，两侧各算一遍，比较。规则无许可时按位；有许可时按 §1.1 的容差。规则带 `when` 时这条路径**走不了**——`checkKernel` 的注释写着"工具没法拿采样值去问条件"。
2. **引擎路径**：把规则施加到它自己的左边（最多 8 次，条件规则 64 次），规则必须触发，重写后的函数也要对得上。
3. **误差路径**：`refine_accuracy` 的规则不走值路径比较相等，而是两侧分别对高精度参照（`RefEvaluator`）算误差，报最坏误差比。

**一个必须说清的后果**：`stdlib/rules.cml` 的 21 条带许可的规则里有 **16 条同时带 `when` 守卫**（`reassociate` 族正好是那 16 条：7 + 3 + 6），它们因此**完全不走值路径**，只做第 2 条路径——也就是今天这 16 条只被检查了"它能在自己的左边上触发"，两侧的值从来没有在任何一组输入上比较过。容差那条路径今天只被 `test/cases/modules/rules/check_bound.cml` 那类**不带守卫**的规则走到（`rules.check_bound_follows_the_permission` 里 `wrong_reassociate` 的 `FAIL` 就是它）。

### 1.2 今天的报告词汇（逐字，探针实测）

三条用途各一行（无许可、`#requires(finite_math)`、`#requires(reassociate)`**不带守卫**、`#requires(refine_accuracy)`、`#requires(nondeterministic)`、带 `when`、张量各一条；探针规则见附录 A）：

```
ok plain: 100 samples agree, fired 8 of 8
ok finite: 100 samples agree, fired 8 of 8
FAIL reassoc: the two sides differ at (-inf, 7, -1): -nan and -inf
FAIL accurate: the rewritten left side is less accurate at (-115.74200000000002): its error 0.0087152 exceeds the original's 0 (ratio 1.96249e+13)
unchecked anywhere: the permissions it needs have no numeric bound
ok guarded: the condition selects the instances, fired 2 of 64
ok tensor_law: 100 samples agree, fired 8 of 8
```

以及任务书点名的那条边界缺陷（第 0 节第 4 条的两行）：

```
unchecked generic_add_zero: a generic rule, whose instances are not checked here

ok wide_mul_div: 200 samples agree, fired 8 of 8

FAIL narrow_mul_div: the two sides differ at (-2147483648): 0 and -2147483648
```

**读法**：`ok` 覆盖了三种很不一样的处境——真的证不了但采样一致（`plain`）、许可放宽了容差后采样一致（`finite`）、条件规则压根没比较（`guarded`）；`unchecked` 是第四个字，只在两种情形出现（带许可但无界、泛型规则）。任务书要的三个字里，`proved` 与 `sampled` 今天都没有对应物，`refuted` 的对应物是 `FAIL`，而 `FAIL` 今天把"采样撞上反例"与"许可允许的不等"混在一起（`reassoc` 就是后者：它是一条**不带 `when` 守卫**的 `reassociate` 规则，所以走到了值路径，而 `reassociate` 一个位都不含 `no_infs`，样本里就有 `-inf`；标准库的 16 条 `reassociate` 规则都带守卫，走的是"条件选择实例"那一行，见 §1.1 末尾）。这一处冲突是交给作者的第一个问题。

### 1.3 今天有没有求解器接入

| 断言 | 证据 |
|---|---|
| 没有求解器代码 | `git grep -n -i -w solver -- src include modules tools cmake conanfile.txt`：零命中 |
| 没有 Z3 | `git grep -n -i -w z3`：只有 `docs/design/rewrite/v2.cn.md` 第 8.1 节的"求解器（Z3）作为可选的构建依赖"与任务书第 5.27 节 |
| 没有 SMT | `git grep -n -i -w smt`：只有设计稿第 8.1 节与示例的性能说明（那里的 SMT 是"同时多线程"的意思，与求解器无关） |
| 没有依赖位 | `conanfile.txt` 六条 requires（`cpp-httplib`、`fmt`、`gtest`、`nlohmann_json`、`taskflow`、`pybind11`），无求解器 |
| 没有构建开关 | `cmake/options.json` 四个选项（`ENABLE_FASTVM_JIT`、`ENABLE_FASTVM_COMPUTED_GOTO`、`CAMEL_ENABLE_PYTHON`、`CAMEL_TENSOR_BLAS`），无求解器 |

所以第 8.1 节的"依赖"那一条今天**一行都不存在**：没有可选的构建依赖，也没有"缺了它怎么办"的先例可循。

### 1.4 今天的许可表达与它隐含的前提映射

许可写在规则声明前：`#requires(...)`，解析在 `src/parse/ast/builder.cpp`（第 869 行起），名字必须是核心的许可标签，否则报 `SemanticDiag::TagNotAPermission`（今天是负向用例 `rules.requires_unknown` 等 5 条）。

| 许可 | 今天读成什么 | 8.1 要它读成什么 |
|---|---|---|
| `reassociate` | 容差 `steps + 1` ulp；采样域**不**缩小。标准库里这 16 条同时带 `when`，所以这一档容差**走不到**（§1.1 末尾） | 实数上相等的命题（第 8.1 节） |
| `contract` | 不加容差（`rewriteErrorBound` 里是空分支） | 同 `reassociate` |
| `no_nans` / `no_infs` / `no_signed_zeros` | 采样时**不抽**该类特殊值；判定时也把任一侧的中间值出现无穷 / NaN 的点排除（`no_infs`、`no_nans`），并让精确比较不看零的符号（`no_signed_zeros`） | "输入与中间值不是 NaN/无穷/负零/次正规数"的假设 |
| `flush_subnormals` | 采样时**不抽**次正规数 | 同上一行的假设，其中次正规数那一项 |
| `change_precision` | 容差 `relative += 2^-8 * (steps + 1)` | 与 bf16 精度对应的误差界 |
| `refine_accuracy` | 改走误差比路径（`RefEvaluator`） | "右边不比左边差"的误差界命题 |
| `approx_math` | **`rewriteErrorBound` 里没有这个位** | 未定（设计稿没说） |
| `nondeterministic` | `checkable = false` → `unchecked` | 未定（设计稿没说） |

**关键**：今天这套映射仍然是**采样域 + 容差**（上表前两条的许可已顺带把中间值出界的点从反例空间里排除，`flush_subnormals` 还只到采样域），不是一张可以从许可直接读出的**假设列表**。8.1 要的是一张可以从许可直接读出的前提表，交给求解器。它是本计划里"许可到前提的映射"这一项交付物的核心，也是两个问题文件里问题最多的那一块。

**还有一层**：许可与 `when` 今天是**两个各自独立的面**——许可决定采样域与容差，`when` 决定"这条实例算不算数"，而两者在判定语义上从未合到一起。8.1 要把两者都变成前提，所以前提表是**许可的假设 ∪ `when` 的假设**，这是 S3 与 S4 的交界，S4 的产物必须与 S3 的前提表拼成同一张表，不能各写各的。

### 1.5 `when` 与 `fact` 今天的位置

- **`when`**：有实现，编译成第三个函数 `name__when(params)`（`docs/technical/46_rules.md` 第 6 节）。`check` 对它只有一个处置：值路径跳过，只跑引擎路径，报告 `ok name: the condition selects the instances, fired k of m`。**条件从不被当成前提**，因为采样值上无法求值。
- **`fact`**：`modules/rewrite/fact.cpp` 与 `modules/rewrite/module.cpp#factKernel`（注册名 `"fact"`）实现的是设计稿第 7.5 节**之前**的形态；`src/parse/ast/builder.cpp` 里已经把它算出来的东西叫 `fact` 函数（`range__on0_fact`），声明面也已经叫 `fact`。两件第 8 节的事今天都没有：8.1 的"`fact` 的结论作为前提"，以及 8.6 的"没有函数体的 `fact` 是一张由规则填写的关系表"（`fact dominates(a, b): bool join (a: bool, b: bool) => a | b` 那种写法）。`test/plans/feat/modules/rules.plan.toml` 里的 `rules.fact_reads_a_fact_in_when`、`rules.fact_takes_the_parts_of_a_compound`、`rules.node_of_reaches_a_const_parameter` 三条覆盖的就是它的行为，今天全绿。

### 1.6 `explain` / `why_not` 的报告面

| 位置 | 事实 |
|---|---|
| `modules/rewrite/module.cpp#explainKernel`、`#explainAllKernel` | `explain(r, a, b)` 给最短改写链，`explain_all(r)` 给每一步 |
| `modules/rewrite/module.cpp#whyNotKernel` | `why_not(r, rule, v)` 给"哪条规则先动了手（并点出实例名）"，否则给 `DraftWritings::whyNot` 的 `RuleMiss::detail`（模式在哪不匹配 / 哪项许可缺 / 哪个条件为假）；**没有** 8.7 列的"哪个事实还不知道"那一种答案 |
| `modules/rewrite/proof.h#ProofObject` | 证明的载荷：`RewriteRecord`（步骤 + 节点映射 + 策略）、`RewriteStats`、`input`、可选的目标链 |

**与形式化验证的关系**：8.1 要的反例（`refuted` 附反例）与 8.7 的 `why_not` 是**两个面**——`why_not` 回答"这条规则为什么没在这里触发"，是**运行后**的事实；反例回答"存在这样的输入使两侧不等"，是**对所有输入**的命题的否定见证。两者都必须给，但**不能互相冒充**：今天 `why_not` 已经有报告实例类型的能力（`whyNotKernel` 里对 `rule->name() + "<"` 的分支，`rules.generic_law_instance_is_read_out_by_why_not` 今天绿），所以泛型逐实例的**报告**有一半已经在手上，缺的是**判定**。

### 1.7 泛型规则今天的实例化（H232 的接口）

| 位置 | 事实 |
|---|---|
| `include/camel/compile/gir/rule_templates.h#GenericRuleEntry` | 一条泛型规则的模板 + 每个试过的类型元组一条 `instances` |
| `include/camel/compile/gir/rule_templates.h#GenericRuleInstance` | 一次实例化的结果：类型元组（`types` 与给人读的 `typeText`）、`made`、失败原因 `why`、实例自己的 `lhs`/`rhs`/`when` 与 `whenArity` |
| `include/camel/compile/gir/rule_templates.h#genericRuleInstance` | 取某个元组的实例，按元组缓存 |
| `modules/rewrite/module.cpp`（`genericRuleInstance` 的两处调用） | 匹配处（改写路径）与 `why_not` 的读出处 |

**今天 `check` 与它的唯一交互是**：`checkKernel` 看到 `RuleObject::ruleId() != 0` 就直接 `continue`，输出 `unchecked <name>: a generic rule, whose instances are not checked here`，注释里写着这条路"是这座桥的下一步"。所以 H232 的衔接点非常小：`check` 那一侧要调 `genericRuleInstance` 枚举元组，并对每个**实例**（`GenericRuleInstance::lhs` / `rhs` / `when`）分别判定。类型集合按规则：算子组注册过的类型里满足 `when` 的每一个，给了 `program` 就并上程序里出现的类型；多个类型参数时取笛卡尔积，积过大按种子抽样并在报告里写明抽了哪些。

**今天词表里没有的东西**：`stdlib/rules.cml` 的 69 条里**一条泛型规则都没有**（`rule <名字>::<` 零命中）。所以"泛型规则对每个实例分别证明"的验收要用 `test/cases/modules/rules/generic_law.cml` 那一类用例（或 H232 折叠后的新库）来做，不能拿今天的库直接做。

### 1.8 `stdlib/rules.cml` 今天的规模与分布

737 行，69 条 `rule` 声明（`export rule`）。按元素族分：

| 族 | 条数 | 例 |
|---|---|---|
| `int`（64 位，回绕） | 13 | `int_add_zero`、`int_combine`、`int_fold`、`int_distribute` |
| `i32`（同上，32 位） | 13 | `int32_add_zero`、`int32_combine`、`int32_fold`、`int32_distribute` |
| `float`（f64） | 16 | `float_add_minus_zero`、`float_combine`、`float_log1p`、`float_expm1` |
| `f32` | 14 | `float32_combine`、`f32_log1p`、`f32_expm1` |
| 张量（`Tensor<int>` / `Tensor<f32>`） | 15 | `tensor_f32_combine`、`tensor_i64_distribute`、`tensor_log_softmax_of_softmax` |

带许可的 21 条：`#requires(reassociate)` 7 条、`#requires(reassociate, no_infs)` 3 条、`#requires(reassociate, no_infs, no_signed_zeros)` 6 条、`#requires(refine_accuracy)` 5 条。其余 48 条按 `docs/design-principles.cn.md` 第 7.3 节的默认值不需要许可，走 `kNoPermissions` 的按位比较路径。**21 条带许可的规则里 16 条同时带 `when` 守卫**——正好是 `reassociate` 族那 16 条（7 + 3 + 6），所以今天这 16 条一次值比较都没做（§1.1 末尾）。剩下 5 条 `refine_accuracy` 规则走误差路径。今天是**按规则条数**而不是按规则语义挑路径的，这一点决定了第一步该把"每条规则都有一条值比较或一条证明"当成硬要求。

算子的书写是类型化的模式（`:op/add_l`、`:op/add_i`、`:op/add_f`、`:op/add_d` 一类的后缀 `_l`/`_i`/`_f`/`_d`）。整数 `+ - *` 与一元 `-` **回绕**（`include/camel/standalone/num.h#wrapMul` 一族的无符号重解释）；整数 `/` 与 `%` 在除数为 0 时**故障**，除数为 `-1` 时按无符号回绕（`include/camel/utils/int_div.h#quotient`、`#remainder`）。这三条正是翻译器到 SMT 位向量的直接依据——**没有任何未定义行为**，所以翻译是忠实的，不需要 C 语义那样的"UB 即自由"处理。

### 1.9 今天的验证在哪里被运行

- 计划：`test/plans/feat/modules/rules.plan.toml`，`tier = "fast"`，121 个测试里 8 条跑 `check`：`rules.check_accepts_rules_that_hold`、`rules.check_finds_rules_that_do_not_hold`、`rules.library_passes_the_check`、`rules.check_bound_follows_the_permission`、`rules.when_passes_the_check`、`rules.accuracy_check_compares_error`、`rules.accuracy_rejects_naive_softmax`、`rules.accuracy_library_passes_the_check`。全部只断言**输出片段**（`expect_stdout_contains`）与"没有 `FAIL`"，所以判定词汇一改，这些断言要一起改。
- **覆盖并不完整，今天就已经有一条漏网**：`rules.library_passes_the_check` 走 `test/cases/modules/rules/check_library.cml`，那个用例列了 **63** 条规则；`rules.accuracy_library_passes_the_check` 走 `check_accuracy_library.cml`，列了那 5 条 `refine_accuracy` 规则；**第 69 条 `tensor_sigmoid_chain` 从来没有传给 `check`**——它只在 `test/cases/modules/rules/fusion_schedule.cml` 里被 `rewrite` 跑过（`rules_libraries.plan.toml` 的 `rules.fusion_sigmoid_chain`）。也就是说 `stdlib/rules.cml` 头部注释与 `docs/technical/46_rules.md` 第 5 节写的"没有一条规则在通过 `check` 之前进入这个文件"今天有**一条**不成立。任务书的验收是"**每条**规则都有标注"，所以第一步就要把 `69/69` 当硬要求，S0 的验收里带一条"`check` 的输入覆盖 `stdlib/rules.cml` 的每一条规则"。
- 选择规则：`test/plan-selection.json` 把 `stdlib/rules.cml` 与 `modules/rewrite/` 映到 `["smoke", "feat/opt", "feat/trans/pass_dump.plan.toml", "feat/modules/rules.plan.toml", "feat/compile"]`。
- CI：`.github/workflows/ci.yml` 三个常驻腿（两个 ubuntu、一个 windows-2022），每个腿走 `.github/workflows/ci-leg.yml` 的 "Test (smoke + feat)"，命令是 `node scripts/test.js`（快速层）。批次点是 `npm run release-gate`：快速层 + **按计划文件点名**跑可跑的慢层（`docs/test-plans.md` 的 "Layers: fast, slow And debug" 与 "Running" 两处）。

## 2. 缺口清单

"缺哪一层"用四档：**声明面**（规则语言/标准库的写法）、**翻译器**（规则 → 逻辑）、**求解器接入**（依赖与判定）、**报告**（词汇、格式、落地物）、**CI**。

| # | 今天是什么 | 任务要求什么 | 缺哪一层 | 预估代价（文件数 / 构建次数 / 能否并行） |
|---|---|---|---|---|
| G1 | `check` 返回 `string`，报告是自由文本 | 每条规则有 `proved`/`refuted`/`sampled` 的标注 | 声明面 + 报告 | 2 文件（`stdlib/rewrite.cml` 的类型、`modules/rewrite/module.cpp#checkKernel`）/ 1 次 / 可与 G6–G8 并行 |
| G2 | 没有 `CheckReport` 类型（只有裁定的那一行签名提到它） | 每行带判定、命题种类、证据 | 声明面 | 1 文件 + 1 个类型定义 / 同 G1 的构建 / 串在 G1 上 |
| G3 | 规则两侧就是两张图，没有任何逻辑层表示 | 翻译成"对所有输入成立"的命题 | 翻译器 | 新目录 `modules/verify/`，3–4 文件 + `CMakeLists.txt` / 1 次 / 与 G1 并行 |
| G4 | `z3`/`smt`/`solver` 在代码与构建里零命中 | Z3 作可选构建依赖，不进运行时 | 求解器接入 | `cmake/options.json`、`cmake/Options.cmake`、根 `CMakeLists.txt` + 1 文件 / 2 次（ON 与 OFF 各一次配置）/ 与 G3 并行 |
| G5 | 许可只映射到采样域与容差 | 许可 → 前提（假设列表） | 翻译器 + 报告 | 1–2 文件（许可 → 假设） / 与 G3 同批 / 串在 G3 后 |
| G6 | `reassociate`/`contract` 只放宽容差；16 条带守卫的走"条件选择实例"，不带守卫的实测在 `-inf` 处 FAIL | 证它们在**实数**上相等 | 翻译器 | 1 文件（实数模型）+ 报告多一列 / 同 G5 的构建 / 串在 G5 后 |
| G7 | `refine_accuracy` 走误差比路径，措辞是 `FAIL … is less accurate` | "右边不比左边差"的误差界命题 | 报告 + 翻译器 | 与 G5 同文件 / 同上 / 串在 G5 后 |
| G8 | `nondeterministic` → `checkable=false` → `unchecked`（第四个字） | 只能 `sampled`，且要写明原因 | 报告 | 与 G1 同文件 / 同 G1 / 串在 G1 上 |
| G9 | `when` 无法在采样值上求值，值路径整条跳过 | `when` 作前提 | 翻译器 | 1 文件（前提与条件图） / 1 次 / 与 G5 并行 |
| G10 | `fact` 与判定无关 | `fact` 的结论作前提 | 翻译器 + 声明面 | 1 文件；声明面的更名由另一条工作完成，不在本计划 / 与 G9 同批 / 与 G9 并行 |
| G11 | 泛型规则在 `check` 里直接 `continue` | 每个实例分别证明 | 翻译器 + 报告 | 1 文件（枚举元组、取实例）+ 报告每行带元组 / 1 次 / 与 G5、G9 并行 |
| G12 | 报告无格式契约，`FAIL` 混装两种处境 | 反例要可复现、命题种类要显式 | 报告 | 与 G1 同文件 / 同 G1 / 串在 G1 后 |
| G13 | 没有任何东西记录 69 条规则的判定 | 每条规则都有标注 | 报告 + CI | 1 份生成物 + `test/plan-selection.json` 1 条 / 1 次 / 串在 G1–G12 后 |
| G14 | 快速层 `node scripts/test.js` 不装任何求解器 | 求解器是可选依赖，缺了要**如实降级** | CI | 1 个计划文件 + `.github/workflows/ci.yml` 1 个可选 job + `ci-leg.yml` 不动 / 2 次（ON/OFF）/ 串在 G4 后 |
| G15 | 15 条张量规则与 54 条标量规则在报告里没有区别 | 超出覆盖的要标出来 | 报告 | 1 列 / 同 G1 / 串在 G13 前 |
| G16 | 求解器返回 `unknown` 或超时时没有处置 | 不许当 `proved` | 求解器接入 + 报告 | 与 G4 同文件 / 同 G4 / 串在 G4 后 |
| G17 | `check` 今天只跑到 69 条规则里的 **68** 条（`tensor_sigmoid_chain` 从未传入，§1.9） | "**每条**规则都有标注" | 报告 + 测试 | 1 个用例补一条规则名 / 同 S0 的构建 / 与 G1 同批 |
| G18 | 没有任何"翻译覆盖集"的声明（哪些算子、哪些宽度可译） | 8.1 明列浮点四宽度 `f64`/`f32`/`f16`/`bf16` 与整数两宽度 `int`/`i32` | 翻译器 + 报告 | 1 个能力表 + 报告一列 / 与 G3 同批 / 与 G3 同批 |

**合计**：新目录 1 个（`modules/verify/`，约 8–10 个源文件）、既有文件改动 5 个（`modules/rewrite/module.cpp`、`stdlib/rewrite.cml`、`cmake/options.json`、`cmake/Options.cmake`、根 `CMakeLists.txt`）、测试与 CI 各 2–3 个、配置期 2 次（Z3 ON/OFF）、全量构建约 8–10 次。

**按效果排的优先级**（如果只能做一半）：G1+G2+G8+G12+G17（词汇、类型、原因、格式、69/69 覆盖）今天就能做完且不需要求解器，做完之后**任何**以后的求解器工作都有一个诚实的报告面；G3+G4+G16+G18 是求解器接入的地基；G5–G7、G9–G11 是判定本身。所以"先报告、后判定"不是偏好，是因为报告面是**其它所有工作的验收面**。

## 3. 切片与顺序

按**可并行度**排。每片给出：产物、**红前用例**（今天就能跑、今天失败）、验收（逐字命令与期望输出）。红前用例一律以"今天实测到的逐字输出"为对照——即**今天的那一行与要求的那一行不同**。

**本节的路径约定**：`modules/verify/`、`test/plans/feat/modules/verify.plan.toml`、`test/plans/feat/modules/verify_solver.plan.toml`、`test/cases/modules/rules/verify_witness.cml`、`verify_licences.cml`、`verify_generic.cml` 都是**本计划提议新建的文件，今天不存在**；它们的名字在下面每一处都带"（新建）"。本节引用的其余路径今天都已存在。

### S0 判定词汇与报告类型（不引入求解器）

- **产物**：`check` 的每一行以判定开头，取值为闭集 `proved` / `refuted` / `sampled`；另有一列命题种类（`equality` / `real equality` / `error bound`）与一列证据（`solver` / `samples`）。无求解器时**每条规则都是 `sampled`**（今天的行为等价，词汇对齐）。第四个字 `unchecked` 折进 `sampled` 并带原因（`no numeric bound` / `generic rule`）。
- **红前用例**：
  ```
  camel test/cases/modules/rules/check_library.cml std::nvm
  ```
  今天的输出第一行逐字是 `ok int_add_zero: 200 samples agree, fired 8 of 8`。加一条计划断言"每一行匹配 `^(proved|refuted|sampled)\b`"——今天红（首词是 `ok`）。同一片再收掉 §1.9 那条漏网：断言"`check` 的输入覆盖 `stdlib/rules.cml` 的 **69** 条规则"——今天红（今天分两次覆盖 63 + 5 = 68，`tensor_sigmoid_chain` 不在其中）。
- **验收**：
  ```
  camel test/cases/modules/rules/check_library.cml std::nvm
  ```
  期望含 `sampled int_add_zero: samples 200, evidence samples, class equality`（措辞由 S0 定，但首词必须是三个字之一，且不允许出现 `ok ` 开头的行）。另一条验收：把 `stdlib/rules.cml` 的 69 个导出规则名与 `check` 实际跑到的规则名对照，差集为空。
- **可并行**：与 S1 并行（两层不同：报告 vs 翻译器）。

### S1 翻译器：整数与浮点的标量算子

- **产物**：`modules/verify/` 的翻译层。规则的两侧（`GenericRuleInstance::lhs`/`rhs`，或非泛型规则的 `RuleObject` 两侧图）译成逻辑项：
  - **整数**按位向量：`int` 64 位、`i32` 32 位，**回绕语义**。`+ - *` 与一元 `-` 直接映射（依据 `include/camel/standalone/num.h#wrapMul` 一族）；`/` 与 `%` 映射为**有符号**除法/取余，并生成前提**除数非零**（依据 `include/camel/utils/int_div.h#quotient`：除数为 `-1` 时按无符号回绕，与 SMT-LIB 的有符号除法定义一致；除数为 0 在语言里是故障，故前提排除）。常量参数是符号常量。
  - **浮点**按 IEEE 浮点理论，**覆盖 `f64`、`f32`、`f16`、`bf16` 四种**（8.1 的翻译那一条明列这四个）。今天的库里只有 `f64`/`f32` 的规则，`f16`/`bf16` 没有任何规则用到，所以它们可以排在标量覆盖的最后——但**不能悄悄不做**，因为 `change_precision` 这个许可就是 bf16 的（§1.4），S3 的许可到前提映射必须与它对齐。
  - **覆盖之外一律拒绝翻译**，并给出原因（不许猜、不许近似）。8.1 明列的三类覆盖不到是：**张量的整体性质**、**带区域的模式**、**调用外部函数的条件**（`docs/design/rewrite/v2.cn.md` 第 8.1 节的"覆盖不到的"那一条），所以这三类要走"`check` 兜底并在报告里标明"的路，也就是 `sampled` + 原因。
- **红前用例**：今天没有任何翻译器。用同一个探针文件（仓库外，内容见附录 A）读到 64 位那条规则被判"通过"：
  ```
  ok wide_mul_div: 200 samples agree, fired 8 of 8
  ```
  加一条计划断言 `refuted wide_mul_div`——今天红。
- **验收**：
  ```
  camel test/cases/modules/rules/verify_witness.cml std::nvm
  ```
  期望含 `refuted wide_mul_div: counterexample x = -9223372036854775808`（反例值必须是**真正**的反例：在 `int` 上 `x * 2 / 2` 回绕得 `0`，与 `x` 不等）。这是本片唯一能接受的形式——不是"报告里出现了 refuted 这个词"，而是**给出的值代入两侧确实不等**：计划里同时跑一次把该值喂进两侧的可执行对照（`test/cases/modules/rules/typed_boundary.cml` 的 `i32_mul_div(2147483647)` 就是同一形状的既有先例，只是它只做 32 位、只做执行不做判定）。
- **可并行**：与 S0、S3 的浮点部分并行（整数与浮点是两个翻译单元）。

### S2 第一个真正的判定：反例与证明

- **产物**：判定器接上线：把 S1 的命题交给求解器，`unsat` ⇒ `proved`，`sat` ⇒ `refuted` + 反例模型渲染成 Camel 值文本；`unknown`/超时 ⇒ `sampled` 且原因写明（**不许**当 `proved`，见 G16）。
- **红前用例**（三条，覆盖任务书要求的两类）：
  1. `x * 2 / 2 => x`（`int`）今天 `ok wide_mul_div: 200 samples agree, fired 8 of 8` → 要求 `refuted`。
  2. `x + 0 => x`（`int`，标准库的 `int_add_zero`）今天 `ok int_add_zero: 200 samples agree, fired 8 of 8` → 要求 `proved`。
  3. `x * 2 / 2 => x`（`i32`）今天 `FAIL narrow_mul_div: the two sides differ at (-2147483648): 0 and -2147483648` → 要求 `refuted` 且反例是同一个值。**注意这条今天"看起来对了"是巧合**：`pickInt` 的边界表里恰好有 `INT32_MIN`，而 64 位的两端不在表里；所以第 1 条与第 3 条必须一起验收，否则"能给出反例"会被 32 位的运气冒充。
- **验收**：
  ```
  camel test/cases/modules/rules/verify_witness.cml std::nvm
  ```
  期望含 `proved int_add_zero`、`refuted wide_mul_div`、`refuted narrow_mul_div` 三行（`int_add_zero` 那一条也可以由既有的 `test/cases/modules/rules/check_library.cml` 给出，它今天就在跑这条规则），且每条 `refuted` 带一个具体输入值。
- **可并行**：依赖 S1；与 S3、S4、S5 三路并行。

### S3 浮点标量 + 许可到前提的映射

- **产物**：
  - 许可 → 假设列表（§1.4 的表逐条落地）：`finite_math` 的四位 → "不是 NaN / 不是无穷 / 零的符号不敏感 / 输入与结果不是次正规数"；`change_precision` → 与 bf16 对应的相对误差界（复用 `RewriteErrorBound` 的 `relative`）；`nondeterministic` → 不可证，`sampled` 且原因 `no numeric bound`。
  - `reassociate`/`contract` → **实数模型**，命题种类 `real equality`（落到选项 A 还是 C，见问题文件 1 的选项 A/C）。
  - `refine_accuracy` → **误差界命题**，命题种类 `error bound`。
- **红前用例**（两条，覆盖许可的两种处境）：
  1. **库里的真形状**：`float_combine`（`#requires(reassociate)` **且带** `when combineSameSign(c1, c2)`）今天逐字 `ok float_combine: the condition selects the instances, fired 2 of 64`——一次值比较都没做。计划断言 `proved float_combine, class real equality`——今天红。
  2. **不带守卫的许可规则**（探针形状）：`#requires(reassociate)` 的 `x * c1 + x * c2 => x * (c1 + c2)` 不带 `when` 时今天逐字 `FAIL reassoc: the two sides differ at (-inf, 7, -1): -nan and -inf`（`reassociate` 不含 `no_infs`，所以样本里有 `-inf`）。按 8.1 它应当是"实数上相等"，今天却是 `FAIL`——**同一条规则在"有守卫"与"无守卫"下今天给出两种互不相干的报告**，这本身就是要把许可与 `when` 合成一张前提表（§1.4 末）的理由。计划断言这两条都得到 `proved …, class real equality`——今天两条都红。
- **验收**：
  ```
  camel test/cases/modules/rules/verify_licences.cml std::nvm
  ```
  期望含四行：`proved plain, class equality`（无许可，按位）、`proved reassoc, class real equality`、`sampled anywhere: reason no numeric bound`、以及 `refine_accuracy` 的一条 `proved …, class error bound`。
- **可并行**：与 S2 的整数侧并行（`modules/verify/translate_fp.cpp` 与 `licences.cpp` 两个文件，接口在 S1 定下）。

### S4 `when` 与 `fact` 作前提

- **产物**：`when` 的函数图（`GenericRuleInstance::when` / `RuleObject::when`）译成**假设**；报告写明"在 `when` 下证明"（今天的措辞是"the condition selects the instances"，不提证明）。`fact` 的结论：能译的当假设；译不了的把该规则降为 `sampled` 并写明是哪一项做不到（8.1："覆盖不到的用 `check` 兜底并在报告里标明"）。
- **红前用例**：`rule guarded(x: int, const c: int) { x * c => x } when c == 1` 今天逐字 `ok guarded: the condition selects the instances, fired 2 of 64`——完全没有证明路径。库里同形状的两条（`check_when.cml` 的 `double_int` / `double_float`）今天是 `ok double_int: the condition selects the instances, fired 64 of 64`。计划断言 `proved guarded under when`——今天红。
- **验收**：
  ```
  camel test/cases/modules/rules/check_when.cml std::nvm
  ```
  期望含 `proved guarded: under when, class equality`，且它引用的 `when` 文本出现在报告里（读者要能看见假设是什么）。
- **可并行**：与 S2、S3、S5 并行。

### S5 泛型规则逐实例证明（接 H232）

- **产物**：`check` 那一侧的桥。对每条泛型规则：按裁定的类型集合枚举类型元组，逐个用 `include/camel/compile/gir/rule_templates.h#genericRuleInstance` 取实例，对**实例自己的** `lhs`/`rhs`/`when` 分别判定；报告每行带元组（`proved add_zero<int>`、`proved add_zero<i32>`）。一个元组 `refuted` ⇒ 那一行 `refuted`，规则整体**不**报 `proved`。实例没做出来（`GenericRuleInstance::made == false`）⇒ 该元组 `sampled`，原因写 `GenericRuleInstance::why`。
- **红前用例**：泛型规则今天逐字 `unchecked generic_add_zero: a generic rule, whose instances are not checked here`。计划断言输出里同时有 `proved generic_add_zero<int>` 与 `proved generic_add_zero<i32>` 两行（`test/cases/modules/rules/generic_law.cml` 的 `add_zero::<T>` 就是这条规则的写法，它今天在改写路径上是绿的，见 `rules.generic_law_is_instantiated_per_type_tuple`）——今天红。
- **验收**：
  ```
  camel test/cases/modules/rules/verify_generic.cml std::nvm
  ```
  期望：`add_zero` 的每个整数元组一行；`add_minus_zero` 的浮点元组一行；一个**故意改坏的**泛型规则（如 `x * 2 / 2 => x` 的泛型写法）在 `int` 元组上 `refuted` 而在 `i32` 元组上 `refuted`，两行的反例值不同——这就是"每个实例分别证明"的可观测含义。
- **可并行**：与 S2、S3、S4 并行。

### S6 报告落地、69 条规则的标注、CI 收口

- **产物**：
  - `stdlib/rules.cml` 69 条规则的判定报告（生成物；`--check` 模式核实"69 条一条不少、`refuted` 计数为 0"）。张量 15 条标 `sampled` 且原因 `out of coverage`（不在第一步的翻译覆盖内，见交给作者的第二个问题）。
  - `test/plan-selection.json` 加一条规则（`modules/verify/` → 新的计划）。
  - CI：快速层的一条"无求解器时的诚实降级"断言（69 条全 `sampled`，原因 `solver unavailable`）；求解器那条走可选 job + 慢层（§4）。
- **红前用例**：今天没有这份报告。计划断言报告覆盖 `69/69`——今天红。
- **验收**：
  ```
  node scripts/test.js test/plans/feat/modules/verify.plan.toml --tier fast
  ```
  期望 `[SUMMARY] total: N, pass: N, fail: 0`、`RUN_EXIT=0`，且其中一条测试的 desc 说明"报告覆盖 `stdlib/rules.cml` 的每一条规则，`refuted` 计数为 0，未被翻译覆盖的规则标 `sampled` 并写明原因"。
- **可并行**：串在 S0–S5 之后。

**顺序图**：

```
S0 ─────────────────────────────┐
                                ├─► S6
S1 ──┬─► S2 ────────────────────┤
     ├─► S3 ────────────────────┤
     ├─► S4 ────────────────────┤
     └─► S5 ────────────────────┘
```

## 4. Z3 的构建接入（可选依赖）

### 4.1 构建开关

- 名字 `CAMEL_Z3_VERIFY`，默认 **OFF**，登记在 `cmake/options.json`（文件第二行写着 "options are overridden by npm run config"，所以 `npm run config` 能看见并覆盖它）与 `cmake/Options.cmake#add_bool_option`（它已经把 `NAME=0|1` 写进编译定义，验证器因此能在编译期知道自己有没有求解器）。
- 查找用 `find_package(Z3 CONFIG QUIET)`，与 `CAMEL_TENSOR_BLAS` 对 `find_package(BLAS)` 的做法同型：那条选项的说明写着 "without a findable CBLAS the builtin GEMM is the library path and the build is unchanged"，运行期还有一行 `message(STATUS "CAMEL_TENSOR_BLAS=${CAMEL_TENSOR_BLAS}")`。**形状照抄，默认值相反**：CBLAS 缺了是回到内建 GEMM（行为等价），Z3 缺了是判定从"证明"掉到"采样"（行为不等价），所以 Z3 的默认必须是 OFF 且缺了要**说出来**，不能像 CBLAS 那样默默换实现。
- 不把 Z3 塞进 `conanfile.txt` 作硬依赖：那会让每个腿（三个常驻 + macOS + sanitizers）都去拉求解器，而 8.1 说的是"只在验证时需要，不进 `camel` 的运行时"。取用方式二选一，由施工方按平台实测选定：包管理器（conan/系统包）或 pip 轮子（CI 上是 `pip install z3-solver`，主机上是 `~/camel-eval/venv-*/bin/pip`）。

### 4.2 缺失时的行为：如实降级，不是静默跳过

三件事必须同时成立，缺一条就是"谎报"：

1. **配置期**打印一行，照 `cmake/Options.cmake#add_platform_option` 的平台提示体例：`CAMEL_Z3_VERIFY=OFF (Z3 not found)`。
2. **编译期**把 `CAMEL_Z3_VERIFY=0` 写进目标，验证层编成只有采样的版本；产物里没有任何求解器符号（`libcamel.so`、`camel.exe`、`out/latest/libs/` 都不链接 Z3）。
3. **运行期**报告里每条规则 `sampled`，原因写 `solver unavailable`，并回显构建开关（如 `evidence: none (CAMEL_Z3_VERIFY=OFF)`）。**不是**不输出、**不是**输出 `proved`。

判定为三种结果之外还有第四种必须处置：求解器返回 `unknown` 或超时 ⇒ `sampled`，原因写 `solver unknown` / `solver timeout`（G16）。这条今天没有先例，所以它要有一条自己的计划测试（喂一个故意超大的命题，断言得到 `sampled` 而不是 `proved`）。

### 4.3 CI

- **快速层不依赖求解器**：`.github/workflows/ci-leg.yml` 的 "Test (smoke + feat)"（`node scripts/test.js`）保持原样；新计划里"无 Z3 时 69 条全 `sampled` 且原因正确"那一条是**快速**测试，它守的正是"不许静默跳过"。
- **求解器的一条走可选 job**：照 `.github/workflows/ci.yml` 里 `sanitizers` 与 `macos` 的 `workflow_dispatch` 输入体例加一个 `verify-z3`（装 Z3、`-DCAMEL_Z3_VERIFY=ON`、构建），然后**按计划文件**跑：
  ```
  node scripts/test.js test/plans/feat/modules/verify_solver.plan.toml
  ```
  命名一个计划**文件**时它会全跑，与 `--tier` 无关——这正是"慢层留在批次点"要的形式。
- **慢层的归属**：求解器证全库 69 条是分钟级。按 `docs/test-plans.md` 的层规则标 `tier = "milestone"`，并在 `desc` 里写明时间为什么是固有的（"each rule is a solver query over its own two sides, and the library is 69 rules"）；它在里程碑层跑，不进合并闸门与发布闸门的慢层。
- **本地**：`CAMEL_Z3_VERIFY=OFF` 是默认，开发者循环不受影响；要跑证明的开发者用同一个计划文件。

## 5. 不做什么、风险

**不做**：

1. **不做张量规则的翻译**（第一步按任务书只做标量）；15 条张量规则标 `sampled` 并写原因。`f16`/`bf16` 的宽度**要**在覆盖集里留位（8.1 明列），但今天没有规则用到它们，所以实现在 S3 的最后而不是第一步。
2. **不做带区域的模式**（分支、循环上的模式）与**调用外部函数的条件**——8.1 自己就把它们列进"覆盖不到"。
3. **不改语义让验证通过**。`refuted` 的规则按 8.1 **不得入库**，这条是闸门；本计划只提供判定，不提供豁免。
4. **不含声明面的更名**。第 7.5 节的 `fact` 更名由另一条工作完成；本计划只在 `fact` 的前提这一项上留出接口（S4），并明确 S4 的验收在今天的 `fact` 形态下也能跑。
5. **不让求解器进 `camel` 的运行时**，也不因为验证就需要新的遍参数。

**风险**：

1. **`proved` 的可信度取决于翻译器是否忠实。** 一条恒真的验证器可以通过全部验收。必须有一组**故意的坏翻译**用例（把 `bvmul` 换成 `bvadd`、把 `bvsdiv` 换成无符号除法、把回绕换成饱和），断言它们各自被现有测试**抓红**。没有这一组，"能给出反例"这句话就没有分量。这是本计划里最容易漏掉的一步，单列在 S2 的验收里。
2. **实数模型与误差界的命题措辞**（问题文件 1 的问题 1 与问题 2）。在作者裁定前，S3 先按"报告里必须有命题种类一列"落地，判定本身按选项 A 的读法实现，并在计划测试里把命题种类写进期望字符串——这样裁定一变，改的是期望字符串而不是判定器。
3. **多平台的 Z3 可得性**。Windows 上（`windows-2022` 腿）与主机上（Ubuntu 22.04、校园网认证明文代理、访问不了 pypi）取用方式不同；`CAMEL_TENSOR_BLAS` 的先例是"找不到就照常构建"，所以最坏情形是可接受的，但**"CI 里跑一次 ON"这件事必须先在主机上实测一次**（本计划没有做，因为要装依赖，属于施工）。
4. **69 条规则的判定时间**。整数一侧多数是位向量的恒等式，求解很快；浮点一侧（尤其 `refine_accuracy` 的超越函数）可能慢到需要超时。所以"超时 ⇒ `sampled`"这条不是可选项，是让 S6 能跑完的前提。
5. **`check` 的输出被 8 条既有计划测试用字符串钉死**（§1.9）。S0 改词汇会同时改这 8 条的期望；这是**计划内**的改动，不是回归，但提交时要一起改并在提交信息里说明。

## 6. 作者问题（编号，正文在两个问题文件里）

1. **许可到前提的三种读法**：`reassociate`/`contract` 证"实数上相等"还是"按位相等"；`refine_accuracy` 的误差界成真时叫不叫 `proved`；`nondeterministic` 与今天的第四个字 `unchecked` 怎么合并。这留给作者。
2. **判定落在哪个面上**：任务书说"`stdlib/rules.cml` 每条规则都有标注"，标注写进规则语言、写成生成物、还是只在运行时报告里出现；`CheckReport` 每行带什么；覆盖不到的规则标什么、CI 要不要把 `refuted` 当硬失败。这留给作者。
3. **`approx_math` 作为许可的前提是什么**：`src/core/operator_semantics.cpp#rewriteErrorBound` 里没有这个位，8.1 也没说。今天的处置是"它不影响容差"，所以带 `#requires(approx_math)` 的规则按 `kNoPermissions` 的按位比较路径处理——这是不是作者要的，需要确认。出处：`include/camel/core/operator_semantics.h#Permission` 有这个位、`src/core/operator_semantics.cpp#rewriteErrorBound` 没有这个分支、`src/core/operator_semantics.cpp#parsePermission` 认这个名字。**本计划不定下，按今天的读法落地并在报告里写明。**
4. **一个 `refuted` 的规则应当怎样退出标准库**：8.1 说"不得入库"，但今天的库是 69 条手写的规则，没有自动化的入/出库流程。是加一个 `refuted ⇒ 移除该规则` 的闸门，还是只让 CI 红、由人来改？出处：8.1 的报告那一条；`test/plan-selection.json` 把 `stdlib/rules.cml` 映到 5 个计划。

## 附录 A：测量命令与逐字输出

计划形式（主机 `88zjwei`，工作树 `D:\Projects\Camel-wt\h397-h247-plan`）：

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h397-h247-plan -Plan test/plans/feat/modules/rules.plan.toml -Tier fast -Jobs 8
```

尾行（`18c98b856`，2026-10-24）：

```
[SUMMARY] total: 121, pass: 121, fail: 0
[SPLIT] build 51.0 s, test 4.0 s, total 55.0 s
RUN_EXIT=0
```

仓库外探针（不进仓库、不改任何树；主机上用已构建的 `out/latest`）：

<!-- camel-snippet: fragment: H247 的仓库外探针（跑在 /tmp，逐字记录当时的判定输出；它演示的是"今天"的判定面，不是页面示例） -->
```camel
// /tmp/h247_probe.cml
module h247_probe
import { check } from rewrite
import { is_integer } from types

rule generic_add_zero::<T>(x: T) { x + 0 => x } when is_integer::<T>()
rule wide_mul_div(x: int) { x * 2 / 2 => x }
rule narrow_mul_div(x: i32) { x * 2 / 2 => x }

func main(): int sync {
    println(check([generic_add_zero], 200, 7))
    println(check([wide_mul_div], 200, 7))
    println(check([narrow_mul_div], 200, 7))
    return 0
}
```

<!-- camel-snippet: fragment: H247 的仓库外探针（跑在 /tmp，逐字记录当时的判定输出；它演示的是"今天"的判定面，不是页面示例） -->
```camel
// /tmp/h247_probe2.cml（许可面）
module h247_probe2
import { check } from rewrite
import { Tensor } from tensor

rule plain(x: float) { x * 1.0 => x }
#requires(finite_math)
rule finite(x: float, const c1: float, const c2: float) { x * c1 + x * c2 => x * (c1 + c2) }
#requires(reassociate)
rule reassoc(x: float, const c1: float, const c2: float) { x * c1 + x * c2 => x * (c1 + c2) }
#requires(refine_accuracy)
rule accurate(x: float) { x + 1.0 => x }
#requires(nondeterministic)
rule anywhere(x: float, const c: float) { x * c => c * x }
rule guarded(x: int, const c: int) { x * c => x } when c == 1
rule tensor_law(x: Tensor<f32>) { x + x => x * 2.0 }

func main(): int sync {
    println(check([plain, finite, reassoc, accurate, anywhere, guarded, tensor_law], 100, 7))
    return 0
}
```

两条探针的逐字输出已在 §1.1 与 §1.2 引出（`PROBE_EXIT=0`）。运行方式（主机，`LD_LIBRARY_PATH` 指向 conda clang 环境的 `libstdc++`，因为构建用 clang 23）：

```
export CAMEL_HOME=<worktree>/out/latest
export LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib:$CAMEL_HOME/libs
export PATH=$CAMEL_HOME/bin:$PATH
camel /tmp/h247_probe.cml std::nvm
```

计划条目的引用（今天全绿，说明现状基线）：

```powershell
git grep -n "check" -- test/plans/feat/modules/rules.plan.toml
```
