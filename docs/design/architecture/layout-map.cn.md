# 目录对照表（搬家脚本的输入）

依据：`docs/design/architecture/abstractions.cn.md` 第 9 节（仓库布局，已定）、
`docs/design/architecture/refactor-plan.cn.md` 第 1、2 节（阶段与车道）、
`docs/handover/plans/2026-11-22-refactor-stage0.cn.md` 0.5。

这张表是**搬家脚本唯一的输入**（`scripts/layout-move.mjs`）。脚本读它，产出 `git mv` 序列、`#include`
改写与每块的构建目标；脚本不执行，演练在临时树里跑（见 `scripts/layout-move.mjs --help`）。

表的写法有三条规矩，脚本按它们检查，缺一条就报错而不是跳过：

1. **表 A 的每一行对应工作树根下一个条目**，一个不多、一个不少。脚本拿 `readdir` 读出的顶层条目数
   与表 A 的行数相比，两者不等就退出 1（「漏了一行」和「多了一行」都是错）。
2. **表的「目标」是搬家后的路径**，`/` 结尾表示目录。`—` 表示这个路径不搬家。
   顶层的四个容器目录（`src/`、`include/`、`modules/`、`stdlib/`）在表 A 里只写一行「拆到表 B」，
   它们的子条目在表 B 里一一有行。
3. **状态只有两种**：`搬`、`留`。没有第三种：一个路径要么搬、要么留；原先的「待定」那一态
   已随表 C 的 19 项定论取消，脚本不再接受它。表 C 的 19 项已由决策者逐条定下（2026-11-22，
   依据核心抽象第 9 节与第 13–19 节），它的「目标」列就是表 A/B 相应行的出处。

块名取核心抽象第 9 节的五处：`kernel`、`parse`、`compile`、`lib/<模块>`、`tools`。
一个块的构建目标只把**下层**的头文件放进包含路径；`lib/<模块>` 之间只有声明过的依赖可以互相引用。

**构建口径的块名比这张表的「块」列更细，其中三块在表里没有行（2026-11-22 补）。**
`cmake/layout-blocks.cmake` 把 `kernel` 拆成六块，层序就是：`kernel_utils`、`kernel_core`、
`kernel_ops`、`kernel_facts`、`kernel_graph`、`kernel_runtime`。`kernel/ops/`（`camel/ops/operator_record.h`）、
`kernel/facts/`（`camel/facts/facts.h`）与 `kernel/graph/`（`graph_view.h`、`draft_view.h`、
`gc_graph_view.h`）是阶段 2.2 新建的纯头目录，**表 A、表 B 没有对应的搬家行**，
于是搬家脚本的 `blockOfTarget` 仍把这三个目录算成 `kernel_core`，脚本也不会产出这三个块：
它们在生成物里是手加的，`scripts/layout-move.mjs` 的 `LAYERS`、`deriveRoots`、`treesFor` 三处还
不认识它们。**重新生成这个文件前先把这三行补进表、再改脚本，否则重新生成会把手加的三块删掉。**

## 表 A：顶层对照（50 行 = 工作树根的 50 个条目）

| 现有路径 | 目标 | 块 | 状态 | 备注 |
|---|---|---|---|---|
| `.clang-format` | — | — | 留 | 格式化配置 |
| `.git` | — | — | 留 | 工作树里是指针文件、主检出里是目录；git 自身，不是仓库内容 |
| `.gitattributes` | — | — | 留 | |
| `.github/` | — | — | 留 | CI 配置，归合并位 |
| `.gitignore` | — | — | 留 | |
| `.husky/` | — | — | 留 | 提交钩子 |
| `.prettierrc` | — | — | 留 | |
| `.vscode/` | — | — | 留 | 编辑器工作区设置 |
| `ABOUT` | — | — | 留 | |
| `AGENTS.md` | — | — | 留 | |
| `agents/` | — | — | 留 | 施工规则 |
| `assets/` | — | — | 留 | 品牌图 |
| `benchmarks/` | — | — | 留 | 用户程序与训练脚本，不是模块 |
| `bindings/` | — | — | 留 | U18：CPython 扩展留顶层；`python::` 模块在 `lib/python/` |
| `changes.log` | — | — | 留 | |
| `cmake/` | — | — | 留 | 构建胶水与选项；U16 定了 `camel_module.cmake` 落这里 |
| `CMakeLists.txt` | — | — | 留 | 构建入口；脚本只按块改写它的四条钩子行 |
| `CODE_OF_CONDUCT.md` | — | — | 留 | |
| `conanfile.txt` | — | — | 留 | |
| `CONTRIBUTING.md` | — | — | 留 | |
| `diagnostics/` | `kernel/diagnostics/` | `kernel` | 搬 | U17：内核错误码表的数据源；读它的 `scripts/diagnostics-catalog.mjs` 与顶层构建由脚本改路径 |
| `docs/` | — | — | 留 | |
| `editors/` | — | — | 留 | 编辑器扩展；语言服务器本身在 `tools/lang-server/` |
| `examples/` | — | — | 留 | 用户程序 |
| `exemplars/` | — | — | 留 | 用户程序 |
| `export_matrix.csv` | — | — | 留 | 生成物，索引页 |
| `export_matrix.md` | — | — | 留 | 生成物，索引页 |
| `hello.cml` | — | — | 留 | 仓库根的示例程序 |
| `include/` | 拆到表 B | 各块 | 搬 | 拆完顶层不再有 `include/`（U10、U11 定了内核公开头进 `kernel/include/`） |
| `internal/` | — | — | 留 | 构建与闸门 helper，不是独立程序（第 9 节的 `tools/` 指命令行、调试器、语言服务器） |
| `LICENSE` | — | — | 留 | |
| `llms.txt` | — | — | 留 | |
| `logo.png` | — | — | 留 | |
| `modules/` | 拆到表 B | `lib/` | 搬 | 与 `stdlib/` 并成 `lib/<模块>/`；含只有 C++、没有 `.cml` 的 `verify/`（U19） |
| `package-lock.json` | — | — | 留 | |
| `package.json` | — | — | 留 | |
| `promo/` | — | — | 留 | 宣传片素材 |
| `pypi/` | — | — | 留 | 发布清单与打包 |
| `README.md` | — | — | 留 | |
| `REPRODUCE.md` | — | — | 留 | |
| `ROADMAP.md` | — | — | 留 | |
| `scripts/` | — | — | 留 | 构建/测试/检查脚本，同上：不是独立程序 |
| `SECURITY.md` | — | — | 留 | |
| `showcase/` | — | — | 留 | 用户程序 |
| `src/` | 拆到表 B | 各块 | 搬 | |
| `stdlib/` | 拆到表 B | `lib/` | 搬 | |
| `test/` | — | — | 留 | 测试计划、用例、金标、工具；金标归合并位 |
| `third_party/` | — | — | 留 | 最下层，被 `libcamel` 一起编译 |
| `tools/` | `tools/` | `tools` | 留 | 原地成为一个块：命令行、调试器、语言服务器等独立程序 |
| `ZEN` | — | — | 留 | |

## 表 B：四个容器目录的拆分

`<模块>` 是占位符：脚本枚举 `modules/*/`、`stdlib/*.cml` 与 `stdlib/signatures/*.cml` 的实际名字去展开，
并检查每个名字都有行。

| 现有路径 | 目标 | 块 | 状态 | 备注 |
|---|---|---|---|---|
| `src/core/` | `kernel/core/` | `kernel` | 搬 | 类型、算子记录、图数据模型与存储、编辑会话、事实、重写核心、值 |
| `src/runtime/` | `kernel/runtime/` | `kernel` | 搬 | 同上；内部再按组件细分是阶段 1 内部的事，不是这张表的事 |
| `src/utils/` | `kernel/utils/` | `kernel` | 搬 | 基础工具，最下层 |
| `src/parse/` | `parse/src/` | `parse` | 搬 | 源码 → 语法树 |
| `src/compile/` | `compile/src/` | `compile` | 搬 | 语法树 → 图 |
| `src/builtin/` | `lib/std/builtin/` | `lib/std` | 搬 | U1（路 B）：内核只认算子记录，内建算子像模块一样登记 |
| `src/execute/` | 拆到本表以下 21 行 | — | 搬 | U2：逐文件分两半，内核一半是调用一个函数、函数登记表、遍驱动、入口实参、限额、执行观察钩子 |
| `src/execute/camel_pass.cpp` | `kernel/execute/camel_pass.cpp` | `kernel` | 搬 | U2：函数登记表 |
| `src/execute/camel_pass.h` | `kernel/execute/camel_pass.h` | `kernel` | 搬 | U2：函数登记表 |
| `src/execute/entry_args.cpp` | `kernel/execute/entry_args.cpp` | `kernel` | 搬 | U2：入口实参 |
| `src/execute/function_call.cpp` | `kernel/execute/function_call.cpp` | `kernel` | 搬 | U2：调用一个函数 |
| `src/execute/graph_caller.h` | `kernel/execute/graph_caller.h` | `kernel` | 搬 | U2：调用一个函数（它的头随它） |
| `src/execute/limits.cpp` | `kernel/execute/limits.cpp` | `kernel` | 搬 | U2：限额 |
| `src/execute/observer.cpp` | `kernel/execute/observer.cpp` | `kernel` | 搬 | U2：执行观察钩子 |
| `src/execute/pass.cpp` | `kernel/execute/pass.cpp` | `kernel` | 搬 | U2：遍驱动 |
| `src/execute/registration.cpp` | `kernel/execute/registration.cpp` | `kernel` | 搬 | U2：函数登记表 |
| `src/execute/pass_declarations.cpp` | `kernel/execute/pass_declarations.cpp` | `kernel` | 搬 | U2：遍驱动 |
| `src/execute/pass_declarations.h` | `kernel/execute/pass_declarations.h` | `kernel` | 搬 | U2：遍驱动 |
| `src/execute/pass_spec.cpp` | `kernel/execute/pass_spec.cpp` | `kernel` | 搬 | U2：遍驱动 |
| `src/execute/pass_spec.h` | `kernel/execute/pass_spec.h` | `kernel` | 搬 | U2：遍驱动 |
| `src/execute/cpu_isa.cpp` | `lib/std/exec/cpu_isa.cpp` | `lib/std` | 搬 | U2：设备 |
| `src/execute/devices.cpp` | `lib/std/exec/devices.cpp` | `lib/std` | 搬 | U2：设备 |
| `src/execute/exec_plan.cpp` | `lib/std/exec/exec_plan.cpp` | `lib/std` | 搬 | U2：执行计划的计算 |
| `src/execute/executor.cpp` | `lib/std/exec/executor.cpp` | `lib/std` | 搬 | U2：执行计划的计算（执行器本身） |
| `src/execute/fault_isolation.cpp` | `lib/std/exec/fault_isolation.cpp` | `lib/std` | 搬 | U2：故障隔离 |
| `src/execute/fusion_group.cpp` | `lib/std/exec/fusion_group.cpp` | `lib/std` | 搬 | U2：执行计划的计算（融合组） |
| `src/execute/graph_runtime_support.cpp` | `lib/std/exec/graph_runtime_support.cpp` | `lib/std` | 搬 | U2：`graph_runtime_support` |
| `src/execute/standalone_bridge.cpp` | `lib/std/cpp/standalone_bridge.cpp` | `lib/std` | 搬 | U2 没点它；它是 `camel/standalone/bridge.h` 的运行时半边，随 U13 的导出后端进 `lib/std/cpp/` |
| `src/execute/standalone_foreign.cpp` | `lib/std/cpp/standalone_foreign.cpp` | `lib/std` | 搬 | 同上；`camel/standalone/foreign.h` 的运行时半边 |
| `src/execute/macro/` | `lib/std/macro/` | `lib/std` | 搬 | U2 没点它；它是 `std::macro` 这个遍本身，按 U6「按入口名进 `lib/std/`」 |
| `src/passes/` | 拆到本表以下四行 | — | 搬 | 容器 |
| `src/passes/opt/` | `lib/std/cxx/opt/` | `lib/std` | 搬 | U3：C++ 的优化遍归 `std::cxx`；Camel 原生的 `opt` 模块在 `lib/opt/`（由 `stdlib/opt.cml` 那一行展开） |
| `src/passes/sched/` | 拆到本表以下三行 | — | 搬 | U4：按入口名一个后端一个目录 |
| `src/passes/sched/fastvm/` | `lib/std/fvm/` | `lib/std` | 搬 | U4：`std::fvm`；JIT 在 `lib/std/fvm/jit/`，随这一次搬进子目录 |
| `src/passes/sched/nodevm/` | `lib/std/nvm/` | `lib/std` | 搬 | U4：`std::nvm` |
| `src/passes/sched/taskflow/` | `lib/std/prl/` | `lib/std` | 搬 | U4：`std::prl` |
| `src/passes/test/` | `lib/test/` | `lib/test` | 搬 | U5：不属于 `test_support`，也不属于 `std` |
| `src/passes/trans/` | 拆到本表以下九行 | — | 搬 | U6：按入口名进 `lib/std/`，不进 `lib/transforms/` |
| `src/passes/trans/cml/` | `lib/std/cml/` | `lib/std` | 搬 | U6：`std::cml` |
| `src/passes/trans/cpp/` | `lib/std/cpp/` | `lib/std` | 搬 | U6：`std::cpp` |
| `src/passes/trans/stats/` | `lib/std/stats/` | `lib/std` | 搬 | U6：`std::stats` |
| `src/passes/trans/dot/` | `lib/std/gir/dot/` | `lib/std` | 搬 | U6：gir/rgir 的绘制 |
| `src/passes/trans/json/` | `lib/std/gir/json/` | `lib/std` | 搬 | U6：jgir |
| `src/passes/trans/gir_metadata.cpp` | `lib/std/gir/gir_metadata.cpp` | `lib/std` | 搬 | U6：gir/rgir/agir/argir/jgir/gir_regions 六个别名共用的元数据，一处 |
| `src/passes/trans/profile/` | `lib/std/profile/` | `lib/std` | 搬 | U6：`std::profile` |
| `src/passes/trans/tns/` | `lib/std/tns/` | `lib/std` | 搬 | U6：`std::tns`（别名 `std::topo_node_seq`） |
| `src/passes/trans/gc/` | `lib/std/gc/` | `lib/std` | 搬 | U6：`std::gc` |
| `src/passes/trans/nop/` | `lib/std/nop/` | `lib/std` | 搬 | U6：`std::nop` |
| `src/passes/trans/selftest/` | `lib/std/selftest/` | `lib/std` | 搬 | U6：`std::selftest` |
| `src/passes/trans/rulesets/` | `lib/std/rulesets/` | `lib/std` | 搬 | U7：`std::rulesets` 这个遍 |
| `src/profile/` | `kernel/profile/` | `kernel` | 搬 | U9：工具经内核的自省接口读它 |
| `src/service/` | 拆到本表以下三行 | — | 搬 | U8：语义查询、代码生成、性能剖析三份 |
| `src/service/semantic/` | `compile/semantic/` | `compile` | 搬 | U8：语义查询 |
| `src/service/codegen/` | 拆到本表以下一行 | — | 搬 | U8：代码生成，进 `tools/codegen/` 这个工具 |
| `src/service/codegen/source/` | `tools/codegen/source/` | `tools` | 搬 | U8：`tools/codegen/` 已经有 `main.cpp`，源进它的 `source/` |
| `src/service/profiler/` | 拆到本表以下四行 | — | 搬 | U8：性能剖析，进 `tools/profiler/` 这个工具 |
| `src/service/profiler/core/` | `tools/profiler/core/` | `tools` | 搬 | U8 |
| `src/service/profiler/tracers/` | `tools/profiler/tracers/` | `tools` | 搬 | U8 |
| `src/service/profiler/analysis/` | `tools/profiler/analysis/` | `tools` | 搬 | U8 |
| `src/service/profiler/advanced/` | `tools/profiler/advanced/` | `tools` | 搬 | U8 |
| `include/camel/core/` | `kernel/include/camel/core/` | `kernel` | 搬 | 内核公开头，唯一留在包含路径根上的 `camel/` 目录 |
| `include/camel/utils/` | `kernel/include/camel/utils/` | `kernel` | 搬 | |
| `include/camel/runtime/` | `kernel/include/camel/runtime/` | `kernel` | 搬 | |
| `include/camel/parse/` | `parse/include/camel/parse/` | `parse` | 搬 | |
| `include/camel/compile/` | `compile/include/camel/compile/` | `compile` | 搬 | |
| `include/camel/common/` | `kernel/include/camel/common/` | `kernel` | 搬 | U10：与 `core` 并列，是内核公开头的一部分 |
| `include/camel/init.h` | `kernel/include/camel/init.h` | `kernel` | 搬 | U11：顶层不再有 `include/` |
| `include/camel/execute/` | 拆到本表以下 15 行 | — | 搬 | U2（头随其源）：与 `src/execute/` 同一刀切 |
| `include/camel/execute/entry_args.h` | `kernel/include/camel/execute/entry_args.h` | `kernel` | 搬 | U2：入口实参 |
| `include/camel/execute/function_call.h` | `kernel/include/camel/execute/function_call.h` | `kernel` | 搬 | U2：调用一个函数 |
| `include/camel/execute/limits.h` | `kernel/include/camel/execute/limits.h` | `kernel` | 搬 | U2：限额 |
| `include/camel/execute/observer.h` | `kernel/include/camel/execute/observer.h` | `kernel` | 搬 | U2：执行观察钩子 |
| `include/camel/execute/pass.h` | `kernel/include/camel/execute/pass.h` | `kernel` | 搬 | U2：遍驱动 |
| `include/camel/execute/pass/` | `kernel/include/camel/execute/pass/` | `kernel` | 搬 | U2：遍驱动的细分头 |
| `include/camel/execute/cpu_isa.h` | `lib/std/exec/include/camel/execute/cpu_isa.h` | `lib/std` | 搬 | U2：设备 |
| `include/camel/execute/devices.h` | `lib/std/exec/include/camel/execute/devices.h` | `lib/std` | 搬 | U2：设备 |
| `include/camel/execute/executor.h` | `lib/std/exec/include/camel/execute/executor.h` | `lib/std` | 搬 | U2：执行计划的计算 |
| `include/camel/execute/fault_isolation.h` | `lib/std/exec/include/camel/execute/fault_isolation.h` | `lib/std` | 搬 | U2：故障隔离 |
| `include/camel/execute/fusion_group.h` | `lib/std/exec/include/camel/execute/fusion_group.h` | `lib/std` | 搬 | U2：执行计划的计算 |
| `include/camel/execute/graph_runtime_support.h` | `lib/std/exec/include/camel/execute/graph_runtime_support.h` | `lib/std` | 搬 | U2：`graph_runtime_support` |
| `include/camel/execute/loop_driver.h` | `lib/std/exec/include/camel/execute/loop_driver.h` | `lib/std` | 搬 | U2：循环驱动 |
| `include/camel/execute/loop_frame.h` | `lib/std/exec/include/camel/execute/loop_frame.h` | `lib/std` | 搬 | U2：帧 |
| `include/camel/execute/operator_call.h` | `lib/std/exec/include/camel/execute/operator_call.h` | `lib/std` | 搬 | U2：调用算子 |
| `include/camel/passes/` | 拆到本表以下四行 | — | 搬 | U12：遍的头随各自的块 |
| `include/camel/passes/macro/` | `lib/std/macro/include/camel/passes/macro/` | `lib/std` | 搬 | U12：随 `std::macro` |
| `include/camel/passes/opt/` | `lib/std/cxx/opt/include/camel/passes/opt/` | `lib/std` | 搬 | U12：随 U3 的 `lib/std/cxx/opt/` |
| `include/camel/passes/test/` | `lib/test/include/camel/passes/test/` | `lib/test` | 搬 | U12：随 U5 的 `lib/test/` |
| `include/camel/passes/trans/` | 拆到本表以下四行 | — | 搬 | U12：随 U6 的四个出口 |
| `include/camel/passes/trans/cml/` | `lib/std/cml/include/camel/passes/trans/cml/` | `lib/std` | 搬 | U12 |
| `include/camel/passes/trans/dot/` | `lib/std/gir/dot/include/camel/passes/trans/dot/` | `lib/std` | 搬 | U12 |
| `include/camel/passes/trans/json/` | `lib/std/gir/json/include/camel/passes/trans/json/` | `lib/std` | 搬 | U12 |
| `include/camel/passes/trans/gir_metadata.h` | `lib/std/gir/include/camel/passes/trans/gir_metadata.h` | `lib/std` | 搬 | U12：与 `gir_metadata.cpp` 同处 |
| `include/camel/service/` | `compile/include/camel/service/` | `compile` | 搬 | U8：语义查询的头随 `compile/` |
| `include/camel/profile/` | `kernel/include/camel/profile/` | `kernel` | 搬 | U9 |
| `include/camel/standalone/` | `lib/std/cpp/include/camel/standalone/` | `lib/std` | 搬 | U13：导出后端自己的公开头 |
| `modules/<模块>/` | `lib/<模块>/` | `lib/<模块>` | 搬 | 一个 Camel 模块一处；`verify/` 只有 C++、没有 `.cml`，U19 定了它按入口名进 `lib/verify/` |
| `modules/camel_module_abi.cpp` | `kernel/abi/camel_module_abi.cpp` | `kernel` | 搬 | U16：C 接口进 `kernel/abi/` |
| `modules/camel_module.cmake` | `cmake/camel_module.cmake` | — | 搬 | U16：构建胶水进 `cmake/` |
| `stdlib/<模块>.cml` | `lib/<模块>/<模块>.cml` | `lib/<模块>` | 搬 | `.cml` 声明与 C++ 实现放在一起；40 个名字 |
| `stdlib/prelude.cml` | `lib/std/prelude.cml` | `lib/std` | 搬 | 例外：它没有模块入口名，是随每个程序一起编译的内建签名表 |
| `stdlib/rulesets/` | `lib/rulesets/` | `lib/rulesets` | 搬 | U14：与 U7 的 `lib/std/rulesets/` **不是一回事，不合并** |
| `stdlib/signatures/` | 拆到本表以下六行 | — | 搬 | U15：按签名所属模块散到各自的 `lib/<模块>/signatures/` |
| `stdlib/signatures/autodiff.cml` | `lib/autodiff/signatures/autodiff.cml` | `lib/autodiff` | 搬 | U15 |
| `stdlib/signatures/ckpt.cml` | `lib/ckpt/signatures/ckpt.cml` | `lib/ckpt` | 搬 | U15 |
| `stdlib/signatures/nn.cml` | `lib/nn/signatures/nn.cml` | `lib/nn` | 搬 | U15 |
| `stdlib/signatures/tensor.cml` | `lib/tensor/signatures/tensor.cml` | `lib/tensor` | 搬 | U15 |
| `stdlib/signatures/tiles.cml` | `lib/tiles/signatures/tiles.cml` | `lib/tiles` | 搬 | U15 |
| `stdlib/signatures/transforms.cml` | `lib/transforms/signatures/transforms.cml` | `lib/transforms` | 搬 | U15 |

`stdlib/` 的展开规则是 `stdlib/<名字>.cml → lib/<名字>/<名字>.cml`，**41 个 `.cml` 里 40 个走这条规则**，
唯一例外是 `prelude.cml`（上表最后几行已写明）。`stdlib/signatures/<名字>.cml` 另走一条规则：
`lib/<名字>/signatures/<名字>.cml`。脚本会核对：`stdlib/*.cml` 的个数必须等于
「被这条规则展开的」加「被例外行点名的」，`stdlib/signatures/*.cml` 的每个名字都必须有一行，不等就退出 1。

## 表 C：19 条已定（决策者 2026-11-22 逐条定）

依据：核心抽象第 9 节（目录按入口的 Camel 名字、内部按组件真实结构；内核是唯一公开的 C++ API；
遍即函数）与第 13–19 节。**「路 A / 路 B」两列是当时摆出的两条路与各自代价，原样保留不动；
「状态」与「目标」两列是定论，表 A、表 B 里相应的行就按「目标」改。**

| 编号 | 路径 | 问题 | 路 A（代价） | 路 B（代价） | 状态 | 目标 |
|---|---|---|---|---|---|---|
| U1 | `src/builtin/` | 内建算子和内建执行器算内核还是算 `std`？ | `kernel/builtin/`：内核直接登记内建算子，`lib/std` 只放遍；内核因此继续认识具体算子 | `lib/std/builtin/`：内核只认算子记录，内建算子像模块一样登记；`lib/std` 要能被内核加载 | 已定 | `lib/std/builtin/`（路 B：内核只认算子记录，内建算子像模块一样登记） |
| U2 | `src/execute/`、`include/camel/execute/` | 执行基础设施放哪一块？ | `lib/std/exec/`：总方案的车道表这么写；但 `exec_plan` 按第 13 节是**事实**，事实归内核 | 内核对半、`lib/std/exec` 另一半：要逐文件拆，`exec_plan.cpp` 与 `pass.cpp` 分居两处 | 已定 | **拆两半**：调用一个函数、函数登记表、遍驱动、入口实参、限额、执行观察钩子 → `kernel/`；执行计划的计算、帧、循环驱动、调用算子、设备、故障隔离、`graph_runtime_support` → `lib/std/exec/` |
| U3 | `src/passes/opt/` | C++ 的优化遍归哪个模块？ | `lib/std/cxx/opt/`：总方案注明重写车道拥有它 | `lib/std/opt/`：与 `stdlib/opt.cml`（`opt::` 模块）同处，改名后模块名与目录名一致 | 已定 | `lib/std/cxx/opt/`；Camel 原生 `opt` 模块在 `lib/opt/` |
| U4 | `src/passes/sched/` | 三个调度后端是一个块还是三个？ | `lib/std/{nvm,fvm,jit}/`：车道表点了 `lib/std/nvm`，一个后端一个目录 | `lib/std/sched/`：一个块一个构建目标，`fastvm`/`nodevm`/`taskflow` 作子目录 | 已定 | 按入口名一个后端一个目录：`lib/std/nvm/`、`lib/std/fvm/`（JIT 在 `lib/std/fvm/jit/`）、`lib/std/prl/` |
| U5 | `src/passes/test/` | 只给测试用的遍归哪？ | `lib/test_support/test/`：与 `test_support` 模块同处 | `lib/std/test/`：测试遍也是遍，与该模块的其余遍同处 | 已定 | `lib/test/`（两条路都不对：不属于 `test_support`，也不属于 `std`） |
| U6 | `src/passes/trans/` | 变换/导出/打印的遍归哪？ | `lib/transforms/`：车道表把 `lib/transforms` 给变换车道 | `lib/std/trans/`：这些遍现在就是 `std::` 的名字，改名会动 CLI 读数 | 已定 | 按入口名进 `lib/std/`：`lib/std/cml/`、`lib/std/cpp/`、`lib/std/stats/`、`lib/std/gir/`（gir/rgir/agir/argir/jgir/gir_regions 一处）；不进 `lib/transforms/` |
| U7 | `src/passes/trans/rulesets/` | 规则集（C++ 侧）的独占目录名 | `lib/rulesets/`：与 `stdlib/rulesets/` 同一个名字，两块并一处 | `lib/transforms/rulesets/`：规则随变换走；但规则车道要独占它就得跨车道改 | 已定 | `lib/std/rulesets/`（它是 `std::rulesets` 这个遍） |
| U8 | `src/service/`、`include/camel/service/` | 语义查询、代码生成、性能剖析归工具还是前端？ | `tools/service/`：第 19 节说工具经 `compile/` 的语义查询接口，`service` 是工具的底座 | `compile/service/`：语义查询是 `compile/` 该提供的那份接口的现成实现 | 已定 | **拆三份**：语义查询 → `compile/semantic/`；代码生成 → `tools/codegen/`；性能剖析 → `tools/profiler/` |
| U9 | `src/profile/`、`include/camel/profile/` | 性能采样的核心归内核还是工具？ | `kernel/profile/`：工具经内核的自省接口读它 | `tools/profiler/`：只有剖析工具读它，内核不需要 | 已定 | `kernel/profile/` |
| U10 | `include/camel/common/` | 公共小头算内核公开头的一部分吗？ | `kernel/include/camel/common/`：与 `core` 并列 | 顶层 `include/camel/common/`：所有块都要引的，留在最上层的 `include/` | 已定 | `kernel/include/camel/common/` |
| U11 | `include/camel/init.h` | 同上（初始化入口） | `kernel/include/camel/init.h` | 顶层 `include/camel/init.h` | 已定 | `kernel/include/camel/init.h`；顶层不再有 `include/` |
| U12 | `include/camel/passes/` | 遍的头随哪个块 | 随 U3–U6 定下的块（各自块内 `include/`） | 统一放 `lib/std/include/camel/passes/` | 已定 | 遍的头**随各自的块** |
| U13 | `include/camel/standalone/` | C++ 导出程序用的头归哪 | `lib/std/cpp/include/camel/standalone/`：导出后端自己的公开头 | `tools/camel-cpp/`：只有 `camel-cpp` 这个工具生成并需要它 | 已定 | `lib/std/cpp/` |
| U14 | `stdlib/rulesets/` | 规则集的 `.cml` 声明归哪 | `lib/rulesets/`（与 U7 同名合并） | `lib/std/rulesets/`：规则集是 `std::` 的规则库 | 已定 | `lib/rulesets/`（与 U7 **不是一回事，不合并**） |
| U15 | `stdlib/signatures/` | 原生函数的类型签名表归哪 | `lib/<模块>/signatures/`：按签名所属模块散到 6 处 | `lib/std/signatures/`：一处放齐，谁要看都在同一个地方 | 已定 | 签名**按模块散到各自目录**（`lib/<模块>/signatures/`） |
| U16 | `modules/camel_module_abi.cpp`、`modules/camel_module.cmake` | 模块的公共构建胶水与 ABI 桩 | `cmake/camel_module.cmake` + `lib/camel_module_abi.cpp`：胶水进 `cmake/`、源就近 | `lib/abi/`：模块的二进制接口层单独一块，第 15 节的 C 接口落在那里 | 已定 | C 接口 → `kernel/abi/`；构建胶水 → `cmake/camel_module.cmake` |
| U17 | `diagnostics/` | 诊断码登记表（生成 `core/error` 下的码表） | `kernel/diagnostics/`：它是内核错误码表的数据源 | 留顶层：顶层 `CMakeLists.txt` 与 `scripts/diagnostics-catalog.mjs` 都按顶层路径读它 | 已定 | `kernel/diagnostics/`（读它的脚本与顶层构建改路径） |
| U18 | `bindings/python/` | CPython 扩展（宿主 libcamel 的一方） | `lib/python/bindings/`：与 `python::` 模块同一个目录树，两个方向一处 | 留顶层：它不是 Camel 模块，是另一个语言侧的宿主 | 已定 | `bindings/python/` 留顶层；`python::` 模块在 `lib/python/` |
| U19 | `modules/verify/` | 只有 C++、没有 `.cml` 声明的模块 | `lib/verify/`：目录名就是它的入口名（`verify::`） | `lib/test_support/verify/`：它与 `test_support` 是同一类（只给测试用） | 已定 | `lib/verify/` |

## 这张表怎么用

```powershell
# 只生成，不执行：看 git mv 序列、包含改写与每块的构建目标
node scripts/layout-move.mjs --out D:\Temp\layout-out

# 在临时树里真的搬并编译（演练；产物不合入）
node scripts/layout-move.mjs --apply --root D:\Temp\layout-drill
```

`--apply` 只会动 `--root` 指向的树，且拒绝在仓库主干上跑。演练结果与违例清单见
`docs/handover/plans/2026-11-22-refactor-stage0-layout-violations.cn.md`；车道对目录的独占见
`layout-ownership.cn.md`。