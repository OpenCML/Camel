# Fault Isolation (`attempt`) and Clean-up Before Termination

This note describes the fault-isolation operator and what happens to the state of a run when a
fault ends the program or is isolated. The normative text is `docs/design-principles.cn.md`
section 3.9, "空值、错误与故障": a fault ends the program with a located diagnostic, *but can
be isolated at the boundary of a subgraph*, where it becomes an error value; before a program
is ended, its output is flushed and its external resources are released.

Choices the design principles do not make are collected in section 9 as questions for the
language author. Each is implemented the way section 9 recommends and marked in the code and
in the tests with `DESIGN-QUESTION(<label>)`, so that a ruling can be applied in one place.

The name `attempt` is settled (design principles 3.9). In the C++ sources it is written in exactly one place,
`camel::execute::kFaultIsolationName` (`include/camel/execute/fault_isolation.h`); everything
else refers to the operator by its URI `:mark/isolate`, which does not contain the name.
Outside C++ the name is in the signature entry (`stdlib/signatures/globals.cml`), the
generated `docs/stdlib-reference.md`, this note, and the test programs.

## 1. Surface form and graph mapping

### 1.1 Form

`attempt` is a built-in higher-order operator. Its first argument is a function value, the
rest are the arguments that function is called with:

```camel
let b = attempt(risky, 0) !! -1                // a named function and its argument
let ys = attempt(() => xs->map<risky>) !! [-1] // a closure: any expression can be isolated
```

(The sketch in the design principles writes `!! []`. An empty array literal takes its element
type from an annotation only (`let none: int[] = []`); written in place it is a `void[]`, which
`!!` refuses, and `[] as int[]` is refused too. The fallback is a named empty array or a
non-empty literal; that is a limit of array literals, not of this operator.)

It is an ordinary global name resolved like `map` or `println`; the grammar is unchanged.
The function value may be a named function, a lambda, or any expression of a function type
(a closure returned by another function, a parameter).

### 1.2 Type

| `f` | `attempt(f, a...)` |
|---|---|
| `(A...) => T` | `T!` |
| `(A...) => U?` | `U?!` (allowed by `29`, 4.3: "may fail, and the result may be absent") |
| `(A...) => U!` | `U!` — **not** `U!!` (`DESIGN-QUESTION(attempt-fallible-target)`) |

The arguments must have exactly the parameter types of `f` (no conversion, as for every
call). Not accepted, each a compile-time error at the call (`NoMatchingFunction`):

- a function with `with` parameters (it is called through a closure that binds them);
- a function with a `var` parameter: an existing rule of the front end refuses to hand such a
  function to any operator ("an operator calls a function argument with values"). The closure
  form captures the variable instead, by reference (3.5).

When `f` itself returns `U!`, the result is `U!`: an error value `f` returned is handed on
unchanged, and a fault inside `f` becomes an error value of the same type. `T!!` is rejected
by the type system (`TaggedTypeNesting`) because the second mark adds nothing a program could
use; the same reasoning applies here, as the consumer handles "it did not produce a value"
once, with one `!!`. The kind name tells the two apart when needed.

### 1.3 On the graph

One `OPER` node whose URI is `:mark/isolate`. Its first input is the function value, the
others are the arguments — the inputs of a `CALL` node, on a node of another kind. From
`camel x.cml std::rgir` for `let b = attempt(risky, 0) !! -1` (attributes shortened):

```
subgraph main {
    subgraph risky { PORT x; DATA 100; OPER :op/div_l; EXIT }
    N1 [label="Function(graph=risky, tupleSlots=0)", type="DATA"];
    N2 [label="0", type="DATA"];
    N3 [label=":mark/isolate", type="OPER"];      // : int!
    N1 -> N3 [label="0|0", type="NORM"];
    N2 -> N3 [label="0|1", type="NORM"];
    ...                                           // BRCH/JOIN of `!! -1` (29, section 3.1)
}
```

For comparison, the call `f(0)` through a function value is `CALL` with the function on a
`WITH` edge and the argument on a `NORM` edge.

"A marked call of a subgraph" (design principles, table 2.1) is this: the node
calls the graph of the function value, exactly as `CALL` does, and the mark says that the
call is a fault boundary. The structure does not depend on what the callee contains, which is
the criterion of section 1. `:mark/...` is the existing family of operators that the
schedulers run themselves because they call back into graphs (`:mark/map_arr`, ...); a new
node kind was not added (it would have to be taught to every pass, back end and exporter for
no gain: no pass has to look inside).

### 1.4 The boundary

**The boundary is the activation of the callee graph started by that one node**, together
with everything it calls, directly or indirectly, on whatever thread.

- A fault inside the boundary ends the evaluation of everything inside it and nothing else.
  The node produces the failed form of its `T!`; the nodes of the enclosing graph run as if
  the callee had returned.
- A fault raised by evaluating the *arguments* of `attempt`, or the function value, is outside
  (those are nodes of the enclosing graph). So is the recursion-limit check made *for* the
  call: it is the caller that has no room. The enclosing `attempt`, if there is one, isolates
  those.
- **Effects that happened inside the boundary before the fault stay.** Lines already printed
  are printed, files written are written, and a `var` written inside the boundary keeps the
  value written: a `var` is a shared cell (design principles 3.5), and nothing is rolled back.
  `attempt` is not a transaction.
- Values under construction inside the boundary are dropped. Nothing of them is visible
  outside, because the only way out of the boundary is the result of the node.
- By 3.1 an `attempt` whose result is not used outside a `sync` block is not evaluated, like
  any call; by 3.2 whether the callee's effects happen is decided by the callee's own graph.

## 2. Which faults can be isolated

The error value is the fixed built-in `Error` structure (`29`, 2.3): `kind` is the name of
the fault's diagnostic without the `RuntimeDiag::` prefix, `message` is the diagnostic's
message, `location` is `file:line:column` of the place the diagnostic points at (the same
format `error(kind, message)` records; the file name without its directory).

| Fault | Isolated | Why |
|---|---|---|
| `DivisionByZero`, `IndexOutOfRange`, `FieldNotFound` | yes | faults of the program's own computation |
| `InvalidNumberText`, `FormatFailed`, `InvalidArgument`, `OperandTypeUnsupported`, `TensorDimensionMismatch` | yes | an operator refused its operands (includes failed conversions) |
| `FileOpenFailed`, `FileSystemError`, `RegexInvalid`, `TimeFormatFailed` | yes | a library operator that is not declared `T!` failed |
| `RuntimeError` | yes | the generic fault of module operators (e.g. an exception raised in Python) |
| `MaxRecursionDepthExceeded` (count reached, native stack nearly full, frame pool full) | yes | the check is made before the call that would not fit, with head room left (1/16 of the stack); unwinding to the boundary returns every frame and the whole stack above it, so the state at the boundary is the state before the call (3.5). Verified: after it, the same recursion to a depth that fits runs, on all four back ends |
| `MemoryExhausted` | **no** | it is not a `RuntimeFault` but an exception of the allocator, raised with the heap full. Building the error value allocates; nothing guarantees the unwinding freed anything (the objects may be reachable from outside). `DESIGN-QUESTION(attempt-memory-exhausted)` |
| `PassInternalError`, `GraphTooLarge`, any `InternalDiag`, a failed `ASSERT`, a C++ exception that is not a `RuntimeFault` | **no** | defects or limits of the compiler, not faults of the program. Hiding one behind `!! default` would turn a compiler bug into a wrong result |
| `MacroEvaluationFailed`, `EntryArgument*`, `EntryResultNotWritten`, `TranslationRefused`, `UnrecognizedGraphPass` | **no** | not raised while a graph runs |
| `exit(n)` | **no** | not a fault: the program asked to end. It unwinds through the boundary like any exception |

The set is an allow-list (`camel::core::error::isIsolatableFault`, one `switch`): a diagnostic
added later is not isolated until it is put on the list. That is the safe default, since the
cost of a missing entry is a located termination, the behaviour every program has today.
`DESIGN-QUESTION(attempt-fault-set)`.

Nothing here turns a fault into an error value by itself, and what counts as a fault is
unchanged. A fault outside every `attempt` ends the program as before.

## 3. How each back end returns to the boundary

### 3.1 The common part: an isolated fault is an exception that was never recorded

A fault is a `RuntimeFault` thrown by an operator or a scheduler. The scheduler that catches
it knows the node or pc and calls `reportRuntimeFault`, which **records** the diagnostic in
the sink and returns it to be thrown on. Under the command line the sink stops at the first
run-time error: recording throws `DiagnosticsLimitExceededException`, which is what actually
unwinds the scheduler of a program that ends with a fault.

Inside a boundary the fault must not be recorded (it is not a diagnostic of the run), but it
needs its message and position. So:

- `camel::core::error::FaultIsolationScope` (RAII, `camel/core/error/runtime.h`) counts, per
  thread, the boundaries open around the code that thread is running.
- `reportRuntimeFault` asks: is a boundary open on this thread, and is the fault on the
  allow-list? Then it builds the located `Diagnostic` exactly as before
  (`RuntimeErrorReporter::describe`), does **not** add it to the sink, and returns it.
  `Diagnostic::persisted` is false for it.
- Every scheduler already throws what `reportRuntimeFault` returns and already has unwinding
  paths for a `Diagnostic` in flight (they release frames and restore counters). The
  `attempt` node catches `Diagnostic`, asks `isIsolatedFault` (not recorded, on the
  allow-list), and builds the error value with `camel::execute::isolatedFaultToError`.

A program that does not use `attempt` never opens a scope: the only added work is one
thread-local read on the path of a fault that is already ending the program.

Everything not on the allow-list is recorded as before and passes the boundary as the
exception it is.

### 3.2 `std::nvm`

Every call is a nested activation of `NodeVMSchedPass::call` on the native stack. Each
activation catches `Diagnostic`, releases its tail frames and its root frame (LIFO, so the
frame pool's top is back where the caller left it), decrements `currRecursionDepth_`, and
rethrows.

```
call(main) -> evalMarkedOperator_isolate -> [scope] call(f) -> call(g) -> ... fault
                    ^ catch (Diagnostic)        each activation releases its frames
```

The node handler acquires the callee frame, opens the scope, calls, and on `Diagnostic`
writes the error value into the node's slot. Nothing else needs restoring: the handler
asserts in debug builds that the recursion depth is what it was before the call.

### 3.3 `std::fvm`

A direct call does not nest on the native stack: it pushes pc and frame on `pcStack_` /
`frameStack_`. A call through a function value and every `:mark/...` element call run in a
nested activation of `call` (`callElement`). `attempt` is a `:mark` operator (bytecode `SCHD`,
`MarkOpCode::Isolate`), so the boundary always coincides with one nested activation:

```
call#0 [pcStack: main ...] --SCHD isolate--> callElement --> call#1 [pcStack: f, g, h ...] fault
```

`call#1`'s existing handler releases every frame it pushed (`stackDepth_` back to its base),
its root frame, and restores `nestedCalls_` to its base, then rethrows. The `attempt` handler
additionally restores `nestedCalls_` (incremented by `callElement`, which does not use RAII).

### 3.4 `std::jit`

Compiled code calls no C++ and only itself (`07`, section 16). A graph that contains an
`OPER` is not compiled, so **the graph that contains the `attempt` node always runs in the
interpreter** (verified: the log says "contains OPER" for those graphs). The callee and what
it calls may be compiled. The possible stacks:

```
(a) interp(main) -> isolate -> callElement -> invokeOwnedJitFrame -> [wrapper | f f f ...]   native self-recursion
(b) interp(main) -> isolate -> callElement -> interp(f) -FUNC-> invokeOwnedJitFrame -> [wrapper | g g ...]
(c) interp(main) -> isolate -> callElement -> interp(f) -> SCHD map -> callElement -> invokeOwnedJitFrame -> [...]
```

In every shape the native frames between a wrapper and the faulting instruction belong to
one compiled function. The fault stub records kind and pc in `JitContext`, resets `rsp` to
the wrapper's, and the wrapper returns to `invokeOwnedJitFrame`, which releases the frame and
throws the `Diagnostic` from C++. **No C++ exception ever crosses a native frame**; from
there on it is the `std::fvm` case. `raiseJitFault` clears `faultKind`/`faultPc` before it
throws, so the `JitContext` is clean at the boundary; `depthBudget` is recomputed on every
entry into compiled code.

Verified with a callee that is compiled (`compiled.cml`): shape (a) with a division by zero
in straight-line code under `std::jit`, and with a native self-recursion that exceeds the
limit under `std::opt::inline std::jit`; the compiled code runs again afterwards.

The case "compiled `f` calls another graph through the interpreter" does not exist today: a
graph that calls another graph is not compiled. If trampolines are added, `07` 16.5 applies
unchanged (the trampoline must catch and re-raise through the stub); isolation adds no
requirement to it.

**Not verified: Windows x64** (calling convention, the `s_jit_save_depth` path of clang on
Windows). Only Linux x86-64 was run. No JIT code was changed.

### 3.5 What is restored at the boundary

| State | How |
|---|---|
| Frame pool (`FramePool`, `TaskflowFramePool`) | every activation releases its own frames while the exception passes; the handler releases the callee frame if the callee did not |
| Call depth (`currRecursionDepth_`; `stackDepth_`/`nestedCalls_`; `ThreadState::callDepth`) | decremented by the same handlers / RAII (`OpenCall`, `InheritedDepth`) |
| Native stack | unwound by the exception |
| Collector roots | frames are roots only while active in a pool; `RootHandle`s and `TaskflowGcRegion`s are RAII. Temporaries of the boundary are unreachable once it is left. No new root interface was added |
| JIT context | `faultKind`/`faultPc` cleared when the fault is raised; `depthBudget` set on entry |
| Scheduling state of `sync`/`wait` | none exists at run time: control edges are part of the graph, and the linear order is computed per graph, not kept in the scheduler |
| Diagnostics sink | nothing was added (3.1) |
| `var` written inside the boundary | **not rolled back** (1.4) |
| Output buffers | untouched: what was printed stays, in order |

### 3.6 `std::prl` / `std::tf`

Tasks may run on other worker threads. One fact of the existing scheduler makes isolation
possible without new machinery: **every task flow is joined by the thread that started it**
(`runOnExecutor`: `corun` on a worker, `run(...).get()` otherwise; `Subflow::join`). An
exception thrown in a task is kept and rethrown in the starting thread after the flow has
finished. So when the exception arrives at the `attempt` node, on the thread that runs the
node, no task started inside the boundary is still running.

What was global and had to become scoped is "which fault is *the* fault". The run had one
`firstFault_`; it is now per **fault domain** (`TaskflowFaultDomain`): the run has a root
domain, each `attempt` node opens one for its callee, and the domain a thread works in is
part of its `ThreadState`, inherited by every task exactly where the call depth already is
(`InheritedDepth`), together with the thread-local count of open boundaries of 3.1.

- `raiseFault` describes a fault and throws the diagnostic; it records nothing. Concurrent
  element functions can fault together, and which of their faults is the fault is decided
  where their operator has waited for them (below). The fault that arrives at the entry of
  the run is recorded in the sink there (`apply`); one that is not on the allow-list passes
  through every `attempt` node on its way.
- The `attempt` node (`executeIsolateNode`) runs the callee on its own thread, catches the
  `Diagnostic` after the flows inside were joined, and writes the error value.

Sibling work outside the boundary is in another domain and is not affected (`concurrent.cml`).

**What a failure stops** (ruled: design principles 3.9), for an isolated
fault and for one that ends the run alike:

- tasks that have started run to their end, with everything they start (there is no
  preemption; they may still print and write `var`s);
- tasks that have not started do not start: a scope skips its remaining chains
  (`FlowInstance::failed`), and an unordered operator skips the elements after the smallest
  faulting index it knows (`ElementFaults`);
- whoever started the flow waits for all that started, so nothing of a boundary runs after
  the boundary is left.

Before this work the run had one abort flag (`aborting_`) that every item of every graph
polled once a task had failed. It cut started elements short at their next node, which left
values missing in frames still in use; that was harmless for a run about to end, and is not
acceptable for a program that goes on. It is removed; no running code polls anything.

For `attempt(() => xs->map<f>)` where one element faults: on the linear back ends no element
after the faulting one runs; on `std::prl` some do, a different number from run to run, each
of them completely. *Which* effects of the other elements happen thus differs between a
linear and a parallel run, as 3.6 already says for effects in a parallel `map`.

**Which of several concurrent faults is reported** (ruled: design principles 3.6): the one
of the smallest element index; for nested operators the outer index decides first, then the
inner one. The element tasks of `map`, `apply`, `filter`, `unordered_foreach` and the chunks
of `unordered_reduce` do not let an exception escape into the executor (which would cancel the
whole flow): each keeps its failure in the operator's `ElementFaults`, an element whose index
is above the smallest faulting index known does not start, and the thread that started the
operator rethrows the fault of the smallest index after it has waited. A nested operator has
chosen its own smallest index before its fault arrives as the fault of the outer element.

For the report not to depend on which element a worker picked first, "does not start" applies
to the elements *after* the smallest faulting index: an element before it still runs, as it
would have run first on a back end that takes the elements in order. So the fault reported is
the one `std::nvm` and `std::fvm` report, on every run (`lowest_index.cml`, and
`faults/parallel_lowest_index.cml` for a fault that ends the run).
`DESIGN-QUESTION(parallel-fault-lower-elements)`: the alternative, starting nothing at all
after the first fault met, makes the report differ from run to run.

## 4. Clean-up

### 4.1 Output

Not changed, verified again (`06` 4.4.4; `test/plans/feat/consistency/cleanup.plan.toml`): a
fault, `exit(n)` and a normal return all leave through the return of the command line's
`main`; C++ streams are flushed, `fs` operators have closed their files, Python's standard
output is flushed. An isolated fault flushes nothing: what was printed stays buffered, in
order.

### 4.2 External resources: a gap that was found and closed

An external resource is a `ForeignResourceObject` (`23`): a collected wrapper whose finalizer
releases the resource when the collector finds the wrapper dead. **Wrappers still alive when
the run ended were never finalized, however the run ended** — also on a normal return: a
Python object held by `main` was never released (its `__del__` never ran, a file it held was
not flushed; `06` 4.4.4 recorded this as a known limit).

Now `camel::core::rtdata::disposeAllForeignResources()` releases every resource still held,
newest first, and the command line calls it when it leaves `main`, on every path, before the
context and the modules go away. It uses the existing `dispose()` of the control block
(idempotent; a later finalizer only drops the reference), through a registry of live control
blocks; the collector was not touched. A program that creates no foreign resource pays
nothing. `camel-db` and the embedding API do not call it yet (they do not end a process per
run); it is one call to add.

### 4.3 When a fault is isolated

Nothing is finalized at the boundary: objects created inside become unreachable and are
finalized by the next collection that finds them, like any garbage, and at the latest by 4.2
when the run ends. An external resource therefore outlives the boundary by an unspecified
time (`DESIGN-QUESTION(attempt-finalizers)`: the alternative, a collection at every isolated
fault, would make an isolated fault cost a full collection).

## 5. Passes and other libraries

- **Inlining.** The callee of `attempt` is an operand (a function value), not a `FUNC` or
  `CALL` node; the inliner replaces call nodes only, so the callee's body is never copied
  into the enclosing graph and the boundary stays a graph boundary. Inlining *inside* the
  callee is unaffected (the boundary is the callee's graph, whatever was inlined into it).
  The cost: a function called only through `attempt` keeps its call. **No pass was changed.**
  A pass that later specialises higher-order operators on a constant function value must not
  rewrite `:mark/isolate` into a call; `camel::execute::isFaultIsolationUri` is the test.
- **fold / cse / dce.** `:mark/isolate` is declared effectful in the operator-traits
  registry, as `map` is: what it does is what the function does, and its result depends on
  whether the call faults. It is never folded (3.9 on `1 / 0`: a pass must not decide a fault
  at compile time), never merged, and removed only where an effectful unused call already is.
  `std::opt` specialises a callee on a constant argument and then warns
  (`ConstantExpressionFaults`) that the division in the specialised copy always faults; it
  does not fold it, and the behaviour is unchanged.
- **`std::macro`.** The macro evaluator runs none of the operators the schedulers run
  themselves (`map`, `filter`, ..., `attempt`): a macro call with constant arguments whose
  body uses one fails to evaluate, which is a compile-time error (8), reported at the call
  with the operator named. Without `std::macro` the function is an ordinary one.
- **Autodiff, ONNX export, `std::cpp`.** Each refuses with a located diagnostic, with no
  change to them: `grad` at the `grad` call ("no derivative rule for operator
  ':mark/isolate'"); `onnx::export` at the `attempt` ("impure operator ':mark/isolate'
  cannot be exported"); `std::cpp` at the `attempt` (it names the `T!` type of the value, the
  first thing it cannot express, not the operator).
- **Safepoint.** The node is one: both entering the callee and leaving it (normally or
  through a fault) are graph boundaries, where every live value is in a frame.

## 6. Cost for programs that do not use it

No node kind, no frame field, no check on a call path was added. The changed shared code is
`reportRuntimeFault` (one thread-local read, on the fault path), in `std::tf` two more words
copied when a task inherits its context and three polls of a flag *removed* from the item
loops, and the registry insert/erase of 4.2 per foreign resource. Measurements are in
section 8.

## 7. Tests

`test/plans/feat/consistency/attempt.plan.toml` (cases under
`test/cases/consistency/attempt/`): every isolatable kind of fault; every place (direct, three
calls deep, branch arm, closure, element function of `map`/`filter`/`reduce`/`foreach`, round
501 of a tail-recursive loop); nesting; the forms of the result; effects; recursion depth;
boundaries side by side; compiled callees; 200000 rounds of fault-and-isolate; collector
stress with heap verification; parallel siblings; what is not isolated (`exit`, a fault
outside, a fault in an argument); the type rule; eight optimisation pipelines; library
functions written in Camel (`array`); refusals by `std::cpp`, `onnx::export`, `grad`; macro
functions; release of external resources for the four ways a run ends.

## 8. Verification results

Linux x86-64, release build, inside the shared build container (other work was running on
the machine; times are minima of several runs and still noisy).

- **One million isolated faults** (`many.cml`, `N=1000000`: one isolated fault a round, plus
  10000 rounds with the fault in a `map` element). Peak resident memory:

  | N | `std::nvm` | `std::fvm` | `std::jit` | `std::prl` |
  |---|---|---|---|---|
  | 10 000 | 20.6 MiB | 20.7 MiB | 21.1 MiB | 19.9 MiB |
  | 100 000 | 72.7 MiB | 72.7 MiB | 73.0 MiB | 72.3 MiB |
  | 300 000 | | | | 119.7 MiB |
  | 1 000 000 | 108.7 MiB | 109.0 MiB | 109.4 MiB | |
  | 3 000 000 | | 109.0 MiB | | |

  The growth up to about 109 MiB is the collector's heap reaching its working size (every
  isolated fault allocates an error value: two boxes, a struct, three strings); from one to
  three million rounds the peak does not move. The frame pool of `std::nvm`/`std::fvm` is
  one fixed block (4 KiB a call the limit allows; about 1 MiB when this was measured): a frame lost per fault would exhaust
  it after a few thousand rounds, and the run would end with `MaxRecursionDepthExceeded`.
  Time: about 24 µs per isolated fault on `std::fvm`, 31 µs on `std::nvm` (a C++ exception
  through several activations, the located diagnostic, the error value); `std::prl` about
  130 µs.
- **Collector.** `collector.cml` with `CAMEL_GC_VERIFY=both`, `CAMEL_GC_STRESS_ALLOC=5`,
  `CAMEL_GC_STRESS_SAFEPOINT=1`, modes `major` and `both`, on the four back ends, and with
  `CAMEL_GC_ENABLE_YOUNG_COPYING=1` on `std::nvm`, `std::fvm`, `std::prl`: correct output, no
  verification issue.
- **An `attempt` that does not fault.** Two million calls of a one-line function in a
  tail-recursive loop, directly / through a function value / through `attempt ... !! 0`:
  `std::nvm` 813 / 922 / 2385 ms, `std::fvm` 223 / 265 / 868 ms, `std::jit` 272 / 400 /
  1159 ms, `std::prl` 2718 / 3042 / 4377 ms. That is 0.3 µs (`std::fvm`) to 0.8 µs per
  `attempt` over a direct call: a nested activation, the allocation of the `T!` box, and the
  branch of `!!`.
- **Programs that do not use it.** `test/plans/perf/fib.plan.toml`, mean of 10 passes, the
  minimum of five runs of the plan before (trunk) and after (this work), in ms: `nvm` 748 /
  624, `inline_nvm` 761 / 612, `fvm` 136 / 137, `inline_fvm` 138 / 103, `jit` 187 / 164,
  `inline_jit` 6.6 / 5.8. `fib_recursive.cml` on `std::prl`, whole process, minimum of 7:
  2160 before, 1848 and 2431 in two series after. The machine's load average was about 20
  throughout, and the two builds could not be run side by side: the differences, in both
  directions, are noise of the machine. The budgets of the plan are for another machine and
  are exceeded by both builds.

## 9. Questions for the language author

Each is implemented as recommended.

- **`attempt-name`.** Ruled on (design principles 3.9): the name is `attempt`.
- **`attempt-fallible-target`.** `attempt(f)` with `f: () => U!`.
  ```camel
  func checked(x: int): int! { ... throw error('Negative', ...) ... return 100 / x }
  attempt(checked, 0)      // int!: error DivisionByZero;  attempt(checked, -4): error Negative
  ```
  Implemented: the type is `U!`; errors of `f` pass unchanged. Alternatives: reject such an
  `f` (the program writes `attempt(() => checked(x) !! d)`); a nested `U!!` (the type system
  rejects it elsewhere).
- **`attempt-fault-set`.** Which faults are isolated (section 2). Implemented: an allow-list
  of the faults of the program's computation, recursion depth included; compiler defects,
  memory exhaustion and `exit` are not. Alternatives: a deny-list (new faults isolated by
  default); excluding recursion depth; excluding the generic `RuntimeError` of modules.
- **`attempt-memory-exhausted`.** `attempt(() => build_huge()) !! [-1]` when the heap is full.
  Implemented: not isolated, the program ends with the `MemoryExhausted` diagnostic.
  Alternative: isolate it after a collection at the boundary, when that frees enough.
- *Ruled (design principles 3.9; were `attempt-parallel-siblings` and
  `attempt-parallel-first-fault`):* started elements run to their end, elements not yet
  started do not start, and of several faults the one of the smallest index is reported
  (section 3.6).
- **`parallel-fault-lower-elements`.** `range(0, 64)->map<bad>` where every element faults
  and a worker happens to run element 40 first. Implemented: after that fault only the
  elements above 40 do not start; 0 to 39 still run, element 0 faults, and its fault is the
  one reported, as on the linear back ends, on every run. Alternative: start nothing at all
  after the first fault met; the fault reported is then the smallest index among the
  elements that happened to start, and differs from run to run.
- **`attempt-finalizers`.** A resource acquired inside a boundary that faults.
  ```camel
  func hold(d: int): int sync { let r = open_resource('b'); return 100 / d }
  attempt(hold, 0) !! -1       // when is 'b' released?
  ```
  Implemented: by the next collection that finds it, at the latest when the run ends.
  Alternative: a collection at the boundary whenever a fault is isolated.
- **Arguments and the call itself** (not marked in code; it follows from "the boundary is
  the subgraph"). `attempt(f, 10 / z)` with `z == 0`, and an `attempt` made when the
  recursion limit is already reached, fault outside the boundary. Alternative: treat the
  whole call expression as the boundary.
- **A function with a `var` parameter** cannot be given to `attempt` (existing rule for all
  operators); the closure form is used. Should `attempt(f, v)` hand a variable on?
