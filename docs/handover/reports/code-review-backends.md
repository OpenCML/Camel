# 四个执行后端的代码评审

范围：`src/passes/sched/nodevm/`（nvm）、`fastvm/`（fvm，含 `gotovm.cpp`、`casevm.cpp`、`compile.cpp`、`jit/`）、`taskflow/`（prl）；它们共享的 `include/camel/execute/loop_driver.h`、`loop_frame.h`、`src/execute/exec_plan.cpp`、`src/execute/fault_isolation.cpp`、`src/core/error/runtime.cpp`、`include/camel/execute/limits.h`；一致性用例 `test/plans/feat/consistency/`。读的是代码，没有编译、没有运行；"失败场景"是从代码推出的，写明"需先写用例确认"的除外。没有逐行审 JIT 的 x64 编码器（`x64_encoder.h` 近两千行）、MIR 与寄存器分配，也没有审 `handoff.cpp` 里 prl 与字节码的交接细节，列在末尾。

## 总评

循环这一块共享得好：`stepLoops`/`loopBegin`/`loopTest`/`loopEnd`（`loop_frame.h`）是 nvm、fvm（字节码里的三条事件指令）、JIT（`trampoline.cpp:770` 调同一个 `loopTest`）、prl 和宏求值器共用的唯一实现，一轮的顺序、端口绑定、区间融合、`for` 的元素类型全在一处。递归深度的计数规则也写在 `limits.h` 一处。一致性用例（`consistency/*.plan.toml` 的 `backends_agree`）让四个后端同跑一个例子并比较退出码、标准输出和诊断，这个机制是对的。

问题集中在三类：（1）共享得不彻底的地方有三份拷贝（`attempt` 边界、fvm 的两个解释器、prl 内部的三条执行路径）；（2）prl 里有"悄悄退化"的路径，退化时只在 `options_.diag` 下打一行；（3）几处注释与代码已经不符，或把待决问题留在代码里。

## 严重

### 1. fvm 有两份解释器主体，每个平台只编译其中一份
`gotovm.cpp`（`ENABLE_FASTVM_COMPUTED_GOTO`，`callBorrowed` 一个函数 750 行，`gotovm.cpp:213`）与 `casevm.cpp`（`#if !ENABLE_FASTVM_COMPUTED_GOTO`，`callBorrowed` 508 行，`casevm.cpp:68`）是同一套字节码语义的两份手写实现，靠成对的宏族复制：`DEF_BIN_DIV_LABEL`/`DEF_BIN_DIV_CASE`、`DEF_BIN_MOD_*`、`DEF_CMP_OP_*`……每个平台构建时只进其中一个（GCC/Clang 走 goto，MSVC 走 switch），所以**同一份代码仓库的 CI 在 Linux 上测的是 gotovm，在 Windows 上测的是 casevm**；两份之间的分歧只会在换了平台的时候冒出来。已经看得见的差异：`casevm` 在 `default:` 抛 `PassInternalError`（`casevm.cpp:505`），`gotovm` 没有对应的分支；读写帧用的是 `currFrame->get`（casevm）与 `fastFrameGet`（gotovm）两套访问器；`safepoint` 在 goto 版有两处说明、case 版一处。
失败场景：给某个字节码加一条语义（比如新增一个整数运算的故障条件），只改了当前平台编译的那一份，另一份在另一个平台上静默旧行为。
修法：一张操作码表（X-macro）生成两份分发，语义体只写一次，两份只在"怎么跳到下一条"上不同；或者只留一份（computed goto 在 MSVC 上没有，就用 `switch` + 尾部分发，性能差距可以量，不要靠复制保留）。

### 2. `attempt` 的故障边界在 nvm、fvm、prl 各写一份，恢复簿记各不相同
`nodevm.cpp:986–1032`、`fastvm.cpp:~815–856`、`taskflow.cpp:~3020–3075`：取实参、`requireRecursionRoom`、取帧、`FaultIsolationScope`、`try`/`catch (Diagnostic)`、`isIsolatedFault`、把结果设成 `isolatedFaultToError(fault)`、否则 `wrapIsolatedResult`——每一步三份。共享的只有 `isolatedFaultToError` 与 `wrapIsolatedResult`。三份在"出错后把运行时状态还原到边界处"这一步各自为政：
- nvm 依赖 `call()` 的 RAII，只用 `ASSERT(currRecursionDepth_ == depthBefore, …)` 校验；
- fvm 的 `callElement` "counts its nested activation without a guard"，所以出错后要手工 `nestedCalls_ = nestedCallsBefore;`（`fastvm.cpp:~848`），`stackDepth_` 却只 `ASSERT`；
- prl 用 `TaskflowFaultDomain` + `InheritedFaultIsolation` 的线程局部状态，不一样的机制。
这类"手工还原"最容易在新增一个计数器时漏掉一个后端。
失败场景：在 `callElement` 里再增加一个"打开的调用数"的计数，nvm 的 RAII 自动正确，fvm 的 catch 里忘了还原，`attempt` 之后的递归限制在 fvm 上被悄悄收紧。
修法：边界做成一个共享的函数模板 `runIsolated(host, func, args, onFault)`，host 提供"取帧/调用/还原"三个服务（与 `driveLoopRounds` 的思路一致，但这次真的被三个后端调用）；把"还原"收成每个后端一个 RAII 对象，由边界持有，`catch` 里不再手写。

### 3. prl 遇到带循环的图，整张图退成逐节点串行，而且只在诊断选项下说一声
`taskflow.cpp:657`：`if (!plan.shared->loops.empty()) throw PlanMismatch{"it has a loop"};`，`:618` 捕获后把计划标成 `structured = false`，只在 `options_.diag` 下打印 "[taskflow] graph 'x' is evaluated node by node"。含循环的函数在 prl 上因此**完全没有图内并行**；训练步、优化器、任何带 `loop` 的函数都在内。这不改变结果，但"并行调度器"的性能特性对用户不可见，也不可预测。
同一个 `catch` 还吞掉其他 `PlanMismatch`：`"a node ref lies outside the node table"`、`"a node belongs to two branch arms"`、`"a JOIN is reached outside its branch region"`、`"an edge runs against the linear order"`（`:663–898`）——这些是**图的内部不一致**，是 bug，被当成"换条慢路径"悄悄处理，只有开了 `diag` 才看得到。
另外 prl 内部有三条执行路径：结构化计划（任务图）、逐节点线性（`executeLinearNode`/`executePreparedNode`）、串行图交给 FastVM 字节码（`handoff.cpp`），三条路径必须彼此一致；`forceLinear_` 开关存在，但我没有找到把结构化与线性的结果逐程序比较的用例。
修法：区分"这张图不适合并行"（带循环：`structured = false`，计入一个可见的计数，`std::profile` 里列出）与"计划不一致"（内部错误，直接 `PassInternalError`，不悄悄退化）；循环让位给 prl 的并行迟早要做，先把退化显式化；加一个用例：同一批程序在 `forceLinear_` 与结构化计划下比较输出。

## 高

### 4. 并行 `map` 的故障：报告的故障一致，但"哪些元素执行过"在后端之间不同
`ElementFaults`（`taskflow.cpp:2576`）：下标小于已知最小故障下标的元素**仍会启动**（"an element before it still runs"），大于它的不再启动，报最小下标的故障；这让报告的故障与顺序后端一致，但设计原则 3.6 写的是"已经开始的元素跑完，还没开始的不再开始"，这里"还没开始的、下标更小的"仍然开始。顺序后端（nvm/fvm）遇到第一个故障就停，下标更大的元素根本没有执行；prl 上下标更大、**已经被别的工作线程开始**的元素会跑到底，它们的效应（`println`、写 `var`）可见。结果是：带效应的元素函数在 `map` 里出故障时，各后端执行了**不同数量的元素**。原则 3.6 已经说"元素函数里的效应在并行 map 下顺序不作保证"，所以这不是违规，但应当有用例把允许的范围钉住（如：下标小于故障下标的元素必定执行完；下标大于的不保证）。`consistency/attempt.plan.toml:208` 那条用例只比较报告的故障值。
**2026-10-31 已裁**（`301c3790e`）：作者取"保实现、改 3.6 措辞"——3.6 现在写的就是这段代码在做的事（下标大于已知最小故障、还没开始的元素不再开始，下标更小的仍会开始，报下标最小的故障），代码里那条 `DESIGN-QUESTION` 标记已删，用例 `attempt.partial_fault` 把"故障之下的元素全部执行"钉在四个后端上。

### 5. `isIsolatableFault` 是一张手工白名单，默认"不隔离"
`src/core/error/runtime.cpp:246–276`：`switch` 列出可被 `attempt` 隔离的 `RuntimeDiag`，`default: return false`。新加一个运行时故障种类（例如新的张量或设备故障）而忘了加进来，`attempt` 就悄悄不隔离它，程序带位置终止——而该故障在四个后端上的表现一致（都不隔离），所以一致性用例发现不了。方向也值得商榷：从语言规则上说（原则 3.9）可被隔离的是"程序自己的计算产生的故障"，不可隔离的才是少数（内部缺陷、内存耗尽）；白名单应当改成黑名单，新增故障默认可隔离。
修法：给 `RuntimeDiag` 的定义表加一列 `isolatable`，`isIsolatableFault` 从表里读，没有第二处枚举。

### 6. prl 的 GC 握手：头注释与分配器不符，注册表锁在 500 ms 等待期间被持有
- `taskflow.h:111–113` 写着"Not covered (as before graphs ran as tasks): the collection the allocator runs inside an allocation that finds the heap full; it does not go through a poll."。`gen.cpp:674–700 allocElderUnlocked` 的注释明确说**分配点不是安全点、不收集**（只请求在下一个安全点收集；超过 1.5 倍限制抛 `MemoryExhausted`），所以头注释描述的情形在当前分配器里不存在。过时的注释会让后来的人以为 prl 有一个已知的不安全窗口。
- `TaskflowGcGate::collect`（`taskflow.cpp:~350–385`）在持有 `registryMutex_` 的整个等待循环里（最长 `kPatience = 500ms`，含 `sleep_for(50us)`）等其他线程静默；而每个新工作线程第一次触碰（`self()`，`:~295`）要拿同一把锁来注册。结果是：收集器等待期间，**新启动的线程在第一次执行前被阻塞**，最长半秒。需先写用例确认的是延迟尖峰；不会死锁（被阻塞的线程尚未活跃）。
- 整个门是进程唯一的单例（`instance()`），`backoffNs` 是函数内静态变量：同一进程里两个并行运行（Python 绑定里两个 `Context` 同时跑）共享退避状态与统计。
- `residentMegabytes()` 只在 Linux 有值，页大小硬编码 4096（`taskflow.cpp:~230`）。
修法：删掉过时的"Not covered"；等待循环里释放注册表锁（先复制线程指针列表）；门按运行（`Context`）而不是按进程。

### 7. 原地更新的计划是在一个线性化上推出的，prl 按偏序执行
（与 `code-review-memory.md` 第 10 条一致，这里给出后端侧的证据）`operatorUriFor`（`exec_plan.cpp:614`）被 nvm（`nodevm.cpp:649`）、fvm（`compile.cpp:503`）、prl（`taskflow.cpp:1978`）、宏求值器（`macro/eval.cpp:80`）共用，所以四个后端对"这个更新节点原地还是复制"的决定一致——这是好的。但计划是在 `plan.topo` 这一个线性化上推理出来的，prl 按偏序执行，能否有"读者与原地写交错"取决于同步链和顺序边是否覆盖了所有这类读写（`markNonRetainingReads` 用节点编号近似顺序）。没有 prl 上的并行读者对原地追加的用例。
修法见 `code-review-memory.md`；后端侧要加的是一个压力用例：多个并行读者与同一变量的原地更新交错，在 prl 上跑上千次并比较结果。

## 中

### 8. `loop_driver.h` 里一半是死代码，头注释描述的是旧结构
`driveLoopRounds`（`loop_driver.h:~250–303`）、`LoopShape::cond/body`、`callRound`/`beforeRound`/`iterable()`/`elementType()` 这套 `Host` 接口，全仓库没有调用者（`grep driveLoopRounds` 只在头文件里）；`loopShapeOf`（`loop_frame.h:60`）也不设 `cond/body/iterates` 之外的字段。文件头注释却写着"NodeVM, FastVM's native loop and the macro evaluator each supply only the services below"——实际它们都走 `stepLoops`。`takeLoopRound`、`startLoopState` 仍在用，其余可以删；头注释改成"这里是状态的布局和一轮后的取值；轮次的顺序在 `loop_frame.h`"。

### 9. JIT：几处抄写痕迹与一个没钉住的边界
- `x64_backend.cpp:1178`：`LDIV` 的处理里写 `!isTrapFreeConstDivisor(bc, unit.runtimeGraph, bc.opcode == OpCode::IDIV)`，在 `LDIV` 里 `bc.opcode == IDIV` 恒为假，是从 `IDIV` 复制来的；效果碰巧对（"不是 32 位"），但应当写成命名参数 `Width::W64`。
- `compileBytecodeWith`（`x64_backend.cpp:624`）一个函数 1251 行，`collectRuntimeConstructs`（`:369`）176 行；每个整数/浮点/比较操作码一块几乎相同的"取操作数—发指令—存结果"，用宏或表驱动。
- 原生整数除法依赖 `markDivision`（陷阱）把 `#DE` 变成故障；解释器走 `camel::utils::quotient`（`x % -1 == 0`、最小值除以 -1 的规则写在 `int_div.h`）。运行时除数的 `INT64_MIN / -1`、`INT32_MIN / -1`、`% -1` 在 JIT、fvm、nvm 上是否一致，我没有在 `test/cases` 里找到逐项的用例（有一批取最小值的用例，但不是除法）。**需先写用例确认**。
- 帧外代码（"framed/frameless"）的资格判定是一串 `add(pc, "OPER"/"CAST"/…, "graph needs the runtime: …")` 的字符串（`:430–487`）：判定理由是展示用的文字，行为靠 `LowerResult`；两处不会分歧，但新增一个操作码要同时改四处（枚举、判定、降低、字符串）。

### 10. 单位不一致与时间上限
`limits.h` 里递归上限在各后端的口径一致（好）；但 nvm 之外没有看到步数/时间类上限的实现入口（`checkStep|limits\.` 只在 `nodevm.cpp` 命中）。若 `limits.h` 另有时间/步数上限，应当在四个后端上各有用例；我没有找到。**需确认**这些上限是否确实只有 nvm 实现。

### 11. 注释与用例描述过时
- `consistency/attempt.plan.toml:61`："std::prl has no limit of its own and stops at the native stack"；而 `taskflow.cpp:1519` `requireCallRoom` 按 `maxRecursionDepth_` 计数（并另外检查本机栈）。描述说的是旧状态，会误导后来的人以为 prl 不计数。
- `attempt.plan.toml:208` 的描述里留着 "(std::prl reported whichever fault came first.)"：历史叙述。
- `ElementFaults` 的 `DESIGN-QUESTION`（第 4 条）。
- `loop_driver.h` 头注释（第 8 条）。
- `taskflow.h:111`（第 6 条）。

### 12. 超长函数
`NodeVMSchedPass::call`（`nodevm.cpp:243`，528 行）、fvm 的两个 `callBorrowed`（750、508 行）、`compileRuntimeGraph`（`compile.cpp:121`，538 行）、`TaskflowExecSchedPass::buildStructuredPlan`（`taskflow.cpp:649`，382 行）、`X64Backend::compileBytecodeWith`（1251 行）、`applyPassesDetailed`（`pass.cpp:1761`，172 行）。`call` 与 `callBorrowed` 是各自后端的解释器本体，大是难免的，但应当把"每种节点的执行"拆成函数，并让 nvm 的节点执行与 prl 的 `executeLinearNode` 共用同一个（它们是同一个"按计划走一个节点"的循环，prl 的线性路径又抄了一份）。

## 缺的一致性用例（按上面的发现）
1. 含循环的函数在 prl 与其他后端上的输出相同，且在 prl 上有一个可见的"未并行"计数（第 3 条）。
2. 结构化计划与 `forceLinear_` 的逐程序比较（第 3 条）。
3. 并行 `map`：元素函数带效应，故障下标之前的元素必定执行、之后的不保证；允许的集合写进用例（第 4 条）。
4. 一个新增的运行时故障种类能被 `attempt` 隔离（第 5 条；先决条件是把白名单改成表）。
5. `attempt` 之后递归限制、调用计数在四个后端上都复原：边界内外各递归到限制的 90%（第 2 条；`attempt.depth` 已有一部分）。
6. 运行时除数的 `INT_MIN / -1`、`% -1`、`/ 0` 在 JIT 原生路径上（第 9 条）。
7. 不同平台的 fvm 解释器：在 Windows 与 Linux 上各跑同一批字节码的覆盖用例，或把两份合成一份（第 1 条）。
8. 并行读者与原地追加的交错压力（第 7 条）。

## 没有覆盖到的
`x64_encoder.h`、MIR 优化与寄存器分配的正确性；`handoff.cpp` 里 prl 与 FastVM 字节码交接时安全点与帧所有权；`taskflow_frame_pool.cpp` 的帧池线程安全；`macro/eval.cpp` 作为第五个求值器与四个后端的分歧；`pass.cpp` 里流水线与后端选择。
