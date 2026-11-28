# 每日审查：develop 11-27 17:44 → 11-28 16:58（f4484f111..36f7b2adf5）

审查对照设计原则、`docs/design/architecture/abstractions.cn.md` 与 `agents/refactor.md`，范围是 `src/`、`scripts/`、`test/tools/`、`internal/tools/`。下面每条都由决策者对着代码核过。照 `2026-11-28-refactor-not-patch.cn.md` 的那一问处理：机制在重构范围里的，修法写成"归哪件"；不在的，当场修在根上。

## 成立的

1. **遍层按遍名分支**（归重构③"遍即函数"第 2、5 步）。
   - 内核里的分支：`src/kernel/execute/pass.cpp` 有 `specName(spec) != "std::jit"`（缺代码生成器的告警）、`== "std::profile"`、`name == "std::opt" && settle_runs < 1`；`std::run` 的目标清单写成字面量 `(std::fvm, std::nvm, std::jit, std::prl)`，默认 `std::fvm` 也写死。
   - 调试器：`src/tools/debugger/src/run.cpp` 有 `pass == "std::jit"` 与 `"std::prl"`。
   - 兜底遍 `std::run` 写了四遍，分别在 `config.cpp`、`opencmlrc.cpp`、调试器 `run.cpp`、`server.cpp`；`scripts/camel-pass-pipelines.mjs` 又写了一遍 `'std::run': 'std::fvm'`。
   - **修法**：这些都是遍声明上的性质：是不是调度遍、能否被调试器停住、要不要代码生成器、形参的取值范围（`settle_runs: int` 的下限写在声明里，由形参检查统一报）。兜底遍与它的默认目标只在一处声明，其余处读它。
   - **顺带**：`test/tools/check-pass-declarations.mjs` 现在靠 grep `passAliases` 这个变量名把关，换成行为用例：已删的拼写（如 `std::default`）在命令行上被拒。
2. **"调用选中哪个条目"有了第二条路径**（归前端类型检查阶段的重构，见架构第 8 节"重载选择只有一个解析器"）。
   - `src/compile/src/gir/builder_call.cpp` 的 `withParameterOfSelectedEntry` 在 `signatureForArgumentContext` 之外，再扫一遍候选条目，为的是让 `foreach` 作用在 `Range` 上时能定型。
   - **修法**：`foreach` 对 `Range` 这个例子进回归用例，答案归那一个解析器。
   - **当场修**：同处那段解释注释整段重复了一份，删掉一份。
3. **库与库之间互相伸手**：`src/stdlib/transforms/transpose/walk.cpp` 用相对路径 `#include "../../autodiff/pullback.h"`，`transforms/CMakeLists.txt` 加了 `LINK_MODULES autodiff`，为的是共用 `buildReverseCondRegion` 一类的反向区域构造。两个库共用的图构造，放到它们都依赖的那一层，也就是内核的编辑原语，与第 4 条同一处。
4. **"写一个分支"写了三份**：`src/stdlib/transforms/rule_expand.cpp` 新加的 `Brch`/`Join` 构造、`split.cpp#buildHalfBranch`、`jvp.cpp` 的 lift，注释自己点了另两份的名。合成内核编辑会话里的一个原语，三处都调用它。
5. **同一个判断写了两份**：`src/stdlib/tensor/kernels/conv.cpp` 的 `batchNorm` 自己重写了"长度 1 的参数能放到不止一个轴上"的判断与拒绝句；`kernels/shape_rules.h#ambiguousRankOneAxes` 已有这一份，头注释写明内核与声明不许各说各的。改成调用它。
6. **同一个事实两种问法**：`src/stdlib/onnx/exporter.cpp` 按 URI 文本比 `":struct/append_arr"`、`":struct/append_arr_owned"`；同一文件里原地追加那条已经改成比算子 id（`appendInPlaceOper()`）。三个都改成读算子记录或 id。
7. **注释缺陷**：
   - `src/kernel/facts/include/camel/facts/facts.h:620` 一行里混进了制表符与响铃两个控制字符（`\t`、`\a` 转义被吃掉了，读成"<TAB>est/tools/facts""<BEL>live_"）。全仓 grep 一遍控制字符。
   - `src/kernel/execute/pass.cpp`：97–101 行两个半句拼在一起；约 2411 行仍说"核心的别名条目先回答 `std::run`"，可别名表已经删了；文件末尾少一个换行。
   - `src/stdlib/std/fvm/fastvm.cpp:282` 引用用例名与退出码 134，写的是经过，不是设计。
   - `scripts/check-arrangeable.mjs:100` 与 `test/plans/feat/tools/arrangeable.plan.toml:71` 写死了"3,056 of 4,133"，留下比例下限即可。
8. **已当场修（决策者，fsh-dev）**：`merge-batch.mjs` 与 `promote.mjs` 各自留着一份"哪些结论不是红"，两份都没有 `flaky`，于是重跑后过了的 flaky 用例被算成红，还被打印成 flaky red。现在由测试运行器在每行写出 `red`，两边都读它；合并位的工具测试加了一条用例钉住。另外，监督插件里两条关于主检出的提醒，原来写死了路径，改成用测到的路径。

## 看过、不成立或另议的

- 前端 GCT 把 edit/view 块降低成 `graph::open`、`graph::commit_of`、`graph::graph_of` 的调用：块的语义本来就由这几个函数定义，算语言的一部分；要不要改成由声明回答，等块的设计收尾时一起看。
- 内核注释里拿 `tensor:zeros_like` 做例子：只是注释里的例子，不构成依赖。
- `callSiteAtArityFrom`：通用的调用点参数机制，服务的是核心算子，不是单个库。
- 事实库里"哪些库知道哪张图"存了三份索引：方向不同，是否有冗余未核实，不列。
- `vjp-parity` 把"库里有、引擎里没有"的规律改成只报不拦：与 11-27 的定法一致（C++ 引擎是对照，可按需补，不要求库里每条都在引擎里有）。
- ONNX 导出把没人调用的被捕获闭包写成空常量：由计数守住，不改语义。
