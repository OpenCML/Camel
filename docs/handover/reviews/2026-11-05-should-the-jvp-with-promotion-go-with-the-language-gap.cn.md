# 删掉 jvp 里提升 `with` 值的过渡写法，三条用例的对数字会变成 0.0——这一件按什么次序做？

> **标记：本页的默认建议已被 `cb47d540d` 取代**——不是补 `with` 提升，也不是把提升挂到语言缺口上，而是
> `grad(f) = vjp(first_with_as_param(f))`，`jvp` 的 `with` 提升随之删除。本页下面的默认建议（"留着这段过渡
> 写法"）在读到这一行之后**不再有效**；它保留在原处，是因为它钉住的三个数仍然是删除时的验收读数。

2026-11-05，`lane-obey`，读数取 `ab84671c1`（`origin/develop` 的起点）。被催办的原话在
`d6206afe3`（`docs/handover/reviews/questions-2026-10-29.md` 末尾）：

> 用库层 `grad`/`with_grad` 组合起来，同一次合并里删掉 jvp 里提升 `with` 值的过渡写法，删掉总是拒绝的
> `with_grad` 占位，然后让 S2 用库层 `grad` 训练。

这一页只说第一件：**删掉提升**。它删不得，而"删不得"不是困难，是三个数。

## 1. 问句

**作者要求删掉 jvp 里提升 `with` 值的过渡写法，但实测删掉它会让三条钉着对数字的用例变成错数。这一项
是"留到语言缺口补上再删"，还是"改掉那三条用例的期望值"？**

它是问句、不是缺陷报告，因为两条路都自洽，代价只有作者能权衡：

- 作者自己给的判据是「**这些用例保持现在的数**」。按这条判据，删提升就是错的——它把三个手算过的对数字
  改成 `0.0`，而 `0.0` 与"梯度恰好为零"在输出里不可区分。
- 但催办要的是**这一项结束**，不是"这一项以后再说"。若作者认为那三个数可以改，这一项今天就能落地。

## 2. 读数（关掉提升后，三条用例的数）

**位置**：`modules/transforms/jvp.cpp#withIsPromoted`，与它决定的那一路
`modules/transforms/jvp.cpp#liftSignature`（`liftSignature(function, promoteWiths)`；`jvpCalleeSignature`
传 `true`，`jvpSignature` 传 `false`）。把 `withIsPromoted` 恒假（"删掉提升"的最小写法）之后：

| 程序 | 现在 | 关掉提升后 |
| --- | --- | --- |
| `vjp((ww) => weighted<ww>(2.0))(3.0)` 的 `grad` | `3.0` | `0.0` |
| 同一个 `weighted` 的两参数调用 `vjp((ww, t) => weighted<ww>(t))(3.0, 5.0)` 的 `pullback` | `(6.0, 3.0)` | `(0.0, 3.0)` |
| `step<0.5,0.75>(1.5,2.0)` 的 `w`（权重梯度） | `-0.75000000000000000` | `0.00000000000000000` |

- `weighted` 是 `with <x: float> func weighted(y) = x*y + x`（`test/cases/compile/transforms/library_grad_of_a_with_weight.cml`）。
  手算 `d/dw (x*y + x) = y + 1`，`w = 3.0`、`y = 2.0` 时是 **3.0**；关掉提升后答 **0.0**。
- 第二行的手算：`d/dw = t + 1 = 6.0`，`d/dt = w = 3.0`，所以 `(6.0, 3.0)`；关掉提升后权重那个成员变 **0.0**，
  `t` 的成员仍是 **3.0**——**一个成员错，整行看起来还像个梯度**，这比拒绝更难发现。
- 第三行是 `test/cases/compile/transforms/with_grad_composition_meets_the_split_wall.cml` 的 `step`：
  `err = 0.5*1.5 + 0.75 - 2 = -0.5`，`d(loss)/dw = err * x = -0.75000000000000000`；关掉提升后 **0.0**。

**这三处的"现在"一侧，是树上钉住的数**，逐字核对过：`test/plans/feat/compile/transforms.plan.toml` 里
`transforms.library_grad_of_a_with_weight` 的 `expect_stdout_contains`（`weighted engine=3.0 value=9.0 grad=3.0`、
`both value=18.0 pullback=(6.0, 3.0)`）与 `transforms.with_grad_composition_meets_the_split_wall` 的
`expect_stdout`（`value=0.12500000000000000 w=-0.75000000000000000`）。**"关掉提升后"那一列是
`lane-lgrad`（末笔 `48e0327c7`）在宿主上把 `withIsPromoted` 恒假后量的**：本车道在本机不构建，因此照抄它的
三个数，没有重跑。它报的第一行不是新事——`library_grad_of_a_with_weight.cml` 自己的注释写着 2026-11-04 之前
这个程序答的就是 `value=9.0 grad=0.0`，关掉提升只是把那一天请回来。

**机制一句话**：`withIsPromoted` 决定一个非 `var` 的 `with` 值在被调者抬升后的函数里算不算一个线性输入。
关掉它，抬升后的调用只拿得到 `with` 的值、拿不到它的切向（`liftSignature` 的注释：a lifted callee that kept
them a `with` list would have nowhere to take the direction in from），于是 `weighted` 抬升后对权重是常函数，
它的 `lin` 是零映射，`vjp` 的组合答 0.0。

**这段代码的身份不是规则，是桥**（作者 2026-11-04 07:15）：它挡住的缺口有两个，都不是"jvp 写得不好"：

1. `vjp` 的 lambda 体不能调用函数值 `f`；
2. lambda 的形参表写不出 `<w, ...Rest>`（`..xs` 作为 lambda 形参表 2026-11-02 起暂停，
   `docs/handover/reports/2026-11-02-a-lambdas-rest-parameter.cn.md`）。

## 3. 三条路，各自要改什么、改多大

- **① 留着这段过渡写法，把"它是过渡、它挡住什么、什么条件下删"写在代码旁。** *谁要改*：我方，今天可做。
  *改多大*：0 行行为改动，只在 `modules/transforms/jvp.cpp#withIsPromoted` 上方的过渡注释里点名那两个缺口
  （本轮已补，见第 4 节）。*代价*：桥继续存在，而它与 `stdlib/signatures/transforms.cml` 里 `Jvp<F>` 那句
  「a value that reaches a body through a `with` parameter does not move」在字面上并存——**所以那句"这是过渡、
  不是规则"的注记是这条路的全部代价所在**：注记一删，这段代码就会被读成一条语言规则。
- **② 删掉提升，同时改三条用例的期望值。** *谁要改*：那三条用例的拥有者 + 作者先改判据。*改多大*：至少
  `library_grad_of_a_with_weight`、`with_grad_composition_meets_the_split_wall`、`library_grad_is_the_composition`
  三处 `expect_stdout*`，`grad_first_with_parameter` 的四组数也在同一条链上。*代价*：**把三个手算过的对数字
  改成 0.0**。本仓库反复记为"错数比拒绝更糟"的那一格，就是这条路的落点；而且它同时违反作者自己给的判据。
- **③ 先补语言缺口，再在同一次合并里删提升 + 删注记 + 让 S2 用库层 `grad`。** *谁要改*：`jvp` 的调用侧与
  lambda 形参表（`..xs` 那一笔仍暂停）。*改多大*：一条语言能力，加上一次跨模块合并——**这不是在 `jvp.cpp`
  里删十行**。*代价*：这是唯一能让催办原话全部成立的路，也是唯一需要先改语言的路。

## 4. 默认（我方按此继续，不等回复）

**① 留着这段过渡写法**，并在代码旁写明它是过渡、它挡住的两个缺口是什么。

理由：作者给的判据是"这些用例保持现在的数"，而 ② 的唯一结果是把三个对数字变成 0.0（且 `0.0` 与"零梯度"
不可区分）；③ 要一条语言能力，不是这一笔能做的。本轮在 `modules/transforms/jvp.cpp#withIsPromoted` 上方补了
一句点名（`vjp` 的 lambda 体不能调用函数值 `f`；lambda 形参表写不出 `<w, ...Rest>`），**没有删代码，没有改任何
期望值**。

留给下一轮的判据（作者若裁定 ② 或 ③，按这个验收）：

- 若走 ③：`withIsPromoted` 的四处调用点（`liftSignature` 的值列表与 `with` 列表各一处、`promotedSlotOf`
  那一处、抬升后的调用按被调者签名填实参那一处）与被点名的那句注记**同一次删除**；
  `git grep -n withIsPromoted` 回到 0 命中，且那三条用例的 `expect_stdout*`
  与今天逐字相同——**数不变才算补上了缺口**，数变了就是又回到了 2026-11-04 那天。
- 若走 ②：作者要明确写"`with` 权重也答零"，否则这一页的默认继续有效。

**这一页不是把催办办完了。** 催办要的是三件代码改动，这一页一件也没做；它做成的只有一件事：把"为什么
删不得"变成三个可以自己跑一遍的数。