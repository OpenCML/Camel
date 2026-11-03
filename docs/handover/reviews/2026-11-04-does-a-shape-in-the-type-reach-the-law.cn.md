# 作者裁定要的「形状在类型里」，律读不到：`T[n]` 的 `n` 不是值

## 问什么

作者 2026-11-03 16:15 的裁定说，`transpose(g)` 每个参数的形状**由它的类型确定**（`float[2, 3]`），
否则拒绝。这一条在**规则层**没有落地通道：律要把形状交给 `zeros`/`full`，而 `zeros` 只收一个
`int[]` **值**；具名维度 `T[n]` 里的 `n` 在律的右手边不是值。

**要作者裁的是**：给一条「类型 → 形状值」的通道（下面路 1，默认），还是承认下标转置律只能读那个
数组的值、把裁定的第一支限定在 `transpose(g)` 的构建检查上（路 2）。

## 三条来源，逐条读数

### 一、形状写在类型里（`float[3]`）：今天拿到的是数值对，不是形状

新用例 `grad_array_fixed_shape`（`both(xs: float[3]) = xs[1] * xs[0]`，`vjp` 路径）给

```
value = 6.0
d/dxs[0] = 2.0
d/dxs[1] = 3.0
d/dxs[2] = 0.0
```

但同一棵树上的构建读数是

```
TRANSPOSE function=rulesets.jvp::index_into<float> graph=388 type=(int, float[], float) => (float[],) nodes=3 arms=0 rhs=idx_arr_t
TRANSPOSE function=split_of_jvp_of_both_backward graph=348 type=(float) => (float[3],) nodes=11 arms=0 rhs=-
```

成员是**三个**形参，第三个是那个数组的**值**：extent 写在类型里没有让它少一个形参，回答它的是 `split`
带过来的残差，不是类型。把 extent 硬读成值（`export rule idx_arr_t::<T>(xs: T[n], i: int)`，右手边
`scatter_into(zeros_at::<T>(n), i, ct)`）时模块直接拒：

```
error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'n'.
    --> stdlib/rulesets/transpose.cml:352:64
help: Make sure the name is declared and in scope.

warning[SemanticDiag::DimensionNameUsedOnce]: The dimension name 'n' is written once in this signature, so it relates nothing.
help: A name relates the extents that write it. If the size is not a relation, write '?' instead; if it is, write the name in another parameter or in the return type.
```

「写得一次就什么也不关联」是同一条事实的另一面：维度名让类型**不产生实例**，所以它在实例里也不成为值。

固定形状**不解除**值的依赖，而且拿不到值时不是一句诊断。同样的函数走裸 `transpose`：

```
func pick(x: float[3]): float { return x[1] }      // main: let back = transpose(pick); back(1.0)

TRANSPOSE function=pick graph=56 type=(float[3], float) => (float[3],) nodes=3 arms=0 rhs=idx_arr_t
TRANSPOSE op ... resolved=(f: (x: float[3]) => float pure) => (float) => (float[3],) transposed=(float) => (float[3],) graph=(float[3], float) => (float[3],) same=no

Camel Invariant Violation:
    Guarantee  : nargs.size() == normPorts.size()
    Reading    : FastVM indirect call norm-arity mismatch for graph 'transpose_of_pick': expected 2, got 1.
```

即：多出来的形参在调用点没有实参，程序停在一条**不变量**上 —— 与 `transpose_loop_escape.two_values_of_two_kinds`
那条长红是同一族（`caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports`）。

### 二、`T[]`，从 `ct` 与已知值推：推不出来

- 下标读的余切是**元素**（rank 1 读一个位置，结果 rank 0）：`type=(int, float[], float) => (float[],)`
  的第三个形参是 `float`，不带任何 extent。任务里那句「`index_into` 的余切形状就是数组的形状」对
  rank 1 **不成立**，这是本条的量法。
- rank 2 连前向律都到不了：

```
transforms: jvp has no forward rule for `:op/idx_arr` (the rule takes (float[], int), and the operator has (float[2, 3], int) (of which 2 is a data operand)).
```

所以「从 `ct` 和已知值推」在这一条上退化成「从那个数组的值推」——没有第二个已知值。

### 三、两者都不行时拒绝：本车道落不下来

- 裁定要的那句话在 `modules/transforms/transpose_operator.cpp#refusalSentence`，本车道不许碰
  `modules/transforms/`（另有车道上 `split.cpp`、`transpose.cpp`）。
- 在律里「拒绝」= 让 `idx_arr_t` 不存在，`grad_array_no_int` 会从 `6.0`/`[2.0, 3.0]` 变成一句拒绝，
  违反本车道的硬判据（这两行必须逐字节不变）。所以拒绝这一半只能在 `transpose_operator.cpp` 里做。

## 三条路

1. **加一条「类型 → 形状值」的通道（本页默认）**。最小形式是核心一个算子
   `:op/extent_of`（吃一个类型参数，答 `int[]`），律写 `zeros::<T>(extent_of::<A>())`；`A` 是律自己
   命名的整个数组类型 —— `matmul_t::<T, A, B>` 已有先例（"the operand types whole -- their ranks live
   in them"，实例化时 `A` 就是 `float[3]`）。代价：`src/builtin/operators/` 一格 + 注册 + `--verify-ops`
   + 重录 `docs/stdlib-reference.md`；好处：本页第一、二条一起解决，且 `sum_t`、`take_t`、
   `mean_axis_t`、`mul_tensor_t` 四面同一面墙共享（清单见
   `2026-11-03-how-does-a-transpose-law-receive-the-length-it-may-not-read.cn.md`）。
2. **只做构建期拒绝**（`transpose_operator.cpp`）：`transpose(g)` 构建时检查每个参数的形状，定了就过、
   没定就用裁定的话术拒。代价：一处 C++；不解除 `cotangent-needs-a-value`，但它是裁定那句话的**验收面**，
   与路 1 不冲突，可以先落。
3. **已知操作数带长度**（`tensor:element_at`，`lane-knowlen` 已实现并停在 `split` 的环残差）：律只读
   `at = [i, n]`。代价：一个模块算子 + 两条律；上一车道的读数是 7 条转绿、`split` 一条绿转红；它满足
   11:10 那句「只读已知操作数」，但**不**满足 16:15 的第一支（长度仍来自那个数组的值）。

## 默认（本轮按此落）

**路 1 是落点，本轮不写核心**：律保持原样（`zeros_like(xs)`，值从闭包里的残差来）——它今天是**对的**，
只是「值可以没有」这一条没满足；把它改坏去换一句拒绝是本末倒置。本轮落下的是判决性的东西：

- 新用例 `grad_array_fixed_shape`（`test/cases/compile/transforms/grad_array_fixed_shape.cml`）：
  裁定第一支的**数值**面，逐位置断言，第三位 `0.0` 是「余切不能只由读过的两个位置造出来」的读法。
- 本页。

`transpose_operator.cpp` 里的拒绝（路 2）归能碰那个文件的车道；路 1 的核心一格归能碰
`src/builtin/operators/` 的车道。

## 读数（主机 88zjwei，树 `~/camel-work/idxt` = `lane-idxt`，tier fast，jobs 4，load 18.12/23.23/23.41）

| 计划 | 改前 | 本笔 |
| --- | --- | --- |
| `feat/compile/transforms` | 91 = 88 通过 / 2 失败 / 1 已知失败 | 92 = 89 通过 / 2 失败 / 1 已知失败（新用例 `grad_array_fixed_shape.fvm` 通过） |

两条长红在改前改后是同两句话（本笔没碰律）：

```
[FAIL] transpose_loop_escape.two_values_of_two_kinds.nvm
    Runtime indirect call norm-arity mismatch: caller graph 'main' node 3 has 1 args, target graph 'transpose_of_read_outer' has 2 norm ports
[FAIL] transpose_loop_escape.second_state_of_the_same_loop.nvm
    [stdout] 7.0 ..., [Error]: transforms: split does not build this function (this loop is on the chain the exit...)
```

`transpose_hand_closed_lambda.same_answer_as_the_cut.nvm` 改前改后都通过（`cut ct1= (6.0)` /
`cut ct3= (18.0)` / `hand ct1= (6.0)` / `hand ct3= (18.0)`），`grad_array_no_int.fvm` 改前改后都是
`6.0` 与 `[2.0, 3.0]`。