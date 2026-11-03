# 前一半怎么取「这个调用的值分量」

**一句话**：2026-11-03 16:55 的裁定对 `split` 侧的要求是「按出处改掉调用目标 → 按内容」，并给了方向：**前一半保留调用、取 `ACCS TupleIndex 0`**。**这条方向在前一半现在的位置上装不下**：`splitHalvesWith` 给前一半写的参数表**只有值**（`(values…)`），而被调函数答对的那类调用**都吃一个切面里的切线实参**；保留调用就少一个实参，不保留就只能指着别的图。这是一个要作者裁的格，不是施工方可以两选一的地方。

## 实测依据（`lane-bycontent`，`origin/develop = 5e461c590`，改动见 `lane-bycontent` 前三个提交）

1. **前一半的参数表只有值**：`modules/transforms/split.cpp#splitHalvesWith` 里 `forwardParams.emplace_back(function->normTypeAt(i), false)`，`i` 走 `0 .. count/2`；后一半拿同一位置的切线。读数：`REINTERP split function=jvp_of___g_322__ … type=(float) => (float, (float) => float pure)`——抬起的 `(y, dy)` 切成前一半 `(float) =>`、后一半 `(float) =>`。
2. **要取「调用的值分量」的那个调用，吃的正是切线**：`jvp` 的路由 B（2026-11-03 15:15）把一次应用的对「注水」成 `Fill`，`Fill` 的第一格就是那次调用（本页所指的调用就是它）。`jvp_of_f` 里的 `jvp_of_sq(y, dy)`，`dy` 是**在图体内**由 `f` 的切线参数算出来的——前一半没有那个端口，也没有它的生产者。
3. **所以「保留调用 + `ACCS 0`」装不下**；而字面上的「按内容改调用目标」= 对被调函数**再切一次**（取它的前一半），那正是裁定自己点出的无限递归。
4. 现状（本 lane 未改）：`Half::retargets` 与 `makeCopy` 的重定向格仍读 `liftedFrom()`（`git grep -n liftedFrom modules/transforms/split.cpp` → 309、642 两处），把调用改成**直接调 `primal`**。这是「按出处」，裁定要换掉；**换掉的代价是上面那三件里选一件**。

## 三条路

- **甲：前一半也收切线实参。** 前一半签名从 `(values…) => (result, lin)` 变成 `(values…, tangents…) => (result, lin)`，于是「保留调用 + `ACCS 0`」直接成立。**代价**：`split` 的**语言可见类型**变了（`lin` 与 `transpose` 的调用点、`Vjp<F>` 的展开、`stdlib/vjp.cml` 与所有 `split` 用例的签名都跟着变）；**要作者点头**。
- **乙：对被调函数再切一次。** 前一半把调用指向 `buildSplitGraph(callee).forward`，再取 `ACCS 0`。**代价**：被调函数的前一半要**再切一遍**（多一张 `lin` 闭包被丢弃），且**递归函数会无限切**——必须加「正在切这张图」的栈，撞栈时按名字拒绝；另外每多切一张图，`graph=` 编号整体后移，钉了编号的用例要重钉。
- **丙：把被调函数的「值那半边」整段搬进前一半。** 按 `answersAPair` 从被调函数的出口往下走它的值根，把那段节点连参数替换一起复制进前一半（内联）。**代价**：这是「带实参替换的体内联」，本 lane 装不下；好处是**不碰任何签名**、也不递归切图。

## 默认建议（施工方默认）

**先按住：`retargets` 的重定向格原样留着，不动它，也不选上面任何一条。**

理由三条：

1. **三条都要作者点头**：甲改语言可见类型，乙改「一个调用切几次图」并牵动编号钉死，丙要一个新的内联能力。施工方自选哪一条都是替作者决定。
2. **按现状留下的东西是「按出处」，但它答的数是对的**：`feat/transforms/split` **19/19**、`split_call_one` 的期望逐字不变（本 lane 三个提交前后都测过）。按 **对 > 明确拒绝 > 错数 > 崩** 的次序，**先把「按出处」这块留着比换成一个没测过的机制安全**——裁定要的「按内容」在 `residualsOf` 的两处（本 lane 已改）与 `transpose` 的两条路径（已改、`liftedFrom` 归零）都已经落地，剩下的这一格是**动作**而不是判据。
3. **若作者要在甲/乙/丙里选，推荐丙**（其次乙）：丙不碰签名、不递归切图，代价是本 lane 之外的一次内联；乙是三者里唯一会让编号漂移、还要额外防递归的一条。

## 判据 / 哨兵（本次读数，主机 load 1m 14.06–20.96）

- `feat/transforms/split` **19/19**（改前改后同）；`feat/compile/transforms` **91 = 88/2/1**，与基线同。
- 两条长红仍拒绝、原文不变；`grad_array_no_int` 仍 `6.0` / `([2.0, 3.0])`；`transpose_hand_closed_lambda` 四行不变；`jax_has_aux` 未动（`answersAPair` 的签名侧把它挡在外面：一参数、`splitHalvesWith` 判奇数）。
- **`with_grad_composition_meets_the_split_wall` 仍红且理由相同**（`skip=forward`，闭包端口不是参数）——不许被这次改动弄绿。
- 剩余 `liftedFrom` 命中：`modules/transforms/split.cpp#Half`（`Half::retargets` 的判据）、`:642`（`makeCopy` 的重定向动作）；`include/camel/runtime/graph.h` 的 `kGraphRefLiftedFrom` 字段本体保留（清单第 6 项）。