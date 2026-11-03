# 一个值没人用的 `loop` 语句，还算不算一条要执行的语句？

日期：2026-11-03　状态：**已裁定 (a)，落点待量**

## 问题

`loop acc = 0 for v in count(3) do sync { println(v); acc + 1 }` 写在 `main` 里，循环的
值（`acc`）没有任何用户。语句本身是写着的，体里有 `println`。它该不该跑？

设计原则 3.4 说"同步块里的语句都执行，按书写顺序"，`loop` 语句是语句，所以答案是**该跑**。
当前实现**不跑**：体的值门在图上没有任何使用者，整块连同体的效应一起被判死。

## 现象（三个用例，同形）

`test/cases/compile/stream/generators_loop.cml:27,29,30`，三处 `for v in count(n) do sync {…}`。
诊断对三处各报一条 `EffectNotExecuted`（"the sync block it is in is not used"），输出期望的
`0\n1\n2\n-\n0\n1\nend` 一个字符也没有；`std::gir` 里 `main` 连一个 `:io/println` 都不剩。

三处走法完全一样（`CAMEL_P02_WALK=1` 的打印原文，`generators_loop.cml`）：

```
P02WALK graph=main start=26 startKind=11(Oper) stopGate=31 from=30
        syncUsers=0 ctrlUsers=0 syncInputs=1 ctrlInputs=1 normInputs=1 visited=3
        (另两份：26/31、69/74、111/116)
```

从 `println`（26）出发，沿控制与同步使用者向上：

1. 到 **GATE 30**——链位。`chainPlaceGates_` 认得它（`chainPlaceForNode` 登记过），所以
   **穿过去**，不在这里停。这一步说明"集合作用域"没有问题：诊断跑在同一个 `Builder` 实例上。
2. 到 **GATE 31**——停。它 **`syncUsers=0`、`ctrlUsers=0`**（一个顺链使用者都没有），却有
   1 个数据输入、1 个同步前驱、1 个控制前驱。

## 为什么停点是"块值形状"而不是漏登记的链位

按 `docs/design/effects/edges.cn.md` 3.1 的两句：**链位 `GATE` 必有下文，块值 `GATE` 是链的
最后一站**。31 号没有下文，所以它是块值形状——它把块的值交给链尾，自己就是终点，不是漏登记的
链位。全语料读音也一致：2858 个链位全部有后继，3546 个块值全部没有。

所以 `inUnusedSyncBlock` 停在这里**是对的**：图上确实没有任何东西用这个块的值。诊断没有错，
错的是上游——循环语句的值被丢掉了，体跟着丢。

## 两条可选修法

- **(a) 建图时让循环节点成为体区值门的使用者。** 循环每一轮都要消费体区的值，体区的值门本来
  就是给那台循环用的。这是把已有的事实写进图里。
- **(b) 在活跃性分析里加一条"任何 `loop` 语句的体都算要执行"。** 那是给一个特例开后门，以后
  每个类似形状都会想加一条。

## 裁定：(a)

理由是结构上的，不是偏好：(a) 让图说出它本来就有的事实，(b) 让检查认识一个特例。作者一贯反对
"现有机制够用时另发明一个"。

## 落地状态

**形态确认（推翻本页初稿的一句）**：`for v in <生成器> do …` 与 `loop … while …` 走的是同一个
`src/compile/gir/builder_control.cpp#Builder::visitLoopNode`；`visitStaticForNode` 管的是另一条。
本页初稿写"没有 `LOOP` 节点"是**错的**——那是 grep 输出被 `head` 截断造成的误读。完整的
`std::gir` 里三个循环节点都在：

```
N94 [label="LOOP while", type="LOOP", origin=7107, span="26:4-26:66", value_type="(int.., bool, int)"]
NAD [... span="28:4-28:66" ...]   NC5 [... span="29:4-29:80" ...]
```

**量到的形状**（只看 `main` 自己的节点与边）：

| | 在 | 不在 |
|---|---|---|
| 循环节点 | `N94` / `NAD` / `NC5` | |
| 体区算出来的值 | `N8E :op/add_l`，经 `FILL`/`BRCH` 接进循环 | |
| 体内 `println(v)` | | 不在 |
| 链位 `GATE` | | 不在 |

即：体区把**纯值**交给了循环，链那一支（`println` → 链位 → 块值 `GATE`）没有使用者，被剪掉。

## 落点在 `visitBodyNode` / `visitExecNode` / `setArmResult` 这一条线上

- `Builder::visitBodyNode`（`builder_control.cpp`）取 `visitExecNode(gct, tailPositionType())` 的
  返回值，它就是体区交回去的东西；
- `visitExecNode` 收尾（`builder.cpp` 1820–1845）有两条路：值节点为本块独有 → `res` 原样（自己当
  链尾，`linkSyncStatement(lastSyncedNode_, res)`）；否则造一个 `GATE`（`res = gate`）并
  `writeSyncStatement(lastSyncedNode_, gate)`。本用例走的是**造 GATE 那条**；
- `Builder::setArmResult` 把 `arm.output = result`、`arm.value` = 该 `GATE` 的数据输入（注释写明
  "the GATE is what the arm gives"）；
- `buildArm` 建区域时 `synced_ = false; lastSyncedNode_ = {}`（1552–1553 行），区域自己的链从空开始；
- 之后 `visitLoopNode` 里体区的结构被拆成分量时（`if (bodyPart->output != bodyPart->value)` 一带，
  3536–3541 行）只写了一条 `writeControlLink(bodyPart->output, result)`（**顺序**），再把
  `bodyPart->output = result`。区域的结果改成 `result` 之后，那个 `GATE` 就没有任何使用者了。

**还差的条件**：体区交回去的必须是**链的最后一站**，而不是它转发的纯值——要么 `setRegionResult`
仍然指向那个 `GATE`，要么循环节点直接成为它的使用者。现在的代码只把顺序交给了 `result`，
顺序边不进活跃性，所以链那一支被剪。

这两条与"链首不造 `GATE`"不冲突：链首那条管的是链**之前**没有语句的位置，这里要交回的是链的
**最后**一站，那里有语句、有前驱。

## 待办

1. 按上面的"还差的条件"改一处，复跑 `feat/compile/stream` 三个用例；判据是 `main` 的 `std::gir`
   里重新出现体内的 `:io/println`，并且 A 的 34 条掉到 0（逐名报）。
2. **分支臂的同一条线先不动**：`buildArm` / `setArmResult` 也服务分支臂，臂的那一半要用例说话，
   先量再改。

## 试过的一次（已回退）

按"出口仍是门、`result` 从门背后出来"改过 `builder_control.cpp` 3536–3541：若 `result` 就是门
已经转发的那个值，则 `output` 仍指门、只把 `value` 指向 `result`；否则照旧。

结果：**那一段不在本用例的路径上**——临时打印一次也没打出来，因为这段代码在"状态可拆成分量"
的分支里，而这三个循环的状态不是可拆分的聚合。`feat/compile/stream` 146 条里
**114 通过 / 32 失败**，与改之前逐名相同：没有修好一条，也没有多出一条新红。改动已回退，分支上
没有半成品。

**新的落点**：本用例走 `visitLoopNode` 主路径——区域结果与循环的 `with` 输入都是
`bodyPart->output`（3640、3648 两行），所以 `bodyPart->output` 本身是活的；死的那个门**不是**它。
也就是说循环体那条链的最后一站从来没有成为 `bodyPart->output`：`setArmResult` 拿到的就是
`visitExecNode` 交回的东西，而这个体的值节点（`acc + 1`）是本块独有，于是
`builder.cpp` 1820–1845 走"值节点自己当链尾"那一条，门没有交回来。

下一次改落在 `builder.cpp` 1820–1845 的判断上（或 `setArmResult` 收下之后）。量法：在 3648 打
`nodeKind(bodyPart->output)` 与 `nodeKind(bodyPart->value)`，两者都是 `Oper`（不是 `Gate`）就
说明确实没有门，那时再决定"体区交回链的最后一站"该写在哪一处。

## 第三次读数：`synced_ == false`，`lastSyncedNode_ == nullptr`

在 `builder.cpp` 1811 那句 `if (!operands)` 里打一行（`CAMEL_P02_SYNC=1`，`generators_loop.cml`
走 `std::nvm`），这个用例的每一对打印都是同一个形状：

```
P02SYNC synced=0 operands=0 returned=<0|1> sameGraph=1 resKind=0 needs=0
```

三件事一起成立：

- `synced = 0`：走到这句时**所在块不在链上**。`visitBodyNode` 建体区时先把 `synced_` 置 false
  （`builder_control.cpp` 4246 行 `std::exchange(synced_, false)`），体内那个 `sync { … }` 块自己
  的语句是有序的（所以体内确实有链），但块收尾时 `synced_` 已经回到外层的 false。
- `needs = 0`：`valueNeedsSyncEdge(res, lastSyncedNode_)` 为假。它的定义是
  `chainEnd != nullptr && !valueReadsThrough(value, chainEnd)`（`builder_helpers.h` 666 行），
  而体区里 `lastSyncedNode_` 是空的 ⇒ **链尾是空的，所以永远不必造门**。
- `resKind = 0`（`Oper` 一类的值节点），不是 `Gate`。

所以"门没交回来"的确切原因是：**体区这一层没有链尾**，于是收尾处既不造门，也不把值挂到链上；
值以裸值交回，`setArmResult` 收下裸值，循环拿到裸值，体的语句（发打印的那条）没有任何使用者。

**判据（下一次动手用）**：`synced_` 这一个条件对"语句的值没人用"是对的（P0-2 的规矩：纯值不上
链），但它对**要交给机器每轮消费的体区**不对——体区的值有使用者（循环的 `with` 输入），有使用者
就该是链上的一站。所以这一处的判断要按"这个值有没有跨区域的使用者"分岔：没有跨区域使用者才让
值自己当链尾，有就造门并交回门。改成这一条时**分支臂会同时被影响**（`buildArm` 两边共用），
要单独跑臂的用例再动。

## 试过的第二次（已回退）：`visitBodyNode` 里 `synced_ = true`

只改一行（`builder_control.cpp` 4246：`std::exchange(synced_, false)` → `true`），让体区的语句
自己成一条链、链尾不空。

读数是**诊断的措辞变了、结果没变**：

- `feat/compile/stream` 146 条仍是 **114 通过 / 32 失败**，逐名与改前一致，体内
  `:io/println` 仍不回来；
- 但告警从 `the sync block it is in is not used` 变成了
  **`nothing depends on it and it is not in a sync block`** —— 说明打印经过的那一层块终于是
  "不在链上"的另一种判定了，可它仍然不在链上。

所以 `synced_ = true` 没有把链带进体区：体区的语句仍然被判为"不在 sync 块里"。这一条要往下追
的是 `visitExecNode` 里对 `sync { … }` 块本身 `operands` / `synced_` 的判定（1811 那句
`if (!operands)` 的两侧），不是 `visitBodyNode` 的这一个 flag。改动已回退，分支上没有半成品。

**下一步的三个数**：在这一个改法下重打 `P02SYNC`，若 `synced` 仍是 0，说明 `visitBodyNode` 的
`synced_` 在进入 `sync { … }` 块之前又被关掉了；那就要在 `visitExecNode` 入口打
`synced_`/`operands`，找到"谁把它关掉"。

## 交接：四轮读数与已排除的四种可能（2026-11-03）

A（`feat/compile/stream` 里 `stream.generator_*` 那 32 条，加另一个计划里的
`stream_fib.default.{nvm,prl}` 共 34 条）**原样留给下一手**：`feat/compile/stream` 146 条，
**114 通过 / 32 失败**，逐名稳定。四轮下来排除了四种可能，每一种都有读数，不用重走：

1. **不是诊断集合失效**。`chainPlaceGates_` 认得链位：走法 `visited=3`（`println` 26 → 链位
   30 → 31），链位被穿过去了，诊断与建图器是同一个 `Builder` 实例。
2. **不是"值节点独有"**。`P02SYNC` 三数：`synced=0 operands=0 needs=0 resKind=0`。
   `needs=0` 的原因是 `lastSyncedNode_ == nullptr`（`valueNeedsSyncEdge` 要求链尾非空），
   不是"这个值没人要"。
3. **不是 `builder_control.cpp` 3536–3541 那条拆分路径**。打印一次也没触发：这段在"状态可拆成
   分量"的分支里，这三个循环的状态不是可拆分聚合。
4. **不是标记退役的副作用**。`kGCNodeFlagSyncStatement` **还在**，还有 28 个读点
   （`src/runtime/graph_edges.cpp:208`、`src/runtime/draft_regions.cpp:103`、
   `src/passes/opt/generic/generic.cpp:342` 等），`git log -S` 的历史里它是"退成边"而不是被删；
   而**诊断这条路根本不读它**：`inUnusedSyncBlock`
   （`src/copy/…` 实际是 `src/compile/gir/builder_diagnostics.cpp:478`）只沿 ctrl/sync 使用者走，
   碰到不在 `chainPlaceGates_` 里的 `GATE` 就判"块没被用"。所以这条线干净。

**第五轮的读数（推出来的，源头在这条代码路径里）**：把 `visitBodyNode` 的
`std::exchange(synced_, false)` 改成 `true` 之后，告警措辞从"the sync block it is in is not
used"变成"nothing depends on it and it is not in a sync block"，而 `inUnusedSyncBlock` 返回
**false**、效应仍不执行。walk 返回 false 只有一个意思：**打印上面没有任何 ctrl/sync 使用者**
（连链位 `GATE` 都没有）。既然 `synced_` 已经在体区入口被置真而语句仍然不上链，就只有一种解释：
**在 `visitBodyNode` 与 `sync { … }` 块的语句之间，`synced_` 又被关掉了**。

**下一手的第一件事**：在 `visitExecNode` 的入口打 `synced_`/`operands`（第一个数），再在
`sync { … }` 块语句生成的那一句上打一次（第二个数），两处若从 1 变 0，就找到了那处
`std::exchange(synced_, false)` —— 报出 `file#Symbol` 之后才动手改。作者已经裁定过方向：
体区交回链的最后一站，不是裸值；分支臂与循环体共用 `buildArm`，动之前要用例说话。

## 合并前必须解决：`feat/compile/transforms` 比 develop 多 8 条红

本分支（`ca08022e4`）`feat/compile/transforms` 78 条 = **66 通过 / 11 失败 / 1 known**，
11 条逐名：

```
jvp_effect.runs_as_written_and_zero_tangent.nvm
transpose_loop_escape.two_values_of_two_kinds.nvm
transpose_loop_escape.second_state_of_the_same_loop.nvm
transpose_loop_escape.int_is_a_zero_tangent_space.nvm
grad_array_no_int.fvm
grad_of_an_array_parameter.nvm
grad_array_one_element.nvm
grad_array_two_elements_first.nvm
grad_array_sum.nvm
grad_array_product.nvm
grad_of_a_division.nvm
```

develop（`5491d4bf6`）同一份计划是 74 过 / 3 红 / 1 known，三条都是 `transpose_loop_escape.*`。
差集 **8 条** = `jvp_effect` 一条 + `grad_*` 七条。

**我没有在 develop 上跑出来**：把主机的检出切到 `5491d4bf6` 并清空 `build/` 后，
`cmake` 直接报 `CMake Error at CMakeLists.txt:1 (cmake_minimum_required)`（用
`~/camel-eval/venv-build/bin/cmake` 4.4.3 也一样），所以这一格是**没量到**，不是量过为绿。

**从仓库拓扑得到的结论**：`a49cac54d` **不是** `origin/develop` 的祖先，develop 比本分支多
**89 个提交**，其中正好覆盖这 8 条所在的区域：

- `04720eb7c` fix(rules): a rule's `when` decides the instance -- a condition that fails takes the tuple away
- `4f524c534` / `a6ab688a2` 一个规则的条件只问一次
- `08622b019` fix(compile): a `void` body's tail drops the name it ends with

而这 8 条红报的是 `REINTERP ... skip=call node=9 arg=5`（`index_into` 的转置建不出来），
落在 rules/instance 那一块 —— 就是 develop 已经修过的地方。所以**这 8 条是本分支缺 develop 的
提交，不是本分支的改动造成的**；合并 develop 之后必须重测并归零，归零前不合。

`feat/linear/jit_framed`：今天 57 条全过；两边条目数按 `name =` 数相同（24），本分支在这份计划
文件上的改动只有 E 的 8 行期望（1 插入 / 7 删除），没有删用例。此前报的 62 那次运行与今天的
条目不同，但**不是本分支删了 5 条**。
## `jvp_effect`：本分支红、develop 绿（第二条 GATE 造法）

第二条树的基线（`D:\Projects\Camel-wt\dev`，`5491d4bf6`，独立检出）：

```
[PASS] jvp_effect.runs_as_written_and_zero_tangent.nvm      108.35 ms
[SUMMARY] total: 78, pass: 74, fail: 3, known failures: 1
```

本分支是红，原文：`Runtime graph 'jvp_of_g' has GATE node 8 without a wait: no sync
predecessor and no control input.` 图由 autodiff 建（`REINTERP jvp built graph=236 nodes=9
exit=8 function=g`），节点 8 就是出口门，手造在
`modules/autodiff/pullback.cpp#setResult`（378–391 行）：`addGateNode` + `setNormInputs(exit,
{result})` + `setCtrlInputs(exit, {oldExit})`。

它写的等待是**控制输入**——正是 `edges.cn.md` 5.3 说 `GATE` 不再带的那种（"一个数据端口、一个
同步前驱"）。`src/runtime/graph.cpp:619–630` 两者都收，所以建图器还在链首造位门时它旁边那条控制边
让检查通过了；位门没了之后它就只剩那条控制边，而它已经不是这个门的等待。

**修法是让它按建图器的等待方式等（同步前驱），不是在这里手补一条等待**——补会把"绕过建图器"
固化。`setResult` 不是唯一手造门的地方：`pullback.cpp` 还有 3022（臂的结果）、4756（体的结果）、
4844，所以这是一小族而不是一行；**按规矩停在这里**，等作者把它排进 P0-2 的顺序。
### 试过的一步（已回退）：四处改成继承等待，`jvp_effect` 不动

按正路把 `pullback.cpp` 四处手造门的等待从控制输入改成同步前驱：加了一个
`waitWhereItWaited(transaction, gate, old)`，把 `old` 的同步前驱与 ctrl 输入一并接成门的同步前驱，
四处（`setResult` 的出口门、3022 的臂结果、4756 的体结果、4844 的梯度门）都换成它。

读数：`feat/compile/transforms` 仍是 **73 过 / 4 红 / 1 known**，`jvp_effect.*` 仍报同一句
`Runtime graph 'jvp_of_g' has GATE node 8 without a wait`。

**为什么**：`old` 身上**没有可继承的等待**。这个 `jvp_of_g` 是 autodiff 从另一张图重建的，9 个节点、
出口就是那个门；旧出口本身既没有同步前驱也没有控制输入——它能有的那一条（链首位门旁边那条）
在这条分支里已经不造了（P0-2 的那条规矩）。所以"按建图器的方式等"在 autodiff 这条路上**没有入口**：
它真正需要的是"这个值在链上的最后一站"，那是建图器的服务（`Builder::chainPlaceForNode` 一带），
在一个 pass 里够不到。按作者的停手条件（"若发现没有可用的入口，说明要加的是建图器的入口，不是改
autodiff"）停在这里，改动已回退。

门的四处位置、各自的等待是谁，仍然成立：出口门该等"图返回之前最后一次写入的那条链"，臂结果门该等
"臂里的语句"，体结果门该等"体里的语句"，梯度门该等"函数自己返回前的那条链"（注释原文
"the gradient waits for it, so that the function's effects all happen, in their order"）。
四处要的都是**同一种东西**：那条链的最后一站，而这件事今天的 pass 侧拿不到。
## 32 条 stream 红是本分支造成的：bisect 指向 `a49cac54d`

两棵树的同一份计划（`test/plans/feat/compile/stream.plan.toml`，`-Tier fast -Jobs 4`，完整重建）：

| 树 | `[SUMMARY]` |
| --- | --- |
| `dev-baseline` = `origin/develop` = `5491d4bf6` | **146 条全 PASS，0 fail**，`RUN_EXIT=0` |
| `lane-p02` = `989780a2d` | **146 条，114 过 / 32 红**，`RUN_EXIT=1` |

32 条逐名（各 4–5 个后端变体）：`stream.interval_loop_cml`；`stream.generator_sync_order.{nvm,fvm,jit,prl,_default}`；
`stream.generator_early_stop.{…}`；`stream.generator_error_element.{…}`；`stream.generator_local_across_yield`；
`stream.generator_destructured_local`；`stream.generator_loop.{…}`；`stream.generator_cell_across_pulls.{…}`；
`stream.generator_body_fault_ends_stream.{…}`；`stream.generators_loop_computed_initialiser`。
停在同一句上：`The call of 'println' has side effects but is never executed: the sync block it is in is not used`。

所以他们说的"既有缺陷"是我把分支读数当成了缺陷现状。**这 32 条是我这条分支造成的。**

### bisect（每次都在同一棵 `lane-p02` 上完整重建 + 跑同一份计划）

1. `9e48ccfc0`（test/jit 关系读数）贴到 develop 上 → **146/146**，无罪。
2. `bd7a12fbe`（链首不造位门）在我的树上回退 → **仍是 114/32**，无罪。
3. 把与 develop 不同的 23 个代码文件**全部退回 develop 的版本**（只留本分支的计划与文档）→ **146/146**，说明 32 条出在这 23 个文件里。
4. 只把 `src/compile/gir/builder*.{cpp,h}`（我的版本）放回去，其余仍是 develop 的 → **114/32**。

⇒ 第一次让 pass 从 146 掉下来的是 **`a49cac54d`**（`gir: the chain's place is a position, and the diagnostic
walks through it`），它把"链尾"从语句节点改成了**位门**，五处：

- `builder_control.cpp#Builder::buildBranch`（2822 附近，`lastSyncedNode_ = chainPlaceForNode(joinNode)`）
- `builder_control.cpp#Builder::visitLoopNode`（3688 附近，同上，`loopNode`）
- `builder_control.cpp#Builder::buildStreamLoop`（4091 附近，同上，`loopNode`）
- `builder_expr.cpp#Builder::readVariableThrough`（1742 附近，读也取一个位）
- `builder_operators.cpp`（4387 附近，调用点同样先取位再连）

这正是 A 那 32 条的症状来源：链尾成了一个位门，`inUnusedSyncBlock` 之外的读者（生成器体内语句的消费者）
不再把它当"这个块写过东西"，于是 `println` 被判成"所在 sync 块没人用"，效果被丢掉。
### 假设一（"位门没登记"）不成立：五处都经 `chainPlaceForNode`，那里就是唯一的登记点

| 造位门的位置 | 是否登记 |
| --- | --- |
| `builder_control.cpp#Builder::buildBranch`（`lastSyncedNode_ = chainPlaceForNode(joinNode)`） | 是 |
| `builder_control.cpp#Builder::visitLoopNode`（同上，`loopNode`） | 是 |
| `builder_control.cpp#Builder::buildStreamLoop`（同上，`loopNode`） | 是 |
| `builder_expr.cpp#Builder::readVariableThrough`（`const node_handle_t place = chainPlaceForNode(value)`） | 是 |
| `builder_operators.cpp`（`const node_handle_t place = chainPlaceForNode(targetNode)`） | 是 |

`chainPlaceForNode`（`builder.cpp:43`）是唯一把门放进 `chainPlaceGates_` 的地方
（`chainPlaceGates_.insert(place)`，当 `place != node`），所以经它造的门一律登记。

### 下一层：步行在门口停下时问的是"它是不是位门"，从没问"这个门有没有人读"

`inUnusedSyncBlock`（`src/compile/gir/builder_diagnostics.cpp:478–507`）：

- 只沿 `draft.ctrlUsersOf(id)` 与 `draft.syncUsersOf(id)` 走（484 行），**从不沿数据输入/数据使用者走**；
- 遇到 `GATE`：在 `chainPlaceGates_` 里就继续走（496–498），不在就**立刻 `return true`**（500）；
- `return true` 的那一支就是诊断的原句 "the sync block it is in is not used"（540–542）。

所以"块的值门"这一站被当成"这个效果就是块的值"，而**块的值有没有人读，它一次都没问**。生成器体里
`println` 的链尾正是这个值门（`a49cac54d` 之后链尾成了位门，值门在它之后），于是效果被判"没人用"。

第二条同层读数（从代码就能看出来）：`chainLinkSaysSomething`（`builder_helpers.h:626`）要求
`!hasDirectDataDependency(from, to)`，而新链尾的门**数据输入正是它前面那个语句**——那条边因此可能
根本不写。这一条要先量出来（链尾门有没有入边），才能决定是修步行还是修连边。

**这一轮没有改代码**：假设一被否掉了，改哪里要等这两条读数里哪一条成立。
### 探针读数（`generators_loop.cml`，500 行 `return true` 之前，`P02_GATE_PROBE=1`）

```
[P02GATE] from=30  gate=31  kind=13 place=0 dataIn=1 [29 ] dataUsers=0 [] ctrlUsers=0 [] directData=0
[P02GATE] from=73  gate=74  kind=13 place=0 dataIn=1 [72 ] dataUsers=0 [] ctrlUsers=0 [] directData=0
[P02GATE] from=115 gate=116 kind=13 place=0 dataIn=1 [114] dataUsers=0 [] ctrlUsers=0 [] directData=0
```

五个数（三次同样的形状）：撞到的是 `GATE`（kind 13）、**不在 `chainPlaceGates_` 里**（place=0）、
**一个数据输入**（29/72/114，和"撞上它的那个语句"30/73/115 不是同一个节点）、**数据使用者 0**、
**控制使用者 0**、**两个相邻节点之间没有数据边**（directData=0）。

### 判断：不是"登记"，也不是"步行"，是**块的值门成了孤儿**

这个门有一个数据输入、一条从前一个语句来的 ctrl/sync 入边、**却没有任何使用者**——它就是"块交给外面
的那个值"，而本该读它的那一端没有连上来（读的是别处）。所以 `inUnusedSyncBlock` 判"这个 sync 块没人用"
**在它自己的口径里是对的**：那个块的值确实没人读。

要修的是**谁该读这个门**：`a49cac54d` 把链尾改成位门之后，取块值的那一端（`visitLoopNode` /
`buildStreamLoop` 里的 `linkNodes(LinkType::With, bodyPart->output, loopNode)` 与区域结果那条线）
接的是位，而不是这个值门，于是值门悬空、块被判没用。下一轮先用同一支探针打出这个门的 `origin`
与角色，确认它是不是块值门，再动那一处连边。

**这一轮没有改代码**（探针已存 `D:\Temp\p02-probe-gate.patch`，源文件已还原）。
### 三个新读数：这个门**谁都不读**

```
[P02GATE] from=30  gate=31  kind=13 place=0 origin=0 dataIn=[29 ] dataUsers=[] withUsers=[] ctrlUsers=[] syncUsers=[] directData=0
[P02GATE] from=73  gate=74  kind=13 place=0 origin=0 dataIn=[72 ] dataUsers=[] withUsers=[] ctrlUsers=[] syncUsers=[] directData=0
[P02GATE] from=115 gate=116 kind=13 place=0 origin=0 dataIn=[114] dataUsers=[] withUsers=[] ctrlUsers=[] syncUsers=[] directData=0
```

- **origin = 0**（`kInvalidOriginId`：造它的时候没有源位置，是建图器自己的中间节点）；
- 四种使用者**全空**：数据 0、上下文（`With`）0、控制 0、同步 0 —— 这不是"块值门没人读"，是**一个谁都不读、
  谁也不等的死端**；它唯一的一条边是**进来的**（`from`=30/73/115 的 ctrl 或 sync 使用者就是它）；
- `dataIn` = 29/72/114（门读的那个值），与撞上它的语句不是同一个节点。

### 判断：走不了 1，也走不了 2 —— `With` 那条边根本没写

- 第 1 条（没人读就不造门）不成立：这个门不是"多造的一个位"，它是链尾；删了它，值 29/72/114 仍然没人读，
  语句照样被判"没人用"，32 条不会变绿。
- 第 2 条（把消费者连到门上）也不成立：**消费者不存在**——`withUsers` 为空，说明
  `visitLoopNode` 里那句 `linkNodes(LinkType::With, bodyPart->output, loopNode)` 没有落到这个门身上。
  也就是说 `bodyPart->output` 很可能**不是** 31/74/116，而是别的东西，于是循环没有读它。

**停手报作者**（按"两者都不是就停手"）。下一支探针要打的是：`bodyPart->output` 的 id 与 kind、循环节点的
`with` 输入列表、以及 29/72/114 各自的使用者——这三格决定 `With` 是在哪一步丢的。
### 标号读数：`site=8`，是**块的值门**（`builder.cpp:1854`），不是位门

给每个造门点一个标号（1 = `chainPlaceForNode`，2 = `visitLoopNode`，3 = `buildStreamLoop`，
4 = `buildBranch` 的 join，6 = `readVariableThrough`，7 = 调用点，8 = 块的值门），再在 bail 处打出
门的标号、四种使用者、以及门的数据输入各自的使用者：

```
[P02GATE] site=8 from=30  gate=31  kind=13 place=0 dataIn=[29 ] dataUsers=[] withUsers=[] ctrlUsers=[] syncUsers=[] feeds=[29:d=30 31  w=33  c= s=]
[P02GATE] site=8 from=73  gate=74  kind=13 place=0 dataIn=[72 ] dataUsers=[] withUsers=[] ctrlUsers=[] syncUsers=[] feeds=[72:d=73 74  w=76  c= s=]
[P02GATE] site=8 from=115 gate=116 kind=13 place=0 dataIn=[114] dataUsers=[] withUsers=[] ctrlUsers=[] syncUsers=[] feeds=[114:d=115 116 w=118 c= s=]
```

- **标号 = 8**：门造在 `src/compile/gir/builder.cpp:1854`（`visitExecNode` 的 `if (!operands)` 分支，
  "块的值放上链"那一处：`addGateNode` + `Norm(res, gate)` + `writeSyncStatement(tail, gate)` + `res = gate`）。
  它是**块的值门**，按设计**本来就不该进 `chainPlaceGates_`**（位门才是位置，值门是链的最后一站），
  所以这不是"绕过登记"。
- **值 29 有三个读者**：数据 30、31，`With` 33（= 循环节点）；**门 31 一个读者都没有**。
  也就是说：块的值被包进了门，而取块值的那一端（循环的 `With`）读的是**裸值 29**，不是门。

### 结论：门造晚了——消费者在门出现之前就接好了

`res = gate` 之后，`bodyPart->output` / 区域结果那一线拿到的仍是 29，所以循环读 29、门 31 悬空。
下一支探针要打的是**时序**：`visitBodyNode` 返回的是门还是 29、`setArmResult` 收的是哪一个、
`linkNodes(LinkType::With, bodyPart->output, loopNode)` 那一刻 `bodyPart->output` 是谁。