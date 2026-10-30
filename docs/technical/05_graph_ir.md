# 图中间表示（GIR）详解

## 1. 概述

**GIR（Graph IR）** 是 Camel 程序在编译期的核心表示，定义在 `include/camel/compile/gir.h`（命名空间 `camel::compile::gir`，常用别名 `GIR`）。所有「可执行逻辑」都以**有向图**形式存在：节点表示数据、操作或控制，边表示数据依赖或控制依赖。

## 2. 图（Graph）

### 2.1 角色与属性

- 一个 **图**（`GCGraph`，`include/camel/runtime/graph.h#GCGraph`；改写期是它的草稿 `GraphDraft`）对应一个函数体（或顶层模块的可执行部分）。
- **`mangledName()` / `name()` / `id()`**：图名与图的身份；`GraphId`（`camel/runtime/graph_id.h`）在程序内稳定，跨改写不变，由 `Context::graphIds()` 分配。
- **`outerGraphId()`**：外层图（闭包/嵌套函数时非空）。
- **`funcType()` / `closureType()` / `frameSize()`**：签名、闭包元组与帧大小。
- **`staticSlots()`**：静态数据区（`slot_t`），索引 0 保留为空；负 `dataIndex` 通过它寻址。
- **`nodes()` / `nodeCount()`**：节点记录（`GCNode` 连续存放，`Dead` 位也在里面；见 §4 的 `DEAD`）。
- **`normPortCount()` / `withPortCount()` / `closureCount()`**：普通参数端口、`with` 参数端口、闭包捕获的数量；`PORT` 节点属于其中之一。
- **`entryNode()` / `exitNode()`**：图的入口与值出口锚点（出口可为普通值节点或 `GATE`）。
- **`subGraphs()` / `dependencies()` / `staticGraphRefs()` / `ruleGraphs()`**：引用表按角色切出来的四组图（嵌套函数、被调图、静态函数值、声明标签命名的图）。
- **图空间**：节点块、邻接数组与负载都在图空间里分配，随图一起释放。

### 2.2 Draft / 编码 / 冻结 生命周期

当前的图是「草稿 → 编码 → 冻结」三段：

1. **Draft 阶段**
   - 图是 `GraphDraft`（`include/camel/runtime/draft.h`），由 `DraftGraphBuilder`（前端建图，`include/camel/compile/gir/draft_graph_builder.h`）或 `GraphDraft::decode(program, graph)`（改写，从一张 `GCGraph` 取可写副本）产生。
   - 节点是 `DraftNodeHeader`（`include/camel/runtime/draft.h#DraftNodeHeader`）加一块尾（负载与 `Norm`/`With`/`Ctrl`/`Order`/`Sync` 五组邻接表）；draft 允许增删节点、替换边、改区域。

2. **编码阶段（唯一物化入口）**
   - `GraphDraft::encode(mangledName, name)`（`src/runtime/draft.cpp#encode`）把一张 draft 编码成 `GCGraph`；要一起编码的一组新图用 `GraphDraftGroup::encode()`（`include/camel/runtime/draft_session.h#GraphDraftGroup`），一次改写事务用 `RuntimeGraphDraftSession::commit()`（`include/camel/runtime/draft_session.h#commit`）。
   - 编码时 `validateRuntimeGraphPayload` 检查引用、分支臂、同步链与分叉、环（`src/compile/gir/graph_encode.cpp#validateRuntimeGraphPayload`），不合法即内部错误。

3. **冻结阶段**
   - `GCGraph` 的节点是 28 字节的 `GCNode` 记录（`include/camel/runtime/graph.h#GCNode`），邻接视图到 arena 里的定长数组；只读，不再允许结构编辑。
   - 如需改写，先 `GraphDraft::decode()` 得到新的 draft 副本，改完重新编码。

### 2.3 数据索引约定

- **data index**：有符号整数；**正数**表示运行时槽位（动态区），**0** 表示空，**负数**表示静态区索引的相反数（`GCNode::dataIndex` 的注释，`include/camel/runtime/graph.h#GCNode`）。
- **`DraftGraphBuilder::addStaticSlot` / `addRuntimeSlot`**：向图注册静态/运行时槽位并返回索引（`include/camel/compile/gir/draft_graph_builder.h#addStaticSlot`）。
- **`GraphDraft::staticSlotTypes` / `setStaticSlot`**：按索引读静态槽位；建图期可用编译期数据重写槽位。

### 2.4 常用方法

三个入口分开：建图是 `DraftGraphBuilder`（§7.1），改写是 `GraphDraft`（§7.2），一组图的提交是 `RuntimeGraphDraftSession`（§7.4）。值的出口用 `GraphDraft::setExitNode` / `setOutputNode` 设置（`include/camel/runtime/draft.h#setExitNode`）；端口与闭包节点由 `DraftGraphBuilder::addPortNode` / `addRegionPortNode` 共建。旧的 `GraphBuilder::createGraph` / `cloneGraph` / `parametrizeClosure` / `sealGraph` / `sealGraphRecursively` 与 `computeLayout` / `applyLayout` / `LayoutResult` 都已移除。

## 3. 边类型（LinkType）

- **Norm**：普通数据边（主数据流）。
- **With**：with 参数边（如上下文、配置等命名参数）。
- **Ctrl**：控制边（保证顺序、分支汇合等）。
- **Order**：只排序——两端都执行时前者在后者之前，不传递"被用到"（`LinkType::Order`，`include/camel/compile/gir/types.h`；`docs/design/effects/edges.cn.md` 1）。
- **Sync**：`sync` 块里相邻语句之间的边，既排序又让前一条因此**被用到**（`LinkType::Sync`）；它是边，不是节点，见 §4.5。

节点间通过 `LinkType` 连接：建图期是 `linkNodes(LinkType, from, to)`（`src/compile/gir/builder_support.h`），改写期是 `GraphDraft` 上的 setter（如 `setSyncPredecessor`）。同步边与顺序边不进输入端口列表，各存一列（`GCGraphSyncEdge` / `GCGraphOrderEdge`，`include/camel/runtime/graph.h`）。

对外只读访问统一使用：

- `normInputs()` / `withInputs()` / `ctrlInputs()`
- `normOutputs()` / `withOutputs()` / `ctrlOutputs()`

这些接口返回 `node_span_t`。在 draft 节点上它们视图到内部 `vector`，在 frozen 节点上它们视图到 arena 中的定长数组。

## 4. 节点类型（`GCNodeKind`）

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
| FUNC   | 直接子图调用；负载是被调图在引用表里的下标（`GCFuncRef`） |
| OPER   | 原子算子；负载是算子表里的 `OperId` 与调度器解析出的实现（`GCOperBody`） |
| GATE   | **数据与控制依赖的聚合门控节点**（无私有数据；见下节语义约定） |
| DREF   | 临时解引用节点（图构造用，通常不加入图） |
| LOOP   | 循环节点：条件与体是这张图的两个区域，端口是初始状态、各区域的结果与轮次计数；值与收集槽的读法见 `include/camel/runtime/loop.h` |
| DEAD   | 改写删掉的节点留下的位置（无输入；遍历节点的消费者跳过它，编号不复用） |

`SYNC` 曾是一个节点种类（旧编号 12），已从模型里删除：`GCNodeKind` 里那个编号空着（`graph.h#GCNodeKind`），`sync` 块里语句的先后改由**同步边**表达（§3、§4.5）。`LOOP` 是 15、`DEAD` 是 255（同文件）。

除上表一行摘要外，**§4.1–§4.9** 按类型补充**角色、典型 `Norm`/`With`/`Ctrl` 连边约定**及与调度后端的关系；命名与 `GCNodeKind`（`graph.h#GCNodeKind`）一致，编译期的节点是 `DraftNodeRef` 句柄，不再有节点子类（`include/camel/compile/gir/nodes.h` 今天只是一个弃用转发头）。

`LOOP` 与 `DEAD` 在上表里有行、§4 里没有专节：`LOOP` 的端口与区域语义见 `docs/design/graph-model/README.cn.md` 3.5 的 `LOOP` 卡片与 `include/camel/runtime/loop.h`；`DEAD` 没有语义，只表示"这个位置曾有一个节点"。

### 4.1 `GATE`：数据-控制聚合门控

在 GIR 中，`GATE` **特指**在同一点上把**值依赖**与**控制依赖**会合起来的门控节点，用来表达“在指定控制序完成之后再沿值边传播”的语义。

- **边约束（角色定义）**：作为上述门控使用时，`GATE` **至少应有一条 `Norm` 入边**（值来源）与 **至少一条 `Ctrl` 入边**（控制完成或控制序）。二者缺一则不再是该语义下的 `GATE`，而应视为构图错误或尚未完成的中间态。
- **放行语义**：调度/执行上可理解为：**仅当 `Ctrl` 侧所表达的控制序已满足时，才沿 `Norm` 边向下游“放行”该值**（具体实现由 VM 与算子约定；图上则体现为同时依赖两类边）。
- **典型场景**：子图出口上，若“值出口”与“控制完成点”不是同一节点（例如存在经 `println` 等副作用控制路径），则通过 `GATE` 将控制锚到值路径，供 `BRCH/JOIN` 臂或调用点统一消费；详见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md)。

> **口径注（2026-10-30）：与另一份设计文档冲突，待裁。** 本节要求 `GATE` 至少一条 `Ctrl` 入边；`docs/design/effects/edges.cn.md` 5.3 与运行图的封存检查（`src/runtime/graph.cpp#validateRuntimeGraphPayloadImpl`）却把 `GATE` 说成纯传值节点、等待是**同步前驱**，控制输入只是旧图仍在用的写法（`graph.cpp#validateRuntimeGraphPayloadImpl` 两种都接受）。是改本节还是改 5.3、默认建议走哪条，见 [`questions-2026-10-30-gate-control-edge.md`](../handover/questions/questions-2026-10-30-gate-control-edge.md)；裁定前本节正文保持原样。

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

- **`ACCS`**：**下标或字段访问**。基对象经 **`Norm` 入边**接入；索引为**非负整数**（元组/数组下标）或**字符串键**（命名字段）——字符串键以 `SymId` 存在 `GCAccsBody::value` 里（见 §5.1）。出边为投影得到的子值类型。

### 4.4 `BRCH` 与 `JOIN`

二者成对（或可验证地配对）表达**条件控制流 + 数据汇合**，与 FastVM 的 `BRCH`/`JOIN` 字节码一一对应。

- **`BRCH`（分支头）**  
  - **选择子**：**恰好一条 `Norm` 入边**，为判别值（如 `bool` 或 `match` 的 key）。  
  - **臂**：**多条 `Ctrl` 出边**，每条对应一个可执行臂的**控制入口**（臂数 = `ctrlOutputs().size()`）；`if` 为 2 臂，`match` 等为 `with` 臂数 + 1 等，由前端与 codegen 约定。  
  - **`With` 入边**：在 `match` 等多路语义中，可承载各 **case 模式 / arm 相关** 的附加数据（按臂排列，`GCBrchBody::armCount`）；纯 `if` 时常为空。

- **`JOIN`（分支尾 / 汇合）**  
  - **与 `BRCH` 配对**：**第一条 `Norm` 入边**常接回 **`BRCH` 的选择子**或经布局约定的“臂索引”来源，用于运行时判定走了哪一臂。  
  - **各臂尾值**：**`With` 入边**按槽位与各臂一一对应（`armTail(i)`），仅**被执行臂**上的数据流会“活”到汇合点。  
  - **出边**：汇合后的**统一类型**值与控制流继续向下游传递。

### 4.5 同步链（原 `SYNC` 节点）

`SYNC` 节点已从模型里删掉，`GCNodeKind` 里它原来的编号 12 空着（`graph.h#GCNodeKind`）；原来由它承担的"把多条控制前驱收敛成一个后继"改由**同步边**表达（`docs/design/effects/edges.cn.md` 5.3）。

- **语义**：`sync` 块里一条语句跟在书写在它前面的那条语句之后，且前者因此**被用到**——不只是先后（`docs/design/effects/edges.cn.md` 1）。一条语句最多跟一条、也最多被一条跟，所以链不分支（封存检查 `checkSyncEdges` 与 `checkSyncChainForks`，`src/runtime/graph_edges.cpp`）。
- **表示**：`LinkType::Sync`（`include/camel/compile/gir/types.h`）；运行图上不进输入端口列，单独存一列 `GCGraphSyncEdge`（`GCGraph::syncPredsOf()` / `graph.syncEdges()`；draft 侧是 `GraphDraft::syncInputsOf()` / `setSyncPredecessor()`）。
- **由谁写**：构图器的 `writeSyncStatement(from, to)`（`src/compile/gir/builder_helpers.h`）给两端打上"同步块语句"标记，再连一条 `Sync` 边与**同对的**一条 `Ctrl` 边——后者只为仍读这个种类的消费者保留，判定不看它。链上是否写这一对由 `chainLinkSaysSomething` 决定：纯值节点不在链上，已被数据依赖蕴含的先后不再重复写。
- **链尾**：链尾要连到块的值；值节点不归这个块独有时生成 `GATE`——数据端口读该值，等待就是链尾（同步前驱；仍在用控制边的旧图写的是 `Ctrl` 入边，封存检查两者都接受），值原样放行（`docs/design/effects/edges.cn.md` 5.3、`src/runtime/graph.cpp`）。
- **冗余**：**顺序边**的冗余不在建图时省，由 `std::opt::order`（`reduceOrderDependencies`）与规范导出的约简（`reduceDocumentOrdering`，`src/passes/trans/json/gir_canonical.cpp`）去掉；同步链在建图时只写说了什么的那一对（上一条）。

### 4.6 `CALL` 与 `BIND`

- **`CALL`（间接调用）**  
  - **语义**：被调目标是**运行时函数值**（闭包），而非编译期写死在节点里的子图指针。  
  - **边约定**（与 `CALL` 一致）：**`With` 的第一条入边**为 **callee**；其余 `With` 入边为 **with 实参**；**`Norm` 入边**为位置/普通实参。出边类型为被调函数的返回类型。

- **`BIND`（部分应用 / 柯里化一步）**  
  - **语义**：把**已提供的若干实参**与**底层可调用目标**（函数值或可由后续 lowering 解析的目标）组合成**新的函数值**（更小元数的闭包）。  
  - **边**：以 **`Norm` / `With`** 承载被绑定的参数与目标，具体排列与 GCT lowering 一致；对调度器而言，结果仍是**一等函数值**，常再交给 `CALL` 或专码路径消费。

### 4.7 `FUNC` 与 `OPER`

- **`FUNC`（直接子图调用）**  
  - **语义**：callee **编译期绑定**为引用表里的那张图（`GCFuncRef::ref`，`include/camel/runtime/graph.h#GCFuncRef`）；用于顶层/嵌套函数名调用、内联前形态等。  
  - **与 `CALL` 的区别**：`FUNC` **不**把 callee 放在运行时槽里；`inline` 等 pass 常以 `FUNC` 为入口做体图替换。  
  - **与 `JOIN` 没有配对关系**：`FUNC` 的负载只指向被调图；互指的是 `BRCH` 与 `JOIN`（`GCBrchBody` / `GCJoinBody`，`graph.h#GCBrchBody`）。

- **`OPER`（原子算子）**  
  - **语义**：对应注册表中的**内置或模块算子**（`OperatorIndex*`），如算术、`println`、类型谓词等；**无**独立子图，体为 C++ 算子实现。  
  - **边**：实参走 **`Norm` / `With`**，由算子签名决定；若有副作用或需排序，配合 **`Ctrl`** 边连接前后控制头。

### 4.8 值出口锚点（`exitNode`）

- **语义**：图的**唯一返回值锚点**。`GCGraph::exitNode()` 直接返回最终值节点；该节点可以是普通值节点，也可以是用于值-控会合的 `GATE`。  
- **约束**：编码要求存在 `exitNode`（`GraphDraft::setOutputNode`）；从该锚点反向可达定义了“本会执行到的子图区域”。`GATE` 的入边要求见 §4.1 与那里的口径注。

### 4.8.1 函数声明事实在出口节点

有一类声明事实会影响调用者怎么调度这张图，而不只是编译期的元数据。标准标签 `#independent` 是其中之一：设计原则 3.6 说“元素互不依赖”是函数作者声明的性质，运行时据此决定元素调用能否并行。声明保存在模块上（`tag::ofDeclaration`），但调度器拿到的是 `GCGraph`，没有声明它的模块；因此构图器在 `Builder::visitFuncNode` 里把 `independent` 这一个键放到图的**出口节点**上（`tag::markIndependentElements`），随节点属性列编码进运行时图。运行时查询是 `tag::declaresIndependentElements(const GCGraph &)`，只读出口节点，所以它不需要模块，也不会把写在块上的同名标签误当成函数声明。其他声明标签仍只留在模块上；需要跨进程/跨图读取的声明事实，都按这个方式在出口节点留一份。

### 4.9 `DREF`

- **语义**：**解析期 / 构图期辅助节点**，携带**重载集合、算子组或预构建图**等 `variant` 目标（`DraftGraphBuilder::DrefTarget`，`include/camel/compile/gir/draft_graph_builder.h#DrefTarget`；目标存在 builder 的 `drefTargets_` 里），用于在 GCT→GIR 下降过程中暂存“尚未选定唯一实现”的引用。  
  - **生命周期**：通常**不应**出现在编码后的图里；应在 lowering 或特化阶段消解为具体 `FUNC`/`OPER`/`CALL` 等。  
  - **若残留**：编码时遇到即报错（`validateRuntimeGraphPayload`，`include/camel/runtime/graph.h#GCNodeKind`）。

## 5. 节点记录（`GCNode` / `DraftNodeHeader`）

节点没有基类，也没有子类：建图与改写期是带尾块的记录，运行期是定长记录。

- **建图 / 改写期**：`DraftNodeHeader`（`include/camel/runtime/draft.h#DraftNodeHeader`）——`kind`、`dataType`、`dataIndex`、`runtimeFlags`、数值许可 `permissions`、属性 `attrs`、源名 `name`、源位置 `origin`、所在区域 `region`，以及五组入边的计数（`Norm`/`With`/`Ctrl`/`Order`/`Sync`）；负载与邻接表在这条记录的尾块里，经 `DraftNodeView` 读写（`include/camel/runtime/draft.h#DraftNodeView`）。
- **运行期**：`GCNode`（`include/camel/runtime/graph.h#GCNode`）——28 字节，字段 `kind` / `flags` / `dataIndex` / `typeId` / `inBegin` / `withBegin` / `ctrlBegin` / `payload`；输入经 `GCGraph::normInputsOf` / `withInputsOf` / `ctrlInputsOf` 与 `normOutputsOf` / `withOutputsOf` / `ctrlOutputsOf` 读，负载经 `GCGraph::nodeBodyAs<T>()` 读（都在 `include/camel/runtime/graph.h`）。顺序边与同步边不在节点里，是图上的两列（`GCGraphOrderEdge` / `GCGraphSyncEdge`，同文件）。
- **写入口**：draft 上是 `GraphDraft::setNormInputs` / `setWithInputs` / `setCtrlInputs` / `setOrderInputs` / `setSyncPredecessor`（`include/camel/runtime/draft.h#setNormInputs`）；运行期只读。
- **已移除**：`Node` 基类与它的 `inDegree` / `outDegree` / `isSource()` / `isReturn()` / `link` / `unlink` / `replace` / `clone(graph)` / `detach()` / `freezeAdjacency(arena)` 都不在了——度数按输入/输出视图自己数，改写用 draft 的 setter，拿一张图的可写副本用 `GraphDraft::decode()`。

### 5.1 节点属性存在哪里

节点记录只留执行与改写的字段，其余按下表存放：

| 属性 | 存放处 | 出处 |
|---|---|---|
| 端口名（`PORT`） | 建图期 `DraftGraphBuilder::nodePortNames_`，经 `nodePortName()` 读；`GCGraph` 没有端口名的访问器，运行期读的是值的源名 `GCGraph::nodeName` | `include/camel/compile/gir/draft_graph_builder.h#nodePortName`；`include/camel/runtime/graph.h#nodeName` |
| `ACCS` 的字符串键 | `GCAccsBody::value` 存 `SymId`，`key()` 取符号文本 | `include/camel/runtime/graph.h#GCAccsBody` |
| `FUNC` 的被调图 | 建图期 `DraftGraphBuilder::funcTargets_`；冻结后是引用表下标 `GCFuncRef::ref` | `draft_graph_builder.h#funcTargets_`；`graph.h#GCFuncRef` |
| `OPER` 的算子 | 建图期 `DraftGraphBuilder::operTargets_` 与 `operIndexRegistry_`；冻结后是 `GCOperBody::oper`（`OperId`）与调度器解析出的 `operator_t` | `draft_graph_builder.h#operTargets_`；`graph.h#GCOperBody` |
| `DREF` 的目标 | 建图期 `DraftGraphBuilder::drefTargets_`（`variant`）；编码时遇到即报错 | `draft_graph_builder.h#DrefTarget`；`graph.h#GCNodeKind` |
| 源位置 / 源名 | `GCGraph::nodeOrigin` / `nodeName` / `nodeNameSym`（冷数据） | `graph.h#nodeOrigin` |
| SourceContext 语义 | `bindGirNodeDraftDebug` / `resolveGirNodeOrigin` / `girNodeSemantic` / `cloneGirNodeDebugBinding` / `sealPromoteGirNodeDebug`，键是 `gir_draft_node_key_t` | `include/camel/core/source/manager.h#girNodeSemantic` |

旧的 `Node::debugEntityId()`、`Graph::nodeStableIds_`、`Graph::promoteNodeDebugIds()`、`NodeDebugFingerprint` 指纹都不再参与建图与编码（`NodeDebugFingerprint` 只剩定义，`include/camel/compile/gir/types.h#NodeDebugFingerprint`，今天没有代码用它）。

## 6. 各种类的数据放在哪

种类相关的数据是负载里的 POD 结构，按 `kind` 经 `GCGraph::nodeBodyAs<T>()` 读：

- `FUNC`：`GCFuncRef`（被调图在引用表里的下标；`graph.h#GCFuncRef`）。
- `LOOP`：`GCLoopRef`（循环形式、条件/体图的引用下标、收集槽数；`graph.h#GCLoopRef`）。
- `CALL`：`GCCallBody`（`GCCallKind`；`graph.h#GCCallBody`）。
- `ACCS`：`GCAccsBody`（元组下标或字段名；`graph.h#GCAccsBody`）。
- `FILL`：`GCFillBody`（种类与每个值填到哪个位置；`graph.h#GCFillBody`）。
- `BRCH` / `JOIN`：`GCBrchBody` / `GCJoinBody`（配对的另一头与臂数；`graph.h#GCBrchBody`）。
- `OPER`：`GCOperBody`（`OperId` 与调度器解析出的 `operator_t`；`graph.h#GCOperBody`）。
- `DATA` / `PORT` / `GATE` / `CAST` / `COPY`：没有负载。`DATA` 的 `dataIndex` 为负时指向静态区；`PORT` 是图的三张端口之一（形参、`with` 形参、闭包捕获）。

旧的那一层节点子类——`DataNode`、`PortNode`、`FuncNode`、`OperNode`、`BrchNode` / `JoinNode`、`GateNode`、`AccsNode`、`CastNode`——已移除：职责由 `GCNode` 加负载结构承担，建图期则由 `DraftNodeHeader` 加 `DraftGraphBuilder` 的冷数据表承担。`include/camel/compile/gir/nodes.h` 今天只是一个弃用转发头。

`CAST` 仍由 `Builder::visitCastNode`（`src/compile/gir/builder_expr.cpp#visitCastNode`）生成：`Norm` 入边是待转换的值，节点类型是目标类型。

同步边（§4.5）不是节点，没有对应的节点子类。

## 7. DraftGraphBuilder / GraphDraft / RuntimeGraphDraftSession

### 7.1 DraftGraphBuilder

`DraftGraphBuilder`（`include/camel/compile/gir/draft_graph_builder.h#DraftGraphBuilder`）是编译期建图的宿主：它持有一个 `GraphDraft`、一个 `GraphArena`，以及只属于编译期的冷数据（端口名、`FUNC`/`DREF`/`OPER` 的目标、依赖/子图/静态图引用、`OperatorIndex` 的所有权）。GCT → GIR 的下降全经它。

- **建节点**：`addStaticDataNode`、`addPortNode`、`addCastNode`、`addCopyNode`、`addFillNode`、`addAccsNode`、`addBrchNode`、`addJoinNode`、`addCallNode`、`addFuncNode`、`addOperNode`、`addRegionLoopNode`、`addRegionPortNode`、`addGateNode`、`addDrefNode`（同文件）。
- **连边**：`link(LinkType, from, to)` / `unlink(...)`；同步块里相邻语句的链由 `writeSyncStatement` 写（`src/compile/gir/builder_helpers.h#writeSyncStatement`），它同一对节点上还会补一条 `Ctrl`（给仍按控制边读的消费者）。
- **图与图之间**：`addDependencyGraph` / `addSubGraph` / `addStaticGraphRef` / `addRuleGraphRef` 及对应的 `erase*`；读取用 `dependencyGraphs()`、`subGraphs()`、`staticGraphRefs()`、`ruleGraphs()`。
- **冷数据**：`registerNodePortName` / `registerFuncTarget` / `registerDrefTarget` / `registerOperTarget` 写入，`nodePortName()` / `funcTarget()` / `drefTarget()` / `operTarget()` 读出；`lookupOperIndex()` 管 `OperatorIndex` 的生命周期（§5.1 的表）。

旧的 `GraphBuilder`、`sealGraph()` / `sealGraphRecursively()`、`GraphBuilder` 的 `computeLayout()` / `applyLayout()`、`GraphBuilderState`、`detail::NodeMutation` 都已不在代码里：物化改由 `GraphDraft::encode()` 完成（§2.2），构图期的 staging 就是 `GraphDraft` 自己（`draft()` 返回它）。

**内联相关类型**：

- **`inlineCallable()`**（`inlineCallableInDraft`，`include/camel/runtime/draft_inline.h`）返回 **`DraftInlineResult`**（`callNode`、`valueExit`、`ctrlEntry`、`ctrlExit`），作为唯一内联入口；入口不再插入 `SYNC` 节点——多个入口根时每个根各自接上外部控制前驱，`ctrlEntry` 取第一个（`docs/technical/16_inline_rewrite_algorithm.md` 7.3）；需要会合的值-控门控 `GATE` 在 `inlineCallable` 内部完成。
- **让 draft 看见别的图**：旧的 `GraphImportMode` / `importSubGraph` / `importDependency` / `cloneIntoDraft` / `ReferenceOnly` / `CloneIntoDraft` 都已移除。今天：要读别的程序或图，用 `RuntimeGraphDraftSession::adoptProgram(program)` / `adoptGraph(graph)`（`include/camel/runtime/draft_session.h`）；要把一张图变成可写副本，用 `GraphDraft::decode(program, graph)`，新图由 `GraphDraftGroup::reserve()` 登记；把图登记为依赖或子图，用 `GraphDraft::addDependency` / `addSubGraph`（`include/camel/runtime/draft.h#addDependency`）。

### 7.2 GraphDraft

`GraphDraft`（`include/camel/runtime/draft.h`）是改写遍的工作态，与 `DraftGraphBuilder` 共用同一套编辑核心：

- `GraphDraft::decode(program, graph)` 从一张 `GCGraph` 得到可写副本：节点号沿用原图（删掉的节点留 `Dead` 位；过半数是 `Dead` 时重新编号）；新图由 `GraphDraftGroup::reserve()` 登记（`include/camel/runtime/draft_session.h#GraphDraftGroup`），draft 按 `GraphId` 引用它读的图。
- 编辑 API 按边种类给：`setNormInputs` / `setWithInputs` / `setCtrlInputs` / `setOrderInputs` / `setSyncPredecessor`，加 `appendClosureNode` / `removeClosureNode` / `removeNormPort` / `removeWithPort`（`include/camel/runtime/draft.h#appendClosureNode`）。
- `encode(mangledName, name)` 是唯一的物化入口；`revision()` 随每次改动前进，供会话判断"这张图变了没有"。

旧的 `importSubGraph(...)` / `importDependency(...)` / `cloneIntoDraft(...)` 与"root 默认 owned、其余共享"的规则已移除：draft 之间没有 owned/共享之分，一起编码的一组图由 `GraphDraftGroup` 持有，一次改写事务由 `RuntimeGraphDraftSession` 持有（§7.4）。

### 7.3 不可变与裁剪契约

- 编码后的 `GCGraph` 只读：任何追加运行时/静态槽、改邻接、改依赖边的路径都必须先 `GraphDraft::decode()` 到可写副本。
- 运行期入口（FastVM/JIT/Taskflow/Macro）不再补全布局，只接受编码完成的 `GCGraph`。
- 裁剪子图由改写遍自己负责：内联删掉 `FUNC` 路径后，用 `GraphDraft::dropUncalledGraphs(graphs)` 把已无人调用的图从依赖表与子图表里去掉（`src/runtime/draft.cpp#dropUncalledGraphs`），并同步更新受影响的输入/输出边。
- `GCGraph` 析构不承担"自动修复依赖关系"的职责；依赖与子图引用表的一致性必须在改写时收口。

### 7.4 RuntimeGraphDraftSession

`RuntimeGraphDraftSession`（`include/camel/runtime/draft_session.h#RuntimeGraphDraftSession`）是 pass 侧看到的高层接口，替掉了旧的 `GraphRewriteSession`：

- **打开**：`RuntimeGraphDraftSession(context, program)`，或指定一张 `runtimeRoot`（函数值、导出、梯度这类独立程序）；会话覆盖从根可达的图。
- **取草稿**：`edit(graph)`（取或建）、`tryDraft(graph)`、`rootDraft()`。
- **提交**：`commit(detached)` 把会话里的草稿编码成新版本、组装成新 `Program`；`commitChecked()` 在冲突时抛 `GraphCommitConflict`，`conflicts()` 列出提交前被别处改过的图。
- **看见别的图**：`adoptProgram(program)` / `adoptGraph(graph)`；特化用 `findSpecialization` / `rememberSpecialization`。
- **高层改写操作**（`replaceAllUses`、`eraseNode`、`pruneUnreachable`）不在会话上：今天由各遍在 `GraphDraft` 上自己做，改写态与规则引擎用 `DraftWritings`（`include/camel/runtime/writings.h#DraftWritings`）。内联仍用 `inlineCallableInDraft` 返回 `DraftInlineResult`（§7.1）。

### 7.5 Inline Pass 的 SCC-DAG 强正确性语义

`std::opt::inline` 目前采用 **SCC 缩点 + DAG 逆拓扑** 的执行框架，而不是旧的“逐图扫描 + 反复重跑”：

> 深入版说明见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md)（覆盖会话级 use-index 重定向、`inlineCallable` 的控制入口与 `GATE` 语义、slot-safe prune 与预算哨兵）。

1. 先收集可达图并建立 draft 映射；
2. 构建会话级 use-index（`FUNC.bodyGraph` + `DATA(Function).graph`）；
3. uses 驱动执行 legacy->draft 重定向，并同步 `dependencies`；
4. 基于图引用关系做 SCC 分解并缩点为 DAG；
5. 按逆拓扑（callee-first）处理 SCC。

关键约束：

- **`std::opt::inline` 目标策略（可配置）**：当前 pass 先枚举 SCC 内 `FUNC` 候选，再按策略过滤：
  - `std::opt::inline::small`：小子图且**非分支 arm**（非 `DATA/PORT` 节点数 `<= 4`）；
  - `std::opt::inline::arm`：`BRCH` arm 且小子图；
  - `std::opt::inline::hybrid` / `std::opt::inline`：满足任一条件即内联；
  - 默认启用“SCC 入口 callee 保护”：SCC 内若调用目标是该 SCC 的入口 callee（被 SCC 外调用），则不内联。详见 [`16_inline_rewrite_algorithm.md`](16_inline_rewrite_algorithm.md) §1.1 / §6.1。
- **SCC 内目标冻结**：每个 SCC 只处理进入该 SCC 时捕获到的初始 inline target；处理中新增的 FUNC 不在同轮继续展开，避免振荡。
- **重定向完整性**：编码前不允许残留 legacy 图引用。实现上以断言检查 `FUNC` 的被调图与 `DATA(Function)` 的图不再指向 source graph。
- **GATE 门控保留**：当分支内联存在 value + side-effect ctrl 两路出口时，使用 `GATE` 把控制完成点锚到值路径，避免 `BRCH/JOIN` 臂语义破坏（`GATE` 的入边要求见 §4.1 与那里的口径注）。
- **值出口门控契约**：`inlineCallable` 以 `valueExit` 为完成锚点；若值与控制完成点分离，则先插入 `GATE` 再导出 `valueExit`，不再依赖专用 `EXIT.ctrlInputs` 契约。`GATE` 结构由编码时的 `validateRuntimeGraphPayload` 检查（§2.2）。
- **slot-safe prune**：内联后的不可达裁剪不再直接调用通用 `pruneUnreachable` 全量删点；先从 `exitNode` 反向求 live 集，再对 BRCH/JOIN 槽位相关节点做 pin（`BRCH.ctrlOutputs` / `JOIN.withInputs` / `JOIN.ctrlInputs` / `JOIN.normInputs`），仅删除非 live 且非 pinned 节点，避免 arm-slot 对齐被 detach/unlink 破坏。

预算策略（budget）：

- 主调度逻辑不依赖 budget 决策；
- budget 仅作为“异常防护哨兵”：
  - **Debug 构建**：触发即 `ASSERT`（立即失败，强制暴露算法失控）；
  - **Release 构建**：保护性告警并停止当前 SCC 处理，防止线上失控。

### 7.6 FastVM BRCH/JOIN/JUMP 协议收敛

`std::opt::inline` 场景下，FastVM 侧做了两条强约束收敛：

- BRCH 生成阶段按**语义 arm 数**（if:2，match:withCnt+1）固定发射 JUMP 占位，避免“按当前可达输出数量发射”导致的 `jumpIdx` 偏移漂移。
- BRCH 目标回填从单索引映射改为**多索引映射**（`target -> [jumpIdx...]`），保证多个 arm 收敛到同一控制头节点时不会覆盖回填位。

对应回归：

- `fib.cml std::opt::inline std::gir` 通过；
- `inline_unique_subgraph.cml std::opt::inline std::gir` 通过；
- `inline_unique_subgraph.cml std::opt::inline std::fvm` 通过。

## 8. 内存与所有权模型

### 8.1 图的所有权

- **编译期**：一张图是 `graph_ptr_t`，也就是 `std::shared_ptr<DraftGraphBuilder>`（`include/camel/compile/gir/types.h#graph_ptr_t`）；builder 拥有 `GraphDraft`（`draft()`）与 `GraphArena`（`arena()`），两者随 builder 释放。
- **运行期**：`GCGraph` 是 `camel::core::rtdata::Object`，由 `Program` 持有；它占图空间里的一次分配（`mm::graphSpace().alloc`，`src/runtime/graph.cpp#GCGraphBuildAccess`），图空间到上限时抛 `MemoryExhausted`。节点块按 `kNodeChunkNodes`（256）一块单独分配（`graph.h#kNodeChunkNodes`），版本之间共享未改动的块。
- 旧的 `Graph` 类与它的 `ownedNodes_` 已移除（§5）。

### 8.2 Draft 节点与冻结节点

- **Draft 节点**：来自 `DraftNodePool`（`include/camel/runtime/draft.h#DraftNodePool`），不是 `GraphArena`。池按存储类切块——节点头（`DraftNodeHeader`）加 64/96/128 字节的尾，超过 128 的走 Oversize——每 4 KB 一页（`draft.h#kPageBytes`），页用 `std::byte[]` 一次申请、随 draft 释放（`src/runtime/draft.cpp#allocFromArena`）。邻接表在尾块里，所以节点可以增删边。
- **冻结节点**：`GCNode` 记录在节点块里，与边表、负载、静态区同属这张图的分配；对外只有指针/span 视图，结构不可改（要改先 `GraphDraft::decode`，§2.2）。

### 8.3 GraphArena

`GraphArena`（`include/camel/compile/gir/arena.h#GraphArena`）今天只服务建图期的静态函数值（`staticFunctionAllocator`，`src/compile/gir/builder_helpers.h#staticFunctionAllocator`）；draft 节点与冻结图都不在它里面。

- 按 block 向 `metaSpace` 申请（`arena.h#allocateBlock`），内部区分 Draft / Frozen 两区。
- 每个 block 前后各 64 字节 guard band（`kGuardBytes_`，`arena.h#kGuardBytes_`），Debug 构建在回收前校验两端图案（`arena.h#releaseRegion`），越界写尽早暴露。
- 图级指标只有 `peakBytes`、`wasteBytes`、`blockCount`、`draftFreedBytes`（`arena.h#Metrics`）。
- `reserveFrozenBytes()` 在编码前预热容量（`arena.h#reserveFrozenBytes`），减少扩块抖动。
- `releaseDraftRegion()` 是“逻辑释放 + 延迟物理回收”：先跑 tracked dtor 并记 `draftFreedBytes`，块本身留到 `GraphArena` 析构再还给 `metaSpace`——注释给的理由是，提前还回去会让任何悬垂指针当场破坏空闲表（`arena.h#releaseRegion`）。

## 9. 与字节码/后端的对应

- **DATA/PORT** → 常量与参数加载。
- **OPER** → 字节码 **OPER**（变长，需运行时 operator_t）。
- **FUNC/CALL/TAIL** → **FUNC/TAIL/CALL** 字节码。
- **BRCH/JOIN** → **BRCH/JOIN** 字节码。
- **COPY/ACCS/FILL** → **COPY/ACCS/FILL** 字节码。
- **算术/比较** 常被 lowering 为 **IADD/LADD/LLE** 等定长 opcode。

GIR 是「图」形态；线性调度时再通过 **compile** 转为字节码序列（或 NodeVM 直接按拓扑序遍历图节点）。

图本身不再为某个特定 VM 预埋专有状态；各 VM 需要的缓存、索引或运行时辅助信息通过各自维护的 `extras` 协议处理。
