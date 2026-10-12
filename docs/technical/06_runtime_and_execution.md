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
at run time. The higher-order operators keep their own fan-out (one task per element) whatever
the rule says about the scope they sit in. `CAMEL_TF_FANOUT_ALL=1` switches the rule off (every
scope that is not totally ordered runs as a task graph) for comparison; `CAMEL_TF_GC_STATS=1`
prints the number of task graphs a run started (`[taskflow] run: task_graphs=N`).

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
  is "the first" is whichever thread reports first; it is not defined. The other elements of
  the operator are not stopped by a fault: in a `map` over 200 elements whose element 0
  faults, between 16 and all 199 of the others ran (and printed) before the process ended, a
  different number each run. The linear back ends run the elements in order and none after
  the one that faults. Whether elements not yet started should be skipped is not laid down.
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
and stays in order. Python objects still alive at exit are not finalized: a file a Python
object holds open is not flushed (true for a normal return as well).

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
