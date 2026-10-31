# `mean` 的返回类型：元素类型，还是 `f32`

> **一个数被印成 `1075139925` 的时候，错的不是那个数，是它的类型；把这种格子记成"没写规则"，是把病因写错了一层。**

日期：2026-10-31　车道：`lane-nestedlit`（`std::cpp` 写出器，沿轴归约那一族）

**要作者答的一句**：`mean`（以及 `array.cml` 转发给它的那些名字）**返回元素类型 `T`，还是返回 `f32`**？
下面两条路的代价各一句，按字面执行即可。

> **裁定（作者，2026-10-31 14:30）：A——`mean` 返回元素类型 `T`。**
> 整数的均值就是规范形 `T(sum(float(x)) / float(numel(x)))`：按 f64 累加、按 f64 相除，最后按元素类型
> 舍入一次，求值之后按 `int(·)` 既有的转换规则（向零）变成 `int`。`bool` 不属于 `is_numeric`，所以
> `mean(bool)` 不合法，签名照这个收。
>
> 于是 `mean_axis([1, 2, 4] as int[3], 0, false)` 是 `2`（`int(7 / 3)`），四个调度器与 `std::cpp` 逐字一致。
>
> **判据（自这条起）：原则里写着的语义，就不是缺陷——被认为"丢掉了平均"的那个数，正是规范形要求的结果。**
> 这条判据把本页一开始的读法反过来：`2` 不是"丢了平均"，`1075139925` 才是缺陷（那是 `2.3333333f` 的位模式
> 被当整数印出来），修的是那个，不是签名。

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

（裁定取 **A**，这条建议不采纳。回头看，它把"A 的两条下场"当成了 A 的必然后果，其中一条是误判：位模式那一条
是 **kernel 的类型错**（`outType` 被改成 `f32`，再往声明为 `T` 的槽里写 `float`），不是"返回 `T`"这件事的下场。
A 真正只有一条下场，而且它不是下场，是规范形本身。）

## 边界与影响面

- 本页**只记问题**：不动用例、不动 `test/tools/cppexec/expect.txt`，所以 `not_comparable` **不涨**。
- `std::cpp` 侧现在按名字拒整数 `mean`，这是**误诊的产物**（见上），不是一条要保留的拒绝；裁定之后
  按裁定改，不按这一页的倾向改。
- 与这条相邻但不同的一问：`argmax_axis` 与 `array:argmax_axis` 两条身份（已在
  [`one-operator-two-names`](questions-2026-10-31-one-operator-two-names.md) 里记着）。

## 落地（2026-10-31 22:30，`lane-nestedlit`）

**两处缺陷一起收，`std::cpp` 按名字那条拒绝随之删掉。** 规范形写在一个头文件里
（`modules/math/mean.h` 的 `meanValue<T>(total, count)`），解释器的两条路都调它——`tensor` 的轴内核
（`modules/tensor/kernels/reduce.cpp`）与 `array` 的归约（`src/builtin/operators/array_axis.cpp`）——
所以"同一句 `mean_axis` 有两种答案"这件事在结构上不再可能。

1. **kernel 的类型**：`outType` 不再把 `Mean` 改成 `f32`，答案就是元素类型（`reduce.cpp:162`）；
   `Mean` 的累加与 `Sum` 同一套（浮点 f64 从 +0 升序、整数精确），除法在 f64，最后 `meanValue<T>` 一次
   舍入。整数那条支的 `sum` 仍是精确 int64（5.1/7.7 的要求），只有除法和最后那次转换在 f64——
   读数与规范形逐字相同。
2. **`array` 那条路**：`reduceAxis` 里那句 `the mean is taken of floating-point elements` 删掉了，
   整数与浮点走同一条实现；空轴也不再为 `mean` 拒绝（只有四个极值拒），与 kernel 对齐。
3. **`std::cpp` 的规则**（`operator_rules.cpp`）：`tensor:mean_axis` 的元素类型守卫从"只写浮点"放宽到
   `int64`/`i32`/`f32`/`f64`，`rt::meanAxis<T>` 对整数按 `static_cast<T>`（向零）回答；`bool` 仍不写，
   因为 `mean` 声明为 `when is_numeric::<T>()`。
4. **读数**（`std::fvm`；四个调度器 `nvm`/`fvm`/`jit`/`prl` 与 `std::cpp` 逐字一致）：

   ```
   $ camel test/cases/trans/cpp/mean_axis_elements.cml std::fvm
   2
   []
   [2]
   2.3333333
   0.33333334
   [2, 4]
   ```

   `python3 test/tools/cppexec/run.py test/cases/trans/cpp/mean_axis_elements.cml` 报 `1 100.0% same`。
   `mean_axis([1, 2, 4] as int[3], 0, false)` 是 `2`；`f32(ints)` 是 `2.3333333`（要浮点均值先写这一句）。

5. **一条随之显形的缺口**（不是这次改出来的，是"整数 `mean` 不再被拒"之后才被普查看见）：普查那唯一一格
   `does not build` 是 `test/cases/modules/tensor/int64_sum.cml` 里 `sum<0>(big)`——
   `const int64_t v5_sum_axis = rt::sumAxis(...)`，节点类型是元素本身而规则写的是数组。这是
   `sum<axis>` 在秩 1 上的写法问题，记在这里，下一笔修。

6. **闸门**：`[SUMMARY] total: 8133, pass: 8129, fail: 0, skip: 2, known failures: 2`；普查
   `programs 3341 / compared 3329 / not comparable 12 / same 1251 / refused 977 / differs 0 /
   does not build 1`（`not_comparable` 没涨）。