# `extent_of` 到底答「形状」还是答「某一维的长度」？

## 问什么

作者 2026-11-03 19:35 的 ③ 要求 `extent_of`，判据是「在开秩 `T[..]` 上，`extent_of` 能在编译期给出形状里
**一维**的长度」。名字、参数、返回类型都没定。要裁的是：它答整个形状（`int[]`），还是答一维（`int`，
再带一个轴参数）——以及一个类型参数还是两个。

## 读数：今天谁在答这个问题的哪一半

| 名字 | 声明处 | 答什么 | 什么时候答 | 要什么 |
|---|---|---|---|---|
| `shape` | `stdlib/tensor.cml#shape`（张量）、`stdlib/array.cml#shape`（数组） | 整个形状，`int[]` | 类型把每一维都定死时**编译期折叠**；否则运行期读值 | 一个值 |
| `numel` | `stdlib/tensor.cml#numel`、`stdlib/array.cml#numel` | 元素个数，`int` | 同上 | 一个值 |
| `dim<axis>` | `stdlib/tensor.cml#dim`、`stdlib/array.cml#dim` | **一维的长度**，`int`；负轴从最后一维数 | 同上 | 一个值 + 一个 `with` 轴 |
| `extent_pair` | `stdlib/rulesets/jvp.cml#extent_pair` | `[position, length]`，`int[]` | 运行期（它就是 `[p, l]` 的拼写，为的是律的右边不能写列表字面量） | 两个 `int` |
| `extent_of` | **不存在**：`git grep extent_of` 只命中 `docs/handover/reviews/` 的两处提案 | — | — | — |

四条实测（`residual` @ `34b659107`，`camel` + `std::opt`，2026-11-04；用例在
`test/plans/feat/compile/extent_of.plan.toml`）：

1. `t: f32[2, 3]` 上 `shape(t) = [2, 3]`、`dim<0> = 2`、`dim<1> = 3`、`dim<-1> = 3`、`numel = 6`。
2. 同一段程序过 `std::opt` 之后 GIR 里**一个 `tensor:dim` / `tensor:shape` / `tensor:numel` 都不剩**：
   类型把每一维都定死时，答案在**编译期**就是常量（`modules/tensor/ops/layout_ops.cpp#shapeFold`
   + `src/passes/opt/generic/generic.cpp#foldFromTypes`）。**判据的「编译期」那一半今天已经成立。**
3. 同一个值放到 `t: f32[..]` 形参后面：数字一样对（`[2, 3]` / `2` / `3` / `6`），但过 `std::opt` 后
   `tensor:dim` 还剩两个、`tensor:shape` 和 `tensor:numel` 各剩一个——它们**运行期读值**才答。
4. 轴越界不默认：`dim<2>` 在秩 2 上停在 `RuntimeDiag::RuntimeError: dim: dim axis out of range`。

所以缺的**不是**「一维的长度」这个答案——`dim<axis>` 已经在答，且秩已知时编译期就答完了。缺的是**从一个
类型、不带那个值**去问：`T[..]` 的形参后面没有值可用（律的那一侧更没有——`stdlib/tensor.cml` 头注：
「`shape`、`numel`、`dim` 仍然从一个把每一维都定死的类型折叠」，而律的 operand 在转置侧没有值，
author 2026-11-03 11:10 与 17:35）。`stdlib/rulesets/transpose.cml#mul_tensor_t` 的右边读的就是
`x`、`w` 这两个**线性 operand** 本身，作者说的「不要零载体」指的是这里。

## 三条路

1. **加一个核心算子 `:op/extent_of`，一个类型参数，答 `int[]`**：`extent_of::<A>()`，`A = f32[2, 3]` 答
   `[2, 3]`，`A = f32[..]` 明确拒绝（类型没固定任何一维）。代价：`src/builtin/operators/` 一格 + 注册 +
   `--verify-ops` + 重新生成 `docs/stdlib-reference.md`，且 `stdlib/tensor.cml` 要加声明。收益：律的右边
   第一次能问类型。（提案与代价在 `docs/handover/reviews/2026-11-04-does-a-shape-in-the-type-reach-the-law.cn.md`；
   本页不重复它的论证，只补身份表与折叠读数。）
2. **只在 `stdlib/tensor.cml` 里加一个答一维长度的 `extent_of(t, axis)`**：今天就能写（`shape(t)[axis]`），
   但它**仍然要一个值**，所以对律零收益；它只是 `dim<axis>` 换了轴的写法（`with` 变普通 operand）。
   代价：多一个与 `dim` 同义的名字。**不推荐**：同义名字会让「问形状」这件事有两个拼写。
3. **不新增名字**：律要形状就让它把形状当**已知 operand** 一路带过去（`stdlib/tensor.cml#pick_at`
   与 lane-sumshape 的 `sum_stated` 那一族已经这么做）。代价：只有带前半的律能这么写；
   一个裸的 `transpose(g)` 没有前半，就没有地方放那个 operand——正是第 1 条要补的洞。

## 默认（本轮按此落）

**路 1，答整个形状（`int[]`），一个类型参数。** 理由：`zeros`/`full`/`sum_to` 收的都是 `int[]`，
一维长度 `int[]` 下标就能取；反过来拿一维长度拼形状则要一个拼接算子。`T[..]` 上答「类型没固定任何一维」
并拒绝，而不是答空列表——空列表会被 `sum_to` 当成秩 0 的形状，那是一个静默的错数。
本轮**没有**落这条（`src/builtin/operators/` 不在这条车道的许可里）；落的是它的读数：身份表、
折叠与运行期两条用例、越界即拒绝的用例。`extent_of` 的名字留给路 1 的实现者，本轮不占用。