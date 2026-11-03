# 一条**静态具名**的调用，转置时站哪儿：被调函数的 body，还是它的 `lin`？

## 问什么

作者 2026-11-03 19:35 的 ② 说「被调函数的 `lin` 作为外层残差，转置一次调用 = `transpose(lin_g)`」。
今天的树在**两条**支上已经这么做了，第三条支上不是：

| 调用站的样子 | 今天走哪条 | 出处 |
|---|---|---|
| 被调函数答 `(value, tangent)`（一个 `jvp` 过的图） | 切它，转置**第二半**（就是它的 `lin`） | `modules/transforms/transpose.cpp#transposePairCall` |
| 被调函数是**运行期才知道的值**（`split` 交出来的 `lin`、`let` 里的函数值） | 发一个 `transforms:transpose` 算子，用余切调它 | `modules/transforms/transpose.cpp#buildTransposeMember` 的 `callee == nullptr` 支 |
| 被调函数是**静态具名**的普通图 | **就地展开它的 body**（在 body 上逐条反向走律） | 同上，`callee != nullptr` 且不答 pair 的支 |

要裁的是第三条支：**换不换成 `transpose(lin_g)`**。

## 那个站点的原文

`modules/transforms/transpose.cpp#buildTransposeMember`，`GCNodeKind::Call` / `GCNodeKind::Func` 那一臂。
它**拿**被调函数的线性半——当那个半是以**值**（本图收到的 `Port`，即切片交出来的 `lin`）到场时：

> A call reaches its callee one of two ways in this graph model: a CALL node whose first input is the
> callee's value, or a FUNC node that is the call itself, with the arguments as its norm inputs. Both
> are one fact -- a graph is called here -- and both are transposed the same way: the callee's
> transpose is a graph of its own, built beside this one, once per callee (the rule `jvp` reads for a
> lifted function, so two calls of one function call one graph).

> A **Port** is a value this graph *receives* -- a `with` parameter, or a closure port, which is how a
> cut hands its linear half to the half that calls it. That value's transpose is `transpose(v)` at run
> time, so the call can be turned around without seeing the graph behind it.

**拿不到时它拒绝，不就地展开**——同一个文件的 `callee == nullptr` 支：

> **Unless it is a linear function known at run time**, which is exactly what a cut hands out: `split`'s
> second half is a closure value, so a body that calls it (every backward half that has a call in it
> does) calls a value and not a graph. Transposing it is the same rule as transposing a graph, one
> level down: the transposition of `v` is `transpose(v)` -- the operator, written once in the
> transposed graph -- and the call is that result applied to the cotangent. No node reads a closure
> cell, and nothing is recomputed (author, 2026-11-03 17:55).

两个拒绝因而存在：`TransposeRefusalCause::CallValueComputedByTheBody`（那个值不是本图收到的
`Port`，而是 body 自己造的闭包——`let h = (y) => y*x`）与 `CallNoTranspose`（参数个数与被调函数
的形参不符）。**没有「退回去展开 body」这条路**：支是按值的来历选的，不是按成败选的。

**具名被调函数**（`calleeIdOf` 答得出来）走的是另一条：`buildTransposeMember(state, callee…)`
造一张被调函数的转置成员图，在 body 上逐条反向走律——它本来就**不拿**线性半，因为那个半只有
`split` 会交出来。

## 读数

1. **第二条支确实在**（不是「一行都没有」）。同一棵树上的 trace（`residual` @ `34b659107`，
   `f(x)=2.0*x`、`g(x)=f(x)*x`、`vjp(g)(3.0)`）：

   ```
   T resolved function=split_of_jvp_of_g_backward node=5 argumentAt=0 callee=none params=0 pair=0
   T closure function=split_of_jvp_of_g_backward node=5 carrier=2 carrierKind=1 captured=1 closedOver=1 linearType=1 transposedType=1 ct=6
   T path=closure-operator function=split_of_jvp_of_g_backward node=5 args=1
   ```

   即：`lin_g` 体里的调用站（node 5，`norm=[1] with=[2]`，被调函数是 node 2 那个 `(float) => float`
   的**值**）被当成了 `transpose(lin_f)(ct)`。**裁定 ② 的形状在这里已经是今天的行为。**

2. **第三条支是就地展开**。既有用例的读数就是它：`transpose_of_pick`
   （`test/cases/compile/transforms/transpose_refuses_a_parameter_value_it_cannot_be_handed.cml`）、
   以及 `transpose_short.cml` 那两条的
   `TRANSPOSE function=g graph=…` / `TRANSPOSE function=f graph=…`
   （`test/plans/feat/compile/transforms.plan.toml` 的 `transpose_chain_*`/`thrice`/`double` 条目）——
   被调函数是**一个被转置的成员图**，不是 `lin`。

3. **换成 `lin` 今天买不到任何数字**，这是最要紧的一条。`f(x) = 2.0*x` 上，body 转置和 `lin_f` 的
   转置答同一个数（`vjp(flat)(3.0)` 的 `flat'(3) = 12.0`，`flat(x) = 2.0*x*x` 与跨调用写法同值），
   而**跨调用的错数不在这条支上**：`vjp(g)(3.0)` 答 `0.0`，因为 `split(jvp(g)).1` 本身是常数函数
   （`lin(1.0) = lin(0.5) = lin(2.0) = 36.0`），病灶是 `modules/transforms/split.cpp#residualsOf`
   把切片的**切线形参**当成残差列进了闭包，于是后一半带着一个没人读的形参端口
   （`normPorts = [4]`）、读一个前半算不出来的残差（node 1）。用例
   `cross_call_gradient_is_not_zero`（`known_fail`，owner `lane-linres`）钉的就是这条。

## 三条路

1. **第三条支保持就地展开**（本页默认）。代价：`transpose` 要能转置被调函数 body 里的每一条律；被调
   函数 body 读、调用站没有的值就走 `calleeNeeds` 让调用站补。好处：不动今天任何一条绿的读数，
   且跨调用的数字在 `split` 修好之前反正拿不到。
2. **第三条支换成 `transpose(lin_g)`**：在转置成员里发
   `transpose( Accs(1, call( split(jvp(f)) 的前半, 实参 )) )(ct)`。代价：每个具名调用站多一层
   `jvp`/`split` 的图（`modules/transforms/jvp.h#buildJvpGraph` 造的是**自己的 program**，要先解决
   跨 program 的图引用），且每条既有 `TRANSPOSE function=<被调函数>` 的读数会变（至少两条绿用例要重写）；
   好处：被调函数 body 读而不由调用站持有的值不再需要 `calleeNeeds` 补——`lin` 的闭包自己带。
3. **只在具名被调函数的 body 转置失败时**退到 `lin`。代价：一条律的答案会取决于另一条律能不能用，
   这正是 `transposePairCall` 的注释拒绝的「换个规则再来一次」；不推荐。

## 默认（本轮按此落）

**路 1**：`transpose.cpp` 不动，② 的数值验收标成**未达成，等上游**（`split.cpp#residualsOf`）。
路 2 是 ② 的正解，但它要 `split` 先答一个**读自己形参**的 `lin`，否则换过去只是把同一个错数搬到另一条
生成路径上；等 `lane-linres` 那条落地后，本条按路 2 重开，两条 `TRANSPOSE function=<具名>` 的读数一起改。