# 闭包成员的余切应不应该有一个槽？

**状态**：待裁；我方默认见第 3 节（**甲：给闭包成员一个槽**）。

写这页的直接原因：`lane-knownarg`（`d1aa176ee`）删掉"按参数位置从闭包取值"那一段时，需要一条判决——**一个只由闭包成员和常量算出来的值，要不要给它磨一条律去收余切？** 按位置取值没了以后，手的闭包 `(dx) => 2.0 * x * dx` 就卡在这里（`skip=cotangent-needs-a-value uri=:op/mul_d position=0`：给 `2.0*x` 那一份答案要读 `dx` 的值，而 `dx` 是参与微分的参数）。我试了"闭包成员的余切不进答案"，结果**不是拒绝，是静默的零**，见第 1 节第 3 条。

## 1. 读数（实测，含树、时间、负载）

树 `/home/zjwei/camel-work/knownarg`，宿主 88zjwei（32 核），2026-11-04 02:1x，1 m load 14.47/15.58/16.94。

1. **今天的代码里没有任何一条规则说闭包成员收不收余切。** `modules/transforms/transpose.cpp#buildTransposeMember` 的 `wantsCotangent` 只看两种端口：`withPortOf` 里的（回 `false`，注释写明"答案按普通参数给，权重不是参数"）和 `normPorts()` 里的（回 `participatesOf(i)`）；**闭包端口既不在这两处，就落到函数最后那句 `return true`**。所以闭包成员今天收余切，是**兜底那一行**的结果，不是写下来的判决。

2. **余切确实经由它进了答案。** `transpose_tuple_both_slots`（`both(x) = let t = (x, 2x); t.0 + t.1`，读数 `3.0` 与 `(3.0)`：值 `3x`、pullback 3）和 `transpose_no_tangent_tuple_member`（`k(x) = let t = (x, 3); float(t.1) + t.0`，cotangent 1 与 3 读 `(1.0)` 与 `(3.0)`）在**转置后的成员里 `x` 是闭包端口**：它的值由调用方填进闭包，`Transpose<F>` 的答案元组按参数排。

3. **把"闭包成员不接收余切"写成判决后，两条用例的读数掉成零，而且是静默的：**

   | 用例 | 改前（期望、实测） | 改成"不接收"后实测 |
   | --- | --- | --- |
   | `transpose_tuple_component.two_reads_of_one_tuple_add` | `3.0` / `(3.0)` | `3.0` / **`(0.0)`** |
   | `transpose_no_tangent.tuple_member` | `5.0` / `(1.0)` / `(3.0)` | `5.0` / **`(0.0)`** / **`(0.0)`** |

   退出码仍是 0，**没有拒绝、没有诊断**，只是答案变 0。已回退，未入库。同一次试验里另有 14 条用例从绿变红（`transpose_runs_a_loop_rounds_backwards`、四条 `transpose_loop_escape.*`、`transpose_unread_state_answers_zero.jit/.prl` 等），说明"不接收"这条判决**不是一处小改**：它把循环/胶带那一路也一起翻了。

4. **手的闭包今天是拒绝，不是零**：`TRANSPOSE function=__g_318__ skip=cotangent-needs-a-value uri=:op/mul_d position=0 reason=the rule's right side reads a value the caller does not carry`；同一条用例的 `split` 半边读 `cut ct1= (6.0)`、`cut ct3= (18.0)`，是对的。

## 2. 两条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：给闭包成员一个槽** | `Transpose<F>` 的答案元组按"**参数 + 闭包成员**"排（今天的兜底行为写成规则），`wantsCotangent` 对闭包端口不再落到 `return true`，而是"它在答案里有没有槽" | **答案形状变了**：所有按位置读答案的地方都要改——`transpose.cpp#transposeSignature`/`buildTransposeMember` 造答案处、`modules/transforms/split.cpp#answersAPair` 里 `valueArguments / 2` 的对半切（它一改，19 条 `feat/transforms/split` 用例的读数都过这一处）、以及每个 `Transpose<F>` 调用点的 arity 检查。**且必须同时裁一句**：`Vjp<F>` 的**可见**形状随不随之一变（若随，用户会看到多出来的成员；若不随，`Vjp` 只是内部丢弃，答案形状与 `Transpose` 分叉） |
| **乙：闭包成员的余切不进答案** | `wantsCotangent` 对闭包端口回 `false`，答案仍按参数排 | **两条用例的数字要改**（第 1 节第 3 条的 `(3.0)`→`(0.0)`、`(1.0)/(3.0)`→`(0.0)/(0.0)`），**改前要能说清它们今天为什么进**——按第 1 节第 1 条，今天是**兜底那一行**让它进的，**没有设计写在后面**；而按第 1 节第 3 条，这条路一翻就是 16 条（实测），不是 2 条。手的闭包**仍然拒绝**（"闭包成员无槽 ⇒ 不必磨律"只解掉 `2.0*x` 那一份，`sum_t` 那类"读参与微分操作数的形状"仍在） |

第三条路（今天先不做）：`Transpose<F>` 只对**线性**操作数要律、对值位置不问——但那要求图里"谁是线性的"有标记，手写的闭包里没有，等于回到"身份/字段/位置"那一类，作者 17:35 已经否了。

## 3. 我方默认

**甲。** 理由（沿用派工时的原话）：**"今天已经在进答案"是事实上限——把事实写成规则，比把规则写成事实便宜**；而且 `(3.0)` 这样的数字是**量出来的**，不是猜的；乙这条路要改一个量出来的数去迁就一条没人写过的规则。

甲要动的面，我按第 2 节数到的最小集是：**2 条用例的答案元组 + `split.cpp#answersAPair` 一处机制**（那一处覆盖全部 19 条 split 用例的读数，但是同一处改动）。**阈值写死：若落地时发现除这 2 条之外还要改超过 20 条用例的期望，就改乙**，并在 `Vjp<F>` 那一句里写清"答案按参数、闭包成员的余切丢弃"。

## 要作者答的一句

**「`Transpose<F>` 的答案按参数给，闭包成员的余切没有槽；但今天的代码是靠 `wantsCotangent` 的兜底 `return true` 让它进的，实测去掉就是静默的零（`(3.0)`→`(0.0)`，两条用例）。闭包成员的余切应不应该有一个槽？我默认『甲：给它一个槽，答案元组按参数 + 闭包成员排』，并请同时裁一句：`Vjp<F>` 的可见形状随不随之变？」**