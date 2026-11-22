# `Graph` 是 `GCGraph` 改名，还是两者共有的基类？

**状态**：按默认建议当轮开工（2026-11-22 21:20，图车道 `lane/stage2-graph` 撞点）。作者可见此页改判。

## 问

`kernel/graph/include/camel/graph/graph_view.h` 的 `CalleeRef` 持有 `const camel::graph::Graph *`（`:238`），`graph_view.h:94` 与 `kernel/facts/include/camel/facts/facts.h:90` 都只是 `class Graph;` 前置声明。**全树没有任何 `camel::graph::Graph` 的定义**，它与 `camel::runtime::GCGraph` 之间没有任何继承或别名关系。

于是 `GraphView::calleeOf` 无法实现：它必须把存着的 `GraphId` 变成一个 `CalleeRef`，而 `CalleeRef` 指名要 `Graph *`。读方还必须从那个 `Graph *` 走回一个视图 —— `FrozenGraphAdapter` 的构造要 `const GCGraph &`，因此 `Program::graph(GraphId)`（`kernel/runtime/include/camel/runtime/program.h:79`、`:86`）现在答 `GCGraph *`，也要改答 `Graph *`。

**「把 `GCGraph` 改名为 `Graph`」与「让 `Graph` 成为一个基类、两个表示都从它派生」是两件不同的事**：前者会删掉 `GCGraph` 这个名字而草稿表示没有归属，后者要动 `GCGraph` 的定义并让 `Program::graph` 的返回类型收窄到基类。**这一条不改，图车道的 2.1 一次性三项全部停在零。**

## 默认建议：**`Graph` 是基类，`GCGraph` 派生**

理由三条：

1. **`graph_view.h` 是审过的接口头，`CalleeRef` 已经写成 `const Graph *`。** 让它成立的最小改动是**给 `Graph` 一个定义**，而不是改头文件。
2. **今天有两个表示。** `draft_view.cpp` 那条适配层同样要给出 `CalleeRef`。若 `Graph` 只是 `GCGraph` 的新名字，草稿那侧就没有可指的类型。
3. **阶段 3 的计划（`93242e85b`「one graph representation under the view」）本身把「合一」排在后面。** 那说明今天本该是「两个表示、一个视图」，而一个共同的基类正是这句话的最小形状。

具体形状（图车道按此开工，作者可改）：

```cpp
// kernel/graph/include/camel/graph/graph.h —— 新增
namespace camel::graph {
class Graph {          // 今天只有虚析构与「这是哪一类图」的说明，不放任何其它成员
public:
    virtual ~Graph() = default;
};
}
```

- `camel::runtime::GCGraph` 与草稿表示各 `: public camel::graph::Graph` —— **加基类，不改任何已有成员**。
- `Program::graph(GraphId)` 保留现有返回类型**不动**；`CalleeRef` 的构造处用 `static_cast`，并在那一处写一句为什么这是安全的（该 `Graph *` 的来源就是 `Program` 里的那一张）。**先不动 `program.h` 的签名**，因为它是审过的接口头。
- 若作者判「改名」：那么 `draft_view.cpp` 那条适配层要给出一个 `GCGraph` 而它其实不是 GC —— 那会把「两种表示」压成一种，阶段 3 之后再改回来。

## 第二处（独立，已按「等 2.3」处理）

求导提升读的是**每个节点自己的算子实例字节**：`lib/std/autodiff/jvp.cpp:1577`、`:1597` 的 `GCFillBody`（fill 种类 + 槽表）与 `:1749`、`:1760` 的 `GCAccsBody`（种类 + 下标/键），读完按它重建。视图把这四种折成 `NodeKind::Oper`，而 `operatorOf` 明确拒绝回答（`gc_graph_view.h:60-65`），任何查询都不交出实例参数（`graph_view.h:386-398` 只有 `OperId`/`LoopForm`/`CalleeRef`）。

**默认：等 2.3。** 视图给的替代（问算子记录）是 2.3 的活；即便落地，视图仍缺「一个节点的算子实例参数」这一读法 —— 那是 2.3 收尾时视图要补的最后一项，**不在 2.1 里补**，否则 2.1 会顺手把实例参数塞进视图，那是 2.3 的设计决定。

## 顺带纠正一条判据

图车道的字面判据 `git grep -c 'GraphRead'` 到不了 0：`test/tools/writing-freedom.mjs:389`、`:491` 有一个同名子串 `sameGraphReading`。**判据应写成 `GraphRead\b`**（或改那个函数名 —— 但那是另一个模块的读者，不要顺手改名）。
## 决策者答（2026-11-22 22:05）

**同意默认：`Graph` 作基类，两种表示派生。** 这是过渡形状——阶段 3c 换成一种表示后，`Graph` 就是那一种表示本身，基类随之消失。补一处：`Graph` 上加一个纯虚的 `const GraphView &view() const`，否则拿到 `CalleeRef` 或事实锚点的 `Graph *` 的读者走不回视图；两个表示各自交出自己的适配层（3c 之后视图由表示直接实现）。`static_cast` 那一处照你写的注明来源。第二处（提升读 `GCFillBody`/`GCAccsBody`）同意等 2.3；判据改成 `GraphRead\b` 同意。

（2026-11-23 复核：已答，归档。）
