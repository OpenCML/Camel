# Camel 技术总览

## 1. 项目简介

**Camel** 是一种采用现代 C++ 技术栈实现的、**以图为核心**的编程语言。程序在编译期被转换为**图中间表示（GIR）**，运行时通过**图调度**执行：顺序由依赖决定，并行性由编译器与运行时从图中发现，而非由程序员显式编写。

- **前端**：基于 ANTLR4 的语法分析，产出 CST/AST。
- **核心**：AST → 图构造树（GCT）→ 图 IR（GIR），图是程序的原生表示。
- **执行**：支持线性调度（FastVM 字节码、NodeVM 直接遍历）与并行调度（Taskflow DAG）。
- **许可**：MIT；受国家重点研发计划支持。

## 2. 设计哲学（The Zen of Camel）

1. **Let the graph speak.** — 一切计算皆是意图之图。
2. **Graphs are primitives, not artifacts.** — 图是意图的原生形态，而非代码的副产品。
3. **Imperative for operators, declarative for structure.** — 用声明式描述「做什么」，用命令式描述「怎么做」。
4. **Async by nature, sync when explicit.** — 顺序由依赖产生，而非由语法硬编码。
5. **Explicit dependencies, implicit parallelism.** — 并行性由系统发现，而非手写。
6. **Keep code pure, let runtimes specialize.** — 上下文在运行时注入，而非写死在代码里。
7. **Execution is polymorphic and multi-staged.** — 图可被解释、编译、分阶段执行或重写。
8. **Let compilers bear the burden.** — 调度、映射、融合、 lowering 由编译器承担。

## 3. 核心概念

### 3.1 图即程序

- 函数体被编译为一张 **Graph**，节点表示数据/操作/控制，边表示数据依赖（Norm/With）或控制依赖（Ctrl）。
- 子函数对应子图（SubGraph），通过 FUNC/CALL 等节点与主图连接。
- 图的拓扑序与数据流决定执行顺序，无显式顺序的节点可并行。

### 3.2 多态执行

- **线性执行**：FastVM（字节码 + 可选 JIT）、NodeVM（按拓扑序遍历节点）。
- **并行执行**：Taskflow 将图转为 DAG 任务图，按依赖调度到线程池。
- 通过 `-S`/`--scheduler` 选择调度器类型。

### 3.3 类型与算子

- 类型系统包含：基本类型、复合类型（List/Dict/Tuple/Struct）、函数类型（含 with 参数、修饰符等）。
- 运算符在 GIR 中对应 **OperNode**，绑定到 `operator_t`，由运行时根据类型分发。
- 支持 **Inner/Outer/Graph** 等实现标记（ImplMark），用于多态与特化。

## 4. 技术栈概览

| 层次 | 技术 |
|------|------|
| 语法解析 | ANTLR4（OpenCML.g4 / OpenCMLLex.g4） |
| AST | 自研 AST，结构由 docs/Camel.asdl 描述 |
| 图构造 | GCT（Graph Construct Tree）→ GIR Builder |
| 图 IR | GraphIR 命名空间：Graph、Node 及多种节点类型 |
| 调度 | GraphSchedulePass：Linear（FastVM/NodeVM）、Parallel（Taskflow） |
| 字节码 | 自研密集 opcode（定长/变长），见 bytecode.h |
| 运行时 | Frame、Context、Data、Type、operator_t、FunctionType 等 |

## 5. 代码结构速览

```
src/
├── parse/          # 词法/语法、CST、AST
├── compile/        # GCT、GIR、GIR Builder
├── execute/        # Pass 基类、调度接口、变换
├── builtin/passes/ # 内置 Pass：trans（dot/tns）、sched（linear/parallel）
│   └── sched/linear/fastvm/   # 字节码、编译、解释、JIT 骨架
├── core/           # 类型、数据、算子、模块、内存、上下文
├── error/          # 诊断、监听器
├── codegen/        # 源码生成（如格式化/反生成）
└── main.cpp        # 入口、参数解析、命令分发
```

更多细节见各分篇文档。
