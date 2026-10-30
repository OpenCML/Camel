# 顺序边的「写」与「判」：同一句话、同一处谓词

**这条不变式的拥有者是「完工的图」，不是写边的那些地方。**

判定必须在与检查**同一时刻**、对**同一个**节点问同一个问题。写边的时候节点还不完整——`nodeIsFreelySchedulable`
对 `OPER` 要求 payload 里已经有算子（`src/runtime/graph.cpp:1196-1209`），而节点还在构建时 payload 是空的，
于是「写时」它答「有人排它」，一切守卫都放行；等到图冻结、payload 落定，同一个节点答「自由」，封印检查便
拒掉那条当初看起来合法的边。**今晚四个写者守卫与一个 choke point 全部无效，原因都在这里**：
`Builder::rehomeOrderEdges`、`importGraph`、`replaceOrderEdgesAt`、`spliceOrderEdgesOver` 都问得太早，
而插桩（`setOrderInputs`、`appendUserRef` 的 Order 槽、`replaceAllOrderUses`）全静默——那条边根本不是在
这些地方被追加的，是「写的时候以为它要排序」的错觉。

所以过滤只落一处：**草稿装箱成图的那一刻**（`src/runtime/draft_session.cpp` 的 `describeDraftNativePayload` /
`emitDraftNativePayload` 一带），用与冻结后同义的判据问一次，问出「自由」就把边去掉。`graph_edges.cpp:344`
那条判定留着——它是安全网；过滤让它不再触发，而不是替代它。

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

## 现状：谓词已在，过滤未落

这一笔只立了谓词与三条搬边路径的守卫，**它还没有清掉那一对边**。插桩之后的结论在上文：七个可能的写者
（上面四处 + `setOrderInputs`、`appendUserRef` 的 Order 槽、`replaceAllOrderUses`）全静默，边不是在写边处
产生的，而是「写时它看着还要排序、冻结后它自由」造成的。所以下一笔不是继续找写者，而是那处装箱期过滤：

* 落点：`src/runtime/draft_session.cpp` 的 `describeDraftNativePayload` 之前（`draft_session.cpp:512-535`）；
* 判据：冻结后同义的 `nodeTakesNoOrder`——注意它必须拿到每个节点的算子，而草稿自己没有算子表，
  算子由 `DraftPayloadPlan`（`src/runtime/graph.cpp:698` `planDraftPayload`）决定，所以过滤要接在这份计划上，
  而不是在草稿里就地分类；
* `graph_edges.cpp:344` 的判定不动，它是安全网。