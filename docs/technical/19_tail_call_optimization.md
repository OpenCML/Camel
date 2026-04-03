# Camel 线性调度中的尾调用优化（TCO）机制说明

**摘要**：本文档以 **NodeVM**（`NodeVMSchedPass`，直接按拓扑序解释 GIR）为主线，形式化说明尾调用优化（Tail Call Optimization, TCO）的**目标**、**合法性判据**与**运行时状态迁移**。**FastVM**（GIR → 字节码 → 解释或 JIT）仅在附录中对照，便于理解两套实现在判据上的一致性。相关控制与出口约定见 [18_sync_control_semantics.md](18_sync_control_semantics.md)。

---

## 1. 引言

### 1.1 问题与目标

在 Camel 中，每个函数体对应一张 **GIR 图**；对该图的求值由调度器按依赖关系线性（或并行）执行。若递归或互递归通过 **`FUNC` 节点**调用子图，朴素实现会在 C++ 层面对 `call` 进行嵌套调用：每深入一层分配一帧 **Frame**，并增加 C++ 调用栈深度。当递归深度较大时，存在 **C++ 栈溢出** 与 **帧池压力** 的风险。

**尾调用优化**在此处的工程含义是：当某次 `FUNC` 调用在语义上等价于「当前函数体的最后一步——其返回值即为本函数返回值」时，实现**不再**对本次调用使用「嵌套 `call` + 返回后继续执行当前拓扑序列」的路径，而是**在当前 `call` 激活期内**重置当前图、当前帧与拓扑游标，使 callee 图**接棒**执行。这样，任意深度的尾递归在 C++ 栈上仅占用**常数层**的 `call` 嵌套（在纯尾递归链上甚至可保持单层循环）。

本文档只讨论 **NodeVM** 与 **FastVM** 中针对 **`FUNC` 节点**的尾调优化。**`CALL` 节点**（调用一阶函数值）在 NodeVM 中始终走普通嵌套 `call`，不参与上述 TCO。

### 1.2 实现位置（代码索引）

| 组件 | 路径 |
|------|------|
| NodeVM 调度入口 | `src/passes/sched/nodevm/nodevm.cpp`（`apply`、`call`） |
| 拓扑工具 | `include/camel/common/algo/topo.h`（`findReachable`） |
| FastVM 编译期 TAIL 判定 | `src/passes/sched/fastvm/compile.cpp` |

---

## 2. 术语与前提

### 2.1 图、出口节点与槽位

- **图（Graph）**：函数体的 GIR；含若干 **Node**，边分为 Norm / With / Ctrl 等（细节见 [05_graph_ir.md](05_graph_ir.md)）。
- **exitNode()**：该图语义上的**出口锚点**；运行时从此节点对应的 **data index** 读取返回值。验证器通常要求出口为 **GATE** 或等价结构。
- **Frame**：一次 `call(graph, frame)` 的槽位环境；`frame` 与 `graph` 的端口、闭包槽位一一对应。

### 2.2 FUNC 节点与普通调用路径

- **`FUNC` 节点**：携带**静态**子图引用（`bodyGraph()`）；求值时需为子图准备参数并执行子图。
- **非尾调路径**（NodeVM）：`framePool_.acquire(funcGraph)` → `fillFrameForFunc(currFrame, funcFrame, ...)` → 递归 `call(funcGraph, funcFrame)` → 将子图返回值写入当前节点槽位 → 继续当前图的拓扑循环。

尾调路径则**不**发起上述递归 `call`，而是改写 `currGraph` / `currFrame` 并 `goto loop_start`（见第 7 节）。

### 2.3 GATE 与「尾值锚点」

当 `exitNode()` 类型为 **GATE** 时，Norm 侧通常接 **返回值数据流**（常见为 **JOIN** 汇合 `if/else`），Ctrl 侧可接 **须在返回值落定前完成的控制序**（如 `sync` 下的计时、`println` 等）。TCO 判定只关心「**哪一个节点代表返回值路径上的最后一个计算结点**」，因此 NodeVM 将 **GATE** 解析为：

1. 若存在 Norm 输入：取 **最后一个 Norm 输入** 作为 **lastNode**（尾值锚点）。
2. 否则若有 Ctrl 输入：取 **最后一个 Ctrl 输入** 作为 **lastNode**（无值返回场景）。

该规则与 FastVM 的 `resolveTailValueNode` 一致，用于避免把「纯副作用链」误当作返回值尾结点。

---

## 3. NodeVM 执行管线总览

### 3.1 从 `apply` 到 `call`

`NodeVMSchedPass::apply` 对入口图执行：

1. `framePool_.acquire(graph)` 得到 **rootFrame**；
2. `call(graph, rootFrame)`；
3. `call` 内部负责释放 **rootFrame**（及尾调路径上可能涉及的 **curr / twin**）；
4. 将返回值交给 `context_->captureProcessExitCode`。

### 3.2 `call` 的状态变量

在一次 `call(rootGraph, rootFrame)` 内，核心状态包括：

| 变量 | 含义 |
|------|------|
| `currGraph` | 当前正在执行的 GIR |
| `currFrame` | 当前图对应的 Frame |
| `currNodes` | 当前图的拓扑节点序列（`std::span<Node*>`） |
| `twinFrame` | **孪生帧**：互尾递归（图 A 尾调图 B、B 尾调 A）时缓存另一张图的帧 |
| `tillNode` / `skipNode` / `joinNode` | **BRCH/JOIN** 动态跳转（见第 5 节） |
| `currRecursionDepth_` | 语义递归深度计数：仅在进入 `call` 时自增，**仅在离开 `call` 时自减**；尾调路径**不**进入子 `call`，故不在尾链上累加深层计数 |

主循环标签为 **`loop_start`**：每次尾调成功后从此处重新开始，重新计算 `lastNode` 并从头扫描 `currNodes`。

---

## 4. 拓扑序列的构造与语义

### 4.1 `buildTopoNodes`

对给定 `graph`，NodeVM 调用 `findReachable(exitNode, getInputs, false)`：

- **起点**：`graph->exitNode()`（含出口锚点本身）。
- **getInputs(n)**：返回 `n` 的**同图**上游列表，顺序为：
  1. 全部 **ctrlInputs**（控制依赖优先）；
  2. 全部 **dataInputs**（值依赖在后）。

`findReachable` 在 `include/camel/common/algo/topo.h` 中实现为：自出口做 DFS，**先深入各输入，再在回溯时将当前结点追加到向量末尾**，得到的是依赖先于消费者的**后序**列表；对该列表**从头到尾**线性扫描，即满足「上游先于下游」的执行顺序。

### 4.2 缓存

拓扑结果缓存在 `topoNodesOwned_[graph]`，并挂到 `graph` 的 extra 槽位，避免同一张子图重复建序。

### 4.3 与 TCO 的关系

TCO 在「当前 `currNodes` 中某下标 `i` 处命中 `FUNC`」时，可能**放弃**对 `i` 之后所有下标的执行。因此，**合法性判据**必须保证：若执行尾调，则被跳过的后缀在语义上**不包含**仍需执行的计算或副作用。第 6 节的 **平凡后缀** 条件即针对该需求。

---

## 5. 分支（BRCH/JOIN）与主循环的交互（简述）

NodeVM 在遇到 **BRCH** 时设置：

- `tillNode`：选中分支的**头结点**（从此开始执行）；
- `skipNode` / `joinNode`：选中分支的**尾**与对应 **JOIN**。

主循环在 `tillNode` 非空时 **continue** 跳过序列前缀，直到到达分支头；在 `skipNode` 命中后切换到向 **JOIN** 推进的模式。

**尾调触发时**，实现将 **`tillNode`、`skipNode` 清空**。理由：尾调表示当前图剩余工作已由 callee 接替，不应再保留未消费的分支跳转状态，否则易与新的 `currGraph` 不一致。

TCO 合法性判据（第 6 节）基于**整张图**的静态 `currNodes`，**不**对「当前激活分支」单独建后缀；这与 GIR 构造阶段对分支布局的约定相匹配。若拓扑序与分支跳转语义出现非常规交错，需在 GIR 层约束或验证，而非仅在 NodeVM 局部修补。

---

## 6. 尾调合法性的形式化判据（NodeVM）

以下均在**同一次** `loop_start` 迭代开头已解析出：

- `lastNode`：第 2.3 节所述尾值锚点（可能为 JOIN、FUNC、或其他类型，取决于 lowering）。
- `lastNodeIsJoin`：`(lastNode->type() == JOIN)`。

对当前扫描到的结点 `n`，仅当 `n` 为 **`FUNC`** 时才可能触发 TCO。记 `funcNode = cast<FuncNode>(n)`，`funcGraph = funcNode->bodyGraph()`。

### 6.1 形状条件 `tailShape`

定义：

\[
\text{tailShape} \equiv (n = \texttt{lastNode}) \;\lor\; (\texttt{lastNodeIsJoin} \land \texttt{funcNode.hasMatchedJoin()} \land \texttt{funcNode.matchedJoin()} = \texttt{lastNode})
\]

- **第一析取**：出口锚点解析后**直接等于**当前 `FUNC`（少见，多见于特定 lowering）。
- **第二析取**：当前 `FUNC` 在 GIR 上与**该图尾值 JOIN** 配对（分支尾部的子图调用汇入同一 JOIN），表示「该调用的结果即分支汇合值」，是递归模板中的常见形态。

### 6.2 平凡后缀条件 `anchorOk`

在 `currNodes` 中查找 **`lastNode` 首次出现的下标** `anchorIdx`（线性扫描）。若未找到，则不允许尾调。

定义 **平凡后缀** 谓词（与 FastVM `hasOnlyTrivialSuffixAfter` 一致）：

\[
\text{anchorOk} \equiv (\texttt{anchorIdx} < |\texttt{currNodes}|) \land \forall j \in (\texttt{anchorIdx}+1,\, |\texttt{currNodes}|)\,:\, \texttt{currNodes}[j].\texttt{type()} = \texttt{GATE}
\]

即：**在静态拓扑序列中，`lastNode` 之后只能出现 `GATE` 类型的结点。**

**语义解释**：若 `lastNode` 为 **JOIN**，则 JOIN 之后若仍存在 **OPER**、**FUNC**、**CALL** 等，则这些结点尚未执行；此时若从某 `FUNC` 尾调跳出当前循环，将**永久跳过**它们，导致副作用缺失或返回值错误（例如 `sync` 函数中 JOIN 之后的 `println`）。**GATE** 在 NodeVM 的 `switch` 中无操作（仅占位），故允许出现在后缀中。

### 6.3 总判定

\[
\text{isTailCall} \equiv \text{tailShape} \land \text{anchorOk}
\]

仅当 `isTailCall` 为真时，进入第 7 节的尾调路径；否则走第 8 节的普通 `FUNC` 路径。

---

## 7. 尾调触发时的运行时步骤（NodeVM）

设尾调前 `lastFrame = currFrame`（即当前激活帧）。实现**清空** `tillNode`、`skipNode`（`joinNode` 随之失效于后续逻辑）。随后按 **子图是否与当前图相同** 及 **孪生帧** 分岔。

### 7.1 自递归：`funcGraph == currGraph`

- **不**切换 `currGraph` / `currNodes`。
- **不**分配新 Frame：`currFrame` 继续绑定当前图。
- `fillFrameForFunc(lastFrame, currFrame, funcGraph, n)`：用**调用点**的实际参数覆盖当前帧的形参槽位（等价于在同一帧上「重启」该函数体）。
- `goto loop_start`：从下标 0 重新扫描同一 `currNodes`，`currRecursionDepth_` **不**因尾调增加（未进入子 `call`）。

### 7.2 跨图尾调：`funcGraph != currGraph` 且 `twinFrame` 可复用

若 `twinFrame != nullptr` 且 `twinFrame->graph() == funcGraph`：

- **交换** `currFrame` 与 `twinFrame`（互尾递归：在两张图的帧之间轮换）。
- `fillFrameForFunc(lastFrame, currFrame, funcGraph, n)` 向**切换后的当前帧**写入新参数。
- 更新 `currGraph = funcGraph`，`currNodes` 取自子图缓存或 `buildTopoNodes(funcGraph)`。
- `goto loop_start`。

### 7.3 跨图尾调：需分配新帧

否则：

1. 若 `twinFrame` 非空且不是 `rootFrame`，则 `framePool_.release(twinFrame)`。
2. `twinFrame = currFrame`（旧当前帧降为孪生，供另一图复用）。
3. `funcFrame = framePool_.acquire(funcGraph)`，`fillFrameForFunc(lastFrame, funcFrame, funcGraph, n)`，`currFrame = funcFrame`。
4. 更新 `currGraph`、`currNodes`，`goto loop_start`。

### 7.4 与普通路径的对比

尾调路径**不**调用递归的 `call(funcGraph, ...)`，因此：

- 不在尾链上叠加 `currRecursionDepth_`；
- 不执行当前 `FUNC` 结点在 `currNodes` 中**之后**的任何结点（包括本应执行的 JOIN 汇合之后的代码），故必须以第 6.2 节约束后缀。

---

## 8. 非尾调 FUNC 路径（对照）

当 `isTailCall` 为假：

1. `funcFrame = framePool_.acquire(funcGraph)`；
2. `fillFrameForFunc(currFrame, funcFrame, funcGraph, n)`；
3. `callResult = call(funcGraph, funcFrame)`（**嵌套**进入子 `call`，`currRecursionDepth_` 在子调用内 +1/-1）；
4. `currFrame->set(n->index(), callResult)`；
5. 继续 `for` 循环，执行拓扑序列中后续结点。

---

## 9. 图返回值的读取与栈帧释放

### 9.1 正常返回

当内层 `for` 循环**自然结束**（未再尾调跳出），当前 `call` 执行：

- `currRecursionDepth_--`（与入口处的 `++` 配对）；
- `result = currFrame->get(currGraph->exitNode()->index())`；
- **帧释放**（存在 `twinFrame` 时按「先非 root 的 curr / twin，再 root」的顺序，见源码注释中的三种情形）；
- `return result`。

### 9.2 尾调链的语义

尾调链上多次 `goto loop_start` 均发生在**同一次**外层 `call` 的 `try` 块内，直到某次执行**不再**尾调并最终跑完子图，才统一落到上述返回逻辑。因此，**返回值**始终从**最终**的 `currGraph->exitNode()` 读取，**当前帧**已为最终子图对应的帧。

---

## 10. 小结（NodeVM 机制要点）

1. **拓扑序**：自 `exitNode` 反向 `findReachable`，ctrl 输入优先于 data 输入，得到线性执行序列。
2. **尾值锚点 `lastNode`**：若出口为 GATE，取最后一个 Norm 输入（无则最后一个 Ctrl 输入）。
3. **TCO 条件**：`tailShape`（FUNC 与尾值 JOIN/出口对齐）且 `anchorOk`（`lastNode` 在拓扑中之后**仅 GATE**）。
4. **效果**：满足条件时在同一 `call` 内重置图/帧并 `goto loop_start`，避免子 `call` 与 C++ 栈加深；**CALL 结点无 TCO**。

---

## 附录 A：FastVM 中的对应机制（参考）

FastVM 在 **编译期**（`compile.cpp`）将 GIR 转为字节码，尾调用以 **`OpCode::TAIL`** 标记（与 **`OpCode::FUNC`** 相对）。

### A.1 尾值结点 `tailValueNode`

与 NodeVM 的 `lastNode` 解析规则相同：出口为 GATE 时取最后一个 Norm 输入。

### A.2 平凡后缀

对拓扑序列下标 `i`，若对所有 `j > i`，`topoSortedNodes[j]` 均为 **GATE**，则称满足平凡后缀（实现名 `hasOnlyTrivialSuffixAfter`）。

### A.3 FUNC 与 JOIN 两处改写

- **直接处理 FUNC 结点**：`isTail = (node == tailValueNode) && hasOnlyTrivialSuffixAfter(i)`。因常见图中 `tailValueNode` 为 **JOIN**，多数递归 **FUNC** 在此处为假。
- **处理 JOIN 结点**：若 `node == tailValueNode` 且平凡后缀成立，则通过 **`joinTargetMap`** 将此前跳转到该 JOIN 的 **`FUNC` 字节码** 改写为 **`TAIL`**。

解释器 / JIT 遇到 **`TAIL`** 时跳过嵌套调用帧的常规压栈语义，与 NodeVM「不递归 `call`」在效果上对齐。

### A.4 编译开关

`CompileStrategy::enableTailCallDetection` 为 **false** 时，编译器不将 `FUNC` 提升为 `TAIL`。**NodeVM 当前无等价运行时开关**；若需严格对齐「关闭 TCO」的语义，需在 NodeVM 侧增加配置后再实现。

### A.5 判据对齐表

| 概念 | NodeVM | FastVM |
|------|--------|--------|
| 尾值锚点 | `lastNode`（GATE 上 Norm/Ctrl 规则） | `tailValueNode` |
| 后缀约束 | `hasOnlyTrivialTailSuffixAfter` | `hasOnlyTrivialSuffixAfter` |
| FUNC 与 JOIN 关系 | `tailShape` | JOIN 处回填 + FUNC 处直接判定 |
| 执行效果 | `goto loop_start`，不嵌套 `call` | 发出 `TAIL` 操作码 |

---

## 附录 B：已知局限与验证建议

1. **平凡后缀仅允许 `GATE`**：若 lowering 在尾值结点与出口之间插入在 NodeVM 中亦为无操作的结点类型（如某些 **SYNC**），当前仍判为**非平凡**，从而禁用 TCO。扩展白名单需与 FastVM **同步**修改并增加测试。
2. **静态拓扑与动态分支**：判据基于全图拓扑序；依赖 GIR 对 BRCH/JOIN 布局的常规约定。
3. **回归用例**：`test/run/para/merge_sort_tco_smoke.cml` 用于快速验证递归 `sync` 路径上 **JOIN 之后** 的 `println` 仍执行；全量性能场景见 `test/run/para/merge_sort.cml`。

---

## 参考文献档

- [05_graph_ir.md](05_graph_ir.md) — GIR 结点与边
- [06_runtime_and_execution.md](06_runtime_and_execution.md) — 运行时与调度总览
- [18_sync_control_semantics.md](18_sync_control_semantics.md) — GATE、EXIT 与 sync 语义
