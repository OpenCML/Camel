# 按调用跑每一轮的循环：这条拒绝该留、该删，还是连 `setLoopGraphs` 一起删

> **删了也没有用例能证明修好，和"修好了但没人证明"是两件事：前者是半成品，后者是没物可修。**

日期：2026-10-31　车道：`lane-recheck`（`reinterp<jvp>`，前向模式）

**要作者答的一句**：**"按调用跑每一轮的循环形式"现在没有任何生产者（`setLoopGraphs` 全仓无调用者、`draft_graph_builder.cpp:245` 只写 `.kind`、`exec_plan.cpp:260` 是防御分支）——在有生产者之前，这条拒绝该留、该删，还是连 `setLoopGraphs` 一起删？**

## 现象

`reinterp<jvp>` 的遍历里有一条拒绝，触发条件是：一个 `Loop` 节点**没有归属于它的 `LoopBody` 区域**。它说的是一句"这个循环的每一轮是两次图调用（条件图和体图），不是本图里的一块区域，所以还提升不了"，落在循环自己的 `file:line` 上：

```
REINTERP jvp function=f skip=...（省略）
transforms: jvp does not reinterpret a loop that runs its rounds as calls yet
  (its body is a graph of its own, and only a loop whose rounds are a region of this graph is lifted yet)
```

`87c366609` 把**区域形式**做通了：`(36, 24)`，状态提升成一对，切向量逐轮传递。剩下的就是这条拒绝守的形式。

## 三处读数，指向同一个结论：这条形式今天不可达

1. **编译器只建区域形式的循环**：`src/compile/gir/draft_graph_builder.cpp:245` 是唯一建 `Loop` 节点的地方，载荷是 `runtime::GCLoopBody{.kind = kind}` —— **只写 `kind`**，不写条件图/体图 id。
2. **写条件图/体图的 API 没有调用者**：`GraphDraft::setLoopGraphs`（`src/runtime/draft.cpp:2306`）在 `src/`、`modules/` 下**没有一处调用**（`git grep setLoopGraphs` 只剩声明、定义、以及 `draft_clone`/`graph.cpp` 里读它的地方）。
3. **执行侧对它只是防御**：`src/execute/exec_plan.cpp:258-262` 先取 `loopRegionOf(ref, LoopCond/LoopBody)`，`bodyRegion == 0` 时 `continue`，注释写的是 `a loop of two graphs: the back end calls them` —— 有后端"能"调用这种循环，但今天的编译路径不产生它。

于是：**从源码造不出这种循环，`test/` 里也没有直接建 `GraphDraftGroup` 的单元测试**（`test/` 下没有 `unit/`，也没有任何用例直接用 `GraphDraftGroup`）。**提升它能编译，但没有任何一条用例能证明它提升了**——这就是"半成品"与"无物可修"的区别：前者是能力不全，后者是**能力已经全了，只是没人需要它**。

## 三条路，代价各一句

1. **留着拒绝（默认建议）**：`reinterp<jvp>` 对区域循环给出 `(36, 24)`/`(81, 108)`，对"两张图的循环"给一条落在循环上的诊断，**不假装能提升**。代价：一条今天永远不会触发的代码路径，和后端能力之间留着一处不一致；它会在有生产者那一天变成真缺陷。
2. **盲删拒绝、盲写提升**：条件图原样保留，体图提升成一个成员，配一个 `(pair) => (pair)` 的每轮适配成员，节点本身按"每轮调一次 `reinterp<jvp>(体)`"接。代价：**写得出、编译得过、零回归，但没有一条读数**——这正是本车道十天前那类"三个错理论"的形状，而我下一次动它时仍然不知道对不对。
3. **连 `setLoopGraphs` 一起删**：承认"两张图的循环"不是这个运行时的形式，把 `setLoopGraphs`、`GCLoopBody` 的 `condGraph`/`bodyGraph` 字段、`exec_plan.cpp` 的防御分支、以及 `jvp.cpp` 的这条拒绝一起删掉，让"循环"只剩区域形式一种。代价：后端"调用两张图"的能力一起没了（`exec_plan.cpp:260` 的注释说明它是有意留的），而且**这不是我这条车道能裁的**——它动的是运行时与后端的契约。

## 我按默认建议做了什么

按第 1 条执行：**拒绝留着**，`87c366609` 的区域形式与 `7ee2c3213` 的调用提升都不受影响；这条拒绝继续落在循环自己的位置上，一句话说清"只有按区域跑的循环才提升得了"。等有生产者（哪个 pass、哪个 lowering 开关把区域拆成两张图），再照第 2 条的形状一次做对，并补一条能读数的用例。