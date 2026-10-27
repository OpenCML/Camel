# 链位上的等待落在纯值上时，该由谁背 GATE

基线 `origin/develop` `32a294f6c`。下面 (1) 的代码位置在 `527cf9018`（2026-10-28 06:54）上取，并在 `18668d4de`（2026-10-27 10:24）上逐字核对过：两处的行号不同，两个都写出。(2) 的三个现场与 (4) 的红是照录 `std::jgir` 的读数，本页没有复跑。

## 我量到的

**(1) 现状：三处引用同一条有意行为。**

- `src/compile/gir/builder_control.cpp:4079-4088`（在 `18668d4de` 上是 `:4053-4062`，正文逐字相同）——链尾的 sync 边只能落在本块独占的值节点上，否则由本块自己造一个 GATE 来背这次等待：

```
4079     // The chain tail's sync edge may only sit on a value node this block alone owns
4080     // (docs/design/effects/edges.cn.md 5.3): a node some other block already reaches by a sync edge
4081     // -- the value of a body the call at this position expanded (5.7) -- is not this block's to
4082     // wait on, so the wait is carried by a GATE of this block's own. An unread GATE stays either
4083     // way: it is what keeps the edge, and the chain it ends, able to reach the graph's exit.
4084     if (!syncInputs.empty() &&
4085         (!nodeIsKind(resNode, runtime::GCNodeKind::Gate) || !syncInputsOf(resNode).empty())) {
4086         node_handle_t gatedValue = currGraph_->addGateNode(nodeTypeOf(resNode));
4087         linkNodes(LinkType::Norm, resNode, gatedValue);
4088         outputAnchor = gatedValue;
```

- 同一个谓词在 `src/compile/gir/builder.cpp:1471-1482`（在 `18668d4de` 上是 `:1351-1362`）——块的值要在链尾等它时，同样造 GATE：

```
1475             // The chain's tail reaches the block's value by a sync edge -- the block's value node
1476             // not being one this block alone owns (docs/design/effects/edges.cn.md 5.3) -- with the
1477             // control edge the kind names the same pair with beside it. The tail may be the node the
1478             // GATE reads, and the wait is then the only edge that reaches the GATE at all, so this
1479             // pair is written as it stands rather than dropped as "already ordered".
1480             writeSyncStatement(lastSyncedNode_, gate);
```

- `src/runtime/draft.cpp:1997-2006`（两处 rev 行号相同）把 5.3 的删除规则写成代码：GATE 有 sync 前驱就不许删、不许用输入替换：

```
1997             // A GATE waits on the sync chain's tail (docs/design/effects/edges.cn.md 5.3): its wait
1998             // is a **sync predecessor**, and 5.3 keeps a GATE with one -- it "may not be deleted,
1999             // and may not be replaced by its input, unless its sync predecessor is already gone".
```

三处与 `18668d4de` 一字不差（`builder_control.cpp` 与 `builder.cpp` 只是行号差 26 行），不是 rebase 丢块。

**(2) 三个实测现场（`std::jgir` 读数，照录）。**

1. `GATE(g1n37)` 读端口 `x`、扛 `control+sync->:io/println`，其值喂 `mul_d = GATE(x)*6.0`，同图另有 `mul_d = x*6.0` ⇒ 同一算式两份，**CSE 不穿透闸门**。
2. `GATE(g1n16)` 读常量 `2.0`、扛 `control->:io/println`；`GATE(g1n21)` 读 `2.0`、扛 `control->mul_d`。于是 `mul_d` 到 `println` 的**直接** control 边挂在闸门上，不在乘积上。
3. `GATE(g1n46)` 读 `:op/dtod`、扛 `control+sync->println`，`println` 的 normal 输入变成这个 GATE（原来直接是 `dtod`）。

**(3) 对应的两条断言（我们不改）。**

- `test/plans/feat/opt/algebra.plan.toml:744-766`（`order.graph`，用例 `test/cases/opt/generic/algebra_order.cml`）：`:762` 要求每个 `:op/mul_d` 的直接 control 等待里有 `:io/println`，即等待要挂在乘积上、不在闸门上。
- `test/plans/feat/opt/gate_value.plan.toml:107-124`（`other_wait.graph`，用例 `test/cases/opt/generic/gate_value_dependency.cml`）：`:118` 要求有一个 `println` 的 normal 输入就是 `:op/dtod`。

**(4) 这三条在 `18668d4de` 自己的基线上就是红的**，不是我们 rebase 出来的。

**(5) 我们自己能修的部分，不占你的裁定。** CSE 与代数折叠**穿透只转发的 GATE**：口径照 `modules/autodiff/pullback.cpp:249-256` 的 `ungated()`（"A gate orders a value after other nodes … it does not change the value"），让纯优化在闸门两侧看到同一个值。现状是纯优化因此漏触发（现场 ① 的两份 `mul_d`），这部分我们修。5.3 的删除规则我们不碰。

## 要问的一句

**一条链位上的等待，当它落在"纯值"（端口 `x`、常量 `2.0`、纯算式 `dtod`）上时，应由这个纯值自己背一个只转发的 GATE，还是应把等待挂在消费它的节点上——也就是"块的值带 GATE"这条要不要扩展到纯值？**

## 没量到的

- 没量到纯值背 GATE 在调度、内存与调试可视化上的代价（多一个节点、多一条 norm 边；本页没有计时或内存读数）。
- 没量到若改成把等待挂在消费节点上，链活性判断会不会退化：`src/runtime/draft.cpp:1997-2006` 那一段的理由正是"没被读的 GATE 也要留着，链才到得了出口"，那一侧的读数本页没有。
- (2) 的三个现场与 (4) 的三条红是照录，本页未复跑。

本页只报测量与一句询问：不改代码、计划、断言或生成物。