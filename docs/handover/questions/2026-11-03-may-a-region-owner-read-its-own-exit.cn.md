# 一个区域的拥有者能不能读自己区域的端口？

## 问什么

判据收紧成"只读自己和祖先区域的端口"之后，全量 fast 上多出 4 条红、2 个程序，形状只有一种：
**`LOOP` 节点读它自己那个体区的出口端口**。作者的反问是"外层读它，读到的是哪一轮？"；量下来的回答是
**每一轮，读者就是那台循环自己**（第 4 节）。要裁定的是这一种读：放行，还是拒绝。

## 1. 两个程序

`test/cases/compile/expected_type_loop/loop_parts.cml:23-28`：

```camel
func through_match(a: int, c: float): int[] {
    return [match a {
        case 1 => loop s = 2 for q in [c, 3.5] do s
        case _ => 0
    }]
}
```

`test/cases/loops/zero_leaf_state.cml:11-13`：

```camel
func carried(n: int): Nothing {
    return loop s = {} for x in 0..n do s
}
```

## 2. 两段违规原文

```
camel: internal error: Runtime graph 'through_match' breaks its edge invariants (1 violation(s)):
  graph 'through_match' node 10 in region 1 (Arm parent 0): it reads port 9 of region 2 (LoopBody parent 1), which is not the region the node runs in nor one it sits in
```

```
():?:?: [Error]: Pass 'std::opt' failed with an internal error: the draft of graph 'main' cannot be encoded: graph 'main' node 16 in region 1 (Inline parent 0): it reads port 15 of region 2 (LoopBody parent 1), which is not the region the node runs in nor one it sits in
```

## 3. 图：谁是读者，读的是哪个端口

`std::jgir` 取的 `through_match`（节点号与违规文本里的号一致）：

| 区域 | 种类 | 拥有者 | 父 | 出口 |
| --- | --- | --- | --- | --- |
| `g6r1` | `arm` | `g6n3`（`BRCH`，`match`） | `g6r0` | `g6n10` |
| `g6r2` | `loop_body` | `g6n10`（`LOOP for`） | `g6r1` | **`g6n9`（`PORT`）** |

边：`g6n9 -> g6n10 slot 0`，种类 `context` —— 违规文本里的"node 10 读 port 9"就是这一条。
读的不是某一轮的副本：`g6n10` 就是拥有 `g6r2` 的那台循环，它按轮次消费自己体区的出口。

## 4. 为什么只有 `do s` 时出口是 `PORT`，而 `do s + 1` 是 `OPER`

`LOOP` 的 context 端口接的是**体区的出口值**（`region.result`），而出口值是什么由 `do` 表达式决定：
`do s` 的出口就是那个被携带状态的端口本身（`PORT`），`do s + 1` 的出口是一个 `:op/add` 节点（`OPER`）。
这条检查只走 `PORT` 输入（`kindOf(input) == GCNodeKind::Port`），所以后者今天根本看不见 —— 同一个程序把
`do s` 写成 `do s + 1` 就不报（实测：`if f then loop s = a for q in [c, 2.5] do s + 1 else 0` 通过）。
换句话说，被拒绝的从来不是"外层读某一轮"，而是"体区的出口恰好就是状态本身"这一个退化形状。

## 5. 量：全语料里两种读各有几处

3542 个 `.cml`（`test/cases` + `examples` + `stdlib` + `benchmarks`），其中 623 个产出图、1200 个图、
**4523 处跨区域 `PORT` 读**（按检查自己的口径：读者不在根区域、端口在别的区域）：

| 类 | 处数 |
| --- | --- |
| 祖先（读者在端口所在区域内，或在其祖先区域内） | **4522** |
| 拥有者（`reader == region(portRegion)->owner`） | **1** —— `loop_parts.cml` / `through_match`：`g6n10` 读 `g6r2` 的 `g6n9` |
| 既非祖先也非拥有者 | **0** |

按区域对：`arm <- root` 3766、`arm <- loop_body` 389、`loop_body <- root` 167、`loop_cond <- root` 127、
`arm <- loop_cond` 53、`block <- root` 12、`loop_body <- loop_body` 4、`arm <- body` 2、`loop_body <- body` 2、
`loop_cond <- loop_body` 1。全语料里第二种红（`zero_leaf_state`）不在上表里，因为它的循环在 **`std::opt`
编码的草稿**里才被内联进 `Inline` 区，而上面这一列数的是建图器直接产出的图。

## 6. 三条路与代价

- **（甲）给判据加第三句"拥有者可以读自己区域的端口"。** 代价：规则多一句；换来的是建图器里
  "状态原样传下去"这个正当写法不改。`owner` 每个区域只有一个节点，所以这不是回到"同一条链两个方向"。
- **（乙）拒绝，改这两个程序。** 代价：为迁就检查改程序（`do s` 要写成 `do s + 0` 之类），而改掉的
  恰好是"循环体的值就是状态"这个最朴素的形式；且 `do s + 1` 同形不报，规则的解释力反而更差。
- **（丙）只放行 `region.result == 那个端口` 的读。** 代价：在这个形状上与（甲）等价，但留下"拥有者读
  自己区域里别的端口"这条口子，将来还要再议一次；而规则写在端口上，不写在"哪个端口"上，就与（甲）一样简单。

## 7. 默认

**（甲）允许拥有者读自己区域的端口**，并按这个默认已落地：
`regionIsVisibleFrom(portRegion, reader) = isAncestorOf(portRegion, reader) || region(portRegion)->owner == reader`。
规则写进 `docs/design/effects/edges.cn.md` 第 6 节（三句），全量 fast 上 4 条新红归零、
`regions.*` 11 条与 `graph_faces.*` 2 条仍全绿。