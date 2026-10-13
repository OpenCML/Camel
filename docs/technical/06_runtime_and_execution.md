# 运行时与执行

## 1. 概述

Camel 的运行时围绕 **Frame**（栈帧）、**Context**（全局/模块上下文）、**Data/Type**（值与类型）以及 **调度器**（Scheduler）展开。图（GIR）被编译为线性字节码或直接按图遍历，由调度 Pass 选择执行方式。

## 2. Frame（栈帧）

- **定义**：`src/core/context/frame.h`，继承自 `Object`。
- **职责**：为一次「图执行」保存运行时状态：当前图、静态区、动态区、类型信息等。

### 2.1 与图的绑定

- **graph_**：当前执行的 GIR Graph。
- **FrameMeta**：包含 `frameSize`、`runtimeDataType`、`staticArea` 等，由 `installFrameMetaInfoForGraph(graph)` 根据图安装。

### 2.2 数据槽（Slot）

- **slot_t**：`uint64_t`，一个槽位可存整型、指针或其它装箱值。
- **data_idx_t** 约定（与 GIR 一致）：
  - **正数**：动态区（运行时参数、局部）索引。
  - **负数**：静态区索引的相反数（图内编译期常量）。
  - **0**：无效/空。
- **dynamicArea_**：动态区槽位数组；**staticArea_**：静态区（Tuple），与图的 `staticDataArr_` 对应。
- **get\<T\>(index) / set(index, value)**：按 data_idx_t 读写槽位；正负分别映射到动态区/静态区。
- **codeAt(index) / typeAt\<T\>(index)**：取槽位的 TypeCode 或 Type*，用于 ACCS、FILL、OPER 等运行时类型分发。

### 2.3 调用栈

- FastVM 使用 **pcStack_**、**frameStack_** 实现栈式调用与尾调用（TAIL）。
- 注释提到「FastVM 的优化依赖于复用刚刚释放的栈帧数据」，说明帧池或复用逻辑在 `FramePool` 等处。

## 3. Context

- **Context**（`core/context/context.h`）持有模块、全局作用域、类型解析等，贯穿编译与执行。
- 各 Pass 和调度器通过 `context_ptr_t` 访问上下文，用于查找算子、类型、模块等。

## 4. 调度器类型

### 4.1 GraphSchedulePass

- 基类：`execute/sched.h` 中的 **GraphSchedulePass**，继承 **GraphIRPass**，接口为 `apply(graph, os)`，返回（可能改写后的）图。
- 具体调度器实现「如何执行图」：线性调度会驱动解释器或字节码 VM，并行调度会构建 DAG 并提交任务。

### 4.2 线性调度（Linear）

- **FastVMSchedPass**（`builtin/passes/sched/linear/fastvm/`）：
  - **precompile**：将 GIR 编译为字节码（`compile.cpp`），可选优化（`optimize.cpp`）。
  - **call**：用 **gotovm** 解释执行字节码，或（若启用 JIT）走 JIT 入口。
  - 字节码定义在 **bytecode.h**：定长（RETN, JUMP, LADD 等）、变长（BRCH, FUNC, OPER 等），布局为 Header + operands + Extra。
  - 尾调用（`TAIL`）的合法性判据见 [19_tail_call_optimization.md](19_tail_call_optimization.md)。
- **NodeVMSchedPass**（`builtin/passes/sched/linear/nodevm/`）：
  - 不生成字节码，**getTopoNodes** 得到拓扑序，按序 **eval** 各节点（Data/Port/Copy/Fill/Accs/Func/Call/Oper 等），直接操作 Frame 槽位。
  - 对部分算子（如 map_arr、reduce_arr）有专门分支，用于数组等复合类型的语义。
  - **FUNC** 尾调用优化与 FastVM 对齐（同一文档）。

### 4.3 并行调度（Parallel）

- **TaskflowExecSchedPass**（`builtin/passes/sched/parallel/taskflow/`）：
  - 将 GIR 转为 **Taskflow** 的 DAG，每个节点或一组节点对应一个 Task。
  - **buildExitTask / buildDataTask / buildPortTask / buildCopyTask / buildFillTask / buildAccsTask / buildFuncTask / buildCallTask / buildOperTask** 等为不同 NodeType 建任务。
  - **buildBranchJoinRegion** 处理 BRCH/JOIN；**connectDependencies** 按图边建立任务依赖。
  - 通过线程池执行，实现「显式依赖、隐式并行」。

#### 4.3.1 Task granularity (粒度规则)

The parallel scheduler does not make every node, or every call, a task. A scope of a graph (its
top level, or one arm of a branch) runs as a task graph only when it contains at least two items
with no path between them that are each worth a task: a higher-order operator (`map`, `filter`,
`reduce`, `foreach`, ...), a call of a *heavy* graph, or a branch one of whose arms contains such
an item. Every other scope runs inline on the calling thread, in the linear order.

A graph is heavy when its body, or the body of a graph it calls directly or indirectly,
contains a higher-order operator, an operator of a module (`tensor:matmul`, `algo:sort`: any
operator that is not one of the built-in `:...` operators), or a call of a function value (whose
callee is not known when the plan is built). A graph that only does built-in arithmetic and
comparisons and calls such graphs, like the body of `fib`, is light: a task per call costs more
than the call. A recursive graph is heavy exactly when its body is heavy apart from the
recursive calls.

The rule is static: it is decided once per graph when its plan is built, and nothing is measured
at run time. The higher-order operators decide for themselves, whatever the rule says about the
scope they sit in: the ordered ones (`reduce`, `foreach`) run their steps on the calling thread,
the unordered ones hand their elements out in measured chunks (4.3.2). `CAMEL_TF_FANOUT_ALL=1`
switches the rule off (every scope that is not totally ordered runs as a task graph) for
comparison; `CAMEL_TF_GC_STATS=1` prints the number of task graphs a run started and the number
of workers (`[taskflow] run: task_graphs=N workers=W`; scopes, and unordered operators that
asked for helpers).

#### 4.3.2 What the parallel scheduler costs, and what it is for (开销模型)

`std::prl` (`std::tf`) evaluates a graph with its own node-by-node evaluator
(`src/passes/sched/taskflow/`), on the thread that reached the graph, and hands work to other
workers in two places only: a scope with two unordered heavy items (4.3.1), and the elements of
an unordered higher-order operator. The number of workers is `-t` / `--threads`, by default the
number of hardware threads. Everything below was measured on one machine (Linux x86-64 in a
container, 18 logical CPUs of a laptop processor with two kinds of cores, release build) while
other builds were running on it; each table row comes from rounds in which the configurations
ran in turn, and only the comparison within a round means anything. Nothing here was measured
on Windows, on macOS or on an idle machine. The programs and the scripts are in
`benchmarks/parallel/` (`run.sh` repeats the tables, `stack_sample.sh` the stack samples).

**Cost model.** What a program pays on `std::prl` in addition to its operators:

| What | When | Cost now | Before (trunk of 2026-10-12) |
|---|---|---|---|
| A node of a graph | every executed node | the evaluator's dispatch on the node kind, operands read through the graph. About the cost of `std::nvm`; `std::fvm` runs the same graph as bytecode 4 to 6 times faster | the same, plus resolving every operator by its URI (two string copies, two hash lookups, the read lock of the operator table) |
| A call | every `FUNC`, `CALL` and element call | a frame from the calling thread's own list, the callee's plan from the thread's own cache, one check whether a collector is waiting | the mutex of the callee's graph twice, a hash insert and erase for the set of active frames, the shared lock of the plan table, two sequentially consistent stores |
| A built-in operator, a branch | every one | nothing (the collector's region stays held; a branch checks whether a collector is waiting) | two sequentially consistent stores each |
| A step of `reduce` / `foreach` | every element | a call | a task, in a chain of one task per element, inside a task graph per operator |
| An element of `map` / `filter` / `apply` / `unordered_foreach` | every element | a call, plus one shared counter and two clock reads per *chunk* | a task |
| Help from other workers | per unordered operator that asks for it | one task per helper (at most one per worker), their wake-up | (one task per element) |
| A scope run as a task graph (4.3.1) | every execution of that scope | a pooled task graph: one task per chain, a mutex for the pool | the same |

**How the elements of an unordered operator are run** (`runElements` in `taskflow.cpp`).
Whether an element is worth a task cannot be read off the graph: a light graph can recurse for
seconds, and a built-in operator can build a large array. So it is measured. The elements are
claimed in chunks from a shared counter by whoever works on the operator. A chunk starts at one
element; every finished chunk sets the size of the next ones to what fits 50 microseconds at the
pace just measured (at most four times the last size, at most an eighth of an even share per
worker). When the element function is *bounded and light* (built-in operators and direct calls
without recursion), the calling thread starts alone and asks for helpers only when the rest, at
the pace of its last chunk, is worth 200 microseconds or more; otherwise the helpers start at
once, so that elements of seconds each run side by side from the first. With one worker, or one
element, everything runs on the calling thread. The decision depends on time, so which thread
ran an element and how many task graphs a run started can differ between two runs of a program
with a light `map`; the result does not. The fault rule is unchanged (design principles 2.10):
elements that started run to their end, elements after the smallest faulting index known do not
start, the fault of the smallest index is reported (`ElementFaults`); within a chunk the elements
run in index order.

**Where the time went before** (stack samples of the trunk build, `stack_sample.sh`):

- `map_heavy` (64 elements, each a scalar recursion), 18 workers: 71% of all thread samples were
  inside `pthread_mutex_lock`, 84% under `TaskflowFramePool::acquire` / `release`. Every call
  took the mutex of its callee's graph twice, and all workers were running the same function.
  The run took 8 s of wall time and 30 to 49 s of CPU time for 0.25 s of work on `std::fvm`; the
  process made 1.6 to 2.4 million voluntary context switches.
- `fib` on one thread, where nothing is shared: of the samples of the working thread, 46% were in
  the frame pool (mutex, hash set), 15% in the lookup of an operator by URI, 13% in the lock of
  the plan table.
- After the changes the same sample of `fib` shows no lock: 29% is the argument copy of a call
  (`fillFrameForDirectInvoke`, shared with the other back ends), the rest is spread over the
  evaluator.

**Measurements.** Wall-clock seconds of the whole process, minimum / median over the rounds;
CPU is user + system, minimum. "before" is the trunk the work was rebased on, "after" the same
trunk with the changes. `std::prl` with the default 18 workers unless a row says otherwise.

| Program (`benchmarks/parallel/cases/`) | `std::fvm` | `std::nvm` | `std::prl` before | `std::prl` after | CPU before -> after | Rounds, load average |
|---|---|---|---|---|---|---|
| `fib` (scalar recursion, no task) | 0.17 / 0.19 | 0.63 / 1.20 | 2.21 / 3.90 | 0.87 / 2.30 | 1.96 -> 0.74 | 5, 29 to 34 |
| `fib`, one worker | | | 2.50 / 4.18 | 1.07 / 1.27 | 1.99 -> 0.74 | same |
| `small_calls` (2e6 loop iterations, two small calls each) | 0.24 / 0.42 | 0.98 / 1.40 | 4.33 / 5.12 | 1.37 / 1.52 | 2.71 -> 0.96 | 5, 29 to 35 |
| `map_light` (`map`, `filter`, `reduce` over 3e5 elements, a few operators each) | 0.05 / 0.15 | 0.11 / 0.15 | 1.12 / 1.32 | 0.08 / 0.10 | 2.30 -> 0.11 | 5, 29 to 33 |
| `map_medium` (2e4 elements of some tens of calls) | 0.21 / 0.22 | 0.75 / 0.88 | 3.52 / 3.66 | 0.14 / 0.19 | 13.9 -> 1.18 | 5, 24 to 33 |
| `map_heavy` (64 elements, a recursion of milliseconds each) | 0.25 / 0.29 | 1.07 / 1.25 | 6.99 / 8.17 | 0.26 / 0.28 | 29.9 -> 2.13 | 5, 20 to 26 |
| `collatz_reduce` (8 `map` elements, each a `reduce` over 5000) | 0.45 / 0.48 | (1.2) | 7.82 / 8.67 | 0.39 / 0.42 | 46.3 -> 2.35 | 3, 14 to 15 |
| `tensor_train` (60 SGD steps of a small MLP, after `std::macro`) | 0.22 / 0.24 | (0.3) | 0.22 / 0.25 | 0.25 / 0.26 | 0.88 -> 0.90 | 3, 15 |
| `mc_pi` (16 elements, one module operator of 0.25 s each) | 4.07 / 4.16 | (4.0) | 0.91 / 0.93 | 0.75 / 0.91 | 7.2 -> 6.5 | 3, 14 to 16 |
| `asian_mc` (16 elements, a module operator each) | 2.21 / 2.22 | | 0.44 / 0.50 | 0.48 / 0.49 | 3.4 -> 3.7 | 3, 13 to 14 |
| `merge_sort` (8e6 values, leaves sorted by a module operator) | 1.40 / 1.43 | | 0.79 / 0.83 | 0.72 / 0.83 | 2.3 -> 1.9 | 3, 12 |

The `std::nvm` figures in parentheses are from an earlier round. In a quieter round (load 6 to
9, an earlier trunk) `fib` took 0.11 s on `std::fvm`, 0.43 s on `std::nvm`, 1.34 s on `std::prl`
before and 0.52 s after; `small_calls` 0.19, 0.60, 1.61 and 0.67.

**Reading.** Before, every program whose work was written in Camel (scalar recursion, small
calls, higher-order operators with element functions in Camel) ran 12 to 30 times slower on
`std::prl` than on `std::fvm`, and with more workers it used several times the CPU without
getting faster: the workers were waiting for each other. After:

- Programs with independent coarse elements written in Camel (`map_medium`, `map_heavy`,
  `collatz_reduce`) are as fast as `std::fvm` or faster, 4 to 6 times faster than `std::prl` on
  one worker, and 3 to 5 times faster than `std::nvm`. The CPU time with 18 workers is 1.3 to 1.5
  times that with one (part of it the slower kind of core).
- Elements that are module operators (`mc_pi`, `asian_mc`, `merge_sort`) are unchanged: this is
  what the scheduler was good at, a speed-up of 2 to 5 over the linear back ends.
- A program with nothing independent in it (`fib`, `small_calls`) pays for the evaluator: it runs
  at about the speed of `std::nvm`, 4 to 6 times slower than `std::fvm`. More workers change
  nothing; they sleep.
- A light `map` over a large array (`map_light`) is no longer slower than `std::nvm` and about
  as fast as `std::fvm`; there is little to gain from more workers at a fraction of a microsecond
  per element.
- A tensor program (`tensor_train`) is the same on every back end: the time is in the kernels,
  which have their own threads, and the graph of a training step is mostly a chain.

**What `std::prl` is for, then:** programs with independent pieces of work that are each much
longer than a call: elements of a `map` / `filter` that call module operators or recurse,
independent calls of heavy graphs. For a program that is sequential by its dependencies,
`std::fvm` remains 4 to 6 times faster and `std::prl` cannot help.

**Remaining gaps, and questions that are not the scheduler's to decide.**

- `DESIGN-QUESTION(prl-light-graph-evaluator)`: the remaining factor of 4 to 6 on scalar code is
  the difference between walking the nodes of a graph and running its bytecode, not scheduling.
  It can only go away by running light graphs (the ones the granularity rule never makes tasks
  of) on the bytecode interpreter inside a worker. Candidates: (a) `std::prl` compiles light
  graphs with FastVM's compiler and runs them on a per-worker interpreter: this needs the
  interpreter to be re-entrant per thread, to take frames the collector's handshake can see, and
  to count calls and report faults the way the scheduler does; (b) a pass before `std::prl` that
  marks or outlines light regions, with the same requirements at run time; (c) leave it and say
  that scalar-heavy programs belong on `std::fvm`. (a) and (b) change how back ends are composed.
- `DESIGN-QUESTION(prl-coarsening-pass)`: for scopes, the granularity is still the static rule
  of 4.3.1: two unordered calls of heavy graphs become a task graph every time, whatever their
  size at run time (a recursive split keeps making task graphs down to its leaves), and two
  unordered calls of light graphs never do, however long they run (`fib(40) + fib(40)` runs on
  one thread). The scheduler could measure scopes as it now measures elements, but a scope has
  no counter to claim from; the alternative is a coarsening pass before `std::prl` that decides
  per call site. Not done: either changes which parts of a program run concurrently.
- `DESIGN-QUESTION(prl-thread-budget)`: the tensor kernels and the scheduler each start as many
  threads as there are hardware threads (`tensor_train` under `std::prl` has 36). `-t` limits
  the scheduler only. Whether one budget should cover both is undecided.
- The calling thread of an operator that got helpers waits for them without working when it is
  not a worker itself (the main thread): with `-t N` there are never more than N threads
  computing.
- Not verified: behaviour on an idle machine (all timings were taken under load 7 to 35), on
  more or fewer cores than 18, on Windows and macOS (the per-thread caches and the clock reads
  are portable C++, but nothing was built there), and with the generational collector as the
  optional collector (the frame pool marks active frames in the frame header instead of a set;
  the stop-the-world handshake is unchanged, and the stress tests with a verified major
  collection at every safepoint pass).

**The stack of a worker thread, and a dependency on the inside of Taskflow.** The evaluator nests
on the native stack, and the default recursion limit is measured against 8 MiB (section 4.4).
`tf::Executor` starts its workers with `std::thread`, which cannot be told a stack size: a
worker would have the process's stack limit with glibc, the executable's value on Windows (1 MiB
under `python.exe`) and 512 KiB on macOS. Taskflow has no hook for the creation of its threads
(`WorkerInterface` runs on the thread after it exists), so the build replaces the thread type:
`cmake/TaskflowWorkerStack.cmake` copies the Taskflow headers of the Conan package into the build
tree at configure time, rewrites three lines (the member `std::thread _thread;` and its accessor
in `taskflow/core/worker.hpp`, and `_workers[id]._thread = std::thread(` in
`Executor::_spawn`, `taskflow/core/executor.hpp`) to `camel::utils::BigStackThread`
(`include/camel/utils/big_stack_thread.h`: `pthread_create` / `_beginthreadex` with 8 MiB at
least, joinable and movable like the part of `std::thread` Taskflow uses), and puts the copy
before the package on libcamel's include path. Only libcamel includes Taskflow, so there is one
definition of `tf::Worker`.

This depends on how Taskflow 3.10.0 is written, not on its interface. **When the version of
Taskflow in `conanfile.txt` changes, look at it again**: the configuration stops with an error
that names the line it did not find when one of the three is gone; if Taskflow has come to
create its threads elsewhere, or uses more of `std::thread` (`get_id`, `native_handle`,
`detach`), the rewrite or `BigStackThread` has to follow, and
`-DCAMEL_TASKFLOW_WORKER_STACK=OFF` builds with Taskflow as it is (workers then have the
platform's default stack) until that is done. The class does nothing a collector has to know
about: a worker registers with the stop-the-world gate on its first safepoint
(`TaskflowGcGate::self`), whichever way its thread was started, and reads its stack bounds from
the thread itself.

### 4.4 How each back end runs a call, a fault and the end of a program

Facts established while the four back ends were made to agree (`test/plans/feat/consistency`).
They are what "the same behaviour on every back end" rests on. The numbers were measured on
Linux x86-64 with a release build; nothing here has been run on Windows or macOS.

#### 4.4.1 Calls and frames

| | A call that is not a tail call | A tail call | Frame memory |
|---|---|---|---|
| `std::nvm` | a nested activation of `NodeVMSchedPass::call` on the native stack, for a direct call (`FUNC`), a call through a function value (`CALL`) and every element call of a higher-order operator alike | rebinds graph and frame inside the same activation (`goto loop_start`) | `FramePool`, a bump allocator over one block |
| `std::fvm` | a direct call pushes its return pc and frame on the interpreter's own stack (`pcStack_` / `frameStack_`, vectors that grow up to the limit) and jumps: no native stack. A call through a function value and an element call of a higher-order operator run in a nested `callBorrowed` on the native stack | the `TAIL` bytecode: releases the frame, takes the callee's, jumps | `FramePool` |
| `std::jit` | the interpreter of `std::fvm` for every graph the JIT does not compile (any graph with an operator, or that calls another graph). A compiled self-recursion nests on the native stack through the trampolines, uncounted | compiled as a jump | `FramePool` |
| `std::tf` | `executeCallNode` -> `evalGraph` on the native stack of the thread that runs the call; a scope with independent heavy items becomes a task graph, whose tasks may run on other threads | the `for (;;)` loop of `evalGraph` | `TaskflowFramePool`: per graph, grown in chunks |
| macro evaluator (`std::macro`) | `executeFunction` -> `executeGraph` on the native stack; no tail calls, every call nests | none | its own `FramePool` |

Measured cost of one open call of `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)`:
`std::nvm` about 545 bytes of native stack and 100 bytes of frame; `std::fvm` about 100 bytes of
heap for a direct call and about 1.7 KiB of native stack for a nested activation; `std::tf`
about 520 bytes of native stack; the macro evaluator about 630 bytes of native stack a frame
(two frames a level). On an 8 MiB stack the native stack ends near 15400 open calls on
`std::nvm`, 4890 nested activations on `std::fvm`, 15860 calls on `std::tf` and 13400 frames in
the macro evaluator.

#### 4.4.2 The recursion limit

One definition for all back ends (`include/camel/execute/limits.h`): the limit is on the calls
open at once that were not tail calls; a call is a `FUNC`, a `CALL` or an element call of a
higher-order operator; the entry graph's call of `main` is not counted (`kEntryCalls`).

- `std::nvm` counts activations of `call` (`currRecursionDepth_`), minus the entry graph's.
- `std::fvm` counts the entries of its call stack plus its nested activations
  (`stackDepth_ + nestedCalls_`). Before, nested activations were not counted: a recursion
  through a function value was unbounded and overflowed the native stack.
- `std::tf` counts per thread (`ThreadState::callDepth`), and a task continues from the count of
  the thread that started it (`InheritedDepth`), so the count follows a recursion across
  workers. It applies the limit only when one is given; by default the native stack bounds it.
- All of them raise the fault with `camel::execute::throwRecursionLimitReached`, in the caller
  and before the callee's frame is taken, so the diagnostic is the same text at the same call
  expression.

Every call that nests on the native stack also asks `nativeStackNearlyExhausted()` (the
thread's stack bounds, read once per thread). When less than a sixteenth of the stack (at least
128 KiB) is left, the call is refused with the same diagnostic, saying the native stack is
full. No value the limit can be set to ends in a stack overflow. The frame pool of `std::nvm`
and `std::fvm` is sized from the limit (4 KiB a call, at most 256 MiB); filling it is the same
located diagnostic.

#### 4.4.3 Faults

- A fault is a `RuntimeFault` thrown by an operator or by the scheduler. The scheduler that
  catches it knows the node (or the bytecode pc) that was running and reports it there
  (`reportRuntimeFault`), which gives the diagnostic its source position; the report is thrown
  on as a `Diagnostic` and ends the pass.
- `std::tf` reports the first fault of a run and no other (`TaskflowExecSchedPass::raiseFault`):
  element functions that run concurrently can fault together, and each used to add its own
  diagnostic (a different number of them from run to run). Which of several concurrent faults
  is "the first" is whichever thread reports first; it is not defined. When a task fails,
  tasks that have started run to their end, tasks of the same flow that have not started do
  not start, and the thread that started the flow waits for all that started (so some of the
  other elements of a `map` run, a different number each run, each of them completely). The
  linear back ends run the elements in order and none after the one that faults. This policy
  is a recommendation awaiting a ruling: `33_fault_isolation.md`, 3.6 and section 9.
- A fault raised inside the call made by the fault-isolation operator (`attempt`) does not end
  the program: at the operator it becomes an error value. How each back end gets back to
  that boundary, and what is restored there, is in `33_fault_isolation.md`.
- Integer division: a zero divisor is `DivisionByZero` (for `/`, `%`, `/=`, `%=`, and for
  floats too). The one quotient that does not fit, the smallest integer divided by -1, wraps
  like the other integer arithmetic (`camel/utils/int_div.h`); it used to be a hardware trap
  (SIGFPE) on every back end.
- `==` / `!=` on floats compare values (IEEE), in the operator as in the bytecode `std::fvm`
  compiles them to; the operator used to compare bit patterns, so `std::nvm` and `std::tf` had
  `nan == nan` true and `-0.0 == 0.0` false.

#### 4.4.4 How a program ends

- `main` returns: the scheduler hands the value to `Context::captureProcessExitCode`, the pass
  driver sees the pipeline consumed, and the command line returns from `main` with the code.
- A fault: the `Diagnostic` unwinds the scheduler (frames released, running tasks joined), the
  pass driver records the failure, the command line prints the diagnostics after the program's
  output on standard output and returns 1 from `main`.
- `exit(n)`: the operator throws `camel::core::ProcessExit` (`camel/core/process_exit.h`). It
  unwinds the scheduler like a fault; the pass driver turns it into a completed run with exit
  code `n` and no diagnostic. `exit` and `sleep` are globals implemented in the `os` module;
  the executor manager imports a module the first time an operator of it is needed, so they
  work without `import`.

All three leave through the return of the command line's `main`, never through `std::exit` or
a signal, so the same clean-up runs: C++ streams are flushed (text written with `print`, which
does not flush by itself, is not lost), files written by the `fs` operators are complete (each
operator opens, writes and closes), and static destructors run. The Python bridge's
interpreter is never finalized; its standard output is put in line mode when the module loads
and flushed at process exit (`modules/python/module.cpp`), so Python's `print` output arrives
and stays in order. External resources still held when the run ends (a Python object is one)
are released by the command line before it returns, newest first
(`disposeAllForeignResources`, `33_fault_isolation.md` 4.2); before, a wrapper alive at the end
was never finalized, so a file a Python object held open was not flushed.

## 5. 算子与类型分发

- **operator_t**（`core/operator.h`）：算子索引类型，与 GIR 的 **OperNode** 对应。
- 运行时根据操作数类型选择具体实现（如整型加法、浮点加法），OPER 字节码或 NodeVM 的 **evalMarkedOperator** 会调用到这些实现。
- **FunctionType**：函数类型（with 参数、普通参数、返回类型），与 Graph 的 **funcType_** 一致，用于调用约定与类型检查。

## 6. 数据与类型（Data / Type）

- **Data**（`core/data/`）：运行时值（primary、composite、special 等），与 GIR 的静态数据、槽位中的装箱值对应。
- **Type**（`core/type/`）：类型系统，包含复合类型、函数类型等；GIR Node 的 **dataType_** 指向这些类型。
- **core/rtdata/**：运行时数据结构（array、string、struct、tuple、func 等），供 ACCS、FILL、CALL 等使用。

## 7. 小结

- **Frame** 是单次图执行的「栈帧」，槽位与 GIR 的 data_idx 约定一致；**Context** 提供全局环境。
- **线性执行**：FastVM（字节码 + 可选 JIT）或 NodeVM（直接拓扑序执行图）。
- **并行执行**：Taskflow 将图转为 DAG，按依赖调度到多线程。
- 算子与类型在运行时通过 **operator_t** 与 **TypeCode/Type** 分发，与 GIR 的 OPER 节点和类型信息一致。
