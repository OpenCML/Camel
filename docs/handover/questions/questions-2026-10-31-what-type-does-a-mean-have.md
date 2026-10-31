# `mean` 的返回类型：元素类型，还是 `f32`

> **一个数被印成 `1075139925` 的时候，错的不是那个数，是它的类型；把这种格子记成"没写规则"，是把病因写错了一层。**

日期：2026-10-31　车道：`lane-nestedlit`（`std::cpp` 写出器，沿轴归约那一族）

**要作者答的一句**：`mean`（以及 `array.cml` 转发给它的那些名字）**返回元素类型 `T`，还是返回 `f32`**？
下面两条路的代价各一句，按字面执行即可。

## 现象

三处说法，互相不一致：

1. **签名说返回元素类型，而且接受一切数值**：`stdlib/tensor.cml:169`
   ```cml
   export func mean_axis::<T>(xs: T[..], axis: int, keepdims: bool): T[..] when is_numeric::<T>()
   ```
2. **kernel 对整数答 `f32`**：`modules/tensor/kernels/reduce.cpp:160-174`——`Mean` 的输出类型在
   `f64`/`bf16`/`f16` 时取元素类型，其余（含 `int`/`bool`）取 `Float32`；随后 `mean = total / extent`
   一次舍入，存进那个类型（`T(mean)` / `float(mean)`）。
3. **另一条路直接拒**：`src/builtin/operators/array_axis.cpp:182-186`——`array` 那套 `reduceAxis` 对非浮点
   元素的 `mean` 报 `the mean is taken of floating-point elements`。

于是同一句 `mean_axis` 在解释器里有两种答案，取决于走哪条路；`tensor:mean_axis` 走的是第 2 条。

**读数**（`std::fvm`，整数 `[1, 2, 4]` 的均值）：

```
$ printf 'import { mean_axis } from tensor\n\nfunc main(): int sync {\n    let xs: int[3] = [1, 2, 4]\n    println(mean_axis(xs, 0, false))\n    return 0\n}\n' > /tmp/mi.cml
$ camel /tmp/mi.cml std::fvm
1075139925
```

`1075139925` 是 `0x40155555`，也就是 `f32` 的 `2.3333333` 的**位模式**被当整数印了出来
（`python3 -c 'import struct;print(struct.unpack("<I",struct.pack("<f",7.0/3.0))[0])'` 得到同一个数）。
值本身是对的（`7 / 3`），错的是它被读成什么类型。

`std::cpp` 今天按名字拒这一格：

```
$ camel /tmp/mi.cml std::cpp
error[RuntimeDiag::TranslationRefused]: … uses the operator 'tensor:mean_axis' on values of type 'int[3]' and 'int' and 'bool' std::cpp does not translate it for yet.
```

**这不是"没写规则"**：规则在（`argmax_axis`/`sum_axis` 同一笔那一族里），拒绝的原因是**类型不成立**——
签名承诺 `T`，kernel 的答案却是 `f32`。补一条规则只会把位模式搬进生成的程序，病因还在。

## 两条路的代价

**(A) 返回元素类型 `T`（今天的签名）。** 整数均值只剩两种下场：要么四舍五入成整数
（`[1, 2, 4]` → `2`，丢掉的正好是"平均"），要么就是今天这样把 `f32` 的位模式当整数印出来——两种都不是均值。

**(B) 返回 `f32`（与 kernel 的答案一致）。** 签名与解释器的实际答案对齐（`2.3333333`），代价是
`int[..]`（以及 `bool[..]`）上的 `mean` 结果类型与输入不同：`stdlib/array.cml:239` 的转发、`T[..] => T[..]`
这条泛型形状，以及调用点上的类型标注都要跟着写成"整数进、`f32` 出"。

## 我方默认建议

**不裁**（原则：答不出来就问，不定 ad hoc）。若一定要一句倾向：**B**——它是唯一能让"类型"与"答案"
一致的一条，而 A 的两条下场都无法执行。这条倾向不作为落地依据，等作者裁定。

## 边界与影响面

- 本页**只记问题**：不动用例、不动 `test/tools/cppexec/expect.txt`，所以 `not_comparable` **不涨**。
- `std::cpp` 侧现在按名字拒整数 `mean`，这是**误诊的产物**（见上），不是一条要保留的拒绝；裁定之后
  按裁定改，不按这一页的倾向改。
- 与这条相邻但不同的一问：`argmax_axis` 与 `array:argmax_axis` 两条身份（已在
  [`one-operator-two-names`](questions-2026-10-31-one-operator-two-names.md) 里记着）。