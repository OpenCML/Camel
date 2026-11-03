# 问题：引擎的反向表要不要给 `pick_at` 补一条规则？

**日期**：2026-11-04
**状态**：待裁；我方默认见第 3 节（**甲：本车道不做**），本轮按默认开工，已提交。

## 1. 读数（本车道树，`stdlib/rulesets/jvp.cml` 加 `jvp_pick_at` 之后）

同一个读 `read(x) = pick_at(x, [1, 3])`，`x = [1.0, 2.0, 3.0]`，方向 `dx = [0.5, 0.25, -1.0]`：

| 腿 | 结果 |
|---|---|
| `jvp`（规则层前向） | `forward value=2.0 tangent=0.25`，`REINTERP … rules=jvp_pick_at` |
| `vjp`（`transpose(split(jvp(f)))`） | `reverse value=2.0 dL/dx=[0.0, 1.0, 0.0]`，`agreement 0.25`（= `sum(dL/dx * dx)`） |
| `grad`（引擎 `autodiff:grad`） | **仍拒**：exit 1，`autodiff: no derivative rule for operator 'tensor:pick_at' of type 'float': a differentiated value passes through it, so its gradient cannot be dropped silently` |

原因不是律：引擎的反向半读 `DerivativeRegistry`（`modules/autodiff/pullback.cpp`），它认识的读是 `:op/idx_arr`（`src/builtin/operators/derivative.cpp`）；`pick_at` 是同一个读的第二种拼写（`modules/tensor/ops/layout_ops.cpp` 的 OpDef），那里没有 `setVjp`。规则层的律答不了它——`jvp_pick_at` 是 `rulesets.jvp` 里的规则，那条路读的是 C++ 表。

## 2. 两条读法都自洽

- **一处拼写一处规则**：`pick_at` 只服务规则层（`jvp_idx_arr` 的线性半），用户写读用 `x[i]`，引擎已经能对它求导。那么引擎不认识 `pick_at` 是"内部算子的必然"，不是缺口。
- **一个读两种拼写就都该能求导**：`pick_at` 是 `stdlib/tensor.cml` 的公开名字（`exports = {"pick_at"}`），用户写得出来；同一个意思两种写法一半能求导一半不能，和 `take`／`idx_arr` 的分工不是一回事。

## 3. 三条路

**甲（默认）：本车道不动，留给一条允许改 `modules/tensor` 的车道。** 代价：一行注册（`setVjp(defs, "pick_at", …)`，即 `pick_at_t` 的 C++ 孪生：按 `at[1]` 造零、按 `at[0]` 散入），加一个用例；在这之前"直接写 `pick_at` 再 `grad`"仍是明确拒绝。这条拒绝已被钉在 `test/plans/feat/compile/transforms.plan.toml#pick_at_gradient_refused_by_the_engine`，注册落地那天它会红并说出替代它的东西。

**乙：把 `pick_at` 降成内部算子**（`exports = {}`，像 `expand_axis`），用户只剩 `x[i]`。代价：`jvp_idx_arr` 的线性半仍要发它（规则右值不受出口限制），`pick_at_t` 也仍要留；参考页少一行公开名字，且"读一条 lane 的梯度"这条最自然的写法要从 `pick_at` 改口。

**丙：就在本车道加那条注册。** 代价：`modules/tensor/**` 是本次任务的禁区（八条车道今晚在 `stdlib/rulesets/transpose.cml` 上撞过），且它要与 `pick_at_t` 两个地方说同一件事——两处会分叉。

## 4. 未测点

- 补了引擎规则之后，`grad` 与 `vjp` 两腿对同一个读是否逐位相同（现在只有规则层两腿相同）。
- 加权那一步（`sum(pick_at(x, at) * w)`）落在 `mul_tensor` 的伴随上，与本题无关，今天仍拒。