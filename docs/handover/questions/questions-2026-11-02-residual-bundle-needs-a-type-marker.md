# 问题：`transpose` 认"残差包"靠猜，而类型系统里没有那个标记

状态：**等作者裁**（2026-11-02）。这一页只记事实与候选，不替作者选。

## 今天怎么认的

`modules/transforms/transpose.cpp`（`buildTransposeMember` 里，`drafts.setFuncType(signature)` 之后）：

```cpp
const bool bundlesResiduals =
    paramCount > 1 && dynamic_cast<const camel::core::type::TupleType *>(
                          function->funcType()->normTypeAt(0)) != nullptr;
const bool plainParameters = paramCount > 1 && !bundlesResiduals;
```

读法：**第一个参数是元组 → 这是 `split` 的 backward half，那个元组是残差包**；否则多参数的图就是"若干个 float 参数"。
`split` 那边造这个元组的地方是 `split.cpp`：

```cpp
Type *residualsType = TupleType::create(residualTypes);   // 残差的类型，一个普通元组
```

**两边没有共享任何标记**：`split` 造一个普通 `TupleType`，`transpose` 认"第一个参数是 `TupleType`"。`TupleType` 没有名字位，`GraphDraftGroup` 的成员名（`base + "_backward"`）是字符串，不是类型读得到的东西。

## 为什么这不是"类型识别"，是"数参数 + 猜"

- **`paramCount > 1` 是数出来的**，不是读出来的。backward half 的形状恰好是"1 个元组 + k 个切线"，`paramCount` 对它永远 ≥ 2，所以这个条件对真的 backward half 恒真——**它对假的 backward half 也恒真**。
- **一个普通函数只要第一个参数是元组，就被认成 backward half**。`f(p: (float, float), w: float) -> float` 的转置会把它第一个参数当成残差包去 `Accs .0/.1`，而它其实是一个用户写的元组参数。今天没有这样的用例，所以没有红灯——**红灯缺席不是证据缺席**，这是 P1-10 的原话（"用'第一参数是不是元组'来猜残差包"）。
- **反过来也错**：真正的 backward half 如果残差只有一个、且 `split` 决定不打包（单值 pullback 的形状），`paramCount > 1` 也不成立，它落到"第一个参数是 float"那一支。

## 三条候选，都不是今天能做主的

1. **一个带名字/标记的类型**（例如残差包用 `TupleType` 的一个子类型或带 tag 的实例）。**这是类型标记**，最贴 P1-10 的要求；代价是类型系统里多一个概念，而 `TupleType` 是通用的、被解析器和 CML 的元组字面量共用，好处是 `std::cml` 打印回源码时它仍是元组。
2. **图上的一个字段**（例如 `GCGraph::splitBackwardFrom()`）。**这条与原则 2.3 冲突**：图的数据模型只收语言层面的通用概念，`split` 的产物不是用户意图也不是类型，加字段要先证明"推不出、已有手段不够、且通用"。
3. **不做标记，改让 `split` 直接产出一个"已转置好"的成员**，让 `transpose` 根本不进这个分支。这把问题从"识别"挪到"谁负责"，代价是 `transpose(backward half)` 这条独立入口没了。

## 同一笔里已经做了的（与本页无关的那一半）

`split.cpp` 的三处奇偶位置（`halfInputsOf` 的 `i += 2`、两条拒绝里的 `index / 2`、`residualsOf` 的 `index / 2`）改成**按 `Lift<T>` 的类型读**：`valuePortsOf(liftedType)` 与 `parameterOfPort(liftedType, port)`，规则是"参数的类型有切线空间就贡献 `(value, tangent)` 两个端口，否则只贡献 value"。

**A/B 没能造出来，原因是硬事实**：`jvp` 在任何参数没有切线空间时先拒绝——

```
Invalid call to 'jvp' with argument types <> ((n: int, x: float) => float pure):
`jvp` takes a function whose every parameter has a tangent s...
```

所以"3 个参数、其中一个是 `int`"的用例**到不了 `split`**，今天 `Lift<T>` 只可能是成对的二端口形状，奇偶读法与类型读法在所有可达输入上**结果相同**（`split 16/16` 就是这句话的读数）。这不是"改了也没用"：它说明**奇偶读法的正确性一直依赖 `jvp` 的拒绝**，而这个依赖没有任何一处写出来。类型读法把它变成 `split` 自己按类型判断——`jvp` 的契约一旦放宽（例如允许 `int` 参数只带值），`split` 不需要跟着改。

## 作者要裁的

**残差包用什么当标记**（上面三条之一，或第四条）。在裁之前，`transpose.cpp` 那一行**不动**：它今天对可达输入是对的，动它而要引入类型系统里没有的概念，属于"用一个没定的东西换一个没证的错"。