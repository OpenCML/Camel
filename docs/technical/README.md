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
| [07_jit_architecture.md](07_jit_architecture.md) | FastVM JIT 模块架构设计 |
| [08_fastvm_bytecode_semantics.md](08_fastvm_bytecode_semantics.md) | FastVM 字节码执行语义（BRCH/JOIN 等与解释器一致约定） |
| [09_gir_cpp_fib_lowering.md](09_gir_cpp_fib_lowering.md) | GIR 到 C++ 的 `fib` 特化 lowering 契约：适用的节点子集、逻辑帧与物理帧、节点到 C++ 的映射、何时回退到运行时路径 |
| [09_operator_registration.md](09_operator_registration.md) | 算子注册：OperatorGroup 与类型解析器、URI 格式与 Executor 分派、纯度等语义性质登记、全局内置模块与 `.cmo` 模块的注册流程 |
| [10_source_mapping_and_debug_locations.md](10_source_mapping_and_debug_locations.md) | 源码映射、Origin 派生链，以及 GCT/GIR/Runtime 如何回到源码区间 |
| [11_semantic_models_and_reverse_mapping_foundation.md](11_semantic_models_and_reverse_mapping_foundation.md) | AST/GCT/GIR 节点语义、层间 lowering、基础缺口审计与相对精确逆映射底座 |
| [12_namespace_convention.md](12_namespace_convention.md) | **命名空间规范**：路径与命名空间映射、别名约定、新代码规则及迁移状态 |
| [13_macro_system_progress.md](13_macro_system_progress.md) | 宏系统重构阶段性进展、风险与后续路线 |
| [14_funcnode_runtime_convergence.md](14_funcnode_runtime_convergence.md) | FuncNode 运行时收敛方案与迁移要点 |
| [15_gir_graph_gc_milestone.md](15_gir_graph_gc_milestone.md) | GIR Graph 垃圾回收里程碑与阶段成果 |
| [16_inline_rewrite_algorithm.md](16_inline_rewrite_algorithm.md) | `std::inline` 重写算法、SCC 调度、Graph API 协作与不变量 |
| [17_graph_rewrite_api_contracts.md](17_graph_rewrite_api_contracts.md) | Graph 重写 API 契约：事务边界、可写域、内联入口规则与验证器约束 |
| [18_sync_control_semantics.md](18_sync_control_semantics.md) | 同步控制语义解读：`BRCH/JOIN/GATE` 分工、`EXIT` 锚点规则与后端契约 |
| [19_tail_call_optimization.md](19_tail_call_optimization.md) | 尾调用优化（TCO）：以 NodeVM 为主的机制说明（拓扑、判据、帧与尾调路径）；附录含 FastVM 对照 |
| [20_macro_rewrite_refactor_plan.md](20_macro_rewrite_refactor_plan.md) | `std::macro` 系统性重构方案：重写工作区、共享执行语义与迁移顺序 |
| [21_gcgraph_runtime_bridge.md](21_gcgraph_runtime_bridge.md) | `GCGraph` 运行时桥接层：compile/runtime graph 分层、materialization 入口与过渡形态 |
| [22_gcgraph_native_layout.md](22_gcgraph_native_layout.md) | `GCGraph` / `GCNode` 原生布局：只读运行时图规则、节点标识与索引宽度、统一节点块与边区、分支臂区、两阶段 materialization |
| [23_gc_runtime_lifetime.md](23_gc_runtime_lifetime.md) | 运行时 GC 生命周期模型：四个分配域及其所有权、可增长的程序空间、追踪契约、根来源、安全点、写屏障与外部资源契约 |
| [23_graphdraft_shared_core.md](23_graphdraft_shared_core.md) | `GraphDraft` 作为唯一可编辑图表示：目标对象模型、所有权划分、与 `GCGraph` 之间的 encode/decode 契约、编译期构建器方向 |
| [24_gc_verification_guide.md](24_gc_verification_guide.md) | GC 验证指南：测试组织、`std::gc::*` 遍、环境变量开关、标准验证命令、分阶段检查与故障排查 |
| [25_tensor_stack_and_onnx_export.md](25_tensor_stack_and_onnx_export.md) | Tensor stack design: typed kernels, single operator registry (OpDef), refined TensorType, graph optimization, ONNX export, benchmark harness |
| [26_autodiff_as_graph_rewrite.md](26_autodiff_as_graph_rewrite.md) | Automatic differentiation as graph rewriting: pullback transform, derivative registry, tree models and optimizers |
| [27_entry_parameters_and_pure_export.md](27_entry_parameters_and_pure_export.md) | 带参数的 `main`：入口图的参数、从外部绑定实参、未导入模块的遍，以及对纯程序执行 `onnx::export` |
| [28_generic_functions_and_type_functions.md](28_generic_functions_and_type_functions.md) | Generic functions (`typeas`) as subgraph templates instantiated per set of argument types; type declarations with parameters as type functions |
| [29_nullable_and_fallible_values.md](29_nullable_and_fallible_values.md) | `T?` and `T!`: where the one-bit tag lives, `??` / `!!` / `?->` / `match` patterns as branches, `throw` as an exit, and the questions left to the language author |
| [30_diagnostics_reporting.md](30_diagnostics_reporting.md) | 诊断报告：诊断携带的字段、文本形式、一次编译报告多个错误、运行期故障、警告、诊断目录，以及留给语言作者的问题 |
| [31_map_and_set.md](31_map_and_set.md) | The built-in `Map<K, V>` and `Set<T>`: the two implementation routes and the one taken, key types, key order, the run-time table and how the collector traces it, the operators, typed JSON decoding, measurements, and the questions left to the language author |
| [32_semantic_query_layer.md](32_semantic_query_layer.md) | How a tool asks the compiler about a source text by position: the syntactic index, the facts the builders report to a recorder, the join, generic instances and overloads, what is available in text with errors, rename, and the measured cost |
| [33_fault_isolation.md](33_fault_isolation.md) | The fault-isolation operator `attempt` (name provisional): its type and graph form, which faults it isolates, how each back end returns to the boundary and what is restored, parallel siblings, release of external resources when a run ends, and the questions left to the language author |
| [34_handwritten_parser.md](34_handwritten_parser.md) | 手写的词法分析器、解析器与具体语法树：两条排版规则（尖括号紧贴、换行结束语句）、文法判定表、错误恢复、与 ANTLR 解析器的差分结果、性能数据 |
| [35_debugger.md](35_debugger.md) | The debugger `camel-db`: the source view and the graph view of a stopped program, line and node breakpoints and steps, the stack on both back ends, variables by source name, faults, the execution observer in the schedulers and its cost, the Debug Adapter Protocol coverage, and the questions left to the language author |
| [36_gc_handle_rules.md](36_gc_handle_rules.md) | Handle rules: what C++ code (operators, schedulers, passes) may do with run-time objects under a collector that moves them: safepoints, roots and `RootHandle`, write barriers, foreign resources and finalizers, the audit of the repository, and the safepoint-poisoning mode that finds violations |
| [37_generational_gc_report.md](37_generational_gc_report.md) | The generational collector as an option (`--gc generational`): what was fixed to make it sound, test results in the three configurations, measurements against mark-sweep, and the recommendation on the default |
| [38_compile_pipeline_cost.md](38_compile_pipeline_cost.md) | 编译流水线的开销：阶段计时（`--time-phases`）、计数分配器与剖析脚本，各阶段的时间与分配、热点归因、多模块的增长，GCT 改为 arena 中 32 字节节点前后的数字，以及按收益排序的后续重构清单 |
| [nn-autograd.md](nn-autograd.md) | `nn` / `tensor` 自动求导架构：`nn.apply_gradients` 的图合成、helper 调用与参数别名、公共 API、内置 VJP 覆盖范围与已知限制 |
| [jit/README.md](jit/README.md) | FastVM JIT 实现级注解的目录索引 |
| [jit/bytecode-to-asm.md](jit/bytecode-to-asm.md) | JIT 字节码到机器码翻译注解：槽模型、调用约定、逐条字节码经 MIR（V* 虚拟寄存器）与寄存器分配生成 x64 汇编 |
| [jit/optimization-architecture.md](jit/optimization-architecture.md) | JIT 基础优化与架构改进方案：发射时状态、发射缓冲（MIR）与优化 pass、完整 MIR 三层方案 |

编号 09 与 23 各对应两篇文档（`09_gir_cpp_fib_lowering.md` 与 `09_operator_registration.md`；`23_gc_runtime_lifetime.md` 与 `23_graphdraft_shared_core.md`）。两组文档主题互不相关，引用时请使用完整文件名。

## 相关文档（docs 目录）

- [../language/README.md](../language/README.md) — **语言参考（详细版）**：按主题分章，含大量示例
- [../setup.cn.md](../setup.cn.md) / [../setup.en.md](../setup.en.md) — 开发环境配置
- [../cli.cn.md](../cli.cn.md) / [../cli.md](../cli.md) — CLI 使用说明
- [../debugging.cn.md](../debugging.cn.md) / [../debugging.en.md](../debugging.en.md) — 调试 Camel 程序（命令行与 VS Code，单步在 Camel 里的含义）
- [../../tools/debugger/README.md](../../tools/debugger/README.md) — 调试器（camel-db）的 Web 界面与按类型断点 API（libcamel `DebugBreakpoint`）
- [../Camel.asdl](../Camel.asdl) — AST 结构定义（ASDL 格式，实现用）

## 术语速查

- **CST**：具象语法树（Concrete Syntax Tree）
- **AST**：抽象语法树（Abstract Syntax Tree）
- **GCT**：图构造树（Graph Construct Tree）
- **GIR**：Graph IR，图中间表示
- **FastVM**：基于字节码的线性调度虚拟机
- **NodeVM**：基于图节点直接遍历的线性调度器
- **Taskflow**：基于 DAG 的并行调度器
