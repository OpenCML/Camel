# `std::macro` Rewrite Refactor Plan

本文记录 `std::macro` 的系统性重构方案。目标不是“修复某一个宏样例”，而是把它重新纳入当前的 Graph Rewrite API 与运行时执行模型，使其成为一个长期稳定的图重写遍实现。

---

## 1. Repositioning

`std::macro` 的定位需要明确为：

- 它是一个 `GraphRewritePass`
- 它不是单独的“编译期宏子系统”
- 它也不是新的 pass 分类
- 它是一个 **基于运行时执行语义的图重写遍**

换句话说，`std::macro` 与 `std::inline` 一样属于 rewrite pass；与 `NodeVM` 的关系则是：

- `NodeVM` 消费图并产生运行结果
- `std::macro` 借用相同的图执行语义，对满足条件的子图做求值，然后把结果重新物化回 GIR

因此，`std::macro` 不应继续维护一套独立的小型 VM 与独立的图域管理逻辑。

---

## 2. Current Structural Problem

当前 `std::macro` 的失配不是单点 bug，而是结构性问题。它在同一个文件里混合了四类职责：

1. source graph tree 的遍历
2. draft graph 的 materialization 与 ownership 管理
3. 图执行语义解释
4. 执行结果向 GIR 的回填与替换

这四类职责里，只有第 4 类应保留在 `std::macro` 本身。前 3 类都应下沉为公共能力。

当前问题的根因可以概括为两条：

1. `GraphRewriteSession` 只提供局部编辑 primitive，但没有提供 reachability-aware 的 editable workspace 视图。
2. `std::macro` 与 `NodeVM` 共享大量执行语义，却没有共享同一个执行内核。

结果就是：

- `inline` 这类较新的 pass 已经围绕现有 rewrite API 发展出一套额外的“按需 materialize 全图树”的协议
- `macro` 还停留在旧实现方式，直接遍历 source graph tree，并假设它们都可编辑
- 一旦底层 rewrite API 变化，`macro` 就会系统性掉队

---

## 3. Fundamental Principles

### 3.1 Separate traversal domain from edit domain

pass 可以从 source root 收集可达图，但一旦进入 rewrite 阶段，就只能操作 draft-owned graph。

这条原则必须由 API 保障，而不是由 pass 作者手工维护。

### 3.2 Keep graph materialization out of individual passes

复杂 rewrite pass 不应各自维护一套 `ensureEditableGraph(...)`。

可达图树的 materialization、source->draft 映射、canonical graph 查询，应该是 rewrite framework 的职责。

### 3.3 Keep execution semantics single-sourced

CAST / COPY / FILL / ACCS / BRCH / JOIN / CALL / FUNC / OPER 这些节点的解释语义，不应在 `NodeVM` 与 `std::macro` 中各维护一份。

共享的是“怎么执行图”，差异只是：

- 是否允许尝试执行某个节点
- 执行失败时是 hard fail 还是 skip
- 执行结果如何回填图，而不是返回进程结果

### 3.4 Make value materialization a first-class API

`slot_t -> DATA node`、`Function* -> dependency registration`、macro flag 传播，这些不应散落在 pass 私有实现中。

这类“执行值重新物化为 GIR”的逻辑，应成为 rewrite workspace 的公共 API。

### 3.5 Keep pass contract minimal

`std::macro` 本身应只负责：

- 识别候选节点
- 判定是否允许尝试求值
- 调用共享执行服务
- 用返回值替换原节点

它不应再负责：

- source/draft 图域同步
- frame layout / FrameMeta 失效处理
- 子图 dependency 修补细节

### 3.6 Do not introduce a new pass taxonomy for this

`std::macro` 仍然是 `GraphRewritePass`。

需要重构的是：

- rewrite workspace
- shared execution service

而不是在 pass 继承层级上再发明一个新类别。

---

## 4. Refactor Target Architecture

### 4.1 Layer A: Rewrite workspace

在 `GraphRewriteSession` 之上补齐“整图树 rewrite workspace”能力。

建议新增或内聚的能力：

- `materializeReachableGraphs(root)`
- `draftOf(sourceGraph)`
- `sourceOf(draftGraph)`
- `forEachReachableDraftGraph(visitor)`
- `replaceNodeWithStaticValue(node, slot, type)`
- `registerFunctionValueDependency(owner, functionGraph)`

这层的职责是：

- 建立 source->draft 映射
- 保证后续 pass 看到的是稳定的 draft graph tree
- 在 `finish()` 中统一完成 seal / layout / cache invalidation

这一层落地后，`inline` 里私有的 `ensureEditableGraph(...)` 应被下沉并复用。

### 4.2 Layer B: Shared graph execution service

抽取一套共享图执行内核，供 `NodeVM` 与 `std::macro` 共用。

建议职责拆分为：

- `GraphExecutionCore`
  - topo traversal
  - frame filling
  - CALL / FUNC / OPER / BRCH / JOIN 等节点解释
  - recursion / closure / frame pool 管理
- `ExecutionPolicy`
  - 控制哪些节点允许被解释
  - 控制失败是 skip、diagnostic 还是 fail-fast
  - 控制结果如何暴露给调用者

这里不改变 pass 分类，只改变底层服务组织方式。

### 4.3 Layer C: Macro-specific rewrite logic

`std::macro` 本身仅保留三类逻辑：

1. 候选筛选
2. eligibility 判定
3. 替换策略

例如：

- direct `FUNC` 是否满足静态参数要求
- indirect `CALL` 的 callee 是否为静态 `Function*`
- 宏结果是否能安全物化为 `DATA`

这层不再持有自己的 graph materialization 协议，也不再维护独立的执行主循环。

---

## 5. API Changes Needed

### 5.1 Changes to rewrite API

当前 `GraphRewriteSession` 更像事务包装器，还不是完整 workspace。需要补的不是更多 node primitive，而是 graph-domain API。

建议新增：

- reachable graph materialization API
- source/draft identity query API
- graph tree iteration API
- static value materialization API

目标是让 rewrite pass 可以写成：

1. 构建 workspace
2. 遍历 draft graph tree
3. 对候选节点执行替换
4. finish

而不是先自己处理 graph ownership，再进入 rewrite。

### 5.2 Changes to execution API

当前 `NodeVM` 的核心逻辑直接嵌在 schedule pass 里，可复用性太弱。

需要把以下能力从 `NodeVMSchedPass` 中拆出：

- 单图解释主循环
- frame 参数装配
- 节点级 dispatch
- CALL / FUNC 递归管理
- topo cache 管理

`NodeVM` 保留“消费图并产出 exit code”的外壳；`std::macro` 则复用执行核心，但把结果送回 rewrite workspace。

### 5.3 Changes to materialization API

建议给 rewrite workspace 一个显式接口来完成：

- `slot_t` 到静态 `DATA` 节点的转换
- `Function*` 结果的 dependency / subgraph registration
- 节点属性传播，例如 macro flag

避免每个 execution-driven rewrite pass 自己手写这套规则。

---

## 6. Non-Goals

本轮重构不做以下事情：

1. 不重新定义语言层“macro”的语义
2. 不把 `std::macro` 改造成默认 pipeline
3. 不发明新的 pass 基类
4. 不先追求“宏可调用函数参数”这类边界场景的最终语义扩展

本轮只解决框架层正确性与可维护性。

---

## 7. Confirmed Constraint

在本轮重构准备阶段已确认一个重要约束：

- 当前 `std::macro` 执行器要求目标 graph 已 `finalized()`
- 并且必须已经安装 frame layout
- 当前 rewrite draft graph 不满足这个前提

这意味着：

- `std::macro` 不能直接照搬 `std::inline` 的“先切换到 draft graph，再在 draft graph 上执行”的模式
- 在共享执行内核或 source/draft node correspondence 补齐之前，`macro` 仍然只能基于 finalized graph 求值

因此后续迁移时必须显式区分：

1. **在哪个 graph identity 上执行**
2. **在哪个 graph identity 上替换**

如果这层对应关系不先补齐，直接把 `macro` 迁到 draft graph 上会破坏其运行时前提。

---

## 8. Migration Plan

### Stage 1: Extract shared rewrite workspace

目标：

- 把 `inline` 当前私有的 graph materialization 机制沉到公共层
- 让 `std::macro` 与 `std::inline` 使用同一套 editable graph tree API

验收标准：

- 任一复杂 rewrite pass 不再私有维护 `ensureEditableGraph(...)`
- pass 不再直接在 source graph tree 上做结构推断后就尝试写回

### Stage 2: Extract shared execution core

目标：

- 把 `NodeVM` 中可共享的图执行主循环抽出
- 让 `std::macro` 调用同一套执行语义，而不是维护副本

验收标准：

- `std::macro` 与 `NodeVM` 不再拥有两份大规模 switch-dispatch 逻辑
- 节点语义修改时，至少核心路径只需改一个地方

### Stage 3: Rebuild `std::macro` on top of both abstractions

目标：

- 重写 `src/execute/macro/macro.cpp`
- 它只保留 macro-specific eligibility 与 replacement 逻辑

验收标准：

- `std::macro` 不再自己管理 source/draft graph mapping
- `std::macro` 不再自己维护独立 topo executor
- `std::macro std::gir` 能反映真实 rewrite 后的图

### Stage 4: Validation and regression suite

目标：

- 补齐 `test/macro/*.cml` 的回归验证
- 至少覆盖 direct FUNC、indirect CALL、chain、branch、functor、function arg skip 等场景

验收标准：

- `basic.cml`
- `chain.cml`
- `deep_chain.cml`
- `branch_chain.cml`
- `pipeline.cml`
- `functor.cml`
- `function_arg.cml`

上述样例在 `std::macro` 下行为稳定，且 `std::macro std::gir` 输出可检验。

---

## 9. Acceptance Criteria

重构完成后，应满足以下标准：

1. `std::macro` 仍然是 `GraphRewritePass`
2. `std::macro` 不再手工管理 source/draft 图域同步
3. `std::macro` 与 `NodeVM` 共享统一执行语义核心
4. 重写后的 graph sealing / layout / cache invalidation 不再由 `std::macro` 手工兜底
5. `inline` 与 `macro` 使用同一套 rewrite workspace，而不是各自维护图域协议
6. 后续若 rewrite API 再重构，`std::macro` 的适配面应仅限其自身 policy 与 replacement 逻辑，而非整套底层设施

---

## 10. File-Level Impact Forecast

预计主要影响文件：

- `include/camel/compile/gir/rewrite.h`
- `include/camel/compile/gir/draft.h`
- `src/compile/gir/draft.cpp`
- `src/passes/opt/inline/inline.cpp`
- `src/passes/sched/nodevm/nodevm.cpp`
- `src/execute/macro/macro.cpp`
- 可能新增一个共享执行服务目录，例如 `src/execute/graph_eval/`

如果最终实现过程中发现 `NodeVM` 与 `macro` 的共享部分比预期更大，应优先继续下沉服务层，而不是把新逻辑重新塞回 `macro.cpp`。
