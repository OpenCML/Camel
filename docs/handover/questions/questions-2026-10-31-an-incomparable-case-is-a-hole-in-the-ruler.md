# 不可比的用例不是噪声，是尺子的漏洞

日期：2026-10-31　车道：`lane-lit`（`std::cpp` 的语料普查）

**判据（一句话）**：不可比的用例不是噪声，是尺子的漏洞；漏洞要么补上，要么写在尺子上。

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