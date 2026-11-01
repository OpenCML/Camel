# 嵌套分支停在 `jvp`，不是 `split`

**日期**：2026-11-02。**状态**：开路（下一片）。**量的是**：`split.cpp:938` 那条 deferred（"一只臂的
半边没有类型"）今天走到没有。

## 最小程序

```cml
func f(x: float): float {
    return if x > 0.0 then (if x > 1.0 then x * x else 2.0 * x) else 3.0 * x
}
```

读 `vjp(f)(2.0)`（推导式：`vjp = transpose . split . reinterp<jvp>`）。

## 今天停在哪一行

```
REINTERP jvp function=f skip=branch
(vjp)…/stdlib/vjp.cml:56:35: [Error]: An error has occurred: transforms: jvp has no forward transform
for this function.
```

**停在 `jvp`，比 `split` 早一级**，所以 `split.cpp:938` 这一片今天根本走不到。

## 缺的是哪一半

`jvp` 的分支那一支（`jvp.cpp` 的 `Brch` 处理）要求**每只臂的结果**都已经有切向：

```cpp
if (arm == nullptr || arm->result == rt::kInvalidNodeRef ||
    !copied[arm->result] || tangentOf[arm->result] == rt::kInvalidNodeRef) {
    complete = false;
    break;
}
```

嵌套 `if` 的臂，其结果是**内层的那只 `Brch`/`Join`**；而 `tangentOf` 只给"被复制过去的值"配切向，
没有给**一个 `Brch`/`Join` 本身**配切向的地方——分支那一支只处理**最外层**的那只 `Brch`。于是
`tangentOf[arm->result]` 是 invalid，`complete = false`，读到 `skip=branch`。

所以缺的那一半是：**"臂里的臂"要能递归地长出它自己的 `(value, tangent)` 对**——即 `jvp` 的分支处理要
对"臂的结果本身是分支"再做一次同样的处理，而不是只认"臂的结果是一个值节点"。这一半补齐之后，
`split.cpp:938` 那句话（"一只臂的半边没有类型"）才会真正轮到：那时臂的结果是一个 pair 类型的内层
`Join`，`split` 要决定它的半边类型是什么。

## 不做什么

- 不在这里改 `total`/分母，也不把这条形状的用例移进 `test/cases/modules/autodiff`（那是人的决定）。
- 不给嵌套分支编一个"混合两只臂"的答案：暂停的条件是**内层分支实际跑了哪只臂**，那是一个值，不是
  一个可以近似的东西。

## 想补这一半时撞到的东西（2026-11-02 停在这里）

按"给内层 `Brch`/`Join` 配切向"去改，第一步就发现它**不是给已有的值加一个字段**，而是**要一个新的值**：

- `jvp.cpp` 的分支那一支（`for (ref…) if (kind == Brch)`）已经把**每只臂的结果**替换成 `liftedPair(copyOf[arm->result], tangentOf[arm->result])`——一个 `(value, tangent)` 的二元组，注释说"臂的出口和循环体的出口都是这一对，所以只在一处制造"。
- 于是分支的 `Join` 的 `withInputs` 是**一堆 pair**，而它的 `dataType` 仍是标量。**它的切向不是某个现成节点的一半**：哪只臂真的跑了，切向就是那只臂的 pair 的第二元；要把它取出来，只有两条路：
  1. **再造一个 `Join`**（同一只 `Brch`、同样的臂、输入是各臂 pair 的第二元），把它的结果记成 `tangentOf[join]`；
  2. 给 `Join` 的 pair 增加一个"取第二元"的节点。
- 两条路都要在分支这一支里**新增一处 `tangentOf[join] = …` 写入**，并且**新增节点**（一个镜像 `Join`，或一个取元节点）。这已经超出"把已有规则记到调用节点上"那类改动的形状：它是**在规则展开里再走一遍同一只分支**，第二次读"哪只臂是哪只"。

按 15:50 的硬要求（**只有规则一处来源**），我停在这里没有动代码：**这一处该不该是第二处 `tangentOf` 写入点，是作者的话**。要问的具体一句是：

> 分支（`Brch`/`Join`）的切向，是"同一只 `Brch` 上再 `Join` 一次各臂的切向"（镜像 `Join`，O(1) 新节点），还是"把臂的 pair 当值、由 `Join` 自己产出 pair"（改 `Join` 的类型与解构）？前者更像今天的形状，但它确实在分支这一支里**多一处切向写入**；后者要把 `Join` 的 `dataType` 改成提升后的类型，动的是 `Join` 的语义。

在作者答这句之前，嵌套分支保持 `skip=branch`（**有名字、有理由**），不拿"近似"或"混合两只臂"顶上。