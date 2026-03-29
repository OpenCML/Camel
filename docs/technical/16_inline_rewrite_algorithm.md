# `std::inline` 重写算法与 GIR API 协作详解

本文针对当前实现（`src/passes/opt/inline/inline.cpp` + `src/compile/gir/graph.cpp`）进行工程化拆解，重点说明：

- `InlineRewritePass` 的真实执行顺序；
- 它依赖的 GIR Graph API 如何分层协作；
- 正确性不变量（invariant）如何被维持；
- 预算保护、裁剪策略与已知语义边界。

---

## 1. 总体设计目标

当前 `std::inline` 在 **Pass 层**是一个**跨图事务式重写**过程，目标有三层：

1. **跨图引用一致性**：rewrite 会话中不允许残留 legacy/source 图指针。
2. **分支语义不破坏**：保持 `BRCH/JOIN` 的 arm-slot 对齐与控制路径语义。
3. **可收敛性**：通过 SCC + 冻结目标 + 预算哨兵，避免单轮失控膨胀。

### 1.1 重要：`std::inline` 已是策略化目标选择

实现上必须把下面两点分开理解：

| 层次 | 含义 |
|------|------|
| **`GraphBuilder::inlineCallable`** | **通用原语**：对**任意**满足前提的 `FUNC` 节点，把其 `bodyGraph` 克隆进调用方并返回 `InlineResult`。 |
| **`InlineRewritePass`（`std::inline*`）** | **策略层**：先枚举 SCC 内 `FUNC` 候选，再按策略过滤并调用 `inlineCallable`。支持 `small` / `arm` / `hybrid` 三种策略。 |

当前 pass 入口命名与含义：

- `std::inline`：默认综合策略（等价 `std::inline::hybrid`）；
- `std::inline::small`：小子图但**非分支 arm**策略；
- `std::inline::arm`：分支 arm 小子图策略；
- `std::inline::hybrid`：综合策略（满足 `small(non-arm)` 或 `arm` 条件即内联）。

其中「小子图」当前定义为：目标 `bodyGraph` 中，除 `DATA/PORT` 外节点数 `<= 8`。

---

## 2. Pass 入口与事务边界

`InlineRewritePass::apply` 的入口动作：

1. 用 `GraphRewriteSession session(graph)` 开启重写事务；
2. `session.root()` 作为可编辑根图（draft root）；
3. 后续所有编辑通过 `session` 方法完成（而非直接改原图）；
4. 结束时 `session.finish()`：
   - 若无改动，返回 source root；
   - 若有改动，触发 `GraphDraft::seal()`，导出新的 sealed graph。

这意味着 `inline` pass 在架构上遵守“**draft 内改写，seal 时一次性收口**”原则。

---

## 3. 第一阶段：构建可编辑图映射（source -> draft）

### 3.1 可达图收集

`collectReachableGraphs` 从 root 递归遍历：

- `subGraphs()`
- `dependencies()`

得到本次会话需要考虑的 source 图集合。

### 3.2 按需 materialize

`ensureEditableGraph(...)` 是核心桥接函数：

- 如果是 root：直接用现有 draft root；
- 如果是子图：先确保 owner draft 化，再 `importSubGraph(..., CloneIntoDraft)`；
- 如果是依赖图：在 draft root 上 `importDependency(..., CloneIntoDraft)`；
- 维护 `editableGraphsBySource[source_ptr] = draft_ptr` 缓存。

这个阶段确保后续 rewrite 不会“写到共享旧图”。

---

## 4. 第二阶段：会话级 canonical 化（消除 legacy/source 混用）

### 4.1 为什么要单独做这一步

仅完成图级 clone/import 还不够：节点内部仍可能保存 legacy 图引用（例如 `FUNC.bodyGraph` 或 `DATA(Function).graph`）。

如果不统一重定向，后续 seal 可能出现“draft 图结构 + legacy 引用”混杂，造成悬挂/越权引用问题。

### 4.2 机制

当前实现不再依赖独立的 `retargetLegacyRefsByUseIndex(...)`，而是把 canonical 化内聚到
`GraphDraft::cloneIntoDraft` 流程中：

1. 新图克隆后立即登记 `draftBySource_`；
2. 对“新克隆图内部”先执行一次 `retargetKnownDraftRefsInOwner`：
   - 把它内部 `FUNC.bodyGraph` / `DATA(Function).graph` 指向已有 draft；
   - 同步 dependency；
   - 仅在“词法外层匹配”时重建 subgraph，避免错误嵌套。
3. 再执行 `retargetOwnedGraphRefs`，把所有 owned 图中指向该 source 的引用统一替换为新 draft。

pass 层保留 `assertNoLegacyGraphRefs(...)` 作为硬断言，确保会话出口前不再含 legacy/source 引用。

### 4.3 一个可复现的理解示例（A/B/C）

设有三张 source 图：

- `A`（root）
- `B`（被 `A` 引用）
- `C`（被 `B` 引用）

且当前会话按顺序发生：

1. `cloneIntoDraft(B)` 生成 `B'`，并登记 `draftBySource_[B] = B'`；
2. `cloneIntoDraft(C)` 生成 `C'`，并登记 `draftBySource_[C] = C'`。

此时若不做额外处理，`C'` 内部仍可能保留对 legacy `B` 的引用（例如 `FUNC.bodyGraph == B`）。
这会导致同一 rewrite 会话中同时存在：

- 一部分路径使用 draft 指针（`B'`）；
- 一部分路径使用 source 指针（`B`）。

这种混用最终会表现为：

- 图层级错挂（outer/subgraph 污染）；
- dependency 与节点引用不一致；
- seal/validate 阶段出现断言失败或行为偏差。

因此 `cloneIntoDraft` 需要两步协同：

1. **local 修正（`retargetKnownDraftRefsInOwner`）**  
   只处理“新克隆图自身”（如 `C'`）内部引用，把它指向的已知 source 图改到对应 draft。
2. **global 修正（`retargetOwnedGraphRefs`）**  
   扫描当前所有 owned 图，把所有仍指向本轮 source（如 `C`）的引用替换为新 draft（`C'`）。

> 结论：local 解决“新图内部残留引用”，global 解决“全图域历史残留引用”。  
> 只做其中之一都可能留下 source/draft 混用。

### 4.4 顺序为什么必须固定

`cloneIntoDraft` 中的顺序是一个硬约束：

1. 先登记 `draftBySource_[source] = draft`；
2. 再做 local 修正；
3. 最后做 global 修正。

原因：

- 若先修正再登记，local 阶段无法查询到完整映射；
- 若不先做 local，global 阶段不保证能覆盖“新图内部对旧 draft 的跨图引用场景”；
- 该顺序可保证 canonical 化在单轮内收敛。

---

## 5. 第三阶段：SCC 缩点调度（callee-first）

### 5.1 建图与缩点

`buildSccGraphPlan(sourceGraphs)` 基于图间引用构建有向图（caller -> callee），然后：

- Tarjan 求 SCC；
- 凝结成 DAG；
- 取逆拓扑顺序作为处理序（callee-first / bottom-up）。

### 5.2 目的

先处理最深层被引用图，减少“上层先展开导致下层重复重扫”的抖动，提升单轮 rewrite 的稳定性。

---

## 6. 第四阶段：SCC 内联执行模型

### 6.1 初始目标冻结（候选枚举 + 策略过滤）

`collectInitialInlineTargetsForScc` 仅收集进入 SCC 时的初始目标，流程分两层：

1. **候选枚举**：扫描 SCC 内 draft 图上的所有 `FUNC`，并额外标记是否为 `BRCH` armHead；
2. **策略过滤**：
   - `small`：仅小子图且**不是** `BRCH` armHead；
   - `arm`：`BRCH` armHead 且小子图；
   - `hybrid`：满足 `small` 或 `arm` 任一条件。
3. **SCC 入口函数保护**（默认开启）：
   - 对每个 SCC，先根据 use-index 识别“被 SCC 外调用的 callee 图集合”（入口集合）；
   - 若 SCC 内调用目标属于该入口集合，则跳过内联。
   - 该规则用于防止 `fib -> arm -> fib` 这类场景把 SCC 入口函数反复向内展开。

策略过滤发生在应用前；因此 budget 与 SCC 收敛语义仍保持一致，只是目标集合来源由固定模式改为可配置策略。

**内联过程中新增的 FUNC 不在本 SCC 同轮继续处理**，从机制上避免单轮爆炸。

### 6.2 单目标改写步骤

对每个目标：

1. 调 `session.inlineCallable(path, inlineOptions)`，且本 pass 固定：
   - 使用默认 `syncPolicy = InlineSyncPolicy::Auto`
2. `inlineCallable` 内部完成值-控门控（必要时自动插入 `GATE`），并统一返回门控后的
   `valueExit` 与控制入口 `ctrlEntry`；
3. pass 侧在 `eraseNode(path)` 前，会把所有 `path->ctrlInputs()` 前驱的 ctrl 输出重连到
   `inlined.ctrlEntry`（通用控制前驱接线，不再限定 BRCH 特判）；
4. `session.eraseNode(path)` 删除原 FUNC 节点。

### 6.3 GATE 门控的语义价值

内联后值出口通常是数据节点，而控制出口是另一条路径。直接给值节点硬挂控制边会污染调度语义。

`GATE` 在这里承担“值/控会合代理”，保证：

- JOIN 看到的是带控制约束的值；
- 不要求原值节点本体承载控制语义。

---

## 7. `inlineCallable` 的关键语义（底层 API）

`GraphBuilder::inlineCallable` 是唯一内联原语，返回 `InlineResult`：

- `valueExit`：值出口；
- `ctrlEntry`：控制入口；
- `insertedEntrySync`：是否自动插入入口同步节点。

### 7.1 入口 SYNC（按目标数自适应）

`inlineCallable` 先构造“入口目标集合”：

1. 优先使用**参数门控目标**（参数/闭包/free-input 映射产生的 `GATE`）；
2. 仅当没有参数门控目标时，才从 `exitNode`（值出口锚点）反向可达子图中提取入口根节点。

然后按目标数量决定入口形式：

- 目标数 `> 1`：创建 `entrySync`，并连 `entrySync -> target_i`（Ctrl）；
- 目标数 `== 1`：不创建 `entrySync`，直接把该目标作为 `ctrlEntry`。

若目标集合为空（典型于仅 DATA/PORT 直达值出口锚点的子图），先注入隐式目标 `valueExit`，
再按上述统一规则收敛出唯一 `ctrlEntry`。

### 7.2 以 `valueExit/GATE` 为准的控制完成契约（无 `exitSync` 汇聚）

当前实现将控制完成锚点统一定义为 `valueExit`。当“值出口”与“控制完成点”分离时，
会先在目标图内部插入 `GATE`（`Norm+Ctrl`），再将该 `GATE` 作为新的 `valueExit` 导出。

验证器约束由“`EXIT.ctrlInputs` 上限”迁移为 `GATE` 结构约束（`Norm>=1 && Ctrl>=1`）。

---

## 8. 第五阶段：预算哨兵与后处理

### 8.1 三类预算

每个 SCC 内有三道保护：

- `kInlineTargetBudgetPerScc`：初始目标数上限；
- `kInlineApplyBudgetPerScc`：实际应用次数上限；
- `kNodeDeltaBudgetPerScc`：节点净增长上限。

策略是：

- Debug：触发即 `ASSERT`；
- Release：记录告警并跳过/停止当前 SCC。

### 8.2 slot-safe 不可达裁剪

`pruneUnreachableSlotSafe` 不直接全删不可达，而是：

1. 从 `exitNode`（值出口锚点）反向求 live 集；
2. 对 `BRCH/JOIN` 槽位相关节点做 pin；
3. 仅删除“不可达且非 pinned”节点。

原因：`BRCH/JOIN` 的 arm 槽位顺序携带语义，过度 detach/unlink 可能打乱 slot 对齐。

### 8.3 图结构自检

每个改动图经 `GraphBuilder::validateGraph` 校验；会话尾部再做邻接归属断言：

- `assertAdjacencyPointersBelongToGraph` 检查节点邻接指针均属于当前图活跃节点集合；
- 防止出现悬挂邻接或跨图脏指针。

### 8.4 use-site 驱动 registry 清理

在 `session.finish()` 前，inline pass 会执行一轮图级 sweep：

- mark：基于 `FUNC.bodyGraph` + `DATA(Function).graph` 的真实 use-site；
- sweep：删除 owner 上未被引用的 `dependencies` / `subGraphs`。

该步骤用于清理内联后产生的“一次性子图残留注册”。

---

## 9. 涉及的 Graph API 分层图

### 9.1 Pass 层（`InlineRewritePass`）

- 只表达“要做什么”：
  - 收集目标（**仅限** §6.1 所述 `BRCH` 臂首 `FUNC`，见 §1.1）
  - 调 `session.inlineCallable`
  - 替换/删除节点
  - 修补 BRCH/JOIN 连接

### 9.2 Session 层（`GraphRewriteSession`）

- 事务包装：
  - `eraseNode / replaceInput / replaceOutput / inlineCallable`
  - `importSubGraph / importDependency / eraseDependency / eraseSubGraph`
  - `finish` 决定是否 seal 输出

### 9.3 Draft 层（`GraphDraft`）

- 所有权与可写域控制：
  - 默认仅 root owned
  - 非 owned 图必须 import/clone 才能改
- 把 Session 调用落到 `GraphBuilder` 和 `NodeMutation`。

### 9.4 Builder/Mutation 层（`GraphBuilder` + `detail::NodeMutation`）

- 真正执行节点克隆、连边替换、内联展开、布局与封印。
- 其中 `detail::NodeMutation` 仅作为内部桥接层，不属于对外稳定 API。

---

## 10. 当前实现的关键不变量（建议代码评审必查）

1. **No legacy refs in draft**  
   seal 前，`FUNC.bodyGraph` 与 `DATA(Function).graph` 不得再指向 source 图。

2. **BRCH/JOIN arm 对齐不破坏**  
   内联后裁剪必须 slot-safe；不要用会重排 arm 向量的粗暴清理。

3. **跨图改写先 import/clone**  
   不允许直接改 non-owned graph。

4. **InlineResult 合法性**  
   `valueExit` 必须始终可用（必要时为内部门控后的 `GATE`）；并且 `GATE` 结构必须满足 `Norm>=1 && Ctrl>=1`。

5. **会话收口一致**  
   仅在 `changed=true` 时 seal；否则返回 source root，避免伪改写产物。

---

## 11. 已知工程取舍

- **小子图阈值是启发式常量**：当前以“非 `DATA/PORT` 节点数 `<=8`”定义小子图，优点是简单稳定；缺点是缺少调用频次/热度参与，收益上限受限。
- **单轮不追新增 FUNC**：牺牲局部激进展开，换取可收敛与可解释性。
- **入口门控优先策略**：优先将控制入口对齐到参数门控，再在无参数门控时回退到入口根推导。
- **预算在 Release 仅保护性中断**：偏“不中断主流程”的线上策略，定位依赖日志。

---

## 12. 建议的后续演进方向

1. 细化 `InlineSyncPolicy` 的行为边界（尤其是 `Never` 在多入口目标下的容错策略）。
2. 引入“目标收益估计”（调用频次/节点增长率）驱动 selective inline，替代纯节点数阈值。
3. 将 slot-safe prune 的 pin 规则抽象为可复用 API，供其他 rewrite pass 共享。
4. 对 `InlineResult` 增加结构化统计（克隆节点数、导入依赖数、叶数）以提升可观测性。

