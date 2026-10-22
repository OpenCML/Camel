# Camel 路线图

本文档是当前这一轮开发的目标与大纲：要把 Camel 推到哪里，分成哪些方面，每个方面做什么，哪些已经裁定，哪些还要先定设计。语义以 [`docs/design-principles.cn.md`](docs/design-principles.cn.md) 为准，本文档与它冲突时以它为准。

## 1. 目标

Camel 是图原生的通用编程语言，AI 是它首要的应用领域。

- **在 AI 方向**：能替换掉 Python 中表达模型的那一部分（独立使用或嵌入），后端接入现有的运行时。
- **在通用语言方向**：有一定的通用性——类型系统有足够的表达力，出错说得清，长时间跑得住，库够用，工具能让外人上手。

两个方向共用一条判据：**语法元素必须和图有稳定的映射关系。** 任何新特性先回答"它在图上是什么"。

## 2. 当前基线

- 开发分支 `fsh-dev`；默认测试集（`smoke` 加全部 `feat` 计划）2255 条，全部通过，没有遗留失败。
- 核心（`src/` 与 `include/`）约 9.8 万行，库（`modules/` 与 `stdlib/`）约 3.8 万行，工具（`tools/`）另有约 1.6 万行；42 个内建遍（另有 35 个别名）加 2 个由模块提供的遍（`tensor::fuse`、`onnx::export`）；四种执行后端（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`）以及到 C++ 的转译。
- 持续集成在推送（`main`、`develop`、`fsh-dev`）与合并请求时构建并运行默认测试集：Linux x64 与 Windows x64 必须通过，macOS arm64 是试验项，失败不阻断；打标签时另外构建发布包。

下面各项前的记号：`[定]` 语义已裁定，可以直接做；`[稿]` 要先出设计稿，经语言作者过目；`[问]` 还有未决的问题（见设计原则第 15 节）。没有记号的不涉及语义。

各项末尾的"（状态：……）"是 2026-10-12 对照代码与测试逐项核实的进度：`已完成` 后面给出证据所在（文件、测试计划或命令行选项）；`部分完成` 写明做了什么、还缺什么；`未开始` 表示代码里还没有对应的实现；`未核实` 表示在本仓库内无从核实。

## 3. 方面一：AI 与论文相关

目标：真实模型写得出来、导得出去，优化之后不算错。

- **模型支持的广度**：批量把公开的真实模型改写成 Camel；遇到拒绝或缺能力就补能力，不绕开。（状态：部分完成——`benchmarks/` 下已有 mlp、lenet、gru、resnet、transformer 等十一个模型，其中九个的训练梯度与 PyTorch 对照（`test/plans/feat/modules/autodiff.plan.toml`）；成批改写公开模型尚未进行，gru 与 gru_big 的训练步仍导不出）
- **图优化的正确性**（状态：部分完成——生成器与差分测试已有，保序原语与纯度声明各做了一部分，统一的效应与别名分析未开始，见下面各子项）
  - 通用的随机程序生成器加差分测试，覆盖字符串、集合、闭包、高阶函数（现有生成器只覆盖三种程序形状）。（状态：已完成，见 `test/tools/progen`、`test/plans/feat/opt/progen.plan.toml`）
  - 把"保持效应顺序"收进图改写接口，提供不能绕过的保序替换原语。近期修复的静默算错集中在五类根因：效应顺序丢失、把被写的变量当成单一的值、分支臂、宏的操作数被提前内联、无界增长。（状态：部分完成——已有保序的节点替换 `replaceNode`（`src/passes/opt/generic/rewrite.h`）与保序内联 `inlineCallInOrder`（`include/camel/passes/opt/inline.h`）；`GraphDraft` 上不保序的底层编辑仍可直接调用，并非不能绕过）
  - 统一的效应与别名分析，供各遍共用；分析结果的缓存与失效。（状态：未开始）
  - `[定]` 给标准库各模块的算子补上纯度声明。（状态：部分完成——内建算子与 `algo`、`hash`、`fs`、`json`、`math`、`process`、`para`、`random`、`re`、`sim`、`statistics`、`str`、`env`、`this`、`time`、`tensor`、`nn`、`autodiff`、`onnx` 的每个算子都有声明，由 `test/plans/feat/modules/stdlib_registry.plan.toml` 约束（`camel inspect --ops` 可查）；还差 `python`、`pyplot` 两个模块，它们的算子没有声明，按有副作用处理）
- **图的容量** `[定]`：单张图 65,535 的上限不放宽。查清是哪个遍把子图压平到超限（已知案例：gru 的一步需要 100,533 个边表项），让它在容量内工作。（状态：部分完成——上限没有放宽，超限是带位置的 `RuntimeDiag::GraphTooLarge` 或导出时的拒绝（`test/plans/feat/opt/generic.plan.toml`）；已查明压平到超限的是 ONNX 导出时不设上限的内联与展开（`benchmarks/README.md`），但它还不能在容量内工作）
- **下游工具链的适配**：导出器覆盖更多构造；与封闭语言近邻（Futhark、Dex、Relay 等）做同一批程序的实测对比。（状态：部分完成——导出能力见 `export_matrix.md`（119 个算子中 65 个前向算子、23 个梯度算子有 ONNX 下降）；与 Futhark、Dex、Relay 等的同批程序实测对比未做）
- **论文**：稿件与证据在归档仓库中维护，不在本文档展开。（状态：未核实——稿件与证据不在本仓库中）

## 4. 方面二：运行时核心

目标：不静默出错，出错说得清，长时间跑得住。

### 4.1 边界问题

- 堵住静默丢弃：列表推导得到空数组；`case 'a' | 'b'` 只认第一个模式；`??`、`!!` 的右侧被丢弃；非字面量模式抛内部错误。一律改成明确的编译期错误，或者按裁定实现。（状态：已完成，见 `test/plans/feat/compile/not_supported.plan.toml`、`test/plans/feat/compile/tagged.plan.toml`、`test/cases/linear/match/multi_pattern.cml`；同类的还有：没有 `case _` 的 `match` 是警告 `MatchWithoutDefault`，遇到未列出的值是带位置的 `MatchNotExhaustive`（`test/plans/feat/consistency/match.plan.toml`），无人使用的分支臂里的 `return`/`throw` 是警告 `ExitInUnusedBranch`（`test/plans/feat/compile/branch_exit.plan.toml`），函数值可以写在数组、元组、结构体字面量与分支臂里（`test/plans/feat/compile/function_value_positions.plan.toml`））
- 即时编译的整数除法没有除零检查；发布构建下断言为空，越界访问不检查。（状态：部分完成——即时编译的除零已是带位置的 `DivisionByZero`（`test/plans/feat/linear/faults.plan.toml`），数组下标与切片越界在发布构建下也报诊断（`test/plans/feat/consistency/faults.plan.toml`）；`ASSERT` 在发布构建下仍为空，只靠断言把守的检查依旧不生效）
- 四个后端的一致性：把运行期故障与递归用例铺到每个后端做差分测试。（状态：已完成，见 `test/plans/feat/consistency/`（故障、递归上限、取值、清理、`match` 五个计划））
- `[定]` 非尾递归使用可配置的上限；超限在所有后端上都是带位置的诊断，不得崩溃。（状态：已完成，见 `--max-recursion-depth`、`CAMEL_MAX_RECURSION_DEPTH`，`test/plans/feat/consistency/limits.plan.toml`）

### 4.2 类型系统（优先级很高）

- `[定]` 泛型函数按实参类型实例化：泛型函数是子图模板，每组具体类型实例化出一张子图。复用现有的专门化与类型重推断。（状态：部分完成——按实参类型实例化已实现并有测试，泛型函数可以跨模块导出，由导入它的模块各自实例化（`docs/technical/28_generic_functions_and_type_functions.md`、`test/plans/feat/compile/generics.plan.toml` 中的 `generic_exported` 各项，`stdlib/array.cml` 即这样导出）；泛型函数还不能当作函数值，导入的泛型函数体不能使用它所在模块的顶层值，实例放在哪个模块标为 `DESIGN-QUESTION(generic-export)`，待裁定）
- `[定]` 类型变量用 `typeas T` 就地绑定。（状态：已完成，见 `test/plans/feat/compile/generics.plan.toml`）
- `[定]` 类型函数用专门的类型表达式来写，在类型位置上用尖括号调用；带参数的类型声明就是类型函数。（状态：已完成，见 `type Pair<T> = (T, T)`，`test/plans/feat/compile/generics.plan.toml`）
- `[定]` `T?` 与 `T!` 两种专用的内建形式；错误值是固定的内建结构。（状态：部分完成——两种形式与内建的 `Error` 结构已实现并有测试（`docs/technical/29_nullable_and_fallible_values.md`、`test/plans/feat/compile/tagged.plan.toml`）；隐式转换、嵌套、`match` 的写法等十一处行为按建议实现，代码中标为 `DESIGN-QUESTION(nullable-error)`，待裁定）
- `[问]` 类型表达式语言的具体内容；预留运算符的含义；类型里的数值参数如何绑定与计算；`typeof` 用于常量值；`any` 的定位。（状态：未开始）
- 需要新建的基础：类型变量的表示与带绑定的匹配；带参数的类型声明的语法；内建复合类型的规范化；泛型函数体推迟到实例化时定型。（状态：部分完成——类型变量的表示与带绑定的匹配（`include/camel/core/type/typevar.h`）、带参数的类型声明的语法、函数体推迟到实例化时定型已有；内建复合类型的规范化未做）

### 4.3 语言特性

- `[定]` `??`、`!!`、`?->` 实现为图上的分支；`throw` 实现为返回错误值；`try`/`catch` 从语法中移除并给出指引性的诊断。（状态：部分完成——三个运算符、`throw` 与对 `try` 的指引性诊断 `TryCatchNotInLanguage` 已实现并有测试（`test/plans/feat/compile/tagged.plan.toml`）；`throw` 接受什么、`?->` 能否用于 `T?`、`??`/`!!` 右侧的类型按建议实现，标为 `DESIGN-QUESTION(nullable-error)`，待裁定）
- `[定]` 故障隔离算子（名称暂定 `attempt`）与终止前的清理。（状态：部分完成——终止前的清理已做（刷新输出、释放外部资源，`test/plans/feat/consistency/cleanup.plan.toml`）；隔离算子 `attempt` 未实现）
- `[稿]` 循环：`for`/`while` 语法与图上的循环节点。（状态：部分完成——设计稿已写出（`docs/design/loops/README.cn.md`），尚待语言作者裁定；`for`/`while` 语法与循环节点都没有实现）
- `[定]` 映射与集合作为内建类型，用数组字面量加 `as` 构造。（状态：部分完成——`Map<K, V>` 与 `Set<T>` 已实现并有测试（`docs/technical/31_map_and_set.md`、`test/plans/feat/compile/collections.plan.toml`）；键的类型、顺序、重复键、相等与打印、下标、可空的值、调用形式、持久化结构按建议实现，标为 `DESIGN-QUESTION(map-*)`，待裁定）
- 解构（结构体、数组）、默认参数、关键字参数、F 字符串：语法已有，尚未实现。（状态：未开始）
- 语言参考文档与实现对齐：现有十余处写了但没有实现，若干处实现了但没有写。（状态：已完成，见 `docs/language/` 各章与英文版 `docs/language/en/`；文法接受而编译器拒绝的写法汇总在第 8 章 8.6，每个示例都实际运行过）

### 4.4 图 API

- 保序替换原语（见方面一）；改写接口文档与代码对齐（文档仍在用已经不存在的名字）。（状态：部分完成——保序原语的状态见方面一；`docs/technical/17_graph_rewrite_api_contracts.md` 已改用 `RuntimeGraphDraftSession` 等现名，但仍提到代码中不存在的 `GraphBuilder` 与 `detail::NodeMutation`）
- 遍的前置与后置条件的声明；分析管理器。（状态：未开始）
- 各遍改为工作表算法，建立大图的编译时间基准（已知有二次方行为）。（状态：未开始）

### 4.5 诊断系统

- 一次报告多个错误（构建器里已有继续机制，但命令行把错误上限设成了 0）。（状态：部分完成——一次运行报告全部互相独立的编译错误，上限为 `--max-errors`（默认 20，`test/plans/feat/compile/diag_options.plan.toml`）；一个文件只报第一个语法错误，标为 `DESIGN-QUESTION(diagnostics)`，待裁定）
- 带代码片段、下划线和次要位置的呈现。（状态：已完成，见 `--error-format text`，`test/plans/feat/compile/diag_render.plan.toml`）
- 运行期的诊断码细分：兜底码原本被引用 147 次，消息应带上下标、长度、实际值。（状态：部分完成——内建算子与 `fs`、`json`、`re`、`time`、`math` 等模块的故障已有各自的码并带上实际值；兜底码 `RuntimeDiag::RuntimeError` 仍被引用 89 次，其中 `python` 模块 53 次）
- 语法错误专门化（原本只有一个码，正文是解析器的原文）。（状态：已完成，见 `include/camel/core/error/diagnostics/messages/syntax.h` 中的 16 个错误码）
- 调用栈回溯与内联链；警告的按码开关与升级为错误；错误码目录与 `--explain`。（状态：部分完成——警告的按码开关与升级为错误（`-Wno-<name>`、`-Werror`、`-Werror=<name>`）、错误码目录（`docs/diagnostics.md`）与 `--explain` 已做；调用栈回溯与内联链未做）
- 把用户可触发的 C++ 异常与断言转成带位置的诊断。（状态：部分完成——内存耗尽、图超限、转译拒绝、遍内部的异常已是诊断（`MemoryExhausted`、`GraphTooLarge`、`TranslationRefused`、`PassInternalError`）；其余异常仍由命令行兜底为不带位置的文本，发布构建下的断言不生效）

### 4.6 内存管理

- `[定]` 分代回收：先审计模块并立下句柄规则；再在解释执行的后端上启用并做压力验证；即时编译等栈映射做好之后再打开。（状态：部分完成——会移动对象的年轻代回收可用 `CAMEL_GC_ENABLE_YOUNG_COPYING=1` 在解释执行的后端上打开，并有堆校验与压力测试（`test/plans/feat/mm/`）；默认关闭；模块审计与句柄规则没有成文；已知缺陷：`std::macro` 产生的对象常量在移动回收下通不过堆校验（`docs/technical/31_map_and_set.md` 第 11 节））
- 小对象堆可增长（现为固定 32 MB）；按存活量调节回收节奏；线程本地分配，去掉全局锁。（状态：部分完成——老年代满后小对象转入大对象空间，大对象空间按上次回收后的存活量调节回收节奏（`CAMEL_GC_GROWTH_PERCENT`，`test/plans/feat/mm/gc_pacing.plan.toml`、`test/plans/feat/mm/gc_elder_exhaustion.plan.toml`）；老年代本身仍固定为 32 MB，线程本地分配与去掉全局锁未做）
- 堆的上限与内存耗尽时的诊断；长时间与大堆的测试；堆快照与分配剖析工具。（状态：部分完成——图、永久、元三个空间可增长并有可配置的上限，耗尽时是带位置的 `RuntimeDiag::MemoryExhausted`（`test/plans/feat/mm/capacity.plan.toml`）；运行期的堆没有自己的上限；有约 190 MB 的大堆测试，没有长时间测试；堆快照有 `std::gc::snapshot`，分配剖析工具未做）

## 5. 方面三：非核心支持

目标：库够用，后端的覆盖一致，能和别的语言互通。

- **执行遍**（状态：部分完成——见下面各子项）
  - `std::jit`：恢复对算子调用、下标访问、函数值调用、跨图调用的编译（现在只编译纯算术加自递归的图，其余整张图退回解释器）；补除零检查与栈深探测；`[稿]` 栈映射与安全点，与分代回收一起设计；ARM64 后端。（状态：部分完成——除零检查与编译后递归的深度检查已做（`test/plans/feat/linear/faults.plan.toml`、`test/plans/feat/consistency/limits.plan.toml`）；含算子调用、下标访问、函数值调用、跨图调用的图仍整张退回解释器；栈映射与安全点的设计稿、ARM64 后端未开始）
  - `std::prl`：非 Linux 平台的栈耗尽防护；故障在并行任务之间的传播。（状态：部分完成——并行任务中的故障会终止整个程序并只报告第一个（`test/plans/feat/consistency/faults.plan.toml` 覆盖 `std::prl`）；Windows 与 macOS 上读取栈界的代码已写，未经编译与运行）
  - `std::fvm`：单张图的槽位上限（32,767）随"图的容量"一项一并处理。（状态：部分完成——超过 32,767 个槽位在任何构建下都被检出并终止，不再静默截断（`src/passes/sched/fastvm/bytecode.h`）；报出的仍是不带位置的文本，上限本身未随"图的容量"处理）
- **标准库** `[定]`：原语用 C++，组合逻辑用 Camel。（状态：部分完成——原语在 C++ 模块里（`modules/`），用 Camel 写的库已有 `stdlib/array.cml`（数组与高阶函数，建立在内建算子与 `algo` 的三个原语之上）；其余组合逻辑还没有用 Camel 写的库，见下面各子项）
  - 字符串（切分、修剪、大小写、替换、查找；`[定]` 按字节，另有码点算子）、数组与高阶函数、位运算：已有（`str`、`array`、`math`，见 `docs/stdlib-reference.md`）。未做：字节缓冲区、带比较函数的排序、`group_by`（等映射类型）。（状态：部分完成——`str`（C++）、`array`（Camel，`stdlib/array.cml`）与 `math` 的位运算函数已有，由 `test/plans/feat/modules/stdlib_values.plan.toml`、`stdlib_faults.plan.toml` 测试；字节缓冲区、带比较函数的排序（现有 `sort_by` 按键排序）、`group_by` 未做）
  - `math`、`statistics` 已补全；`time` 的补全；`fs` 补全与路径处理；子进程、套接字、日志；CSV、TOML、base64、压缩。（状态：部分完成——`math`、`statistics` 已补全（`docs/stdlib-reference.md`），`fs` 有 `try_read_text`、`read_chunk`、`is_file`、`is_dir`、`mkdir`、`mkdirs`；`time` 仍只有 `now`、`strftime`、`strptime`，路径处理、子进程、套接字、日志、CSV、TOML、base64、压缩未做）
  - 签名文件已与实现对齐并由测试约束（`scripts/stdlib-reference.mjs --verify-ops`），参考文档由签名文件生成；纯度已逐个算子声明并由测试约束。`nn` 等模块的签名文件未做。（状态：部分完成——11 个签名文件（`stdlib/signatures/`）与注册的算子一致（`scripts/stdlib-reference.mjs --verify-ops`），`docs/stdlib-reference.md` 由 `scripts/stdlib-reference.mjs` 生成（`--verify-doc`），纯度逐个算子声明（`camel inspect --ops`），三者都由 `test/plans/feat/modules/stdlib_registry.plan.toml` 约束；`nn`、`autodiff`、`onnx`、`sim` 没有签名文件）
  - 用 Camel 写的测试框架与断言库。（状态：未开始）
- **模块接口** `[定]`：整理出一层公开的 C++ 接口并按版本承诺兼容；纯度声明进入这层接口。（状态：未开始）
- **跨语言互操作**：Python 桥的自动类型转换；从 Python 里 `import camel` 运行图；C FFI。（状态：未开始）
- **转译到 C++**：支持数组、元组、结构体与闭包（现在只接受标量与字符串）；产物可独立部署。（状态：未开始）

## 6. 方面四：外部工具链

目标：外人能上手，能调试。

- **持续集成**：推送与合并请求触发，三个平台构建并运行测试；去掉发布包里的 `-march=native`；随后加入 sanitizer、覆盖率与性能基线。应最先落地，它是其他各项的安全网。（状态：部分完成——推送与合并请求触发，Linux x64 与 Windows x64 构建并运行默认测试集，macOS 为试验项、未经验证（`.github/workflows/ci.yml`）；发布包改用 `CAMEL_TARGET_ARCH=x86-64-v2`，不再是 `-march=native`；sanitizer 只有手动触发的一项；覆盖率与性能基线未做）
- **调试器** `[定]`：源码行与图节点两种视图，两种单步；变量查看与调用栈；实现标准调试协议。（状态：部分完成——已有的 `camel-db`（`tools/debugger`）提供图节点视图、节点断点与按节点单步；源码行视图与按行单步、变量查看、调用栈、标准调试协议未做）
- **语言服务器**：悬停、跳转到定义、补全、重命名、语义高亮、增量同步（现在只有诊断与格式化）。（状态：未开始）
- **编辑器支持**：语法高亮定义与 VS Code 扩展。（状态：未开始）
- **其他工具**：REPL；`camel test`；性能分析器；文档生成器；包管理器。（状态：部分完成——已有按阶段计时的 `-P`/`--profile` 与性能分析器 `camel-pr`（`tools/profiler`）；文档生成器只有生成标准库参考的 `scripts/stdlib-reference.mjs`，没有面向用户代码的文档生成器；REPL、`camel test`、包管理器未做）
- **文档**：入门教程、按任务组织的指南、标准库参考、通用示例集；设计原则的英文译本同步（目前落后于中文版）；`[定]` README 的定位表述更新。（状态：部分完成——入门教程（`docs/tutorial/tutorial.cn.md`、`tutorial.en.md`）、标准库参考（`docs/stdlib-reference.md`，由签名文件与库源码生成）、通用示例集（`examples/`，由 `test/plans/feat/examples.plan.toml` 逐个运行并核对输出）、中英文语言参考（`docs/language/`、`docs/language/en/`）、设计原则的英文译本（`docs/design-principles.en.md`）、README 的定位表述已完成；按任务组织的指南只有按任务分目录的示例集，没有成文的指南）

## 7. 依赖关系

- 类型系统（4.2）挡着：`T?`/`T!`、映射与集合、用 Camel 写的泛型库函数。应尽早开始。
- 持续集成（方面四）是所有并行开发的前提。
- 即时编译的覆盖面扩大与分代回收必须一起设计。
- 循环节点（4.3）在设计稿通过之前不动代码；语法部分不必等它。
- 统一的效应分析挡着循环类优化、逃逸分析与闭包去虚化。

## 8. 工作方式

- 语义问题按"Camel 应当是什么"提交语言作者裁定，不按现有实现将就；裁定写入设计原则文档后再据以开发。
- 每条工作线在独立的克隆与分支上进行，完成后经验收合回 `fsh-dev`；合入 `develop` 与发布按 [`agents/branching.md`](agents/branching.md) 执行。
- 每项改动附带测试；标准库里的优化遍不得把程序改错。
- 发现的缺陷如实记录并附最小复现；只修复明确的实现错误，不用临时规则掩盖。
