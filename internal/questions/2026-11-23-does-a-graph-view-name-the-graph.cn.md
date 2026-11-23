# 图的名字要不要在视图上，以及一份草稿答它的什么
2026-11-23 ｜ 来自 `lane/view-surface`（`C-1123-59` 的 c 项）｜ 对象：`kernel/graph/graph_view.h`、`stdlib/rewrite/module.cpp#isRuleSide`

## ① 事实：唯一的读者拿名字当一个标记用

`stdlib/rewrite/module.cpp#isRuleSide`（`459-464`）：

```cpp
/// Whether a graph is one side of a rule: those are the patterns, not part of the program the
/// rules are applied to.
bool isRuleSide(const camel::runtime::GCGraph &graph) {
    const std::string &name = graph.name();
    return name.ends_with("__lhs") || name.ends_with("__rhs") || name.ends_with("__when");
}
```

三处调用（`514` 的 `programValueTypes`、`1042` 的改写轮、`1711` 的饱和轮）都是「跳过规则那一侧」。**这是这条查询唯一的目的：把一张图排除出「程序自己的图」。**

`name()` 存在哪里：`GCGraph::name()` 读 `debug_->name`（`graph.cpp:2998-3001`），也就是编码时由 `createGraphDebugRecord(mangledName, name)` 写进去的那一份（`graph_encode.cpp:707`、`draft_session.cpp:517`）。所以它和 `originOf` 是同一类读数——**编译/调试元数据，不是内容**。视图上 `originOf` 已经在答了（「0 when unknown」），`name()` 没有。

**草稿答不出这个名字。** `GraphDraft` 没有名字：名字是 `GraphDraft::encode(mangledName, name)` 的**参数**（`draft.h:1278`），由调用方在编码那一刻给。`draft.h` 里与名字有关的只有逐节点的 `nodeName`/`setNodeName`。

## ② 现在怎么绕过去

只有一条路：`isRuleSide` 收 `const GCGraph &`。三处调用都在 `collectReachableGraphs(*base, ...)` 的循环里，而 `collectReachableGraphs` 交出的就是 `GCGraph *`（`kernel/runtime/reachable.h:20-22`），**所以今天这三处手里本来就有图**——这条查询目前并没有让谁多带一个指针。

代价在下一步：`C-1123-55`（阶段 3a）要迁的正是这些「遍历可达图」的读者。一旦遍历改为交出视图（那是它必然的方向，否则视图永远只是旁路），`isRuleSide` 就成了那个必须回去拿 `GCGraph *` 的地方——而它只需要一个字符串后缀。

## ③ 加上之后哪些调用点简化，不加的代价

会简化：

- `isRuleSide` 改收 `const GraphView &`，三处调用改成 `isRuleSide(graph->view())`，形状不再钉在表示上；
- 任何只读视图的读者可以问「这张图叫什么」，而不必为了一个名字拿整张表示；
- 和 `originOf` 一致：视图上「编译/调试元数据」这一类读数就有了两个成员，而不是一个。

不加的代价：

- 视图的承诺是「一个 API 代替学两个」，而这个名字只有一半能答（冻结图能、草稿不能），于是任何视图读者都得知道「名字只有冻结图有」——正好是这条面存在要消除的那种知识；
- 迁移期每个遍历图的读者都要为它留一条回到表示的路。

## ④ 我的默认：加，但先只答冻结图，并且把「草稿答什么」写清楚

```cpp
/// The graph's name: the label a debugger and a dump use, empty when it has none. Compile and
/// debug metadata, not content (compare `originOf`). A draft answers empty: a draft is not
/// named until it is encoded, and the name is `encode`'s argument.
virtual std::string_view name() const = 0;
```

理由：它对冻结图是**已经存着的一格**，答它不是新语义；对草稿答「空」也是实话，而且「空」在这条面上已有先例（`originOf` 答 0）。这样 `isRuleSide` 可以立刻改收视图，而 `input-views` 那一格留待裁定（见下）。

**要请裁的另一半**（我按默认不动）：`isRuleSide` 用名字后缀判断一件**语义**上的事。两条路各带代价：

- **(甲) 就答名字**（上）：改动最小，但「一张图是不是规则的一侧」继续由一串字符决定，而字符串是调试元数据；
- **(乙) 让图标示它自己**：图上加一个「这是一侧规则」的标记，`isRuleSide` 读标记。更直接，但那是**新存一格可推导的东西**（设计原则 2.3 说能从内容推出来的不存），而且今天没有任何东西需要它除了这一条判断。

我建议先按 (甲) 落地名字查询（它无论如何都要有，`std::cml` 打印器、诊断、`explain` 都在用名字），(乙) 另开一页，不夹在这条车道里。