# 三条反向律：把「线性操作数的形状」换成「已知实参」

**状态：已落，闸门读数见第六节。** 树 `D:\Projects\Camel-wt\sumshape`（`lane-sumshape`），基线
`4c744f258`。作者 2026-11-03 17:35 裁决的直接落地：
「`jvp` 规则在写线性部分时，把转置需要的原值信息（这里是 `xs` 的长度或形状）作为**已知实参**交给那个线性调用……
对应的转置规则只读这个已知实参来造零数组，不再读线性操作数」，并「同类规则一起查」。

## 一、改的是哪三条，各自「改前读什么 → 改后读什么」

三条规则得的是同一种病：转置律的右边要构造一个和被操作数同形状的数组，而形状只能从**线性操作数**身上读，可那个操作数在转置侧没有值。

| 律（`stdlib/rulesets/transpose.cml`） | 改前读什么 | 改后读什么 |
|---|---|---|
| `sum_stated_t`（原 `sum_t`） | `xs`：`spread_like(xs, ct)` = `full(shape(xs), ct)` | 只读 `at`：`spread_over(ct, at)` = `full(at, ct)` |
| `take_stated_t`（原 `take_t`） | `t`：`scatter_rows(zeros_like(t), indices, ct, 0)` | 只读 `at`：`zeros_of_shape(ct, at[1+n:len(at)])` 造底，`at[1:1+n]` 当索引 |
| `mean_axis_stated_t`（原 `mean_axis_t`） | `xs`：`ones_like(xs) * (ct / lanes)`，`lanes = numel(xs)/numel(ct)` | 只读 `at`：`full(at[2:], 1.0) * (ct / lanes)`，`lanes = Π(at[2:]) / numel(ct)` |

`git grep` 证据（树内实测）：

```
$ git grep -n "rule sum_t::\|rule take_t::\|rule mean_axis_t::" -- stdlib/rulesets/transpose.cml
（无匹配，exit=1）
$ git grep -n "zeros_like(t)\|ones_like\|shape(xs)\|shape(t)" -- stdlib/rulesets/transpose.cml
（无匹配，exit=1）
$ git grep -n -A2 "export rule sum_stated_t\|export rule take_stated_t\|export rule mean_axis_stated_t"
stdlib/rulesets/transpose.cml:354:export rule sum_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {
    sum_stated(t, at) => (ct: T) => spread_over(ct, at)
stdlib/rulesets/transpose.cml:469:export rule take_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {
    take_stated(t, at) => (ct: T[..]) => take_cotangent(ct, at)
stdlib/rulesets/transpose.cml:503:export rule mean_axis_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {
    mean_axis_stated(t, at) => (ct: float[..]) => mean_axis_cotangent(ct, at)
```

三条律的右边只出现 `ct` 与 `at`；三个辅助函数（`spread_over(ct, at)`、`take_cotangent(ct, at)`、
`mean_axis_cotangent(ct, at)`）的参数表里没有 `t`。

## 二、已知实参是什么形状，以及为什么是这个形状

正向半（`stdlib/rulesets/jvp.cml`）把形状算好交给线性调用：

| 正向律 | 线性调用 |
|---|---|
| `jvp_sum` | `sum_stated(dxs, shape(xs))` |
| `jvp_take` | `take_stated(dt, stated_rows(indices, shape(t)))` |
| `jvp_mean_axis` | `mean_axis_stated(dxs, stated_axis_shape(axis, keepdims, shape(xs)))` |

两条约束决定了它长这样，都是前一批（`lane-knownarg`，`4c744f258`）量出来的，不再重测：

1. **一个 `int[]`，不是两三个普通操作数。** 转置走查在查表之前就把「普通操作数不是 1 个或 2 个」的节点拒掉
   （`skip=arity uri=tensor:mean_axis operands=3`）。所以 `mean_axis` 的 axis/keepdims 和 `take` 的索引都塞进同一个列表。
2. **列表由函数拼，不写在规则右边。** 规则右边写带计算元素的列表字面量会读「写在它后面」的节点
   （`skip=rule uri=:op/idx_arr reason=the rule's right side uses a node written after it`），
   这就是 `extent_pair` 当初存在的原因。新增 `stated_axis_shape` / `stated_rows` 用的是同一个办法，拼接用 prelude 的 `concat`。

列表布局（三个算子各一，写在 `stdlib/tensor.cml` 的声明上）：

- `sum_stated(t, at)`：`at` = `shape(t)`，值就是 `sum(t)`。
- `take_stated(t, at)`：`at` = `[n, i0..i(n-1), d0..d(rank-1)]`，前导计数切分列表。
- `mean_axis_stated(t, at)`：`at` = `[axis, keepdims, d0..d(rank-1)]`，flag 写 `1`/`0`（列表只有一个元素类型）。

## 三、新增的算子（C++）

三个「申报形状的孪生」，每个都是 **2 个普通操作数** `(t: Tensor, at: IntArray)`，键能落在
`ruleKeyOf` 的 arity 2 上（`element_at` 是第一个这样的孪生，照它的做法）：

| 算子 | 值 | 形状检查 | 推理 |
|---|---|---|---|
| `sum_stated` | `sumAllKernel`（同一个 kernel，所以累加顺序、元素类型、只舍入一次都是定义自己的） | `at` 对 `shape(t)` | `wholeReductionDtype(Sum)` |
| `take_stated` | `k::take(input, at[1..n])` | 同上（偏移 `n+1`）+ 每个索引对第 0 维 | 开秩、操作数元素类型 |
| `mean_axis_stated` | `k::reduceAxis(Mean, t, at[0], at[1]!=0)` | 同上（偏移 2） | 开秩、`reductionDtype(Mean)` |

形状检查只有一处机制 `checkStatedExtents`（`modules/tensor/ops/support.cpp`），三个算子各按自己的偏移调用它；
声明了不等于验证过，所以它是**故障**而不是悄悄改形状。

`spec` 上唯一的搬家：`isIntegerCode`/`isIntegerType`/`intListArg` 从 `layout_ops.cpp` 的匿名命名空间提到
`support.h`/`support.cpp`（纯搬移，`reduction_ops.cpp` 也要读整数列表）。没有新增 `.cpp`
（`modules/tensor/CMakeLists.txt` 是显式源文件列表，新文件要改 CMake）。

## 四、`take_stated_t` / `sum_stated_t` / `mean_axis_stated_t` 的型式参数为什么是 `<T, X, A>`

转置走查按「余切的元素类型 + 每个操作数一个」的实例去建律（`modules/transforms/transpose.cpp#ruleCotangent` 把
`ctElement` 摆在最前）。只写 `<T>` 时 `take_stated_t` 直接读
`skip=rule-ranks ... reason=it takes 1 type parameter(s), and it was asked for 3`；写成 `mul_tensor_t`
那种「一个操作数一个型式参数」之后实例建得起来。三条律统一成这个型式。

## 五、五条验收

| # | 条目 | 读数 |
|---|---|---|
| 1 | `relu_gradient_through_the_library` 绿且数值对 | **仍红，但不是这条车道能修的那一半**，见第七节。三条律本身的数值由新用例钉住：`sum`→`[1,1,1]`、`take`→`[1,0,1,0]`、`mean_axis`→`[0.25,0.25,0.25,0.25]` |
| 2 | `feat/compile/transforms` 不新增红 | **91 = 86 通过 / 4 失败 / 1 已知失败，与基线逐条相同**（失败的仍是 `transpose_loop_escape.two_values_of_two_kinds`、`transpose_loop_escape.second_state_of_the_same_loop`、`transpose_hand_closed_lambda.same_answer_as_the_cut`、`relu_gradient_through_the_library`） |
| 3 | `feat/transforms/split` | **18/19，唯一红的是 `split_loop_value_projected`**，与基线同 |
| 4 | 三条律里读线性操作数命中数 0 | 第一节的 `git grep` |
| 5 | 每条改过的律都有用例 | 新增 `test/plans/feat/compile/stated_extents.plan.toml`：3 条数值 + 2 条故障（形状检查是故障而不是改形状） |

## 六、读数（树、时间、负载）

主机 `88zjwei`（32 核）。

| 运行 | 树 | 时间 | 1m/5m/15m load | 结果 |
|---|---|---|---|---|
| 基线 `feat/compile/transforms` | `/home/zjwei/camel-work/base-4c74` @ `4c744f258` | 2026-11-04 02:40 前后 | 15.38 / 20.14 / 20.36 | 91 = 86 / 4 / 1（同样四条） |
| 本车道 `feat/compile/transforms` | `/home/zjwei/camel-work/sumshape` @ `lane-sumshape` | 2026-11-04 02:47 前后 | 16.30 / 17.18 / 18.81 | **91 = 86 / 4 / 1，四条同基线** |
| 基线 `feat/transforms/split` | `base-4c74` | 同上 | 15.49 前后 | 19 = 18 / 1（`split_loop_value_projected`） |
| 本车道 `feat/transforms/split` | `sumshape` | 同上 | 15.49 / 16.96 / 18.71 | **19 = 18 / 1，同基线** |
| 本车道 `feat/compile/stated_extents`（新） | `sumshape` | 同上 | 16.56 / 17.28 / 18.86 | **5/5 通过** |

负载 1m 全程 15–20（主机有其他车道），**读数不比安静主机可比**，只用于「与基线逐条相同」这种同条件对比。

## 七、`relu_gradient_through_the_library` 现在卡在哪：不是形状，是闭包成员的余切

改前它停在 `skip=cotangent-needs-a-value uri=tensor:sum position=0`（`sum_t` 读它自己的操作数）。
`sum` 那一半修好之后，走查**往前推进了一个节点**，停在：

```
TRANSPOSE function=split_of_jvp_of_total_backward contribution=0 node=4 type=int[] space=none from=11
TRANSPOSE function=split_of_jvp_of_total_backward skip=cotangent-needs-a-value uri=tensor:multiply position=0
```

即 relu 的 mask（`where(x > 0, 1, 0)`）——它是后一半从**闭包成员** `x` 算出来的值。走查问乘积律要 mask **自己的**
余切，而那个位置的答案要读另一个操作数的值，也就是切线，转置永远没有。同一条拒绝在既有红用例
`transpose_hand_closed_lambda.same_answer_as_the_cut` 上读作 `:op/mul_d position=0`，同一句话。

这就是已存在的待裁页
`docs/handover/questions/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md`
（它第 1 节第 4 条量的就是这条拒绝），落点在 `modules/transforms/transpose.cpp#buildTransposeMember` 的
`wantsCotangent` 兜底 `return true`，不在规则里。**本车道按规定不碰 `transpose.cpp` / `split.cpp`**，
所以这一条留给那条车道；`relu_gradient_through_the_library` 的红不是本车道造成，也不是本车道能消的。

## 八、留给合并的

1. `docs/stdlib-reference.md` 与 `test/.../ast_hashes` 需要按 §5 第 6 步重新生成（本车道没有本地构建，跑不了
   `node scripts/stdlib-reference.mjs --write`）。
2. `feat/compile` 目录下多了一个 plan 文件 `stated_extents.plan.toml`（+5 条，全绿），`transforms.plan.toml` 本身没加条目，
   所以它仍然是 91。
3. `rules_angle_args.mean_axis_transpose` 仍是 `known_fail`（`skip=arity uri=tensor:mean_axis operands=3`）：它走的是直接
   `transpose(loss_mean)`，被 `transpose.cpp` 的 arity 闸挡着，与本次改动无关。