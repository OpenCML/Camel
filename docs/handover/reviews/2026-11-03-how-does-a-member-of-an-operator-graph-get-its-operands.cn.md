# 一页一问：算子图里的"成员"怎么拿到操作数？

## 实测事实

`transpose(f)` 修到今天，成员的捕获按 2026-11-02 17:10 的裁定走"捕获端口 + 闭包元组"，闭包**整份等于 `f` 的残余表**（`split.h#residualsOf`，与 `buildLinearClosure` 填 `lin` 用的是同一张表）。这条落地后：

- `withPorts` 归零、`with-arity mismatch` 全树 0；
- 但 `transpose` 数组族 6 条**仍然拒绝**，拒绝行自带原因：
  `skip=needs-a-value-outside-the-closure param=0 type=float[] candidates=0`
- 原因：`rulesets.jvp::index_into`、`autodiff::div_tangent` 是**算子图（operator graph）**，不是 `split` 切出来的半边 —— 它们的 `closureNodes=0`、`residualsOf` 对它们返回空，所以"从继承的闭包里取"在它们这一层**无处可取**。

## 问题

同一个"成员"概念在今天有两种载体：

1. **`split` 切出来的半边** —— 有残余表，闭包有内容，捕获这条路成立（今天已落地）；
2. **算子图里的成员**（`jvp` 的 `index_into`/`div_tangent` 这类）—— **没有残余表、没有闭包**，但它要的值确实存在（在外一层的图里，`capturesForMember` 那一处手上就握着）。

**要问的是**：第 2 类成员的捕获，凭据从哪来？可选：

- **(甲)** 算子图的成员也建一张残余表（谁来建、按什么顺序，需要一条规则）；
- **(乙)** 算子图成员的值由**建它的那一处**显式交接（`capturesForMember` 手上已有，写成闭包填充即可，不需要表）；
- **(丙)** 这类形状明确拒绝（今天的行为），把它写进语言参考的"转置"一节。

## 我方默认

**按 (乙)**：**建成员的那一处（`transpose.cpp#capturesForMember`）手上就握着调用点的实参**，直接按成员的捕获端口顺序填进闭包元组，不需要"残余表"这种中间凭据；**(甲) 会把两套编号混在一起**（今晚已经因为"两套编号"多花了两小时）；**(丙) 会把合法程序判死**。

**在裁定前不再动捕获这一层**（`b2f` 已停在这里，其余车道不许碰）。