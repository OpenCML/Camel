# 编译流水线

## 1. 总体流程

从源码到执行的大致阶段如下：

```
源文件 (.cml)
    → 词法/语法分析 (ANTLR4)
    → CST（具象语法树）
    → AST（抽象语法树）
    → 图构造树 (GCT)
    → 图中间表示 (GIR)
    → 图优化 / 变换 (可选)
    → 调度 (Linear / Parallel)
    → 线性：GIR → 字节码 → FastVM 解释 或 JIT
    → 并行：GIR → Taskflow DAG → 多线程执行
```

- **CST**：保留语法细节，可由 `camel inspect --cst` 等查看。
- **AST**：由 AST Builder 从 CST 构建，结构见 Camel.asdl；`camel inspect --ast` 可打印。
- **GCT**：图构造树，AST 到 GIR 的中间表示；`camel inspect --gct` 可打印。
- **GIR**：最终图 IR；`camel inspect --gir` 可打印。拓扑序等可由 `--topo-node-seq` 查看。

## 2. 各阶段说明

### 2.1 词法与语法

- **词法**：`antlr/OpenCMLLex.g4`。
- **语法**：`antlr/OpenCML.g4`。
- 解析器由 ANTLR4 生成（如 `parse/antlr/OpenCMLParser.h`），产出 CST。
- 入口与错误处理见 `main.cpp`、`parse/parse.h`、`error/listener.h`。

### 2.2 CST → AST

- **ast_builder**：遍历 CST，构造 AST（Stmt/Data/Type 等）。
- AST 节点定义在 `parse/ast/`（stmt.h, data.h, type.h, other.h 等）。
- 语义信息（符号、类型）在后续阶段或与 GCT 构建时结合。

### 2.3 AST → GCT

- **GCT Builder**（`compile/builder/gct_builder.cpp`）将 AST 转为 **图构造树（GCT）**。
- GCT 节点类型（LoadType）：DECL, FUNC, DATA, TYPE, NREF, DREF, VARI, WAIT, LINK, WITH, ACCS, BRCH, CASE, CAST, ANNO, EXIT, EXEC, EXPT 等。
- GCT 仍是树形结构，但已按「图构造」的语义组织（声明、链接、分支、退出等）。

### 2.4 GCT → GIR

- **GIR Builder**（`compile/builder/gir_builder.cpp`）遍历 GCT，生成 **GraphIR** 命名空间下的 **Graph** 与 **Node**。
- 每个 GCT `LoadType` 对应一个 `visitXxxNode`：递归访问子节点、创建 GIR 节点、用 `Node::link(LinkType, from, to)` 建立边。
- 典型模式：单子节点（如 ACCS、CAST、VARI、WAIT）访问 `gct->at(0)` 得到值节点，创建目标 GIR 节点后以 `Norm` 边连接 `valueNode → targetNode`。
- 每个函数体对应一个 **Graph**，内含 **Node**（DATA, PORT, OPER, CALL, FUNC, BRCH, JOIN, EXIT, CAST 等）及 Norm/With/Ctrl 边。
- 子函数对应子图，通过 `Graph::addSubGraph` 等与根图关联。
- 源码映射不再停留在 AST tokenRange，而是通过 `SourceContext / OriginTable / DebugMap` 继续传播到 GCT、GIR、bytecode 与 runtime。详见 `10_source_mapping_and_debug_locations.md`。

### 2.5 图优化与 Pass

- **GraphIRPass**（`execute/base.h`）：对 GIR 做只读或改写，接口为 `apply(graph, os)`。
- **GraphSchedulePass**（`execute/sched.h`）：调度类 Pass，将图转化为可执行形式（线性序列或并行 DAG）。
- 内置 Pass 在 `builtin/passes/`：例如 trans（dot 可视化、topo-node-seq）、sched（linear、parallel）。
- 通过 `camel inspect -P --pass-until <n>` 可执行到指定图优化阶段。

### 2.6 线性调度：GIR → 字节码 → 执行

- **FastVM 路径**：
  - **compile**（`fastvm/compile.cpp`）：将 GIR 编译为字节码（Bytecode 序列）。
  - 字节码格式见 `bytecode.h`（OpCode、定长/变长、operands、BytecodeExtra 等）。
  - **gotovm**：解释执行（Computed Goto 分发表）。
  - 可选 **JIT**：热点字节码编译为 x86-64 机器码（见 07_jit_architecture.md）。
- **NodeVM 路径**：不生成字节码，直接对 GIR 做拓扑序遍历并执行节点（`builtin/passes/sched/linear/nodevm/`）。

### 2.7 并行调度

- **TaskflowExecSchedPass**（`builtin/passes/sched/parallel/taskflow/`）：将 GIR 转为 Taskflow DAG，按节点依赖在线程池上调度执行。

## 3. 与 CLI 的对应

| 阶段     | 查看方式示例           |
|----------|------------------------|
| Tokens   | `camel inspect --tok`  |
| CST      | `camel inspect --cst` |
| AST      | `camel inspect --ast` |
| GCT      | `camel inspect --gct` |
| GIR      | `camel inspect --gir` |
| 拓扑序   | `camel inspect --topo-node-seq` |
| 执行到某 Pass | `camel inspect -P --pass-until <n>` |

## 4. 小结

- 流水线是 **线性 + 分阶段**：源码 → CST → AST → GCT → GIR → 调度 → 字节码/直接执行或并行 DAG。
- **图** 从 GCT 阶段开始显式出现，到 GIR 成为程序的第一表示；后续所有执行与优化都基于 GIR（或由其派生的字节码/DAG）。
