# `match type` 能不能像拆类型包那样拆开数组类型的维度列表，让 `ShapeOf<T>` 用 GN 写得出？

**状态**：待裁。裁定 `d9b509aec` 说 `tensor:shape` 的返回类型要说出长度，做法是「在 `stdlib/types.cml` 里用 GN 写一个类型函数，用 `match type` 拆开维度包」。这一页只问那一句做法里的前提：**数组类型的维度列表，`match type` 今天拆得开吗**。下面是不改任何代码量出来的读数。

## 1. 读数：拆不开（探针程序的逐字输出）

在一棵类型层与 `develop`（`1ba69259d`）逐字节相同的树上跑的（`git diff 09455eae3 1ba69259d -- src/core/type src/compile/gct src/compile/gir stdlib/types.cml stdlib/tensor.cml` 为空），所以这不是旧二进制的行为。

**① 把数组类型当类型包拆。** 写成 `FirstOf` 那种 `case (W, ...Rest)`：

```cml
type B0<T> = match type T {
    case (E, D, ...Rest) => int
    case _ => bool
}
is_same::<B0<float[2, 3]>, int>()    // false
is_same::<B0<int>, int>()            // false
```

两条都落到 `_`。数组类型**不是**包，`case (E, D, ...Rest)` 选不中它。原因在
`src/core/type/typevar.cpp#takePackApart` 的第一句：`if (!TypeCallType::isPack(subject)) { return false; }`——只有 `$pack` 能被拆，而 `$pack` 的成员是**类型**。

**② 数组的「维度」不是类型。** `include/camel/core/type/composite/array.h#ArrayType` 的注释自己说形状是「an element type plus a shape」，形状是 `std::vector<ExtentForm>`：每一维是一个常量或一个维度名上的线性式，**不是 `Type`**。所以维度列表即使拆开，拆出来的东西也没有类型层的身份可以绑定给一个名字，秩这个数在类型表达式里也没有一处可以写（`int[k]` 的 `k` 今天是常量或维度名上的线性式，不是「某个类型有几维」）。

**③ 形状词只能分辨「是不是数组」。**

```cml
type A0<T> = match type T { case float[...] => int  case _ => bool }
is_same::<A0<float[...]>, int>()     // true   （开放秩的身份模式只选开放秩）
is_same::<A0<float[2, 3]>, bool>()   // true   （不选固定秩）

type C0<T> = match type T { case array => int  case _ => bool }
is_same::<C0<float[2, 3]>, int>()    // true
is_same::<C0<int>, bool>()           // true
```

`case array` 选一切数组，`case float[...]` 只选开放秩的：两种写法都答不出「几维」。

## 2. 于是裁定那一条做法缺一块

库里已有的、能读数组类型的类型函数是 `ElementOf`、`WithElement`、`RowsOf`（`stdlib/types.cml#RowsOf`）：它们取元素类型、换元素类型、加一维，**没有一个数得出维数**，也没有一个能把「一个列表类型有几条」变成结果数组的维数。所以 `ShapeOf<T>`（`E[a, b]` 答 `int[2]`、`E[...]` 答 `int[]`）今天写不出来；写不出来的不是名字，是那块类型层表达力。

这一块挡住的是三处**今天在声明之外说形状**的 C++：

- `modules/tensor/ops/gradient_ops.cpp#sumToInfer`——`sum_to` 的结果秩；
- `modules/tensor/ops/vjp.cpp#shapeListType`——`tensor:shape` 节点的长度（`vjp` 与 `accumulateOperand` 的调用点用它）；
- `modules/tensor/ops/layout_ops.cpp#shapeInfer`——`shape` 自己的 `infer`，今天答开放列表 `int[]`。

裁定落地的那一天，这三处按裁定原文「C++ 不加特例」删掉；在那之前它们是今天 `fail 0` 的一部分，不能先动。

## 3. 我方默认（按此继续，不等回复）

**不加新的 C++ 类型函数原语。** 理由与 `FirstOf` 的返工同一条（`1e32a65c2`：取包头的 `FirstOf<P>` 曾被写成 `TypeCallType::evaluate` 里的一个 C++ 原语，判掉，改成库里 `match type` 拆包）。`RowsOf`、`ElementOf` 是既有的，**新增一个**是同一个动作，不因为名字叫 `ShapeOf` 就不同。

**8 条动态 batch 的导出用例继续记为 `known_fail`**（裁定原文：「在这之前，按默认把 8 条记为 `known_fail`，可以」），`known_fail_reason` 指向
`2026-11-05-does-a-shape-declaration-need-a-length-in-its-return-type`。

**等**`match type` 能拆开数组类型的维度列表（或类型层另有说法）**再按裁定做** `ShapeOf<T>`；在那之前不写 `ShapeOf<T>`，不改 `tensor:shape` 的声明，不动上面那三处 C++。

## 4. 需要裁的那一句

**数组类型的维度列表要不要成为 `match type` 能拆开的东西（从而 `ShapeOf<T>` 用 GN 写得出），还是让「一个列表有多长」留在类型层说不出来的状态、8 条用例就停在 `known_fail`？**