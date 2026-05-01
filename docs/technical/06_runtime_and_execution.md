# 运行时与执行

## 1. 概述

Camel 的运行时围绕 **Frame**（栈帧）、**Context**（全局/模块上下文）、**Data/Type**（值与类型）以及 **调度器**（Scheduler）展开。图（GIR）被编译为线性字节码或直接按图遍历，由调度 Pass 选择执行方式。

## 2. Frame（栈帧）

- **定义**：`src/core/context/frame.h`，继承自 `Object`。
- **职责**：为一次「图执行」保存运行时状态：当前图、静态区、动态区、类型信息等。

### 2.1 与图的绑定

- **graph_**：当前执行的 GIR Graph。
- **FrameMeta**：包含 `frameSize`、`runtimeDataType`、`staticArea` 等，由 `installFrameMetaInfoForGraph(graph)` 根据图安装。

### 2.2 数据槽（Slot）

- **slot_t**：`uint64_t`，一个槽位可存整型、指针或其它装箱值。
- **data_idx_t** 约定（与 GIR 一致）：
  - **正数**：动态区（运行时参数、局部）索引。
  - **负数**：静态区索引的相反数（图内编译期常量）。
  - **0**：无效/空。
- **dynamicArea_**：动态区槽位数组；**staticArea_**：静态区（Tuple），与图的 `staticDataArr_` 对应。
- **get\<T\>(index) / set(index, value)**：按 data_idx_t 读写槽位；正负分别映射到动态区/静态区。
- **codeAt(index) / typeAt\<T\>(index)**：取槽位的 TypeCode 或 Type*，用于 ACCS、FILL、OPER 等运行时类型分发。

### 2.3 调用栈

- FastVM 使用 **pcStack_**、**frameStack_** 实现栈式调用与尾调用（TAIL）。
- 注释提到「FastVM 的优化依赖于复用刚刚释放的栈帧数据」，说明帧池或复用逻辑在 `FramePool` 等处。

## 3. Context

- **Context**（`core/context/context.h`）持有模块、全局作用域、类型解析等，贯穿编译与执行。
- 各 Pass 和调度器通过 `context_ptr_t` 访问上下文，用于查找算子、类型、模块等。

## 4. 调度器类型

### 4.1 GraphSchedulePass

- 基类：`execute/sched.h` 中的 **GraphSchedulePass**，继承 **GraphIRPass**，接口为 `apply(graph, os)`，返回（可能改写后的）图。
- 具体调度器实现「如何执行图」：线性调度会驱动解释器或字节码 VM，并行调度会构建 DAG 并提交任务。

### 4.2 线性调度（Linear）

- **FastVMSchedPass**（`builtin/passes/sched/linear/fastvm/`）：
  - **precompile**：将 GIR 编译为字节码（`compile.cpp`），可选优化（`optimize.cpp`）。
  - **call**：用 **gotovm** 解释执行字节码，或（若启用 JIT）走 JIT 入口。
  - 字节码定义在 **bytecode.h**：定长（RETN, JUMP, LADD 等）、变长（BRCH, FUNC, OPER 等），布局为 Header + operands + Extra。
  - 尾调用（`TAIL`）的合法性判据见 [19_tail_call_optimization.md](19_tail_call_optimization.md)。
- **NodeVMSchedPass**（`builtin/passes/sched/linear/nodevm/`）：
  - 不生成字节码，**getTopoNodes** 得到拓扑序，按序 **eval** 各节点（Data/Port/Copy/Fill/Accs/Func/Call/Oper 等），直接操作 Frame 槽位。
  - 对部分算子（如 map_arr、reduce_arr）有专门分支，用于数组等复合类型的语义。
  - **FUNC** 尾调用优化与 FastVM 对齐（同一文档）。

### 4.3 并行调度（Parallel）

- **TaskflowExecSchedPass**（`builtin/passes/sched/parallel/taskflow/`）：
  - 将 GIR 转为 **Taskflow** 的 DAG，每个节点或一组节点对应一个 Task。
  - **buildExitTask / buildDataTask / buildPortTask / buildCopyTask / buildFillTask / buildAccsTask / buildFuncTask / buildCallTask / buildOperTask** 等为不同 NodeType 建任务。
  - **buildBranchJoinRegion** 处理 BRCH/JOIN；**connectDependencies** 按图边建立任务依赖。
  - 通过线程池执行，实现「显式依赖、隐式并行」。

## 5. 算子与类型分发

- **operator_t**（`core/operator.h`）：算子索引类型，与 GIR 的 **OperNode** 对应。
- 运行时根据操作数类型选择具体实现（如整型加法、浮点加法），OPER 字节码或 NodeVM 的 **evalMarkedOperator** 会调用到这些实现。
- **FunctionType**：函数类型（with 参数、普通参数、返回类型），与 Graph 的 **funcType_** 一致，用于调用约定与类型检查。

## 6. 数据与类型（Data / Type）

- **Data**（`core/data/`）：运行时值（primary、composite、special 等），与 GIR 的静态数据、槽位中的装箱值对应。
- **Type**（`core/type/`）：类型系统，包含复合类型、函数类型等；GIR Node 的 **dataType_** 指向这些类型。
- **core/rtdata/**：运行时数据结构（array、string、struct、tuple、func 等），供 ACCS、FILL、CALL 等使用。

## 7. 小结

- **Frame** 是单次图执行的「栈帧」，槽位与 GIR 的 data_idx 约定一致；**Context** 提供全局环境。
- **线性执行**：FastVM（字节码 + 可选 JIT）或 NodeVM（直接拓扑序执行图）。
- **并行执行**：Taskflow 将图转为 DAG，按依赖调度到多线程。
- 算子与类型在运行时通过 **operator_t** 与 **TypeCode/Type** 分发，与 GIR 的 OPER 节点和类型信息一致。
