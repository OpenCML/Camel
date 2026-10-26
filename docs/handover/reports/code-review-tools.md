# 用户工具的代码评审：命令行、语言服务器、VS Code 扩展、测试运行器

范围：`tools/camel-cli/`（`main.cpp`、`config.cpp`、`check.cpp`、`module_failures.*`、`diag_limit.h`、`timeit.cpp`）、`tools/lang-server/` 与 `src/service/semantic/`、`editors/vscode/`（`src/extension.ts`、`syntaxes/`、`scripts/check-grammar.mjs`）、`test/tools/run-tests.mjs`、`scripts/test.js`、`test/runner.mjs`；环境变量按 `grep getenv` 全仓库核对，并对照 `docs/cli.md` 里"留在环境里的东西"一段。读的是代码，没有编译、没有运行；写明"需先写用例确认"的是从代码推出的。没有审：`tools/format/`、`tools/debugger/`（只数了其中的 `DESIGN-QUESTION`）、`timeit.cpp` 的细节、`test/tools/` 下的其他脚本。

## 总评

做得好的：对未知选项、未知警告名、缺失的值一律报错而不是放过（`rejectUnknownOptions`、`applyDiagnosticOptions`）；`--max-errors`/`--max-warning` 的汇总行；`module_failures.h` 要求每个调用点写出"问的是哪一组模块"；VS Code 扩展是薄客户端，`check-grammar.mjs` 把 TextMate 文法的关键字、运算符、内建类型和上下文关键字表与词法表逐项核对；测试计划的字段是白名单（`PLAN_FIELDS`，拼错的断言名是错误而不是注释）。

问题集中在四处：（1）**接受却什么都不做的选项**与**不再被读取的环境变量**；（2）退出码一刀切（几乎全是 1），而且几处入口用 `std::exit` 绕过收尾；（3）`runCommand` 一个函数 670 行，控制流里有一条可能"出了故障却以 0 退出"的穿透路径；（4）语言服务器里有一处能复现的签名帮助错误和一张与编译器不同步的内建类型表。

## 严重

### 1. 接受选项却不起作用（违反自己写下的"拼错了静默无效比没有选项更糟"）
`config.cpp:applyDiagnosticOptions` 里有这句注释（"a misspelt -Wno-<name> that silently did nothing would be worse than no option"），但下面这些选项解析后**没有任何读取者**（`grep` 工具目录各命名的出现次数）：
- `-n/--no-cache`（`config.cpp:661`，`noCache` 只有声明与赋值）。仓库没有编译缓存（设计原则 10.3 写"缓存尚未实现"），也没有 `--cache-dir`；这个选项现在欺骗用户相信存在缓存。
- `-S/--scheduler`（`config.cpp:632`，`schedular` 无读取者）：调度器是由遍列表（`std::fvm` 等）决定的，这个选项什么都不改，帮助文字只写"scheduler type"。
- `inspect -p/-P/--pass-until`（`:751`，`passUntil` 只在已死的 `_printInspectArgs` 里读）。
- `check -c/--config`、`-e/--ignore`（`configFilePath`、`ignoreDefiFile` 只在 `_printCheckArgs` 里读）；`check -o/--output`（`outputFile` 在 `check` 路径上不被使用，`checkFrontend` 的输出固定写到 `os`）。
- `-d/--docs`：打印 `"Not implemented yet"` 并以 0 退出（`config.cpp:816`）。
- `-P/--profile`：帮助自己写着"a release build accepts the option and writes nothing"（`:628`）。
失败场景：`camel -n prog.cml`、`camel -S nvm prog.cml std::fvm` 都成功，用户以为选了没选的东西。
修法：删除上面的死选项与相应的变量、`_printCheckArgs`/`_printInspectArgs`/`printCliArgs`（也是死代码，`printCliArgs` 无调用者）；`-P` 在发布构建里要么真的工作，要么报 `Error: --profile needs a debug build`，与其他"需要先决条件"的选项（`--profile-mode needs --profile`）同一规则。

### 2. 退出码几乎全是 1；多处用 `std::exit(1)` 绕过收尾
- 用法错误、编译错误、运行时故障、`-Werror`、内部缺陷、`--explain` 找不到、`--timeit` 与 `--profile` 冲突，全部 `return 1`；程序自己的 `exit(n)` 经 `processExitCodeOr` 也可以是 1，三者无法区分。仓库里自己的脚本用 2 表示用法/先决条件错误（`test/tools/run-tests.mjs:651,670` 的 `process.exit(2)`、`scripts/camel-for-ai.mjs` 的"exit 2"、`install-stamp.mjs`），命令行工具与它们不一致。
- 内部缺陷：`main.cpp:804–809`：先 `os << "camel: internal error…"`（**写到标准输出**，而用法错误写到标准错误），再 `ASSERT(false, …)`，然后 `return 1`。`ASSERT` 在 `NDEBUG` 下被编译掉（`assert.h`），所以 Debug 构建在这里中止（退出码不是 1），Release 构建以 1 退出——同一缺陷在两个构建里退出码不同。
- `parseArgs`（`config.cpp:905–976`）有九处 `std::exit(1)`，而同一函数里另有 `exitCode = 1; return false` 与 `return false`（信息命令成功时也返回 `false`，靠全局 `exitCode` 区分）。`std::exit` 跳过 `runCommand` 里 `ScopedRuntime`、`phase::Scope` 与阶段报告的析构，`--time-phases` 在这些路径上不写报告；三种风格并存。
修法：`parseArgs` 返回一个 `ParseResult { Run, Exit(code) }`，不再用全局 `exitCode` 与 `std::exit`；退出码表写进 `docs/cli.md`：0 成功，1 程序有错误或故障，2 用法/配置错误，70（`EX_SOFTWARE`）内部缺陷，程序自己的 `exit(n)` 保持 n；内部缺陷写标准错误，Debug 与 Release 同码。

### 3. `runCommand`：670 行的单函数，一条路径出了故障却可能以 0 退出
`main.cpp:153–810`。嵌套的 `try`/`catch` 五层，里面还有 `static PassDumpState passDump`、`static bool passWarningsAsJson`（`:625,642`，函数内静态量被进程级回调持有，`--timeit` 多次运行时共享），全局 `string targetFile`（`:65`）和文件级的 `using namespace std; using namespace CLI;`。
控制流问题（`:700–707`）：
```
} catch (Diagnostic &d) {
    if (!d.persisted) { ctx->runtimeDiagSink()->add(std::move(d)); }
}
```
这个 `catch` 之后没有 `return`，也没有转储：控制流落到 `memperf::stop(); … return ctx->processExitCodeOr(0);`。它能给出非零退出码，**只靠** `add()` 对第一个错误抛出 `DiagnosticsLimitExceededBaseException`（`runtimeDiagsConfig` 的 `Error` 限额为 0，`:176`）进到外层 `catch`；而一个 `persisted` 的诊断（已经存进别处）则不经过 `add`，直接落到成功路径：**不打印、退出码 0**。正常的故障报告因此依赖"异常撞限额"这条隐含机制。需先写用例确认（让一个 `persisted` 的 `Diagnostic` 逃出 `applyPassesDetailed`）。
修法：`catch (Diagnostic &d)` 里显式 `dump` 并 `return 1`；`runCommand` 拆成"解析与上下文""编译""检查""运行""转储"几个阶段函数，各自返回状态；文件级 `using namespace` 与全局删掉。

### 4. 环境变量作为行为开关：文档列出的与实际读取的不一致，且有"设了但没人读"的
项目约定行为设置不放环境变量（`docs/cli.md` 写明留在环境的只有进程环境类：`CAMEL_HOME`、`CAMEL_STD_LIB`、`CAMEL_PACKAGES`、日志与诊断格式、资源上限、收集器诊断与步调、`CAMEL_STARTUP_PROFILE`；`CAMEL_EXE` 已改成 `--camel`）。实际读取的还有：
- **改变结果的**：`CAMEL_TENSOR_ISA=generic`（`modules/tensor/kernels/cpu.cpp:64`，换内核，浮点结果的比特可能不同；`docs/cli.md` 不在列表里）。这不是进程环境，应是 `--tensor-isa` 或遍参数。
- **诊断类但未列入**：`CAMEL_TENSOR_OPSTATS`、`CAMEL_ONNX_TRACE`、`CAMEL_ONNX_DUMP`、`CAMEL_ONNX_PREP_STATS`、`CAMEL_TIME_PHASES`（`phase::enableFromEnvironment`，同时又有 `--time-phases`）、`CAMEL_COLL_VALIDATE/FANOUT/LEAF_CAPACITY/PAD_NODES`（`collection_tree.cpp`）、`CAMEL_GC_LOG_MOVES`、`CAMEL_GC_PRINT_CONFIG`、`CAMEL_PYTHONPATH`、`CAMEL_DB_WORKER_PORT`（调试器内部）、`CAMEL_TILES_NVRTC`（路径）。
- **产品代码里的测试挂钩**：`CAMEL_TILES_TEST_INVALIDATION`（`modules/tiles/src/region_test_pass.cpp:243`）。
- **仍保留的旧别名**：`CAMEL_MODULE_PATH`（"the legacy"，`install_layout.h:175`、`context.cpp:721`、`context.h:179`）——不考虑兼容的项目里不该有 legacy 名字。
- **同一个设置两处读取**：深度/轮数上限的选项在 `config.cpp:readDepthLimit/readRoundLimit` 里只"检查"环境变量（"the environment's is read where it is used"），真正消费在 `src/execute/limits.cpp:30,148` 与 `include/camel/standalone/core.h:188,211`（独立运行时又写了一遍 `CAMEL_MAX_RECURSION_DEPTH`、`CAMEL_ERROR_FORMAT`）。校验与使用分在三处，规则改一处会漏另外两处。
- **设了却无效**：`test/plans` 里 8 个以上计划设 `CAMEL_GC_ENABLE_YOUNG_COPYING=0`（`attempt.plan.toml:115`、`jit_framed.plan.toml:38,50,151,187,211`、`var_reference.plan.toml:3038`、`mm/gc.plan.toml:389`），而 `docs/cli.md:551` 自己写着它"is not read and changes nothing"，这些计划同时带 `--gc mark-sweep`，所以结果碰巧对；环境变量那一半是死配置，下一个读这个计划的人会以为它关掉了年轻代复制。`docs/cli.md:551` 还保留着"旧拼写不被读取"的说明（项目不留"曾经怎样"）。
- 测试运行器自己的开关：`CAMEL_TEST_BUILD`（选 Debug/Release，同时又有 `--build`）、`CAMEL_TEST_TIMEOUT_SCALE`、`CAMEL_TEST_RUN_DIRS_KEEP`、`CAMEL_TEST_RESULTS_ROOT`、`CAMEL_TEST_SHOW_OUTPUT`/`CI`、`CAMEL_GATE_BUILD_SECONDS`（`run-tests.mjs:36,266,320,552,894,936`）。
修法：写一张"环境变量全表"到 `docs/cli.md`（读取处、用途、是否影响结果），把影响结果的变成选项或遍参数，把诊断类的收成一个前缀统一的调试族并明确"不影响结果"；删除 `CAMEL_MODULE_PATH`；限额只在 `limits.cpp` 一处读取（CLI 调它的 `parse`）；清掉计划里的死环境变量。

## 高

### 5. 语言服务器：签名帮助把 `=>` 的 `>` 当成泛型括号闭合，带函数类型形参的签名会错
`src/service/semantic/queries.cpp:1244 parametersIn`：按字符扫描打印出来的函数类型，`'<'` 与 `'>'` 当作括号层级；而 `=>` 里的 `>` 也被当成 `'>'` 减一层（`--depth`）。失败场景：`(f: (x: int) => int, y: int) => int`：扫描到内层的 `=>`，深度从 2 回到 1 再到 0，在 `>` 处 `flush`，得到参数 `f: (x: int) =`，第二个参数 `y: int` 丢失（后面的 `)` 使深度为 -1，永远不再 `flush`）。标准库里 `with <f: (x: T) => U>` 的形式（`map`、`filter`、`sort_by`……）随处都是，所以对这些函数，"当前参数高亮"和参数列表都是错的。需先写用例确认，但从代码上是确定的。
更根本：从**打印出来的类型文本**反解析参数，是在字符串层面重做类型结构。`SemanticModel` 已经有类型对象，签名帮助应当直接取函数类型的参数列表（名字、类型、是否 `with`）。

### 6. 语言服务器的内建类型名是第三份拷贝，且落后于编译器
`queries.cpp:156 builtinTypeNames()`：`int float bool string void i32 i64 f32 f64 c64 c128 Bytes Map Set`。编译器的根类型作用域（`src/compile/gct/builder.cpp:296–340`）还有 `bf16`、`f16`。结果：补全不给 `bf16`/`f16`，`queries.cpp:1535` 判"内建类型"也认不出它们（语义着色把它们当作普通名字）。`editors/vscode/scripts/check-grammar.mjs` 核对的是 TextMate 文法对 `builder.cpp` 的一致性，**没有**核对这张表；关键字这张就做对了（`keywords()` 直接读 `keywordTable()`，注释写着"so that the list cannot drift"）。
修法：编译器导出一个 `builtinTypeNames()`（或从同一张表生成），语言服务器、文法检查、`builder.cpp` 都读它。

### 7. 语言服务器：取消不会中断工作，异常被悄悄吞掉
- `Server::dispatch`（`server.cpp:145`）单线程执行，`transport_.takeCancelled(id)` 只在请求开始前检查一次，`checkCancelled()` 在处理函数**返回之后**再检查（`:169`）；`workspace.cpp`/`reading.cpp` 内部的 `checkCancelled()` 只在少数循环里。`semantic::analyze`（整个文件的编译，`:304`）不可中断。用户连续输入时，旧请求仍然完整地算完才被丢弃。
- `Server::run` 的 `refreshStale()` 外面是 `catch (...) {}`（`server.cpp:124`）：分析里抛出的任何异常都被丢弃，没有日志；`server.cpp:58,187` 也有裸 `catch (...)`。通知（无 `id`）里的 `json::exception` 在 `dispatch` 的 `catch` 里只对请求回应（`:174–182`），对通知静默丢弃，格式错误的 `didChangeWorkspaceFolders` 不会留下痕迹。
- 一次内部断言失败（`ASSERT` 在 Debug 下中止）会带走整个服务器进程，所有打开的文档都失去服务；服务端没有"每个文档的分析在子进程/受保护区里跑"的隔离。
- `main.cpp:94`：`--dump-model` 缺文件参数时落到 "ignoring unknown argument"，然后**开始服务标准输入**（阻塞在读上），命令行的拼写错误变成挂起。`std::_Exit(code)`（`:114`）跳过析构，Python 等外部资源不释放。

### 8. `analyze` 静默丢弃对不上的事实；服务层依赖编译器内部头
`analyze.cpp` 头注释："A fact whose range matches nothing is dropped: the model then says less, never something wrong."——丢弃没有计数，也没有日志，LS 的"悬停/跳转突然没了"没有任何线索可查。应记录丢弃数（调试输出里一行），并有一个"丢弃数为 0"的黄金用例，守住 `SemanticRecorder` 与语法索引范围的对应。
`analyze.cpp` 还包含 `core/module/entity_internal.h`、`camel/compile/gir/draft_graph_builder.h`、`camel/compile/gir/value_entity.h`：服务层直接读编译器内部结构，编译器内部一改，服务层就要跟着改；这些应当由 `SemanticRecorder` 的接口提供。

## 中

### 9. 命令行的两套解析器
`main()`（`main.cpp:826–855`）在 clipp 之前手工取出 `--time-phases`、`-I`/`--include`/`--include=`/`-Idir`，理由是 clipp 的可重复值会吞掉后面的位置参数。`-I` 却**仍然**在 clipp 的 `run` 组里声明（`config.cpp:665`），帮助里列着它，真正生效的是 `main()` 里的手工分支；`--time-phases` 不在 clipp 里，只在 `kMeasurementOptionsHelp`。两套解析要保持一致：新增一个带值的全局选项必须记得两处都改。clipp 的缺陷应当绕在一个地方（预处理函数 `takeGlobalOptions(argv)`），在 clipp 的声明里去掉被取走的选项，帮助文字由同一张表生成。

### 10. 诊断与输出的流向不统一
编译诊断、运行时故障、内部缺陷、"Uncaught diagnostic"、`[camel] execution failed without diagnostics …` 都写到标准输出（`os`，`main.cpp:720、767、795、805`；设计意图是"stdout 留给程序输出与诊断数组，工具解析它"），警告与用法错误写标准错误。对人和管道来说，"内部缺陷"这种不是诊断数组一部分的东西不该进标准输出；`Error: Cannot open file` 又走标准错误。至少写成一条规则：诊断数组（JSON）和程序输出在 stdout，其余一切人读的信息在 stderr，并让 `os` 只用于前者。

### 11. 测试运行器
- 2,877 行一个文件；约 400 行是"槽位计时"记账（`slotTiming`、`summarizeSlotTimings`、`loop_lag`……），每次运行打印一行十余个指标的 `[SLOT]` 行（`:2840–2860`），与"这个运行器判断测试对不对"的职责无关，应当放进一个单独的、默认关闭的剖析模块。
- 预算规则按 CPU 时间判（`CAN_READ_CHILD_CPU = fs.existsSync('/proc/self/stat')`），非 Linux 回退到墙钟：Windows/macOS 开发机上，同一条"通过的测试用时不得超过自己超时的四分之一"规则在不同机器上用不同的量判定，注释承认"the margin rule falls back to the wall clock"；开发机上偶发红、网关上绿，是最难查的一种不一致。
- 错误提示里写死 `path.join(path.dirname(REPO_ROOT), 'camel-build.ps1')`（`:643`）：指向仓库外兄弟目录的脚本；仓库公开后这条路径不存在。
- 退出码：缺少二进制 `2`，失败 `1`，运行器自己崩溃也是 `1`（`:2875`）——"测试失败"与"运行器出错"同码。
- `known_fail` 字段在运行器里有 6 处引用，`test/plans` 里 0 个使用：死特性或没有被用到的豁免通道，应当删或写用例。

### 12. VS Code 扩展与 LS 各自找服务器
`extension.ts:66–110` 实现了"配置路径 → `$CAMEL_HOME/bin` → `PATH`"的查找（含 `PATHEXT`、`~` 展开），`install_layout.h` 在 C++ 一侧又实现了安装根解析，`tools/lang-server/main.cpp:106` 在用 `CAMEL_HOME` 兜底。三处规则相同才对，但没有共享。扩展只传 `--stdio`，没有把工作区的 `-I` 根、标准库覆盖等传给服务器（服务器靠 `workspaceFolders` 自己猜，`server.cpp:workspaceRoots_`），命令行能用 `-I` 解析的导入，编辑器里可能找不到。需先确认。

### 13. 注释与消息里的历史叙述、未决问题
`main.cpp:798`（"it used to be printed bare: a line reading `bad any_cast`"）、`tools/format/fmt.cpp:186,383`（"they used to be written twice / dropped"）、`tools/lang-server/server.cpp:588`、`tools/parsecheck/snapshot.cpp:15`（"parser was removed"）、`test/tools/run-tests.mjs:712,1682`（"they used to be, once"）、`scripts/install-stamp.mjs:15`、`scripts/common.js:336`、`scripts/plan-inventory.mjs:12`、`scripts/check-doc-links.mjs:208,476`、`docs/cli.md:551`。`tools/debugger/` 里有 5 处 `DESIGN-QUESTION(...)`（`debug_core.cpp:684,719`、`frames.cpp:177`、`run.cpp:127`），代码里不留未决问题。
`config.cpp:988`：用户可见的 `Error: Run requires an input file…` 等消息是对的；但 `-P` 的帮助文字（"debug builds only: a release build accepts the option and writes nothing"）是在帮助里承认选项无效。

### 14. LSP 的魔数
`server.cpp:63–110`：`symbolKind` 返回 `12/23/3/14/13`，`completionItemKind` 返回 `3/6/22/25/9/5/14/1`，靠行尾注释说明含义。应当是 `enum class SymbolKind : int { Function = 12, … }`（LSP 规范的名字），否则协议改一个版本或加一个种类时无从检查。`handleRequest` 是 20 多个 `if (method == "…")` 串；一张 `unordered_map<string, handler>` 才能一次性列出支持的方法，也让能力声明（`initialize` 的返回）与实际分派可以互相核对。

## 缺的测试
1. 每个被接受的命令行选项都有一个"改变了什么"的断言；`-n`、`-S`、`-p`、`-c`、`-e`、`check -o`、`-d` 目前没有（第 1 条）；一个"所有被声明的选项都有读取者"的静态检查最好。
2. 退出码表：用法错误、编译错误、运行时故障、程序 `exit(3)`、内部缺陷各一条，Debug 与 Release 同码（第 2 条）。
3. 逃出 `applyPassesDetailed` 的 `persisted` 诊断：必须有输出、必须非零（第 3 条）。
4. 环境变量审计脚本：`grep getenv` 的结果与 `docs/cli.md` 的表逐项对账，计划里的 `envs` 每个名字都有读取者（第 4 条）。
5. LS 的签名帮助：`map`、`filter`、`sort_by` 的参数列表与当前参数下标（第 5 条）；`bf16`/`f16` 的补全与着色（第 6 条）。
6. LS：`--dump-model` 缺参数应当报错退出；通知里的坏参数应当有日志；一次分析中的内部异常不应使服务器退出（第 7 条）。
7. `analyze` 的"丢弃数"黄金用例（第 8 条）。
8. 运行器：Windows 上的预算规则与 Linux 上同一用例得出同一结论（第 11 条）。
