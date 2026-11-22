# 问题：`pick_at` 该补一条**前向**律吗？

**日期**：2026-11-04
**状态**：待裁；我方默认见第 3 节（**甲：补 `jvp_pick_at`，一行**），本轮**未做**。

## 1. 读数

1. `pick_at` **有转置律**（`stdlib/rulesets/transpose.cml#pick_at_t`），**没有前向律**。对一个经过 `pick_at` 的值取 `grad`，报 `no derivative rule for operator 'tensor:pick_at' of type 'float'`。
2. `pick_at` 是**已声明**算子（`stdlib/tensor.cml`，名字在 `kDeclaredOperators` 里），所以它是用户写得出来的名字，不是内部细节。
3. 库里**已经有一处**产出了 `pick_at` 节点：`stdlib/rulesets/jvp.cml#jvp_idx_arr` 的右边是 `pick_at(dxs, extent_pair(i, len(xs)))`——那是"索引读"的线性半。一阶能跑，是因为一个律的右边不再被求导；但**作为节点它仍然没有前向律**。
4. 我的第一版用例用 `sum(pick_at(lane, [1, n]))` 读一条 lane 的梯度，撞上第 1 条；换成 `sum(take(lane, [1]))` 才有读数（提交 `141fdce08`）。

## 2. 这不是"设计如此"

`pick_at` 是**线性**的选择：它的伴随（把余切放回被选中的位置）已经写出来了（`pick_at_t`），说明它在导数体系里是"可以有两半"的。缺的只是**同样线性**的那么一行：`d(pick_at(xs, at)) = pick_at(dxs, at)`。

缺它的后果不是"慢"，而是**用户写得出、却对它取不了梯度**——按严重性次序（对 > 明确拒绝 > 错数 > 崩），这是"明确拒绝"，可以接受，但属于该补的缺口而不是该留的口子。

## 3. 三条路

**甲（默认）：补 `jvp_pick_at`，一行。** 右边直接 `pick_at(dxs, at)`：`at` 里已经带着长度（`at[1]`），所以**不需要**再造一个 stated 孪生，也不需要新的端口。
- 收益：口径和 `take` 对齐（`take` 两半都有：`jvp_take` 与 `take_stated_t`），且"读一条 lane 的梯度"这种最自然的读法不再需要绕道 `take`。
- 注意（**别混在同一个提交里**）：读出一条 lane 之后通常还要加权，那一步落在 `mul_tensor` 的伴随上，而那条律今天要读一个反半段没有携带的值（`skip=cotangent-needs-a-value uri=tensor:multiply position=0`，我在本轮撞到过）。所以补完 `jvp_pick_at` 之后，"pick 出来再乘"可能仍然拒——那是另一件事。

**乙：不补，文档写"要读单个位置就用 `take`"**。代价：用户看得见 `pick_at` 却没有它的前向律，同一个意思两种写法一半能求导一半不能。

**丙：把 `pick_at` 降成内部算子**（`internal(...)`，像 `expand_axis` 那样 `exports = {}`），只留 `take`。代价：`jvp_idx_arr` 的线性半要改，且用户少一个直接写法；`pick_at_t` 还得留（右边仍会产出它）。

## 4. 未测点

- 补了 `jvp_pick_at` 之后，`sum(pick_at(x, [1, n]))` 是否真能两腿逐位相同（没跑过）。
- `pick_at` 的 `at` 是 `[n, i0..]` 两段意思，前向律要不要把它当值读（`jvp_take` 的做法是把它交给孪生 `take_stated`；`pick_at` 没有孪生，因为长度已经在 `at` 里）。
- `pick_at_t` 与新的 `jvp_pick_at` 是否会让同一节点在二阶路径上出现两条律（没量过）。