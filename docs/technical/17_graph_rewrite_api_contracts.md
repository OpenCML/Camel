# Graph 重写 API 关键约束

本文聚焦 **Graph 重写 API**（不是 GIR 概念本身），用于约束 pass/工具代码在改写图时的行为边界与正确性契约。

覆盖对象（名字以代码为准）：

- `RuntimeGraphDraftSession`（`include/camel/runtime/draft_session.h`）：一次重写事务；
- `GraphDraft`（`include/camel/runtime/draft.h`）：一张运行时图 `GCGraph` 的可写副本；
- `GraphDraftGroup`（同在 `draft_session.h`）：一组新建的、可以互相引用的图；
- `inlineCallableInDraft` 等改写原语（`include/camel/runtime/draft_inline.h`）；
- 提交时的校验 `validateRuntimeGraphPayload` / `validateRuntimeBranchArms` / `validateRuntimeGraphCalls`
  （`src/runtime/graph_build.h`）。

前端建图用的是另一套对象（`src/compile/gir/` 下的 `Builder` 与 `DraftGraphBuilder`），它最终同样编码成
`GCGraph`，并经过同一组校验（第 4 节）。

---

## 1. 分层职责与边界

### 1.1 `RuntimeGraphDraftSession`（pass 视角）

- 一个会话对应一次事务：`edit(graph)` 取得某张图的 `GraphDraft`（第一次取时从该图解码），`commit()` 统一收口；
- `commit()` 重新编码**整个可达闭包**（被改的图、引用它们的图、静态 `Function` 值指向的图），不是只编码被改的
  那一张；
- 没有任何改写时不要提交：`rewriteReachableGraphs`（`src/passes/opt/generic/rewrite.cpp`）在改写数为 0 时
  直接返回原来的根图；
- pass 不应绕过会话直接改已编码的 `GCGraph`：它是只读的。

### 1.2 `GraphDraft`（可写域）

- `GraphDraft::decode(graph)` 得到副本；节点用 `gc_node_ref_t` 标识，输入与使用者两个方向由 `GraphDraft`
  一起维护（`appendInput` / `unlinkInput` / `replaceInput` / `set*Inputs` / `replaceAll*Uses` / `eraseNode`）；
- 分支的臂锚点（见 4.1）存在 `BRCH` 的负载里，节点被替换时用 `retargetBranchArmAnchors` 迁移；
  `isBranchArmAnchor(id)` 为真的节点不可随意删除；
- 被调图的引用（`FUNC` 的被调图、静态 `Function` 值）在草稿里仍是 `GCGraph *`，提交时由会话换成重新编码后的图。

### 1.3 `GraphDraftGroup`（新建图）

- `reserve()` 先为每张图取得一个键，键可以写进别的草稿里（`FUNC` 的被调图、静态 `Function` 值）；
- `encode()` 一次编码全组，并把键换成编码后的图。键在编码前不可解引用。

---

## 2. 写操作硬约束

### 2.1 图归属一致

- 边的两端必须是同一个 `GraphDraft` 的节点；禁止跨图直接连边；
- 跨图的值只能经由端口（`PORT`，普通形参、`with` 形参与闭包槽位）进入。

### 2.2 已编码的图不可变

- `GCGraph` 一经编码即只读；任何结构修改都发生在 `GraphDraft` 上，经 `commit()` 得到新图。

### 2.3 依赖与子图登记由提交维护

- 草稿里只需把 `FUNC` 的被调图与静态 `Function` 值写对；`dependencies` / `subGraphs` / `staticGraphRefs` 由
  编码按节点的实际引用重建（`collectDraftDependencyGraphs` / `collectDraftStaticGraphRefs`，
  `src/runtime/draft_session.cpp`）；
- 提交后，同一次提交的所有图里不会留下指向旧图的引用。

### 2.4 没有控制输入的 `GATE`

- 改写可能让某个 `GATE` 失去全部控制输入（它等待的分支被剪掉了）。`commit()` 先对闭包里的每张草稿执行
  `dissolveUnorderedGates()`：这样的 `GATE` 只是转发，直接消除；
- 充当臂头或臂尾的 `GATE` 不消除，失去控制输入时改为等待它的 `BRCH`。

---

## 3. `inlineCallableInDraft` 契约（当前实现）

**与 `std::opt::inline` pass 的区分**（阅读本节时请先建立预期）：

- **`inlineCallableInDraft`**：通用的内联原语；只要调用方传入合法的 `FUNC` 节点，就展开它的被调图。
- **`std::opt::inline`（`InlineRewritePass`）**：按策略（`InlineTargetStrategy`，
  `include/camel/passes/opt/config.h`）选择目标并调用该原语：`std::opt::inline::small`（Small）、
  `std::opt::inline::arm`（Arm，分支臂的被调图，不要求小）、`std::opt::inline::hybrid` / `std::opt::inline`（Hybrid，
  默认）。对递归的限制、`blockCallsToSccEntryCallees`、大小与增长预算等选择规则见
  [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md) §1.1 与 §6.1，本文不重复。

因此：本节描述的是 **原语的契约**；**不要**把它等同于「整个编译管线会对所有 `FUNC` 调用做内联」。

### 3.1 输入前提

- 目标节点是当前草稿里的 `FUNC`，被调图非空，有入口节点与输出节点；
- 实参个数等于被调图的形参个数（普通形参在前，`with` 形参在后）。

### 3.2 输出契约（`DraftInlineResult`）

- `valueExit`：内联后的值出口；原调用的值使用者改读它；
- `ctrlEntry`：内联体的控制入口（原调用是某条臂的臂头时，臂头迁到这里）；
- `ctrlExit`：内联体的控制完成点（原调用的控制使用者改等它；原调用是臂尾时，臂尾迁到这里）。

### 3.3 入口：调用的控制前驱交给谁

被调图的每个节点在调用方里都必须排在“原调用所排在其后的那些东西”之后：原调用的控制输入（它前面的语句、
它所在臂的 `BRCH`），以及仅凭实参把原调用放进某条臂的那些实参（`armPlacingArguments`）。

1. **入口根**：被调图里出口可达、不是值载体（`DATA` / `PORT`）、且输入全是 `DATA` / `PORT` 的节点
   （`collectExecutableEntryRoots`；`SYNC`、`GATE` 也算）。被调图的其余可执行节点都在某个入口根之后。
2. 入口根只有一个时，控制前驱直接接到它上面，它就是 `ctrlEntry`；有多个时新建一个 `SYNC` 接住控制前驱，
   各入口根等待这个 `SYNC`。
3. 被调图没有入口根（函数体只是转发某个形参或常量）时，在值出口上套一个 `GATE` 接住控制前驱
   （值出口本身就是下面第 4 条的形参 `GATE` 时不必再套）。否则结果不等待任何东西：原调用是臂尾时，臂尾
   成了分支外的值，实参在臂里算出的代码就不属于任何臂了。
4. **形参 `GATE`**：原调用有控制前驱时，被调图读到的每个实参都经过一个等待这些前驱（多个入口根时等待
   上面的 `SYNC`）的 `GATE`。就顺序而言它们是冗余的（每个可执行节点都在某个入口根之后）；保留它们是因为
   把图当作值 DAG 读的消费者（ONNX 导出器）把“经过 `GATE` 的常量”理解为“这个值要等分支或前一条语句之后
   才存在”，去掉后它的 4 条拒绝诊断测试失败。形参 `GATE` 不是入口：不要把控制前驱只接到它们上面。

### 3.4 控制完成语义

- 值与控制完成点分离时，新建一个 `GATE`（值输入为值出口，控制输入为控制完成点）并把它同时作为
  `valueExit` 与 `ctrlExit`；
- 校验保证 `GATE` 同时有值输入与控制输入（见第 4 节）。

### 3.5 形参端口上的使用

- 形参 `PORT` 的值使用全部改读实参；
- 形参 `PORT` 上的控制使用也改到实参上，不丢弃。前端只在一处产生这种边：分支捕获了某个形参时，
  `PORT -> BRCH`（“被捕获的值在 `BRCH` 之前就绪”，见 4.1 的条款 5）。

---

## 4. 验证器约束（提交时必须满足）

结构检查在**所有构建模式**下执行（不是 `ASSERT`），位置是运行时图编码之后：前端编码
（`src/compile/gir/graph_encode.cpp`）、每次 `RuntimeGraphDraftSession::commit()` 与
`GraphDraftGroup::encode()`（`src/runtime/draft_session.cpp`）。失败时抛出 `std::runtime_error`；在 pass
内由 pass 驱动（`src/execute/pass.cpp`）转为 `RuntimeDiag::PassInternalError`，诊断中给出**pass 名**
（即哪个遍的提交），程序不会开始执行。

`validateRuntimeGraphPayload`（单图，`src/runtime/graph.cpp`）：

1. 入口/出口/输出/返回锚点、端口、闭包节点、所有边的节点引用有效；数据槽位在范围内；无残留 `DREF`；
2. 邻接表双向一致（每条输入边在对端的输出表中出现，反之亦然）；
3. `BRCH` 有唯一选择输入，`BRCH`/`JOIN` 的臂数与负载一致，臂头臂尾的引用有效；
4. `GATE` 同时有值输入与控制输入；
5. `FUNC` 有被调图，`CALL` 有被调输入；
6. 分支臂不变量（4.1；`validateRuntimeBranchArms`，`src/runtime/branch_arms.cpp`）。

`validateRuntimeGraphCalls`（同一次提交的图全部编码之后）：

7. 每个 `FUNC` 的普通实参数与 `with` 实参数等于被调图的端口数。

旧的 `src/compile/gir/validate.cpp`（`assertGraphSealingPreconditions` 等，以 `ASSERT` 实现、没有任何调用点）
已于 2026-10-13 删除（图数据模型迁移第 1 步）；上面的检查取代了它。

测试用的故障注入遍（`src/passes/test/fault.cpp`）只在设置环境变量 `CAMEL_ENABLE_TEST_PASSES` 时可用，
用来验证破坏结构的图会在提交时被拒绝：`test::drop_call_argument`（条款 7）与
`test::leak_branch_arm`（条款 6）。

### 4.1 分支臂不变量

**后端怎样执行一个分支。** 四个调度器（NodeVM、FastVM 及其 JIT、Taskflow）和宏求值器都从同一个线性顺序出发
（`buildReachableExecutionTopoIndices`，`src/execute/graph_runtime_support.cpp`）：从出口沿三种输入边
（norm / with / ctrl）做后序遍历。遇到 `JOIN` 时，先排好分支内部从外面读的一切，再排 `BRCH`，然后**按臂的
顺序从各臂的臂尾出发**排出各臂，最后是 `JOIN`。于是 `BRCH` 与 `JOIN` 之间是每条臂一段连续的区间，各以
臂尾结束。执行时 `BRCH` 跳到被选中那条臂的区间起点（`resolveRuntimeBranchArmEntry`），臂尾之后跳到
`JOIN`；其余区间整段跳过。

这只有在“因 `BRCH` 而执行的节点恰好分成各条臂”时才是这张图的一次正确执行。精确表述如下。

**定义。** 设 `B` 是一个 `BRCH`，`J` 是它负载里记录的 `JOIN`，`tail_i` 是 `J` 的第 `i` 个 with 输入，
`head_i` 是 `B` 记录的第 `i` 条臂的臂头。

- **活节点**：出口（没有出口时为返回节点）沿输入边可达的节点。只有活节点会被排进线性顺序。死节点不参与下面
  任何一条（改写会留下无人使用的节点，例如被内联体里结果没人读的内层分支；它们不执行）。
- **分支内部** `I(B)`：从 `B` 出发沿三种输出边可达、且路径不经过 `J` 的活节点（不含 `B`、`J`）。三种边同等
  对待：读了分支内部的值，或者等待分支内部的节点，都使一个节点落在分支内部。
- **第 `i` 条臂** `A_i`：`I(B)` 中 `tail_i` 沿输入边（三种边）可达的节点，含 `tail_i` 本身（当它在
  `I(B)` 中时）。`tail_i` 不在 `I(B)` 中（分支之外算好的值、常量）表示这条臂没有代码。

**不变量。** 对每个活的 `B`：

1. **配对**：`J` 是 `JOIN`，它的 norm 输入是 `B`；`J` 是活的；`J` 的 with 输入数等于 `B` 的臂数，且
   `B` 记录的第 `i` 条臂的臂尾就是 `tail_i`。
2. **恰好一条臂**：`I(B)` 的每个节点恰好属于一个 `A_i`。
   - 属于两条臂（两个臂尾都依赖它）：它只会排进先排的那条臂的区间，另一条臂被选中时读到的是没有算过的值；
   - 不属于任何臂（等待 `B`，却没有哪条臂的结果依赖它）：没有任何后端会执行它；`J` 的控制输入若在
     `I(B)` 中，也必须属于某条臂。
   - 推论：分支内部的节点除 `J` 之外没有分支外的使用者（那样的使用者按定义也落在 `I(B)` 中，同样要属于
     恰好一条臂）。分支的值与完成只经由 `J` 流出。
3. **臂头**：`head_i` 是活节点时，从它出发沿输出边（不经过 `B`、`J`）到达的 `I(B)` 中的节点都属于
   `A_i`。后端从臂头与臂尾两头找一条臂的入口，臂头之后若有别的臂的节点，入口就会落到别的臂里。
   臂头本身**不要求**是臂的第一个节点，臂里的节点也不要求都在臂头之后：内联之后臂的节点可以直接等待
   `B`（见下面“臂头不是臂的定义”）。
4. **不交叉**：`I(B)` 中的 `JOIN`，它的 `BRCH` 也在 `I(B)` 中。即两个分支要么互不相交，要么一个整体位于
   另一个的某条臂里（嵌套分支的 `BRCH`、`JOIN` 与全部臂属于外层的同一条臂——这由条款 2 得出）。
5. **臂捕获的外部值**：`I(B)` 的节点读到的、不在 `I(B)` 中的值无条件执行，并且排在 `B` 之前（线性顺序在排
   `B` 之前先访问它们）。它们不属于任何臂，不受本不变量约束；所以改写不得把“只应在某条臂被选中时执行”的
   节点留在分支外面（例如带副作用的节点、可能出故障的节点）——这一条**无法**从结构上校验，由各改写负责
   （内联的做法见 3.3）。

各种情形怎么算：

| 情形 | 归属 |
|---|---|
| 臂内的调用（`FUNC` / `CALL`） | 一个普通节点，属于它所在的臂；被调图是另一张图，单独校验 |
| 嵌套分支 | 内层的 `BRCH`、`JOIN` 与各臂的节点都是外层 `I(B)` 的节点，属于外层的同一条臂 |
| 臂捕获的外部值（包括常量、形参） | 不在 `I(B)` 中，见条款 5 |
| 值恰为外部值或常量的臂 | 没有代码；`B` 直接跳到 `J` |
| 数据边与控制边 | 定义 `I(B)` 与 `A_i` 时不加区分 |
| 死节点 | 不参与；可以仍然是记录的臂头 |
| 一个 `BRCH` 被多个 `JOIN` 读（`devirtualizeBranchedCallInDraft`，只在 `enableBranchedCallSplit` 下产生） | 仅供导出的值 DAG，不可执行，不校验 |

**臂头不是臂的定义。** 前端建出的臂是单个 `FUNC` 节点，既是臂头也是臂尾。内联之后，臂的多个节点各自直接
等待 `B`（内联体的各个入口根、形参上的 `GATE`），后续改写还可能让记录的臂头变成没人使用的死节点。因此
后端与本不变量都以**臂尾**定义一条臂；臂头只是找入口时的一个起点（条款 3）。把“臂 = 臂头之后的节点”当作
约定去维护是此前多个缺陷的来源（`test/cases/opt/progen/README.md` 的根因 A、B、C、C1）。

**违反时的报告。** 一条 `PassInternalError`，说明是哪张图、哪个节点（种类、编号、算子或被调函数名）、
属于哪两条臂或不属于任何臂、是哪个 `BRCH`/`JOIN`；pass 名由诊断的阶段给出。例：

```
Pass 'test::leak_branch_arm' failed with an internal error: Runtime graph 'main' breaks the
branch-arm invariant: OPER 12 (:io/println) runs between BRCH 7 and its JOIN 6 and belongs to
no arm (no arm's result depends on it): no back end would run it (see
docs/technical/17_graph_rewrite_api_contracts.md, 4.1).
```

**开销。** 每张图一次活性遍历，每个分支一次限于分支内部的遍历（到 `JOIN` 为止），总体与图的大小近似成
线性。在 `benchmarks/` 的 mlp、gru、resnet、transformer、resnetbn 上经
`std::macro std::opt std::stats`（每个程序校验 544–1164 张次图）累计 1.0–2.2 毫秒，占编译用时的
0.25%–1.1%（Release 构建，2026-10-12）。因此没有开关，所有构建模式下都执行。

---

## 5. 对 pass 作者的建议

- 通过 `RuntimeGraphDraftSession` 完成改写，不直接改已编码的图；
- 不要假设“值出口等于控制完成出口”，统一使用 `DraftInlineResult` 的三个字段；
- 删除或替换节点时用 `replaceNode`（`src/passes/opt/generic/rewrite.h`）：它把被删节点的顺序约束交给使用者，
  并且不越过臂头；
- 不要让 `JOIN` 直接等待某条臂内部的节点：那个等待属于这条臂的臂尾。需要时在臂尾与 `JOIN` 之间插入一个
  `GATE`（值输入为原臂尾，控制输入为要等待的节点），并把 `BRCH` 记录的臂尾改成它（`inlineCallInOrder`，
  `src/passes/opt/inline/engine.cpp`）；
- 涉及 `BRCH`/`JOIN` 时，按 4.1 判断一个节点属于哪条臂（`branchArmNodes` / `armRegions`，
  `src/passes/opt/generic/generic.cpp`，与校验用的是同一个定义），不要只从臂头向后找；
- 新增改写时，先想清楚它对 4.1 的五条各意味着什么：提交时的校验能拦住条款 1–4，条款 5 要靠改写自己。
