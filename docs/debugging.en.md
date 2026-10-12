# Debugging Camel Programs

[中文简体](debugging.cn.md) | English

`camel-db` is Camel's debugger. This guide is about source-level debugging through the
[Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (DAP): breakpoints
on source lines, stepping, the call stack, variables. Any editor with a DAP client can use it;
the VS Code extension in [`editors/vscode`](../editors/vscode) is set up for it.

A Camel program is a graph, and the debugger does not pretend otherwise. What "the next line",
"the call stack" and "a variable" mean in Camel is explained in [section 3](#3-what-you-see-and-why);
read it once, it will save you from surprises. The design and the internals are in
[technical note 34](technical/34_debugger.md).

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
    "stepBy": "line"
}
```

| Attribute | Meaning |
|-----------|---------|
| `program` | The `.cml` file. Required. |
| `passes` | The pass sequence, as on the command line (`camel file.cml std::inline std::fvm`). Empty or absent: the graph as compiled, on `std::nvm`. |
| `args` | Values for the parameters of `main`, each `name=value` (the `--arg` option of `camel`). |
| `cwd` | Working directory of the program. |
| `env` | Environment variables added for the program. |
| `stopOnEntry` | Stop before the first node that has a source position. |
| `stepBy` | `line` (default) or `node`: what the step commands step by, see [3.2](#32-next-line-is-the-next-line-that-executes). |

Breakpoints are set in the gutter as usual. *Step Over*, *Step Into*, *Step Out*, *Pause*,
*Continue*, *Stop*, the *Call Stack*, *Variables* and *Watch* panes and hovering over a name
work through the standard protocol. The *Breakpoints* pane has two entries for faults:
**Faults that end the program** (on by default) and **Faults isolated by attempt**.

## 2. From the command line

`camel-db --dap` speaks DAP on standard input and output, so it is normally started by an
editor. To try a session without one, the repository has a small scripted client:

```bash
node test/tools/debugger/dap_session.mjs test/cases/tools/debugger/fixtures/recursion.cml \
     --passes std::fvm b:8 go stack vars c stack vars
```

`b:<lines>` sets breakpoints, `go` starts, `c` continues, `n` / `s` / `o` step over, into and
out, `ni` / `si` step by graph node, `stack`, `vars`, `nodes`, `e:<name>` inspect; the header of
the script lists every step. The session above, with a breakpoint in a tail-recursive function:

```text
-> setBreakpoints {"source":{"path":".../recursion.cml"},"breakpoints":[{"line":8}]}
   line 8: verified
-> configurationDone
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in loop at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #6 loop  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
   [Locals]
     n: int = 5
     acc: int = 0
-> continue {"threadId":1}
   (node %18 OPER :op/eq_l)
<- stopped (breakpoint) in loop (tail call, 2 frames replaced) at recursion.cml:8:17
-> stackTrace {"threadId":1}
   #8 loop (tail call, 2 frames replaced)  recursion.cml:8
   #2 main  recursion.cml:13
   #1 __root__  recursion.cml:11
   [Locals]
     n: int = 4
     acc: int = 5
```

Other editors: configure a DAP client to run `camel-db --dap` with a `launch` request carrying
the attributes of the table above.

`camel-db` also has a Web UI with a graph view and memory inspection; that is described in
[`tools/debugger/README.md`](../tools/debugger/README.md).

## 3. What you see, and why

### 3.1 The program stops before a graph node

Execution is the execution of graph nodes: an operator, a call, a branch. The debugger stops
*before a node runs* and shows the source position of that node, so the highlighted position is
a line **and a column**: in `let c = a + b` it is the `+`.

A line can hold several nodes, and a line can hold none:

- A line **without a node** cannot hold a breakpoint. The breakpoint is shown as unverified
  and its message says which case it is: there is no code on the line; or the code is *not
  executed because nothing depends on its result*; or a pass removed it. The second case is
  Camel's execution model at work. In

  ```camel
  func next(x: int): int {
      let unused = x + 100     // nothing uses it, the body is not a sync block: never runs
      return x + 1
  }
  ```

  the `+ 100` is not part of the graph that runs, and a `println(x)` in the same position
  would not print. Put the code in a sync block (`func next(x: int): int sync { ... }`) or use
  its result to make it run.
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

The debugger never reorders the program and never evaluates anything in order to show it.

### 3.3 The call stack is a chain of subgraphs

A function is a subgraph; a frame is one activation of a graph.

- **`__root__`** at the bottom is the entry graph, whose only work is to call `main`.
- **Tail calls replace the frame.** A function that ends in a call does not return to itself,
  and both back ends reuse its frame. The stack shows what exists: a tail-recursive loop is
  *one* frame however long it has been running, labelled with the number of frames it
  replaced: `loop (tail call, 4 frames replaced)`. No frames are invented.
- **The arms of a branch are subgraphs of their own.** `if c then f(x) else g(x)` compiles to
  a branch node and two small graphs. A stop inside an arm is shown as
  `pick [arm at line 9]`; when the branch is the last thing the function does, the arm has
  replaced the function's frame (`... (tail call, 1 frame replaced)`). An arm sees the names of
  its function. The arm that is not taken does not run: a breakpoint in it is valid and is
  never hit.
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

Scopes: **Locals** (parameters and the names declared in the function), **Captured** (names a
lambda captured), **Inlined** (names of functions inlined into this graph), **Nodes** (every
value node of the graph by number, kind and label: the graph view of the frame).

**Watch and hover** accept a name, optionally followed by member accesses: `p.x`, `t.0`,
`xs[2]`. Other expressions are refused: evaluating one would mean building and running a graph
in the stopped program.

### 3.5 Faults

A fault (division by zero, index out of range, recursion too deep) stops the program where it
is raised, as an exception: the editor shows the text of the diagnostic at its position, and
the stack and variables of that moment. Resuming lets the fault take its course: the program
ends with the diagnostic and exit code 1, exactly as without a debugger.

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
| optimisation passes before the back end (`std::inline`, `std::opt`, `std::macro`, ...) | supported; see below |

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
- With the pass `std::macro`, macro functions are evaluated while the pass runs, before the
  program starts; breakpoints in them are hit at that time.
- After `std::inline` a name bound to an inlined call is not shown under its name (see section 4).
- Not verified: Windows (the adapter's process handling is ported but untested), macOS, and the
  behaviour of the VS Code user interface itself.
