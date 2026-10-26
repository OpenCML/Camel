# `array` 重写说明

草稿是 `docs/handover/stdlib/array.cml`。**全部未编译、未运行**（本机无工具链），下面“待验证的写法”一节列出我没把握的语法点，请施工方第一轮先编译这几处。

## 1. 函数一览

| 名字 | 处理 | 一句话理由 |
|---|---|---|
| `argsort` | 保留；声明由 6 条增为 8 条（加 `bf16`、`f16`） | 排序的唯一原子（设计原则 10.4：排序是内存与布局内核）。有了 `bf16`/`f16` 的两条，`is_ordered` 的每个类型都有 `argsort`，`sort` 才能一个定义覆盖所有可排序类型 |
| `sort` | 保留；改为 Camel 定义 `xs[argsort(xs)]` + `#impl(sort_kernel)` | 照 `questions-2026-10-23.md` “sorts-per-element-type” 第 2 条：函数体就是排序的语义，`when is_ordered`；`i32/int/f32/float` 走内核（保留 `xs = xs->sort` 原地形），其余类型（含 `string`、`bool`、`bf16`、`f16`）走函数体 |
| `insert_sort` `quick_sort` `merge_sort` | **删除** | 算法名不是语义：三者与 `sort` 结果逐位相同（见 §4 零的次序），只差速度，用户不该选；“一个概念一个名字”（约定 3）。C++ 里只留一个排序算法 |
| `merge_sorted_arrays` | **删除** | `sort(concat(a, b))` 同义；线性归并是一屏 Camel（`examples/algorithms/merge_sort.cml` 已自己写了）。不值得做原子；要做内核，也得先有 Camel 定义，而归并的 NaN/±0 次序在泛型里很啰嗦（见 Q4） |
| `reverse` | 保留 | `xs[::-1]`，已是一行 |
| `take<n>` `drop<n>` | 保留 | 按定义截断，与 `xs[:n]` 越界即故障互补 |
| `enumerate` | 保留 | |
| `flatten` | 保留；**原生算子删除**，改 `loop … do flat->extend<xs>` | 不属于原子的三种情形；`extend` 在循环状态上计数为 1 时原地追加（设计原则 3.10、10.5），总量线性 |
| `unique` | 保留；**原生算子删除**，改 `kept->contains<x>` 的循环 | 同上。任意可比较的 `T` 一个定义；代价是对不同值的个数二次，文档写明（长数组先 `sort`）。若测量表明太慢再按 10.4 配 `#impl` 内核 |
| `find_index<pred>` | **新增** | `find`、`index_of`、`any`、`all` 原先各写一份“带提前结束的下标循环”；抽出这一个，其余四个各一行。它本身也是常用函数 |
| `find<pred>` | 保留，改写在 `find_index` 之上 | |
| `index_of<x>` | 保留，改写在 `find_index` 之上 | `==` 语义不变（NaN 找不到） |
| `any<pred>` `all<pred>` | 保留，各一行 | |
| `count<pred>` | 保留 | 顺序循环，不经 `filter`（`filter` 无序、要分配） |
| `sum` | 保留；**四个分臂（int/i32/c64/c128 + 四个浮点）合成一个函数体**，两臂：浮点（f64 累加、舍入一次）与其余（自身类型累加、回绕） | 约定 6.5“一个定义覆盖所有类型”；`sumF64` 与四个浮点臂删除 |
| `min` `max` | 保留，仍 `T?` | 见 Q3 |
| `sort_by<key>` | 保留，加 `when is_ordered::<K>()` | 键类型不可排序时在调用处报“没有适用的重载”，不等到 `argsort` |
| `flat_map<f>` `zip_with<f>` | 保留 | |
| `is_elem_i32/int/f32/f64/bf16/f16` 六个谓词 | **删除**，由 `types::is_same::<T, U>()` 取代 | 见 §3 |
| `bf16/f16/bool/string` 的 `sum`/`min`/`max` 内核（共 18 条声明与执行器） | **删除** | 这些类型少用，走函数体即可；内核只留四个热类型（10.4：其余在测量表明更慢时再配） |
| `module array` 行、所有 `/// @pure` | 删除 | 按任务书 |

### 回调是否要求 `pure`

**一个都不要求。** 理由：本模块里回调的调用要么按元素顺序、遇到答案即停（`find_index`、`find`、`index_of`、`any`、`all`、`count`，都是 `loop`，顺序由状态依赖决定），要么经 `map`（`sort_by`、`flat_map`、`zip_with`），而 `map` 的语义早已写明“元素之间无序、元素函数里的效应由使用者负责”（设计原则 3.6）。库没有任何一处对回调“调用次数不同于元素个数”或“推测调用”，所以不需要纯度才能保证结果。旧文件头那句“传入的函数必须是纯的”删掉——它限制了用户（例如 `any` 里打日志），却没有语义上的需要。

### 参数传法

全部符合“配置在尖括号、数据在圆括号、`->` 只传一个值”：`xs->take<3>`、`xs->sort_by<key>`、`xs->find<pred>`。唯一两个数据地位对等的是 `zip_with<f>(xs, ys)`，两个都是普通参数（设计原则 11.4）。

## 2. 需要的 C++ 改动

`src/builtin/operators/array.cpp`、`array_reduce.cpp`：
1. 删 `__array_flatten__`、`__array_unique__`，以及 `getArrayOpsMap` / `declareArrayOperatorFacts` 里 `flatten`、`unique` 两项。
2. 删 insertion/quick/merge 三套排序（`SortAlgo`、`insertionSortSlots`、`quickSortSlots`、merge 一份、`kSortAlgorithms`、`sortFormName` 的算法维度）和 `merge_sorted_arrays_*`（4 个执行器）。只留一个算法，名字 `sort_<t>` 与 `sort_<t>_in_place`（`<t>` ∈ i,l,f,d），原地形注册不变。
3. 新增 `argsort_bf`、`argsort_f6` 两个执行器（`kArgsortTypes` 加两行；比较用 `halfToFloat`，与 `min_bf` 同路）。
4. 比较器 `numberLess`（argsort 与 sort 共用）加零的符号规则：两者都是零时 `-0 < +0`；NaN 仍全部在数之后、彼此等价。这样“排序结果与算法无关、`sort` 与 `xs[argsort(xs)]` 逐位相同”才成立（见 Q5）。
5. `array_reduce.cpp` 删 `sum_bf`、`sum_f6`、`min_{bf,f6,b,s}`、`max_{bf,f6,b,s}`（两处注册 + 纯度声明）。
6. `src/passes/sched/taskflow/taskflow.h:508` 注释里的 `array:sort` 无需改。

`stdlib/types.cml`：加 `is_same::<T, U>(): bool`（`match type T { case U => true; case _ => false }`，设计原则 5.5 允许 `case T` 写类型变量）。本文不改它，建议给 `types` 的负责人。

`scripts/stdlib-reference.mjs` 的 `array` 条目注释里“(the sorts, argsort, flatten, unique)”改成“(argsort and the kernels of sort, sum, min and max)”；`stdlib_registry.plan.toml` 的算子清单（`array:` 前缀）同步。

## 3. 内核的写法

旧文件用六个 `is_elem_*` 谓词让每条内核声明的 `when` 恰好命中一个类型。草稿改成一个通用的 `is_same::<T, i32>()`，`sum`/`min`/`max` 的内核声明由 22 条降到 12 条，每族只有四个热类型；排序由“三套算法 × 四类型共 12 条加 `sort` 4 条声明”变成 4 条内核。`#impl(f)` 标在 Camel 定义上，内核是本模块私有无体声明，沿用旧约定。

## 4. 行为变化（要写进用例）

- `sort` / `sort_by` 现在接受 `string`、`bool`、`bf16`、`f16`（旧：调用处拒绝）。`test/cases/modules/stdlib/reduced_sort_refused.cml` 与 `array_sort_no_declaration.cml` 原来钉“拒绝”，要改成钉“可排序”，并新增对 `c64` 的拒绝（`when` 不满足）。
- 浮点排序次序：`-0` 在 `+0` 之前，NaN 在最后（与 `min`/`max` 同一规则，7.7）。
- `sum`/`min`/`max` 的结果不变；`bf16`/`f16`/`bool`/`string` 现在走函数体，用例 `array_reduction_kernels.cml` 里这些类型的“内核对函数体”比较随之删去。

## 5. 调用点迁移

数量是在“导入了 `array` 的文件”里按名字数的行数，上界估计（`find`、`count` 可能混入 `str` 的同名函数）。

| 名字 | 代码 | 文档 |
|---|---|---|
| `insert_sort` | 4 个文件 / 11 行 | 1 |
| `quick_sort` | 3 / 10 | 1 |
| `merge_sort` | 7 / 14 | 1 |
| `merge_sorted_arrays` | 8 / 12 | 1 |
| 其余名字 | 无调用方式变化（`sort` 20/47、`sum` 22/60、`take` 11/21、`find` 7/12、`count` 8/19、`sort_by` 8/19、`min` 6/32、`max` 8/33、`flat_map` 10/13 …） | |

必须改的文件：
- 三个排序变体：`test/cases/modules/std/array/every_operator.cml`、`sort_basic.cml`、`sort_reference.py`；`test/cases/modules/stdlib/array_sort_kernels.cml` 与 `test/plans/feat/modules/array_sort_kernels.plan.toml`（整个用例钉的是三套算法的内核，删掉或改钉 `sort` 内核与函数体一致）；`test/cases/consistency/new_values/sort_rebinding.cml`；`examples/README.md`、`README.cn.md`；`scripts/perf-gate.mjs`；`test/plans/feat/examples.plan.toml`、`std.plan.toml`。
- `merge_sorted_arrays`：`test/cases/para/core/merge_sort.cml`、`merge_sort_small.cml`、`concurrent_alloc.cml`（并行分治排序的负载）。它们要自带一个归并（同 `examples/algorithms/merge_sort.cml`），性能基线要重录。
- 声明机制用例（把 array 当“原生声明”的样本，声明变了）：`test/cases/compile/native/array_arity.cml`、`array_sort_arity.cml`、`array_sort_type.cml`、`array_sum_kernel_overlap.cml`、`array_type.cml`、`array_declaration_import.cml`、`array_sort_kernel_overlap.cml`；`native_declarations.plan.toml`、`resources.plan.toml`、`array_reduction_kernels.plan.toml`、`stdlib_registry.plan.toml`。这些测的是声明机制，应换到另一个仍有无体声明的模块（例如 `argsort` 保留的几条）。
- `reduced_*`：`reduced_sort_refused.cml`（见 §4）、`reduced_sum_refused.cml`、`array_reduction_arms.cml`（分臂不再存在）。
- golden：`test/plans/feat/consistency/golden/attempt_library.txt:14` 写着 `InvalidArgument at array.cml:300:12`，是 `zip_with` 里 `zip` 的位置；新文件里行号变了，重录。旧文件“内核声明必须放在文件末尾，否则故障行号会动”的那条限制不再需要说明，但新文件把内核仍放在末尾。
- 文档：`docs/language/03_data_model.md:122`（及英文）的函数清单加 `find_index`；`docs/stdlib-reference.md`、`docs/reference/camel-for-ai-stdlib-signatures.md` 由 `scripts/stdlib-reference.mjs` 重新生成；`docs/design/arrays-memory/plan.cn.md`、`docs/technical/06_runtime_and_execution.md`、`09_operator_registration.md` 里提到旧排序名的段落。
- 唯一的库内调用方 `stdlib/opt.cml:23`（`flatten, all, any`）签名不变，无需改。

## 6. 与其他模块的重叠

| 重叠 | 现状 | 建议归属 |
|---|---|---|
| `tensor::take(t, indices)` | 与核心下标 `xs[idx]`（即 take，设计原则 5.4）同义，又与 `array::take<n>`（取前 n 个）同名异义 | 删除 `tensor::take`；`array::take` 留在 array |
| `tensor::flatten(t, axis?)` | 是重塑，`array::flatten` 是拼接内层数组，同名异义，两边都按名字导入会冲突 | `tensor::flatten` 改名（建议并入 `reshape`，或叫 `collapse`），`array::flatten` 留 |
| `tensor::sum::<T>(t)` 与整体 `mean::<T>` | 与 `array::sum`、`statistics::mean` 同名同义（整体归约），只差秩 | 见 Q1 |
| `tensor::max/min/argmax/argmin(t, axis, keepdims)` | 轴形式，与 array 的整体 `max/min` 是“不同算子、由导入决定”（约定 4 的参数分类），不冲突 | 留 tensor |
| `math::min/max/clamp` | 标量二元；`array::min/max` 的函数体直接调用 `math::min/max`，NaN 与 ±0 规则只写一处 | 留 math |
| 核心 `apply<f>`（`globals.cml`：“`f` 保持元素类型的 `map`”） | 与 `map` 完全重叠 | 建议核心删除（不在本文范围） |
| `tensor_definitions.cml` 的 `mean` | 规范式 `T(sum(float(x)) / float(numel(x)))` 与 `statistics::mean` 同形 | 两处一致，不需合并 |

## 7. 待验证的写法（编译时请先看这几处）

1. `match type T { case _ when is_float::<T>() => … }`：守卫写在 `case _` 上（设计原则 4.4 说 `case p when 条件`，没有 `match type` 里 `_` 的例子）。不通过则退回 `case float | f32 | bf16 | f16 =>`。
2. `loop flat = [] as T[] for … do …`：循环状态初值用 `[] as T[]` 给空数组取型。
3. `when is_same::<T, i32>()` 写在无体内核声明上，且 `is_same` 两个类型实参按位置给。
4. `#impl(sort_kernel)` 采纳内核后，`xs = xs->sort` 的原地改写（`Builder::inPlaceRebindingOf`）要经 `OperatorTable::inPlaceForm` 找到 `array:sort_i_in_place`；归约走过这条路，排序还没有。
5. lambda 形参类型省略：`(x) => x > 4`、`(y) => y == x`、`(acc, x) => acc + x` 全按 5.9 由期望的函数类型推出；`find_index` 里 `T` 要先由接收者定下。
6. `xs->find_index<pred> != null` 里 `->` 与 `!=` 的优先级（按 6.12 应该是 `->` 更紧）。
7. 在泛型体里 `T(0)`、`T(...)` 把类型变量当转换函数用（设计原则里规范式 `mean` 就这样写）。
8. 例子里 `[3, 9, 2]->min ?? 0`。

## 8. 开放问题（带我的推荐）

**Q1. 整体归约的秩。** 任务书写 `sum(f32[..]): f32`。草稿的 `sum/min/max` 是一维 `T[]`，原因：Camel 里写整体归约要遍历所有元素，而 `reduce` 对秩 ≥ 2 的数组是按行折叠（plan 6.1），核心没有“展平后遍历”；`tensor::sum::<T>(t: T[..])` 已是 C++ 的整体归约。推荐：数组的整体归约最终归 `array`（`T[..]`，内核逐元素扫描），`tensor` 只留带轴的形式，删掉 `tensor::sum::<T>`/`mean::<T>`；前提是核心先给一个展平遍历（或内核直接声明 `T[..]`，函数体用 `numel`）。在那之前两处并存、按名字导入。请定方向。

**Q2. 空数组的 `min`/`max`：`T?` 还是故障。** 设计原则 7.7 写“对空区间的归约是故障”，而 `array` 的 `min`/`max` 一直是 `T?`，`tensor` 的 `max(axis)` 空轴是故障。推荐：保持 `T?`（“可能没有”用 `T?`，约定 6.5），7.7 补一句“列表形的函数（`array`、`statistics`）答 `T?`，带轴的张量归约才故障”。`sum` 的空数组是 0（单位元），不是 `T?`。

**Q3. 同上，涉及 `statistics`。** 见 `statistics.notes.md` Q1。

**Q4. 删除三个排序变体与 `merge_sorted_arrays`。** 推荐删除（理由见表）。代价是并行用例要自带归并、性能基线重录。若你想留一个线性的归并，推荐名字 `merge_sorted(xs, ys)`（对等的两个普通参数），函数体 `sort(concat(xs, ys))`，内核在两输入已有序时线性扫描、否则退回排序，这样内核与函数体逐位一致。

**Q5. 排序里的 `-0` 与 NaN 次序。** 推荐 `-0 < +0`、NaN 在最后（和 `min`/`max`、`argmax` 同一套，7.7；`questions-2026-10-23.md` 也写了“按 IEEE 的全序或既有规则”）。否则 `sort` 的结果取决于算法，函数体与内核无法逐位一致。

**Q6. `is_same` 放进 `types.cml`。** 推荐放，理由是它一次去掉六个只为内核声明服务的谓词。

**Q7. 暂不新增的候选。** `argmax`/`argmin`（一维，7.7 的次序；C++ 已有张量版内核，缺 Camel 定义）、`chunks<n>`（训练分批：`[xs->drop<i>->take<n> for i in range(0, len(xs), n)]`，`n < 1` 的故障种类要定）、`group_by<key>`（现在有 `Map` 了，旧文件头说“它需要映射类型”；但 `Map` 的原地更新保证未确认，键必须可作键）。推荐先加 `argmax`/`argmin`、`chunks`，`group_by` 等 `Map` 的原地更新确认后再加。你说一声我就补。

**Q8. 元素类型自带可空时的 `find`/`min`/`max`。** `T?` 作结果，`T` 本身是 `int?` 就成了 `int??`，不允许。目前实例化时才报错。推荐不处理（这是 5.8/3.9 的既定限制，不是库的事）。

---

# 第二轮修订（决策者批复后）

以下**取代**上文与之冲突的部分（上文 §1 中 `sum/min/max` 的 `T?`/一维说法、§8 的 Q1/Q2/Q4/Q5/Q7、§6 中 tensor 整体形式的建议）。

## R1. 规则

- `sum`、`mean`、`min`、`max`、`argmax`、`argmin` 取 `T[..]`，归约所有元素（行主序），都在 `array`。函数体先 `flatten(xs)`；`T[..]` 的 `flatten` 是新的第二个重载（`T[][]` 的那个仍是拼接），底下一个私有原子 `array:elements`（布局内核：行主序元素视图，连续数组共享数据块）。
- 空数组：`sum` 是单位元 0（不是故障）；`mean`、`min`、`max`、`argmax`、`argmin` 是故障（7.7），种类 `InvalidArgument`（目录里没有更贴的；`tensor` 现用 `DivisionByZero`/`RuntimeError`，统一时请定）。`min`/`max` 不再答 `T?`。
- `mean` 现在在 `array`（`T[..]`，仅浮点元素类型；整数数组的均值要先转换，不答整数）。`statistics` 只剩 `variance`、`stdev`、`median`、`quantile`。
- 新增 `argmax`、`argmin`（一维以上的行主序位置；7.7 规则：首个 NaN 胜出，并列取前者，+0 大于 −0）和 `chunks<n>`。
- 不新增 `group_by`。

## R2. 本稿使用 `fail` 的全部位置

`fail<'Kind'>('message')`：拟议的核心原语（待语言作者批准）。我假设它的结果类型能适配任何期望类型（用在 `if` 的一臂里）。

| 文件 | 位置 | 种类 |
|---|---|---|
| array.cml | `chunks`：n < 1 | `InvalidArgument` |
| array.cml | `some_elements`（`mean`、`min`、`max`、`argmax`、`argmin` 共用）：空数组 | `InvalidArgument` |
| statistics.cml | `quantile`：q 不在 [0, 1] | `InvalidArgument` |
| statistics.cml | `quantile`：空数组 | `InvalidArgument` |
| statistics.cml | `variance_of`（`variance`、`stdev` 共用）：少于两个元素 | `InvalidArgument` |

`quantile` 的 q 越界不再靠下标越界“碰巧”故障，旧 C++ 的 `InvalidArgument` 得以保留。

## R3. 对 C++ 的影响（在 §2 之上）

- 内核（`sum_*`、`min_*`、`max_*`）收 `T[..]`：按元素总数扫描；空数组上 `sum_*` 答 0，`min_*`、`max_*` 故障（`InvalidArgument`），与函数体一致。内核返回 `T` 而不是 `T?`，`min_*/max_*` 的空数组分支由 `NullSlot` 改为故障。
- 新增原子 `array:elements`（私有，`T[..]` → `T[]`）。
- 张量模块的整体形式 `sum::<T>(t)`、`mean::<T>(t)`（`stdlib/signatures/tensor.cml`）**删除**，其 C++ 内核并入上述 `array` 内核或直接共用；带轴的形式留 tensor，待 tensor 重写时移动。`tensor_definitions.cml` 里的 `mean` 规范式因此指向 `array::mean`，并要改用 `array` 的 `sum`。
- `argmax`/`argmin` 当前的 Camel 定义用 `precedes(a, b) = argsort([b, a])[0] == 1`，每次比较分配一个两元素数组，只作语义；`tensor` 已有 `argmax_axis` 内核，应作为 `#impl` 内核补上（`i32/int/f32/float`）。

## R4. “空数组求和”需要改的用例

- 全部 `min(..) ?? d`、`max(..) ?? d` 的写法去掉 `?? d`：`test/cases/modules/stdlib/array_basic.cml`、`array_large.cml`、`array_reduction_kernels.cml`、`reduced_minmax.cml`（含空数组期望 `null` 的断言，改为钉故障）、`examples/numeric/monte_carlo_pi.cml`、`random_walk.cml`、`examples/text/table_report.cml`，以及 `docs/stdlib-reference.md`（重生）。
- 对空数组调用 `max` 的示例（`examples/text/table_report.cml` 对表格列求最大宽度）要确认列非空。

## R5. `types::is_same`（给 `types.cml` 的片段）

```cml
/// True when `T` and `U` are the same type.
/// @example is_same::<int, int>()    // true
/// @example is_same::<int, i32>()    // false
export func is_same::<T, U>(): bool {
    return match type T {
        case U => true
        case _ => false
    }
}
```

## R6. 新的风险与待验证

1. `flatten` 两个重载：`int[][]` 既匹配 `T[][]`（T = int），也可能匹配 `T[..]`（T = `int[]`？）。若 `T[..]` 的 `T` 必须是标量元素类型则无歧义；否则要给 `T[..]` 版加 `when`（例如元素不是数组），或把二者并成一个。编译时先试这一处。
2. `[[1, 2], [3, 4]] as int[2, 2]->flatten` 的例子写法：`as` 与 `->` 的优先级未验证；不行就先 `let`。
3. `fail<'InvalidArgument'>(…)` 作 `if` 一臂的类型：假设可适配。
4. `chunks` 里列表推导的 `i` 来自 `range(0, len(xs), n)`，`n >= 1` 保证 `range` 不故障（`step` 为 0 才故障）。
5. `argmin` 的 `(x) => x != x` 判 NaN 适用于一切 `is_ordered` 类型（`string`/`bool` 恒为假）。

## R7. 与 tensor 的重叠（给 tensor 批次）

- `tensor::take(t, indices)` 与核心下标 `xs[idx]` 同义，且与 `array::take<n>` 同名异义：删除 `tensor::take`。
- `tensor::flatten(t, axis?)` 是重塑，`array::flatten` 是元素的行主序视图/拼接：同名异义。建议 `tensor::flatten` 并入 `reshape`（或改名），`array::flatten` 保留。
- `tensor::sum::<T>(t)`、`tensor::mean::<T>(t)` 整体形式删除（R3）；`max/min/argmax/argmin(t, axis, keepdims)` 轴形式留 tensor，之后随 tensor 重写移动。

---

# 第三轮修订（tensor 批次带来的改动）

## S1. 并入 `array` 的东西

- **沿轴的归约**：`sum`、`mean`、`min`、`max`、`argmax`、`argmin` 各有第二个形式 `with <axis: int, keepdims: bool = false>`，结果是 `T[..]`（`argmax/argmin` 是 `int[..]`）。与整体形式同名，一个名字一个来源；`tensor` 里的轴形式删除。每个形式是一个 Camel 包装函数，底下一个私有原子：`array:sum_axis`、`mean_axis`、`min_axis`、`max_axis`、`argmax_axis`、`argmin_axis`（C++ 的 `tensor:sum_axis` 等改前缀并入 `array` 的执行器，VJP、融合、`index_eval` 里的 URI 同改）。原子的元素类型条件在包装函数的 `when`：`sum/min/max/argmax/argmin` 整数与浮点，`mean` 浮点；`bool` 不再是这些归约的元素类型。空的轴与越界的轴是故障 `InvalidArgument`（旧 `RuntimeError`/`DivisionByZero`）。
- **`shape`、`numel`、`dim<axis>`**：从 `tensor` 搬来，无体声明，`dim` 的轴改为尖括号。任意元素类型（不再限 `is_numeric`）。C++ 折叠（`shape`、`numel`、`dim` 在类型已定时折成常量）随之改到 `array` 的执行器前缀。
- **`to_bytes`**（`when is_plain`）与 **`from_bytes`**（计算签名，在新文件 `array.signatures.cml`；形状改为尖括号：`b->from_bytes::<f32><shape: [2]>`）：从 `tensor` 搬来，`fromBytesOperatorGroup` 改注册在 `array` 下。

## S2. 记入“核心事项”的三条

1. **重载选择把 `with` 参数算进去**（决策者裁定）：`with` 参数的个数与名字和普通参数一样参与选择，所以 `xs->sum` 取整体形式、`xs->sum<0>` 取沿轴形式。设计原则 4.4 与约定 4 里“尖括号里的参数不参与重载选择”改为：参与选择的是 `with` 形参的**个数与名字**，不是它们的值。
2. **类型名作用于数组是逐元素转换**（已裁定，同 `math`、`nn`）：`f32(xs)`、`T(xs)`、`float(xs)`；因此没有 `cast`。`array` 的函数体里原来写 `reduce` 把每个元素 `float(x)` 的地方，`sum64` 可以改成对 `float(es)` 求和，行为相同。
3. **`fail<'Kind'>(…)`** 仍待语言作者批准；使用位置不变，另外 `tensor.cml` 的 `one_hot` 用一处（`IndexOutOfRange`）。

## S3. 已知的遗留

- 沿轴的包装函数声明结果是 `T[..]`，丢掉 C++ 推理器原来给出的静态各维（常量轴时结果的秩与各维是知道的）。后续：在核心里给“沿轴归约”一个类型函数（`ReducedShape<S, axis, keepdims>`），包装函数的返回类型改用它，原子的类型规则留在 C++。
- `precedes` 仍是语义定义；`argmax/argmin` 的 `#impl` 内核待补。


## S4. 空数组的 `sum`（作者裁定后）

`sum` 对空数组或空轴得单位元 0，不是故障；`some_elements` 不再用于 `sum`（函数体直接 `flatten`），`sum` 沿轴形式在长度为 0 的轴上也答 0。上文 R1、R2、R3 与 R4 中凡把 `sum` 列为故障的、以及“空数组求和”需要改的用例清单（`reduced_sum_refused.cml:67`、`core_views.cml:31`）一律作废，不需要迁移。`mean`、`min`、`max`、`argmax`、`argmin` 仍是故障。