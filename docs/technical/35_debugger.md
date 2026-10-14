# The Debugger: Source View, Graph View and the Debug Adapter Protocol

This note is the design of source-level debugging in `camel-db` (`tools/debugger`) and of its
Debug Adapter Protocol (DAP) front end. It follows the ruling of
[design principles](../design-principles.cn.md) 2.9: *the debugger has a source-line view and a
graph-node view, and it steps by line and by node.* Positions and origins are those of
[10](10_source_mapping_and_debug_locations.md); names come from the builders' report channel of
[32](32_semantic_query_layer.md); faults are those of [33](33_fault_isolation.md). The guide
for users is [Debugging Camel Programs](../debugging.en.md).

Section 9 says what is implemented and verified, and what is not. Points that need a ruling of
the language author are marked `DESIGN-QUESTION(<tag>)` here and in the code, and collected in
section 8.

## 1. Why two views

A Camel program is a graph. Evaluation is driven by dependencies (design principles 2.1): two
statements that do not depend on each other have no order, a call whose result nothing uses
does not run, a function is a subgraph, a pass may have rewritten everything before it runs. A
debugger that only knows source lines then shows things it cannot explain: the highlighted line
jumps backwards, a line with a `println` is never reached, a breakpoint in a small function
stops inside its caller.

The graph view explains each of them, so the debugger always knows both:

```
source view                    graph view
  4  let a = f(x)                [f] --+
> 5  let b = g(x)   <- stopped   [g] <-- current node
  6  let c = a + b               [+] --+

step by line -> the next line the scheduler reaches (6, or 4 when f has not run yet)
step by node -> the next node the scheduler runs
```

## 2. Where the program is stopped

The program stops only *before a node runs* (or where a fault was raised, 6.3). A stop is:

- **the stack of frames** (section 5). A frame is one activation of a graph: the runtime graph
  (`GCGraph`), its `Frame` of slots, and the node about to run in it;
- for the top frame, **the current node**: its ref in the graph, kind and label;
- **the source location** of the current node: the primary span of its origin
  (`GCGraph::nodeOrigin` -> `SourceContext`), file, line, column, end. A node without an origin
  (a port, a gate, the entry call of `main`) has no location; the stop then shows the graph
  only;
- **the reason**: breakpoint (line or node), step, pause, entry, fault.

The relation between lines and nodes is many to many, and the debugger never hides that:

| Case | What happens |
|------|--------------|
| One line, several nodes (`println(f(x) + g(x))`) | The line is entered once (2.1); node steps walk the nodes |
| One line, nodes in several graphs (a generic function's instances; a function inlined at several places) | A line breakpoint binds to all of them |
| A node whose source is in another function (after `std::inline`) | The frame is the graph that runs; the location is the source it came from, and the frame says "inlined" (5.3) |
| A line with code but no node | The code is not executed: nothing depends on it, or a pass removed it. A breakpoint there is unverified and says which (3.2) |
| A node without a line | Reached by node steps only |

**What each view shows.** The source view: the location of the top frame, the call stack with
a location per frame, the variables of the selected frame by their source names. The graph
view: the graph of the selected frame as nodes and edges, with for each node its kind, label,
origin and line, whether it has run in this activation, its value when it has, and which node
is current; plus the graph as it was after each pass.

### 2.1 Entering a line

Both the line breakpoint and the line step are defined by one notion. Every frame remembers
the line of the last located node it ran. *A frame enters line L* when it is about to run a
located node of line L and the last located node it ran was not on L (or it has run none).
Nodes of a callee belong to the callee's frame and do not make the caller leave its line, so
`let a = f(x) + 1` is entered once even though `f` runs in between.

The arm of a branch is a graph of its own (5.4), but it is code of the function that
branches. Its frame therefore starts on the line its function is on: a line the branch node and
the first node of the arm share is entered once, and a line step that started in the function
goes on in an arm that replaced the function's frame by a tail call.

## 3. Breakpoints

### 3.1 Source line breakpoints

`file:line` is mapped to the set of origins whose primary span *starts* on that line, taken
over every node of every graph the scheduler will run. The program stops when a frame enters
the line (2.1) at such a node.

So a line is stopped at once per visit of a frame, not once per node. A frame whose nodes of
line 5 are interleaved with others by the scheduler stops again when it comes back to line 5;
that is what happens, and the graph view shows why. Each activation is a frame of its own: a
breakpoint in a recursive function, or in the element function of `map`, stops once per call.
`DESIGN-QUESTION(line-breakpoint-hits)`.

Breakpoints are resolved against the graph that will actually run. They are answered twice:
when they are set (against the graph the compiler built), and again when the pass sequence has
finished and the scheduler starts (against the graph it was given); a change is sent as a DAP
`breakpoint` event.

### 3.2 A line without nodes

A breakpoint on a line with no node to stop at is reported `verified: false` with the reason,
found by comparing three sets of origins on that line: those the graph builder created, those
in the graph before the passes, those in the graph that runs.

| Builder | Before passes | Running | Message |
|:--:|:--:|:--:|---|
| none | | | No code on this line. |
| some | none | | The code on this line is not executed: nothing depends on its result. Evaluation is driven by dependencies; to run it, use its result or put it in a sync block. |
| some | some | none | The code on this line is not in the graph that runs: a pass (...) removed or replaced it. |
| | | some | verified |

The second row is the sentence design principles 2.1 asks tools to be able to say.

### 3.3 Node breakpoints

A node breakpoint is an origin id (what the graph view offers when a node is clicked); the
program stops before every node with that origin, in whichever graph it now is. This is the
existing `gir-breakpoints` of the HTTP API, kept.

### 3.4 Other breakpoints

Allocation breakpoints and pipeline-stage breakpoints (`DebugBreakpoint::Hit`) exist only in a
Debug build of `libcamel`; they are unchanged and reachable over the HTTP API only.

## 4. Stepping

All steps resume the program and stop at the first node that satisfies a predicate. `S` is the
frame stopped in, `d` its depth, `L` its current line.

**Step by node** (DAP `next`/`stepIn` with `granularity: "instruction"`): the next node the
scheduler runs. Nodes the scheduler does nothing for (constants, ports, gates) are not stops.
`stepIn` stops in any frame; `next` does not stop in frames deeper than `d`.

**Step by line** (DAP `next`, granularities `statement` and `line`): the first located node N
in a frame F with depth(F) <= d such that F is not S, or line(N) is not L. In words: run until
this frame enters another line, or until it has returned and its caller runs its next located
node.

The line reached is the next one *in execution order*, not the next one in the text.
`DESIGN-QUESTION(line-step-order)`. Reasons:

- the next line of the text may never run (nothing depends on it), may already have run, or
  may run much later; stopping "there" would mean stopping somewhere else and pretending;
- inside a sync block the control edges make execution order equal to the order of the text,
  so there the step is the familiar one, with no special case in the debugger;
- outside a sync block the user sees the order the scheduler chose, which is the fact that
  explains the program's behaviour. The order is deterministic on `std::nvm` and `std::fvm`
  for a given graph (the topological order they compute), so a session is repeatable.

**Step in** (DAP `stepIn`): as step by line, in any frame: the first located node N in frame F
with F not S or line(N) not L. It enters a callee when the next node is a call.

**Step out** (DAP `stepOut`): the first located node in a frame of depth less than `d`.

### 4.1 Tail calls

A tail call replaces the frame (section 5.2): the callee takes the place of S at the same
depth. For stepping, the replacement is a new frame at depth `d`, so a line step at
`return loop(n - 1, acc)` stops at the first line of `loop`, and step out runs until the whole
chain of tail calls has returned to the real caller. A recursion written as tail calls is thus
stepped like the loop it is. `DESIGN-QUESTION(step-over-tail-call)`.

### 4.2 Inlined calls

After `std::inline` the callee's nodes are in the caller's graph and there is no frame. Nodes
keep their origins, so the debugger knows which *function of the source* a node was written in
(the innermost function whose span contains the node's span), though not at which call it was
inlined (10: there is no "inlined at" chain). Stepping uses the source function as a frame
would be used:

- a line step that starts in source function A does not stop at nodes of the same frame that
  were written in a function A does not contain (the inlined callee): it steps over it;
- step in stops there;
- step out, started in inlined code of B, stops at the first located node of the same frame
  not written in B, or in a shallower frame.

The stack shows one frame, named after the graph that runs, marked as running inlined code of
B. `DESIGN-QUESTION(inlined-frames)`.

## 5. The call stack

The debugger keeps its own stack, fed by the scheduler hooks (7.1): a frame is pushed when an
activation runs its first node, popped when it ends (also when a fault unwinds it), replaced
at a tail call. It is the same on both back ends, although they hold calls differently:

| | A call | What the debugger sees |
|---|---|---|
| `std::nvm` | every call is a nested activation of the interpreter loop on the native stack | activation enter/leave |
| `std::fvm` | a direct call pushes on the interpreter's own stack (`FUNC`/`RETN`), no native frame; a call of a function value and each element call of `map`, `filter`, `reduce`, `foreach` is a nested activation | the opcode just executed (`FUNC` pushed, `RETN` popped, `TAIL` replaced) and activation enter/leave |

The frame below the top shows the call node it is waiting in. The entry graph (`__root__`,
whose only work is the call of `main`) is shown last, without a location.

### 5.2 Tail calls

After a tail call the caller's frame is gone; both back ends reuse it. The stack is shown as
it is, and the frame that was entered by a tail call carries the count of frames it replaced:
`loop (tail call, 3 frames replaced)`. No frames are invented. `DESIGN-QUESTION(tail-call-stack)`.

### 5.3 Inlined code

See 4.2: `main [inlined code of g]`, located in `g`.

### 5.4 Arms and lambdas

`if c then a else b` (and `??`, `!!`, `?->`, `match`) is a branch node and one graph per arm;
the builder names these graphs itself (`__g_7__`), like the graphs of lambdas. A frame of such
a graph is shown as what it is in the source: `fact [arm at line 4]`,
`main [lambda at line 14]` (the nearest enclosing graph with a name of the source, and the first
line of the graph's nodes). Which graphs are arms is read from the branch nodes of the running
program (`GCGraph::branchArmsOf`).

An arm takes the values it uses from its function as unnamed `with` ports. The debugger names
such a port after what the caller passes there (a port of the caller, or the name bound to the
node), so an arm shows `n`, not `port`. When the branch is the last thing a function does, the
arm is entered by a tail call and replaces the function's frame; the frame says so
(`fact [arm at line 4] (tail call, 1 frame replaced)`). `DESIGN-QUESTION(arm-frames)`.

## 6. Variables, values, faults

### 6.1 Names

A frame has slots, not names. The graph builder reports every binding to the recorder of the
semantic query layer (`SemanticRecorder::valueBound`, added for this): the name, its
declaration, whether it is a `var`, and where its value is: the origin of the node the name is
bound to, or the index of the port for a parameter or a captured name. At a stop the debugger
finds, in the graph of the frame, the node with that origin (or the port), and reads its slot.
The node that creates the cell of a `var` carries the origin of the declaration for this (the
builder gave it none before).

Scopes of a frame:

- **Locals**: parameters (`with` parameters marked), `let` and `var` names of the function the
  graph was built from;
- **Captured**: names the function captured from an enclosing one (closure ports);
- **Inlined**: names of other functions whose nodes were inlined into this graph, grouped by
  function;
- **Nodes**: every value node of the graph by ref, kind and label: the graph view's values.

### 6.2 Values

A run-time value does not carry its type (design principles 2.7); the type is the static type
of the node or slot, and values are printed with it, by the program's own printing code.

| Value | Shown as |
|---|---|
| a name whose node has not run in this activation | `<not evaluated>`: evaluation is dependency-driven, a `let` written earlier may run later or never (an arm not taken). Its slot is not read |
| `var` | the current content of the cell, type `var T` |
| captured name | the value in the closure; a captured `var` is the shared cell |
| array, tuple, struct | summary and children (elements, fields), the first 100 |
| `T?` | `null` or the value; `T!`: the value or `error <kind>: <message>` |
| `Map`, `Set` | the printed form |
| tensor and other module types | the type with its static shape, and the printed form cut at a length |
| function value | its graph and number of captured values |

Printing is cut at a fixed length. Whether a node has run is tracked by the debugger per frame
(the slot the previous node wrote), so a slot that was never written is never dereferenced.

**Evaluate** (DAP `evaluate`): a name of the selected frame, optionally followed by member
accesses (`p.x`, `t.0`, `xs[2]`). No other expressions: evaluating one would mean building and
running a graph in the stopped program.

All reads happen on the thread of the stopped program, while it waits in the hook: nothing
runs concurrently with them, no collection can start, and no pointer to a Camel object is kept
after the answer has been formatted (the handle rule of 23/24). References handed to the
client (DAP `variablesReference`) are paths (frame, name, member, member...), resolved again
on each request and void after the program resumes.

### 6.3 Faults

A fault (division by zero, index out of range, ...) is raised as `RuntimeFault`; the scheduler
that catches it tells the debugger before it releases any frame. The debugger stops there with
reason `exception`: the stack, the node and the variables are those of the fault. The message
is the diagnostic's; DAP `exceptionInfo` gives its name and text. When the program is resumed
the fault continues as without a debugger: it ends the program with the located diagnostic, or
becomes an error value at the boundary of an `attempt`.

Two exception filters:

- `uncaught` (default on): faults that will end the program;
- `isolated` (default off): faults raised inside an `attempt`, which will become error values.

## 7. Architecture

```
 editor --DAP/stdio--> camel-db --dap  (adapter process)
                           |  spawns, owns stdout/stderr pipes
 browser --HTTP--> camel-db (gateway) --HTTP--+
                                              v
                                     camel-db --run-worker   (one per debuggee)
                                       HTTP thread: /api/... , /api/debug/...
                                       main thread: compile, passes, scheduler
                                                      |  ExecutionObserver
                                                      v
                                                  DebugCore
```

- **The debuggee always runs in a process of its own** (the existing rule of `camel-db`). A
  program that crashes or loops takes down or blocks the worker only; the adapter reports the
  exit, and `disconnect` kills the worker.
- **`DebugCore`** (`tools/debugger/src/core`) is the one place that controls a program:
  breakpoints, the stack, stepping, stops, variables, events. It knows neither HTTP nor DAP.
  It is the `ExecutionObserver` of the scheduler in the worker.
- **The HTTP API** of the worker exposes the core as `/api/debug/<command>` (JSON in, JSON out)
  and `/api/debug/events` (long poll). The existing endpoints the Vue front end uses
  (`gir-breakpoints`, `step`, `continue`, `step-paused`) are now implemented by the core.
- **The DAP adapter** (`camel-db --dap`) speaks DAP on stdin/stdout, spawns one worker, forwards
  the worker's standard output and error as `output` events, and translates requests to
  `/api/debug` commands and core events to DAP events. It holds no debugging logic.
- The control channel stays HTTP on 127.0.0.1 (the debugger's product principles forbid a
  second protocol between its processes); events are pulled by long poll.

### 7.1 Changes to the schedulers

One interface in `libcamel`, `camel::execute::ExecutionObserver`
(`camel/execute/observer.h`), installed process-wide by the debugger and null otherwise:

| Hook | `std::nvm` | `std::fvm` |
|---|---|---|
| activation enter / leave | a guard object in `NodeVMSchedPass::call` | a guard object in `FastVMSchedPass::call`, on the path taken while an observer is installed |
| before a node / an instruction | where `HitGirNode` was (replaced) | `inspectInstruction`, at the dispatch point of the inspected loop |
| tail call | one call in the tail-call path | none: the core sees the `TAIL` opcode |
| fault | first statement of `catch (RuntimeFault)` | `observeFault` at the two places the inspected loop catches a fault: around an operator call, and around the loop for faults the instructions raise themselves |

**`std::fvm` has two instances of its interpreter loop.** `gotovm.cpp` is the loop of a normal
run (`callBorrowed`). `inspectvm.cpp` compiles the same file a second time under the name
`callBorrowedInspected`, with the two hook points defined. `FastVMSchedPass::call` chooses per
activation: the inspected loop while an observer is installed, the normal one otherwise. So a
normal run executes the loop it executed before this work, instruction for instruction (the
disassembly of `callBorrowed` was compared with the one built from the trunk: no difference
outside addresses).

The first version put the hook where the predecessor of this work had put it: one call inside
the branch on the inspection-mode flag that every dispatch already tests. That branch is never
taken in a normal run, and it still cost 13-19% on call-bound programs: with a real call at
the dispatch point the compiler kept the loop's program counter on the stack instead of in a
register (the inline code that had been there calls nothing). A register-preserving calling
convention for the hook did not change that. Hence the second instance.

The switch interpreter (`casevm.cpp`, for builds without computed goto) has no hooks: it does
not compile at present (it includes a header that no longer exists), so no build uses it.

Cost without a debugger: `std::nvm`, one load of a pointer per node (as before: the node hook
it replaces was one atomic load) and one per call; `std::fvm`, one load of a pointer per
nested activation (a call of a function value, an element call of a higher-order operator;
direct calls and tail calls stay inside the loop) and nothing per instruction. Measurements
are in section 9.

`std::fvm` executes bytecode, not nodes. An instruction is mapped to its node by the origin
the compiler recorded for its pc and the slot it writes; instructions without a node (jumps)
are not stops.

### 7.2 Which graph, which back end

- **Pipeline.** The launch configuration gives the pass sequence, exactly as on the command
  line. The default is none: the graph as written, on `std::nvm`, the closest to the source.
  Debugging the graph *after* optimisation passes is supported and is a feature: breakpoints
  bind by origin, the graph view shows the rewritten graph and the graph after each pass.
- **`std::jit`** runs under the debugger as `std::fvm`: compiled code has no hooks. The
  replacement is made by the worker (`debuggablePasses` in `tools/debugger/src/run.cpp`) and
  told to the user: the core's `started` event carries the notice, the adapter prints it in the
  debug console.
- **`std::tf` / `std::prl`** (parallel) run as `std::fvm` as well, with a notice: several nodes
  run at once, there is no single current node, and the scheduler has no hooks. The sequential
  order of `std::fvm` is one of the orders the parallel scheduler may choose (design principles
  2.7: no order is promised between the elements of `map` and between independent nodes), so
  the program is the same program; what cannot be observed this way is a defect that depends
  on the interleaving. A stop-the-world mode showing the node of each worker is possible
  later; it needs hooks in the task bodies. `DESIGN-QUESTION(parallel-debugging)`.
- **Passes that rewrite the graph** (`std::inline`, `std::opt`, `std::macro`, ...) run as asked.
  Positions survive because nodes keep their origins through rewrites (10); breakpoints are
  answered again against the graph the scheduler is given (3.1). What is lost: the name of a
  `let` bound to a call that was inlined or a value that was folded (its node is gone: the
  name shows *not in the graph*, the Nodes scope shows what exists); frames of inlined
  functions (4.2). What `std::macro` evaluates itself (a macro call with
  constant arguments) runs before the scheduler starts and is not stopped in; the call is a
  constant of the graph afterwards.
- Order of evaluation under the debugger is the back end's own; the debugger never reorders.

### 7.3 DAP surface

`camel-db --dap` (`tools/debugger/src/dap/adapter.cpp`) serves one session on standard input
and output and writes nothing else to standard output.

**Order of a session.** `initialize` -> `launch` (the adapter starts the worker, has it
compile the program, answers, then sends `initialized`) -> `setBreakpoints` /
`setExceptionBreakpoints` (answered against the compiled program) -> `configurationDone` (the
program starts). Breakpoints may be changed at any time later.

**The end of a session has one path**: the worker process exits (the adapter kills it when the
core reports the run finished, on `disconnect`, after `terminate`, or it dies by itself), the
rest of its output is forwarded, then `exited` and `terminated` are sent. So `output` always
precedes `exited`. The exit code is what `main` returned for a run that completed, 1 for a
run that failed (as `camel` exits), 143 for a run ended by `terminate`, and `128 + signal`
for a worker that crashed.

| Request | | Notes |
|---|---|---|
| `initialize` | yes | capabilities: `supportsConfigurationDoneRequest`, `supportsTerminateRequest`, `supportTerminateDebuggee`, `supportsSteppingGranularity`, `supportsExceptionInfoRequest`, `supportsEvaluateForHovers`, two `exceptionBreakpointFilters`; honours `linesStartAt1` / `columnsStartAt1` |
| `launch` | yes | `program`, `passes` (array or string), `args` (`name=value` array, or an object), `cwd`, `env`, `stopOnEntry`, `stepBy` (`line` / `node`) |
| `attach` | no | error response: `camel-db` starts the program itself |
| `setBreakpoints` | yes | source lines; each answered `verified` or with the reason (3.2); `breakpoint` events when the answer changes after the passes |
| `setExceptionBreakpoints` | yes | filters `uncaught` (default), `isolated` |
| `configurationDone` | yes | starts the program, once |
| `threads` | yes | one thread, id 1 |
| `stackTrace` | yes | `startFrame` / `levels`; a frame without a source position is `subtle` |
| `scopes`, `variables` | yes | Locals, Captured, Inlined, Nodes; references are void after a resume |
| `evaluate` | names | a name with member accesses (6.2); anything else is an error response |
| `continue`, `next`, `stepIn`, `stepOut` | yes | `granularity: "instruction"` (or `stepBy: "node"`) steps by node |
| `pause` | yes | takes effect at the next node |
| `exceptionInfo` | yes | name, message and located text of the diagnostic |
| `terminate` | yes | ends the run at its next node; the process is killed after 2 s without one |
| `disconnect` | yes | kills the worker and ends the adapter |
| `camel/graph` | yes | custom: the graph of a frame (nodes with state and value, edges, DOT) |
| `restart`, `setFunctionBreakpoints`, `setVariable`, `setExpression`, `stepBack`, `goto`, `completions`, `disassemble`, `readMemory`, `loadedSources`, `modules`, data and instruction breakpoints, conditions, hit counts, logpoints | no | error response `Unsupported request` (the capabilities do not announce them) |

| Event | | Notes |
|---|---|---|
| `initialized` | yes | after the launch response |
| `stopped` | yes | reasons `breakpoint` (with `hitBreakpointIds`), `step`, `pause`, `entry`, `exception` (with the diagnostic's text) |
| `continued` | yes | |
| `output` | yes | `stdout` and `stderr` of the program; `console` for notices of the debugger |
| `breakpoint` | yes | reason `changed` |
| `exited`, `terminated` | yes | in this order, after the last `output` |
| `camel/stopped` | yes | custom, before each `stopped`: the frame with its current node |
| `thread`, `module`, `loadedSource`, `process`, `capabilities`, `progress*`, `invalidated`, `memory` | no | |

A request that cannot be served gets an error response with the reason in `message` and in
`body.error`: unknown commands, requests before `launch`, queries while the program runs,
references that do not exist. A message that is not a request gets an error response too. The
adapter does not end on any input; it ends on `disconnect` or when its input is closed.

Output and stops travel on different channels (the worker's pipes, the core's event poll):
output written just before a stop can reach the client after the `stopped` event.

**VS Code** (`editors/vscode`): the extension contributes the debugger type `camel` with the
launch attributes above, breakpoints for the language, an initial configuration and two
snippets, and registers a `DebugAdapterDescriptorFactory` that returns
`DebugAdapterExecutable(<camel-db>, ['--dap'])`; `camel-db` is found by the setting
`camel.debugger.path`, `$CAMEL_HOME/bin`, then `PATH`. A configuration provider debugs the
current file on F5 when there is no `launch.json`. `scripts/check-manifest.mjs` checks the
contribution against the adapter's source (every launch attribute is one the adapter reads).

## 8. Design questions

Each is implemented as recommended and marked in the code.

1. `line-breakpoint-hits`: stop once when a frame enters the line (recommended), or once per
   node of the line.
2. `line-step-order`: the next line in execution order (recommended), or in the text.
3. `step-over-tail-call`: a line step over a tail call stops in the callee (recommended), or
   runs until the chain returns to the caller.
4. `tail-call-stack`: show the stack as it is, marking replaced frames (recommended), or keep
   a shadow record of replaced callers for display.
5. `inlined-frames`: one frame marked as inlined code, stepping by source function
   (recommended), or have `std::inline` record call sites so that virtual frames can be shown.
6. `parallel-debugging`: run on the sequential `std::fvm` with a notice (implemented), refuse
   to debug, or stop-the-world with one current node per worker.
7. `arm-frames`: show the arm of a branch as the frame it is, named after its function
   (implemented), or fold arms into the frame of their function so that the stack looks like
   the source. Minimal example: `func fact(n: int): int { return if n <= 1 then 1 else n * fact(n - 1) }`
   stopped in the third call shows `fact`, `fact [arm at line 1] (tail call, 1 frame replaced)`
   twice, `main`; folded it would show `fact` three times.

## 9. Status

Verified on Linux x86-64 (clang, Release build, computed goto). Not verified: Windows (the
adapter and `process.cpp` build from a port of the gateway's `CreateProcess` code that has not
been run since), macOS, and the VS Code user interface (the tests speak DAP to the adapter).

| Area | State |
|---|---|
| Line breakpoints, with the reason when unverified; re-answered after the passes | implemented, tested on `std::fvm` and `std::nvm` |
| Node breakpoints (HTTP API) | implemented, tested through the gateway |
| Steps by line and by node: over, in, out | implemented, tested on both back ends |
| Stack: calls, recursion, tail calls, arms, lambdas, generic instances | implemented; arms and tail calls tested on both back ends |
| Variables: locals, captured, `var` cells, structured values, not evaluated, not in the graph | implemented, tested |
| Evaluate: names with member accesses | implemented, tested |
| Faults: stop with the diagnostic, isolated faults by filter | implemented, tested on both back ends |
| DAP adapter (7.3) | implemented, tested |
| `std::jit`, `std::prl` | run as `std::fvm` with a notice; tested |
| After `std::inline` / `std::opt` | positions kept, breakpoints re-answered, inlined code labelled; tested for no crash and for the surviving positions |
| Graph view over DAP (`camel/graph`) | implemented, one check; no editor front end uses it yet |
| Web UI: line breakpoints, source view | not done: the Vue front end still offers node breakpoints and node steps only |
| Conditional breakpoints, hit counts, logpoints, set value, restart, attach | not done |
| Stop at an error value that reaches the exit of `main` unhandled | not done: reported as without a debugger (stderr, exit code 1) |
| A frame per inlined call (4.2), the names of `let`s bound to inlined calls | not done: needs the inliner to record call sites |

**Tests** (`test/plans/feat/tools/debugger.plan.toml`, in the default set): `http_gateway` (the
Web UI's API), `dap_session`, `dap_stepping`, `dap_faults`, `dap_protocol`. The DAP tests start
`camel-db --dap` and speak the protocol on its pipes (`test/tools/debugger/client.mjs`); every
wait is on an event or a response with a 60 s limit, none on time, and each process a test
starts is ended by `disconnect` and then by its pid. `test/tools/debugger/dap_session.mjs` runs
a scripted session by hand.

**Cost without a debugger.** Three programs (`fib(32)` by double recursion; a tail recursion of
8,000,000 steps; 600,000 steps that each call `map` with a lambda over four elements), the
`camel` of the trunk against the `camel` of this branch, CPU time of the whole process, 41
rounds with the binaries alternated, lower quartile; `base2` is a copy of the trunk binary run
as a third contestant, to show the noise of the method (the machine was shared: load average
14-19 on 18 cores).

| Program | Back end | trunk | branch | branch / trunk | copy of trunk / trunk |
|---|---|---:|---:|---:|---:|
| fib | `std::fvm` | 353.5 ms | 324.3 ms | 0.92 | 0.98 |
| fib | `std::nvm` | 1640.4 ms | 1521.7 ms | 0.93 | 1.00 |
| tail | `std::fvm` | 567.2 ms | 580.6 ms | 1.02 | 0.96 |
| tail | `std::nvm` | 2029.9 ms | 2028.5 ms | 1.00 | 1.00 |
| hof | `std::fvm` | 521.0 ms | 527.2 ms | 1.01 | 1.02 |
| hof | `std::nvm` | 827.1 ms | 873.0 ms | 1.06 | 1.01 |

The differences are within what two copies of the same binary show (up to 5% here, more on
single rounds), with `hof` on `std::nvm` at the edge of it: that program makes a nested
activation per element, each of which now loads the observer pointer twice (the guard and
the node hook that was there before). On `std::fvm` the loop of a normal run is the trunk's
machine code (7.1); the version with a hook in the loop measured 1.13-1.19 on `fib` and
`tail` by the same method and was replaced.
