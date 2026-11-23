# 「这是不是一个真正的 OPER 记录」该由视图答，还是每个读者自己拼两步
2026-11-23 ｜ 来自 `lane/view-surface`（`C-1123-59` 的 a 项）｜ 对象：`kernel/graph/graph_view.h`、`rewrite/fact.cpp#Source`、`rewrite/module.cpp#stepsOf`、`kernel/runtime/node_roles.cpp`

## ① 事实：`kindOf(ref) == NodeKind::Oper` 不回答这个问题

两个适配器都把 CAST、COPY、FILL、ACCS 折进 `NodeKind::Oper`（`gc_graph_view.cpp#viewKind`、`draft_view.cpp` 的同一张表），而这四个从 `operatorOf` 拿到 `kNoOper`——它们身上没有算子记录。于是「这个节点是不是一个真正的 OPER 记录」在视图上是**两个查询拼起来**才答得出的问题：

```
kindOf(ref) == NodeKind::Oper && operatorOf(ref) != kNoOper
```

**谁要它**（今天真的在问这条的读者）：

- `kernel/runtime/node_roles.cpp:30-36`：本文件里一个 13 行的自由函数 `operatorOf(graph, ref)`，把这两步包成 `std::optional<OperId>`，`kNoOper` 归 `nullopt`；注释写着「`NodeKind::Oper` is not enough on its own」。效应那条角色（`:47-64`）就靠它区分「不是算子」和「是一个算子、但表里没说」。
- `stdlib/rewrite/module.cpp#stepsOf`（`3798-3805`）：数「图里有几个算子」，成员是 `graph.node(ref)->kind == GCNodeKind::Oper`。它**故意**数的是存储的 OPER，不数那四个结构种类——因为一轮的参数是「算子」，CAST/FILL 不是。
- `stdlib/rewrite/fact.cpp#Source`：`kind`（`178-180`）读的是 `GCGraph`/`GraphDraft` 的头，`operOf`（`184-193`）再从 payload 里重读 `GCOperBody`。这个结构整个不认识视图，就为了答这一条。
- `stdlib/test/facts_compare.cpp:201`：`operatorArrangeable(view.operatorOf(node))`——它把 `kNoOper` 连同「不是 Oper 种类」一起当成「不参与重排」。

**同一个 `kNoOper`，今天被读成两个相反的意思**：

| 读者 | 它读成什么 |
| --- | --- |
| `node_roles.cpp:30-36`（角色注册表） | 「不是一个算子」→ 这个节点没有角色 |
| `stdlib/onnx/module.cpp:188-196`、`stdlib/tensor/module.cpp:277-281` | 「不是一个算子」→ 不报/不给角色 |
| `kernel/facts/facts.cpp#termsOfOperator`（`905-925`） | 「一个没人声明过的算子」→ 四项全保守（有副作用、可能故障、不可重排） |
| `stdlib/test/facts_compare.cpp:201`（经 `operatorArrangeable`） | 同上，保守 |

也就是说，一个 CAST 节点在角色注册表那里「不是算子」，在事实层的可重排判断那里「是一个没人声明过的算子」。这不是假设，是今天的读数——而它做出来的正是新 `graph_view.h` 那段警告的事：**`kNoOper` 一说两义，读者按自己的需要挑一个。**

**相邻的另一个问题已经有答案，只是不在视图上**：`kernel/runtime/oper_shape.h#shapeOfOperation`（`:85-98`）对两种形态都答「这个节点做四件事里的哪一件」——OPER 节点读记录里的 `shape`，那四个种类读 `shapeOfKind`。视图上没有对应的查询。所以今天一个视图读者想问「这个节点干什么」也没有入口。

## ② 现在怎么绕过去

三条路，各写一遍：

1. 拼两步，包成 `std::optional`（`node_roles.cpp`）；
2. 直接绕过视图读表示（`fact.cpp#Source`、`module.cpp#stepsOf`，还有 `graph/nodekind.cpp` 这类按存储种类的表）；
3. 只问 `kindOf`，把四个结构种类当成算子记录（任何这样写的地方都会把 CAST 数成算子）。

`C-1123-55`（阶段 3a）要把 `src/stdlib/{rewrite,transforms,autodiff}` 与 `src/kernel/rewrite` 从直读 `GCGraph`/`GraphDraft` 改走只读接口。上面第 2 条是那条车道的主要阻力之一：`stepsOf` 一旦改问视图，用 `kindOf` 会把 CAST 数进去，用两步才是今天的读数——也就是说，**迁移时这条查询缺了就会静默改读数**。

## ③ 加上之后哪些调用点简化，不加的代价

会简化：

- `node_roles.cpp` 的 13 行局部帮助函数变一次调用，它上面那条「`NodeKind::Oper` is not enough on its own」的注释可以删掉；
- `fact.cpp#Source::kind`/`operOf` 这一对不必为了这条而留在表示上（配合 `C-1123-59` 的 b 项，`Source` 才有可能只剩一个 `GraphView`）；
- `module.cpp#stepsOf` 迁到视图时不需要在本地重写「存储的 OPER」这条判断；
- `facts_compare.cpp:201` 不必让 `operatorArrangeable` 自己处理 `kNoOper`。

不加的代价：

- 同一个判断在三个地方各写一遍，其中一处（`node_roles.cpp`）还带一整段解释它为什么必须这么写；
- 任何只读视图的读者想区分「算子记录」与「结构种类」，都必须先拿到 `GCGraph*`；
- 迁移期最危险的一格：`stepsOf` 的读数会随写法变（数不数 CAST），而两次读数都编译得过、都不报错。

## ④ 我的默认：加，而且只加在头文件里

在 `GraphView` 上加

```cpp
/// True when the node holds an operator record: an OPER node whose payload names one.
/// `NodeKind::Oper` alone is not this answer -- the view folds CAST, COPY, FILL and ACCS
/// into that kind and they name no operator (see the file comment).
bool isOperRecord(NodeRef ref) const {
    return kindOf(ref) == NodeKind::Oper && operatorOf(ref) != camel::core::kNoOper;
}
```

理由：它是**一条定义**，不是一份新答案——和 `NodeRange`/`nodes()` 一样写在头文件里，由已有的两个查询拼出来，两个适配器都不用改（`kindOf`/`operatorOf` 正在另一条车道上被改，这条默认不碰它们）。`operShapeOf` 是**另一个**问题（节点做什么，不是它有没有记录），要不要一起上请分开裁；我按默认建议只做 `isOperRecord`。