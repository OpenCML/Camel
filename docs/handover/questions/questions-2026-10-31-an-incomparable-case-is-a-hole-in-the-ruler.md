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