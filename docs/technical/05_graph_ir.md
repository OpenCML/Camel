# 图中间表示（GIR）详解

## 1. 概述

**GIR（Graph IR）** 是 Camel 程序在编译期的核心表示，定义在 `src/compile/gir.h`（命名空间 **GraphIR**）。所有「可执行逻辑」都以**有向图**形式存在：节点表示数据、操作或控制，边表示数据依赖或控制依赖。

## 2. 图（Graph）

### 2.1 角色与属性

- 一个 **Graph** 对应一个函数体（或顶层模块的可执行部分）。
- **name_**：图名（函数名或匿名）。
- **outer_**：外层图（闭包/嵌套函数时非空）。
- **funcType_**：函数类型（参数、返回值、with 等）。
- **staticDataType_ / runtimeDataType_ / closureType_**：静态数据、运行时数据、闭包捕获的类型元组。
- **staticDataArr_**：静态数据区（编译期常量），下标 0 保留为空。
- **runtimeDataSize_**：运行时数据区大小（从 1 起，0 保留）。
- **nodes_**：图中所有节点（含 PORT、DATA、OPER、CALL、EXIT 等）。
- **normPorts_ / withPorts_ / closure_**：普通参数端口、with 参数端口、闭包捕获节点。
- **exitNode_**：图的唯一出口节点（EXIT）。
- **subGraphs_**：按名称索引的子图集合（嵌套函数）。
- **dependencies_ / dependents_**：图级依赖（被谁依赖 / 依赖谁），用于多图调度或内联等。

### 2.2 数据索引约定

- **data_idx_t**：有符号整数；**正数**表示运行时槽位（动态区），**0** 表示空，**负数**表示静态区索引的相反数（见注释）。
- **addStaticData / addRuntimeData**：向图注册静态/运行时槽位并返回索引。
- **getStaticData / setStaticData**：按索引读写静态数据。

### 2.3 常用方法

- `create(funcType, outer, name)`：工厂方法。
- `ports()` / `normPorts()` / `withPorts()` / `closure()`：端口与闭包节点。
- `setOutput(node)` / `exitNode()` / `outputNode()`：设置与获取出口。
- `rearrange()`：在节点增删后重排（拓扑等），内部会清 dirty 标记。
- `clone()`：深拷贝图（用于内联、特化等）。
- `parametrizeClosure()`：将闭包捕获转为参数节点。

## 3. 边类型（LinkType）

- **Norm**：普通数据边（主数据流）。
- **With**：with 参数边（如上下文、配置等命名参数）。
- **Ctrl**：控制边（保证顺序、分支汇合等）。

节点间通过 `Node::link(LinkType, from, to)` 连接，通过 `normInputs_/withInputs_/ctrlInputs_` 与 `normOutputs_/withOutputs_/ctrlOutputs_` 访问。

## 4. 节点类型（NodeType）

| 类型   | 说明 |
|--------|------|
| DATA   | 编译期常量，存于静态区，索引为正的 data_idx |
| PORT   | 参数端口，占运行时槽位，有 name/isVar |
| CAST   | 运行时类型转换 |
| COPY   | 运行时数据拷贝 |
| FILL   | 运行时构造复合数据（如 struct/array） |
| ACCS   | 运行时下标/字段访问，accsIndex_ 为 string 或 size_t |
| BRCH   | 条件分支 |
| JOIN   | 分支汇合 |
| CALL   | 调用（闭包/函数值） |
| BIND   | 部分应用（绑定参数） |
| FUNC   | 子图/函数引用，持有一个 func_ptr_t |
| OPER   | 原子算子，持有一个 oper_idx_ptr_t（对应 operator_t） |
| EXIT   | 图出口，产出返回值 |
| SYNC   | 同步点（无私有数据） |
| NREF   | 节点引用（无私有数据） |
| DREF   | 临时解引用节点（图构造用，通常不加入图） |

## 5. 节点基类（Node）

- **graph_**：所属图。
- **nodeType_ / dataType_ / dataIndex_**：类型、数据类型、数据槽索引。
- **macro_ / const_**：是否宏、是否常量。
- **normInputs_ / withInputs_ / ctrlInputs_**：三类入边对应节点列表。
- **normOutputs_ / withOutputs_ / ctrlOutputs_**：三类出边对应节点列表。
- **inDegree / outDegree / dataInDegree / dataOutDegree**：度数。
- **isSource() / isReturn()**：无入边 / 无出边。
- **link / unlink / replace**：静态方法，用于建图与图改写。
- **clone(graph)**：在另一图中复制该节点（虚函数，各子类实现）。
- **detach()**：从图中断开所有边。

## 6. 典型节点子类

- **DataNode**：通过 `Graph::getStaticData(dataIndex_)` 取编译期数据。
- **PortNode**：name_、isVar_，由 `addRuntimeData()` 分配槽位。
- **FuncNode**：保存 `func_ptr_t`，类型为函数返回类型。
- **OperNode**：保存 `oper_idx_ptr_t`，类型为算子返回类型；OPER 指令在运行时通过 operator_t 分发。
- **ExitNode**：图的唯一出口，可带 dataIndex_ 指向返回值槽位。
- **BrchNode / JoinNode**：与 BRCH/JOIN 字节码对应，用于条件分支与汇合。
- **AccsNode**：accsIndex_ 为字段名或下标，用于 FILL/ACCS 等字节码的语义。
- **CastNode**：运行时类型转换；由 GCT 的 `CastLoad` 在 `visitCastNode` 中生成，normInputs 为待转换的值节点，dataType 为目标类型。

## 7. 与字节码的对应

- **DATA/PORT** → 常量与参数加载。
- **OPER** → 字节码 **OPER**（变长，需运行时 operator_t）。
- **FUNC/CALL/TAIL** → **FUNC/TAIL/CALL** 字节码。
- **BRCH/JOIN** → **BRCH/JOIN** 字节码。
- **COPY/ACCS/FILL** → **COPY/ACCS/FILL** 字节码。
- **算术/比较** 常被 lowering 为 **IADD/LADD/LLE** 等定长 opcode。

GIR 是「图」形态；线性调度时再通过 **compile** 转为字节码序列（或 NodeVM 直接按拓扑序遍历图节点）。
