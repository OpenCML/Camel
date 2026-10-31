# 不可比的用例不是噪声，是尺子的漏洞

日期：2026-10-31　车道：`lane-lit`（`std::cpp` 的语料普查）

**判据（一句话，2026-10-31 作者的原话）**：不可比的用例不是噪声，是尺子的漏洞——要么让它可比，要么写明为什么不比。

## 这一句是从哪来的

`run.py <目录>` 读整个 `test/cases`（3225 个程序）的结局里有 `differs` 一档：
"翻译了、编译了、跑了，输出不一致"。今天的读数是 **8** 条，分三类：

| 类 | 条数 | 是哪些 |
| --- | ---: | --- |
| 计时 / 当前时间 | 2 | `linear/nvm/acc.cml`、`perf/fib_recursive.cml`：打印自己量到的墙钟秒数 |
| 口径 | 1 | `consistency/generic_values/candidate_body_fails.cml`：`std::fvm` 自己以 `PassInternalError` 停下，没有可比的参考读数 |
| 真缺陷 | 5 | `compile/array_shape/err_as.cml`、`opt/generic/fold_cast_fails.cml`、`compile/array_shape/rank_zero_field_literal.cml`、`modules/stdlib/fault_gcd_overflow.cml`、`modules/tensor/shape_inference/computed_extent_run_time.cml` |

前两类共 3 条不是"翻译得不对"，是**尺子量不动**：两条量的是时间，一条的参考读数本身坏了。

## 选了哪条路

**写在尺子上**：三条名字与理由写进 `test/tools/cppexec/census.sh` 的 `not_comparable`
（并在输出里列出来），任何 `differs` 计数都要把它们剔掉。

没选"让用例不打印时间"：那两个程序的产出就是它们量到的秒数，删掉打印等于删掉它们存在的理由；
要它们不打印，就得给它们换一个身份（不是 benchmark），那是另一笔。

## 真缺陷那一类（各是独立的一笔）

- `err_as.cml` / `fold_cast_fails.cml`（同一个）：**到固定形状的转换在 `std::cpp` 上被整个丢掉**
  —— `v as f32[4]` 印 `6`（`len` 原数组），解释器在 `5:13` 报
  `RuntimeError: conversion to 'f32[4]' failed: the array has shape [6].`。缺的是运行时那一次
  形状比较（不是元素个数比较：`[2, 3]` 与 `[3, 2]` 元素数相同，形状不同，解释器照样报）。
- `rank_zero_field_literal.cml`：rank-zero 的值在 standalone 运行时里**不存在**——
  `Array::setShape` 对 `rank == 0` 直接返回（`include/camel/standalone/array.h`），因为那里
  `rank == 0` 的意思是"没写形状"，而解释器里 rank-zero 是一个**值**（设计原则 5.4）。
  这是给作者的**问题**，不是一笔代码改动能定的：秩 0 到底表示"没有维度"还是"没有写维度"。
- `fault_gcd_overflow.cml`：`gcd` 对最小 int64 的拒绝，在 C++ 侧被溢出绕过去了（取负/取绝对值的
  未定义行为），与窄化语义是同一条线。
- `computed_extent_run_time.cml`：`reshape<[n, 3]>` 在运行时算出的形状不满足调用点的
  `f32[2, 3]`，翻译侧没查调用点的形状契约。

## 顺带一条

同一份普查还印出 **100 条 `does not build`**（生成的 C++ 编不过）。那既不是 `differs` 也不是
`refused`，尺子上还没有它这一格——谁要拿这份普查说事，得先把这一格说清楚。

## 合并 `develop` 之后重算一遍（`f84303109` 的树）

语料 3236 个程序：**same 1100 / not a program 1073 / refused 965 / does not build 91 /
differs 7**。`differs` 的 7 条按同一把尺子分开：

- **① 计时：3 条** —— `linear/nvm/acc.cml`、`linear/nvm/fib.cml`、`perf/fib_recursive.cml`。
  `fib.cml` 是新出现在这一档的（上一轮它还是 `same`），名单已经补进去。
- **② 真缺陷：3 条** —— 其中两条这一轮修掉了：
  - `modules/stdlib/fault_gcd_overflow.cml`：`math:abs` 被译成 `std::abs`，最小值上是未定义行为，
    优化器把 `abs(a) < 0` 折掉，`gcd` 的拒绝就丢了。改成 `rt::absOf`（num.h 的环绕取负），
    `abs_smallest_int.cml` 守住两名读数。
  - `loops/zero_leaf_state.cml`：零字段结构体印成 `{  }` 两个空格；文本是按"开的一半 + 关的一半"
    拼的，中间没有字段就多留了一个空格。零字段时写成 `{}`。
  - 剩下的 `compile/array_shape/rank_zero_field_literal.cml`：rank-zero 在 standalone 运行时里还不是
    一个值（`setShape` 对 `rank == 0` 直接返回），这是给作者的问题，不是一笔补丁能定的。
- **③ 口径：1 条** —— `consistency/generic_values/candidate_body_fails.cml`，理由同上。

另外 `err_as.cml` 与 `fold_cast_fails.cml`（上一轮列在真缺陷里的两条）在 `develop` 上已经不再
`differs`：`rt::asShape` 与 `rt::Array<T>::shaped` 落地之后，到固定形状的转换会检查形状了。
修这两条的是 `develop` 上的提交，不是这一轮的。

## 再一笔：`reshape` 的目标形状是程序算出来的（2026-10-31 晚）

`modules/tensor/shape_inference/computed_extent_run_time.cml` 上一个读数是 `differs`：
`std::fvm` 在 `7:25` 报 `TensorDimensionMismatch: Array shape mismatch in 'reshape': reshape changes
tensor element count: [6] -> [3, 3].`，而生成的程序印 `21.0`。

根因不在检查，在**取错了形状**：`reshape<[n, 3]>(y)` 的目标形状是算符的**值参数**（一个 `int[]`），
规则却从**结果类型**读 extents —— 那是调用点契约填进去的 `[2, 3]`，不是程序说的 `[n, 3]`。
于是生成的是 `rt::reshape(y, {2, 3}, 6, ...)`：形状被换成契约里的那个，元素数正好相等，检查就过了。

修法：`with` 参数不是常量（不是 DATA 节点）时，目标形状是程序算的 → 生成
`rt::reshapeBy(y, rt::Array<int64_t>{n, 3}, at_...)`，由运行时读这些值、按值的乘积查元素数，
并在运行时把两个形状都写进报告（`rt::extentText` / `rt::shapeText`，`[6] -> [3, 3]`）。
规则要能分辨"写死的形状"和"算出来的形状"，所以 `OperatorCall` 多了一句
`withIsConstant(i)`（`function_emitter.cpp` 里按 `GCNodeKind::Data` 判定）；写死的形状仍走
`rt::reshape` 的原路径，静态形状的检查与以前逐字节相同。

## rank-zero：两条并行读数（供裁定，不修）

同一条程序 `test/cases/compile/array_shape/rank_zero_field_literal.cml`：

```
std::fvm : 1.5           std::cpp : [1.5]
std::fvm : 2.5           std::cpp : [2.5]
```

`f32[..]` 位置上写一个元素，解释器给的是**rank-zero 的值**（设计原则 5.4），印出来就是那个元素；
`std::cpp` 里 `rt::Array` 没有 rank-zero 这个值 —— `setShape` 对 `rank == 0` 直接返回
（`include/camel/standalone/array.h`），因为那里的 `rank == 0` 表示"没写形状"，于是同一个值被
当成一维的一个元素，印成 `[1.5]`。要修先得定语义：**rank 0 是"没有维度"，还是"没有写维度"**。
这是给作者的问题，已在本页记着，等裁定。

## 四个格子：`does not build` 是缺陷，不是拒绝

**判据（一句话）**：**编译不过不是"拒绝"，是缺陷；把缺陷算进拒绝里，就是让尺子替翻译器开脱。**

**尺子的第二读法**：`test/tools/cppexec/census.sh` 现在一次读四个格子，语料是
`test/cases benchmarks examples exemplars`（`run.py` 判定，脚本只数格子）：

| 格子 | 是什么 |
| --- | --- |
| `same` | 译了、编了、跑了，输出与退出码都和 `std::fvm` 一致 |
| `refused` | `std::cpp` 拒绝（`TranslationRefused`） |
| `differs` | 译了、编了、跑了，输出与 `std::fvm` 不一致 |
| `does not build` | **C++ 编译器**拒绝了 `std::cpp` 写出来的程序 |

`defect = differs + does not build`，报在 `refused` 之前；`does not build` 的每一条都印编译器
自己的第一行，连同它指的那行程序文本。

**读数（3323 个程序）**：

| | 条数 |
| --- | ---: |
| `same` | 1204 |
| **`defect`（`differs` 0 + `does not build` 7）** | **7** |
| `refused` | 1003 |
| 不是程序（`not a program`） | 1100 |
| 不可比（名单，已从 `differs` 里剔除） | 9 |

**不可比的 9 条**：8 条打印自己量到的时间或当前时间（`linear/nvm/acc.cml`、`linear/nvm/fib.cml`、
`perf/fib_recursive.cml`、`linear/jit/perf_fib_smoke.cml`、`para/core/monte_carlo_pi.cml`、
`para/core/unordered_foreach_timing.cml`、`modules/std/time/basic.cml`、`examples/perf/fibs.cml`），
1 条参考读数本身坏了（`consistency/generic_values/candidate_body_fails.cml`，`std::fvm` 自己以
`PassInternalError` 停下）。名字与理由在 `census.sh` 的 `not_comparable` 里。

**7 条编不过的程序（编译器原话第一行）**：

| 程序 | 编译器说 |
| --- | --- |
| `compile/binding_join/join.cml` | `error: no matching function for call to 'append'` |
| `compile/native/idx_field_missing.cml` | `error: no viable conversion from 'const S' to 'const Str'` |
| `effects/callback_write_through_parameter.cml` | `error: no matching function for call to 'apply'` |
| `effects/value_made_after_a_call_captures_its_version.cml` | `error: no matching function for call to 'run'` |
| `linear/byref/var_argument_reads_the_nearest_write.cml` | `error: no matching function for call to 'bump'` |
| `linear/operators/string_plus_assign.cml` | `error: no matching function for call to 'append'` |
| `opt/defun/widened_own_values.cml` | `error: no matching function for call to 'apply'` |

这 7 条一条也不进 `refused`：**它们是写给 C++ 的那一侧的错**，而 `refused` 是"这个程序我不译"的
回答。把两者并成一格，读数会好看，缺陷会消失。
## 这 7 条已经编出来了：四条根因，四个提交

"编译不过不是拒绝，是缺陷；……——这句话的下一步是逐个把那 7 条编出来。" 这 7 条编出来了。四条根因，
每个根因一个提交，每个提交带一条最小用例（`test/cases/trans/cpp/` 下，也进了
`test/tools/cppexec/expect.txt`），并且每条都做了反证：**去掉修复，红的就是那一条**，其余不动。

| 根因 | 程序 | 修前编译器原话第一行 | 产物里改的是什么 |
| --- | --- | --- | --- |
| ① `var` 实参 | `linear/operators/string_plus_assign.cml`、`linear/byref/var_argument_reads_the_nearest_write.cml` | `error: no matching function for call to 'append'` / `'bump'` | 传给 `var` 形参的是格（`rt::Cell<T>`）本身，不是它的值；格先被赋过值再传时，没被认作逃出函数 |
| ② `rt::Fn` 两处拼法 | `effects/callback_write_through_parameter.cml`、`effects/value_made_after_a_call_captures_its_version.cml`、`opt/defun/widened_own_values.cml` | `error: no matching function for call to 'apply'` / `'run'` | 函数值按**它占的那个位置的函数类型**拼；位置是 `void` 函数类型时，值在造它的地方丢掉结果 |
| ③ 记录上的 `[]` | `compile/native/idx_field_missing.cml` | `error: no viable conversion from 'const S' to 'const Str'` | 记录是 C++ 聚合、运行期没有字段表，所以名字得是写的人读得出的那个；没有这个字段就是解释器自己的 FieldNotFound 报告，名字在里面 |
| ④ 空字面量 | `compile/binding_join/join.cml` | `error: no matching function for call to 'append'` | `[]` 是 `void[]`，元素类型由它占的位置给：结果数组的元素，或算子点名的那个元素 |

四条都只改产物里的类型与取值，没有一处是把编译器的话压下去：没有 `static_cast`，没有把模板放宽到
"什么都能进"，也没有把哪条改成 `refused` 换读数。

关于第四条的判断（"推不出元素类型，是该拒绝还是该修"）：这里**不是推不出来**。`[]->append<1>` 的
join 给出 `int[]`，`[]->contains<1>` 比较的是 `int` —— 元素类型是知道的，错的是拼法：拿字面量自己的
`void[]` 去拼。所以它落在"修类型"。真正推不出来的形状是另一类（位置本身没有类型），那类的问题是
拒绝的措辞，不是这一条。

**四条修复之后，同一把尺子（3327 个程序）**：

| | 修前 | 修后 |
| --- | ---: | ---: |
| `same` | 1204 | **1215** |
| **`defect`（`differs` + `does not build`）** | **7** | **0** |
| `refused` | 1003 | 1003 |
| 不是程序（`not a program`） | 1100 | 1100 |
| 程序总数 | 3323 | 3327（多 4 条最小用例） |

`same` 多 11 条 = 7 条修好的加 4 条新用例。`refused` 一条没动：这 7 条里没有一条被折进拒绝。

## `refused` 的销账：先按第一句理由分类，再一条一类判"该拒"还是"该修"

`refused` 是一个格子，不是一种回答。把 1003 条按**拒绝理由的第一句**归类——名字和类型从句子
里抽掉，剩下的是句子自己的形状——前 8 类如下（第 8 名并列，故列 9 行；句子原话取自示例程序）：

| 条数 | 第一句理由（原话） | 例 |
| ---: | --- | --- |
| 465 | `function 'main' uses the operator 'onnx:export_model' on a value of type '(m: Mlp, x: f32[..]) => (float, Mlp)', which has no translation in std::cpp yet.` | `benchmarks/e13/l2x3_scalar_downstream_probe.cml` |
| 199 | `function 'main' uses a value of type 'Map<string, int>'; std::cpp translates numbers (real and complex), bool, string, enums, arrays, tuples, structs, T?, T!, functions and var variables of these, and void.` | `examples/data/nullable_lookup.cml` |
| 157 | `the program has no call of 'main' std::cpp can find.` | `benchmarks/common/camel_bench.cml` |
| 74 | `function 'main' uses the operator 'onnx:supported_operators', whose result of type 'string[]' has no translation in std::cpp yet.` | `benchmarks/tools/onnx_capabilities.cml` |
| 30 | `function 'main' uses 'rand:normal_array' to fill a shaped array, and std::cpp writes no shaped array value: the pass has no tensor type, so the draw cannot be handed out.` | `examples/models/naive-in-expert-out/train_step.cml` |
| 18 | `function 'total' uses the operator ':op/idx_arr' on values of type 'f32[..]' and 'int' std::cpp does not translate it for yet.` | `test/cases/compile/struct_entry/concrete_into_open.cml` |
| 17 | `function 'main' calls function 'tally<int>', which writes captured variables, and std::cpp does not translate their new versions yet.` | `test/cases/compile/generics/effects.cml` |
| 11 | `function 'main' converts a value of type 'f32' to 'f32[..]', which std::cpp does not translate yet.` | `test/cases/compile/array_shape/open_rank_element.cml` |
| 11 | `function 'main' reads the block counters of the Camel runtime, and a program std::cpp writes has no runtime block space of its own: the counters are not available on this target.` | `test/cases/feat/mm/gc/gc_block_adopted.cml` |

剩下 6 条是"走一个 rank 开放的值"，再往下是 15 条各 1-2 条的尾巴（入口参数的形状、分支
畸形、循环收集值、顶层值非常量、按名字索引结构体）。这些类**还没判**，尺子把它们原样打出来，
不折进任何一堆。

### 最大的一类：465 条"算子对它的操作数类型没有规则"

判一句（作者的话）：「**语言规定这个目标必须有吗？**」——同一句话把这一类分成两堆：

| | 条数 | 是什么 |
| --- | ---: | --- |
| **目标不具备** | **423** | 算子要的值在一个独立的 C++ 程序里根本不存在：形状数组与张量（`tensor:` 144）、被微分的函数（`autodiff:` 138）、导出的模型（`onnx:export_model` 91）、`Device<>`（`tiles:` 26）、模块自己的 kernel 表（`nn:` 14、`ckpt:` 2）、`json:encode` 3、`pyplot:plot` 1、用例自带的两套 test 模块 4。语言不规定 `std::cpp` 这个目标要有张量和这些模块的能力，所以拒绝正确，从待修清单划掉。 |
| **该修** | **42** | 算子跑在目标已经有的值上（`int[]`、`f32[]`），答的也是已有的值：`array:*` 33 条（`sum_f` 6、`argsort_l` 5、`sum_l` 5、`sort_l` 4、`max_l` 3、`sort_l_in_place` 2，其余 8 个各 1）与 `rand:*` 9 条（`shuffle_l` 8、`shuffle_i` 1）。这不是目标的边界，是写的人还没写的规则。 |

**一条判据（进页，一句话）**：拒绝的销账不是把话说得更客气，是把每一条分成"该拒"和"该修"两堆——而"该修"那一堆要一条条编出来、跑出来。

尺子按这条改：`census.sh` 现在把 `refused` 按第一句理由的形状打出来（上面那张表就是它打的），
并对判过的那一类再打一次"目标不具备 / 该修"的两堆——一堆一类地判，判过的在尺子里留名，没判的
原样打出。

**那堆"该修"要一条条编出来、跑出来，不是改成措辞。** `array:` 那一堆 33 条这一轮已经落成规则：
`argsort` / `sort`（含 in-place 形式）/ `merge_sorted` / `sum` / `min` / `max` / `dim` 七个算子族，
在 `operator_rules.cpp` 里按声明的元素类型和"一维的 run"接上，语义直接取解释器自己的那几条
Statement（`numberLess` 的顺序、f64 累加一次的 `sum`、`IEEE 754-2019` 的 `min`/`max`、空归约的
`InvalidArgument`、`dim` 的越界报告），所以跑出来的是同一行输出，不是"编过了"。
这一轮 33 条里 25 条从 `refused` 变成 `same`（全语料 `same` 1215 → 1241，`refused` 1003 → 978，`defect` 仍是 0），余下 8 条各有各的别的类：3 条卡在 in-place 形态的*绑定*（`xs = xs->sort`）、1 条是形状数组上的归约、4 条是 `Bytes` 结果。同一轮的红-去-证明：把 `argsort` 那六条规则拿掉，用例回到
`uses the operator 'array:argsort_l' on a value of type 'int[]', which has no translation in std::cpp yet.`；
装回去，用例 `same`。

**`rand:` 那 9 条**：`rand:shuffle_*` 是"该修"里最后的一族，规则写的是解释器自己的内核，不是"另一种也均匀的
洗牌"——每个位置取**它自己那把 key** 折出来的一个 f64 抽样（`-3 - index` 的计数器、block 的 53 位、不做模数
纠正也不加缓冲），按（抽样，位置）排序，再按这个顺序取元素。分布对不等于逐字节对：要么连解释器的偏差
一起抄，要么读回来的顺序就不是那一行。`random.h` 里写的就是 `modules/random/operators.cpp` 的同一个 block
函数、同一次 fold、同一个排序，所以种子在程序里（`key(42)`）时两次运行逐字节相同，没有一条用例被挪进
"不可比"。
**一条判据（进页，一句话）**：分布对不等于逐字节对——要跟解释器一样，就得连它的偏差一起抄。

同一轮红-去-证明：拿掉四条 `shuffle` 规则，`test/cases/trans/cpp/shuffle_reads_back.cml` 回到
`uses the operator 'rand:shuffle_l' on a value of type 'int[]', which has no translation in std::cpp yet.`。

### 第二类判过了：`203` 条"写的人不带的声明类型" = `83` 该拒 + `120` 该修

判据还是那一句「语言规定这个目标必须有吗？」，只是这次问的是**那个类型**：它是这台机器根本没有的东西，
还是一块写的人还没写出拼法的数据。

| | 条数 | 构成 |
| --- | ---: | --- |
| **目标不具备** | **83** | `Device<...>` 28（GPU 缓冲）、`Rule` 22、`Graph` 12、`Node<...>` 10（编译器自己的值：规则、图、图的节点）、`PyObject` 7（Python 运行时的对象）、模块自己的状态 4（张量结构）。语言不规定一个独立的 C++ 程序要有 GPU、图、Python 对象或张量状态，所以拒绝正确，从待修清单划掉。 |
| **该修** | **120** | `Bytes` 46、流 `T..` 35、`Map<...>` 22、`Json` 11、`Set<...>` 5、装这些的数据 struct 1。它们是普通数据，目标有地方放，只是写的人还没有拼法——这是缺口，不是边界。 |

最大一堆是"该修"，其中最大的一族是 `Bytes` 46 条；下一笔只修这一族，不一次全修。尺子现在把这一类的
两堆也打出来（"目标不具备 / 该修"各按族列数），没判过的族照旧原样打印，不猜进任何一堆。

**一条判据（进页，一句话）**：不可比也要有理由清单——它和拒绝一样，是会被滥用的一个筐。

尺子按这条站住：`not_comparable` 这一轮**没有涨**（仍是 12 条），每一条都写着"为什么两边不可比"
——打印自己测的秒数（`block_sort_merge`、`bootstrap_medians`）、打印当前时间（`modules/std/time`）、
参考实现在 1e8 元素上自己 exit 1（`para/core/merge_sort`）——没有一条是"比不了就划过去"。
