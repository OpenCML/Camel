# `std::cpp` 的数组比较：缺的是哪一半

## 问的是什么

`a != b` 与 `a == b` 在 `std::cpp` 下、`a`/`b` 是 `f32[4]` 这类数组时，现状如何？如果被拒，是**没翻译**还是**翻译了但语义错**（两者修法不同）。

## 量到的现状（`a`、`b` 都是 `f32[4]`）

- **`a == b`：有翻译，且正确。** 生成的 C++ 是一次 `rt::Array<float>::shaped(...) == ...`，走 `camel::standalone::Array<T>::operator==`（`include/camel/standalone/array.h`）——按内容逐元素、不做同一性短路，因此"数组里有 NaN 就不等于自己"这条和解释器一致。`cppexec` 对拍 `std::fvm`，输出一致。
- **`a != b`：有翻译。** 图上它是 `:op/eq` + `:op/not`（`!=` 建成 `!(==)`），两个算子都有规则，结果与上面同一个 `operator==` 取反。
- **`a === b`：没有翻译。** 拒图在 `tensor:strict_eq`：`function 'main' uses the operator 'tensor:strict_eq' on a value of type 'f32[4]', which has no translation in std::cpp yet`。`a !== b` 落在同一族（`tensor:strict_neq` / `tensor:not`）。
- **且这不是比较一家的事**：`a + b`（`f32[4]`）同样被拒，`tensor:add` has no translation。逐元素的加、减、乘、除、比较，**全都没有**。

## 判：没翻译，而且缺的不是一条 lowering，是一层

`std::cpp` 的后端是"每个算子一条规则 + standalone 运行时里一个同名函数"的形状：`tensor:sum`、`tensor:mean`、`tensor:reshape`、`tensor:to_bytes`、`tensor:ones`/`zeros` 各有手写实现，`Array<T>` 自己只有 `operator==` 这样的整数组操作。**逐元素层不存在** —— 没有"对两个数组的每个位置跑一个函数"的抽象，也没有广播规则。因此 `tensor:strict_eq` 不是"漏了一条",而是"它脚下的那层还没建"。

这也解释了为什么整数组比较能通：它只需要 `Array::operator==`，而逐元素比较需要"两个数组 → 一个 bool 数组"，那是新东西。

## 两条路

**A. 补整个逐元素层。** 在 standalone 运行时里加一个 `zipWith` 形状的抽象（两个数组/一个标量 → 结果数组），含**广播规则**与**不可广播形状的错误文本**，然后把 `+ - * /`、比较（`lt/le/gt/ge/strict_eq/strict_neq`）、`not` 一起接上。代价：新抽象要定契约（结果形状、广播失败报什么、逐位与解释器一致），测试要对拍四个后端；**比较只是这条路最后的一个用户**，单独给比较开路会让后端更不对称（`===` 能用而 `+` 不能）。

**B. 保持现状，把边界写清。** 整数组 `==`/`!=` 已可用且被对拍；逐元素（`===`、`!==`、`+`）尚未翻译。代价 0。

## 默认

**B。** 理由：A 是"给一个后端加一层能力"，属于新设计（广播语义、错误文本、逐位一致），不是补一条 lowering；而且没有裁定要求 `std::cpp` 覆盖张量逐元素。**如果作者要 A，请按"`std::cpp` 的逐元素数组层"立项，从 `+` 开始，比较排在同一批里** —— 不建议只补比较。

## 本轮已做的守卫

`test/cases/trans/cpp/array_compare.cml`：等、首元素不同、末元素不同三种，整数组 `==` 与 `!=` 各一行；由 `test/tools/cppexec` 编译、运行、与 `std::fvm` 对拍（`feat.trans.cpp` 55/55）。末元素不同那一种是特意要的：一个"比到第一个元素就返回"或"只比长度"的实现会在这行答 true。