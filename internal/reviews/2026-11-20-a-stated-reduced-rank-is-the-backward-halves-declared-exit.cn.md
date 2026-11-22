# 写明降维结果秩的函数，为什么把 `vjp` 挡在「反向半边的出口类型」上

**问句（一句）**：`sum<0, false>` 的导数在开秩写法下全绿，而**同一个函数把降维结果的秩写进签名**（`func lane(x: float[2, 2]): float[2]`）时 `vjp` 停在
`The value the graph 'split_of_jvp_of_lane_backward' hands on has type 'float[...]', and the graph declares 'float[2]'`——
反向半边的**声明出口类型取自前向半边写明的秩**，而 lane 之和的伴随写出的是开秩张量。**补这一步该由「法则把答案的秩写出来」承担，还是由 split 的出口检查「在声明秩处接受开秩答案」承担？**

## 默认建议（本轮按此开工）

**由法则侧承担，本线不动 split 的检查。** 理由：`split` 的出口检查是**把错型变成一句拒绝**的那一处；放宽它就是把「拒绝」换成「一个带着声明秩、实际是开秩的值」，
正是作者 2026-11-20 那条口谕里不许的那一步（不拿断言兜、不拿调用方兜）。而法则手里本来就有 `extents` 端口，它**有能力**把自己的答案写成已知秩——
缺的是「怎么写」：`sum_axis_stated_t` 今天用 `lane_spread` 把余切铺回去，铺的过程最后落在 `full(extents, 1.0) * reshape(...)` 上，这个复合的类型是开秩的。

⇒ 所以本线的默认是：**先把它记成一条有主的既有缺陷（与 `stated_mean_axis_*` 三条同源），等 rank-zero 那条车道把法则侧的「答案形状」写法定下来**；本线只提供读数与机制，
不碰 `modules/transforms/split.cpp` 的检查，也不给任何计划加 `known_fail`。

## 一、两次读数（2026-11-20 06:47，`0024b76b4` 的树，Release 安装，`std::nvm`，`--trace transforms`）

同一段程序、同一个算子，只差签名里写不写降维结果的秩：

```
① func lane(x: float[2, 2]): float[...] { return sum<0, false>(x) }
   open-rank pullback=[[1.0, 1.0], [1.0, 1.0]]          ⇒ exit 0，答案正确

② func lane(x: float[2, 2]): float[2]  { return sum<0, false>(x) }
   [Error]: The value the graph 'split_of_jvp_of_lane_backward' hands on has type
   'float[...]', and the graph declares 'float[2]'; the value is node 7.
   （其后还有一句以 "A return is a position with an expected type" 开头，运行器的 [stdout] 回显在 400 字处截断）
                                                        ⇒ exit 1，**没有数值产出**
```

⇒ **⇒ 关键事实：这一格不产出错值，它产出一句拒绝。** 所以它**不是**作者 2026-11-20 06:20 那条「四个元素全是未初始化内存」的读数所能来自的地方
——那条读数给的是四个数，而这一格给的是一个类型句子。

## 二、机制：反向半边的声明出口类型，来自前向半边写明的那个秩

`split` 从**提升函数的类型**切两半，`modules/transforms/split.cpp#splitHalvesWith`：

```
Type *result  = exitPair->typeAt(0);      // 前向半边的答案：(值, 切线) 的第 0 个
Type *tangent = exitPair->typeAt(1);      // 第 1 个 —— 就是前向半边**写明的**那个秩
...
halves.backward = FunctionType::create(param_vec_t(), tangentParams, tangent)
```

⇒ 前向半边写明 `float[2]`，提升后的切线类型就是 `float[2]`，于是 `split_of_jvp_of_lane_backward` **声明自己的出口是 `float[2]`**。
⇒ 而反向半边实际交出来的值，是法则 `stdlib/rulesets/transpose.cml#sum_axis_stated_t` 用 `#lane_spread` 铺回去的那个张量：
`full(extents, 1.0) * reshape<lane_shape(extents, axis)>(ct)`——**`full(extents, …)` 的形状是一个运行期值，所以这个复合的类型是 `float[...]`**。

⇒ 两句话撞在出口上。检查在 `src/runtime/graph_edges.cpp#takeExitTypeMismatch`，以 `include/camel/runtime/graph_exit_type.h#GraphExitTypeMismatch` 送出，
在过遍边界时翻成 `RuntimeDiag::ExitTypeMismatch`（`include/camel/core/error/diagnostics/messages/runtime.h#ExitTypeMismatch` 的名字与
`src/core/error/diagnostics/messages/runtime.cpp` 的正文）。**检查本身是对的**：那张图标了 `float[2]`，却交出一个 `float[...]`，它无法证明两者相同。

⇒ **⇒ 所以缺的不是检查，是「法则把 `extents` 变成自己答案的秩」这一步。** `sum_axis_stated` 之所以叫 stated，正是因为它带着 `extents` 端口
（`modules/tensor/ops/reduction_ops.cpp#sumAxisStatedInfer` 用这个端口算答案类型）；转置后的那条法则同样拿得到 `extents`，却没有用它把答案的秩写出来。

## 三、这不是新缺陷：三条 `known_fail` 已经钉着同一堵墙

`test/plans/feat/compile/stated_extents.plan.toml` 里 `stated_mean_axis_gradient`、`stated_mean_axis_element_gradient`、
`stated_mean_axis_answered_by_the_engine` 三条 `known_fail`，原文：

> The engine leg is green and stays pinned … What it cannot do yet is read the library leg beside it, because `split`'s backward graph
> is checked against the forward half's declared return type and the rank-zero answer arrives as `float[...]` there.

⇒ **⇒ 逐字同源**：checker 一样，`float[...]` 一样，差别只是那三条的算子是 `mean_axis`、形状撞的是秩 0，本条是 `sum_axis`、撞的是秩 1 的 `float[2]`。
⇒ 所以本页**不是报新缺陷**，是同一处缺陷在 `sum<0, false>` 上又一次显形，而且这次给了它一个更小的题材（一个 2×2 张量、一个秩 1 的答案）。

## 四、两条路，各自的代价

| | 谁改 | 代价 |
|---|---|---|
| **法则侧**（默认） | `stdlib/rulesets/transpose.cml` | 要一种「用 `extents` 把答案写成已知秩」的写法。`sum_axis_stated_t` 手上有 `axis`、`keepdims`、`extents` 三个端口，秩由这三者完全决定，缺的只是表达方式；`reshape` 的法则在 `62fcd3040` 之后已经绑定「节点自己写的形状」，是不是够用要那条车道判 |
| **split 侧** | `modules/transforms/split.cpp#splitHalvesWith` 与出口检查 | **不许**：放宽检查等于把「拒绝」换成「带声明秩的开秩值」，而那正是设计原则第 9 节反复拦住的一类改动（拿断言或调用方兜住一个本体没算出来的东西）。**本线不碰。** |

⇒ 一个附带的观察，供 rank-zero 那条车道参考：`halves.backward` 的出口类型取的是**前向半边写明的秩**，而不是「参数的类型」。
对 `float[2,2] → float[2]` 这种形状，反向半边的答案**本来**就是 `float[2,2]`（与前向的输入同形），声明里写成 `float[2]` 这个事实说明
**反向半边的出口类型不是从「它交什么」推出来的，是从「前向声明了什么」抄过去的**。这一条与 `stated_mean_axis_*` 三条的读数完全一致，
而它也是「补一句 `take_stated` 就能好」这个想法的依据——**但要不要这么补，是那条车道的裁定，不是本线的。**

## 五、对 `sum<0, false>` 的导数意味着什么（本线的正面读数）

这条拒绝**不影响**本线钉住的那些数：它们全部用开秩写法读出，而且四后端一致（`test/cases/compile/transforms/sum_axis_lane_derivative.cml`
与 `sum_axis_in_a_training_step.cml`，2026-11-20 06:00 起在 `feat/compile` 里绿）。本线据此认为：

```
· lane 之和的**导数**是对的（`[[1.0, 1.0], [1.0, 1.0]]`、`[2.0, 2.0]`），四个后端逐字一致
· lane 之和的**签名**（写不写降维后的秩）是另一件事，撞的是 split 的出口类型，不是这条法则的算术
· 本页的这条拒绝在任何写法下都不产出数字 ⇒ 它解释不了「四个元素全是同一个未初始化值」那条读数
```

---

**已答（2026-11-21 移入 `reviews/`）**：答在 `questions-2026-10-29.md` 的「负责人回报（2026-11-20 09:10）」一节，「四、`sum<0,false>` 那条线索已完全关闭」那一段（第 5738 行，量于 `33d19e6ed`）：本页的默认建议被采纳 ——「由法则侧承担，不放宽 `split` 的检查」。正文不在此复制。
