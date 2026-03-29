# 图中间表示（GIR）详解

## 1. 概述

**GIR（Graph IR）** 是 Camel 程序在编译期的核心表示，定义在 `include/camel/compile/gir.h`（命名空间 `camel::compile::gir`，常用别名 `GIR`）。所有「可执行逻辑」都以**有向图**形式存在：节点表示数据、操作或控制，边表示数据依赖或控制依赖。

## 2. 图（Graph）

### 2.1 角色与属性

- 一个 **Graph** 对应一个函数体（或顶层模块的可执行部分）。
- **name_ / stableId_**：图名与稳定标识；`stableId_` 用于跨 clone / rewrite 追踪图来源。
- **outer_**：外层图（闭包/嵌套函数时非空）。
- **signature_**：图的签名与布局摘要，内部包含 `funcType`、`staticDataType`、`runtimeDataType`、`closureType`、`runtimeDataSize`。
- **staticDataArr_**：静态数据区（`slot_t`），索引 0 保留为空；负 `data_idx_t` 通过它寻址。
- **nodes_**：图中所有节点（含 PORT、DATA、OPER、CALL、EXIT 等）。
- **normPorts_ / withPorts_ / closure_**：普通参数端口、with 参数端口、闭包捕获节点。
- **exitNode_**：图的唯一出口节点（EXIT）。
- **subGraphs_**：按名称索引的子图集合（嵌套函数）。
- **dependencies_ / dependents_**：图级依赖（被谁依赖 / 依赖谁），用于多图调度或内联等。
- **arena_**：图私有的 `GraphArena`。seal 后 frozen 邻接数组等只读数据分配在这里，并随 Graph 一起释放。
- **extras_**：图级 O(1) 扩展缓存槽位。Graph 本身不绑定具体 VM；各后端自行约定 index。

### 2.2 Draft / Sealing / Sealed 生命周期

当前 GIR 采用 builder staging + seal 单向物化：

1. **Draft 阶段**
   - 图通过 `GraphBuilder` 或 `GraphDraft` 被构造/编辑。
   - Node 的邻接关系保存在 `std::vector<Node *>` 中，便于增删改边。
   - 图允许增删节点、替换边、内联、调整依赖等结构性修改。

2. **Sealing 阶段（唯一物化入口）**
   - 通过 `GraphBuilder::sealGraph()` 或 `sealGraphRecursively()` 完成。
   - 内部经 `finalize()`：若图已 `Sealed`，则直接返回（幂等）；否则按固定流水执行：
     `rearrange` → `promoteNodeDebugIds` → `freezeAdjacency` → `packStaticSlotsToFrozen`
     → `installFinalFrameLayout` → `releaseDraftRegion`。
   - `sealGraph()` 负责调用 `finalize()` 并执行 builder 的 **consume 语义**（`graph_` 置空）。

3. **Sealed 阶段**
   - `Graph::finalized()` 为 `true`。
   - 节点只允许只读访问，不再允许结构编辑。
   - 如需改写，必须先 `cloneGraph()` 得到新的 draft 副本。

### 2.3 数据索引约定

- **data_idx_t**：有符号整数；**正数**表示运行时槽位（动态区），**0** 表示空，**负数**表示静态区索引的相反数（见注释）。
- **addStaticData / addRuntimeData**：向图注册静态/运行时槽位并返回索引。
- **getStaticDataSlot / setStaticData**：按索引读静态槽位；构图期可由 `setStaticData` 用编译期数据重写槽位。

### 2.4 常用方法

- `GraphBuilder::createGraph(funcType, outer, name)`：创建 draft 图。
- `GraphBuilder::cloneGraph(graph)`：克隆图，产出新的 draft 图。
- `ports()` / `normPorts()` / `withPorts()` / `closure()`：端口与闭包节点。
- `setOutput(node)` / `exitNode()` / `outputNode()`：设置与获取出口。
- `GraphBuilder::parametrizeClosure()`：将闭包捕获转为参数节点。
- `GraphBuilder::sealGraph()`：封印单图。
- `GraphBuilder::sealGraphRecursively()`：递归封印图树。
- `computeLayout()` / `applyLayout()`：布局纯计算与写回分离，供 seal/export 路径复用。
- **`LayoutResult`**（`builder.h`）：`computeLayout()` 的只读汇总结果——节点与槽位映射、`staticDataArr`、各 `TupleType` 与 `runtimeDataSize` 等；由 `applyLayout()` 一次性写回 Graph，与 seal/export 解耦。

## 3. 边类型（LinkType）

- **Norm**：普通数据边（主数据流）。
- **With**：with 参数边（如上下文、配置等命名参数）。
- **Ctrl**：控制边（保证顺序、分支汇合等）。

节点间通过 `Node::link(LinkType, from, to)` 连接。

对外只读访问统一使用：

- `normInputs()` / `withInputs()` / `ctrlInputs()`
- `normOutputs()` / `withOutputs()` / `ctrlOutputs()`

这些接口返回 `node_span_t`。在 draft 节点上它们视图到内部 `vector`，在 frozen 节点上它们视图到 arena 中的定长数组。

## 4. 节点类型（NodeType）

| 类型   | 说明 |
|--------|------|
| DATA   | 编译期常量，存于静态区，节点自身通常持有负 `data_idx_t` |
| PORT   | 参数端口，占运行时槽位，有 name/isVar |
| CAST   | 运行时类型转换 |
| COPY   | 运行时数据拷贝 |
| FILL   | 运行时构造复合数据（如 struct/array） |
| ACCS   | 运行时下标/字段访问，索引为数字或字符串键 |
| BRCH   | 条件分支 |
| JOIN   | 分支汇合 |
| CALL   | 调用（闭包/函数值） |
| BIND   | 部分应用（绑定参数） |
| FUNC   | 直接子图调用节点，节点内缓存目标 `Graph*` 与运行时 `Function*` 原型 |
| OPER   | 原子算子，节点内持有裸 `OperatorIndex*` |
| EXIT   | 图出口，产出返回值 |
| SYNC   | 同步点（无私有数据） |
| GATE   | **数据与控制依赖的聚合门控节点**（无私有数据；见下节语义约定） |
| DREF   | 临时解引用节点（图构造用，通常不加入图） |

除上表一行摘要外，**§4.1–§4.9** 按类型补充**角色、典型 `Norm`/`With`/`Ctrl` 连边约定**及与调度后端的关系；命名与 `NodeType`（`include/camel/compile/gir/types.h`）、子类（`include/camel/compile/gir/nodes.h`）一致。

### 4.1 `GATE`：数据-控制聚合门控

在 GIR 中，`GATE` **特指**在同一点上把**值依赖**与**控制依赖**会合起来的门控节点，用来表达“在指定控制序完成之后再沿值边传播”的语义。

- **边约束（角色定义）**：作为上述门控使用时，`GATE` **至少应有一条 `Norm` 入边**（值来源）与 **至少一条 `Ctrl` 入边**（控制完成或控制序）。二者缺一则不再是该语义下的 `GATE`，而应视为构图错误或尚未完成的中间态。
- **放行语义**：调度/执行上可理解为：**仅当 `Ctrl` 侧所表达的控制序已满足时，才沿 `Norm` 边向下游“放行”该值**（具体实现由 VM 与算子约定；图上则体现为同时依赖两类边）。
- **典型场景**：子图出口上，若“值出口”与“控制完成点”不是同一节点（例如存在经 `println` 等再连到 `EXIT` 的副作用控制路径），则通过 `GATE` 将控制锚到值路径，供 `BRCH/JOIN` 臂或调用点统一消费；详见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md)。

### 4.2 `DATA` 与 `PORT`

- **`DATA`（常量 / 静态字面）**  
  - **语义**：承载**编译期已知**的标量或复合数据的图内表示；值物化在图的**静态区**（负 `data_idx_t` 寻址），不占用形式参数运行时槽位。  
  - **边**：通常以 **`Norm` 出边**把常量接到消费点；是否参与控制流取决于下游是否还有 `Ctrl` 依赖，节点本身不隐含控制副作用。  
  - **典型用途**：字面量、枚举判别值、已折叠的常量折叠结果等。

- **`PORT`（形式参数 / 运行时入口槽）**  
  - **语义**：表示当前子图的**形参**（或经布局分配的**运行时槽位**）；调用方在 enter 时把实参写入对应槽位。端口**名**与 `isVar` 等属性由 `Graph` 侧集中保存（见 §5.1）。  
  - **边**：多经 **`Norm` 边**向体内算子、分支条件等提供输入；也可与 `With`/`Ctrl` 组合，取决于具体 lowering。  
  - **与 `DATA` 的区别**：`DATA` 是静态只读槽；`PORT` 是每次调用实例化的**动态入口**。

### 4.3 `CAST`、`COPY`、`FILL`、`ACCS`

- **`CAST`**：对已有值做**运行时类型转换**（宽化/窄化、装箱视图等，以 `dataType` 与算子为准）。**`Norm` 入边**为待转换值，出边为转换结果；通常不自带控制语义，需与上游 `Ctrl` 组合时由调度保证序。

- **`COPY`**：显式**值拷贝**，用于需要打破共享、满足值语义或 ABI 要求的场景（如复合类型按副本传递）。入出以 **`Norm`** 为主。

- **`FILL`**：在运行时**构造复合值**（如 `struct` / `tuple` / `array` 等）的聚合节点；多个 **`Norm` 入边**对应各字段/元素来源，出边为合成后的单一值。

- **`ACCS`**：**下标或字段访问**。基对象经 **`Norm` 入边**接入；索引为**非负整数**（元组/数组下标）或**字符串键**（命名字段）——字符串键存于 `Graph::nodeAccsKeys_`（见 §5.1）。出边为投影得到的子值类型。

### 4.4 `BRCH` 与 `JOIN`

二者成对（或可验证地配对）表达**条件控制流 + 数据汇合**，与 FastVM 的 `BRCH`/`JOIN` 字节码一一对应。

- **`BRCH`（分支头）**  
  - **选择子**：**恰好一条 `Norm` 入边**，为判别值（如 `bool` 或 `match` 的 key）。  
  - **臂**：**多条 `Ctrl` 出边**，每条对应一个可执行臂的**控制入口**（臂数 = `ctrlOutputs().size()`）；`if` 为 2 臂，`match` 等为 `with` 臂数 + 1 等，由前端与 codegen 约定。  
  - **`With` 入边**：在 `match` 等多路语义中，可承载各 **case 模式 / arm 相关** 的附加数据（见 `BrchNode::caseInputs()`）；纯 `if` 时常为空。

- **`JOIN`（分支尾 / 汇合）**  
  - **与 `BRCH` 配对**：**第一条 `Norm` 入边**常接回 **`BRCH` 的选择子**或经布局约定的“臂索引”来源，用于运行时判定走了哪一臂。  
  - **各臂尾值**：**`With` 入边**按槽位与各臂一一对应（`armTail(i)`），仅**被执行臂**上的数据流会“活”到汇合点。  
  - **出边**：汇合后的**统一类型**值与控制流继续向下游传递。

### 4.5 `SYNC`

- **语义**：**纯控制汇聚点**，不承载业务载荷类型（实现上常为 `Void` 数据类型）；用于把**多条控制前驱**（例如多个参数门控、多个副作用完成点）收敛为**单一控制后继**，以便后续只挂一条 `Ctrl` 边。  
- **边**：**多条 `Ctrl` 入边**表示“这些序都到达后才继续”；出边可为 **`Ctrl` / `Norm`**，取决于后续是控制头还是已经与会合后的值相连。  
- **与 `GATE` 的区别**：`SYNC` **不要求**同时有 `Norm` 值入边；`GATE` **必须**同时有值与控制两门控语义（见 §4.1）。

### 4.6 `CALL` 与 `BIND`

- **`CALL`（间接调用）**  
  - **语义**：被调目标是**运行时函数值**（闭包），而非编译期写死在节点里的子图指针。  
  - **边约定**（与 `CallNode` 一致）：**`With` 的第一条入边**为 **callee**；其余 `With` 入边为 **with 实参**；**`Norm` 入边**为位置/普通实参。出边类型为被调函数的返回类型。

- **`BIND`（部分应用 / 柯里化一步）**  
  - **语义**：把**已提供的若干实参**与**底层可调用目标**（函数值或可由后续 lowering 解析的目标）组合成**新的函数值**（更小元数的闭包）。  
  - **边**：以 **`Norm` / `With`** 承载被绑定的参数与目标，具体排列与 GCT lowering 一致；对调度器而言，结果仍是**一等函数值**，常再交给 `CALL` 或专码路径消费。

### 4.7 `FUNC` 与 `OPER`

- **`FUNC`（直接子图调用）**  
  - **语义**：callee **编译期绑定**为具体 **`Graph*`**（子图体），并缓存运行时 **`Function*`** 原型；用于顶层/嵌套函数名调用、内联前形态等。  
  - **与 `CALL` 的区别**：`FUNC` **不**把 callee 放在运行时槽里；`inline` 等 pass 常以 `FUNC` 为入口做体图替换。  
  - **与 `JOIN` 配对**：若带“函数值 + 控制汇合”等形态，可出现 `hasMatchedJoin()` 一类结构（见 `FuncNode`）。

- **`OPER`（原子算子）**  
  - **语义**：对应注册表中的**内置或模块算子**（`OperatorIndex*`），如算术、`println`、类型谓词等；**无**独立子图，体为 C++ 算子实现。  
  - **边**：实参走 **`Norm` / `With`**，由算子签名决定；若有副作用或需排序，配合 **`Ctrl`** 边连接前后控制头。

### 4.8 `EXIT`

- **语义**：图的**唯一返回值出口**（一张可执行图在 IR 层面对外只有一个 `EXIT` 节点语义）。**`Norm` 入边**提供返回的**值**；可选 **最多一条 `Ctrl` 入边**表达“控制完成后再返回”（与 `validateGraph` 中 `EXIT.ctrlInputs.size() <= 1` 契约一致，见 §7.5）。  
- **约束**：`Graph::setOutput` / seal 路径要求存在 `EXIT` 且结构合法；从 `EXIT` 反向可达定义了“本会执行到的子图区域”。

### 4.9 `DREF`

- **语义**：**解析期 / 构图期辅助节点**，携带**重载集合、算子组或预构建图**等 `variant` 目标（见 `DrefNode::dref_target_t`），用于在 GCT→GIR 下降过程中暂存“尚未选定唯一实现”的引用。  
  - **生命周期**：通常**不应**出现在最终 sealed、可执行的图中；应在 lowering 或特化阶段消解为具体 `FUNC`/`OPER`/`CALL` 等。  
  - **若残留**：视为构图不完整或 lowering 缺陷。

## 5. 节点基类（Node）

- **graph_**：所属图指针。Node 不再绑定 `Graph&`，以便 clone / 重挂接等路径复用。
- **nodeType_ / dataType_ / dataIndex_**：类型、数据类型、数据槽索引。
- **macro_ / const_**：是否宏、是否常量。
- **frozen_**：节点是否已进入 frozen 模式。
- **draft 邻接表**：`normInputs_ / withInputs_ / ctrlInputs_` 与对应 outputs，仅 draft 阶段使用。
- **frozen 邻接表**：6 组 `{Node**, uint16_t}`，仅 frozen 阶段使用，指向 `GraphArena` 上的定长数组。
- **inDegree / outDegree / dataInDegree / dataOutDegree**：度数。
- **isSource() / isReturn()**：无入边 / 无出边。
- **link / unlink / replace**：静态方法，用于建图与图改写。
- **clone(graph)**：在另一图中复制该节点（虚函数，各子类实现）。
- **detach()**：从图中断开所有边。
- **freezeAdjacency(arena)**：将 draft 邻接表搬迁到 arena，清空 vector，并置 `frozen_=true`。

### 5.1 节点属性集中存储

为了让 frozen 节点尽量不直接持有非平凡析构成员，若干属性已从 Node 自身移到 Graph 侧集中保存：

- **节点调试实体 id**（`Node::debugEntityId()` / `Graph::nodeStableIds_`）：
  - **Draft**：构造时为占位串 `draft:{graphStableId}:{seq}`，仅保证图内可区分；不再暴露节点地址语义。
  - **Seal**：`GraphBuilder::finalize()` 在 `rearrange()` 之后调用 `Graph::promoteNodeDebugIds()`，将每条目替换为 **`NodeDebugFingerprint` 物化字符串** `gnode:{016x}{016x}`（定义见 `include/camel/compile/gir/types.h`）。指纹输入包含：所属图的 `stableId()`、`NodeType`、`dataIndex`、各 `LinkType` 上规范化排序后的邻接槽位，以及类型相关盐（如 `PORT` 端口名、`FUNC` 子图 `stableId`、`OPER` 的 `OperatorIndex*`、`DATA` 的静态槽位字节等）；并在同图固定遍历序下混入 **tie-breaker 序号**，进一步降低 128-bit 碰撞风险。
  - **SourceContext**：构图期 `bindGirNodeDraftDebug(Node*, origin_id, SemanticBundle)`；seal 时 `sealPromoteGirNodeDebug(Node*, entityId)` 写入 `DebugMap` 与按实体 id 索引的语义表。对外查询统一 `resolveGirNodeOrigin(Node*)`、`girNodeSemantic(Node*)`；克隆/内联用 `cloneGirNodeDebugBinding`。
- `PortNode::name`：由 `Graph::nodePortNames_` 保存。
- `AccsNode` 的字符串 key：由 `Graph::nodeAccsKeys_` 保存。

Node 自身只保留查询入口，不再直接持有这些 `std::string`。

## 6. 典型节点子类

- **DataNode**：通过 `dataSlot()` 暴露静态槽位，配合 `dataType()` 解释具体值；不再经旧的静态值回投路径重建编译期 `Data`。
- **PortNode**：端口名不再存于节点内，而由 Graph 集中管理；槽位由 `addRuntimeData()` 分配。
- **FuncNode**：节点内保存目标 `Graph*` 与一个运行时 `Function*` 原型；调度热路径直接取 `graph()`，宏执行等非热路径可复用 `rtFunc()`。
- **OperNode**：节点内保存裸 `OperatorIndex*`；真正所有权由 Graph 的 `operIndexRegistry_` 维护。
- **ExitNode**：图的唯一出口，可带 dataIndex_ 指向返回值槽位。
- **BrchNode / JoinNode**：与 BRCH/JOIN 字节码对应，用于条件分支与汇合。
- **GateNode**：即 **`GATE`**（§4.1），数据-控制聚合门控。
- **SyncNode**：即 **`SYNC`**（§4.5），控制汇聚。
- **AccsNode**：索引已改为 POD 形式：数字索引保存在节点内，字符串索引键保存在 Graph 侧。
- **CastNode**：运行时类型转换；由 GCT 的 `CastLoad` 在 `visitCastNode` 中生成，normInputs 为待转换的值节点，dataType 为目标类型。

## 7. GraphBuilder / GraphDraft / GraphRewriteSession

### 7.1 GraphBuilder

`GraphBuilder` 是 Graph 的唯一底层可变构造入口，负责：

- 创建 / 克隆图；
- 在 draft 图上增删节点、端口、依赖、子图；
- 在 seal 时统一完成布局与冻结。

要点：

- Graph 自身不暴露公开的 mutable 接口。
- `sealGraph()` 具有 **consume 语义**：调用后 builder 失效。
- `computeLayout()` 与 `applyLayout()` 分离，使布局计算可在不同导出路径中复用。
- `GraphBuilderState` 作为构图期 staging：写入先落 staging，并保持与 Graph draft 视图同步；
  `seal` 时进入唯一终态物化流程并消费 staging，Graph 仅保留执行期只读视图。
- `detail::NodeMutation` 仅作为 Builder/Rewrite 内部桥接，不属于稳定对外 API；
  结构改写应经 `GraphBuilder/GraphDraft/GraphRewriteSession` 通道进入。
- 结构自检：`validateGraph` / `validateGraphRecursively`（`GraphDraft::seal()` 在递归封印前会调用后者）。
  其中 **`hasOutput()==true`（存在 EXIT）是 seal 前硬约束**，缺失 output 直接视为图结构不完整并拒绝封印。

**内联与 import 相关类型**（与 `include/camel/compile/gir/builder.h` 一致）：

- **`InlineOptions` / `InlineSyncPolicy`**：`Auto`、`Force`、`Never` 控制内联时控制同步边的插入策略。
- **`inlineCallable()`** 返回 **`InlineResult`**（调用点、`valueExit`、`ctrlEntry`、是否插入入口 `SYNC`、导入的子图与依赖等），作为唯一内联入口；值-控门控 `GATE` 在 `inlineCallable` 内部完成。
- **`GraphImportMode`**：`importSubGraph` / `importDependency` 可选用 **`ReferenceOnly`**（仅登记共享引用）或 **`CloneIntoDraft`**（先 `cloneIntoDraft` 再登记），与下节「仅 root 默认 owned」规则一致。

### 7.2 GraphDraft

`GraphDraft` 是 rewrite pass 的工作态包装：

- 从 sealed graph 克隆出一个 draft 副本；
- 默认只允许编辑 root draft-owned graph；
- 若要改写子图或依赖图，必须显式 `importSubGraph(...)` / `importDependency(...)` 或 `cloneIntoDraft(...)`；
- 完成编辑后通过 `seal()`：先 **`validateGraphRecursively(root_)`**，再 **`GraphBuilder::sealGraphRecursively(root_)`**（内部按子图与依赖做 DFS，每张图走单图 `sealGraph()`）。

说明：当前 `cloneGraph()` 是浅克隆策略。`GraphDraft` 仅把 root 视为 draft-owned；子图/依赖图默认共享引用，不递归纳入 draft-owned 集合。只有在显式 import/clone 后，它们才进入当前 rewrite 事务的可写集合。

因此，rewrite pass 不再直接在原图上做“边改边 rearrange”的历史式修改。

### 7.3 补充：不可变与裁剪契约

- `finalized()==true` 的图只允许只读访问；任何追加 runtime/static 槽、改邻接、改依赖边的路径都必须先 clone 到 draft。
- 运行期入口（FastVM/JIT/Taskflow/Macro）不再补全布局，只接受 sealed graph。
- 裁剪子图必须由显式 rewrite pass 负责。以 inline 为例，删除 `FUNC` 路径后必须同步执行：
  - `eraseSubGraph(owner, subGraph)`
  - `eraseDependency(owner, subGraph)`
  - 若存在跨图节点替换，还需同步更新对应输入/输出边
- Graph 析构不承担“自动修复依赖关系”的职责；依赖与子图注册表的一致性必须在改写时收口。

### 7.4 GraphRewriteSession

`GraphRewriteSession` 是 pass 侧看到的高层接口，包装了：

- `substituteNode` / `replaceNode`
- `replaceAllUses`
- `eraseNode`
- `replaceInput` / `replaceOutput`
- `inlineCallable`
- `importSubGraph` / `importDependency` / `retargetDependency`
- `pruneUnreachable`

其中 `inlineCallable()` 返回显式 `InlineResult`，把 value exit、control entry、导入的引用图等信息一并交还给 pass，而不是再依赖“最后一个输入/输出是谁”的隐式约定。

`finish()` 在 `changed == false` 时会直接返回原始 source root；只有真正发生 rewrite 时，才会触发 `GraphDraft::seal()` 并导出新的 sealed graph。

### 7.5 Inline Pass 的 SCC-DAG 强正确性语义

`std::inline` 目前采用 **SCC 缩点 + DAG 逆拓扑** 的执行框架，而不是旧的“逐图扫描 + 反复重跑”：

> 深入版说明见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md)（覆盖会话级 use-index 重定向、`inlineCallable` 的 `SYNC/GATE` 语义、slot-safe prune 与预算哨兵）。

1. 先收集可达图并建立 draft 映射；
2. 构建会话级 use-index（`FUNC.bodyGraph` + `DATA(Function).graph`）；
3. uses 驱动执行 legacy->draft 重定向，并同步 `dependencies`；
4. 基于图引用关系做 SCC 分解并缩点为 DAG；
5. 按逆拓扑（callee-first）处理 SCC。

关键约束：

- **`std::inline` 目标策略（可配置）**：当前 pass 先枚举 SCC 内 `FUNC` 候选，再按策略过滤：
  - `std::inline::small`：小子图且**非分支 arm**（非 `DATA/PORT` 节点数 `<= 4`）；
  - `std::inline::arm`：`BRCH` arm 且小子图；
  - `std::inline::hybrid` / `std::inline`：满足任一条件即内联；
  - 默认启用“SCC 入口 callee 保护”：SCC 内若调用目标是该 SCC 的入口 callee（被 SCC 外调用），则不内联。详见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md) §1.1 / §6.1。
- **SCC 内目标冻结**：每个 SCC 只处理进入该 SCC 时捕获到的初始 inline target；处理中新增的 FUNC 不在同轮继续展开，避免振荡。
- **重定向完整性**：seal 前不允许残留 legacy 图引用。实现上以断言检查 `FUNC.bodyGraph` 与 `DATA(Function).graph` 不再指向 source graph。
- **GATE 门控保留**：当分支内联存在 value + side-effect ctrl 两路出口时，使用 `GATE`（`Norm` + `Ctrl` 双入边的聚合门控）将 ctrl 依赖锚定到值路径，避免 `BRCH/JOIN` 臂语义破坏。
- **`EXIT` 控制入边契约**：`EXIT.ctrlInputs.size() <= 1`；`inlineCallable` 以 `valueExit` 或唯一 `EXIT.ctrlInputs[0]` 为完成锚点，必要时内部插入 `GATE`，不再通过 `exitSync` 汇聚。由 `validateGraph` 校验；详见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md)。
- **slot-safe prune**：内联后的不可达裁剪不再直接调用通用 `pruneUnreachable` 全量删点；先从 `EXIT` 反向求 live 集，再对 BRCH/JOIN 槽位相关节点做 pin（`BRCH.ctrlOutputs` / `JOIN.withInputs` / `JOIN.ctrlInputs` / `JOIN.normInputs`），仅删除非 live 且非 pinned 节点，避免 arm-slot 对齐被 detach/unlink 破坏。

预算策略（budget）：

- 主调度逻辑不依赖 budget 决策；
- budget 仅作为“异常防护哨兵”：
  - **Debug 构建**：触发即 `ASSERT`（立即失败，强制暴露算法失控）；
  - **Release 构建**：保护性告警并停止当前 SCC 处理，防止线上失控。

### 7.6 FastVM BRCH/JOIN/JUMP 协议收敛

`std::inline` 场景下，FastVM 侧做了两条强约束收敛：

- BRCH 生成阶段按**语义 arm 数**（if:2，match:withCnt+1）固定发射 JUMP 占位，避免“按当前可达输出数量发射”导致的 `jumpIdx` 偏移漂移。
- BRCH 目标回填从单索引映射改为**多索引映射**（`target -> [jumpIdx...]`），保证多个 arm 收敛到同一控制头节点时不会覆盖回填位。

对应回归：

- `fib.cml std::inline std::gir` 通过；
- `inline_unique_subgraph.cml std::inline std::gir` 通过；
- `inline_unique_subgraph.cml std::inline std::fvm` 通过。

## 8. 内存与所有权模型

### 8.1 Graph 的所有权

- `Graph` 仍由 `graph_ptr_t`（`std::shared_ptr<Graph>`）管理生命周期。
- Graph 析构时会一并释放：
  - `ownedNodes_` 记录的节点对象（由 `GraphArena` tracked dtor 统一析构）；
  - `arena_`；
  - Graph 侧集中持有的字符串与注册表。

### 8.2 Draft 节点与 Frozen 节点

- **Draft 节点**
  - 在 `GraphArena` 上构造，`Graph` 通过 `ownedNodes_` 记录节点集合。
  - 使用 `std::vector<Node *>` 维护邻接关系。
  - 支持增删改边。

- **Frozen 节点**
  - 对象本身与邻接数据都位于 `GraphArena` 生命周期域内。
  - 对外只暴露 span 只读视图。
  - 任何结构修改都会被断言拦截。

### 8.3 GraphArena（metaSpace 分段区域）

`GraphArena` 是图私有的 **metaSpace 分段 arena**：

- 底层不再直接 `malloc`，而是按 block 向 `metaSpace` 申请；
- 内部区分 `DraftRegion` / `FrozenRegion` 两个逻辑区，支持 `releaseDraftRegion()`；
- Node 对象通过 tracked dtor 托管析构，邻接冻结数组与静态槽打包落在 FrozenRegion；
- 提供图级指标：`peakBytes`、`wasteBytes`、`blockCount`、`draftFreedBytes`、`allocFailCount`；
- 支持 seal 前容量预热（`reserveFrozenBytes`），降低大图 finalize 期间扩块抖动。
- 每个 block 带前后 guard band（debug 检查），用于尽早暴露 arena 用户侧越界写。
- `releaseDraftRegion()` 当前采用“逻辑释放 + 延迟物理回收”：seal 后先释放 draft 对象生命周期（tracked dtor），物理块回收延迟到 `GraphArena` 析构统一处理；若检测到 `metaSpace` 已损坏则跳过回收并直接丢弃块引用，避免 teardown 二次崩溃。

## 9. 与字节码/后端的对应

- **DATA/PORT** → 常量与参数加载。
- **OPER** → 字节码 **OPER**（变长，需运行时 operator_t）。
- **FUNC/CALL/TAIL** → **FUNC/TAIL/CALL** 字节码。
- **BRCH/JOIN** → **BRCH/JOIN** 字节码。
- **COPY/ACCS/FILL** → **COPY/ACCS/FILL** 字节码。
- **算术/比较** 常被 lowering 为 **IADD/LADD/LLE** 等定长 opcode。

GIR 是「图」形态；线性调度时再通过 **compile** 转为字节码序列（或 NodeVM 直接按拓扑序遍历图节点）。

图本身不再为某个特定 VM 预埋专有状态；各 VM 需要的缓存、索引或运行时辅助信息通过各自维护的 `extras` 协议处理。
