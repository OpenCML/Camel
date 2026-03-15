# 图中间表示（GIR）详解

## 1. 概述

**GIR（Graph IR）** 是 Camel 程序在编译期的核心表示，定义在 `include/camel/compile/gir.h`（命名空间 `camel::compile::gir`，常用别名 `GIR`）。所有「可执行逻辑」都以**有向图**形式存在：节点表示数据、操作或控制，边表示数据依赖或控制依赖。

## 2. 图（Graph）

### 2.1 角色与属性

- 一个 **Graph** 对应一个函数体（或顶层模块的可执行部分）。
- **name_ / stableId_**：图名与稳定标识；`stableId_` 用于跨 clone / rewrite 追踪图来源。
- **outer_**：外层图（闭包/嵌套函数时非空）。
- **signature_**：图的签名与布局摘要，内部包含 `funcType`、`staticDataType`、`runtimeDataType`、`closureType`、`runtimeDataSize`。
- **staticDataArr_**：静态数据区，索引 0 保留为空；负 `data_idx_t` 通过它寻址。
- **nodes_**：图中所有节点（含 PORT、DATA、OPER、CALL、EXIT 等）。
- **normPorts_ / withPorts_ / closure_**：普通参数端口、with 参数端口、闭包捕获节点。
- **exitNode_**：图的唯一出口节点（EXIT）。
- **subGraphs_**：按名称索引的子图集合（嵌套函数）。
- **dependencies_ / dependents_**：图级依赖（被谁依赖 / 依赖谁），用于多图调度或内联等。
- **arena_**：图私有的 `GraphArena`。seal 后 frozen 邻接数组等只读数据分配在这里，并随 Graph 一起释放。
- **extras_**：图级 O(1) 扩展缓存槽位。Graph 本身不绑定具体 VM；各后端自行约定 index。

### 2.2 Draft / Sealed / Frozen 生命周期

当前 GIR 采用显式的“两阶段”模型：

1. **Draft 阶段**
   - 图通过 `GraphBuilder` 或 `GraphDraft` 被构造/编辑。
   - Node 的邻接关系保存在 `std::vector<Node *>` 中，便于增删改边。
   - 图允许增删节点、替换边、内联、调整依赖等结构性修改。

2. **Seal 阶段**
   - 通过 `GraphBuilder::sealGraph()` 或 `sealGraphRecursively()` 完成。
   - 内部先执行布局计算与写回：`computeLayout()` -> `applyLayout()`。
   - 然后安装 `FrameMeta`，最后把所有节点的邻接 `vector` 搬迁到 arena 中的定长数组。

3. **Frozen 阶段**
   - `Graph::finalized()` 为 `true`。
   - 节点只允许只读访问，不再允许结构编辑。
   - 如需改写，必须先 `cloneGraph()` 得到新的 draft 副本。

### 2.3 数据索引约定

- **data_idx_t**：有符号整数；**正数**表示运行时槽位（动态区），**0** 表示空，**负数**表示静态区索引的相反数（见注释）。
- **addStaticData / addRuntimeData**：向图注册静态/运行时槽位并返回索引。
- **getStaticData / setStaticData**：按索引读写静态数据。

### 2.4 常用方法

- `GraphBuilder::createGraph(funcType, outer, name)`：创建 draft 图。
- `GraphBuilder::cloneGraph(graph)`：克隆图，产出新的 draft 图。
- `ports()` / `normPorts()` / `withPorts()` / `closure()`：端口与闭包节点。
- `setOutput(node)` / `exitNode()` / `outputNode()`：设置与获取出口。
- `GraphBuilder::parametrizeClosure()`：将闭包捕获转为参数节点。
- `GraphBuilder::sealGraph()`：封印单图。
- `GraphBuilder::sealGraphRecursively()`：递归封印图树。
- `computeLayout()` / `applyLayout()`：布局纯计算与写回分离，供 seal/export 路径复用。

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
| FUNC   | 子图/函数引用，节点内持有裸 `FunctionData*` |
| OPER   | 原子算子，节点内持有裸 `OperatorIndex*` |
| EXIT   | 图出口，产出返回值 |
| SYNC   | 同步点（无私有数据） |
| NREF   | 节点引用（无私有数据） |
| DREF   | 临时解引用节点（图构造用，通常不加入图） |

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

- `stableId`：由 `Graph::nodeStableIds_` 保存。
- `PortNode::name`：由 `Graph::nodePortNames_` 保存。
- `AccsNode` 的字符串 key：由 `Graph::nodeAccsKeys_` 保存。

Node 自身只保留查询入口，不再直接持有这些 `std::string`。

## 6. 典型节点子类

- **DataNode**：通过 `Graph::materializeStaticData(dataIndex_)` 暴露只读静态值视图；冻结图优先从静态运行时池回投到编译期表示。
- **PortNode**：端口名不再存于节点内，而由 Graph 集中管理；槽位由 `addRuntimeData()` 分配。
- **FuncNode**：节点内保存裸 `FunctionData*`；真正所有权由 Graph 的 `funcDataRegistry_` / 静态数据侧统一维护。
- **OperNode**：节点内保存裸 `OperatorIndex*`；真正所有权由 Graph 的 `operIndexRegistry_` 维护。
- **ExitNode**：图的唯一出口，可带 dataIndex_ 指向返回值槽位。
- **BrchNode / JoinNode**：与 BRCH/JOIN 字节码对应，用于条件分支与汇合。
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

### 7.2 GraphDraft

`GraphDraft` 是 rewrite pass 的工作态包装：

- 从 sealed graph 克隆出一个 draft 副本；
- 只允许编辑 draft-owned graph；
- 完成编辑后通过 `seal()` 一次性封印整棵图树。

因此，rewrite pass 不再直接在原图上做“边改边 rearrange”的历史式修改。

### 7.3 GraphRewriteSession

`GraphRewriteSession` 是 pass 侧看到的高层接口，包装了：

- `replaceNode`
- `eraseNode`
- `replaceInput` / `replaceOutput`
- `inlineNode`
- `addDependency` / `eraseDependency`

`finish()` 内部会触发 `GraphDraft::seal()`，返回新的 sealed graph。

## 8. 内存与所有权模型

### 8.1 Graph 的所有权

- `Graph` 仍由 `graph_ptr_t`（`std::shared_ptr<Graph>`）管理生命周期。
- Graph 析构时会一并释放：
  - `ownedNodes_` 中的 draft 节点；
  - `arena_`；
  - Graph 侧集中持有的字符串与注册表。

### 8.2 Draft 节点与 Frozen 节点

- **Draft 节点**
  - 由 `ownedNodes_` 中的 `unique_ptr<Node>` 管理。
  - 使用 `std::vector<Node *>` 维护邻接关系。
  - 支持增删改边。

- **Frozen 节点**
  - 当前对象本身仍由 Graph 持有，但其邻接数据已迁入 `GraphArena`。
  - 对外只暴露 span 只读视图。
  - 任何结构修改都会被断言拦截。

### 8.3 GraphArena

`GraphArena` 是图私有的 bump allocator：

- 为 frozen 邻接数组等只读数据提供整块连续内存；
- 不支持逐对象 `free`；
- Graph 销毁时整体释放；
- 避免 metaspace/freelist 在高频小对象上的碎片和管理开销。

## 9. 与字节码/后端的对应

- **DATA/PORT** → 常量与参数加载。
- **OPER** → 字节码 **OPER**（变长，需运行时 operator_t）。
- **FUNC/CALL/TAIL** → **FUNC/TAIL/CALL** 字节码。
- **BRCH/JOIN** → **BRCH/JOIN** 字节码。
- **COPY/ACCS/FILL** → **COPY/ACCS/FILL** 字节码。
- **算术/比较** 常被 lowering 为 **IADD/LADD/LLE** 等定长 opcode。

GIR 是「图」形态；线性调度时再通过 **compile** 转为字节码序列（或 NodeVM 直接按拓扑序遍历图节点）。

图本身不再为某个特定 VM 预埋专有状态；各 VM 需要的缓存、索引或运行时辅助信息通过各自维护的 `extras` 协议处理。
