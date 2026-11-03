# 切线重算的那个"带上下文输入的节点"，`split` 该拷什么？

日期：2026-11-03　状态：**默认 A（`split` 承认上下文输入），但先做一个不裁定的测量**

## 出处（只写有出处的）

- `stdlib/rulesets/jvp.cml#jvp_softmax` 的右手边：
  `softmax<1>(x) => (dx: T[..]) => softmax<1>(x) * (dx - sum<1, true>(softmax<1>(x) * dx))` ——
  切线里 `y = softmax<1>(x)` 是**重算**的，不是从值那半边取来的。
- `modules/transforms/split.cpp` 的拒绝：`skip=backward construct=a node whose input was not copied
  uri=- why=an operand of the node is not reachable from the half's exit`（本轮实测原文）。
- 对照：`stdlib/rulesets/jvp.cml#jvp_relu` 的掩码 `where(x > 0, 1, 0)` 只依赖 `x`，
  `split` 把它当残差带着走（实测 `contribution=0 node=6 type=bool[..] from=7`），**不撞这条墙**。
- 施工方默认：下面的三条路、代价与"先测 C"都是本轮（lane-softmax）的判断，无作者出处。
- **另一处同一类墙，已在树上**：`test/plans/feat/compile/transforms.plan.toml#vjp_loop.rounds_are_undone_in_reverse`
  的 XFAIL 说明原话——"`split` builds both halves; the refusal is one step before the walk, at the call
  the backward half recomputes (`index_into(dxs, 0)`), whose array argument is not a residual the cut
  carries"。那一例的重算是 `index_into`，这一例是 `softmax<1>`；**两例都是"后半边重算的东西没被当残差带过去"**，
  所以下面默认的 A 不是为一条用例新开的机制，而是把已在树上的那条说明推到它的通解。

## 现象（实测，2026-11-03）

同一条损失 `sum(softmax<1>(x) * w)`：

- **引擎**答得出，逐元素与手算到 1e-16 一致：
  `[[-0.17007706463866937, -0.37517307550291684, -0.5460605174868518, 1.0913106576284373],
  [0.10848803305563078, -0.046500634627606695, -0.22389029087827908, 0.16190289245025496]]`；
- **库层**在**走查之前**就停：
  `REINTERP jvp function=total graph=250 … rules=jvp_softmax,jvp_mul_tensor,jvp_sum` 之后
  `REINTERP split function=jvp_of_total skip=backward construct=a node whose input was not copied
  uri=- why=an operand of the node is not reachable from the half's exit`，**整份输出里没有任何
  `TRANSPOSE` 行**。

所以审计表里"`softmax` 只差一条 `softmax_t`"是**错的**（本轮纠正）：补律没用，因为转置从未开始。

## 墙的形状（量到的部分，和没量到的部分）

量到的：`jvp_softmax` 的切线在**后半边**重算 `y = softmax<1>(x)`；`split` 拒绝，理由是那个节点的
某个操作数"not reachable from the half's exit"。`relu` 的掩码 `where(x > 0, 1, 0)` 不撞它——因为
掩码只由 `x` 算出，`split` 能把它当残差带着走（实测 `contribution=0 node=6 type=bool[..] from=7`）。

**没量到的是"哪一类重算被拦"**，而且原来的猜测（带 `<>` 上下文输入的节点才算）已经有一处反例：
`test/plans/feat/compile/transforms.plan.toml#vjp_loop.rounds_are_undone_in_reverse` 里重算的是
`index_into(dxs, 0)`，**没有** `<>` 参数，拒绝原文与这里同形。所以共性只能说到"后半边重算的东西
没被带过去"，再往下分要靠这次实验：

> 给 `exp` 补 `jvp_exp` + `exp_t`（切线 `exp(x) * dx`，正好是"只依赖 `x` 的重算"），看 `vjp(sum(exp(x)))`
> 是过还是同样被拦。过了 → 墙只拦"重算里还带上下文输入 / 被读两次的值"；也被拦 → 墙拦一切重算，
> 那么 `relu` 能过就是"残差恰好只依赖输入"的特例，A 的代价要按后者估。

`exp` 今天连 `jvp` 都没有（实测 `skip=rule uri=tensor:exp reason=the operator has no forward rule
bound at all`），所以这一格既是差集的下一个成员，也是这条墙的判别实验——两件事一次做完。

## 三条路与代价

1. **A：`split` 承认后半边重算的节点**（拷节点时把它的输入一起带过去，或把它当残差）。代价：一次 pass
   改动，且要把"重算的中间值算不算残差"说清——`modules/transforms/transpose.cpp` 里 `mean_axis` 那条
   `skip=arity` 的注释已经记过同一个问题的另一面（"a law whose right side reads the forward value of
   an *intermediate* node makes the walk declare a port the caller was never told to fill"），所以这条
   路会同时碰到"回传的契约里能不能多一个端口"。
2. **B：让律的右手边能引用值节点**（例如 `let y = softmax(x)`）。代价：给律语言加一次绑定语义，
   等于把"律的右手边只写表达式"这条前提改掉；比 A 大。
3. **C：让 lift 认出"同一个表达式已经算过"**（`softmax<1>(x)` 在值半边已经有一个节点，切线里那两处
   复用它，而不是再造）。代价：一次 lift/规范化改动，**不涉及契约裁定**；但如果 lift 本来就复用，
   这条墙就不该出现——所以**C 必须先测**：读数只要看一次 `skip=backward` 运行后的图里
   `softmax<1>` 节点有几个（一个 = 复用了，墙在别处；两个 = 重算，C 成立）。

## 默认建议：先做 `exp` 那个实验，再按结果走 A

那一次实验（`jvp_exp` + `exp_t`，再加一条 `vjp(sum(exp(x)))` 的读数）**今天就能取**，不涉及任何裁定，
而且它本身就填掉差集的一格；它同时把 A 的代价定下来（拦"带上下文输入的重算"还是"一切重算"）。
若墙只拦前者，默认走 **A**——它是 `split` 的问题，本该由 `split` 回答，而 `mean_axis` 那条 `skip=arity`
是同一个问题的另一面，两处应当一起回答。B 不建议：代价落在语言的核心前提上，收益只是省一次 lift
改动。C 保留为 A 的替代（若 `exp` 也拦，说明 lift 确实在重算，C 就成了最小改动）。