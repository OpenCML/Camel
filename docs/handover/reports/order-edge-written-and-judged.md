# 顺序边的「写」与「判」：同一句话、同一处谓词

一条顺序边的合法性只问一件事：**它的 user 端是不是一个什么都不排的节点**。这句话以前只写在封印检查里
（`src/runtime/graph_edges.cpp`），写边的那五条路各问各的，于是出现过「写了一条自己判死的边」——本次就是
一例（`6 -> 12`、`12 -> 15` 落在纯节点上）。现在这句话有一个名字，冻结图与草稿各一份：

- `camel::runtime::nodeTakesNoOrder(const GCGraph &, gc_node_ref_t)` —— `include/camel/runtime/graph.h`
- `camel::runtime::nodeTakesNoOrder(const GraphDraft &, gc_node_ref_t)` —— `include/camel/runtime/draft.h`

封印检查、`importGraph`、`replaceOrderEdgesAt`、`spliceOrderEdgesOver` 都调它；**新增一条搬顺序边的路时，
先问它再写**，不要重新写一遍判定。

**草稿那一份的边界，三句话**（这是这一层的规则，不只是代码）：

1. **`CAST` 保留它的顺序**：一笔写在「转换可能失败」时的顺序，不会因为之后的类型细化而失效；丢掉它是遍的
   选择，不是缺陷。
2. **拥有区域的 `BRCH` / `LOOP` 保留**：它替区域说话，区域整体与外面代码的顺序就记在它身上。
3. **其余自由的节点不收顺序**：`BRCH`、`JOIN`、`DATA`、`PORT`、`COPY`、`FILL`、`ACCS` 以及可自由重排的
   `OPER` —— 它们没有先后可言，顺序边落在上面什么也排不了。

## 现状：谓词已在，写者未明

这一笔只立了谓词与三条搬边路径的守卫，**它还没有清掉那一对边**：写者仍未定位。已排除四处——
`Builder::rehomeOrderEdges`、`importGraph`、`replaceOrderEdgesAt`、`spliceOrderEdgesOver`。下一笔是插桩
`appendOrderDependency` 与 `setOrderInputs`，让写者自报家门。