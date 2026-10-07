# Camel CLI User Guide

[中文简体](cli.cn.md) | English

    camel [options] [<target file>]
    camel <command> [command-options] <target file>

For the default run command, the input file may be given as a positional argument or via `--input <file>`. At least one is required.

## Global Options (logging)

Logging is configured via a **global severity threshold** and optional **scope filters**. Output is written to **stderr** when logging is active (threshold not `off`). If nothing is set, the default threshold is **`fatal`** (only fatal-level lines).

### Threshold

- `-l`, `--log-level <level>`: `fatal` | `warn` | `info` | `debug` | `trace` | `off`  
  Messages at or above the chosen level are eligible to print (e.g. `warn` allows `warn` and `fatal`).
- Shortcuts (same as setting `--log-level`):
  - `-v`, `--verbose` → `warn`
  - `-vv` → `info`
  - `-vvv` → `debug`
  - `-vvvv` → `trace`

If both `--log-level` and `-v*` appear, the **last** one on the command line wins.

### Scope filter (after threshold)

- `--log-preset <none|wall|extra>`: Restrict logs to documented scope prefix sets (`none` = no preset filter).
- `--log-include <scopes>`: Comma-separated scope prefixes; when set, **overrides** the preset list for filtering. A log line’s scope must equal a prefix or start with `prefix.` .

**Note:** In **release** builds of `libcamel`, `debug` and `trace` **macros** are stripped at compile time, so `-vvv` / `-vvvv` / `--log-level debug|trace` will not show those lines even though the CLI accepts them. Use a **debug** build to see them.

---

## Main Execution (Default Behavior)

Executes the specified `.cml` file or directory as the entry point. **Input file** must be provided either as a positional argument or via `--input <file>` (at least one required).

- `--input <file>`: Input file path (alternative to positional; use with `--passes` to avoid ambiguity)  
- `--passes <list>`: Comma-separated pass list (e.g. `--passes std::gir,other`). If omitted, first positional is file, rest are passes.  
- `--timeit <pass>/<N>`: Measure one pass `N` times from a fresh graph; text mode prints samples plus a compact summary table, while `--output-format json` emits structured benchmark data  
- `--dump-after-each-pass <dir>`: Write the graph before the first pass (`00_input`) and after each pass that returns a graph (`NN_<pass>`, `::` written as `.`) into `<dir>`: a readable DOT (`.rgir.dot`, the `std::rgir` format with `origin`/`span`/`srcfile` per node) and the `std::stats` counters (`.stats.json`). Scheduling and translation passes consume the graph and leave no snapshot. Compare two snapshots with `python tools/graph-diff/graph_diff.py BEFORE AFTER`  
- `--arg <name>=<value>`: Value of a parameter of `main` (repeatable; see "Programs whose `main` takes parameters")  
- `-o`, `--output <file>`: Where the value `main` returns is written (default: standard output; same section)  
- `-P`, `--profile`: Record time spent in each stage and output a performance report  
- `-S`, `--scheduler <type>`: Specify the scheduler type  
- `-t`, `--threads <num>`: Maximum number of threads  
- `-n`, `--no-cache`: Disable cache modules  
- `-I`, `--include <dir>`: Add module search path (can be used multiple times)  
- `-L`, `--stdlib <path>`: Specify standard library path (default: `./stdlib` under current directory)  
- `-E`, `--error-format`, `--output-format <text|json>`: Output format (default: `text`); `--timeit` reuses this, and the test harness expects `json`

### Programs whose `main` takes parameters or returns a value (`--arg`, `-o`)

`main` is a function like any other and may take parameters, ordinary ones and `with` ones. They are the inputs of the program: a run gets their values from the command line, a translation takes them as the inputs of what it produces (see "ONNX export as a pass").

    camel model.cml --arg x=x.npy --arg p=weights --arg scale=0.5 -o out

- **`--arg name=value`** gives the parameter `name` its value; the option is repeated for each parameter. How `value` is read follows the declared type of the parameter:
  - `int`, `float`, `bool`, `string`: the literal (`3`, `0.5`, `true` / `false`, any text).
  - `Tensor`: a `.npy` file (the extension may be left out). The file is read as `tensor.load_npy` reads it (float64 data is stored as float32, the module's float storage) and the tensor is then checked against the declared element type and shape; a mismatch (an integer file for a float parameter, another shape) is an error, not a conversion.
  - a struct or a tuple (a model's weights): a directory holding one file per leaf, named by the leaf's path with dots (`fc1.w.npy`, `0.npy`), which is the layout `nn.save_params` writes.
  - `with` parameters are given the same way as ordinary ones. A `var` parameter cannot be given.
- **Binding is partial application.** A parameter that gets a value becomes a constant of the entry graph before the first pass runs, so the passes see it as one (they fold and specialize with it). The others stay parameters of the entry graph; `std::rgir` shows them as `PORT` nodes named like the parameters.
- **Running needs every parameter.** A scheduling pass (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`, or the default run) refuses an entry graph that still has parameters with `RuntimeDiag::EntryArgumentMissing` (`0x04000010`, stage `run`), located at the definition of `main` and naming the parameters without a value. Nothing runs; no default, zero or random value is ever assumed. Rewriting and translation passes accept open parameters.
- **A bad argument** (a name that is no parameter, a name given twice, text that is not a literal of the type, a missing file, a tensor of another shape, `--arg` for a `main` without parameters) is `RuntimeDiag::EntryArgumentInvalid` (`0x04000011`), located at `main`, before any pass runs.
- **The value `main` returns goes to the output**, `-o` / `--output <file>`, which is standard output by default, when a scheduling pass has run the program. The exact rule:
  - `main` returns nothing (`void`): nothing is written. With `-o` that is `RuntimeDiag::EntryResultNotWritten` (`0x04000012`), located at `main`.
  - `main` returns an exit code (`int`, `bool`, `byte`: the types the process exit code is derived from): the value is the exit code, as for any `main`, and is **not** printed on standard output, so a program whose `main` returns its status behaves as it always did. It is written (as text) only when `-o` names a file.
  - `main` returns anything else (a `float`, a `string`, a tensor, a tuple, a struct, ...): that value is the result of the program. Without `-o` it is printed on standard output as `println` prints it, on a line of its own after the program's own output (a tuple or struct in its text form, e.g. `(tensor([...]), tensor([...]))`). With `-o path` it is written in the layout `--arg` reads: a scalar as a text file holding the literal, a tensor as a `.npy` file (`.npy` is appended when `path` has another extension or none), a struct or tuple as the directory `path` with one file per leaf (`0.npy`, `1.w.npy`).
  - A value that cannot be written to the file (an unwritable path, a type without a file format such as an array or a function) is `EntryResultNotWritten`, located at `main`; the exit status is 1.
  - The rule does not depend on whether `main` has parameters. Translation passes and dumps are not affected: they write what they always wrote.
- `--timeit` binds `--arg` like a plain run; the result of `main` is neither printed nor written there.

### Pass pipelines

The passes given on the command line run in the order written. Each pass transforms the graph, executes it (a scheduling pass) or translates it (a translation pass).

- **Default pipeline.** With no pass given, `std::default` runs; it is an alias of `std::nodevm` (the node VM). The same default runs after the given passes when none of them consumed the graph. `.opencmlrc` (`passes.fallback`) can change it.
- **Every pass name is checked before anything runs.** After the program is compiled (passes contributed by modules exist only then) and before the first pass runs or the first `--dump-after-each-pass` snapshot is written, all names of the pipeline and of the fallback are resolved (aliases first). The first unknown one is reported as `RuntimeDiag::UnrecognizedGraphPass` with stage `pass:<name>`, the available passes are listed on stderr, and the exit code is 1. Nothing has executed: `camel f.cml std::nvm std::no_such_pass` does not run the program.
- **Passes written after a pass that consumes the graph never run.** A scheduling pass (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`) executes the program and a translation pass (`std::gir`, `std::rgir`, `std::cpp`, `onnx::export`, the bytecode and JIT dumps) translates it; neither hands a graph on, so the pipeline ends there. `camel f.cml std::nvm std::inline` runs the program on the node VM and never runs `std::inline`. This is accepted, with the warning `RuntimeDiag::PassesNeverRun` (`0x0410000F`, stage `pass:<consuming pass>`) on stderr before anything runs, naming the passes that will not run (see "Warnings").
- **Pass order is not restricted by the language, and it can matter.** A pass does not promise to produce the same graph as another order would: it modifies the graph and may have side effects. "Any pass sequence gives the same behaviour" is not a property of Camel.
- **Standard optimisation passes** (`std::devirtualize`, `std::specialize`, `std::inline`, `std::opt::fold`, `std::opt::cse`, `std::opt::dce`, `std::opt::simplify`, `tensor::fuse`) aim to preserve a program's results. That is a quality requirement on those passes, checked by tests, not a language guarantee.
- **`std::macro` is optional.** Without it a `macro` function is an ordinary function that runs with the program.
- **Libraries built on passes can have preconditions.** The `autodiff` library needs `std::macro` to run before `grad` / `value_and_grad` / `vjp` can be expanded: a graph on which macros have not run lacks the information differentiation needs. This is a constraint of that library, not of the language core.

### Common Passes

- `std::inline`: inline-only rewrite pass; default target strategy is `std::inline::hybrid`
- `std::inline::small` / `std::inline::arm` / `std::inline::hybrid`: explicit inline target strategies
- `std::devirtualize`: runtime-native devirtualization pass
- `std::specialize`: runtime-native specialization pass
- `std::opt`: aggregate runtime-native optimization pipeline (`devirtualize` + `specialize` + `inline`)
- `std::fvm`: high-performance bytecode VM scheduler
- `std::jit`: JIT-enabled bytecode VM scheduler
- `std::gir`: dump the current graph IR and stop
- `tensor::fuse`, `onnx::export`: passes contributed by modules. Writing `<module>::<pass>` on the command line loads the module `<module>` when the program has not imported it, so a program need not import a module only to be translated by its pass. A module that cannot be found, or that has no such pass, leaves the name unknown (`RuntimeDiag::UnrecognizedGraphPass`)

### Pass order: `std::macro` before the inlining passes

Macro operators over functions (`grad`, `value_and_grad`, `vjp` of `autodiff`) transform a function's graph as compiled. Run `std::macro` first (`camel m.cml std::macro std::opt::simplify std::fvm`): it expands them ahead of time and the later passes optimize the result. Without `std::macro`, or with it after `std::inline` / `std::opt` / `std::opt::simplify`, the operator runs with the program; to keep that working, the inlining passes inline nothing into the functions an unexpanded macro operator will transform (every function, when the operator's function argument is not a constant, e.g. a parameter). Such a pipeline gives the same results but leaves those functions unoptimized. See `docs/technical/26_autodiff_as_graph_rewrite.md` §2.

### What `std::macro` reports

`std::macro` replaces a call of a `macro` function or macro operator by its result when every argument is a constant. Two things can keep a call in the program:

- **An argument is not a constant.** This is the normal case and not a failure; nothing is printed. With `-v` a log line names the call and the argument: `<Macro> 'count' at m.cml:11:17 stays a run-time call: argument 1 is not a constant.`
- **The evaluation fails**: an operator raises an error (division by zero, `autodiff: no derivative rule for operator ...`), an exception escapes, or the evaluation needs more than 64 nested calls. This is a compile-time error (decided 2026-10-06, design principles 2.7): `RuntimeDiag::MacroEvaluationFailed` (`0x0400000E`, stage `pass:std::macro`), located at the call, with the reason: `m.cml:19:17: [Error]: 'boom' could not be evaluated by std::macro: DivisionByZero: Attempted division by zero.` The pipeline stops there with exit code 1; no later pass runs and the program is not executed.

Consequences of the rule:

- A program is rejected for a failing macro call **even if the call would never be reached at run time** (a call in a function nothing calls, or in an arm that is not taken): `std::macro` evaluates every macro call whose arguments are constants.
- A macro recursion deeper than the evaluator's 64 frames is a failed evaluation, so a program that runs without `std::macro` can be rejected with it (`count(40)` for `macro func count(n) = if n == 0 then 0 else 1 + count(n - 1)`).
- Effects that the failed evaluation performed before it failed have happened at pass time (once per round of the pass). They are not repeated, since the program does not run.
- Without `std::macro` in the pipeline nothing changes: the call is an ordinary call and fails, if at all, when it is reached.
- `onnx.export_model` expands macros of the function it exports with the same evaluator while the program runs; a failed evaluation there is the same diagnostic, with stage `run`.

Before this rule (up to commit `b0fa6b4b`) a failed evaluation was the warning `RuntimeDiag::MacroEvaluationDeferred` and the call was left to run with the program; that behaviour is one commit away (`git revert` of the commit that introduced `MacroEvaluationFailed`).

### ONNX export as a pass (`onnx::export`)

    CAMEL_ONNX_OUT=model.onnx camel m.cml std::macro std::opt::simplify onnx::export

`onnx::export` is a translation pass contributed by the `onnx` module. As the last pass of a pipeline it writes an ONNX model and consumes the graph: the program is not executed.

- **What is exported** is declared in the program, by the same call that exports at run time: `onnx.export_model(fn, example, path[, dynamic_axes[, target]])`. The pass looks for that call in the entry graph left by the preceding passes and reads its arguments there, so they must be constants of the graph by then: the function (a closure may capture constants), the example inputs (their dtypes and shapes become the model inputs), `dynamic_axes` and `target`. `dynamic_axes` applies to every tensor argument and cannot name one of them: with a single tensor argument its axis 0 is the symbolic size `batch`; with several (weights passed next to the data) each argument gets its own symbolic size (`<input>_dim0`), so nothing is claimed about their equality. Parameters passed as a struct are never dynamic. Values that only exist at run time (random initial weights, a loaded file) are not available to a pass; such a program exports with `camel m.cml`, which runs `export_model`.
- **Where it is written**: the environment variable `CAMEL_ONNX_OUT` if set (it then requires exactly one `export_model` call), otherwise the call's `path` argument, which must be a string literal (`get_env(...)` is not evaluated by a pass).
- **What the pass does itself**: it binds the example types (dtype and shape) to the function's parameters, re-infers node types, and then finishes the simplification of its private copy of the function with the two things only an export knows: the shapes of the examples (shape arithmetic folds, a recursion bounded by a shape unrolls) and the split of the gradient of a data-dependent branch into per-arm calls, which is valid only for a consumer that never executes the graph. This is the same simplification `export_model` performs, so the two entries accept the same functions and write equivalent models. The pass does not expand macros (that is `std::macro` before it), and it still needs `std::opt::simplify` before it: the export request is only visible once `main` has been inlined into the entry graph.
- **Refusal**: if the graph still needs one of those (the entry graph still calls `main`, a macro operator such as `grad` is not expanded, a call remains in the function that is not a tail recursion: a tail recursion whose depth depends on the model input is exported as an ONNX `Loop`), or contains something ONNX cannot express (an impure operator, an operator without a lowering), the pass writes no file and exits non-zero with the diagnostic `RuntimeDiag::TranslationRefused` (`0x0400000B`, stage `pass:onnx::export`); the message names the pass to run. Every refusal has a source position: the offending node when there is one; the branch, for an `export_model` call written in an arm of a branch whose condition is not known at compile time (`if get_env('MODE') == ... then export_model(...)`: the pass does not run the program, so it cannot tell whether the request is made; the message gives the position of the call); the `export_model` call for a refusal about the request as a whole (a dynamic axis out of range, an unknown target, the number of examples, a result that is not a tensor, a model file that cannot be written; with `CAMEL_ONNX_OUT` and several calls, the second call); the entry function `main` when no call is visible (the entry graph still calls `main` and the request is inside it) or when the refusal is about `main` as the model (a parameter without a shape, `CAMEL_ONNX_OUT` not set). When no call is visible although the inliner ran, the message says why the inliner left the call behind which the request sits: its growth budget was exhausted (the message gives the budget, whether it is the default or set by `CAMEL_OPT_GROWTH_BUDGET`, what was spent and what the call needs, and names the variable to raise), or the function that makes the request reaches a recursion and is not small, or it is recursive, or too large to copy to its call sites. Only when no pass that inlines ran does it ask for `std::opt::simplify`. An impure operator is refused wherever it is reachable from the function's result, through data or control edges and inside the functions it calls: a `println` statement of a `sync` function is refused at the `println`, not left out of the model. `onnx.export_model` applies the same check.

**A program without an `export_model` call: `main` is the model.** A program that only computes is exported by the same pass composition, with nothing written for the export in the program (not even `import onnx`: naming the pass loads the module):

    CAMEL_ONNX_OUT=model.onnx camel model.cml std::macro std::opt::simplify onnx::export
    CAMEL_ONNX_OUT=model.onnx camel model.cml --arg p=weights std::macro std::opt::simplify onnx::export

When no `export_model` call exists anywhere in the program, the pass translates the entry graph itself, that is the computation of `main`:

- **Inputs**: one per parameter the entry graph still has, `with` parameters first, named like the parameter. A tensor parameter must declare its element type and shape (`Tensor<f32, const [-1, 3]>`); a negative extent is a dynamic axis (symbolic size `<name>_dim<axis>`). A parameter declared `Tensor` without them is refused, naming the parameter. A number is a rank-0 input. A struct or tuple gives one input per leaf, named by its path (`p_fc1_w`, `t_0`). A `main` without parameters gives a model without inputs (a constant model).
- **Weights**: a parameter given a value with `--arg` at export is a constant of the graph and becomes an initializer, named after the parameter or, for a struct, after its fields (`w`, `b`). So the usual split is: data as ordinary parameters (inputs), weights as `with` parameters, which are initializers when their values are supplied at export and inputs when they are not. A training step written as `main` (weights in as `with` parameters, updated weights in the result) is exported without values: the weights are inputs and outputs.
- **Outputs**: the value `main` returns; a tuple or struct is expanded into one output per leaf (`output`, or `output_0`, `output_1_w`, ...).
- **Where it is written**: `CAMEL_ONNX_OUT`, which is required here.
- **Refusal**: the pass translates what it can and refuses where it cannot, with `TranslationRefused` at the construct: an effect (`println`, a file read), an unexpanded macro (run `std::macro`), a call no simplification removes, an operator without a lowering. There is no separate check that the program is "pure"; a program that does more than compute exports one of its functions with `onnx.export_model` instead, and the refusal says so. The pass inlines the call of `main` itself, so it also works without `std::opt::simplify` before it; `std::macro` is still needed for programs that use `grad` or `value_and_grad`.
- A program that does contain an `export_model` call is never translated this way, wherever the call is: if the call is not visible in the entry graph (nothing was inlined), the refusal names `std::opt::simplify` as before.

To check an exported model against the program itself, run the program with the same values and compare: `camel model.cml --arg x=x.npy --arg p=weights -o out` writes what `main` returns in files a NumPy script reads.

Running the program without passes (`camel m.cml`) keeps working: `export_model` then expands macros and simplifies the function itself. What it accepts and the pass refuses are programs whose export request or captured values only exist at run time (random initial weights, a path from `get_env`).

### Warnings

A warning (a `[Warn]` diagnostic) does not stop a program. The compiler's warnings about effects:

| Name | Code | Where | Meaning |
|---|---|---|---|
| `SemanticDiag::EffectNotExecuted` | `0x03100004` | the call, assignment or branch | It has side effects but nothing depends on it and it is not in a sync block, so it never runs. Legal (evaluation is driven by dependencies); reported so that a forgotten `sync` is seen. One warning per dropped chain (`println(log(1))` reports the `println`). Never reported inside a sync block |
| `SemanticDiag::IgnoredSideEffect` | `0x03100002` | the call or branch | It writes a variable (a `var` argument, or a captured variable) and is neither waited nor in a sync block: it is not ordered after earlier uses of the variable |
| `SemanticDiag::VarParamInAsyncFunction` | `0x03100001` | the parameter | A `var` parameter in a function without the `sync` modifier |
| `SemanticDiag::CapturedWriteInAsyncFunction` | `0x03100003` | the function | The function writes a captured variable and has no `sync` modifier |
| `RuntimeDiag::PassesNeverRun` | `0x0410000F` | no source position (a command-line matter) | Passes are written after a pass that consumes the graph; they will not run. Printed with the program's warnings (the last element of the stderr array in JSON mode) |

What counts as an effect for `EffectNotExecuted`: an operator that is neither pure nor state-only in the operator-traits registry (the same set the effect trace records), a call of a function declared `sync func` or with a `var` parameter, a call of a function whose own result depends on an effect, and a branch with such an arm. A call through a function value is only recognised by its type (`sync` modifier, `var` parameter).

- **A program that compiles without an error** prints its warnings on **stderr**, once, after compilation and before the first pass runs, at the default log level (no `-v` needed). In text mode, one line per warning: `(module)file:line:col: [Warn]: text suggestion (name=SemanticDiag::IgnoredSideEffect, code=0x03100002)`. With `--output-format json`, one JSON array on stderr with the same objects as the error array below (`"severity":"Warn"`). Nothing is written when there is no warning (no empty array).
- **stdout is the same in both modes as without warnings**: the program's output, and the diagnostics array only when there is an error. Tools that parse stdout are not affected.
- **A module that has an error** shows its warnings with the error, as before (in the stdout array in JSON mode); they are not repeated on stderr.
- `camel check` prints them the same way. `--timeit`, `camel inspect`, `camel-cpp` and the debugger do not print warnings.

### JSON diagnostics (`--output-format json`)

Errors are printed on stdout, after any program output, as a JSON array of objects:

```json
{"range":{"start":{"line":6,"character":13},"end":{"line":6,"character":14}},"severity":"Error","code":"0x04000001","source":"Camel","message":"Attempted division by zero.","data":{"stage":"run","name":"RuntimeDiag::DivisionByZero","moduleName":"m","modulePath":"m.cml","suggestion":"..."}}
```

- `range` is 0-based; `data.modulePath` is the file. A diagnostic without a source position (an unknown pass name, passes that never run) has an all-zero range and an empty `modulePath`.
- `data.name` is the stable diagnostic name; `code` is its numeric form.
- Every graph a pass commits (and every graph the front end encodes) is validated structurally, in release builds as well: node, slot and edge references in range, inputs and outputs mirroring each other, `BRCH`/`JOIN` arm counts, a `GATE` with a value and a control input, a direct call passing as many arguments as its callee takes. A violation is a `RuntimeDiag::PassInternalError` naming the pass that committed the graph, before anything runs.
- An exception that escapes a pass without being a diagnostic (graph validation when a rewrite is committed, a cycle met while a scheduler builds its execution order, an arena limit) is reported as `RuntimeDiag::PassInternalError` (`0x0400000C`) with stage `pass:<name>`, the exception text in the message, no source position, and exit code 1. It marks a defect of the compiler, not a refusal of the program. The stage names the pass even when it is a scheduling pass, so it does not tell whether part of the program had already run. An exception outside any pass is reported as `InternalDiag::UnknownInternalError` with stage `internal`.
- `data.stage` tells where in the pipeline the diagnostic was raised: `parse`, `typecheck`, `pass:<name>` for a rewrite or translation pass (including an unknown pass name), `run` for a scheduling pass (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`), or `internal`.

### Effect trace (`CAMEL_EFFECT_TRACE`)

Setting the environment variable `CAMEL_EFFECT_TRACE=<file>` makes the process append one JSON line per executed effectful operator (any operator not registered as pure or as state-only in the operator-traits registry: `println`, file I/O, `py_*`, assignment `:op/assn*`, in-place array operators, ...), in call order. Creating and reading a `var` variable (`:var/new`, `:var/get`) are state-only and emit no event; a store into one (`:op/assn*`) does, as before variables became cells:

```json
{"seq":0,"uri":":io/println","stage":"run","origin":{"file":"a.cml","line":12,"col":5},"payload_sha1":"...","payload":"string=hello|||"}
```

- `stage` is `macro` while `std::macro` evaluates a call ahead of time and `run` otherwise.
- `origin` is the 1-based source position of the operator node, or `null` when the node has none (nodes synthesized by passes).
- `payload_sha1` hashes the printed argument values, taken before the operator runs.
- `payload` is the text that is hashed: each argument as `type=value` followed by `|`, the normal arguments first, then `||`, then the `with` arguments (in the hashed bytes the two separators are `0x1f` and `0x1e`). It is cut to 2048 bytes; a cut line also carries `"payload_truncated":true`, and `payload_sha1` still covers the full text.

The trace is written by a wrapper installed where operator URIs are resolved, so all back ends (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`) and the macro evaluator are covered. It does not record whether the operator ran inside a `sync` scope. The file is truncated at the first effect of each process. See `include/camel/core/effect_trace.h`.

### Resource limits

Limits of the compiler and the back ends. A pass that reaches one of its limits stops transforming and passes the program on (it means the same, it is only less optimized), unless the table says it fails.

| Limit | Value | Adjustable | When reached |
|---|---|---|---|
| Growth of `std::inline`, `std::specialize`, `std::opt`, `std::opt::simplify` (nodes copied by specializing and inlining; one budget for all rounds of `std::opt::simplify`) | 4096 nodes | `CAMEL_OPT_GROWTH_BUDGET=<nodes>` | nothing further is specialized or inlined; the remaining calls stay calls. Logged with `-v` |
| Size of a graph that `std::inline` inlines into | 4096 nodes | no | that call is not inlined |
| Rounds of `std::opt::simplify` | 16 | no | the program is passed on as it is. Logged with `-v` |
| Type-only specializations of one recursive function (a tensor argument whose shape changes at every level of the recursion) | 8 | no | the call keeps its callee |
| Non-tail recursion on `std::nvm`, `std::fvm` and the interpreted part of `std::jit` | 256 frames | no | the program stops with `RuntimeDiag::MaxRecursionDepthExceeded` (`0x04000004`, stage `run`, exit 1) at the call expression that would open frame 257; what ran before it has run |
| Recursion in code `std::jit` compiled to native code (a self-recursive function whose branch arms were inlined, e.g. after `std::inline` or `std::opt`) | none | no | limited only by the frame pool; exhausting it aborts the process (`std::bad_alloc`), without a diagnostic |
| Recursion on `std::tf` | none | no | limited only by the native stack; exhausting it is a segmentation fault |
| Ahead-of-time evaluation of a `macro` function by `std::macro` | 64 evaluator frames (a self-recursion of 32 levels: `count(31)` is evaluated, `count(32)` is not) | no | the evaluation has failed: error `RuntimeDiag::MacroEvaluationFailed` at the call, the pipeline stops (see "What `std::macro` reports") |
| Nodes, node blocks and edges of one graph | 65535 each (16-bit) | no | the pass that built the graph fails with `RuntimeDiag::PassInternalError` (`... exceeds the target integral width`) before anything runs |
| Permanent space (types and layouts) | 32 MB | no | the pass fails with `RuntimeDiag::PassInternalError` (`Permanent space exhausted`) before anything runs |

Recursion depth counts frames, not source-level calls: a call in tail position reuses its frame (see `docs/technical/19_tail_call_optimization.md`), so tail recursion of any depth runs, and the frames of `main` and of the entry graph count towards the 256 (in `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)` called from `main`, `depth(253)` is the deepest call that runs on `std::nvm` and `depth(255)` on `std::fvm` / `std::jit`). The limits marked "no" are compile-time constants (`nodevm.h`, `fastvm.h`, `src/execute/macro/eval.cpp`, `generic.h`); there is no command-line option or `.opencmlrc` key for them (`.opencmlrc` only configures the pass list). The recursion limit is not the same under every pipeline today: `std::inline std::jit` and `std::tf` run `depth(300)` to completion where `std::nvm`, `std::fvm` and plain `std::jit` stop with the diagnostic.

`std::opt::simplify` unrolls a recursion whose arguments are constants one level per round (`fib(15)` would become its whole call tree); the growth budget is what stops it. `onnx.export_model` simplifies the function it exports without the growth budget and the inline size cap, because an export has to reach straight-line code; a pipeline that exports through `std::opt::simplify onnx::export` is held to them and may need `CAMEL_OPT_GROWTH_BUDGET` raised for a large model (the export pass then refuses with `TranslationRefused` rather than writing a partial model, and the refusal names the budget, its value and the variable).

---

## Information Display

- `-V`, `--version`: Show version information  
- `-h`, `--help`: Show help information  
- `-d`, `--docs`: Show documentation of package or module  
- `-a`, `--about`: Show copyright and project information  
- `-z`, `--zen`: Show the Zen of Camel

---

## check: Code Health Checker

    camel check [options] <cml file or directory>

- `-i`, `--lexical-only`: Perform lexical analysis only  
- `-s`, `--syntax-only`: Perform syntax check only (excluding formatting or semantics)  
- `-O`, `--output-format <text|json>`: Output format (default: `text`)  
- `-N`, `--max-warning <num>`: Maximum number of allowed warnings  
- `-c`, `--config <file>`: Path to rule definition file  
- `-e`, `--ignore`: Ignore definition files  
- `-o`, `--output <file>`: Output file (default: console)  
- Available global options: `-v` / `-vv` / `-vvv` / `-vvvv`, `--verbose`, `--log-level`, `--log-preset`, `--log-include`

---

## inspect: Intermediate State Viewer

    camel inspect [options] <cml file>

- `-t`, `-T`, `--tok`, `--token-stream`: Print lexical tokens  
- `-s`, `-S`, `--cst`, `--concrete-syntax-tree`: Print Concrete Syntax Tree (CST)  
- `-a`, `-A`, `--ast`, `--abstract-syntax-tree`: Print Abstract Syntax Tree (AST)  
- `-c`, `-C`, `--gct`, `--graph-construct-tree`: Print Graph Construction Tree (GCT)  
- `-g`, `-G`, `--gir`, `--graph-ir`: Print Graph Intermediate Representation (GIR)  
- `--tns`, `--topo-node-seq`: Print topologically sorted node sequence  
- `--gen`, `--gene-code`: Generate code from AST  
- `-p`, `-P`, `--pass-until <n>`: Execute up to a specified graph optimization stage  
- Available global options: `-v` / `-vv` / `-vvv` / `-vvvv`, `--verbose`, `--log-level`, `--log-preset`, `--log-include`

---

## Related Tools

Camel CLI (`camel`) handles run, check, and inspect. Formatting is provided by a separate tool:

- **camel-format**: Format `.cml` source files. Usage: `camel-format [options] <file>`  
  - `-i`, `--inplace`: Write back to file  
  - `-t`, `--tab-size <N>`: Indent size (default 4)  
  - `-u`, `--use-tabs`: Use tabs  
  - `-q`, `--quote-prefer <single|double>`  
  - `-m`, `--max-width <N>` (default 80)

- **camel-cpp**: Transpile `.cml` to C++ and compile in one step. Outputs to `tmp/`.
  - Usage: `camel-cpp <file.cml>`
  - Equivalent to: `camel <file.cml> std::cpp` with output written to `tmp/<stem>.cpp`, then compiles with clang++, producing `tmp/<stem>.exe` and `tmp/libcamel.dll`.
  - Built at: `build/tools/camel-cpp/Release/camel-cpp.exe` (Windows; libcamel.dll is copied alongside for portability).

Other tools: `camel-codegen`, `camel-profiler`. All are built under `build/tools/`.

---

## Examples

```bash
# Run the main file (positional)
camel main.cml

# Run with input and passes via options (no positional required)
camel --input main.cml --passes std::gir,other

# Run and record performance
camel -P main.cml

# Measure only std::jit across 10 fresh executions
camel --input test/cases/perf/fib_recursive.cml --timeit std::jit/10

# Emit the timeit summary as JSON
camel --input test/cases/perf/fib_recursive.cml --timeit std::jit/10 --output-format json

# Transpile to C++ and compile (one step, output to tmp/)
camel-cpp test/cases/linear/nvm/fib.cml

# Format code (use camel-format, not camel format)
camel-format -i src/xxx.cml

# Syntax check only and output in JSON
camel check -s -O json src/

# Print intermediate representation
camel inspect --gir main.cml

# View Zen of Camel
camel --zen
```

