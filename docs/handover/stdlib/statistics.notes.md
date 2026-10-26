# `statistics` 重写说明

草稿是 `docs/handover/stdlib/statistics.cml`。**未编译、未运行**。写法上的不确定点同 `array.notes.md` §7（lambda 形参推断、泛型体里 `T(...)`、`xs->quantile<0.5>`）。

## 1. 总体决定

原来是 C++ 模块（`modules/statistics`）加一份纯声明的 `.cml`，五个函数，**一律答 `float`**，接受 `i32/int/f32/float`。新的是**纯 Camel 模块**，建在 `array` 的 `sort`、`sum` 上，**函数答元素类型**，只接受浮点元素类型：

- 设计原则 10.4：统计属于“其余一切用 Camel 定义”，不是三种原子情形之一；写成 Camel 后 `mean` 与规范式 `T(sum(float(x)) / float(numel(x)))` 逐字对应（`questions-2026-10-24-evening.md`“whole-reduction-element-type-residual-reds”），“在 f64 里算、舍入一次”写在明处，不再是 C++ 注释里的承诺。
- 设计原则 7.3：浮点归约返回元素类型。`f32` 数组的 `mean` 答 `f32`。
- 设计原则 5.1：不做隐式转换，整数数组在这里不被悄悄当成浮点——`when is_float::<T>()`，调用处写 `xs->map<(x) => float(x)>`（见 Q1）。
- 约定 6.5：不用哨兵。空数组的 `mean` 原来答 `0`，样本标准差/方差在少于两个元素时原来答 `0`，都是哨兵；现在答 `T?` 的 `null`（`median`、`quantile` 本来就是 `float?`）。

## 2. 函数一览

| 名字 | 处理 | 一句话理由 |
|---|---|---|
| `mean` | 保留；答 `T?` | 同上；定义 = 规范式 |
| `variance` | 保留；答 `T?`，n < 2 为 null | 样本方差 n - 1 在 n = 1 时无定义 |
| `stdev` | 保留；答 `T?` | 由同一个私有 `variance_of` 在 f64 里开方后再舍入一次（不是 `sqrt(variance(xs))`：那会舍入两次） |
| `median` | 保留；一行 `xs->quantile<0.5>` | 原来是独立的 C++ 算子；0.5 分位的线性内插对偶数个元素正是两个中间值的平均 |
| `quantile<q>` | 保留；答 `T?` | 内插式与旧 C++ 一致（`q*(n-1)`，“type 7”），但 `f == 0` 时直接取 `sorted[lo]`，不再算 `sorted[lo]*1 + sorted[hi]*0`——后者在 `sorted[hi]` 是无穷时得 NaN，旧实现有这个缺陷 |
| 私有 `wide` `variance_of` `quantile_of` | 新增 | 把“先加宽到 f64”写成一处 |

没有新增或删除公开名字。每个公开函数是 3 行以内。

## 3. 需要的改动

C++（整个模块删除）：
- `modules/statistics/`（`module.cpp`、`operators.cpp`、`executor.*`、`CMakeLists.txt`）；根 `CMakeLists.txt:256` 的 `add_subdirectory(modules/statistics)`。
- `src/passes/trans/cpp/operator_rules.cpp:474-475`（`statistics:mean`、`statistics:stdev` 的 `runtimeFunction("statisticsMean"/"statisticsStdev")`）和运行时里对应的两个函数。写成 Camel 之后 `std::cpp` 把它们当普通函数转译，不需要特规则（请施工方确认 `std::cpp` 对 `sort`、`sum` 的支持；`array` 的原生算子目前不在 `operator_rules.cpp` 里）。

脚本与计划：
- `scripts/stdlib-reference.mjs:161` 的 `statistics` 条目改 `kind: 'camel'`（去掉 `source`/`declaration`）；`scripts/stdlib-declarations.mjs:21` 注释里的 `statistics` 去掉。
- `test/plans/feat/modules/stdlib_registry.plan.toml:23` 的模块清单仍含 `statistics`，`:39` 的纯度表里 `statistics:median` 删掉（Camel 函数的纯度由推断给出）；`test/plan-selection.json` 里指向 `modules/statistics` 的规则改指 `stdlib/statistics.cml`。
- `test/plans/feat/compile/native_declarations.plan.toml` 里三条把 `statistics` 当“原生声明样本”的用例（`test/cases/compile/native/statistics_arity.cml`、`statistics_declaration_import.cml`、`statistics_type.cml`，及 `:358` 附近的 desc）：测的是声明机制，换到另一个仍有无体声明的模块，不留 `statistics`。

## 4. 行为变化（要写进用例）

| 旧 | 新 |
|---|---|
| `mean([1, 2, 3, 4])` → `2.5`（`float`） | 整数数组被拒（`when is_float::<T>()` 不满足，诊断列出条件）；写 `[1.0, 2.0, 3.0, 4.0]->mean ?? 0.0`，或 `xs->map<(x) => float(x)>->mean` |
| `mean([])` → `0` | `null` |
| `stdev([5.0])`、`variance([5.0])` → `0` | `null` |
| `f32[]` 的 `mean` → `float` | `f32?` |
| `quantile` 的 q 越界 → `InvalidArgument` | `IndexOutOfRange`（见 Q2） |
| `sorted[hi]` 是无穷、`f == 0` 时 NaN | 正确的无穷 |

## 5. 调用点迁移

在导入了 `statistics` 的 23 个文件里：`mean` 14 个文件 / 19 行，`stdev` 9 / 10，`variance` 3 / 5，`median` 4 / 9，`quantile` 4 / 8。每一处都要加 `?? 默认值`（或 `match`），整数数组的要先转浮点。

- 库内：`stdlib/perf.cml:8,31,32`（`mean(results) * 1000.0`、`stdev(results) * 1000.0`，`results` 若是 `float[]`，改成 `(mean(results) ?? 0.0) * 1000.0`；基准样本为空本来是调用方错误，所以此处用 `??` 取 0 不算哨兵扩散，仍请施工方按语义定）。
- 用例：`test/cases/modules/std/statistics/basic.cml`、`test/cases/modules/stdlib/stats_quantiles.cml`（含整数字面量数组，如 `variance([1, 2, 3, 4])`、`median([3, 1, 2])`，要改成浮点字面量；空数组与单元素的期望从 `0` 改成 `null`）、`fault_quantile_range.cml`（故障种类 `IndexOutOfRange`）、`registry_all.cml`、`test/cases/para/core/bootstrap_medians.cml`（`mean(medians)` 是 `int[]`，要先转浮点）、`test/cases/trans/cpp/exec/draws.cml`（含 `mean(none)` 空数组与 `stdev([1])`，期望变）。
- 示例：`examples/numeric/random_walk.cml`、`monte_carlo_pi.cml`；两个 README 的函数表。
- 文档：`docs/tutorial/examples/12_stdlib.cml` 与 `tutorial.cn.md`/`tutorial.en.md` 的统计段；`docs/language/05_modules.md`（及英文）、`docs/cli.md`、`docs/design/stdlib/README.cn.md` 里的 `statistics` 表述；`docs/stdlib-reference.md`、`docs/reference/camel-for-ai-stdlib-signatures.md` 由脚本重生。
- 工具脚本：`test/tools/cppexec/run.py`、`facts-cost.py`、`gcbench/gcbench.py`、`jitcov/bench.py`、`perfbaseline/ab.py`、`examples/models/naive-in-expert-out/thread_scale.py` 提到 `statistics` 的地方逐个核（多数是 Python 的 `statistics` 模块，不用改）。

## 6. 与其他模块的重叠

| 重叠 | 建议 |
|---|---|
| `tensor::mean::<T>(t)`（整体，C++）、`tensor::mean(t, axis)`（轴）、`tensor_definitions.cml` 的 `mean` | 这里的 `mean` 与规范式同形。整体形与 `statistics::mean` 重叠，归属见 `array.notes.md` Q1；轴形式留 tensor |
| `nn` 里的 batch norm 等用到方差 | 它们是逐轴的张量运算，不重叠 |
| `array::sum` | `statistics` 依赖它（`sum(w)` 在 f64 上就是 `float` 的 f64 累加，一次舍入对 f64 恒等），不再复制累加循环 |
| 模块名 | 约定 6 的分层表写的是 `stats`，现在叫 `statistics`；推荐保留 `statistics`（已有教程与用例），表里改名即可 |

## 7. 开放问题（带推荐）

**Q1. 整数数组。** 推荐：拒绝（现稿）。理由：(a) 设计原则 5.1 对整数同样不做隐式转换；(b) 7.3 的“归约答元素类型”对整数没有合理答案（整数的 `mean` 要么截断要么变类型）；(c) 调用方写出 `map<(x) => float(x)>` 是一次显式的、看得见的决定。代价：整数数组的统计多一步，且没有“数组整体转元素类型”的短写法（`tensor::cast` 用字符串名，不是 Camel 的风格）。若你认为代价太大，备选是给整数数组另加一组答 `float?` 的声明，但那会让同名函数按元素类型有不同的结果类型，我不推荐。数组的元素类型转换缺一个正式的函数（`to::<U>`？），不在我的模块里，请定。

**Q2. 空与不足的答案 `T?` 还是故障，以及 q 越界的故障种类。**
- 空/不足答 `null`：与 `array::min/max`、`median/quantile` 一致；`tensor` 的整体 `mean` 对空张量是 `DivisionByZero` 故障。若要“各处同一条规则”（用户的规则统一），二者需要统一，推荐统一成 `T?` 用于列表形函数，张量保留故障并在 7.7 写明区别（见 `array.notes.md` Q2）。
- q 越界：Camel 里没有“主动抛出指定种类的故障”的手段，草稿靠下标越界自然产生 `IndexOutOfRange`，且 `n == 1` 或数据含 NaN 时不会故障（返回那个值/NaN）。推荐接受（文档如实写）；若要 `InvalidArgument`，需要语言给一个断言式的构造，我不推荐为一个函数加。

**Q3. 少于两个元素的 `variance`/`stdev` 答 `null`。** 旧答 `0`。推荐 `null`（n - 1 = 0 的除法没有意义，0 是错误答案）。

**Q4. 性能。** `wide` 把数组整体复制成 `float[]`，`quantile` 再排序一次，`variance` 复制两次。统计函数不在热路径上；按 10.4 等测量后再加内核。当前草稿**没有**任何原生内核。

---

# 第二轮修订（决策者批复后）

以下**取代**上文冲突部分（§2 中的 `T?`、§4、§5 中的 `?? 默认值`、§7 的 Q1–Q4）。

- `mean` 移到 `array`（`T[..]`），`statistics` 现有 `variance`、`stdev`、`median`、`quantile` 四个函数，都答 `T`，不再答 `T?`。
- 不足的情形都是故障 `InvalidArgument`（7.7）：`variance`/`stdev` 少于两个元素；`median`/`quantile` 空数组；`quantile` 的 q 不在 [0, 1]（旧 C++ 的 `InvalidArgument` 保留）。`fail` 的使用位置见 `array.notes.md` R2。`median`、`quantile` 含 NaN 仍答 NaN。
- 整数数组仍被拒绝。修法写在模块头：`cast(xs, 'f64')`（`tensor` 的 `cast`，`'f64'` 与 `'float'` 同义，见 `modules/tensor/ops/elementwise_ops.cpp`；数组元素类型转换目前就是它的字符串拼写，不是 Camel 风格，等 tensor 重写时换成 `T` 类型实参的形式）。
- 调用点：去掉所有 `?? 默认值`；`mean` 改从 `array` 导入（`import { mean } from array`），其余仍从 `statistics`。`stdlib/perf.cml:31,32` 写回 `mean(results) * 1000.0`，空样本即故障。
- 行为变化表：空数组 `mean` 和不足两个元素的 `variance`/`stdev` 由 `0` 变成故障；`quantile` 越界仍是 `InvalidArgument`；其余（整数被拒、结果类型为元素类型）同上文。
- `tensor` 的整体 `mean` 删除后，规范式 `mean::<T>(x: T[..]): T = T(sum(float(x)) / float(numel(x)))` 的唯一来源是 `array::mean`。
