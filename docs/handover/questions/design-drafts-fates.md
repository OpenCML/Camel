# `docs/design/` 21 个设计稿目录的去留（交决策者审，2026-10-25）

文档清理分两步走，第二步要等图上的边重构与张量翻转合入，所以这一张表现在就要被看到。本页给出
**21 个目录逐个的归类、依据与处置**。归类只有三种：**已落地**（结论并进设计原则或对应的技术页，
然后删稿）、**被取代**（直接删）、**进行中**（保留；稿子里已经不成立的段落删掉或改写）。

判据来自**代码与测试**，不采信稿子自己的说法；每一条依据给出文件、符号或用例。文中不写行号。
参考密度（引用了该目录的文件数，`git grep` 实测）：op-semantics 57、effects 43、gpu 42、
graph-model 26、arrays-memory 24、graph-store 23、cpp-standalone 19、generic-structure 9、
rewrite 8、io-concurrency-streams 7、training 6、showcase 5、blueprint 4、stdlib 4、public-repo 3、
tiles-targets 1、tiles-route 1、indexed-computation 1、incremental 1、native-subgraph-call 2、
ai-stack 0。

## 一、结论表

| 目录 | 稿子数 | 归类 | 依据（代码与测试） | 处置 |
|---|---|---|---|---|
| `ai-stack` | 1 | 被取代 | 全仓 0 处引用。它的基线清单把 `tiles` 的 `source`/`run`/`available`/`device` 当成现行 API，而 `@gpu` 与 `run`/`run_device` 已从代码、用例、`stdlib/tiles.cml` 里删除；它记作"缺"的结构化 `LOOP` 已在 `include/camel/runtime/graph.h` 的节点种类里。交付路线分别由设计原则 13、`tiles-route`、`tiles-targets`、`training` 承担 | 删（整目录） |
| `arrays-memory` | 2 | 进行中 | 通用机制已落到核心内存层（`mm::Block`、`mm::blockSpace()`、`std::mem::blocks()`）；设计原则 3.5、5.4、10.5 已把结论写进规范。未完成：`T[维度]` 全面取代 `Tensor` 类型名 | 保留两稿（`plan.cn.md` 是张量翻转的实施计划） |
| `blueprint` | 1 | 被取代 | 总纲把规则体系的"16 条待定"、`analysis`、`@kernel`、单图 65,535 容量写成现行，这些已分别由设计原则 5.5–5.10、`rewrite/v2.cn.md`、设计原则 6.6（`#kernel`）、设计原则 2.4（32 位编号、单图无上限）取代。唯一没有替代物的是第 4 节的红线清单 | 红线清单并进设计原则 1，删目录 |
| `cpp-standalone` | 1 | 进行中 | 转译器在 `src/passes/trans/cpp/*`，用例 `test/plans/feat/trans/cpp.plan.toml`；稿子自己的覆盖率表记着阶段 4（映射与集合）、阶段 5（张量）未做 | 保留 |
| `effects` | 2 | 进行中 | `edges.cn.md` 的三种边正在实现：控制边与 `SYNC` 仍在树上，设计原则 3.1 已写入该结论。`plan.cn.md` 的 `uses`/`pure` 在 `src/parse` 里没有对应的关键字，`include/camel/core/resource_semantics.h` 只是算子侧的资源声明数据（属算子语义，不是函数类型上的 `uses`） | 保留两稿 |
| `generic-structure` | 1 | 进行中 | `match type`、`static for`、`fields`、`static loop`、`t.[n]` 在 `src/compile/gir/builder_static.cpp`、`builder_when.cpp`、`src/parse/syntax/parser_expr.cpp`；`::<T>` 已被 `stdlib/{array,autodiff,graph,nn,onnx,optim,para,random,rules}.cml` 使用。剩余：rest 参数、`constas`、`StaticTuple` | 保留；删掉"单图节点上限 65,535"那一段 |
| `gpu` | 6 | 进行中（由 GPU 车道改写） | `tiles::compile` 已注册（`modules/tiles/src/region_pass.cpp`），测试遍 `tiles::test_outline*` 在 `region_test_pass.cpp`；放置标签 `#gpu`/`#cpu`、运行时形状、整段编译缓存、回传图类型未做 | 保留；本车道一个字不改 |
| `graph-model` | 1 | 进行中（未实施部分已被取代） | 稿子头注自己写明第 4、5、9、11 步与第 3 步第 4 片由 `graph-store/README.cn.md` 取代。已实施的部分仍是**在用依据**：`include/camel/runtime/graph.h`（冻结布局、节点种类、`LOOP` 卡片）、`loop.h`（不变量 I13 与 I18/I19）、`operator_table.h`、`symbol.h`、`src/compile/gir/{builder,captures,program}.cpp`、`test/plans/feat/compile/{module_load,module_root,namespace_rules}.plan.toml` 都按它的节次引用。节点集收敛（把节点种类收成 9 种）尚未做 | 保留（节点卡片与不变量仍是在用依据） |
| `graph-store` | 3 | 进行中 | 第 1–8 步已落地，见 `docs/technical/44_values_with_several_writings.md`、`45_graph_module.md`、`46_rules.md`；第 9 步 `std::cml` 未做，`plan-roundtrip.cn.md` 是它的续篇 | 保留三稿；改掉 `plan.cn.md` 里"`@gpu` 是 GPU 用例里的装饰器"那句 |
| `incremental` | 1 | 进行中 | 方向已定、中期；`std::incremental` 在代码里不存在 | 保留 |
| `indexed-computation` | 1 | 进行中 | 拟议的目标架构；它的计算图语义层已由 `op-semantics/README.cn.md` 第 1 节逐项对应并实现（`include/camel/core/index_semantics.h`、`operator_semantics.h`）；调度图与资源图仍只是拟议 | 保留 |
| `io-concurrency-streams` | 2 | 进行中 | `Stream` 只出现在 `stdlib/io.cml` 的注释里，标准库里没有流类型；`plan.cn.md` 把 14 条探针全部按"今天失败"记录 | 保留两稿 |
| `native-subgraph-call` | 1 | 已落地 | `src/execute/function_call.cpp` 的 `invokeFunction` 被 `src/builtin/operators/higher_order.cpp`、`modules/tiles/src/module.cpp`、`src/passes/test/native_call.cpp` 调用；`callsGraphs`/`elementCalls` 在 `include/camel/core/operator_semantics.h` 并由四个后端与 C++ 发射器读；用例 `test/cases/loops/native_callback.cml`、`test/cases/linear/jit/native_calls.cml`，宏路径 `test/cases/macro/hof_calls.cml` | 结论并进 `docs/technical/06_runtime_and_execution.md`，删目录 |
| `op-semantics` | 1 | 进行中 | 阶段 1、2、3a、3b、4 已实现：`include/camel/core/{operator_semantics,index_semantics,resource_semantics}.h`、`include/camel/runtime/{facts,definitions,writings,loop}.h`、`modules/*/semantics.cpp`，技术页 `09_operator_registration.md`、`40_graph_facts.md`、`41_resources_and_definitions.md`。剩余：Tiles 侧迁移与审计项 | 保留 |
| `public-repo` | 1 | 进行中 | 顶层没有 `internal/`，`src/` 未按层分目录 | 保留 |
| `rewrite` | 3 | 进行中 | 第一版设计仍是在用依据（`docs/technical/46_rules.md` 把它作为图存储第 8 步的设计）；第二版已定、按步实施（`stdlib/rules.cml` 里已有 `rule …::<T>` 的泛型律）；形式化验证未开工：`z3` 在 `src`、`include`、`modules`、`tools`、`cmake`、`conanfile.txt` 里零命中 | 保留三稿 |
| `showcase` | 2 | 进行中 | `showcase/` 下已有 `c1-linear-logistic`、`c2-mlp-mnist`、`c6-vae`、`d4-mcts`、`d5-tree-lstm`；第三批（GPU 路径）与第四批（切分与导出）未做 | 保留两稿 |
| `stdlib` | 1 | 进行中 | `stdlib/` 有 34 个 `.cml` 声明文件（起点是 3 个），声明成为唯一事实来源这一步在推进；`statistics` 未改名 `stats`；`data`、`frame`、`log`、`stream`、`arrow` 等模块不存在 | 保留 |
| `tiles-route` | 1 | 进行中 | `tiles::compile` 已注册并有测试遍；块级内核语言、符号形状编译、CPU 内核后端未做 | 保留；`@kernel` 改成 `#kernel`，删掉描述已不存在的 `attrDecoratorGroup("kernel", …)` 实现途径那段 |
| `tiles-targets` | 1 | 进行中 | 目标描述符、编译出的 CPU 内核、dispatch 检查未做；`DeviceStorage` 资源与 `retainHostValue` 已落地 | 保留；`@kernel`→`#kernel`，"revise the decorator"改成放置标签，"完成原生子图调用"改成已完成 |
| `training` | 6 | 进行中 | `modules/ckpt`、`stdlib/tree.cml`、`stdlib/optim.cml`、`modules/nn` 在；稿子的阶段表记着阶段 2–7 未完成（设备随机数、常驻单步、混合精度、显存与重计算、四卡） | 保留六稿 |

**统计**：已落地 1、被取代 2、进行中 18。

## 二、删掉的三个稿子：依据与代价

| 删掉的路径 | 依据 | 代价（读者失去什么） | 引用清零 |
|---|---|---|---|
| `docs/design/ai-stack/README.md` | 全仓 0 处引用；基线描述的是已删除的 API 与已实现的能力 | 失去 2026-10-15 那份现状清单与"自研还是集成"的调研。它的每一条基线都已过时，保留会让读者把已删除的 `run`/`run_device` 当成现行 | `git grep -F "design/ai-stack"` 0 命中；`git grep "ai-stack"` 0 命中 |
| `docs/design/blueprint/README.cn.md` | 依赖图与阶段排序已由各设计稿与看板承担；契约与红线由设计原则承担 | 失去"八个大块"的全貌图与依赖图。第 4 节的 8 条红线**已并进设计原则 1**（带强制机制），其余条目的现行说法在设计原则里都有更准确的版本 | `git grep -F "design/blueprint"` 0 命中；`git grep "blueprint"` 0 命中；`git grep "重构总纲"` 0 命中 |
| `docs/design/native-subgraph-call/README.md` | 调用服务已实现且四个后端与宏求值器共用；高阶算子建立在它之上；用例在 | 失去"故障与观察事件覆盖率仍不足"这一条待办，以及 Tiles 的 `tiles:exec` 在无 GPU 时改走该服务的草图。草图在 `tiles-targets` 里也有；覆盖率一条记在本表 | `git grep -F "design/native-subgraph-call"` 0 命中；`git grep "native-subgraph-call"` 0 命中 |

删除同时改掉的引用（逐处）：

| 被改的页 | 原文 | 改后 | 理由 |
|---|---|---|---|
| `docs/design-principles.cn.md` 1 | "当前的红线清单见 [重构总纲](design/blueprint/README.cn.md) 第 4 节" | 8 条红线逐条列出，每条带强制机制 | 结论并进规范，不留指向已删稿的链接 |
| `docs/design-principles.en.md` 1 | 同上（英文） | 同上（英文） | 同上 |
| `docs/design/arrays-memory/README.cn.md` 状态行 | "总览见 [重构总纲](../blueprint/README.cn.md)" | 删去该句 | 目标页已删 |
| `docs/design/io-concurrency-streams/README.cn.md` 状态行 | "依据：…；[重构总纲](../blueprint/README.cn.md)" | 删去该句 | 同上 |
| `docs/performance/graph-store-baseline.md` 首段 | "给图存储重构（[总纲](../design/blueprint/README.cn.md)）留一份当前的数字" | "给图存储重构留一份当前的数字" | 同上 |
| `docs/roadmap.cn.md` 开头 | "> 下一轮大规模重构的总指南见 [下一轮重构总纲](design/blueprint/README.cn.md)。" | 删去该行（下一段已经把设计原则与 `docs/design/` 的定位写清楚） | 同上；这一页属另一条车道的改写范围，这里只做链接修正 |
| `docs/design/training/README.cn.md` 相关文档 | 列表里有 `[原生算子回调子图](../native-subgraph-call/README.md)` | 删去该项 | 目标页已删；结论已进技术页 |
| `docs/design/tiles-route/README.cn.md` 第 10 节 | 评审输入列表里有 `docs/design/native-subgraph-call` | 删去该项 | 同上 |
| `test/cases/macro/hof_calls.cml` 注释 | "the function-call service … (docs/design/native-subgraph-call, item 7)" | "… (docs/technical/06_runtime_and_execution.md, the native operator callback service)" | 注释里的路径引用指向已删稿 |
| `docs/technical/06_runtime_and_execution.md` 第 5 节 | —— | 新增两条：调用服务（`invokeFunction`、调用作用域、四个后端与宏求值器）与高阶算子（`elementCalls` 与快路径），指向 `09_operator_registration.md` | 已落地稿的结论并进技术页 |

## 三、进行中稿子里删掉或改写的段落（原文 → 改后 → 理由）

1. `docs/design/generic-structure/README.cn.md` 第 7.9 节第 (4) 条
   - 原文："单图节点上限 65,535（graph-model R4）在展开大参数树时可能触及，展开结果应保持按子树分图，由内联遍按需合并"
   - 改后："展开大参数树会把图撑大，展开结果应保持按子树分图，由内联遍按需合并"
   - 理由：编号已是 32 位、单图不再有容量上限（设计原则 2.4），引用的那条"容量不放宽"也已撤回；保留数字会让读者按 16 位容量设计展开策略。
2. `docs/design/tiles-route/README.cn.md` 第 3、6.3、6.5、7、10 节共 12 处
   - 原文：`@kernel`（装饰器）
   - 改后：`#kernel`（标签）
   - 理由：块级内核是标准标签 `#kernel`（设计原则 6.6 与 13.1），不是模块提供的装饰器；`@name(...)` 这种旧标注形式已改成 `#[...]`（用例 `test/cases/compile/tags/err_old_annotation.cml` 说的就是这件事）。
3. `docs/design/tiles-route/README.cn.md` 第 6.5 节"实现途径（2026-10-19）"
   - 原文：模块导出 `attrDecoratorGroup("kernel", "tiles:kernel", …)` … 执行器把 `kernel` 映射到 `attrDecoratorIdentity`
   - 改后：标签由声明它的库定义、由读取它的遍实现；读取 `#kernel` 的遍与块级内核语言是尚未做的部分，没有遍读它时按 9.4 给警告
   - 理由：`modules/tiles` 里没有 `attrDecoratorGroup("kernel", …)`，那条实现途径不存在；而 `#kernel` 含义按标签机制读。
4. `docs/design/tiles-targets/README.md` "Public boundary" 与 "Implementation sequence"
   - 原文："Keep `@kernel`'s …"；"Finish backend-independent native subgraph invocation and its GC/fault tests."；"revise the decorator and inspection API together"
   - 改后："Keep `#kernel`'s …"；"… are done（指向技术页：NodeVM、FastVM、JIT 与 Taskflow 用同一个服务）"；"revise the `#gpu` placement label and the inspection API together"
   - 理由：同上两条；那句"完成原生子图调用"是唯一还挂在"待做"里的已落地项，会把已完成的步子再派一遍。
5. `docs/design/graph-store/plan.cn.md` 测量前言
   - 原文："`@gpu` 是 GPU 用例里的装饰器（`test/cases/modules/tiles/gpu_decorator.cml`）… 本计划按 `#tag { }` 块与节点标签的同一机制处理"
   - 改后："放置只写 `#gpu` 标签（`@gpu` 装饰器与显式运行算子已删除），标签是声明或节点上的编译期事实（6.2），本计划按标签的同一机制处理"
   - 理由：`@gpu` 已删除，`test/cases/modules/tiles/gpu_decorator.cml` 不在树上；`#tag { }` 这种写法也不是现行语法。

## 四、没有做、以及需要你裁的两处

- **`docs/design/gpu/` 一个字没改**（6 个稿子）：放置标签、驻留、整段编译这一块由另一条车道正在改它的 §2.4/§2.6。它的归类是**进行中**。稿子里仍写着 `@gpu`/`run`/`run_device` 的历史，应由那条车道在改写时一并清掉；本车道只报告，不动它。
- **`graph-model` 的正文没有清**：它的头注自己声明第 1 节的"现状"与 R4（单图容量不放宽）已被第二版取代，但正文里还有若干处按 65,535 讲故事（节点卡片、存储表示、实测数字）。没有动它们，原因是**节点集收敛的工作目标正是这一稿的 3.4.1**，现在改，落地后还得再改一次，且会与那条车道撞同一段文字。要不要现在就按"32 位、无上限"把正文里的数字一并改写，请你裁。
- **`rewrite/README.cn.md` 判为保留**：第二版设计稿在开头把它称作"第一版"并只作扩展，技术页也把它当图存储第 8 步的依据，所以没有当"被取代"删。若你认为第二版已经吃掉第一版，请裁；那时 `docs/technical/44_values_with_several_writings.md` 与 `46_rules.md` 的引用要一起改。
- **`docs/roadmap.cn.md` 只做了一行链接修正**（删掉指向已删稿的那句引用）。这一页属另一条车道的改写范围；如果那条车道同时改到这里，以它的版本为准即可。
- **`docs/design/effects/edges.cn.md` 与 `effects/plan.cn.md` 判为进行中**：三种边与资源声明都还没有落到树上，它们仍是这两件事的设计。等边重构与资源声明的施工落地，这两稿要按"已落地 → 并进设计原则与技术页 → 删"再走一遍。
- 技术页里描述控制边、同步、过渡张量类型与张量栈的几页**没有改**（按清理的分步安排，等边重构与张量翻转合入后再按实现改写）。