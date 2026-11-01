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
