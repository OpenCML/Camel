# §1.3 落地记录：作者的判据缺一个事实，而这条判据的读数缺一个人口

`lane-assignkind`，2026-11-18，分支 `lane-assignkind`（基于 `d1eb10b7f`，按作者第 4 条暂不合 `develop`）。三笔：`30dc9924c` + 两个编译修正。

## 一、作者的原话

> **「『这是一次赋值/路径赋值』按 GCT 节点的种类判，不按被调名字。** `builder_control.cpp:3443`、`:3445`（`callee.rfind("__assn", 0)`）、`builder_expr.cpp:3011`、`builder_operators.cpp:2043`、`:2363`、`program.cpp:651` 都在比 `"__assn__"` 字符串。另外 `visitLoopNode` 里有第二遍词法扫描，和 `scanBodyForSsa` 回答同一个问题（这个名字在循环里写没写），留一个。」

## 二、一个必须记下的事实：GCT 里没有「赋值」这个节点种类

**`x = e` 在 GCT 里就是 `LINK(DREF __assn__, target, value)`。** `NodeKind` 只有 `LINK`/`ACCS`/`SACC`/`DREF` 这些；`GCNodeKind` 是运行期图的种类（`DATA`/`PORT`/`OPER`…），不在这里。

⇒ **所以「按节点种类判」按字面做不到。** 节点能答的只有两件事：

1. **形状**：`LINK`、三个实参、目标种类（`DREF` = 名字 / `ACCS`|`SACC`|`__idx__` = 路径）；
2. **算子身份**：编译器自造的被调带算子 id（`Tree::operRefOf`）。

组名（`__assn__` / `__assn_owned__` 等）是最后那点**名字**，被收进算子表一处（`core::isAssignmentGroup`，读 `kCompound` 表 + 两个组名）。

**这一步落地的形状**：新增 `assignmentFormOf(node)`（`builder_helpers.h`），从节点答 `None / Plain / Compound / Rebuilt / Owned`；9 处（作者点名的 6 处 + 同族 3 处）改为读它，**每处原先接受的「形」逐位保留**（「丢弃值」只认 `Plain`，`ssaAssignmentOf` 认 `Plain+Rebuilt`，`inPlaceRebindingOf` 只认 `Plain`）。

## 三、`feat/compile` 的两个数，不是同一个人口

| 人口 | total | fail | known |
|---|---|---|---|
| 上一只手报的（不含 gpu-plans） | 2627 | 17 | 17 |
| **未改动的 `develop`（含 gpu-plans）** | **2696** | **39** | 17 |

**⇒ total 差 69，fail 差 22。** 「非 known 红 0」这句话在两个人口上不是同一句。

**⇒ 而正确的比较方式今晚已经用过三次**：**逐名比较 `[FAIL]` 集合**，不是比总数。`lane-assignkind` 在未改动的 `develop` 与自己的树上各跑一次，54 行 `[FAIL]` **逐条相同**，其中 15 行是 gpu-plans 的 recorded，**新增 0、消失 0**。

## 四、§1.3 还没收干净的两处

1. **`builder_diagnostics.cpp:1013/1042/1081/1100` 那 4 处 `__assn` 比较还在** —— 在 `lane-rrread` 的文件里，归它。
2. **「两遍扫描留一个」没做，理由成立**：`scanBodyForSsa` 答的是「**每个 `var` 声明能不能成值**」（按声明，带 region 深度 / 路径写 / `var` 实参），`visitLoopNode` 的扫描答的是「**哪些名字被哪一类区域写**」（按名字：本块直写 / 臂或内层循环体 / 出不去）。**问题不同，只是同一次遍历走了两遍。** 合并要把 200 行走法重写成「一次走、两个答案」，差一处就改「循环携带哪些名字 / 哪些 var 成值」的语义，与本轮「零行为变化」冲突。

**⇒ 所以作者那句「回答同一个问题」不成立**，而正确的合并方向也因此不同：不是「删掉一份」，是「**一次遍历产出两个答案**」。这一半留下，单独一笔做。

## 五、五个读数

`feat/opt` 1538/1538 ｜ `feat/para` 100/100 ｜ `feat/tools` 70/70 ｜ `feat/opt/e9` 212/212 ｜ `feat/compile` 逐名与 `develop` 相同（54 行、0 new）

**无可观测的正面证据，理由是纯表示改造**：接受集逐位相同（表里 11 个组 = 前缀 `__assn` 的全部），而 S1/S3 的形状仍被既有用例钉住（`recursive_struct_array.cml` 的 `acc += …` 仍由循环头携带、`element_assigned_append` 的 `--copies` 仍报复制、`var_closure` 仍以 `ImmutablePassedToVarParam` 拒在赋值处）。