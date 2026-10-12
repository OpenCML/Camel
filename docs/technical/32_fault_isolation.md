# Fault Isolation (`attempt`) and Clean-up Before Termination

This note describes the fault-isolation operator and what happens to the state of a run when a
fault ends the program or is isolated. The normative text is `docs/design-principles.cn.md`
section 2.9, "空值、错误与故障": a fault ends the program with a located diagnostic, *but can
be isolated at the boundary of a subgraph*, where it becomes an error value; before a program
is ended, its output is flushed and its external resources are released.

Choices the design principles do not make are collected in section 9 as questions for the
language author. Each is implemented the way section 9 recommends and marked in the code and
in the tests with `DESIGN-QUESTION(<label>)`, so that a ruling can be applied in one place.

The name `attempt` is provisional. It is written in exactly one place,
`camel::execute::kFaultIsolationName` (`include/camel/execute/fault_isolation.h`); everything
else refers to the operator by its URI `:mark/isolate`, which does not contain the name.

## 1. Surface form and graph mapping

### 1.1 Form

`attempt` is a built-in higher-order operator. Its first argument is a function value, the
rest are the arguments that function is called with:

```camel
let b = attempt(risky, 0) !! -1                // a named function and its argument
let ys = attempt(() => xs->map<risky>) !! []   // a closure: any expression can be isolated
```

It is an ordinary global name resolved like `map` or `println`; the grammar is unchanged.
The function value may be a named function, a lambda, or any expression of a function type
(a closure returned by another function, a parameter).

### 1.2 Type

| `f` | `attempt(f, a...)` |
|---|---|
| `(A...) => T` | `T!` |
| `(A...) => void` | `void!` |
| `(A...) => U?` | `U?!` (allowed by `29`, 4.3: "may fail, and the result may be absent") |
| `(A...) => U!` | `U!` — **not** `U!!` (`DESIGN-QUESTION(attempt-fallible-target)`) |

The arguments must have exactly the parameter types of `f` (no conversion, as for every
call). A function with `with` parameters is not accepted (wrap the call in a closure). `var`
parameters are accepted: `attempt` hands the same references on.

When `f` itself returns `U!`, the result is `U!`: an error value `f` returned is handed on
unchanged, and a fault inside `f` becomes an error value of the same type. `T!!` is rejected
by the type system (`TaggedTypeNesting`) because the second mark adds nothing a program could
use; the same reasoning applies here, as the consumer handles "it did not produce a value"
once, with one `!!`. The kind name tells the two apart when needed.

### 1.3 On the graph

One `OPER` node whose URI is `:mark/isolate`. Its first input is the function value, the
others are the arguments — the inputs of a `CALL` node, on a node of another kind:

```
main:
  DATA   Function(graph=risky, tupleSlots=0)
  DATA   0
  OPER   :mark/isolate : int!        <- [norm] Function(risky), 0
  ...                                   (the BRCH/JOIN of `!! -1`, as in 29 section 3.1)

  CALL                 : int         <- [with] Function(risky); [norm] 0     (for comparison)
```

"A marked call of a subgraph" (design principles, table of section 1) is this: the node
calls the graph of the function value, exactly as `CALL` does, and the mark says that the
call is a fault boundary. The structure does not depend on what the callee contains, which is
the criterion of section 2.9. `:mark/...` is the existing family of operators that the
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
  value written: a `var` is a shared cell (design principles 2.5), and nothing is rolled back.
  `attempt` is not a transaction.
- Values under construction inside the boundary are dropped. Nothing of them is visible
  outside, because the only way out of the boundary is the result of the node.
- By 2.1 an `attempt` whose result is not used outside a `sync` block is not evaluated, like
  any call; by 2.4 whether the callee's effects happen is decided by the callee's own graph.

## 2. Which faults can be isolated

The error value is the fixed built-in `Error` structure (`29`, 2.3): `kind` is the name of
the fault's diagnostic without the `RuntimeDiag::` prefix, `message` is the diagnostic's
message, `location` is `file:line:column` of the place the diagnostic points at (the same
format `error(kind, message)` records).

| Fault | Isolated | Why |
|---|---|---|
| `DivisionByZero`, `IndexOutOfRange`, `KeyNotFound`, `FieldNotFound`, `MatchNotExhaustive` | yes | faults of the program's own computation |
| `InvalidNumberText`, `FormatFailed`, `InvalidArgument`, `OperandTypeUnsupported`, `TensorDimensionMismatch` | yes | an operator refused its operands (includes failed conversions) |
| `FileOpenFailed`, `FileSystemError`, `JsonDecodeFailed`, `JsonEncodeUnsupported`, `RegexInvalid`, `TimeFormatFailed` | yes | a library operator that is not declared `T!` failed |
| `RuntimeError` | yes | the generic fault of module operators (e.g. an exception raised in Python) |
| `MaxRecursionDepthExceeded` (count reached, native stack nearly full, frame pool full) | yes | the check is made before the call that would not fit, with head room left (1/16 of the stack); unwinding to the boundary returns every frame and the whole stack above it, so the state at the boundary is exactly the state before the call (section 3.5) |
| `MemoryExhausted` | **no** | it is not a `RuntimeFault` but an exception of the allocator, raised with the heap full. Building the error value allocates; nothing guarantees the unwinding freed anything (the objects may be reachable from outside). `DESIGN-QUESTION(attempt-memory-exhausted)` |
| `PassInternalError`, `UnrecognizedOperatorURI`, `UnsupportedBytecode`, `OperatorNotImplemented`, `GraphInliningFailed`, `GraphTooLarge`, any `InternalDiag`, a failed `ASSERT`, a C++ exception that is not a `RuntimeFault` | **no** | defects or limits of the compiler, not faults of the program. Hiding one behind `!! default` would turn a compiler bug into a wrong result |
| `MacroEvaluationFailed`, `EntryArgument*`, `EntryResultNotWritten`, `MissingMainFunction`, `TranslationRefused`, `UnrecognizedGraphPass` | **no** | not raised while a graph runs |
| `exit(n)` | **no** | not a fault: the program asked to end. It unwinds through the boundary like any exception |

The set is an allow-list (`camel::core::error::isIsolatableFault`, one `switch`): a diagnostic
added later is not isolated until it is put on the list. That is the safe default, since the
cost of a missing entry is a located termination, the behaviour every program has today.
`DESIGN-QUESTION(attempt-fault-set)`.

Nothing here turns a fault into an error value by itself, and what counts as a fault is
unchanged. A fault outside every `attempt` ends the program as before.

## 3. How each back end returns to the boundary

### 3.1 The common part: an isolated fault is an exception that was never recorded

Today a fault is a `RuntimeFault` thrown by an operator or a scheduler. The scheduler that
catches it knows the node or pc and calls `reportRuntimeFault`, which **records** the
diagnostic in the sink and returns it to be thrown on. Under the command line the sink stops
at the first run-time error: recording throws `DiagnosticsLimitExceededException`, which is
what actually unwinds the scheduler.

Inside a boundary the fault must not be recorded (it is not a diagnostic of the run), but it
needs its message and position. So:

- `camel::core::error::FaultIsolationScope` (RAII, `camel/core/error/runtime.h`) counts, per
  thread, the boundaries open around the code that thread is running.
- `reportRuntimeFault` asks: is a boundary open on this thread, and is the fault on the
  allow-list? Then it builds the located `Diagnostic` exactly as before, does **not** add it
  to the sink, and returns it. `Diagnostic::persisted` is false for it.
- Every scheduler already throws what `reportRuntimeFault` returns and already has unwinding
  paths for a `Diagnostic` in flight (they release frames and restore counters). The
  `attempt` node catches `Diagnostic`, checks `!persisted` and the allow-list, and builds the
  error value with `camel::execute::isolatedFaultToError`.

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

The node handler opens the scope, acquires the callee frame, calls, and on `Diagnostic`
writes the error value into the node's slot. Nothing else needs restoring: the handler
asserts in debug builds that the recursion depth is what it was before the call.

### 3.3 `std::fvm`

A direct call does not nest on the native stack: it pushes pc and frame on `pcStack_` /
`frameStack_`. A call through a function value and every `:mark/...` element call run in a
nested activation of `call` (`callElement`). `attempt` is a `:mark` operator, so the boundary
always coincides with one nested activation:

```
call#0 [pcStack: main ...] --SCHD isolate--> callElement --> call#1 [pcStack: f, g, h ...] fault
```

`call#1`'s existing handler releases every frame it pushed (`stackDepth_` back to its base),
its root frame, and restores `nestedCalls_` to its base, then rethrows. The `attempt` handler
additionally restores `nestedCalls_` (incremented by `callElement`, which does not use RAII).

### 3.4 `std::jit`

Compiled code calls no C++ and only itself (`07`, section 16). A graph that contains an
`OPER` is not compiled, so **the graph that contains the `attempt` node always runs in the
interpreter**. The callee and what it calls may be compiled. The possible stacks:

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

The case "compiled `f` calls another graph through the interpreter" does not exist today: a
graph that calls another graph is not compiled. If trampolines are added, `07` 16.5 applies
unchanged (the trampoline must catch and re-raise through the stub); isolation adds no
requirement to it.

Not verified: Windows x64 (calling convention, the `s_jit_save_depth` path of clang on
Windows). Only Linux x86-64 was run.

### 3.5 What is restored at the boundary

| State | How |
|---|---|
| Frame pool (`FramePool`, `TaskflowFramePool`) | every activation releases its own frames while the exception passes; the handler releases the callee frame if the callee did not |
| Call depth (`currRecursionDepth_`; `stackDepth_`/`nestedCalls_`; `ThreadState::callDepth`) | decremented by the same handlers / RAII (`OpenCall`, `InheritedDepth`) |
| Native stack | unwound by the exception |
| Collector roots | frames are roots only while active in a pool; `RootHandle`s and `TaskflowGcRegion`s are RAII. Temporaries of the boundary are unreachable once it is left |
| JIT context | `faultKind`/`faultPc` cleared when the fault is raised; `depthBudget` set on entry |
| Scheduling state of `sync`/`wait` | none exists at run time: control edges are part of the graph, and the linear order is computed per graph, not kept in the scheduler |
| Diagnostics sink | nothing was added (3.1) |
| `var` written inside the boundary | **not rolled back** (1.4) |
| Output buffers | untouched: what was printed stays, in order |

### 3.6 `std::prl` / `std::tf`

Tasks may run on other worker threads. Two facts of the existing scheduler make isolation
possible without new machinery:

1. **Every task flow is joined by the thread that started it** (`runOnExecutor`: `corun` on a
   worker, `run(...).get()` otherwise; `Subflow::join`). An exception thrown in a task is
   kept and rethrown in the starting thread after the flow has finished. So when the
   exception arrives at the `attempt` node, on the thread that runs the node, no task started
   inside the boundary is still running.
2. A failure already stops work that has not started (`aborting_`).

What was global and had to become scoped is "which failure, and who stops". Both move into a
**fault domain** (`TaskflowExecSchedPass::FaultDomain`): the abort flag, the first fault, a
link to the enclosing domain. The run has a root domain; each `attempt` node opens one for
its callee. The domain a thread works in is part of its `ThreadState` and is inherited by
every task exactly where the call depth already is (`InheritedDepth`, now `InheritedTask`).

- `raiseFault` reports the first fault *of the domain* (and only that one: concurrent
  element functions can fault together) and marks the domain aborting.
- A task that is about to start an item, a call or an element asks whether its domain *or an
  enclosing one* is aborting, and does not start if so. A process-wide count of aborting
  domains keeps this at one relaxed load while no fault is in flight.
- The `attempt` node catches the `Diagnostic` after its flows were joined, drops the domain,
  and writes the error value.

Sibling tasks outside the boundary are in another domain and are not affected.

**Siblings inside the boundary** (`DESIGN-QUESTION(attempt-parallel-siblings)`). For
`attempt(() => xs->map<f>)` where one element faults: elements already started run to their
end (there is no preemption; they may still print and write `var`s); elements not yet started
do not start; the node returns the error value only after all started ones have ended, so
nothing of the boundary runs after the boundary is left. The same policy applies to a fault
that is not isolated: the program ends after the started elements have ended. The linear back
ends run elements in order and none after the faulting one, so *which* effects of the other
elements happen differs between a linear and a parallel run — as 2.7 already says for effects
in a parallel `map`.

Which of several concurrent faults becomes the error value is whichever is reported first; it
is not defined (`DESIGN-QUESTION(attempt-parallel-first-fault)`).

## 4. Clean-up

### 4.1 When a fault ends the program (not isolated)

Verified, not changed (see `06` 4.4.4 and `test/plans/feat/consistency/cleanup.plan.toml`):
the exception unwinds the scheduler, the pass driver records the failure, the command line
returns from `main`. C++ streams are flushed, `fs` operators have closed their files, static
destructors run.

External resources (`ForeignResourceObject`, section 23) are released by their finalizers,
which the collector runs when it finds the object dead, and which the memory manager runs for
every object still alive when the heap is torn down at process exit. What this work checked
and what it found is in section 8.

### 4.2 When a fault is isolated

Nothing is finalized at the boundary: objects created inside become unreachable and are
finalized by the next collection that finds them, like any garbage. An external resource
therefore outlives the boundary by an unspecified time (`DESIGN-QUESTION(attempt-finalizers)`:
the alternative, a collection at every isolated fault, would make an isolated fault cost a
full collection).

Output is not flushed at the boundary either; it stays buffered and in order.

## 5. Passes and other libraries

- **Inlining.** The callee of `attempt` is an operand (a function value), not a `FUNC` or
  `CALL` node; the inliner replaces call nodes only, so the callee's body is never copied
  into the enclosing graph and the boundary stays a graph boundary. Inlining *inside* the
  callee is unaffected (the boundary is the callee's graph, whatever was inlined into it).
  The cost: a function called only through `attempt` keeps its call. A pass that specialises
  higher-order operators on a constant function value must not rewrite `:mark/isolate` into
  a call; `camel::execute::isFaultIsolationUri` is the test to use.
- **fold / cse / dce.** `:mark/isolate` has no entry in the operator-traits registry, so it
  is treated as effectful: never folded (its result depends on whether the callee faults,
  which is exactly what folding must not decide at compile time, see 2.9 on `1 / 0`), never
  merged, and removed only where an effectful unused call already is.
- **`std::macro`.** The macro evaluator does not evaluate `:mark/...` operators ahead of
  time; a macro function whose body uses `attempt` is left as a call.
- **Autodiff, ONNX export, `std::cpp`.** Each refuses the node with a located diagnostic.
- **Safepoint.** The node is one: both entering the callee and leaving it (normally or
  through a fault) are graph boundaries, where every live value is in a frame.

## 6. Cost for programs that do not use it

No node kind, no frame field, no check on a call path was added. The changed shared code is
`reportRuntimeFault` (one thread-local read, on the fault path) and, in `std::tf`, the abort
test (one relaxed atomic load, as before, of a different variable) and one more word copied
when a task inherits its context. Measurements are in section 8.

## 7. Tests

`test/plans/feat/consistency/attempt.plan.toml`, cases under `test/cases/consistency/attempt/`.

## 8. Verification results

(Filled in at the end of the work.)

## 9. Questions for the language author

(Filled in at the end of the work; the labels used above are the entries.)
