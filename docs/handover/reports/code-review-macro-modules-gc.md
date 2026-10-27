# `std::macro`、模块系统与垃圾回收器的代码审查

范围：(1) `src/execute/macro/{macro,eval,scan,rewrite}.cpp`、`runtime.h`；(2) `src/core/context/context.cpp`（导入、候选名、搜索、`.cmo`/`.cml`）、`src/core/module/{dynamic,userdef}.cpp`、`include/camel/utils/install_layout.h`、`src/compile/gct/builder.cpp:711-824` 的 `visitImport`、五个工具入口；(3) `src/core/mm/{gen,mm}.cpp`、`include/camel/core/mm/gen.h`、`src/passes/sched/taskflow/{taskflow.cpp,taskflow.h,handoff.cpp}` 的 GC 栅栏、`rtdata` 的写屏障调用点。对照设计原则 §8、§10.2。只读代码，没有构建、没有运行；标"需确认"的是由代码推断、未实测。写屏障只核对了调用面（`array/tuple/struct/func/collection_tree`），没有逐个存储点审；`gen.cpp` 的 `markPhase` 与 `gc_debug.cpp` 的校验器没有逐行审。

按严重度排序。

## 高

### R1 分配器是进程级单例，每个 `Context` 覆盖它的根集与图清扫器，销毁任何一个都会把它们清空

- `src/core/context/context.cpp:228-231`：`Context::create` 调 `autoSpace().setObjectRootSet(&ctx->runtimeGraphMgr_->gcRoots())` 与 `setGraphSweeper(...)`；`:105-108` 析构时无条件 `setObjectRootSet(nullptr)`、`setGraphSweeper(nullptr)`。`gen.cpp:146-155` 的两个 setter 只是替换成员，没有"属于谁"的记录。
- 失败场景：同一进程里存在两个活着的 `Context`（语言服务器每个分析一个、`camel` 的某些工具在主上下文之外另建一个、测试进程依次创建与销毁）。创建 B 之后 A 的图根不再被标记，major GC 把 A 的图持有的静态数据当垃圾回收（悬空）；销毁 B 之后 A 的根集与清扫器都为空，A 的图空间永不清扫。Python 绑定因为只有一个静态 Context（`bindings/python/module.cpp:188`）而碰不到，但它也不能与别的 Context 并存。
- 修法：根集与清扫器改成登记制（`registerRootSet(owner, ptr)`/`unregister(owner)`，同 `registerExternalRootTracer` 的做法），GC 遍历所有登记项；或者明确规定"一个进程一个 Context"并在第二次 `create` 时抛错。

### R2 `.cmo` 找到了但加载失败，会悄悄改试下一个候选名乃至同名内建模块

- `context.cpp:429-452`：`loadModuleFrom` 返回空（ABI 不符、缺符号、依赖 DLL 缺失，`dynamic.cpp:403-451`）时 `acceptModule` 返回空，循环 `continue` 到下一个候选名；候选里还有 `getBuiltinModule(name)`（`:445-454`）。加载失败的原因只记在 `lastCmoLoadError_`，只有"所有候选都失败"时才进入报错文本。
- 失败场景：磁盘上的 `math.cmo` 与宿主 ABI 版本不符，而内建里有同名的 `math`：导入静默落到内建模块，用户拿到的是另一份实现，没有任何提示；候选名之间也有串味（`a.b.foo.cmo` 坏了会退到 `foo`）。
- 修法：文件命中后的加载失败应当终止这一次导入并报 `ModuleNotFound`/`ImportedModuleHasErrors`，带 `.cmo` 的路径与原因；不要继续试别的候选名。

### R3 "同名的任意文件"都被当作模块，且选中哪个取决于目录的列举顺序

- `context.cpp:643-690`（`findModuleUnder`）：每个目录列一次，`files.byStem.try_emplace(stem, path)` 取**目录迭代的第一个**同名（不含扩展名）文件，只有 `.cmo` 优先。函数头注释自己写着"`D/xx.*` (any text/source file with the same name)"。
- 失败场景：目录里有 `foo.cml` 和 `foo.md`、`foo.json`、`foo.cml.bak` 之外的 `foo.txt`、编辑器遗留的 `foo~`——`import foo` 可能解析到 `foo.md`，把 Markdown 当源码解析；文件系统不同、同一目录的结果不同。目录列表按 `Context` 缓存且**永不失效**（`directoryListings_`，`context.cpp:671,690`），语言服务器里新建的文件看不到。
- 修法：只认 `.cml` 与 `.cmo`（原则 10.2 说"文件即模块"，没有说"任意扩展名"）；同一目录里 `.cml` 与 `.cmo` 并存时有明确优先序（现在是 `.cmo`，且 `.cml` 作为声明模块伴随，`:850-870`）；缓存加上目录修改时间或在语言服务器的重新分析时清空。

### R4 入口模块在五个工具里都被命名为 `"main"`，与原则 10.2 相反

- 原则 10.2："A file is a module, and the module's name is the file's name … there is no placeholder name `main`, for the entry file too (the pass-name prefix is `counts` for both `./counts.cml` and `counts.cml`)."
- 代码：`tools/camel-cli/main.cpp:274`、`tools/camel-cli/timeit.cpp:259`、`tools/codegen/main.cpp:82`、`tools/debugger/src/compile.cpp:68`、`src/service/semantic/analyze.cpp:512` 都是 `make_shared<UserDefinedModule>("main", path, ctx, parser)`；`diagnostic.moduleName = "main"`（`main.cpp:589`）。`Module::name()` 又是导入解析的基准（`importModule` 里的 `importerName`，相对导入与"向上前缀回退"都用它，`context.cpp:420-430,578-606`）。
- 失败场景：入口文件里写 `shapes::area`（用自己的名字限定）解析不到；`import .x` 这类相对导入以 `"main"` 为基准而不是文件名；通过路径判断"入口自己"只靠 `fs::equivalent`（`:871-880`）。测试用例大量仍写 `module foo` 头（如 `test/cases/feat/bindings/python_rules.cml`），说明原则里"没有 module 声明"与代码没有同时落地。
- 修法：先裁定哪一边是目标状态；若原则是目标，五处入口统一用一个函数 `moduleNameOfFile(path)`（取文件主名），并在这里去掉对 `"main"` 的依赖。

## 中

### R5 STW 靠协作式栅栏，等不到就放弃并退避，之后不升级

- `taskflow.cpp:326-337,405-420`：收集线程设置 `stopRequested_`，最多等 500 ms 让其余线程离开区域；超时则 `gaveUp++` 并把下次重试推迟 10 ms、翻倍到上限 1 s（`kMaxBackoffNs`），**这次不收集**。`TaskflowGcRegion::safepoint` 也只在 `collectorWanted()` 时让出。
- 失败场景：有一个工作线程长时间处在不 poll 的算子里（网络/文件读、`Python` 调用、等 GPU 同步、`sleep`）——每次收集都放弃，堆无限增长。`CAMEL_GC_HEAP_LIMIT_MB` 在这种情况下也不起作用：分配只置 `heapLimitExceeded_`，要在下一个 safepoint 才处理（`gen.cpp:560-566,799-815`），而 safepoint 被放弃。结果是内存上限在并行后端下是建议性的。
- 正确性方面（做对了的）：栅栏是 Dekker 式的（`enter` 先置 `active` 再读 `stopRequested_`，seq_cst），放弃时不收集，因此没有"线程还在跑就搬动对象"的路径。
- 修法：放弃次数超过阈值时升级——报一条可见的诊断（含哪个线程在区域里、在哪个算子），并让阻塞类算子在进入阻塞前显式 `leave()` 区域（它们本来就不持有帧外的 GC 指针）。

### R6 GC 的环境变量：解析失败静默变成"没设"或"开着"

- `mm.cpp:41-60`：`envSize` 解析失败返回 0（`"4MB"`、`"1e3"` → 0），调用方把 0 当作"用默认/不限制"；`envFlag` 把除 `0/false/off/no` 以外的任何非空串都当真（`"maybe"`、`"disable"` → 开）。
- 失败场景：`CAMEL_GC_HEAP_LIMIT_MB=2GB`、`=1e3` 静默变成**没有内存上限**；`CAMEL_GC_POISON=disable` 打开了每个 safepoint 都搬动全部年轻对象的毒化模式（性能骤降，且 `CAMEL_GC_POISON implies CAMEL_GC_GENERATIONAL`，`gc_debug.cpp:29-30`，还改变收集器类型）。
- 同模块共有约 17 个 `CAMEL_GC_*` 开关（`mm.cpp:66,107-126,157,200,238-272`），既有压力测试用的，也有生产参数（`NURSERY_MB`、`LARGE_BUDGET_MB`、`HEAP_LIMIT_MB`）。
- 修法：解析失败时报错并说明变量名与合法形式；调试类开关（`STRESS_*`、`POISON`、`VERIFY`、`LOG_MOVES`）与生产参数分开命名并集中在一个表里读取，表里同时给出文档字符串。

### R7 所有分配经过一把全局互斥量；old→young 记忆集以对象为粒度

- `gen.cpp:62-75`：`alloc()` 每次都取 `mutex_` 并建 `AllocationScope`；并行后端的所有线程在此串行。写屏障的快路径无锁，但慢路径（老对象存入年轻引用）也取同一把锁（`:266-300`）。
- `gen.cpp:906-940`：记忆集条目是整个老对象，每次 minor 对条目 `updateRefs` 整个对象，随后 `hasYoungRefsUnlocked` 再整个走一遍。一个已晋升的百万元素槽位数组每增加一个年轻元素，每次 minor 都扫描 100 万个槽位（两遍）——循环里往老数组追加字符串/记录会变成二次复杂度。没有卡表。
- 修法：线程本地分配缓冲（TLAB）把锁频率降到每缓冲一次；记忆集对数组/元组记录被改动的下标段（卡）。

### R8 收集过程中抛出异常，堆停在半搬动状态；重入被静默吞掉

- `gen.cpp:854-860`：`if (inGC_) return; // Reentrancy guard`——重入是编程错误，却被当作无事发生；`:995-1010` 的 `catch (...)` 只清扫描状态、复位 `inGC_` 后重新抛出，此时 haven/cache 已交换、部分对象已转发、`rememberedSet_` 已清空。晋升时宿主拒绝内存（`bad_alloc`）会走到这里。
- 失败场景：异常作为普通故障向上传播，若被 `attempt` 之类接住，程序在一个半搬动的堆上继续运行。
- 修法：收集内部的异常一律视为致命（记录后 `std::terminate`/abort），重入改为断言。

### R9 `std::macro`：把内部错误当作用户的宏求值失败

- `eval.cpp:337-346`（`tryEvaluate` 末尾）`catch (const std::exception &e) { noteFailure(candidate, e.what()); }`。`std::logic_error`（如 `gen.cpp` 里的 "GC invariant violated"）、断言抛出、`bad_alloc`、`MemoryExhausted` 都落在这里，被报告成"宏求值失败: …"附在用户的调用位置上。
- 失败场景：编译器自身的缺陷（栈检查、GC 不变量）表现为用户代码里的一个宏错误；`MemoryExhausted` 也被吃掉，进程以为只是这个调用失败，继续处理下一个候选。
- 修法：只接 `MacroExecutionError`、`Diagnostic`、`RuntimeFault`；其他异常原样向上。

### R10 宏失败报告的次序与注释不符；故障格式化有三份

- `eval.cpp:435-480`（`reportFailures`）注释写"the first refusal in source order is its own place and reason"，代码取 `failures.front()`——候选收集顺序（可达图遍历顺序），不是源码位置序。对比 `autodiff` 的 `Engine::refuse` 是按位置排序的（`pullback.cpp:4131-4195`）。同一次展开的多个拒绝，哪个当主故障取决于图的遍历顺序。
- 故障转文本的拼装有三份：`eval.cpp:309-335` 手写的 `{i}` 替换、`bindings/python/module.cpp:696-713` 的 `DiagnosticBuilder::commit(a[0..9])`、`src/execute/standalone_bridge.cpp`（注释称同一构造）。
- 修法：排序后再取首条；格式化走 `DiagnosticBuilder` 一处。

### R11 宏求值器是第四个图解释器，且没有跨轮缓存

- `eval.cpp:830-1115`（`executeGraph`，约 285 行）自己实现了 `Cast/Copy/Brch/Join/Loop…` 的执行，包括与调度器相同的 "(tail, join) 弧对"（注释"Same as the schedulers"）。两处规则要同步维护；循环轮数、递归深度、栈检查也各写一份（`macroDepthLimit`、`nativeStackNearlyExhausted`）。
- 每一轮都新建 `MacroEvaluator`（`macro.cpp:27-30`，含一个 `framePoolBytes(macroDepthLimit)` 大小的帧池）；没能求值的候选（参数还不是常量）下一轮重新被收集、重新走 `noteNotStatic`，N 个这样的调用 × R 轮是 O(NR)；相同参数的纯宏调用也不去重（原则 §8 说宏有副作用时副作用会发生，这点是对的，但无副作用的宏可以缓存）。
- `firstReport`（`eval.cpp:399-406`）用函数内静态集合去重"stays a run-time call"警告，进程内永不清空：同一进程里第二次运行（语言服务器、Python 绑定）同样的警告被吞掉。
- 修法：求值器复用某个已有的节点 VM（`NodeVMSchedPass::callGraph`，绑定里已经这样用）；`firstReport` 的集合挂在 `Context` 上。

### R12 模块搜索逻辑与模块根的构造重复

- `context.cpp` 里"遍历 `searchPaths`，相对路径以入口目录为基"的循环出现三次（`getModulePathAndKind` `:695-710`、`getModuleSearchDiagnostics` `:790-800`、`getSearchPathBases` `:815-830`），诊断重放"实际试过的顺序"靠人工保持一致。
- 模块根的构造 `buildModuleSearchPaths` 在六个调用点各自传选项：`tools/camel-cli/main.cpp:219`、`timeit.cpp:195`（`buildSearchPathsForEntry`）、`tools/codegen/main.cpp:53`（不带 `-I`）、`tools/debugger/src/compile.cpp:54`（不带 `-I`）、`service/semantic/analyze.cpp:482`、`bindings/python/module.cpp:201`。codegen 与调试器因此忽略 `-I`/`--include`，与 `camel` 的行为不同。库搜索目录还有 `buildHostLibrarySearchDirs`、`buildAppLibrarySearchDirs`、`registerRuntimeSearchDirs` 三份各自从安装根推。
- 修法：把"模块根 + 入口目录 + 选项"收成一个 `ModuleResolver`，由 `Context` 持有，三种用途（查找、诊断、列根）都从它出；工具入口只传命令行选项。

### R13 环境依赖：`CAMEL_HOME` 不校验，保留了"遗留"变量，缺省值静默回落到当前目录

- `install_layout.h:127-163`：`CAMEL_HOME` 设了就用，不检查目录存在或是否像一个安装；`CAMEL_MODULE_PATH` 标注为 legacy 仍被读取（项目约定内测阶段不保留兼容层）；`CAMEL_STD_LIB` 另一个覆盖口；取不到可执行文件目录时 `exeDir = current_path()` 并把它加入模块根（`:213-217`）。
- 对宿主进程的影响：`bindings/python` 里 `executableDirectoryRaw()` 是 Python 解释器所在目录，没有 `CAMEL_HOME` 时模块根里有 `<python>/`、`<python>/stdlib`、`<python>/../stdlib`、安装根推到的 `/usr` 等（`:187-206`），与 Python 自己的 `stdlib` 目录重名。
- 修法：`CAMEL_HOME` 指向无效目录时报错；删 `CAMEL_MODULE_PATH`；取不到可执行文件目录时报错而不是用当前目录；宿主进程（Python）必须显式给出根。

### R14 `.cmo` 加载器：核心里硬编码模块名，全局放宽 DLL 搜索路径，握手只有一个整数

- `dynamic.cpp:386-395`：`if (moduleName == "python" || moduleName == "pyplot")` 在通用加载器里分支去找 Python 桥接库；用户目录里一个叫 `python.cmo` 的第三方模块同样触发。
- `:350-368`：`registerRuntimeSearchDirs` 把 `.cmo` 所在目录、安装根的 `libs`、`bin`、安装根本身加入**进程全局**的库搜索路径（`addLibrarySearchPath`），永不撤销；在 Windows 上这是 DLL 植入面（模块目录里的同名依赖库优先被加载）。
- `:419-433`：兼容性握手只有 `camel_module_abi_version()` 一个整数（当前 3）；没有编译器/标准库版本，类型表布局或 `Object` 虚表变动而忘了加版本号时，加载成功但行为未定义。
- 句柄从不关闭：`Context::addLoadedDll` 只存进 map（`context.h:447`），析构不 `closeDll`——这是对的（类型表里模块注册的类型对象带虚表，不能卸载），但没有注释说明，且同路径二次登记会覆盖句柄。
- 修法：Python 桥接作为 `.cmo` 声明的依赖由模块自己的加载钩子完成；库搜索路径只在加载这一个 `.cmo` 期间生效；握手加上构建标识；在 `addLoadedDll` 处写明"永不卸载"。

### R15 导入候选名里有原则 10.2 没有的"向上前缀回退"

- `context.cpp:593-607`：非相对导入除原名外，还依次试 `currentModule + "." + name`、去掉末段的前缀…直到空前缀（`i=0` 又重复原名一次）。原则 10.2 对 `import` 的描述是"自己的目录 → 模块根 → 标准库"，没有按导入者名字逐级上溯的规则；与 R4 叠加后（入口名为 `main`）更难预测。
- 修法：删除，或写进原则并给出一个例子。

## 低

- `gen.cpp`：`safepoint`、`minorGC`、`majorGC` 三个函数体（`:332-420`）几乎逐行相同；自测代码（`debugRunRememberedSetSelfTest` 96 行、`debugRunPoisonSelfTest` 42 行）放在生产源文件里（已在 `code-review-memory.md` 提过类似项）。
- `taskflow.cpp:326-337` 把测试需求写进注释（`3,525 of the 3,532 polls of the _prl arms of test/plans/feat/mm/gc_collections.plan.toml`），应改成性质描述。
- `eval.cpp` 的 `tryExecuteStaticOper` 165 行、`executeGraph` ≈285 行；`context.cpp` 的 `importModule` 约 110 行。
- 历史叙述型注释：`taskflow.cpp:318-322`（"skipped 3,525 of the 3,532 polls … and left them with 12 collections"）、`install_layout.h:156-161`（"Baking it in here made it a root for every library too"）、`context.cpp:449-452`（"as before"）、`macro.cpp:30-34` 尚可，`eval.cpp:309-335` 一带（"A fault that refused several places carries one report per place"）是现状描述，保留。
- 写屏障靠每个存储点显式调用（`Array/Tuple/Struct::set`、`Function` 构造），`ownerType` 允许为 `nullptr`（`rtdata/func.cpp:93`）；收集器依靠"屏障与分配点记录的对象类型旁表"（`recordObjectTypeUnlocked`）来扫描没有静态类型的老对象（`gen.cpp:918-922`）。绕过 `set` 直接写 `data()` 的代码路径我在 `src/` 与 `modules/` 里没有找到（`grep` 无结果），但没有机制阻止，建议用一个调试期的堆校验（`CAMEL_GC_VERIFY`）在压力测试里常开。
- 终结器在分配器锁内执行（`finalizeDeadYoungUnlocked`、`sweepOldGen` 的 `finalizeObjects`），靠"终结器只入队、延迟释放在锁外运行"（`runDeferredReleases`）规避死锁；这个规则只存在于约定里（上一轮 `code-review-memory.md` 第 7 项）。`PyObject` 的删除器在 `runDeferredReleases` 里取 GIL（锁外），与 `taskflow` 的 `collectorMutex_` 仍在持有：只要没有持 GIL 的线程停在栅栏上就不会死锁（目前 Python 算子不 poll，所以成立）。

## 逐项核对

| 项 | 结论 |
|---|---|
| 原则 §8：宏求值失败是运行期故障；多个拒绝一次报告 | 做到（`reportFailures` → `MacroEvaluationFailed`，其余作为 related）；主故障的次序见 R10。 |
| 原则 §8：宏有副作用就照常发生；循环最多 100 万轮 | 副作用按 "macro" 阶段记录（`MacroStageScope`）；轮数上限走 `macroDepthLimit`（需确认与"100 万轮"是否同一个值）。 |
| 宏求值与 GC | 求值器的值根、帧池、`valueRoots_` 都登记为根（`registerExternalRootTracer`），草稿在最后一次求值后才创建，注释说明了原因。没有发现漏根。 |
| 模块：命名、重命名、命名空间导入 | `visitImport`（`builder.cpp:711-824`）覆盖 `import m`、`import m as x`、`import {a as b} from m`、`from m::N`；自导入无效果；冲突由后续阶段报告。未发现问题。 |
| 模块：`.cml`+`.cmo` 配对 | `.cmo` 优先，旁边的 `.cml` 作为声明模块并继承导出（`context.cpp:850-870`）；加载失败的处理见 R2。 |
| GC：根 | 根集（图）、外部追踪器、`RootHandle` 三种；多 Context 的问题见 R1。 |
| GC：写屏障 | 屏障条件正确（老→年轻、"刚出生即老"集合 `freshOld_`）；调用面见低项。 |
| GC：终结器 | 年轻与老年代都在收集内调用 `finalize()`，释放通过延迟队列在锁外进行。 |
| GC：与并行后端 | 见 R5、R7。 |

## 测试缺口

1. 两个 `Context` 并存/先后销毁后的 major GC（R1）。
2. 磁盘上的 `.cmo` ABI 不符且存在同名内建模块时的导入结果（R2）。
3. 目录里存在 `foo.md`、`foo.txt` 与 `foo.cml` 时的解析，及新建文件后的再次导入（R3）。
4. 入口模块名随文件名变化的用例（R4：`counts.cml` 与 `./counts.cml` 同名，入口里 `counts::f`）。
5. 栅栏放弃路径：一个长时间不 poll 的算子 + 持续分配，断言堆有界或有诊断（R5）。
6. `CAMEL_GC_*` 的非法值（R6）。
7. 宏求值里 `std::logic_error`/`bad_alloc` 不被吞（R9）；多个拒绝的主故障位于源码最靠前处（R10）。
8. 模块根：`-I` 对 `camel`、codegen、调试器、Python 绑定行为一致（R12）。
