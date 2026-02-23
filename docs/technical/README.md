# Camel 技术文档

本目录为 Camel 编程语言的**技术文档**集合。Camel 是一种**以图为核心**的新型编程语言：程序被表示为图中间表示（GIR），计算由图的调度与执行完成，并行性由依赖关系隐式表达。

## 文档索引

| 文档 | 说明 |
|------|------|
| [00_language_reference.md](00_language_reference.md) | **Camel 语言参考**（类 Python Language Reference：词法、类型、执行模型、表达式、语句、文法概要） |
| [01_overview.md](01_overview.md) | 项目总览、设计哲学与核心概念 |
| [02_language_spec.md](02_language_spec.md) | 语言规范摘要（语法与语义，参考 test 推断） |
| [03_trees_and_structures.md](03_trees_and_structures.md) | 各种树与中间结构详解（CST、AST、GCT 及 GIR 视图） |
| [04_compilation_pipeline.md](04_compilation_pipeline.md) | 编译流水线：从源码到图与字节码 |
| [05_graph_ir.md](05_graph_ir.md) | 图中间表示（GIR）详解 |
| [06_runtime_and_execution.md](06_runtime_and_execution.md) | 运行时、虚拟机与调度器 |
| [07_jit_architecture.md](07_jit_architecture.md) | FastVM JIT 模块架构设计（原稿见 [../design/JIT_ARCHITECTURE.md](../design/JIT_ARCHITECTURE.md)） |
| [08_fastvm_bytecode_semantics.md](08_fastvm_bytecode_semantics.md) | FastVM 字节码执行语义（BRCH/JOIN 等与解释器一致约定） |
| [09_operator_registration.md](09_operator_registration.md) | 算子注册：OperatorGroup、DynamicFuncTypeResolver、Executor、URI 格式及注意事项 |

## 相关文档（docs 目录）

- [../language/README.md](../language/README.md) — **语言参考（详细版）**：按主题分章，含大量示例
- [../setup.cn.md](../setup.cn.md) / [../setup.en.md](../setup.en.md) — 开发环境配置
- [../cli.cn.md](../cli.cn.md) / [../cli.md](../cli.md) — CLI 使用说明
- [../../tools/debugger/README.md](../../tools/debugger/README.md) — 调试器（camel-db）与按类型断点 API（libcamel `DebugBreakpoint`）
- [../Camel.asdl](../Camel.asdl) — AST 结构定义（ASDL 格式，实现用）

## 术语速查

- **CST**：具象语法树（Concrete Syntax Tree）
- **AST**：抽象语法树（Abstract Syntax Tree）
- **GCT**：图构造树（Graph Construct Tree）
- **GIR**：Graph IR，图中间表示
- **FastVM**：基于字节码的线性调度虚拟机
- **NodeVM**：基于图节点直接遍历的线性调度器
- **Taskflow**：基于 DAG 的并行调度器
