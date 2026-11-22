# 两状态循环的一半，是"那个循环的投影"——不是一个能顺手认出来的形状

**2026-11-03，`737a7e896`（原 `lane-loopreverse`，该分支已删）。`split(jvp(f))` 遇到两状态循环时的设计格。今天两道门都关着，
这一页把两道门分别是什么、三条路各要付什么代价、以及为什么先不动的理由钉住。**

## ① 今天的读数

程序（`/tmp/probe_tuple_state2.cml`，两个 float 状态的循环）：

```camel
func f(x: float): (float, float) {
    return loop s = (x, 0.0) for k in [2.0, 3.0] do (s.0 + k * s.1, k * k * s.0 + s.1)
}
```

`jvp` 把它做出来（值类型按 `[s0.v, s0.d, s1.v, s1.d, …]`），提升本身是对的：

```
primal= (13.0, 13.0)
lift v= (13.0, 13.0)  d= (13.0, 13.0)
```

`split` 拒绝，且现在说得对（这一笔之前它说的是"a port"）：

```
REINTERP split function=jvp_of_f skip=forward construct=a loop uri=-
why=the cut enters a loop through the loop cell, and that cell is the exit:
this loop is inside the chain the exit reads, so the half would have to copy a body region
```

**第一道门：出口形状**（`modules/transforms/split.cpp#exitShapeOf`）。`jvp` 给两状态循环的出口
是一个**二元 Tuple Fill**（value 半边 + tangent 半边），而这一格只把两种东西认成 loop 形状：

```cpp
if (node->kind == rt::GCNodeKind::Fill) {
    const auto *fill = function->nodeBodyAs<rt::GCFillBody>(exit);
    if (fill != nullptr && fill->fillKind == rt::GCFillKind::Tuple &&
        function->withInputsOf(exit).size() == 2) {
        return ExitShape::Straight;          // <- 两状态循环的 pair 落在这里
    }
}
if (node->kind == rt::GCNodeKind::Loop) {
    return function->funcType() != nullptr &&
                   node->dataType() == function->funcType()->exitType()
               ? ExitShape::Loop           // <- 一状态循环走这里（出口*就是* loop）
               : ExitShape::NotAPair;
}
```

两状态的 `pair` 满足第一个分支（二元 Tuple Fill）⇒ `Straight` ⇒ `cutLoopGraph` 根本没被调用 ⇒
直链那套 `Half` 走进去，在 loop 节点上拒绝（上面那句）。**这不是判错，是这一格看不见**：它认的是
"出口是 Fill"与"出口是 Loop"，而这里是第三种——**出口是 Fill，其中每一个成员都是同一个 LOOP 的
`ACCS` 投影**（value 半边取 `[s0.v, s1.v]` 即 0、2，tangent 半边取 1、3）。

**第二道门：切分能力**（`split.cpp#readLoopCut`）。就算第一道门认了，把 loop 交给 `cutLoopGraph`
之后立刻撞上：

```cpp
const std::span<const gc_node_ref_t> ports = function->regionPorts(cut.bodyRegion);
if (ports.size() != 2) {
    return refuse(
        "this loop carries " + std::to_string(ports.size() > 0 ? ports.size() - 1 : 0) +
        " states, and the cut handles one: a tape per state would have to be interleaved, "
        "which the round's single element cannot express");
}
```

两状态的循环体有 3 个 region 端口（元素 + 两个状态），于是这句话会变成
`this loop carries 2 states, and the cut handles one: a tape per state would have to be
interleaved, which the round's single element cannot express`。

一句话：**第一道门是"看不看得见"，第二道门是"接不接得住"。今天的墙其实在第二道**——因为一状态的
同构形状（出口就是 loop）已经被切了（`split_loop_taped_rounds` 绿），而多状态的 tape 布局从没定过。

## ② 三条路，各带代价

| 路 | 做什么 | 代价 |
| --- | --- | --- |
| **(甲)** | 让 `exitShapeOf` 认"出口的 Fill 的每个成员都是同一个 LOOP 的 `ACCS` 投影"，交给 `cutLoopGraph` | **多状态循环的出口形状要重新定义**：`ExitShape::Loop` 今天的意思是"出口节点本身是 loop"，认了之后它得能表示"出口是一个 loop 的投影"，`cutLoopGraph` 里所有假设 `exit` 就是 loop 节点的地方（`LoopView` 的读法、`readLoopCut(function, exit, …)` 的入参）都要跟着改 |
| **(乙)** | 让 `readLoopCut` 支持多状态 tape：一轮写 `k` 个 tape（或一条交错 tape），反向按状态拆 | **tape 布局与配对规则全变**：tape 元素类型、每轮的写入顺序、反向半边的循环体、以及切分产物的签名都要重算；这是 `split` 里最"结构"的一块，不是一个 pass 顺手能定的 |
| **(丙)** | 明确拒绝，并把今天这句拒绝钉住（就是现在这条） | **多状态循环的梯度永久缺**：`split(jvp(f))` 永远停在拒绝，`f` 的两状态循环拿不到 `split` 那一半 |

## ③ 我方默认

**先 (丙)，把 (甲) 记成下一步的设计题。**

理由不是"省事"，是顺序：**(乙) 会动 tape 布局**——那是 `split` 与运行时 `LoopView` 共同的约定
（一轮一个元素、`ACCS` 半边的配对、反向循环靠常量 `1` 数轮数），改了它，一状态那条已经绿的路径
也要跟着重新验证一遍。而 (甲) 只改"怎么把一个出口认成 loop 的投影"，**不动 tape**，所以它是
(b) 之前必须先落的一格：**先把形状认出来，让拒绝变成"我知道这是什么，但我不接"**，再谈
接不接得住。

这也解释了为什么 (丙) 不是无用的终点：今天的拒绝文字已经把"是哪一格该接它"说出来了
（`the cut enters a loop through the loop cell, and that cell is the exit`）。**一句话结论：
这个格不是"顺手就能定"的，因为它的第二道门要改 tape 布局，而那是一状态路径也已经依赖的约定——
先改形状识别（甲），才有资格谈 tape（乙）。**

## 判据

1. `split` 计划在这棵树上仍是 14/4（四红是 `split_branch_author_vjp.{nvm,fvm,jit,prl}`，本树基线；
   `521f` 那棵树在 `c068a798d` 是 18/18，两者不互相冒充基线）。
2. 两状态循环的 `jvp` 读数不变：`primal= (13.0, 13.0)`、`lift v= (13.0, 13.0)`、
   `lift d= (13.0, 13.0)`；`split` 的失败形态是**拒绝**，不是错数。
3. 走 (甲) 的验收：拒绝文字从 `a loop` 变成 `this loop carries 2 states, and the cut handles one`；
   走 (乙) 的验收：`split(jvp(f))` 的两半都绿，且一状态那两条（`split_loop_taped_rounds`、
   `split_identity_pipeline`）读数一个数字不变。