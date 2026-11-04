# 乘积的转置还要不要一个零载体？

## 问什么

`rulesets/transpose.cml#mul_tensor_t` 的右半边要把 cotangent 归约回操作数的形状：`sum_to(ct * w, x)`。
`sum_to(t, like)` 的第二个参数是**值**（`stdlib/tensor.cml#sum_to`：`export func sum_to::<T, S>(t: T[..],
like: S): S`，形状从那个值的 slots 读），所以律需要一个 `x`。而裸 `transpose` 里 `x` 可能没有值
（`transpose(pick)` 的参数就是没有值的那个人）。今天走查给的答案是
`modules/transforms/transpose.cpp#shapeFromTypeOf`：把 `extent_of` 的答案（`#statedExtentsOf`）交给
库自己的 `tensor:zeros`，把这个**零数组**当 `x` 递过去。那是不是零载体？17:55 的裁定说不要零载体。

## 读数（2026-11-04，宿主 `88zjwei`，树 `lane-extentof` `1b76b4ff5`，load 16–20）

1. **`sum_to` 的第二参数是值，不是形状**：`stdlib/tensor.cml#sum_to` 的签名是 `sum_to::<T, S>(t: T[..],
   like: S): S`。**它不是 `sum_to_shape(t, shape: int[])` 的替代**——名字像，语义不同：一个吃一个值、
   从值的 slots 取目标形状，一个吃一个 `int[]`（今天不存在这个算子）。
2. **今天造零载体的地方**：`modules/transforms/transpose.cpp#shapeFromTypeOf`（注释自述 "the value a law
   that reads an operand for its **shape** and has no stated twin is handed -- `mul_tensor_t`'s
   `sum_to(ct * w, x)`"），调用点在同一个文件的参数回填处（`forwardOf[...] = carrier`）。
3. **可这条例程今天走不到**——量出来的，不是推的。探针 `/tmp/probe_debt.cml`：
   ```
   func energy(xs: float[3], w: float[3]): float { return sum(xs * w) }
   let t = transpose(energy)
   ```
   读数是 `TRANSPOSE function=energy skip=rule uri=tensor:sum reason=unbound`，然后是一条
   `RuntimeDiag::RuntimeError` 说 `tensor:sum` 这一格没有律可查。原因和那条 `known_fail` 同源：
   `tensor:multiply` 的结果类型是 `float[..]`，所以 `sum` 归约的那个操作数**类型里没有 extents**，
   `sum_from_type_t` 拿不到端口，走查在 `sum` 就停了，**根本到不了 `mul_tensor_t`**。
4. 所以零载体今天**不是**某个程序能走到的一步：能走到的是纯读形状的律（`pick_at_t` 那一类），
   `mul_tensor_t` 的那条是**潜在的**。可代码注释把它写成了 `mul_tensor_t` 的既成用法，而
   `ct * w`、`ct * x` 是**读元素**的乘法——零数组喂进去，`d/dw` 会得到 `0.0`，正是裁定要防的那种
   「错得没有诊断」的形状。**这一段的不一致就是这条债。**

## 三条路收敛成两条

**C（另立 `mul_tensor_from_type_t::<T, X, W, A>(x: X, w: W, at: A)`，头多一个端口）不是第三条路**：
右半边仍要一个"把值按**形状**求和"的算子，而 `sum_to` 的第二参数是值，`full(at, 1.0) * (ct * w)` 是广播
不是归约（答案会多出元素）。所以 C = B 的用法，只在 B 的算子存在时成立。

**A. 这一轮不动（本页默认）**
- 零载体留在 `#shapeFromTypeOf` 这一处；`mul_tensor_t` 不改。
- 代价：那句"不要零载体"在一处不成立，且注释自述的用法与 `ct * w` 读元素的事实不一致；今天没有程序
  能走到这处（读数 3），所以暂时不产出错数。
- 好处：没有一个今天答得出来的程序变红；不新增算子。

**B. 加 `sum_to_shape(t, shape: int[])`**
- `modules/tensor` 一个算子 + `stdlib/tensor.cml` 一条声明 + `mul_tensor_t` 改写成
  `sum_to_shape(ct * w, extent_of::<X>())`；零载体从这一处消失。
- 代价：动 `modules/tensor`（别人的车道）；`X` 是律自己的类型参数，走查把它绑成开秩时
  `extent_of::<X>()` 按名拒绝，**开秩的乘积转置会从"今天能答"变成"拒绝"**；新算子只服务一条律。

## 为什么问

A 与 B 都自洽，代价一个是"留一处与裁定字面相抵的机制"、一个是"让一类今天能答的程序变红并且动
`modules/tensor`"。这两个代价只有作者能权衡，所以按 A 记着，等 `modules/tensor` 有别的理由要动
`sum_to` 时一起做。

同题的地方清单在 `docs/handover/questions/2026-11-04-which-shape-reads-can-become-extent-of.cn.md`
（那一页回答"哪些 `shape(xs)` 能换成 `extent_of`"；本页只回答"这一处零载体怎么收"）。