# Debugging Camel Programs

[中文简体](debugging.cn.md) | English

`camel-db` is Camel's debugger. This guide is about source-level debugging: breakpoints on
source lines and on functions, stepping, the call stack, variables. In an editor it goes through
the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (DAP): any
editor with a DAP client can use it, and the VS Code extension in
[`editors/vscode`](../editors/vscode) is set up for it. In a terminal, the REPL of `camel-db`
has the same commands ([section 2](#2-from-the-command-line)).

A Camel program is a graph, and the debugger does not pretend otherwise. What "the next line",
"the call stack" and "a variable" mean in Camel is explained in [section 3](#3-what-you-see-and-why);
read it once, it will save you from surprises. The design and the internals are in
[technical note 35](technical/35_debugger.md).

Verified on Linux x86-64. On Windows the adapter and its process handling are built but have not
been run; in VS Code nothing has been tried by hand (the tests speak the protocol to the adapter
directly), see [section 5](#5-limits).

---

## 1. In VS Code

Build Camel (`npm run build`) and install the extension as described in
[Editor Support](editor-support.en.md). The extension starts `camel-db --dap`; it looks for
`camel-db` in the setting `camel.debugger.path`, then in `$CAMEL_HOME/bin`, then on `PATH`.

- **Without a `launch.json`**: open a `.cml` file and press F5. The file is debugged on the
  bytecode VM (`std::fvm`).
- **With a `launch.json`**: *Run and Debug → create a launch.json file → Camel*, or add a
  configuration with the snippets *Camel: Debug current file* and *Camel: Debug with a pass
  sequence*:

```json
{
    "type": "camel",
    "request": "launch",
    "name": "Debug Camel file",
    "program": "${file}",
    "passes": ["std::fvm"],
    "args": ["n=10"],
    "stopOnEntry": false,
    "stepBy": "line",
    "frames": "functions"
}
```

| Attribute | Meaning |
|-----------|---------|
| `program` | The `.cml` file. Required. |
| `passes` | The pass sequence, as on the command line (`camel file.cml std::opt::inline std::fvm`). Empty or absent: the graph as compiled, on `std::nvm`. |
| `args` | Values for the parameters of `main`, each `name=value` (the `--arg` option of `camel`). |
| `cwd` | Working directory of the program. |
| `env` | Environment variables added for the program. |
| `stopOnEntry` | Stop before the first node that has a source position. |
| `stepBy` | `line` (default) or `node`: what the step commands step by, see [3.2](#32-next-line-is-the-next-line-that-executes). |
| `frames` | `functions` (default) or `subgraphs`: whether the arms of branches are part of the frame of their function, or frames of their own, see [3.3](#33-the-call-stack-is-a-chain-of-function-calls). |

Breakpoints are set in the gutter as usual; a *function breakpoint* (the **+** of the
*Breakpoints* pane) takes the full name of a function, `module::path::name`, or the name without
its module when only one module has it ([3.3](#33-the-call-stack-is-a-chain-of-function-calls)).
*Step Over*, *Step Into*, *Step Out*, *Pause*, *Continue*, *Stop*, the *Call Stack*, *Variables*
and *Watch* panes and hovering over a name work through the standard protocol. The *Breakpoints*
pane has two entries for faults: **Faults that end the program** (on by default) and **Faults
isolated by attempt**. The command **Camel: Toggle Subgraph Frames** (also a button in the title
of the *Call Stack* view during a Camel session) switches `frames` while the program runs or is
stopped.

## 2. From the command line

### 2.1 The REPL

`camel-db <file.cml>` loads the file and gives a prompt. The commands of a debug session:

| Command | |
|---------|---|
| `break <file>:<line>`, `break <line>`, `break <function>` (`b`) | a breakpoint on a line (of the loaded file when only a line is given) or on a function by its full name |
| `delete` | delete all breakpoints |
| `run [pass ...]` (`r`) | run the loaded file, with a pass sequence (`run std::fvm`), and wait for a stop |
| `continue` (`c`) | continue and wait for the next stop |
| `next` (`n`), `step`, `finish` | step over, into, out, by source line |
| `nexti` (`ni`), `stepi` (`si`) | step over, into, by graph node |
| `pause` | stop the running program at its next node |
| `bt` (`backtrace`, `where`) | the call stack |
| `locals [n]` | the variables of frame `n` of `bt` (default: the top frame) |
| `print <name>` (`p`) | the value of a name, with member accesses (`p.x`, `t.0`, `xs[2]`) |
| `frames [functions\|subgraphs]` | show or switch how arms of branches are shown ([3.3](#33-the-call-stack-is-a-chain-of-function-calls)) |

A command that lets the program run waits until it stops or ends, and prints on the way what the
program printed. `help` lists every command (the others manage gateway tasks);
Ctrl+C stops waiting, not the program. A session with
[`folded.cml`](../test/cases/tools/debugger/fixtures/folded.cml) (each command is echoed as the
core command it sends, `(camel-db) debug ...`; those lines are left out here):

```text
(camel-db) file folded.cml
Loaded: folded.cml (task 8766).
(camel-db) break 31
Breakpoint 1 at folded.cml:31
(camel-db) break 18
Breakpoint 2 at folded.cml:18 (not verified: Line 18 is not executed: nothing uses the value of `unused`. Evaluation is driven by dependencies; to run it, use its result or put it in a sync block.)
(camel-db) run std::fvm
Running folded.cml
Stopped (breakpoint) in main  folded.cml:31:17
(camel-db) step
Stopped (step) in pick  folded.cml:17:18
(camel-db) next
Stopped (step) in pick  folded.cml:19:18
  line 18 was not executed: nothing uses the value of `unused`
(camel-db) next
Stopped (step) in pick  folded.cml:20:9
(camel-db) next
Stopped (step) in pick  folded.cml:21:13
(camel-db) bt
#0  pick  folded.cml:21:13
#1  main  folded.cml:31:17
#2  __root__
(camel-db) locals
[Locals]
  n: int = 3
  base: int = 4
  unused: int = <not in the graph: nothing uses its value>
  r: int = <not evaluated>
  big: int = 12
  ok: bool = <not evaluated>
(camel-db) frames subgraphs
Frames: subgraphs (every subgraph that runs is a frame).
(camel-db) bt
#0  pick [arm at line 20]  folded.cml:21:13
#1  pick  folded.cml:20:9
#2  main  folded.cml:31:17
#3  __root__
(camel-db) continue
14
Run completed.
Breakpoint at folded.cml:18 was never hit: line 18 was not executed: nothing uses the value of `unused`.
Program finished: completed (exit code 0).
```

### 2.2 A scripted DAP session

`camel-db --dap` speaks DAP on standard input and output, so it is normally started by an
editor. To try a session without one, the repository has a small scripted client:

```bash
node test/tools/debugger/dap_session.mjs test/cases/tools/debugger/fixtures/recursion.cml \
     --passes std::fvm b:8 go stack vars c stack vars
```

`b:<lines>` sets breakpoints, `f:<names>` function breakpoints, `go` starts, `c` continues,
`n` / `s` / `o` step over, into and out, `ni` / `si` step by graph node, `stack`, `vars`,
`nodes`, `e:<name>` inspect, `frames:subgraphs` / `frames:functions` switch the frames
(`--frames` sets them at launch); the header of the script lists every step. The session above,
with a breakpoint in a tail-recursive function:

```text
-> setBreakpoints {"source":{"path":".../recursion.cml"},"breakpoints":[{"line":8}]}
   line 8: verified
-> configurationDone
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in spin at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #11 spin  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
-> scopes {"frameId":11}
   [Locals]
     n: int = 5
     acc: int = 0
-> continue {"threadId":1}
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in spin (tail call, 1 frame replaced) at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #13 spin (tail call, 1 frame replaced)  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
-> scopes {"frameId":13}
   [Locals]
     n: int = 4
     acc: int = 5
```

Other editors: configure a DAP client to run `camel-db --dap` with a `launch` request carrying
the attributes of the table above.

OpenCML-Ext provides the GIR and memory interface through this native adapter. Configure
`stopOnPassEntry: true` and `passBreakpoints: [2]` with the pass sequence to stop at initial
or returned-GIR boundaries. The compact GIR strip separates actual execution from snapshot
history; selecting history never resumes execution. Use **Camel: Inspect Debug Memory** for
paused-region usage, paged bytes/objects and explicit refresh. Allocation breakpoints require
Debug and pause after allocator locks are released. See
[`tools/debugger/README.md`](../tools/debugger/README.md).

## 3. What you see, and why

### 3.1 The program stops before a graph node

Execution is the execution of graph nodes: an operator, a call, a branch. The debugger stops
*before a node runs* and shows the source position of that node, so the highlighted position is
a line **and a column**: in `let c = a + b` it is the `+`.

A line can hold several nodes, and a line can hold none:

- A line **without a node** cannot hold a breakpoint. The breakpoint is shown as unverified
  and its message says which case it is: there is no code on the line; or the code is not
  executed because nothing uses its result (*Line 2 is not executed: nothing uses the value of
  `unused`.*); or a pass removed it. The second case is Camel's execution model at work. In

  ```camel
  func next(x: int): int {
      let unused = x + 100     // nothing uses it, the body is not a sync block: never runs
      return x + 1
  }
  ```

  the `+ 100` is not part of the graph that runs, and a `println(x)` in the same position
  would not print (*nothing uses its result*). Put the code in a sync block
  (`func next(x: int): int sync { ... }`) or use its result to make it run. When the program
  ends, the debugger says again why such a breakpoint was never hit (in the debug console, or in
  the REPL).
- A line of a function **nothing calls** holds a breakpoint, which says *Nothing in the program
  calls `helper`.*, is never hit, and is explained at the end the same way. Of a breakpoint that
  was not hit for another reason (an arm not taken, a call not made) nothing is said: the
  debugger only says what it knows from the graph.
- A **line breakpoint** stops when a function activation *enters* the line: once per visit,
  not once per node of the line.

### 3.2 "Next line" is the next line that executes

Evaluation is driven by dependencies. Inside a sync block statements run in the order they
are written; outside one, the scheduler picks an order that respects the dependencies, and
code nothing depends on does not run at all.

- **Step Over** (`next`) runs until this activation is about to execute a node on *another
  line*, or has returned to its caller. The line reached is the next one **in execution
  order**. In a sync block that is the next line of the text. Outside a sync block it may be a
  later line (lines that do not execute are skipped) or, in principle, an earlier one; the
  order is the one the back end uses for that graph, the same on every run.
- **Step Into** (`stepIn`) does the same but also stops inside a function called on the way.
- **Step Out** (`stepOut`) runs until the caller executes its next node.
- **Stepping by node.** A line step treats a line as one stop. To walk the nodes of a line one
  by one (`println(f(x) + g(x))` is three calls and an addition), launch with
  `"stepBy": "node"`: the same three commands then stop at the next *node*, and the column
  shows which one. DAP clients can also ask per request with `granularity: "instruction"`.
  The `camel/stopped` event that accompanies every stop carries the node
  (`%20 OPER :op/add_l`).
- **Tail recursion.** F10 steps over the recursive call until the tail-call chain returns to
  its real caller; F11 enters the next activation at its first executable location. Breakpoints,
  faults and manual pauses still interrupt F10. The extension's GIR stop context shows the
  new arguments and their previous values without inventing extra stack frames.
- **Lines that did not execute.** When a line step goes past a line that did not execute
  because nothing uses its result, the debugger says so, once: in the debug console (and in
  the REPL, under the stop): *line 18 was not executed: nothing uses the value of `unused`*.
  It does so for the lines between where the step started and where it stopped, the rest of a
  function the step left and the start of one it entered; not for a line that simply has not run
  yet, nor for an arm of a branch that was not taken.

The debugger never reorders the program and never evaluates anything in order to show it.

### 3.3 The call stack is a chain of function calls

A function is a subgraph; a frame is one activation of a function.

- **`__root__`** at the bottom is the entry graph, whose only work is to call `main`.
- **Tail calls replace the frame.** A function that ends in a call does not return to itself,
  and both back ends reuse its frame. The stack shows what exists: a tail-recursive loop is
  *one* frame however long it has been running, labelled with the number of frames it
  replaced: `spin (tail call, 2 frames replaced)`. No frames are invented.
- **The arms of a branch are part of their function's frame.** `if c then f(x) else g(x)`
  (and `match`, `&&`, `||`, `??`, `!!`, `?->`) compiles to a branch node and a small graph per
  arm, but an arm is code of the function it is written in, and the stack shows it so: a stop in
  an arm is in the frame of `pick`, a line step goes from the function into the arm and back as
  through any other lines, the names declared in the arm are among the function's while the arm
  runs, and `fact` calling itself in its `else` arm is `fact` above `fact`. The arm that is not
  taken does not run: a breakpoint in it is valid and is never hit.
- **The real subgraphs, on request.** With `"frames": "subgraphs"` (or *Camel: Toggle Subgraph
  Frames*, or `frames subgraphs` in the REPL) each arm is a frame of its own,
  `pick [arm at line 9]`, above the frame of its function, or in its place when the branch is
  the last thing the function does (`... (tail call, 1 frame replaced)`); a line step then steps
  over an arm as over a call, and the tail calls count the frames of arms too
  (`spin (tail call, 4 frames replaced)` is `spin (tail call, 2 frames replaced)` with frames of
  functions). A loop will be shown the same way once Camel has loops.
- **Full names.** Functions are shown by their full names, `module::path::name`, as everywhere
  in Camel: `names_lib::Config::clamp` for a function of the namespace `Config` of the module
  `names_lib`, `util::half` for one of a namespace of the program itself (whose module is left
  out). A function breakpoint takes the same name, or the name without its module when only one
  module has it (`Config::clamp`); otherwise the breakpoint says the name is ambiguous and lists
  the full names.
- **Lambdas** are shown as `main [lambda at line 14]`. Each element call of `map`, `filter`,
  `reduce` and `foreach` is a frame of its own.
- **Generic functions** have one graph per set of argument types (`twice<i64>`,
  `twice<string>`); a breakpoint in the source binds to all of them.

### 3.4 Variables are the values of nodes that have run

A `let` names the value of a node. At a stop:

- a name whose node **has run** in this activation shows its value;
- a name whose node **has not run yet** shows `<not evaluated>`. This is normal: a `let`
  written earlier may run later, or never (if nothing needs it). The debugger does not
  evaluate it for you, because evaluating would run code (and its effects) that the program
  has not run;
- a name whose node is **not in the graph** (nothing depends on it, or a pass removed it)
  says so;
- a **`var`** is a shared cell: it shows the current content, with type `var T`. A captured
  `var` is the same cell as in the function it was captured from;
- **`T?`** shows the value or `null`; **`T!`** the value or `error <kind>: <message>`, which
  expands into kind, message and location;
- arrays, tuples and structs expand into elements and fields (the first 100).

Scopes: **Locals** (parameters and the names declared in the function, and in the arms of it
that are running), **Captured** (names a lambda captured, and the values of a module's top level
a function uses, by their full names: `names_lib::Config::limit`), **Inlined** (names of
functions inlined into this graph), **Nodes** (every value node of the graph by number, kind and
label: the graph view of the frame).

**Watch and hover** accept a name, optionally followed by member accesses: `p.x`, `t.0`,
`xs[2]`; a value of a module is found by its full name or by the end of it written in the source
(`limit`, `Config::limit`). Other expressions are refused: evaluating one would mean building
and running a graph in the stopped program.

### 3.5 Faults

A fault (division by zero, index out of range, recursion too deep) stops the program where it
is raised, as an exception: the editor shows the text of the diagnostic at its position, and
the stack and variables of that moment. Resuming lets the fault take its course: the program
ends with the diagnostic and exit code 1, exactly as without a debugger.

An error *value* (`throw`, a `T!` that failed) is not a fault: it is an ordinary value, shown
in the variables as `error <kind>: <message>`, and does not stop the program.

A fault raised inside `attempt(...)` is isolated: it becomes an error value at the boundary
and the program goes on. Such faults do not stop the program unless the filter *Faults
isolated by attempt* is switched on.

## 4. Back ends and passes

| Pipeline | Under the debugger |
|----------|--------------------|
| none | the graph as compiled, on `std::nvm` (the node VM): closest to the source |
| `std::nvm`, `std::fvm` | supported |
| `std::jit` | runs as `std::fvm`: compiled code cannot be stopped. The debug console says so |
| `std::prl` / `std::tf` (parallel) | runs as `std::fvm`: several nodes run at once, so there is no single current node. The debug console says so. The order under the debugger is the sequential one |
| optimisation passes before the back end (`std::opt::inline`, `std::opt`, `std::macro`, ...) | supported; see below |

Passes rewrite the graph, and the debugger shows the graph that runs. Nodes keep their source
positions through passes, so stops and breakpoints stay on the right lines where the nodes
survive. What changes:

- a breakpoint whose line no longer has a node is verified when it is set (against the graph
  as compiled) and reported as unverified, with the reason, once the passes have run;
- after inlining there is no frame for the inlined function. The frame is the caller's, marked
  `main [inlined code of double]` while it runs the inlined nodes; a line step started in the
  caller steps over them;
- a name bound to a call that was inlined, or to a value that was folded away, shows *not in
  the graph*; the **Nodes** scope still shows every value there is.

## 5. Limits

- One thread, one program per session; no attach, no restart request (the editor restarts by
  launching again), no conditional breakpoints, hit counts or logpoints, no data breakpoints,
  no changing of values, no reverse stepping.
- Expressions other than names with member accesses cannot be evaluated.
- The program's output and the stop events travel on different channels: output printed just
  before a stop may appear in the console a moment after the editor shows the stop. At the end
  of a session the output is always complete before the program is reported as ended.
- Standard input is not connected to the program.
- A stop inside an operator implemented in C++ is not possible; `pause` and `terminate` take
  effect at the next node (a program that reaches none for two seconds is ended with its
  process on `terminate`).
- With the pass `std::macro`, a call of a macro function with constant arguments is evaluated
  by the pass, before the program starts, and replaced by its result: the debugger does not
  stop in that evaluation, and a breakpoint on the line of the replaced call becomes
  unverified (*this line only defines constants*).
- An error value that reaches the exit of `main` unhandled (`main(): T!`) is not a stop: no
  frame is left by then. It is reported as without a debugger, `UnhandledError` on standard
  error and exit code 1; break on the `throw` to see where it is made.
- After `std::opt::inline` a name bound to an inlined call is not shown under its name (see section 4).
- Verified: Windows Release and Linux Debug protocol tests, plus the Windows VS Code UI.
  macOS remains unverified. Windows Debug currently exceeds the PE DLL export limit, a build
  limitation rather than an enabled allocation-breakpoint capability.
