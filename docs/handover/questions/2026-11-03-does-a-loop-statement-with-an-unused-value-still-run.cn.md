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