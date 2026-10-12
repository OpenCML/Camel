# Graph 重写 API 关键约束

本文聚焦 **Graph 重写 API**（不是 GIR 概念本身），用于约束 pass/工具代码在改写图时的行为边界与正确性契约。

覆盖对象：

- `RuntimeGraphDraftSession`
- `GraphDraft`
- `inlineCallableInDraft` / `inlineCallInOrder`
- `GraphBuilder`
- `detail::NodeMutation`（内部桥接）/ `Node::link|replace|unlink`

---

## 1. 分层职责与边界

### 1.1 `RuntimeGraphDraftSession`（pass 视角）

- 负责事务入口与统一收口（`include/camel/runtime/draft_session.h`）；
- `commit()` 重新编码可达闭包并替换运行时根图；没有改动时 pass 不调用 `commit()`，沿用原图，不导出伪改写图；
- pass 不应绕过 session 直接改运行时图（`GCGraph`）。

### 1.2 `GraphDraft`（可写域与所有权）

- 默认仅 root draft-owned；
- 非 owned 图（子图/依赖图）必须先 `import*` 或 `cloneIntoDraft`；
- 对非 owned 图写操作属于违反契约。

### 1.3 `GraphBuilder`（底层原语）

- 负责真正的结构编辑（增删点边、内联、布局、seal）；
- `sealGraph*` 是单向终结路径；sealed 图不得再结构修改。

---

## 2. 写操作硬约束

### 2.1 图归属一致

- `Node::link` / `replaceInput` / `replaceOutput` 要求两端节点同图；
- 禁止跨图直接连边。

### 2.2 sealed 不可变

- 任意节点编辑前都要求图处于 draft（`!finalized()`）；
- sealed 状态只允许只读访问。

### 2.3 依赖与子图注册一致

- 改写 `FUNC.bodyGraph` / `DATA(Function).graph` 后，必须同步 dependency；
- 不允许留下“节点引用了图，但 registry 未登记”的状态。
- 自递归引用（owner 引用 owner 自身）也必须显式触发 dependency 注册逻辑，
  以正确设置 `looped=true`。
- 在会话收口前建议执行一次“use-site 驱动 sweep”：
  - mark：按 `FUNC.bodyGraph` 与 `DATA(Function).graph` 统计 owner 的真实引用集合；
  - sweep：删除 owner 上未被真实引用的 `dependencies` 与 `subGraphs` 条目。

### 2.4 词法子图所有权约束

- `subGraphs` 表示词法嵌套关系，重建时必须满足 `sub->outer == owner`；
- 仅“被引用”不等于“是子图”：别名引用应保留在 dependency，不应写入 subGraphs；
- 否则会出现图层级污染（例如顶层函数被错误挂到 arm 子图下）。

### 2.5 clone/canonical 化顺序约束

- 对 source 图 `G` 做 `cloneIntoDraft` 时，应先登记 `draftBySource_[G] = draftG`；
- 然后先修正 `draftG` 内部对“已知 draft”的引用，再执行全 owned 域重定向；
- 这个顺序可避免跨图引用遗漏，保证会话内 graph identity 单一（source 不与 draft 混用）。

#### 2.5.1 两阶段修正的职责边界

`GraphDraft` 当前使用两类重定向函数，职责不能混淆：

- `retargetKnownDraftRefsInOwner(ownerDraft)`：**局部修正**  
  仅修正“新克隆图自身”里指向已知 source 的引用（`FUNC.bodyGraph`、`DATA(Function).graph`、
  dependencies/subGraphs）。
- `retargetOwnedGraphRefs(source, draft)`：**全局修正**  
  扫描当前会话所有 owned 图，把仍指向该 `source` 的引用替换为 `draft`。

工程上必须两者同时存在：前者兜住“新图内部残留”，后者兜住“历史图域残留”。

#### 2.5.2 失败模式（为什么这不是可选优化）

若跳过上述任一步，会出现典型不一致：

- source/draft 指针混用（同一语义图出现双身份）；
- dependency 注册与节点实际引用脱节；
- subgraph 词法层级被污染（错挂到错误 outer）；
- 递归图 `looped` 语义未被正确触发。

因此该流程是 rewrite correctness contract，而不是针对某个单测的临时绕过。

---

## 3. `inlineCallableInDraft` 契约（当前实现）

**与 `std::inline` pass 的区分**（阅读本节时请先建立预期）：

- **`inlineCallableInDraft`**（`include/camel/runtime/draft_inline.h`）：**通用**内联原语；只要调用方传入合法的 `FUNC` 节点，即可展开其被调图（`GCFuncBody::calleeGraph`）。需要保持语句次序的调用方使用其包装 `inlineCallInOrder`（`include/camel/passes/opt/inline.h`）。
- **`std::inline`（`InlineRewritePass`）**：按策略选择目标并经 `inlineCallInOrder` 调用该原语：
  - `std::inline::small`：小子图且非分支 arm；
  - `std::inline::arm`：分支 arm 小子图；
  - `std::inline::hybrid` / `std::inline`：综合策略；
  - 默认还启用“SCC 入口 callee 保护”：若调用目标是该 SCC 的入口 callee（被 SCC 外调用），则不内联；
  见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md) §1.1 与 §6.1。

因此：文档与本节描述的是 **`inlineCallableInDraft` 的契约**；**不要**把它自动等同于「整个编译管线会对所有 `FUNC` 调用做内联」。

### 3.1 输入前提

- 目标节点必须是当前 draft 中的 `FUNC`；
- 其被调图（`GCFuncBody::calleeGraph`）非空且有值出口锚点（`exitNode`）。

### 3.2 输出契约（`DraftInlineResult`）

- `callNode`：被内联的 `FUNC` 节点；
- `valueExit`：内联后值出口（必要时为内部门控后的 `GATE`）；
- `ctrlEntry`：内联后控制入口；
- `ctrlExit`：内联后控制出口（没有独立的控制完成点时等于 `valueExit`）。

前提不满足时返回的结果中 `valueExit` 无效（结果按布尔值判断为假）。

### 3.3 入口控制生成规则

1. 优先基于参数映射生成参数门控目标（`GATE`）；
2. 无参数门控时，从 `exitNode`（值出口锚点）反向可达区域推导入口根；
3. 入口目标数：
   - `>1`：创建 `entrySync` 并扇出；
   - `=1`：直接以该目标作为 `ctrlEntry`（不建 `SYNC`）。

若入口目标集合为空（例如子图只有 DATA/PORT 到值出口锚点的路径），先将 `valueExit` 作为
隐式入口目标，再按上述规则收敛，保证最终只返回一个 `ctrlEntry`。

### 3.4 控制完成语义

- 控制完成锚点统一使用 `valueExit`。
- 若值与控制完成点分离，先在目标图内部插入 `GATE`，再导出该 `GATE` 作为 `valueExit`。
- 验证器保证 `GATE` 满足 `Norm>=1 && Ctrl>=1`。
- 因此不再使用 `exitSync` 兜底汇聚。

---

## 4. 验证器约束（提交时必须满足）

结构检查在**所有构建模式**下执行（不是 `ASSERT`），位置是运行时图编码之后：前端编码
（`src/compile/gir/graph_encode.cpp`）、每次 `RuntimeGraphDraftSession::commit()` 与
`GraphDraftGroup::encode()`（`src/runtime/draft_session.cpp`）。实现为
`validateRuntimeGraphPayload` 与 `validateRuntimeGraphCalls`（`src/runtime/graph.cpp`），失败时抛出
`std::runtime_error`；在 pass 内由 pass 驱动转为 `RuntimeDiag::PassInternalError`，诊断中给出 pass 名，
程序不会开始执行。

`validateRuntimeGraphPayload`（单图）：

1. 入口/出口/输出/返回锚点、端口、闭包节点、所有边的节点引用有效；数据槽位在范围内；无残留 `DREF`；
2. 邻接表双向一致（每条输入边在对端的输出表中出现，反之亦然）；
3. `BRCH` 有唯一选择输入，`BRCH`/`JOIN` 的 arm 数与负载一致，arm 首尾引用有效；
4. `GATE` 同时有值输入与控制输入（没有控制输入的 `GATE` 在提交前由 `dissolveUnorderedGates` 消除）；
5. `FUNC` 有被调图，`CALL` 有被调输入。

`validateRuntimeGraphCalls`（同一次提交的图全部编码之后）：

6. 每个 `FUNC` 的普通实参数与 `with` 实参数等于被调图的端口数。

`src/compile/gir/validate.cpp` 中的 `assertGraphSealingPreconditions` /
`assertGraphTreeSealingPreconditions` 是旧的 seal 前检查（以 `ASSERT` 实现）；自编译期重写 API 移除后
已没有任何调用点，在任何构建模式下都不执行。上面的检查取代了它。

测试用的故障注入遍 `test::drop_call_argument`（`src/passes/test/fault.cpp`）只在设置环境变量
`CAMEL_ENABLE_TEST_PASSES` 时可用，用来验证破坏结构的图会在提交时被拒绝。

---

## 5. 对 pass 作者的建议

- 优先通过 `RuntimeGraphDraftSession` 及其 `GraphDraft` 完成改写，不直接改运行时图；
- 不要假设“值出口等于控制完成出口”，统一使用 API 返回值；
- 涉及 BRCH/JOIN 时，谨慎处理 arm-slot 语义，配合 slot-safe 裁剪；
- 对会生成一次性子图的优化（如 BRCH arm 内联），在 pass 尾部做 registry sweep，
  避免“无引用图仍挂在依赖/子图表”。
- 新增 rewrite pass 时，先写“契约断言”，再写优化逻辑。

