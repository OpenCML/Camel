# 一次读错「哪棵树」的上报，以及它教会的两件事

**日期**：2026-11-20
**本页的原样**：本页由合并车道（`lane-merge-3`）写在 `b2d5170b4` 上，标题是「`log_softmax_jvp`：注释写 `dx − Σ(p·dx)`，实现写 `dx − p·Σdx`，哪条是准则？」。**那个标题的前提是错的**：两条式子不在同一棵树上。本页由审计车道（`lane-law-doc`）在 `8b34a1deb` 上**整页重写**；文件名保持不动（`agents/branching.md` 的先例：正确地原地改，让后来的人看见被推翻的那条路与走到的那条并存），**所以文件名里那句「the comment sums p·dx, the code scales by p」是旧前提的名字，不是本页的主张**。
**状态**：等作者裁定（§7 一句）。**默认建议在 §6。**
**读数的限制**：本页**没有一条读数是跑出来的**。主机 `10.126.62.88:22` 从 2026-11-20 10:43 起不可达。所有引用都是**按符号读源码**，且每一条都写明它来自哪棵树：`b2d5170b4`（= `origin/develop`，主干）、`5810292c4`（= 本地分支 `lane-softmax-axis` 的 tip，**未合、未推**，检出在 `D:/Projects/Camel-wt/softmax`）、`8b34a1deb`（本页所在的本车道）。

## 1. 结论先行

1. **主干上没有这条矛盾。** `b2d5170b4` 的 `stdlib/rulesets/jvp.cml#jvp_log_softmax` 是一条 `rule`，右端逐字就是它文档那句公式：两侧同形，**正确**。主干上**根本没有 `log_softmax_jvp` 这个函数**（`*.cml` 里搜不到；这个名字只出现在本页原来的正文里）。
2. **主干上的「差一个广播」不是错，是转置关系。** 前向律写 `dx − Σ(p·dx)`（`J = I − 1pᵀ`），反向律 `stdlib/rulesets/transpose.cml#log_softmax_t` 写 `ct − p·Σct`（`Jᵀ = I − p1ᵀ`）。两条都对，互为转置，`#log_softmax_t` 的注释把这件事写出来了。
3. **原页引的那条「实现」在另一棵树上，而且在那棵树上确实错了。** `5810292c4` 的 `log_softmax_jvp` 体逐字是 `dx - exp(log_softmax<axis>(x)) * sum<axis, true>(dx)`——它把 `p_i` 放在本该由全轴加权和 `Σ_j p_j dx_j` 占的位置上。那是一个**待合分支里的真缺陷**，不是主干缺陷。
4. **上报错在「哪棵树」这一步，不在数学。** 把第 3 条报成第 1 条树的缺陷，于是「合并车道读错了文件、coordinator 未经核对就上报」这句话成立**只在树名这一层**：**主干的两条律都是对的，差的是转置关系**；错的是把分支的函数体当成了主干的实现。

## 2. 三个逐字，各带树名

**主干（`b2d5170b4`）的律**——文档与实现同形：

```
/// `d(log_softmax(x)) = dx - sum(exp(y) * dx)` with `y = log_softmax(x)`
export rule jvp_log_softmax::<T>(x: T[...]) when is_float::<T>() {
    log_softmax<0>(x) => (dx: T[...]) => (dx - sum<0, true>(exp(log_softmax<0>(x)) * dx))
}
```

**主干（`b2d5170b4`）的转置律**——`Jᵀ`，正确：

```
export rule log_softmax_t::<T>(x: T[...]) when is_float::<T>() {
    log_softmax<0>(x) => (ct: T[...]) => (ct - exp(log_softmax<0>(x)) * sum<0, true>(ct))
}
```

**分支（`5810292c4`）的函数**——这一条是错的：

```
export func log_softmax_jvp::<T>(x: T[...], dx: T[...], axis: int): T[...] when is_float::<T>() {
    return dx - exp(log_softmax<axis>(x)) * sum<axis, true>(dx)
}
```

同一笔里的 `softmax_jvp` 是**对的**（`softmax<axis>(x) * (dx - sum<axis, true>(softmax<axis>(x) * dx))`，即 `p ⊙ (dx − Σ(p·dx))`）。**两条律在分支上不一致：一条对，一条错。**

## 3. 上报错在哪一步

原页的证据是「我 grep 了 `exp\(y\)|sum<axis`，命中文档那一行，于是写下『文档行已独立确认在主干上』」。**命中文档行证明的是「文档存在」，不是「文档与实现不一致」——两个不同的命题。** 后一个命题要对**同一棵树**的**同一侧**取证：实现那一侧的 `log_softmax_jvp` 在 `b2d5170b4` 上不存在。

这一整类错误的形状值得单独记一句：**一条律的「文档 vs 实现」比对有两个端点，端点的树名是两个不同的名字，而 grep 只看见字符串。** 端点分属两棵树时，字符串照样能对上，比对却什么都不意味。

## 4. 原页对的那一半（留在页里，因为它属于分支）

`5810292c4` 的体确实是 `p·Σdx`，**这一半是真的**，我在那棵树上逐字读过。它为什么绿：那一笔的两个新用例（`log_softmax_axis_jvp.cml`、`softmax_axis_jvp.cml`）都把 `x` 取成 `float[2, 2, 2]` **全零**，于是 `p = 1/2` 处处相等，两条公式在这一点上同值——`log_softmax_axis_jvp.cml` 自己的手算就写着 `t_e = dx_e - (dx_e + dx_p) / 2`，「把 `exp(y_e) = 1/2` 提到求和号外面」那一步正是代入 `x = 0`。`docs/stdlib-reference.md` 的 `@example` `log_softmax_jvp([0.0, 0.0], [1.0, 0.0], 0)` 也同值：两条公式都答 `[0.5, -0.5]`（手算，两边同值）。

⇒ **这一条属于 `lane-softmax-axis`，由那棵树的树主处理**（它的检出在 `D:/Projects/Camel-wt/softmax`）。本页只记录它、不越界改它；本车道也没有改过 `stdlib/**`。

## 5. 它教会的两件事

1. **引用「文档 vs 实现」时，两个端点各带树名。** §3 那条。补一条硬要求：本页这类读数必须写成「`<树>` 的 `<符号>`」，否则一条未合分支的缺陷会以主干的名义存在下去。
2. **均匀输入让公式分辨不出对错。** 分支那两条用例落在 `x = 0`（`p` 均匀）上，读数绿而它证明不了这条律。**这与我在 71 行表里最重的那条同形**（§7 起三条）：`sum_axis_lane_derivative.cml` 给 lane sum 的全 1 切线，搬到极值上就会让一条错的律变绿。

## 6. 我在 71 行表里真找到的三条（本页的重心）

审计车道在 `f6fccdcf8` 上把 `stdlib/rulesets/jvp.cml` 的**全部 71 条律**逐条对过文档头（表在 `2026-11-20-when-a-laws-doc-formula-and-its-right-side-disagree.cn.md`：71 行、语义不一致 5 行、拼写不一致 2 行、「读数不分辨」5 行）。最重的三条：

1. **`#jvp_max_axis` / `#jvp_min_axis`（连同 `#jvp_max_axis_stated` / `#jvp_min_axis_stated`）：右端交出的是「切线的极值」，不是「极值处的切线」。** 右端是 `max_axis_stated(dxs, mask, …)`，而它的**值**是 `max<axis, keepdims>(t)`、**掩码不读值**（`stdlib/tensor.cml#max_axis_stated` 与 `modules/tensor/ops/reduction_ops.cpp#extremumAxisStatedKernel` 都这么写）。数学要的是选择算子 `S·dx`。**反方向是对的**（`stdlib/rulesets/transpose.cml#max_axis_stated_t` 正是 `S` 的转置），所以 `vjp`/`grad` 全绿，**只有前向错**；而按 `jvp(` 与 `max<`/`min<` 求交集，树里**没有一条用例读它的前向读数**，所以没有任何读数会红。
2. **`#jvp_softmax` 的轴。** 公式那一半两侧一致且已被非退化切线钉住（`rules_angle_args_softmax.cml`：写成 `y * (dx - sum(dx))` 会答 `-1.0`）；不一致的是轴（文档 "over the same axis" vs 写死 `<1>`），而唯一用例是 rank-2 上的 `softmax<1>`，恰好是律唯一能答的轴。顺带：它的注释「This law answers the wrong axis, silently」在本树已陈，`softmax<2>` 今天按名拒绝。
3. **驳回主干那条。** 即 §1、§2：`#jvp_log_softmax` 无罪；`#log_softmax_t` 无罪；两式之差是转置。

## 7. 默认建议（写死，作者可推翻）

1. **主干不动。** `#jvp_log_softmax` 与 `#log_softmax_t` 都不改（§1）。本页原文的「走 A 改实现」作废。
2. **把本页的编号搬对**：`5810292c4` 的那条读数转给 `lane-softmax-axis` 的树主——要么改体（`dx - sum<axis, true>(exp(log_softmax<axis>(x)) * dx)`，与主干同形），要么在那一笔里写明为什么留 `p·Σdx`。无论哪条，都补一条 `x` 非均匀的用例（`x = [1.0, 2.0, 3.0]`、`dx = 1`、轴 0：正确答 `0.0`，`p·Σdx` 答 `1 − 3p = [0.7299, 0.2658, -0.9957]`——这组数主干的 `log_softmax_gradient_through_the_engine.cml` 已经算过，`n = 3`；手算，未跑）。
3. **以 §6 第 1 条为主**：`jvp_max_axis` / `jvp_min_axis` 与两条 stated 孪生按「极值处的切线」重写右端，并补一条常切线之外的前向用例（`x = [[1.0, 3.0], [0.5, 2.0]]`、`dx = [[1.0, 0.0], [0.0, 1.0]]` ⇒ 正确 `[0.0, 1.0]`，今天的右端 `[1.0, 1.0]`；手算，未跑）。
4. **一条纪律**：一条律的读数要在「两式不同值的点」上取；全零、全 1、单元素只能证明没崩。本页原稿与 `sum_axis_lane_derivative.cml` 是同一个反例形状的两个样本。

## 8. 要作者回答的一句

主干两条律无罪、`5810292c4` 的 `log_softmax_jvp` 体是一条待合分支里的真缺陷（用例在 `x = 0` 上看不见）——按 §7 分头处理（主干不动、分支树主补一条非均匀用例、`jvp_max_axis`/`jvp_min_axis` 以前向读数为准重写），认不认？