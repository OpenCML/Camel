# 一问：GIR 的 handle 应该名字一个事务，还是名字一张草稿？

> 状态：**等作者裁决。**
> 相关：`src/compile/gir/builder_support.h:117`（`nodeDraftOf`）；`include/camel/compile/gir/draft_graph_builder.h:52`（`draft()`）；对照 `modules/graph/read.cpp:119`（`NodeView` 已经是"名字一个事务"）；`docs/handover/reviews/graph-store-step2-b-ledger.cn.md` §46。

## 现象

编译期的图由 `DraftGraphBuilder` 拿着，节点是一个 `node_handle_t`：

```cpp
struct node_handle_t {            // builder_support.h
    runtime::GraphDraft *draft;
    runtime::gc_node_ref_t id;
};
inline runtime::GraphDraft *nodeDraftOf(node_handle_t node) { return node.draft; }
```

**这个 handle 是今天唯一一处能绕开事务、直接拿到可写草稿的地方。** `nodeDraftOf` 有 **66** 个调用点，`DraftGraphBuilder::draft()` 有 **132** 个调用点，两者加起来是 b6 剩下的最后一块——`docs/…ledger.cn.md` §44 的 389 里，这一层占了 9 处代码引用，但**真正卡住的不是那 9 个拼写，是这 198 个调用点的形状**。

## 为什么不能自己定

b6 的判据是"**谁能拿到草稿**"。`modules/graph` 已经答过一次：`NodeView` 收的是事务，读图要问它（`read.cpp:119`）。**GIR 这一层当时没答**，因为编译期要造图、要写图，而"造的时候收事务（那是所有者）"这条规矩在 2026-10-30 才定下来（§41 的 b6 第一条）。

**今天两处说的不是同一句话**：运行期的 handle 名字一个事务，编译期的 handle 名字一张草稿。**这不是错，是还没决定。**

## 选项

- **① handle 名字一个事务**（与 `NodeView` 同型）：`nodeDraftOf(node)` 变成 `nodeTransactionOf(node)->graph()`；要写的走 `->edit()`。
  **代价**：`nodeDraftOf` 的 66 个调用点**形状不变**（仍是"向它要图"），但 **`DraftGraphBuilder::draft()` 的 132 个调用点要迁**——`draft()` 这个访问器一删，132 处都得说清自己是读还是写。分两笔：先 `DraftGraphBuilder` 收事务，再 handle。
  **收益**：handle 不能再被复制成"一张没有所有者的草稿"；b6 的最后一个例外消失。
  **验收**：账本 `constructions` 不变（仍只在事务里造），`node_handle_t` 里不再有 `GraphDraft`，整闸绿且 `tiles`/`gir` 的黄金文件一字不改。
- **② 保持名字草稿**：0 改动，把这条写进设计原则作为"编译期的例外"。
  **代价**：要在 3.6 旁边加一条例外，而这条例外**正好是 b6 想消灭的那件事**；且下一个人仍能从一个 handle 拿到可写草稿，绕过事务的写入没有任何地方拦。
  **验收**：原则里新增一句例外；账本 `references` 停在今天这个数上，b6 记为"有例外地完成"。
- **③ handle 同时存事务和草稿**：`node_handle_t { GraphTransaction *txn; GraphDraft *draft; }`。
  **代价**：**两条真值来源**，两者不同步时没有任何断言能发现；拼写多一个，保证一条没多。
  **反对。**

**默认建议：①**，分两笔走（先 `DraftGraphBuilder::draft()` 的 132 个调用点改成 `transaction().edit()` / `transaction().graph()`，再把 handle 换成事务）。理由是它让"运行期的 handle"和"编译期的 handle"说同一句话，且**第一笔就能把 132 个调用点按读/写分成两堆**——那本身就是一次有价值的清点。

**要作者答的一句**：GIR 的 `node_handle_t` 应该名字一个事务（①，与 `NodeView` 同型，132 个调用点要迁），还是名字一张草稿（②，原则里记一条编译期例外）？

**在裁定之前**，GIR 这一层不动：`nodeDraftOf` 与 `DraftGraphBuilder::draft()` 保持原样，`docs/…ledger.cn.md` §46 把这条记为"b6 的最后一块，等答"。