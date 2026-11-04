# `mean` 的切线今天不降秩：轴不是最后一维且 `keepdims=false` 时是「响亮拒绝」，不是错数

**一句话**：`stdlib/rulesets/jvp.cml#jvp_mean_axis` 的切线写成 `dxs * T(1.0 / float(shape(xs)[axis]))`，它乘的是**操作数形状**的切向，而归约输出的切向应当是被归约后的形状；于是 `axis` 不是最后一维、`keepdims=false` 时形状对不上。**实测：不是静默错数，是执行期一句 `TensorDimensionMismatch`。**

本页记录的**不是**「长度从哪读」那一笔（那已落 `048dd4771`，`vjp` 的 `axis=0` 读数 `[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]`）；这一条属**广播/形状语义**，不属那一项。

## 1. 读数（`x = [[1, 2, 3], [4, 5, 6]]`，`ct = 1.0`，全部实测于 `048dd4771`）

| 走法 | 结果 |
| --- | --- |
| `vjp`，`axis=0`，`keepdims=false`，`w=[1, 2, 3]` | **过**，`[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]`（正确） |
| `vjp`，`axis=1`，`keepdims=false`，`w=[10, 20]` | **拒**（执行期）：`TensorDimensionMismatch: Array shape mismatch in 'multiply': cannot broadcast [2, 3] with [2].` |
| `vjp`，`axis=1`，`keepdims=true`，`w=[10, 20]` | **拒**：同一句 `[2, 3] with [2]` —— 先撞的是切向半边的乘法；`w` 自己的 `[2]` 与输出 `[2, 1]` 也是一处不匹配 |
| `vjp`，`axis=1`，`keepdims=true`，`w=[[10], [20]]` | **过**，`[[3.333333333333333, …×3], [6.666666666666666, …×3]]`（正确：`w[i] / 3`） |
| 裸 `transpose`，`axis=1` | 更早被拒：`TRANSPOSE function=loss_axis1 skip=arity uri=tensor:mean_axis operands=3` —— `stdlib/rulesets/transpose.cml#mean_axis_t` 根本没跑，它里面的广播那一处今天够不到 |

`axis=1` 的正解与 `axis=0` 的对照用例**逐元素**分开：同一份 `x`、`w=[10, 20, 30]` 时 `axis=0` 给 `[[5, 10, 15], [5, 10, 15]]`（沿列递增），`axis=1` 给 `[[3.333…, 3.333…, 3.333…], [6.666…, 6.666…, 6.666…]]`（沿行恒定）；若把轴长错读成 2（另一维），`axis=1` 会得 `[[5, 5, 5], [10, 10, 10]]`，第 `[0][0]` 位就分开（5 对 3.333）。

**小结（三条）**：① 这一类里没找到静默错数；② 形状不对时要么广播成立且数**恰好对**（因为乘法把 `broadcast` 做在了它该做的地方），要么 `TensorDimensionMismatch` 响亮拒绝；③ 真正要的降秩（`dxs * 1/n` 产出输出形状）今天没有写。

## 2. 要裁的问句

**问**：`mean_axis` 的切线要不要写成**降秩**的（按 `keepdims=true` 的形状算完再交回输出秩），还是把「轴为最后一维、或 `keepdims=true`」记成这条律**有定义的范围**，其余组合就是今天这句拒绝？

## 3. 三条路与代价（默认建议：走 1）

1. **记作既有限制（默认）**：零改动。`axis` 非末维 + `keepdims=false` 保持今天这句运行时拒绝；要用就写 `keepdims=true`（实测正确）或把轴放最后。代价：这条组合在库层永远没有梯度，且是**运行期**才发现，不是类型期。
2. **改律降秩**：把 `dxs * 1/n` 换成「按 `keepdims=true` 的形状乘、再 `sum_to` 到输出的秩」。代价：律的右边要说出**输出的形状**，而今天只能写常量（`reshape<[2, 2]>` 那种）或端口值，动态轴长拼不出来 —— 会撞角度参数那条墙（`stdlib/rulesets/jvp.cml` 里 `reshape`/`softmax` 的同一段说明）。
3. **放宽广播**：让 `[2, 3]` 与 `[2]` 按尾轴对齐成 `[2, 2]`（numpy 的形状）。代价：改的是所有算子共享的广播语义，与 `mean` 无关，影响面最大，不该由一条律推动。

**默认建议：走 1。** 理由：今天的失败是**响亮**的（`TensorDimensionMismatch`，带两种形状），没有任何静默错数被观察到；而 2 的真实障碍是「律里说不出动态形状」，那是变换层的形状表达问题，不是把 `mean` 写得更聪明能解决的。走 1 之后，本页这条只作为**已量过的既有限制**留在索引里；等形状表达那块动了再回来看 2。

## 4. 判据 / 哨兵

**判据已经有一个用例钉着，缺的只是把它的名字写在这里**：`test/cases/compile/transforms/mean_axis1_keepdims_gradient_reads_the_row_length.cml`，
计划项 `mean_axis1_keepdims_gradient_reads_the_row_length`（`test/plans/feat/compile/transforms.plan.toml`）。
它逐元素钉住 `vjp` 的读数，不是只钉一个总数：

```
mean_axis1 keepdims value=120.0 cotangent=[[3.333333333333333, 3.333333333333333, 3.333333333333333], [6.666666666666666, 6.666666666666666, 6.666666666666666]]
```

**绿哨兵（必须留绿）**：那一条用例就是"轴长读的是行长度 3、不是另一维 2"的机器判据。错读成另一维（`w[i] / 2`）
会在第 `[0][0]` 位答 `5` 而不是 `3.333`——**第一个元素就分开**；错读成 `numel(x) = 6` 会在同一位置分开。
它也钉住 `skip=arity uri=tensor:mean_axis` **不出现**，所以任何把 `mean_axis_t` 退回三操作数形态的改动在这里红。

**红哨兵（今天的拒绝不许变成静默的答案）**：`axis=1`、`keepdims=false` 今天在执行期拒
（`TensorDimensionMismatch ... cannot broadcast [2, 3] with [2].`，两种形状都在话里）。走默认的 1 时它必须仍是这句拒绝；
哪天它变成"给出一个数"而没有任何一条逐元素正解先写进用例，那就是静默错数进了语言——`arity` 墙倒下之后
`stdlib/rulesets/transpose.cml#mean_axis_t` 里的广播正是最可能做出这件事的地方。
