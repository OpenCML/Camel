# 编译流水线

> **先读 [`../design-principles.cn.md`](../design-principles.cn.md)。** 它是语言作者对 Camel 根本定位与执行模型的规范性说明：与实现或其他文档冲突时以它为准；它没有覆盖或标为“未定”的问题，向语言作者提问，不要自行选择。
>
> 与设计原则的对应（第 4 节）：遍序列由用户在命令行上组合，语言核心不限制顺序，不同顺序可以给出不同结果；不指定遍时执行 `std::default`（即 `std::nodevm` 的别名，见 `src/execute/pass.cpp`）；标准优化遍不应把程序改错，这是软件质量要求而非语言约定；自动微分与导出都是建立在遍机制之上的库/转译遍。

## 1. 总体流程

从源码到执行的大致阶段如下：

```
源文件 (.cml)
    → 词法/语法分析 (手写解析器)
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

- **词法**：`src/parse/syntax/lexer.cpp`，记号表 `include/camel/parse/syntax/token_kinds.def`。
- **语法**：`src/parse/syntax/parser*.cpp`，每个函数对应一条产生式（语言参考 8.5）。
- 解析器是手写的递归下降解析器，产出 CST（`syntax::Tree`），见 [34_handwritten_parser.md](34_handwritten_parser.md)。
- 入口与错误处理见 `main.cpp`、`parse/parse.h`、`error/listener.h`。

### 2.2 CST → AST

- **ast_builder**：遍历 CST，构造 AST（Stmt/Data/Type 等）。
- AST 节点定义在 `parse/ast/`（stmt.h, data.h, type.h, other.h 等）。
- 语义信息（符号、类型）在后续阶段或与 GCT 构建时结合。

### 2.3 AST → GCT

- **GCT Builder**（`src/compile/gct/builder.cpp`）将 AST 转为 **图构造树（GCT）**。树由 `gct::Tree`（`include/camel/compile/gct/tree.h`）持有：节点是 arena 里 32 字节的记录，名字在树内驻留，模块存活多久树就存活多久，详见 [03](03_trees_and_structures.md) 4.7 与 [38](38_compile_pipeline_cost.md)。
- GCT 节点种类（`NodeKind`）：DECL, FUNC, DATA, TYPE, NREF, DREF, VARI, WAIT, LINK, WITH, ACCS, BRCH, CASE, CAST, ANNO, EXIT, EXEC, EXPT 等。
- GCT 仍是树形结构，但已按「图构造」的语义组织（声明、链接、分支、退出等）。

### 2.4 GCT → GIR

- **GIR Builder**（`src/compile/gir/builder.cpp`）遍历 GCT，生成 **GraphIR** 命名空间下的 **Graph** 与 **Node**。
- 每个 GCT `NodeKind` 对应一个 `visitXxxNode`：递归访问子节点、创建 GIR 节点、用 `Node::link(LinkType, from, to)` 建立边。
- 典型模式：单子节点（如 ACCS、CAST、VARI、WAIT）访问 `gct->at(0)` 得到值节点，创建目标 GIR 节点后以 `Norm` 边连接 `valueNode → targetNode`。
- 每个函数体对应一个 **Graph**，内含 **Node**（DATA, PORT, OPER, CALL, FUNC, BRCH, JOIN, EXIT, CAST 等）及 Norm/With/Ctrl 边。
- 子函数对应子图，通过 `Graph::addSubGraph` 等与根图关联。
- 源码映射不再停留在 AST tokenRange，而是通过 `SourceContext / OriginTable / DebugMap` 继续传播到 GCT、GIR、bytecode 与 runtime。详见 `10_source_mapping_and_debug_locations.md`。

### 2.4.1 多个模块：一个建图器、一张 root、三步加载

（设计原则 2.11、2.12；图模型设计稿 3.7、3.9、第 8 节。）

- **一个建图器**：程序的所有模块由同一个 `gir::Builder`（`Builder::of(context)`，`src/compile/gir/program.cpp`）建在同一张 `__root__` 上。每个模块是一个 `ModuleUnit`：它的 GCT、诊断与顶层作用域。模块顶层的值是 root 上的节点；函数各是各的图。
- **捕获绑定到值**（`src/compile/gir/captures.cpp`）：捕获端口记着它代表的节点（"来源"）。直接调用处与函数值处，调用者按同一个来源取它看到的值（`captureInto`），不在调用处按名字重找。直接调用的捕获在链接步按被调图的端口次序接线（`linkCaptureSites`，不动点）。
- **三步加载**：
  1. *声明*：`import` 只绑定名字。GCT 先声明本模块的类型（导出的类型立即导出），再把被导入的模块编译到 `CompileStage::Declared`，再建其余部分；之后 `Builder::declareModule` 建函数图（无函数体）、登记模板、顶层值与类型、填导出表。导出的值是 `ExportedValue`（`include/camel/compile/gir/value_entity.h`）。
  2. *建图*：入口模块的 `compile(Done)` 调 `Builder::buildProgram`：按导入次序建各模块的顶层声明；函数与值在第一次被用到时按需建（`ensureFunctionBuilt`、`buildTopLevelValue`，跨模块也是），所以顶层与书写顺序无关；正在建的值又被用到是 `ModuleValueCycle`。
  3. *链接*：捕获接线、入口调用、效应摘要的不动点（`settleEffectfulGraphs`）与依赖它的告警，逐模块报告。
- **名字**（`src/compile/gir/names.cpp`，类型在 GCT 的 `lookupType`）：一条优先级规则——局部 > 本模块（所在命名空间、外层命名空间、顶层；模块名可作限定）> 按名字导入的 > `using namespace` 带来的 > 内建。`using namespace` 按块生效：模块顶层、命名空间块（作为声明的 `scopeUsings`）、函数体里的块，GCT 树的旁表 `Tree::usingsOf` 记着每个块与顶层声明的 `using`。导入分 `ImportLayer::Named` 与 `ImportLayer::Ambient` 两层。

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
