# `log_softmax_jvp`：注释写 `dx − Σ(p·dx)`，实现写 `dx − p·Σdx`，哪条是准则？

**日期**：2026-11-20
**提出者**：合并车道（`lane-merge-3`，基线 `origin/develop` = `f6fccdcf8`）
**来源**：本页这条是从一支**未合分支** `lane-softmax-axis`（`5810292c4`）的内容发现里捞出来的，**发现者是合并车道，不是实现车道**。该分支未合、未推：我在本树 `git merge --no-ff` 合过，看到这一条后 `git merge --abort` 撤回，`develop` 停在 `f6fccdcf8`。
**状态**：等作者裁定。**默认建议：改实现**（第 5 节写死）。
**读数的限制**：本页**没有一条读数是跑出来的**。主机 `10.126.62.88:22` 从 2026-11-20 10:43 起 TCP 与 ICMP 全部不可达（第一笔合并的 gpu 计划刚跑完时还通），所以「哪条公式对」是**推导**、「用例分辨不出」是**读用例源码**。凡本页引用的代码都写明树名（`f6fccdcf8` = `origin/develop`、`5810292c4` = 那支未合分支）。代码按符号引用。

## 1. 两条逐字

同一笔里，**注释**说一条公式，**实现**算另一条。都出自 `stdlib/rulesets/jvp.cml`，在 `5810292c4` 上。

`#jvp_log_softmax` 的文档注释：

```
/// `d(log_softmax<axis>(x)) = dx - sum<axis, true>(exp(y) * dx)` with `y = log_softmax<axis>(x)`:
```

`#log_softmax_jvp` 的体：

```
export func log_softmax_jvp::<T>(x: T[...], dx: T[...], axis: int): T[...] when is_float::<T>() {
    return dx - exp(log_softmax<axis>(x)) * sum<axis, true>(dx)
}
```

注释：`dx − Σ_axis(exp(y)·dx)`。实现：`dx − exp(y)·Σ_axis(dx)`。**乘与求和的位置换了。**

## 2. 哪条对：`I − 1pᵀ` 的 `1` 在左，所以广播的是那个标量

`log_softmax(x)_i = x_i − LSE(x)`，逐元素偏导 `∂y_i/∂x_j = δ_ij − p_j`，其中 `p = exp(y) = softmax(x)`。于是

```
J = I − 1 pᵀ        ⇒   dy_i = dx_i − Σ_j p_j dx_j
```

`1` 在左边，两侧合起来是一个**标量** `pᵀdx`，它的值被广播到每一位上。⇒ **注释那条是对的**：
`dx_i − Σ_j p_j dx_j`，也就是 `dx - sum<axis, true>(exp(y) * dx)`。

实现给的是 `dx_i − p_i · Σ_j dx_j`：它把 `p_i`（第 i 位自己的概率）放到了本该由 `Σ_j p_j dx_j`（全体加权和）占的位置上。**两条只在 `p` 沿轴均匀时相等**，因为那时 `p_i` 是常数、可以提到求和号外面。

## 3. 这不是「一直如此」：这一笔把一条对的换成了错的

`f6fccdcf8` 的 `#jvp_log_softmax` 逐字：

```
export rule jvp_log_softmax::<T>(x: T[...]) when is_float::<T>() {
    log_softmax<0>(x) => (dx: T[...]) => (dx - sum<0, true>(exp(log_softmax<0>(x)) * dx))
}
```

**那正是注释那条公式**，只是困在轴 0（rank-1 输入的唯一轴，它自己的注释也这么写）。`5810292c4` 做了两件事：把轴一般化（对，是 `jvp_reshape` / `jvp_permute` 的形状），以及把 `sum(p·dx)` 换成 `p·sum(dx)`（错）。**第二次改动没有出现在任何一句提交说明里**，而它比第一次重要。

## 4. 现有用例为什么看不见：`x` 全是零，`p` 就均匀

`5810292c4` 的两个新用例都把 `x` 取成 `float[2, 2, 2]` 全零（`softmax_axis_jvp.cml`、`log_softmax_axis_jvp.cml`），于是 `p = 1/2` 处处相等，两条公式在**这个点上**给出同一个数：`32.0 / 8.0 / 2.0` 三条全对。`docs/stdlib-reference.md` 的 `@example` 同理：`log_softmax_jvp([0.0, 0.0], [1.0, 0.0], 0)` 两条公式都答 `[0.5, -0.5]`。

`log_softmax_axis_jvp.cml` 的手算过程里能看见这一刻：它写

```
t_e = dx_e - exp(y_e) * sum<axis,true>(dx)_e = dx_e - (dx_e + dx_p) / 2
```

**等号右边的那一步就是在代入 `x = 0`**（`exp(y_e) = 1/2` 是常数，所以可以乘进求和号里，也可以乘在外面）。推导与实现是同一条错公式的两处写法，而读数校验不了它。

一把能分辨的尺子（供新用例用）：取 `x = [1.0, 2.0, 3.0]`、`dx = [1.0, 1.0, 1.0]`、轴 0。正确公式给 `dx_i − Σ_j p_j dx_j = 1 − Σp = 1 − 1 = 0.0`，**逐位精确为 0**；错的那条给 `1 − 3 p_i = [0.7299082804888587, 0.2658145868356071, -0.9957228673244654]`。这组数在树里已经算好了 —— `test/cases/compile/transforms/log_softmax_gradient_through_the_engine.cml` 的注释用它记 `grad` 的正确答案，那里 `n = 3`、`p` 由 `[1,2,3]` 得来。差 0.9 量级，不是一个容差能糊过去的。

## 5. 参考文档是怎么来的：生成器只渲染 `export func`，所以它连正确那条都没有

`docs/stdlib-reference.md` 在 `5810292c4` 上是 `node scripts/stdlib-reference.mjs --write` 的输出（提交说明写的就是这句，没有人手改）。它**只渲染 `export func`**：在 `f6fccdcf8` 上它有 `#### reshape_jvp`、`#### mean_axis_stated`，**没有任何一条 `#### jvp_…`** ——rule 的散文不进参考文档。

所以它这一笔新增的 `#### log_softmax_jvp` 只带一句转述：

```
The tangent the rule above writes: `exp` of the recomputed `log_softmax` (`p`), the tangent's sum
over the axis, and the product subtracted from the tangent element by element.
```

转述的是**实现**那条形状（`p` 乘在求和外面）；正确公式所在的段落挂在 rule 上，**一个字都不渲染**。⇒ 参考文档既不能当准则，也不能当旁证：它对着实现，而实现对着的是一个没写下来的问题。

## 6. `softmax_jvp` 那一侧是对的，别一起改

```
return softmax<axis>(x) * (dx - sum<axis, true>(softmax<axis>(x) * dx))
```

`softmax` 的 jacobian 是 `diag(p) − p pᵀ`，于是 `dy = p ⊙ (dx − Σ(p·dx))`，与实现逐字相符。**只有 `log_softmax` 这一侧错**。

## 7. 问题

**`log_softmax_jvp` 的实现 `dx − exp(y)·sum(dx)` 与它自己注释里的 `dx − sum(exp(y)·dx)`，哪条是准则？**

## 8. 三条路与代价

- **A（默认）改实现**为 `dx - sum<axis, true>(exp(log_softmax<axis>(x)) * dx)`。代价**几乎为零**：`x = 0` 时两条公式同值，所以今天 4 条读数（`32.0/8.0/2.0` 与 `@example` 的 `[0.5, -0.5]`）**一行不动**。要付的只有一条新用例：`x` 非均匀、`dx = 1`，期望按第 4 节手算（正确为 `0.0`，错的为 `1 − 3p`）。这笔新增读数的期望要作者批。
- **B 留实现、改注释与参考文档**。代价：把一条只在均匀点上成立的 JVP 立为准则。`stdlib` 是用户程序导数的来源，错数在非均匀输入上**不报错、只错值**——这是设计原则里最贵的一类。
- **C 整笔退回**，轴命名与切线分两次做。代价：`*_stops_at_split` 两条已经按「倒着走停在新 callee」改写了，退回要连那四句一起回滚；而轴命名本身是上一页的默认建议（`2026-11-20-a-law-that-states-its-angle-answers-only-that-angle.cn.md`），方向没错，**不该连坐**。

## 9. 默认建议（写死，作者可推翻）

**走 A**：改 `#log_softmax_jvp` 的体，**同一笔里**重跑 `node scripts/stdlib-reference.mjs --write`（参考文档是生成物，别单独提一笔 `chore: regenerate`），并补一条 `x` 非均匀的用例把这一层钉住。`#softmax_jvp` 不动。

## 附：我第一轮把方向读反了

我发现这一条时先报的是「文档错、实现对」，理由是「`J = I − p 1ᵀ`，所以 `dy = dx − p·Σdx`」。错在把 `1` 和 `p` 放反了：`1` 在左 ⇒ 广播的是标量 `pᵀdx`。这一段留在页里，因为它就是这一整类错误的形状 —— **推导写错时，读数照样对**（`x = 0` 什么都分辨不出），于是「我量过了」这句话会替一条错的推导背书。