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

## 补一次：那 19 条落在哪个臂的范围里

由构造可以确定，不需要再跑一遍：

- `buildExecPlan`（`src/execute/exec_plan.cpp:781`）的 `plan->topo` 就是 `computeExecutionOrder` 的返回值，而 `topoIndexByRef`（同文件 794-797）只给 `topo` 里的节点编号，其余一律 `kNotInPlan`。
- `BranchArmEntries`（`src/execute/graph_runtime_support.cpp:700-718`）只遍历 `topoOrder`，臂的 `[entry, tail]` 是**计划序里的下标区间**。
- 那 19 条不在 `live` 里，就不在 `topo` 里，**因此不在任何臂的 `[entry, tail]` 内，也没有任何 BRCH 的跳转目标覆盖它们**：19 条全部如此，0 条在臂内。

**换句话说**：它们不是"在某条臂里而没跑到"，是**没有任何臂认领它们**——它们从计划里就不存在。"有同步前驱"这件事在 `live` 这一关就被用完了，`BranchArmEntries:707-711` 那句"臂的节点排在 BRCH 之前就跳过"根本没有机会被问到。

**补种子也不跑的道理就在这里**：`live` 补上它们，它们进了 `topo`，但 19 条语句所在的区域在那张计划序里没有 BRCH 覆盖它们（臂的入口按区域取，见同一处的注释），于是"进了计划、仍然没人负责让它跑"。**它们缺的不是"被算作要跑"，是"有人负责让它跑"。**

## 再补一次：把它们全塞进计划之后，臂的区间是多少

路 A（把每个属于控制区域的节点都塞进 `live`，`CAMEL_ROUTE_A=1`）打开后，同一张图打印计划序与臂区间（`exec_plan.cpp:827` 之后，`armEntries` 旁）。最后一张图（22 个节点）的十条：

```
id=133 kind=6  idx=4   armEntry=132 armTail=132      ← BRCH
id=131 kind=11 idx=5
id=357 kind=6  idx=15  armEntry=356 armTail=356      ← BRCH
id=347 kind=11 idx=16
id=349 kind=11 idx=17
id=353 kind=6  idx=18  armEntry=352 armTail=352      ← BRCH
id=351 kind=11 idx=19
id=352 kind=7  idx=20
id=354 kind=11 idx=21
id=355 kind=11 idx=22
```

三条事实：

1. **每个臂的 `[entry, tail]` 是"一个节点"**（entry == tail），不是一串：`133 → 132`、`357 → 356`、`353 → 352`。臂里其余的语句**不在区间里**。
2. **臂的入口节点自己不在任何控制区域**：`132` 与 `356` 没有出现在这张表里（表只列"控制区域内的节点"和 BRCH），说明 `regions.controlOf(entry) == 0`。
3. **有一个臂的区间落在 BRCH 之前**：`353` 在 idx 18，它的 `352` 在 idx 20（之后，还好）；而 `133` 在 idx 4、入口 `132` 不在表里，`BranchArmEntries:707-711` 的规矩是"臂节点排在 BRCH 之前就跳过"——**这一条规矩确实会被问到，而不是像上一节猜的那样问不到**。

**把三节连起来**：`live` 漏了 19 条 → 塞进计划后它们仍不在任何 `[entry, tail]` 里（区间只有一个节点，而且入口自己不在区域里）→ **臂的范围根本不覆盖臂里的语句**。这就是"放进计划还是不跑"的答案：**它在计划里的第几位有了，但没有任何区间覆盖它。**