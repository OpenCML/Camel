# 删掉 BRCH→臂的 ctrl 之后，为什么 `std::opt` 之后有 6 条语句不跑了

**一句话**：把 `linkArmEntries` 里 `linkNodes(LinkType::Ctrl, brchNode, node)` 那一行删掉（`src/compile/gir/builder_control.cpp:2861`，即 BRCH 与臂入口之间那条控制边，`docs/design/effects/edges.cn.md` §5.2 说" BRCH 与臂之间没有边，只靠区域归属"），**同一张图、同样的节点、同样的边数记录之外只少了这一条边**，`entering.positions.*` 的打印从 8 行掉到 2 行。

## ① 两串打印（`test/cases/consistency/entering/positions.cml`，`std::opt` + `std::nvm`）

保留下来的那份（`test/plans/feat/consistency/golden/entering_positions.txt`，8 行）：

```
7 0 ok 3 value 4 value -1
int int? int?
5 9 a
1a -1b
5 null value 6
[1, null, 3] [4, null, 6] [10, null, 11] [[1, null], []] 4 7 0
3 null 4 -1
one -
```

删掉那条 ctrl 之后（stdout 全文，2 行）：

```
big
value 2
```

**后面 6 行一个字都没打**——不是次序挪了一位，是那些语句没有跑。

## ② 三份名单都相同（所以问题不在"谁被删了"）

在 `std::opt` 内部打点，改前/改后各一份，逐行比对：

| 名单 | 改前 | 改后 | 差集 |
| --- | --- | --- | --- |
| `replaceNode` 删掉的节点（id + kind） | 45 条（Oper 39、kind=2 两条、Gate 4 条） | 45 条 | **空** |
| `replaceNode` 交接的 `chainPreds` / `inheritedWait` | `(1,1)`×4 + `(0,1)`×2 | 相同 | **空** |
| `bypassValueGate` 看到的 `ctlIn` / `syncIn` / 值用户数 | 44 条 Gate | 44 条 | **空** |

**成员、交接、旁路三处都没有变化，输出少了 6 行。**

## ③ 六条语句在不在计划的 `live` 集合里

在 `src/execute/graph_runtime_support.cpp` 的 `computeExecutionOrder` 里打点（`live` 构造之后），跑同一张图：

```
PROBE-LIVE nodes=370 live=27 deadStatements=19 ids=99,105,112,147,160,174,188,193,196,199,202,208,214,229,243,257,264,270,276,…
```

- **不在**：370 个节点里只有 27 个进了 `live`，**另有 19 条"在同步链上"的语句不在 `live` 里**——而 `live` 是"只有出口依赖的东西才会被排进计划"的那张集合。
- 但把 `live` 的种子补成"所有同步链上的语句"之后，**那 7 条红一条都没变**（`entering.positions.*` 5 条 + `entering.graph_*` 2 条）。

**两件事同时成立**，所以需要作者判一句：

1. `live` 的算法漏掉了 19 条语句（它们有同步前驱，按 §1.5 "同步边也承载'被使用'"应当算执行的一部分）；
2. **把它们补进 `live` 并不能让它们跑**——这说明就算进了计划，臂的跳转/入口逻辑（`BranchArmEntries::entry`、`plan->branchArmEntries`）也没有走到它们。

## 请作者回答的那一问

**删掉 BRCH→臂的那条 ctrl 之后，一条"只在同步链上、下游没人读"的语句，凭什么算作执行的一部分——靠 `live` 的种子（活性），还是靠臂的区域归属（计划）？**

- 若"靠活性"：`live` 的种子应当把"同步链上的语句"种进去，那么请一并判"为什么补进去之后它们仍然不跑"（是臂入口没算对，还是计划跳过了它们）。
- 若"靠区域归属"：请判"臂的入口与范围该由什么定"——现在 `BranchArmEntries` 取"臂区域在计划序里的第一个节点"，而计划序本身又来自 `live`。

**这一问不能用"补齐快照"或"重录 golden"绕过**：8 行与 2 行是两种行为，不是两种记录。