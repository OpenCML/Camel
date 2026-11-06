# `match type` 能不能像拆类型包那样拆开数组类型的维度列表，让 `ShapeOf<T>` 用 GN 写得出？

**状态**：已落地（2026-11-05，见 §5）。裁定 `d9b509aec` 说 `tensor:shape` 的返回类型要说出长度，做法是「在 `stdlib/types.cml` 里用 GN 写一个类型函数，用 `match type` 拆开维度包」。这一页只问那一句做法里的前提：**数组类型的维度列表，`match type` 今天拆得开吗**。下面 §1–§4 是不改任何代码时量出来的读数与当时的默认；§5 是裁定与落地后的读数。

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

## 5. 裁定与落地（2026-11-05）

作者裁定（`080778522`），逐字：

> **`ShapeOf<T>` 写成 `match type T { case E[...Dims] => int[<Dims 的长度>]; case E[...] => int[] }`**
> **包的长度用值层拆包已有的写法取出（`indices ws` 的个数之类，不另造新语法）。**
> **`ElementOf`、`WithElement`、`RowsOf` 今天是 C++ 原语，理由是"`match type` 拆不开"。拆得开以后，这三个也改用 GN 写在库里，删掉 `typecall.cpp` 里的登记。同一类事只走一条路。**

**做法。** `case E[...Dims]`：元素位置写一个新的普通名字，数组类型就被一次拆成"元素 + 各维"——元素绑到那个名字，各维绑成维度包 `Dims`（`src/core/type/typevar.cpp#takeArrayApart`，模式登记在 `include/camel/core/type/typecall.h#MatchTypeArm` 的 `arrayPatterns`）。维度包写在一个维的位置上表示"它有几个成员"，即秩：`visitExtent` 见到绑定过的维度包，就在那里写 `ExtentForm::linear({{name, 1}})`（`src/compile/gct/builder.cpp`）。`case E[...]` 是开秩（秩没写出来），没有可拆的。`case float[...]` 这类**元素位置写类型名**的写法仍是身份模式，选择语义不变。

**两条独立答案**（`test/cases/compile/match_type/array_dims.cml`，plan `match_type.array_dims`，nvm/fvm 两个后端都过）：`ShapeOf<float[2, 3]>` 是 `int[2]`、`ShapeOf<float[2, 3, 4]>` 是 `int[3]`、`ShapeOf<float[3]>` 是 `int[1]`、`ShapeOf<float[]>` 是 `int[1]`（秩写着，长度没写）；`ShapeOf<float[...]>` 是 `int[]`（秩开着，长度说不出）。**反判据**：`case E[...Dims]` 不选 `int`（探测用的 `DimsProbe` 落到 `bool`），也不选 `float[...]`（开秩归 `case E[...]`），所以"选得中"不是"凡是数组都选得中"。

**没有改掉的一处（要作者答）**：`ShapeOf<T>` 的第三臂 `case _ => int[0]` 是**默认**，所以 `ShapeOf<string>` 也答 `int[0]`。想让它拒答，就得在 `case` 上加守卫（`case _ when is_numeric::<T>() => …`）——那是新语法，而 `match type` 今天**没有**守卫位置（`docs/handover/reviews/questions-2026-10-30-match-type-guard.md`：在 `match type` 里写 `case _ when` 报 `Expected '=>' before 'when'`）。在类型层另列一张"数值类型"的名单可以做到，但那是把 `is_numeric` 的判断复制到类型层，同一类事两条路，不做。一个标量答 `int[0]` 本身是对的（秩 0 的张量形状是空列表），所以要裁的是**"非张量的叶子类型"要不要在类型层被区分出来**，不是 `ShapeOf` 的写法。

**`tensor:shape` 的声明**已是 `export func shape::<T>(t: T): ShapeOf<T>`（`stdlib/tensor.cml#shape`），形状随类型的秩定长，`extent_of` 与它不再同型（`test/cases/compile/transforms/extent_of_is_not_shape.cml` 里两边都显式放宽成 `int[]` 再比）。

**三个原语改为库函数**（单独一个提交）：`ElementOf<T>`、`WithElement<A, E>`、`RowsOf<T>` 现在是 `stdlib/types.cml` 里的 `match type`，`src/core/type/typecall.cpp#kTypeFunctions` 里只剩 `PayloadOf`、`KeyOf`、`ValueOf`、`ParamsOf`、`WithOf`、`ResultOf`、`Tangent`。`ElementOf` 的集合那一臂写成 `case set => KeyOf<T>`，因此 `KeyOf` 放宽到集合（集合的元素就是它的键）。

**留下的**：§2 点名的三处 C++（`sumToInfer`、`shapeListType`、`shapeInfer`）这一次没有动，它们仍在声明之外说形状；`RowsOf`/`WithElement` 对**带前缀的开秩**（`f32[3, ...]`）会丢掉前缀（`case E[...]` 不绑前缀），这一点按裁定后的写法接受。