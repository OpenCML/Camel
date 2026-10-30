# `sync` 链上「可能出故障」的边界：是「显式声明了故障」，还是「没声明就当作可能」？

日期：2026-10-30　现状分支：`lane-eff-rule`

## 现象

建图那条链位规则（`src/compile/gir/builder_helpers.h` 的 `statementKeepsItsChainPlace`）：

```cpp
(hasEffect && !nodeStandsForItsRegion(node)) || nodeMayFault(node)
```

后半 `nodeMayFault` 读算子 trait，而 trait 只有一个来源（`src/core/operator_table.cpp:46`）：

```cpp
// No fault list: undeclared, may fault. An empty list: it never faults.
.mayFault = !facts.faults || !facts.faults->empty(),
```

即**只有显式写了 `faults = []` 的算子才算「不会出故障」**。现在语义表里几乎没有算子写 `faults`，
所以几乎每个算子都算可能出故障。后果：`sync` 块里**每一个调用**——包括写在表达式里的
`tensor:sum`、`tensor:reshape`、`chain(x)`——在「数据边已经给了顺序」的情况下仍被判为应占链位，
成对写 SYNC+CTRL。

实测（`std::gir`，`test/cases/modules/tiles/region_execution.cml`）：`main` 的链从 6 段变成一条
覆盖 18 个节点的链（`reshape -> chain -> sum -> println -> …` 一路 SYNC）；
`tiles::test_outline_host_resident` 的 `TILES_OUTLINED` 从 base 的 **10** 掉到 **5**；
`lane-eff-rule` 全量 fast 是 `total 7804 / fail 70`。

**对照实测**：把 `nodeMayFault` 的 Oper 一支从 `traits->mayFault` 收窄成「语义里显式声明了
faults」（`OperatorTable::semantics(id)->faults` 有值且非空），重新构建后 `TILES_OUTLINED`
**只从 5 涨到 6**。也就是说 10→5 掉的 5 个区域里，**只有 1 个由这一半吃掉，另外 4 个由
`isEffectNode` 的效应半吃掉**（`println` 本身是效应，`sum` 因为「可能出故障」也进来）。
所以只收窄这一处不能把读数修回去，但它决定了这一半该不该保留。

## 为什么不能自己定

设计原则 4.6 把链的两端写成「带效应的语句，或可能出故障的节点」，`docs/design/effects/edges.cn.md`
5.3 说这样一条边是「语句占链位」的理由。**「可能出故障」这句话没有说它以什么为判据**：
是算子自己声明过的故障，还是「没声明就当可能」这个保守缺省。两种读法在语义表（将来要不要给
每个算子写 `faults`）、在内联是否安全（语句会不会掉出链）、在 tiles 的区域读数上给出不同答案，
而这属于语义/规则层的裁定，不该由改这一处的人顺手定。

## 可执行的选项

**选项 A（判据 = 显式声明）**：把 `nodeMayFault` 的 Oper 一支改成
`OperatorTable::instance().semantics(id)->faults` 有值且非空；并在 `docs/design/effects/edges.cn.md`
4.6 补一句「可能出故障指算子声明过的故障」。派工影响：`lane-eff-rule` 的 70 条里，tiles 那 22 条
中约 1/5 回位，其余不变（因为效应半仍在），所以这一项**不单独**使闸门变绿。

**选项 B（判据 = 没声明就算可能，保持现状）**：不改代码，把 4.6 补成「没声明故障即按可能出故障
处理，声明 `faults = []` 才算不会」。派工影响：`lane-eff-rule` 的读数按现状固定下来，
22 条 tiles 期望要么随「谁算语句」的裁定回位，要么改成 5 —— 后者需要另一次授权。

**默认建议**：**选 B**。把「没声明」读成「可能」是保守方向，A 会让「看着纯、其实会抛」（越界、
除零、分配被拒、不能承载的转换）的语句在内联后重新可能掉出链，而那正是第 9 条刚堵上的洞
（行为错，比读数漂移严重）。B 的代价（表达式节点被判成语句）不该由这一半来收，
该由「谁算语句」来收。