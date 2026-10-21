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
- `commit()` 只为**改过的图**编码新版本：图按编号（`GraphId`）引用别的图，别的图换了版本，它不必换。需要新版本的
  有：内容改变了的图、原程序没有的图（特化出的新图）、以及持有静态 `Function` 值的图——函数值指着它被做出时的
  程序，所以持有指向（直接或经调用、函数值间接）出现新版本之图的函数值的图也要重编码。其余的图原样放进新的
  `Program`。被要求编辑而没有改动的草稿（`GraphDraft::mutations() == 0`），以及编码后内容与原图相同的图
  （且不持有函数值），沿用原图；
- 没有任何改写时不要提交：`rewriteReachableGraphs`（`src/passes/opt/generic/rewrite.cpp`）在改写数为 0 时
  直接返回原来的根图；
- pass 不应绕过会话直接改已编码的 `GCGraph`：它是只读的。

### 1.2 `GraphDraft`（可写域）

- `GraphDraft::decode(graph)` 得到副本；节点用 `gc_node_ref_t` 标识，输入与使用者两个方向由 `GraphDraft`
  一起维护（`appendInput` / `unlinkInput` / `replaceInput` / `set*Inputs` / `replaceAll*Uses` / `eraseNode`）；
- 分支的臂是区域（见 4.1），节点属于哪条臂由它的区域决定；替换节点时新节点要放进旧节点的区域；
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
  `PORT -> BRCH`（“被捕获的值在 `BRCH` 之前就绪”）。

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
3. `BRCH` 有唯一选择输入，`BRCH`/`JOIN` 的臂数与负载一致；
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

**臂是区域。** 一个 `BRCH` 的每条臂是图里的一个 `Arm` 区域（`GCRegion`，`owner` 为该 `BRCH`，`index` 为臂号）；
臂里的代码就是属于这个区域（或嵌在其中的区域）的节点。`BRCH` 的负载只有它的 `JOIN` 与臂数；`JOIN` 的第 `i`
个 with 输入是第 `i` 条臂的结果。没有臂头、臂尾这样的记录。

**后端怎样执行一个分支。** 四个调度器（NodeVM、FastVM 及其 JIT、Taskflow）和宏求值器都从同一个线性顺序出发
（`computeExecutionOrder`，`src/execute/graph_runtime_support.cpp`）：从出口沿三种输入边（norm / with / ctrl）
做后序遍历。遇到 `JOIN` 时，先排好各臂从分支外面读的一切，再排 `BRCH`，然后**按臂的顺序**把各臂区域的节点
各排成一段连续的区间，最后是 `JOIN`。执行计划（`ExecPlan::branchArmEntries`、`branchArmTails`）给出每条臂的
入口（区间里第一个真正执行的节点）与末尾：`BRCH` 跳到被选中那条臂的入口，末尾之后跳到 `JOIN`；其余区间整段
跳过。

**不变量**（`validateRuntimeBranchArms`，`src/runtime/branch_arms.cpp`，每次提交都检查；只看活节点，即出口沿
输入边可达的节点）。对每个活的 `BRCH` `B`，`J` 是它负载里的 `JOIN`：

1. **配对**：`J` 是 `JOIN`，它的 norm 输入是 `B`；`J` 是活的；`B` 的每条臂都有一个区域。
2. **等待 `B` 的节点在某条臂里**：以 `B` 为控制输入的节点（`J` 除外）属于 `B` 的某条臂。否则没有后端会执行它。
3. **只读一定会执行的节点**：一个节点只读与它同一个控制区域、或包住它的控制区域里的节点；分支内部的值与完成
   只经由 `J` 流出（`J` 读各臂的结果）。读另一条臂里的节点，意味着只有那条臂被选中时它才会算过。
4. 一个 `BRCH` 被多个 `JOIN` 读（`devirtualizeBranchedCallInDraft`，只在 `enableBranchedCallSplit` 下产生）
   是仅供导出的值 DAG，不可执行，不校验。

**区域由谁维护。** 前端在写臂的地方直接建出臂：臂的每个节点落在这条臂的区域里（`GraphDraft::setInsertRegion`），
臂里读外面的节点、等待 `BRCH` 的根节点由建图补上控制边；`JOIN` 的 with 输入就是臂的结果。自动微分把臂提成
子图再放回时用 `raiseBranchArms` / `lowerBranchArms`（`callSite` 标出还是一次调用的臂）。改写遍
维护区域：新加或改接输入的节点落在其输入中最内层的控制区域里（`GraphDraft::inheritRegion`，`JOIN` 除外）；
`rewriteNode` 保留区域；擦除 `BRCH`/`LOOP` 时它拥有的区域并入外层（`dissolveRegionsOf`）；一个遍替换节点时，
新节点要放进被替换节点所在的区域（`DraftNodeInit::region`、`setNodeRegion`）。

**违反时的报告。** 一条 `PassInternalError`，说明是哪张图、哪个节点（种类、编号、算子或被调函数名）、属于哪个
`BRCH` 的哪条臂；pass 名由诊断的阶段给出。例：

```
Pass 'test::leak_branch_arm' failed with an internal error: Runtime graph 'main' breaks the
branch-arm invariant: SYNC 12 runs between BRCH 7 and its JOIN 6 and belongs to no arm: it waits on
the BRCH and no arm's result depends on it (see docs/technical/17_graph_rewrite_api_contracts.md, 4.1).
```

### 4.2 边的不变量（`graph_edges.cpp`）

提交（与建图编码）对每张产出的图检查两件事，随调用检查一起执行，失败是一条带图名与节点编号的运行时错误：

1. **无环**：沿输入（值、`with`、控制）走，回不到自己。消息给出环上的路径。唯一的例外是会话声明自己建的形态没有后端会运行（`RuntimeGraphDraftSession::allowNonExecutable`；现在只有导出的 `enableBranchedCallSplit`，它造出同级 `JOIN`、其使用者合并的形态）；后端在建执行计划时拒绝环。
2. **边上的类型相容**：`COPY`/`GATE` 的类型与输入相容，`CAST` 不是被禁止的转换，`JOIN` 的每条臂能放进 `JOIN`，`ACCS` 取出的元素类型相容，调用的实参、结果与被调图的形参、结果相容。**相容**指相等、互为细化（已知形状的张量与同元素类型的无形状张量）、或可互相赋值（`Type::unify`、`assignableFrom`），形参名与字段名不计。算子的结果不与算子的类型解析器比较：节点的类型是解析器结果的细化（形状推断之后）。

新增一类检查时，先让一个测试 pass 在快层上只收集违规（不拒绝），列出已有 pass 的违规，再让它强制。测试：`test/plans/feat/opt/edges.plan.toml`。

### 4.3 提交冲突

一个会话从某个程序开始；提交时上下文的程序可能已被别的提交换成了新的。`conflicts()` 给出会话读过或写过、而在上下文的程序里是另一个版本的图；`commitChecked()` 在非空时抛 `GraphCommitConflict`（调用者用当前程序开新会话重做）。普通的 `commit()` **不**失败：过期会话的提交得到一个自己的程序（从它开始时的程序而来，不是上下文的程序）。这是常态：运行中的程序对旧程序的函数值调用 `with_grad`、导出等，在快层里每次运行都有上千次。过期而不冲突的提交目前不并入最新程序（设计 4.3.1 的“在最新快照上替换改过的图”还没有做）。

---

## 5. 对 pass 作者的建议

- 通过 `RuntimeGraphDraftSession` 完成改写，不直接改已编码的图；
- 不要假设“值出口等于控制完成出口”，统一使用 `DraftInlineResult` 的三个字段；
- 删除或替换节点时用 `replaceNode`（`src/passes/opt/generic/rewrite.h`）：它把被删节点的顺序约束交给使用者，
  ；
- 涉及 `BRCH`/`JOIN` 时，一个节点属于哪条臂由它的区域决定（`nodeRegion`、`armRegionOf`）；加节点时给对区域，
  提交时的校验会拦住读了另一条臂、或等待 `BRCH` 却不在任何臂里的节点。
