# 问：`sync` 链上「可能出故障」的边界，是「显式声明了故障」还是「没声明就当作可能」？

日期：2026-10-30　分支：`lane-eff-rule`（本条规则在这支上，不在将合入的 `lane-eff` 上）

## 现象

上一轮批准的那条建图规则（`2da47b1d1`，`src/compile/gir/builder_helpers.h` 的
`statementKeepsItsChainPlace`）是：

```cpp
(hasEffect && !nodeStandsForItsRegion(node)) || nodeMayFault(node)
```

后半 `nodeMayFault` 读的是算子的 trait 位，而全部 trait 只有一个来源
（`src/core/operator_table.cpp:46`）：

```cpp
// No fault list: undeclared, may fault. An empty list: it never faults.
.mayFault = !facts.faults || !facts.faults->empty(),
```

也就是说：**只有显式写了 `faults = []` 的算子才算「不会出故障」，没写的都算「可能出故障」。**
而现在语义表里几乎没有算子写 `faults`，于是几乎每个算子都算可能出故障。

后果不是抽象的：`sync` 块里**每一个调用**（包括写在表达式里的 `tensor:sum`、`tensor:reshape`、
`chain(x)`）在「数据边已经给了顺序」的情况下仍然被判为应当占链位，于是成对写 SYNC+CTRL。
实测量：`test/cases/modules/tiles/region_execution.cml` 的 `main`，链从 6 段变成一条覆盖
18 个节点的链（`std::gir` 里 `reshape -> chain -> sum -> println -> …` 一路 SYNC），
`tiles::test_outline_host_resident` 的 `TILES_OUTLINED` 从 base 的 **10** 掉到 **5**。

**对照实验（这一页最值钱的一句）**：我把 `nodeMayFault` 的 Oper 一支从
`traits->mayFault` 收窄成「语义里显式声明了 faults」（`semantics()->faults` 有值且非空），
重新构建后 `TILES_OUTLINED` **只从 5 涨到 6**。也就是说 10→5 掉的 5 个区域里，
**只有 1 个是「可能出故障」这一半吃掉的，另外 4 个是 `isEffectNode` 的效应半吃掉的**
（凡带外部效应的算子都成链位——`println` 本身是效应，`sum` 因为「可能出故障」也进来了，
而 `main` 里那 5 个 `println(...)` 表达式的区域大多数是被效应半判成语句的）。

## 两条路的代价

**路 A：判据改成「显式声明了故障才算可能出故障」。**

- 得到：`sync` 链回到只带「真声明会抛」的语句，表达式里的纯值不再成链位，tiles 类读数接近 base。
- 代价：语义表里现在基本没人写 `faults`，等于把这一半**事实上关掉**。于是
  「看着纯、其实会抛」的语句（越界、除零、分配被拒、不能承载的转换）在**内联之后**
  又回到「数据边已给顺序就不占链位」，也就是第 9 条要堵的那个洞只堵了一半——
  `nn_grad_check` 那 18 条能过是因为我把 splice 的链接上了，但换个形状仍可能掉出链。
  这不是可以靠「以后补 `faults` 声明」解决的：补声明要动整个 stdlib 的语义表。

**路 B：判据保持「没声明就当作可能出故障」（现状）。**

- 得到：保守方向正确，4.6 的「可能出故障」本来就是要保守；第 9 条的洞不会从这一半漏。
- 代价：`sync` 块的链变成「整棵表达式树」。跟着变的有两类读数，实测（`lane-eff-rule` 全量
  fast）：`total 7804 / pass 7725 / **fail 70** / skip 7 / known 2`，其中
  - tiles 区域计数 22 条（`compiled_region_*` 4、`host_resident_*` 4、`outlined_*` 4、
    `native_region_*` 5、`preferred_resident_*` 4、`public_region_graph` 1）；
  - `algebra_rules.*` / `camel_passes.*` / `saturate.*` 共 19 条；
  - 图快照与行为读数约 29 条（`gir.*`、`boundaries.*`、`sync.ctrl_dedup_snapshot`、
    `entering.*`、`comprehension.*`、`rename.*`、`poison:*`、`01/02/05/08_*`、docs/tutorial）。
  这 70 条里**没有一条**是「仅 SYNC+CTRL 成对、端口编号没挪」，所以一条都不该重录。

**路 C（另一页要问的那条）**：判据不动，改的是「谁算语句」——见
[`2026-10-30-what-counts-as-a-statement.md`](2026-10-30-what-counts-as-a-statement.md)。

## 默认建议

若只在 A、B 之间选，**选 B（保持现状）**：把「没声明」读成「可能出故障」是保守方向，
A 会让第 9 条刚堵上的洞重新开口，而这个洞是**行为错**（语句不执行），比读数漂移严重。
B 的代价不该由这一半来收，该由「语句」的定义来收（路 C，另一页）。
在此之前，`TILES_OUTLINED` 那 22 条期望保持 base 的 10 不动——它们是症状，不是病因。