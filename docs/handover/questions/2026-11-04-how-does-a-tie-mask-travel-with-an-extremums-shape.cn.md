# `max_axis` / `min_axis` 的孪生算子：并列极值的选择怎么随形状走？

**状态**：**甲已落地并量到**（第 1 节第 8 条，tip `3d95855c7`），此页留作读数与理由；这一页不再是待裁项，除非有人要改乙。

写这页的直接原因：派工要求把 `sum_stated` 的模式推到 `sum_axis` / `max_axis` / `min_axis` / `mean` 四个算子上。前两个与后一个都做完了，`max_axis` / `min_axis` 卡在一个不是"要不要写律"的问题上：**极值的余切不是形状问题，是"哪几个位置"的问题**。

## 1. 读数（实测，含树、时间、负载）

树 `/home/zjwei/camel-work/axistated`（tip `d0f1ae2d4`，宿主 88zjwei，2026-11-04 03:57 UTC-5，1 m load 14.27 → 14.77，宿主忙，读数不与静置机可比）。

1. **`sum_axis_stated` 通了两腿**：`test/plans/feat/compile/stated_extents.plan.toml` 8/8 绿（`total: 8, pass: 8, fail: 0`）。负轴用例的读数原样是 `sum_axis -1: value=15.0 engine=[[0.0, 0.0, 0.0], [1.0, 1.0, 1.0]] library=[[0.0, 0.0, 0.0], [1.0, 1.0, 1.0]] same=true`，走到的律是 `rhs=sum_stated_t,take_stated_t,sum_axis_stated_t`。
2. **`mean_stated` 通了两腿**：同一次跑里 `mean: value=7.0 ... same=true`，`rhs=mean_stated_t`。
3. **引擎的并列语义就是规格**：`modules/tensor/ops/reduction_ops.cpp#extremumAxisVjp` 算 `share = cast_like(strict_eq(x, expand(output)), expanded)`，再 `multiply(share, expanded)`——**每个并列位置都拿到整份余切，不摊分**。所以孪生律的读法只能逐元素选，不能"取第一个 argmax"，也不能"除以并列个数"。
4. **反向律读不到"带余切的算子的值"**：`sum(lane * lane)` 这一路在库侧停于 `skip=cotangent-needs-a-value uri=tensor:multiply position=0 reason=the rule's right side reads a value the caller does not carry`（`mul_tensor_t` 要给 `w` 的余切就得读 `x` 的值）。**推论**：掩码不能在反半段用 `x` 现算，必须由前半段作为**残差**带过来。
5. **`pick_at` 只有转置律、没有前向律**：`no derivative rule for operator 'tensor:pick_at' of type 'float'`——想"只读一条 lane"得走 `take`（`jvp_take` + `take_stated_t` 都在），这也是本轮两条用例的读法。
6. **"三个普通操作数会被拒"这条已经不成立**：`modules/transforms/transpose.h` 的 `TransposeRefusalCause` 里没有 `Arity`，`transpose.cpp` 里也没有任何 `skip=arity` 的发生点；`stdlib/tensor.cml` 第 116 行那句"第三个位置会在找律之前被拒"是旧事实。这条一旦成立，掩码就能作为**第三个端口**，不必挤进 `at`。
7. **`shape(bool[])` 不存在**：`shape` 的头部是 `when is_numeric::<T>()`，`bool` 不是numeric，所以律里量不到 `bool[]` 的形状。这是甲的关键约束——**掩码不能自证形状，形状只能从 `at` 读**。
8. **甲落地后的读数（并列用例两条，两腿）**：`max_axis tie: value=5.0 engine=[[0.0, 1.0, 1.0], [0.0, 0.0, 1.0]] library=[[0.0, 1.0, 1.0], [0.0, 0.0, 1.0]] same=true`；`min_axis -1: value=5.0 engine=[[0.0, 1.0, 1.0], [0.0, 1.0, 0.0]] library=[[0.0, 1.0, 1.0], [0.0, 1.0, 0.0]] same=true`。两个并列位置各拿整份 1.0，与第 3 节手算的期望值逐位相同；`stated_extents` 计划 10/10 绿。
9. 甲的两个未测点都通了：`bool[..]` 能写进声明、`ParamKind::BoolArray` 能注册成端口，且走图时 `bool` 操作数的**值**作为残差到了反半段（否则 `where(mask, ...)` 会以 `cotangent-needs-a-value` 拒，实测没有）。

## 2. 三条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：`bool[..]` 掩码作为第二个普通操作数** | `max_axis_stated(t: T[..], mask: bool[..], at: int[])`，`at = [axis, keepdims, d0..]`；转置律 `where(mask, lane_spread(ct, at[2:len(at)], at[0], at[1] != 0), 0.0)`；前半段用 `xs >= lane_spread(max_axis(xs, axis, keepdims), ...)` 造掩码（`>=` 与引擎的 `strict_eq` 在同一批值上选同一批位置，NaN 两边都选不到） | 三个普通端口，`bool[]` 没有切空间所以律只答一份余切；**两个未测点**：(a) 声明里的 `bool[..]` 能不能写、`ParamKind::BoolArray` 的核读不读得到操作数；(b) 走图时 `bool` 操作数的**值**会不会作为残差交给反半段（`bool` 没有余切，所以不是 `parameter-shape` 那一条，但"值能不能读到"没量过） |
| **乙：掩码塞进 `int[]`** | `at = [axis, keepdims, d0.., n, f0..f(n-1)]`，律里把 `n` 个标志变回 `x` 形状的掩码 | 需要"运行期维度 reshape + 整数比 1"，`strict_eq` 在 `stdlib` 里没有声明，`reshape` 的角参能不能吃运行时 `int[]` 没量过；**比甲多两个未测点，且会把 `at` 变成两种语义混在一个列表里** |
| **丙：不做孪生** | `max_axis` / `min_axis` 仍只有前向律，库侧转置照旧拒 | 库里 `max` / `min` 的梯度永远是红的一半；而 `lane-tranche2` 那种"前半段带掩码"的写法（`jvp_max` 用 `sum_stated(where(xs >= max(xs), dxs, 0.0), shape(xs))`）在**整张量极值**上已经能走，**只有沿轴的极值**需要这一页 |

## 3. 我方默认

**甲。** 理由：它把并列语义原样搬过来（`where` 逐元素、每个并列位置拿整份），只用现成的算子（`where`、`full`、`reshape`、`>=`），不新增任何 C++ 侧语义，也不动 `modules/transforms/**`；乙为了省一个端口把两种东西挤进一个列表，读数会更难讲。

**阈值写死**：落地时先只做一条读数——`max_axis_stated(t, mask, at)` 的最小用例（值 `x = [[1.0, 3.0, 3.0], [0.5, 1.0, 2.0]]`，`max_axis(x, 1, false) = [3.0, 2.0]`，`sum` 之后 `dL/dx = [[0.0, 1.0, 1.0], [0.0, 0.0, 1.0]]`，两腿 `same=true`）。若这一步的读数说**掩码的值到不了反半段**，就改丙（并在 `docs` 里留这条读数），不在甲的第二个变体上再试。

第 3 节那句期望值是**手算的**（并列两个位置各拿整份 1.0，第三个位置 0.0；第二行只有末位是最大值），落地前不要拿运行输出改它。

## 4. 落地了什么

甲在同一个 lane 里做完并量到（第 1 节第 8 条）：`max_axis_stated(t, mask: bool[..], at)` 与 `min_axis_stated` 的声明、`kDeclaredOperators` 登记、`modules/tensor/ops/reduction_ops.cpp` 的核（值走 `k::reduceAxis`，掩码不读）、`jvp_max_axis`/`jvp_min_axis`（掩码写成 `xs >= max_axis(xs, axis, true)`，内层 `keepdims` 让它对任何轴都能广播）、`max_axis_stated_t`/`min_axis_stated_t`（`where(mask, lane_spread(...), 0.0)`），以及两条两腿用例。

顺带改了一句旧话：`stdlib/tensor.cml` 里"第三个普通操作数会在找律之前被拒"已不成立，改成"范围共用一个列表是因为反向律要整份读它"，并注明两个极值算子就带第三个端口。