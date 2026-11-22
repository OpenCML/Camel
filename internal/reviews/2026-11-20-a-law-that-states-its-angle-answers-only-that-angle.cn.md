# 写死一个轴的律只答那一个轴：`jvp_softmax<1>` 与 `jvp_log_softmax<0>` 要不要像 `jvp_reshape` 那样把轴当参数？

**日期**：2026-11-20
**提出者**：分支卫生审计车道（`lane-audit-stale`，基线 `origin/develop` = `aa449ceea`）
**状态**：等作者裁定。**默认建议：把这两条轴律改成 `jvp_reshape` / `jvp_permute` 那个形状——轴作参数，任何轴都能写。**
**来源与限制**：本页的两条读数是从一支**未合分支** `lane-vjp-forward-backup`（`3f752a95c`）里捞出来的。该分支的 golden 重录（`d5b157a8a`，把 `stdlib/rulesets/jvp.cml` 那一行记成 `2f43a110650b3b1`）**已陈**：`origin/develop` 上同行为 `3c09a0494de711d5`，`jvp.cml` 自该分支分叉点 `4a5f68386` 之后又动了 29+/10−，把那套 golden 合进去会删掉 `develop` 的 37 行。**那笔重录已随分支丢弃，不作本页依据**；分支打 `parked/lane-vjp-forward-backup` 标签后删除，记录措辞是「两条读数未验证，已捞成问题页；golden 重录已陈丢弃」。
**分界线**：分支自己记的读数在第 1 节，主干上复量的读数在第 2 节。**两者不是同一件事**，第 3 节说差在哪。
**读数的载体**：第 2 节的两条走法都是一次性探针（一个 `.cml` 加一条 plan 条目），**读数记在本页，探针不进树**——它们在 `feat/compile` 里没有 plan 条目，进树会让 AST 记录与计划选择各缺一块。要复量时按本页写死的形状重建即可。

## 1. 分支记的读数（在 `4a5f68386` 之后、未合）

分支的页 `2026-11-19-a-rules-angle-argument-is-a-port.cn.md` 记了三条，逐字：

```
softmax axis 2 value=26.000000 tangent=13.500000     <- the law in the tree, which says `softmax<1>`
softmax axis 2 value=26.000000 tangent=2.666667      <- the same law with the axis bound

log_softmax axis 1 value=-23.070858 tangent=13.500000   <- `jvp_log_softmax`, which says `<0>`

REINTERP jvp function=f ... rules=jvp_reshape,jvp_permute,jvp_softmax,jvp_transpose,jvp_mul_tensor,jvp_sum
mix value=2.000000 tangent=0.000000
```

它的读法是：律的左端写 `softmax<1>`，调用写 `softmax<2>`，**律照样答，而且答的是轴 1 的数**——不响，不报，交出一个轴不对的切线。`log_softmax` 那条同理：律说 `<0>`，调用写 `<1>`，切线回的是 `13.5`。

`13.5` 这个数不是随手写的，它正是轴 0 律的答案。手算：`f(x) = sum(log_softmax<1>(x) * w)`，`x` 全 0（`[2, 3]`），`w = 1..6`，`dx = 1..6`。值 `= -log(3) * 21 = -23.070858`。沿轴 1 的切线应是 `dx - 每行均值`，即 `[[-1,0,1],[-1,0,1]]`，点乘 `w` 得 `2 + 2 = 4.0`；沿轴 0 则是 `[[-1.5,-1.5,-1.5],[1.5,1.5,1.5]]`，点乘 `w` 得 `-9 + 22.5 = 13.5`。**分支记到的 `13.5` 就是轴 0 律在说话。**

## 2. 主干上的复量（`aa449ceea`，Release，`[provenance] tip (dirty: 3 files) | binary built 2026-11-19T20:51:09-05:00 | tier fast | jobs 8`）

`dirty: 3 files` 是一次性探针自己（两个 `.cml` 加一条 plan 条目），**没有一处被跟踪的改动**，编译面就是 `aa449ceea`。写作期间主干前进到 `b3c20efcd`（「the AST row the function-operand merge left unrecorded」）——那一笔只往 `test/plans/feat/parse/golden/ast_hashes.txt` 加一行，不动 `src` / `include` / `stdlib` / `modules`，所以下面这两条读数对 `b3c20efcd` 同样成立。

同一段走法放到今天的 `develop` 上，**两条都不是 `13.5`，也不是 `4.0`——是被拒**。逐字：

`log_softmax<1>` 那条（探针：`f(x) = sum(log_softmax<1>(x) * [[1,2,3],[4,5,6]])`，`x` 全 0，`jvp(f)(x, dx)`，`dx = [[1,2,3],[4,5,6]]`）：

```
transforms: jvp has no forward rule for `tensor:log_softmax` (the rule 'jvp_log_softmax'
writes 1 `<>` value(s) of its own ((int)), and this node's configuration is not that one
((float[...], int))).  (name=RuntimeDiag::RuntimeError, code=0x04000000)

READING_REFUSAL {"transform":"jvp","function":"loss_log_softmax","cause":"rule","uri":"tensor:log_softmax"}
```

`attention` 那段混合（我保留的是决定问题的三段：`reshape<[8, 2, 8]>` → `permute<[1, 0, 2]>` → `softmax<2>`，`--trace transforms`）：

```
REINTERP jvp function=mix skip=rule uri=tensor:softmax reason=the rule 'jvp_softmax' writes
1 `<>` value(s) of its own ((int)), and this node's configuration is not that one ((float[2, 8, 8], int))

READING_REFUSAL {"transform":"jvp","function":"mix","cause":"rule","uri":"tensor:softmax"}
```

两条都停在**律写死的 `<>` 与调用的 `<>` 不是同一个值**上，`mix` 那条还多给了一行 `REINTERP ... skip=rule`。

**`float[2, 8, 8]` 这个类型是这一段里最有用的一处证据**：只有 `reshape<[8, 2, 8]>` 把 `[8, 16]` 变成 `[8, 2, 8]`、再 `permute<[1, 0, 2]>` 换成 `[2, 8, 8]` 之后，`softmax` 才会看到这个形状——所以那两条律**确实把调用写的角度绑进去了**，走法是被 `softmax<2>` 挡住的，不是被它们。

## 3. 差在哪：参数轴 vs 写死轴

同一条 `stdlib/rulesets/jvp.cml` 里，四条律现在是两种写法：

```
export rule jvp_reshape::<T>(x: T[...], dims: int[]) ... {  reshape<dims>(x) => ... }   // 轴是参数
export rule jvp_permute::<T>(x: T[...], perm: int[]) ... {  permute<perm>(x) => ... }   // 轴是参数
export rule jvp_softmax::<T>(x: T[...]) ...            {  softmax<1>(x)  => ... }       // 轴写死 1
export rule jvp_log_softmax::<T>(x: T[...]) ...        {  log_softmax<0>(x) => ... }    // 轴写死 0
```

`reshape` 与 `permute` 已经移过了；`softmax` 与 `log_softmax` 还没有。**匹配器现在会比对 `<>`**，所以写死的那两条律只能答自己那一个轴——分支当年记的「答错轴但照样答」已经不存在了，`13.5` 与 `mix value=2.000000` 都是**旧基线**上的形状，不是今天的形状。

于是今天的状态是：**轴不对不再是静默错值，而是这个操作在这个轴上前向无律**。这比「答错」轻——它不交出错的数；但它比「没有」重——`attention` 的混合本来整段能提升，现在停在 `softmax<2>`，而 `jvp_softmax` 就在树里、只是写死了 `1`。

一条附带的事实：这条拒绝走的是 `RuntimeDiag::RuntimeError, code=0x04000000`，句子自己说「The runtime has no diagnostic of its own for this fault」。**它有话，但没名字**——写 `softmax<2>` 的人拿到的是一条泛型运行时错误，不是一个能检索的诊断码。

## 4. 默认建议（本页写死，作者可推翻）

**把 `jvp_softmax` 与 `jvp_log_softmax` 改成参数形状**：`softmax<axis>(x) => (dx) => softmax<axis>(x) * (dx - sum<axis, true>(softmax<axis>(x) * dx))`，`log_softmax` 同理，轴作为律的最后参数、由调用绑定。理由：同一文件里 `reshape` / `permute` 已经这么写了，两条轴律留成常量是**同一个改动的剩余部分**，不是另一种设计；而且不改的话，「`attention` 的混合能整段提升」这句话在主干上永远不成立。

顺带（同一笔或紧随一笔）：给这条拒绝一个具名诊断，替换今天的 `RuntimeDiag::RuntimeError`——一条能说清「律只写了轴 1、调用要轴 2」的拒绝，值得一个码。

## 5. 要作者回答的一句

`jvp_softmax` 与 `jvp_log_softmax` 要不要现在改成「轴作参数」（与 `jvp_reshape` / `jvp_permute` 同形），并给这条拒绝一个具名诊断？

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

develop 上默认已落：`jvp_softmax`、`jvp_log_softmax` 以 `axis` 为参数（`src/stdlib/rulesets/jvp.cml:536/563`），`layer_norm` 前向律也已有。规则从节点绑定 `<>` 值就是普通匹配（9.11："规则与事实的形参 `x: T` 的类型是 `Node<T>`"）。还剩三件：规则右侧写 `<>` 属核心抽象第 6 节、第 11 节第 2 条"规则语言四项扩充"，由决策者在变换一步核实；这条拒绝要改成具名诊断（9.9："在编译期拒绝并点名这个运算"）；`rules_angle_args.plan.toml` 头那句"只能写死"要改。
