# 源码映射与调试定位

本文档说明 Camel 当前这套源码映射系统是如何把源码位置从 `parse -> AST -> GCT -> GIR -> bytecode/runtime/debugger` 贯通起来的，以及在 AST 到 GCT/GIR 的有损转换前提下，GCT/GIR 节点的源码区间到底是如何确定的。

## 1. 为什么需要独立的源码映射系统

在旧实现里，源码位置主要停留在前端：

- parser/token 可以提供 line/column。
- AST 节点保存 `TokenRange`。
- diagnostics 在最终输出前，把 `TokenRange` 转回字符行列。

这条链只能覆盖前端和一部分语义诊断。一旦进入 GCT、GIR、bytecode 或 runtime，位置就会迅速丢失，debugger 也只能停留在 `graphId/nodeId` 视角，而不是源码级视角。

所以现在把位置系统独立成了 `SourceContext`，核心组件在：

- `include/camel/core/source/ids.h`
- `include/camel/core/source/manager.h`
- `src/core/source/manager.cpp`

## 2. 当前实现的核心对象

### 2.1 SourceManager

`SourceManager` 负责注册源文件文本，并保存：

- `SourceFileId`
- 文件原文
- `lineStarts`

因此所有位置内部统一先表示成 offset，再在需要输出给用户时转换成 `line/column`。

### 2.2 SpanArena

`SpanArena` 只表达“源码中的哪一段”，也就是：

- `SpanId -> { fileId, startOffset, endOffset }`

它不关心这段源码在编译链中代表什么语义。

### 2.3 OriginTable

`OriginTable` 负责表达“当前实体是从哪段源码、哪个上游实体派生而来”的语义链：

- `OriginId -> OriginRecord`
- `OriginRecord.primarySpan`
- `OriginRecord.parent`
- `OriginRecord.stage`
- `OriginRecord.kind`
- `OriginRecord.inputs`

这里最重要的是：

- `primarySpan` 是当前实体对外展示时使用的主源码区间。
- `parent` 表示主派生链。
- `inputs` 用于记录多源输入，但当前实现对外展示时仍优先使用 `primarySpan`。

### 2.4 DebugMap

`DebugMap` 是 debugger/运行时的快速索引层，不直接存源码区间，只存：

- `graph stableId -> OriginId`
- `node stableId -> OriginId`
- `bytecode pc -> OriginId`

这样 debugger 和 runtime 可以先定位到执行实体，再反查 `OriginId -> primarySpan -> line/column`。

### 2.5 Semantic Side Tables

在 `primarySpan` 之外，`SourceContext` 现在还维护三层语义 side table：

- AST 语义锚点表
- GCT 语义来源表
- GIR graph/node 语义来源表

它们统一使用：

- `SemanticRole`
- `SemanticPart`
- `SemanticBundle`

来表达：

- `mainOrigin`：稳定 fallback
- `semanticParts`：`operator / callee / argument / branchCondition / branchTarget / valueProducer ...`
- `mergedInputs`：多源聚合来源

也就是说，当前系统已经不再只有“单个 origin -> 单个主区间”，而是开始具备“主区间 + 命名语义部件 + 多源输入”的扩展层。

## 3. 各阶段如何建立映射

## 3.1 Parse / AST

入口在：

- `include/camel/parse/parse.h`
- `src/parse/ast/builder.cpp`

当前流程是：

1. `CamelParser::parse()` 先把完整源文件注册到 `SourceContext`。
2. AST Builder 在 `setNodeTokenRangeByContext(...)` 里从 `ParserRuleContext` 取：
   - `start->getStartIndex()`
   - `stop->getStopIndex() + 1`
3. 用这对 offset 创建 `SpanId`。
4. 再创建 `OriginId(stage=AST, kind=AstNode)` 并挂到 AST `Load.origin_` 上。

所以 AST 阶段拥有最接近源码的、最精确的主区间。

## 3.2 AST -> GCT

入口在：

- `include/camel/compile/gct/load.h`
- `src/compile/gct/builder.cpp`

当前实现不再把 `tokenStart_/tokenEnd_` 传播到 GCT，而是直接传播 `OriginId`：

- GCT `Load` 持有 `origin_`
- `setOriginFromAst(...)` 调用 `SourceContext::deriveOrigin(astOrigin, stage=GCT, kind=...)`

关键点在于：

- 如果没有显式指定新的 `SpanId`，`deriveOrigin(...)` 会继承父节点的 `primarySpan`。
- 这意味着 GCT 的“实际源码区间”默认等于它所来源的 AST 节点主区间。

也就是说，当前 GCT 节点的源码区间不是重新计算出来的，而是从负责该 lowering 的 AST 节点继承而来。

## 3.3 GCT -> GIR

入口在：

- `src/compile/gir/builder.cpp`

当前实现里：

- `deriveGirOrigin(...)` 从 `gct->load()->origin()` 继续派生 `OriginId(stage=GIR, kind=...)`
- `registerGraphOrigin(...)` 把 graph stableId 映射到 origin
- `registerNodeOrigin(...)` 把 node stableId 映射到 origin

这一步同样默认继承上游 `primarySpan`，因此 GIR 节点最终显示给 debugger 的源码区间，本质上仍是它上游 GCT 节点的主区间。

## 3.4 GIR -> Bytecode / Runtime

入口在：

- `src/passes/sched/fastvm/compile.cpp`
- `src/passes/sched/fastvm/gotovm.cpp`
- `src/passes/sched/fastvm/casevm.cpp`
- `src/passes/sched/nodevm/nodevm.cpp`

当前做法是：

- FastVM 编译 bytecode 时，把 `node stableId -> OriginId` 转成 `pc -> OriginId`
- FastVM / NodeVM 执行时，把当前 `pc` 或 `node stableId` 写入 `SourceContext.currentRuntimeOrigin`
- `DiagnosticBuilder::commit(...)` 若没有显式 `.at(...)`，会回退使用当前 runtime origin

因此运行时错误现在也可以自动落到当前执行点的源码来源上。

## 4. 有损转换下，GCT/GIR 的源码区间怎么定

这是这套方案里最关键的设计取舍。

从源码到 GIR 的转换本来就是有损的，例如：

- 一个 AST 表达式会被拆成多个 GCT/GIR 节点
- 一个语法糖会被改写成更底层的调用/控制流
- 一个 GIR 节点可能只是某个 AST 节点语义的一部分

当前实现采用的是“主责任区间继承”策略，而不是“重建精确逆映射”。

### 4.1 当前规则

1. AST 节点先拿到精确 `primarySpan`
2. GCT 节点从生成它的 AST 节点 `deriveOrigin(...)`
3. GIR 节点从生成它的 GCT 节点 `deriveOrigin(...)`
4. 若没有显式新 span，则自动继承父 origin 的 `primarySpan`

因此：

- 一个 AST 节点展开成多个 GCT/GIR 节点时，这些节点通常共享同一个源码区间
- 一个运行时 PC 也会回到某个“主责任 AST 区间”

### 4.2 这意味着什么

这套规则保证了两件事：

- diagnostics/debugger 始终能稳定回到“最该负责的那段源码”
- 不需要在每一步 lowering 时重建复杂而脆弱的多源精确映射

但它也带来一个明确限制：

- 它返回的是“主责任区间”，不是“最细粒度的逆变换精确区间”

举例：

- `a + b` 在 GCT/GIR 中可能拆成 `DREF(__add__)`、参数节点、`CALL`
- 当前这些节点大概率都回到同一个上游表达式区间，也就是整段 `a + b`
- 而不会自动细分成只覆盖 `a`、只覆盖 `+`、只覆盖 `b`

### 4.3 多源节点怎么处理

当前 `OriginRecord.inputs` 已经预留了多源输入能力，但当前对外展示时：

- `resolveOrigin(originId)` 仍然只返回 `primarySpan`

所以目前多源节点的策略是：

- 用 `primarySpan` 表示主责任源码区间
- 用 `inputs` 保留附加来源
- debugger/diagnostics 先只展示主区间

这对当前实现足够稳定，但如果未来要做更强的源码级单步，建议进一步增加：

- `Combined` origin 的可视化策略
- 多区间高亮
- “主区间 + 附加来源”展示

## 5. 当前代码中的实际落点

如果想直接看实现，建议从这些文件开始：

- `include/camel/core/source/manager.h`
- `src/core/source/manager.cpp`
- `include/camel/parse/parse.h`
- `include/camel/parse/ast/base.h`
- `src/parse/ast/builder.cpp`
- `include/camel/compile/gct/load.h`
- `src/compile/gct/builder.cpp`
- `src/compile/gir/builder.cpp`
- `src/passes/sched/fastvm/compile.cpp`
- `tools/debugger/src/gir_json.cpp`

## 6. 当前实现的结论

当前这套实现已经做到：

- AST 有精确源码区间
- GCT/GIR 有稳定 `OriginId`
- debugger 可从 GIR graph/node 回到源码区间
- runtime diagnostics 可从当前执行点回到源码区间
- debugger JSON 可进一步读取 `origin + semantic + edge semanticRole`

但也要明确：

- GCT/GIR 的源码区间目前是“继承来的主区间”
- 不是对每个 lowering 结果做完全精确的逆映射
- 多源映射能力已经从底层结构推进到 AST/GCT/GIR 语义 side table，但展示和单步语义还可以继续加强

这正是“彻底重构”与“稳定可用”之间目前选择的平衡点：先统一整条来源链，再逐步把多源与细粒度定位做得更精确。

更细的节点语义、层间 lowering、基础缺口审计与后续路线，见：

- `docs/technical/11_semantic_models_and_reverse_mapping_foundation.md`
