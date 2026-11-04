# 切口的出口上，秩零的切向量由谁定类型

**状态**：待作者裁（本页是 2026-11-04 04:20 裁定「秩 0：只有一个转换点」落地时撞上的那一格）。**本树这一格没有动代码**：端口按裁定回到 `T[..]`、lint 的许可撤销、用例的 `keepdims` 改回 `false`；`vjp` 的**反向图出口**仍然是 `RuntimeDiag::ExitTypeMismatch`，`test/plans/feat/compile/stated_extents.plan.toml#stated_mean_axis_gradient` 因此是一条红。本页记下三次量过的读数和三条路，请作者裁。

**问句（一句）**：当一条律的右值把切向量算成 `T[..]`，而 `jvp` 给这个值配的对子按**值**的类型写成 `(float, float)`，那么在 `split` 切开的两半之间，是**哪一个位置**该做 `T[..]` → `T` 的转换——`jvp` 写对子的那一处（`liftedPair`），`split` 的出口那一处，还是根本不该在这里转换（该由律声明元素）？

## 1. 这一格是什么（一句话）

`mean_axis(x: float[], 0, false)` 的答案是**元素本身**（秩零，5.4）。`jvp_mean_axis` 的右值把它算成 `mean_axis_stated(dxs, at)`，节点类型 `T[..]`（等号右边是运行期的 `at`，推断只能答 `T[..]`）。`jvp` 为一个值配的对子是 `pairTypeOf(valueType)` = `(float, float)`。于是对子的第二个成员声明 `float`，写进去的节点却是 `T[..]`——`split` 把这一半当作 `lin` 的出口，出口值 `float[..]`、声明 `float`，封存层按 5.1 拒（`RuntimeDiag::ExitTypeMismatch`，`0x040001AE`）：

```
The value the graph 'split_of_jvp_of_total_backward' hands on has type 'float[..]', and the graph declares 'float'; the value is node 8.
```

这不是端口的事：端口写 `T` 时崩在 `numelKernel`（另一页），端口写 `T[..]` 时卡在这里。两者是同一个值在**两个不同位置**被要求成两种拼写。

## 2. 三次读数（都在 `88zjwei`，`-Tier fast -Jobs 4`，宿主 1m load 23–27，**不静默**）

| 树 | 改动 | `feat/compile/stated_extents` |
|---|---|---|
| `d7f15f2b3`（基线） | — | `total: 18, pass: 16, fail: 0, known failures: 2` |
| 端口回 `T[..]`，用例仍 `keepdims = true` | 本页第 1 节那句话**不出现** | 同上：16 / 0 / 2，`killed by a signal` 一条都没有 |
| 端口回 `T[..]`，用例 `keepdims = false`（裁定要求） | 本页第 1 节那句话出现 | `total: 18, pass: 15, fail: 1, known failures: 2` |

第三行就是本树交出的状态：**唯一那条红是 `stated_mean_axis_gradient`**，红的句子和两条 `known_fail` 记的是同一句（`..._element_gradient`、`..._answered_by_the_engine` 的注记里已经写着这句）。三条用例走的是同一个缺陷，其中两条被另一条线挂了 `known_fail`，第三条是按裁定解除 `keepdims` 绕法之后新露出来的。

## 3. 试过的修法（被量过，不成立）

**在 `liftedPair` 里按声明成员类型转换**（`modules/transforms/jvp.cpp`）：对子写进去之前，若 `pairType->typeAt(1)` 与成员的 `dataType` 之间是 5.4 的那条边（`camel/core/type/composite/array.h#isRankZeroEdge`，本轮把它从 `builder_helpers.h` 提到类型层，一个定义、builder 与 transform 两处入口），插入 `addCastNode(memberType)`。

- **`vjp` 那条路通了**：`stated_mean_axis_gradient` 从红变绿，答 `stated mean value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]`（手算：四数均值 2.5，每位 1/4）。
- **`jvp`+`split` 那条路先拒后崩**：`split` 的 plumbing tier 没有 Cast 的读法（`node kind 2 has no reading in the plumbing tier`）。给 `split.cpp` 补一个 Cast 分支（照 `Oper` 那样 `addCastNode` + 一个正常输入）之后**建得出图了，但运行期 SIGSEGV**（`total: 18, pass: 15, fail: 3, killed by a signal: 3`），`TRANSPOSE ... rhs=mean_axis_stated_t` 建了 10 个节点的反向图，然后崩在 `numelKernel` 那一类。

也就是说：**`jvp` 里插的那次转换，到了 `split`+`transpose` 之后并没有被当成同一个值**——这和裁定说的"转换只在一处"并不矛盾，它说明那一处**还没找对**。两次改动都已撤回（本树 `git status` 里没有 `modules/transforms/`），只留 `array.h` 的那条 `isRankZeroEdge`（语义与原来 `builder_helpers.h#isRankZeroEdge` 逐字相同，只是提到类型层，供 transform 也能问同一个问题）。

## 4. 三条路

**路 A（推荐，默认沿用）：由 `split` 定，`jvp` 只写值。** 对子的成员就写 `T[..]` 的节点（不转换），`split` 在生成 `lin` 时把出口按**声明的** `Tangent<value>` 转换一次——那里同时知道声明和手上的类型，是这一格唯一同时看见两边的地方。这条路把转换点从 `jvp` 挪到 `split`，仍是一处。**代价**：要把 `jvp` 的对子类型（`pairTypeOf`）与 `split` 的出口类型对齐的规则写清；`Tangent<T>` 对秩零答案的含义要再说一遍（`float[..]` 的切空间是 `float[..]`）。

**路 B：让律声明元素。** 就是被裁掉的那条（`(ct: T)`），而且它只服务秩零那一种形状，数组余切断言会给错数——**不建议**，记在这里只为闭合。

**路 C：让 `*_stated` 的推断在 `at` 是常量时答元素。** `at` 是 `stated_axis_shape` 算出来的运行期值，不是常量；要做到这条得让 `jvp` 在展开律之前把 `at` 折成常量（另一条线已经试过两次，都退回了）。**不建议**。

## 5. 需要作者答的三句

1. 路 A 是不是那个"唯一的转换点"？如果是，`Tangent<T[..]>` 与 `Tangent<T>` 在秩零处是否也按 5.4 视作同一个值（也就是 `split` 的出口转换和 `Builder::coerceTo` 是同一条边）？
2. 若不是路 A，`split` 的 plumbing tier **是否允许出现 Cast 节点**（这是路 A 的前置：今天不允许，我补的分支建图成功但运行期崩，说明它还需要运行期那一半的确认）。
3. 在裁定落地之前，`stated_mean_axis_gradient` 这条红**是否接受**（本树现状），还是先把它退回 `keepdims = true` 等 `split` 修好再一次性改回？本树的默认按裁定走：`keepdims = false`，红留着并报给作者。