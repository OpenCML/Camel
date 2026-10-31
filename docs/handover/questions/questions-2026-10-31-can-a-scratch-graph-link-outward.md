# 临时 `__typeof__` 图里能不能链接外层图的节点：`linkNodes` 的“同图”不变式今天被合法程序打破

**问题**：`src/compile/gir/builder_support.h:185` 的 `linkNodes` 要求 `nodeGraphOf(from) == nodeGraphOf(to)`（“Compile-time link must stay within one graph”），而 `inferTypeOf` 的临时 `__typeof__` 图里，`Builder::visitAccsNode` 会拿一个属于**外层图**的节点来链接。这条边记在临时图之外的图上，而临时图随后被丢掉。

**为什么要问**：这不是一条“换个形状就撞不到”的检查，是一个**合法程序**（`test/cases/compile/static_expansion/tags_restore_readings.cml`，`std::fvm`）今天就会走到的路径。release 档里这条 `ASSERT` 是死的，所以程序一直“能跑”；把它变成活的 `GUARD` 之后它立刻中止，说明**要么边错了，要么不变式写窄了**。这属于编译器语义，不属于“把检查打开”能定的事，所以它在 gir 那一族里被我单独留在原地（117 处里剩的 1 处），没有顺手改代码。

**实测**（`c12eb2281`，`camel tags_restore_readings.cml std::fvm`）：

```
Camel Invariant Violation:
    Location   : ../src/compile/gir/builder_support.h(185)
    Function   : void camel::compile::gir::linkNodes(LinkType, node_handle_t, node_handle_t)
    Guarantee  : nodeGraphOf(from) == nodeGraphOf(to)
    Reading    : Compile-time link must stay within one graph.
```

gdb 回溯（同一次运行，略去 `fail_invariant` 与 `__pthread_kill` 的帧）：

```
#7  camel::compile::gir::linkNodes(LinkType, DraftNodeRef, DraftNodeRef)
#8  camel::compile::gir::Builder::visitAccsNode(camel::compile::gct::Node* const&)
#10 camel::compile::gir::Builder::inferTypeOf(camel::compile::gct::Node* const&)
#12 camel::compile::gir::Builder::visitTypeMatchNode(camel::compile::gct::Node* const&)
```

`visitAccsNode` 的链接点是 `linkNodes(LinkType::Norm, tgtNode, accsNode)`，`tgtNode` 来自 `valueNodeOf(visit(gct->at(0)))`：在 `match type typeof(tags) { case { width: { w: int } } => … }` 的推型里，被访问的目标节点属于外层图，`accsNode` 属于 `inferTypeOf` 新建的临时图。

**两种可能，判不了是哪一种**：

- **A. 边错**：临时图里的访问不该链接到外层图——那 `valueNodeOf` 在临时图里就该给出临时图里的节点（例如在推型图里重放该值的定义），这条链接是错的，`linkNodes` 的不变式是对的，抓到了一条真缺陷。
- **B. 不变式错**：临时图是**故意**站在外层图里的（`inferTypeOf` 里 `scratch->setOuterGraph(currGraph_)`，注释写明“它站在使用它的那张图里，所以它读到的名字按写处解析”），那么借用外层图的节点就是设计如此，`linkNodes` 的“同图”要求写窄了，该改成“同图或临时图借用外层图的节点”这类真实条件。
- **C. 别的写法**（例如临时图不建节点、只做类型推导），由作者定。

**问作者的话**：临时 `__typeof__` 图里链接外层图的节点，是 A（缺陷）还是 B（设计）？如果是 B，`linkNodes` 该换成什么条件才说得清“谁会让它到达”；如果是 A，我这处留下 `ASSERT` 就不该再留，应该跟着修那条链接。

**堵着谁**：`src/compile/gir` 这一族检查的收尾——117 处里 116 处已经搬完（105 处 `GUARD`、11 处内部错误），剩这一处等裁；不裁的话它在 release 里继续是死的，而家族“0 处”这个读数也就报不出来。