# 架构审计（2026-11-13）

负责人担心代码增速过快会留下架构上的债务，要求先做一次审计，再把收敛排进主线。本文是审计结果：按收益排序的债务清单，加上以后防止再积债的规矩。施工安排见 `docs/handover/batches-2026-10-31-72h.cn.md` 第十节。

做法：决策者主持，五个只读子代理分别读重复实现、编译器、执行与调度器、模块与标准库、测试与仓库卫生；关键结论由决策者在 develop `b72d59535` 上复核。行数取 `git ls-files` 加 `wc -l`。

## 一、规模与增长

| 日期 | C++ | 测试 | 文档 | 全仓库 |
|---|---|---|---|---|
| 09-15 | 8.9 万 | 1.0 万 | 1.1 万 | 18.6 万 |
| 10-01 | 10.6 万 | 1.5 万 | 1.2 万 | 21.7 万 |
| 11-01 | 33.0 万 | 23.6 万 | 12.3 万 | 86.6 万 |
| 11-13 | 35.0 万 | 26.1 万 | 15.5 万 | 96.0 万 |

- C++ 里注释 17%、空行 7%，代码约 26 万行。
- 10 月以来增长最多：`src/compile` +4.4 万（到 5.1 万）、`include/camel` +3.1 万、`src/core` +2.4 万、`modules/transforms` +1.8 万（新）、`src/runtime` +1.6 万、`modules/tiles` +1.5 万（新）、`src/passes/test` +0.8 万（新）。
- 改动最频繁的文件（10-01 以来碰过它的提交数）：`src/compile/gir/builder.h` 244、`src/compile/gct/builder.cpp` 236、`src/compile/gir/builder.cpp` 214、语义诊断消息表 206、`src/execute/pass.cpp` 185。各路都往同几个中心文件里追加。

**判断**：体量本身不是问题；问题是只加不减。同一件事有两套实现，中心文件不断长大，层次之间互相引用，测试与过程文件混进产品。

## 二、债务清单（按收益排序）

### A. 正确性：调度器各自推导语义（最先做）

1. **taskflow 不守顺序边**（决策者核实）：在用的结构化计划里，`collectWaits`（`src/passes/sched/taskflow/taskflow.cpp`）只收控制、数据、`with`、同步四种前驱，没有 `orderPredsOf`；只有注释说"留给转储"的旧任务图读它。已写进返工（`reviews/questions-2026-10-29.md`）。
2. **根子上的问题**：线性后端（nvm、fvm、jit）从 `computeExecutionOrder` 取一次线性序再重放；taskflow 自己推导依赖。边的种类在 `exec_plan.cpp`、`graph_runtime_support.cpp`（5 处）、`fusion_group.cpp`、`pass.cpp`、`taskflow.cpp`（4 处）各列一遍，没有共用函数。**修法**：在 `src/execute` 里给出唯一的"执行前要等哪些前驱"，所有后端从它取。

### B. 同一件事的多套实现

| 项 | 规模 | 现状 | 处理 |
|---|---|---|---|
| 反向求导两套：引擎 `modules/autodiff/pullback.cpp` 与 C++ 求导规则表，对库的 `vjp` + `modules/transforms` + `stdlib/rulesets` | 引擎侧约 7600 行 | 344 个用例文件用引擎，118 个用库；11-01 台账里只走库能过的仅 12/127 | 冻结引擎（不再往 C++ 规则表加规则）；设一条长期车道把库推到对齐；对齐后整块删除引擎、规则表、`scripts/vjp-parity.mjs`、`autodiff-migration-ledger.mjs` |
| `fold`/`cse`/`dce`：C++（`src/passes/opt/generic/generic.cpp`）与 Camel（`stdlib/opt.cml`） | C++ 约 1600 行 | `std::opt` 只跑 C++ 的；Camel 版缺剪枝、静态存取折叠、类型折叠、快速合并 | 先补 Camel 版缺的原语，输出等价后删 C++ 版（11-07 定的迁移第一步） |
| `algebra.cpp` 的 `neutral_only` 参考模式 | 约 290 行 | 只有测试在用 | 直接删，连同只测它的用例 |
| 四个后端各写一份节点语义：Cast/Copy/Fill/Accs 的节点体有 6–7 份；`map`/`filter`/`reduce`/`foreach` 有 3 份（nvm、fastvm、taskflow）；FastVM 另有一份间接调用的帧绑定 `populateIndirectCallFrame`；尾调用判定有三种表示 | 约 2000 行可去 | 行为靠手工对齐，taskflow 已经出过偏差 | 节点体、高阶算子语义、帧绑定、尾调用判定各收成 `src/execute` 里的一份，后端只管调度 |
| taskflow 内部三套求值：结构化计划、逐个节点、旧任务图 | 旧任务图约 900 行 | 旧任务图仍被 `executeLinearNode` 的 `default:` 用到 | 查清那几种节点后删旧任务图，再把 `taskflow.cpp` 按 GC 闸门、计划、求值、高阶算子拆开 |
| 优化器：`modules/nn/optim.cpp` 与 `stdlib/optim.cml` | 约 600 行 | 两份都在 | 核实用例都走 Camel 版后删 C++ 版 |
| 第三个 JSON 导出器 `src/compile/gir/debug_json.cpp`；节点种类名表至少 4 份 | 约 300 行 | 只给调试器用 | 调试器改用 `gir.cpp` 的导出，名表收成一份 |

### C. 层次

现在的依赖有环（引用计数是 `#include` 处数）：
- `core → runtime` 21、`runtime → core` 124；`core → compile/parse/execute`（`src/core/module/userdef.cpp` 引 GCT/GIR 建图器与解析器，`src/core/context/context.cpp` 引执行器）；
- `runtime → compile/execute`（`definitions.cpp`、`rules.cpp`）；
- `execute → passes`：`pass.cpp` 直接引用每个遍的头文件（38 处），`passes → execute` 又反过来；
- 模块引 `src` 内部约 10 处（`modules/graph/txn.cpp` 引 `src/passes/opt/generic/rewrite.h`，`modules/rewrite` 两处引 `src/passes/sched/nodevm/nodevm.h` 等）；`src/builtin` 反过来引 `modules/math` 的头文件 3 处；
- 后端引工具层（fastvm、builtin 引 `service/profiler`）。

**处理**：
1. 定层次：`utils < core < runtime（图） < compile < execute < passes < builtin/service/modules`。
2. `Context` 与用户模块加载（`userdef`）上移到 `execute` 或新的 `driver` 层；`graph_id.h`、`entry_args.h` 下移。
3. 遍的注册改成各目录自带的登记表，`pass.cpp` 不再引用具体的遍。
4. `modules/math` 的 `minmax.h`、`mean.h` 移进核心；模块只用公开头文件。
5. CI 加一个引用检查（允许的边列成表），新的反向边直接红。

### D. 测试代码混在产品里

- `src/passes/test`（7900 行，45 个文件）、`src/core/**/*_selftest.cpp`（1300 行）、`pass.cpp` 里的自检遍，都编进 `libcamel` 随发布走。
- **处理**：编成单独的 `camel_test_passes` 库，只在测试时构建，经已有的 `registerModulePass` 登记。依赖上节第 3 条。

### E. 中心文件

| 文件 | 行数 | 混在一起的职责 | 拆法 |
|---|---|---|---|
| `src/compile/gct/builder.cpp` | 8398 | 模块与导入、声明、资源与标签推断、语句与控制流、表达式、字面量与类型收敛、`visitFuncData`（一个函数 1412 行）、类型表达式 | 按职责拆成 7–8 个文件；先把 `visitFuncData` 拆成签名、形参与默认值、函数体、捕获四段 |
| `src/compile/gir/builder.h` + 17 个 `builder_*.cpp` | 约 2.5 万 | 一个 `Builder` 类、约 86 个成员，按主题分文件只是藏起了大小 | 泛型、效应与资源、静态求值抽成各自的类，接口写明 |
| `src/parse/ast/builder.cpp` | 5589 | 语法树转换、改写、校验 | 按语句、表达式、类型、模式拆；工具函数另放 |
| `src/execute/pass.cpp` | 3310 | 自检、注册、别名、帮助文本、效应与边界检查（约 900 行）、改动比较、流水线警告 | 拆成登记、解析、边界检查（移到 `runtime/verify`）、警告、执行；执行本身不到 400 行 |
| `src/passes/sched/taskflow/taskflow.cpp` | 3310 | 见 B | 见 B |
| `modules/transforms/transpose.cpp`、`split.cpp`、`modules/onnx/exporter.cpp`、`modules/rewrite/module.cpp` | 5734、3989、5296、4345 | 拒绝文本、直线、控制流、驱动；求值器、递归识别、值的展开；规则核、引擎、解释、精度检查 | 按结构拆；`rewrite` 的精度检查（约 1700 行，又实现了一遍张量参考语义）单独成文件 |
| `src/runtime/graph.cpp`、`draft*.cpp`、`writings.cpp` | 3119；约 1 万 | 存储、编码、边索引、引用更新；`writings` 是独立的分析引擎 | 把已开头的拆分做完；`writings` 自成子目录 |

### F. 编译器：同一条规则在几层各查一遍

- GCT 与 GIR 两层都报的语义诊断有 21 个（决策者核实），例如 `DeclaredTypeMismatch`、`LiteralNotRepresentable`、`UnresolvedReference`、`UsesPromiseViolated`。同一条规则没有唯一的主人，迟早一处改了另一处没改。
- 资源与效应的推断分在三层：解析器的 `visitResourceClause`，GCT 的 `visitResourceDecl`/`resolveAmbientResource`，GIR 的 `builder_resources.cpp`、`builder_linearity.cpp`、`builder_tags.cpp`。
- 编译器按算子的名字做特例：`":struct/append_arr"`、`":struct/range"` 加 `name() == "range"`、`":op/slice_*"` 等约 15 处。
- **处理**：每条规则定一个归属层，删掉其他层的；**R3 改效应系统时，效应与资源的推断写成一个独立的分析，有写明的输入输出，而不是在三个建图器里各改一遍**；名字特例改成算子上的属性（"原地追加""切片类""区间类"），编译器查属性。

### G. 模块

- `tiles`（1.4 万）、`onnx`（1.1 万）、`ckpt` 无条件构建，`src` 里有约 8 处 tiles 的概念。改成可选构建目标；`sim`（玩具示例）删掉或移进 `examples`。
- 可迁到 Camel 的（按 11-07 的战略，排在本节之后）：`modules/transforms` 的 `jvp`、`vmap`、`remat` 驱动（约 3500–4000 行）；`onnx` 里把递归识别成循环的分析（约 2500 行，先拆出来）；`tensor` 的 `index_eval` 与 `fuse`（约 1650 行）。
- 模块之间以 `tensor` 为中心，各模块直接引 `tensor` 的内部头文件（`kernels/*`、`ops/registry.h`）；给 `tensor` 定一层公开接口（原则 10.5）。

### H. 仓库卫生（不碰产品代码，可以先做）

| 项 | 行数 | 处理 |
|---|---|---|
| `docs/handover/**` 过程记录 | 约 7.8 万 | 整体移到 `internal/` 或归档仓库；之前把仍有效的裁定写进原则或设计稿，代码与用例里引用它的 129 个文件改成引稳定的条目号 |
| `examples/models/naive-in-expert-out/results/*.json` | 34812 | 原始实验数据，移到归档仓库 |
| `benchmarks/`、`promo/`、`showcase/` | 约 5.6 万 | 研究与宣传材料，公开前移出或拆仓 |
| `docs/handover/stdlib/*.cml` 旧草稿副本 | 5748 | 删 |
| `docs/handover/reports/docs-error-system/` 文档树快照 | 8703 | 删 |
| 测试计划 | 9.8 万中约 2–2.5 万 | 计划级默认值（`normalizers`、`timeout_ms`、`kind`、`args`）、按后端与模式展开的矩阵字段、具名的校验函数（1712 段内联校验脚本里只有 953 段不同） |
| 一次性脚本与重叠工具 | 约 4000 | 删 `migrate-pack-dots.mjs`、`milestone-for-changes.mjs`；三个行数统计脚本留一个；文档检查合成一个入口 |
| 重复文件 | 少量 | 根目录与 `assets/` 的 `logo.png` 完全相同；`err_duplicate_head_param.cml` 与 `err_head_param_twice.cml` 相同 |
| 代码与标准库注释里的日期、裁定、交接路径 | 编译器约 100 行，标准库头注释约 1500–2000 行 | 设计理由移进原则或设计稿，注释只引稳定条目号，随改动逐步清 |
| 生成的文档（`docs/diagnostics.md`、`docs/stdlib-reference.md`） | 约 1.9 万 | 标明是生成的，或改由 CI 生成 |

## 三、以后不再积债的规矩

1. **每次合并报净增行数**，分 C++、测试、文档三栏，并列出动过的中心文件。删得比加得多的合并优先。
2. **中心文件设上限 2500 行**：超了的先拆再加；已经超的，碰到时顺手拆一块。
3. **一件事只有一套实现**：新写一套替代旧的，旧的在同一次合并里删掉，或者写明删除条件与车道；不留"兼容"与"留给转储"的旧路。
4. **新增 C++ 要说明为什么不能用 Camel 写**（11-07 已定）。
5. **规则只有一个归属层**：新诊断写在哪一层，其他层不再查同一条。
6. **层次用引用检查守住**，新的反向边是红灯。
7. **决策者每周做一次架构审查**：只看依赖、重复、边界与中心文件，不看局部写法；读数（C++ 净增、超限文件数、反向边数、重复项数）进 `decision-tools/pace.sh`。
