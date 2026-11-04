# `sum_to` 收形状的版本：返回的秩从哪来？

**状态**：待裁；我方默认在第 3 节，**按默认做，不停工**。本页对应每日审查 B2 的下半句（"`sum_to` 加一个收形状的版本，替掉'用零数组当形状'"）。

## 1. 问题

今天 `sum_to_stated` 是这么写的（`stdlib/rulesets/transpose.cml`）：

```cml
func sum_to_stated::<T>(t: T[..], at: int[]): T[..] when is_float::<T>() {
    return sum_to(t, zeros::<T>(at))
}
```

`sum_to(t, like)` 的形状**读自 `like` 的类型**（`stdlib/tensor.cml#sum_to`：第二个操作数"Its second operand is read for its shape and for nothing else"）。于是一个只想要形状的律，必须先造一个**零数组**当形状的载体——作者 2026-11-04 的裁定是**这条要去掉**（B2："替掉'用零数组当形状'"）。

## 2. 两条路，与各自的代价

**甲：新开一个收形状的算子**（`tensor:sum_to_at(t, at: int[])`，`at` 是 `int[]` 值，不是形状载体）。它是 `sum_to` 的第二个真身，需要：算子注册、shape 推断、kernel、vjp、tensor 模块的声明——**五层**。好处是律里一个零都不出现。

**乙（默认）：让返回秩来自类型参数 `X`，形状来自 `at`**：

```cml
func sum_to_stated::<X>(t: X[..], at: int[]): X[..] when is_float::<X>()
```

秩由 `X` 静态给出，形状由 `at` 给，**两者都是已经存在的事实**，不需要新算子，也不需要读任何操作数的值。代价：`sum_to` 本身仍以 `like` 为载体，所以**零数组还在 `sum_to_stated` 体内**——只是它由 `at` 造、由 `X[..]` 定型，**不再是由一个操作数的值来当**。

## 3. 默认（照此做，除非被推翻）

**走乙，并且先做一件事：量 `ct_of_a` / `ct_of_b` 里 `sum_to(..., a)` 那一半到底需要"秩"还是"形状"。**

- 若只是**秩**（上一轮实测的 `SemanticDiag::ReturnNeedsConversion`：`float[..]` 比 `float[?, ?]` 松）⇒ 乙足够，`sum_to_stated::<X>` 的返回类型把秩钉住，律里换成 `sum_to_stated(ct * w, extent_of::<X>())`；
- 若还要**具体的每维长度**（例如 `matmul` 的结果秩依赖两个操作数的秩，而不只是类型参数）⇒ 记进本页，回到甲（新算子）。

## 4. 为什么要问

因为乙会**改变 `sum_to_stated` 的类型签名**（从 `T[..] → T[..]` 变成 `X[..] → X[..]`），凡是按今天签名实例化它的律都会跟着变；而这正是"闭包变长 = 别的用例的签名"那条教训的同一类：**类型层的一小步，可能是整棵树的一步**。这一步的形状（要不要新算子、返回秩从哪来）由作者定，比我替他选安全。

## 5. 与 B3 的关系

B3（律自己写 `extent_of::<X>()`，删掉 C++ 里"多给律一个端口"的加宽）**已经做完并且零红数变化**：四条律（`sum_from_type_t`、`take_from_type_t`、`mean_axis_from_type_t`、`idx_arr_t`）现在都从类型拿形状，`offeredExtents`/`extentsNode`/`valueOf` 的 extents 槽、`statedExtentsNodeOf`、`intArrayTypeOf` 全部删除，`grep` 0 处引用。**所以本页只剩"值怎么构造"这一问，形状那条已经不再是问题。**