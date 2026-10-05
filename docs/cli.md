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
- `-P`, `--profile`: Record time spent in each stage and output a performance report  
- `-S`, `--scheduler <type>`: Specify the scheduler type  
- `-t`, `--threads <num>`: Maximum number of threads  
- `-n`, `--no-cache`: Disable cache modules  
- `-I`, `--include <dir>`: Add module search path (can be used multiple times)  
- `-L`, `--stdlib <path>`: Specify standard library path (default: `./stdlib` under current directory)  
- `-E`, `--error-format`, `--output-format <text|json>`: Output format (default: `text`); `--timeit` reuses this, and the test harness expects `json`

### Pass pipelines

The passes given on the command line run in the order written. Each pass transforms the graph, executes it (a scheduling pass) or translates it (a translation pass).

- **Default pipeline.** With no pass given, `std::default` runs; it is an alias of `std::nodevm` (the node VM). The same default runs after the given passes when none of them consumed the graph. `.opencmlrc` (`passes.fallback`) can change it.
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
- `tensor::fuse`, `onnx::export`: passes contributed by modules; they exist only when the program imports the module (`tensor`, `onnx`)

### Pass order: `std::macro` before the inlining passes

Macro operators over functions (`grad`, `value_and_grad`, `vjp` of `autodiff`) transform a function's graph as compiled. Run `std::macro` first (`camel m.cml std::macro std::opt::simplify std::fvm`): it expands them ahead of time and the later passes optimize the result. Without `std::macro`, or with it after `std::inline` / `std::opt` / `std::opt::simplify`, the operator runs with the program; to keep that working, the inlining passes inline nothing into the functions an unexpanded macro operator will transform (every function, when the operator's function argument is not a constant, e.g. a parameter). Such a pipeline gives the same results but leaves those functions unoptimized. See `docs/technical/26_autodiff_as_graph_rewrite.md` §2.

### What `std::macro` reports

`std::macro` replaces a call of a `macro` function or macro operator by its result when every argument is a constant. Two things can keep a call in the program:

- **An argument is not a constant.** This is the normal case and not a failure; nothing is printed. With `-v` a log line names the call and the argument: `<Macro> 'count' at m.cml:11:17 stays a run-time call: argument 1 is not a constant.`
- **The evaluation fails**: an operator raises an error (division by zero, `autodiff: no derivative rule for operator ...`), an exception escapes, or the evaluation needs more than 64 nested calls. The call stays in the program and runs with it, and the pass prints the warning `RuntimeDiag::MacroEvaluationDeferred` (`0x0410000D`, stage `pass:std::macro`) on stderr at the default log level, located at the call, with the reason: `m.cml:17:17: [Warn]: 'boom' could not be evaluated ahead of time and is left to run with the program: DivisionByZero: Attempted division by zero.` The program is not rejected: if the call is never reached at run time, it runs as it does without the pass. If the run later ends in an error, the warning is also in the diagnostics printed then (and in the `--output-format json` array, with severity `Warn`).

Known gap: effects that the failed evaluation performed before it failed have happened at pass time (once per round of the pass) and happen again when the call runs. A `macro` function with effects that can fail is therefore not reproduced by a pipeline containing `std::macro`.

### ONNX export as a pass (`onnx::export`)

    CAMEL_ONNX_OUT=model.onnx camel m.cml std::macro std::opt::simplify onnx::export

`onnx::export` is a translation pass contributed by the `onnx` module. As the last pass of a pipeline it writes an ONNX model and consumes the graph: the program is not executed.

- **What is exported** is declared in the program, by the same call that exports at run time: `onnx.export_model(fn, example, path[, dynamic_axes[, target]])`. The pass looks for that call in the entry graph left by the preceding passes and reads its arguments there, so they must be constants of the graph by then: the function (a closure may capture constants), the example inputs (their dtypes and shapes become the model inputs), `dynamic_axes` and `target`. Values that only exist at run time (random initial weights, a loaded file) are not available to a pass; such a program exports with `camel m.cml`, which runs `export_model`.
- **Where it is written**: the environment variable `CAMEL_ONNX_OUT` if set (it then requires exactly one `export_model` call), otherwise the call's `path` argument, which must be a string literal (`get_env(...)` is not evaluated by a pass).
- **What the pass does itself**: it binds the example types (dtype and shape) to the function's parameters, re-infers node types, folds what those types make constant (`std::opt::fold` / `std::opt::dce` on its private copy of the function) and lowers the result node by node. It does not expand macros, devirtualize, specialize calls, inline or unroll; those are the job of `std::macro` and `std::opt::simplify` before it.
- **Refusal**: if the graph still needs one of those (the entry graph still calls `main`, a macro operator such as `grad` is not expanded, a call remains in the function), or contains something ONNX cannot express (an impure operator, an operator without a lowering), the pass writes no file and exits non-zero with the diagnostic `RuntimeDiag::TranslationRefused` (`0x0400000B`, stage `pass:onnx::export`) at the offending node; the message names the pass to run. An impure operator is refused wherever it is reachable from the function's result, through data or control edges and inside the functions it calls: a `println` statement of a `sync` function is refused at the `println`, not left out of the model. `onnx.export_model` applies the same check.

Running the program without passes (`camel m.cml`) keeps working: `export_model` then simplifies the function itself, with settings the pass pipeline does not have (it inlines regardless of size, unrolls deeper recursion, and splits the gradient of a data-dependent branch), so it accepts some programs that the pipeline refuses.

### JSON diagnostics (`--output-format json`)

Errors are printed on stdout, after any program output, as a JSON array of objects:

```json
{"range":{"start":{"line":6,"character":13},"end":{"line":6,"character":14}},"severity":"Error","code":"0x04000001","source":"Camel","message":"Attempted division by zero.","data":{"stage":"run","name":"RuntimeDiag::DivisionByZero","moduleName":"m","modulePath":"m.cml","suggestion":"..."}}
```

- `range` is 0-based; `data.modulePath` is the file. A diagnostic without a source position (for example an unknown pass name) has an all-zero range and an empty `modulePath`.
- `data.name` is the stable diagnostic name; `code` is its numeric form.
- Every graph a pass commits (and every graph the front end encodes) is validated structurally, in release builds as well: node, slot and edge references in range, inputs and outputs mirroring each other, `BRCH`/`JOIN` arm counts, a `GATE` with a value and a control input, a direct call passing as many arguments as its callee takes. A violation is a `RuntimeDiag::PassInternalError` naming the pass that committed the graph, before anything runs.
- An exception that escapes a pass without being a diagnostic (graph validation when a rewrite is committed, a cycle met while a scheduler builds its execution order, an arena limit) is reported as `RuntimeDiag::PassInternalError` (`0x0400000C`) with stage `pass:<name>`, the exception text in the message, no source position, and exit code 1. It marks a defect of the compiler, not a refusal of the program. The stage names the pass even when it is a scheduling pass, so it does not tell whether part of the program had already run. An exception outside any pass is reported as `InternalDiag::UnknownInternalError` with stage `internal`.
- `data.stage` tells where in the pipeline the diagnostic was raised: `parse`, `typecheck`, `pass:<name>` for a rewrite or translation pass (including an unknown pass name), `run` for a scheduling pass (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`), or `internal`.

### Effect trace (`CAMEL_EFFECT_TRACE`)

Setting the environment variable `CAMEL_EFFECT_TRACE=<file>` makes the process append one JSON line per executed effectful operator (any operator not registered as pure in the operator-traits registry: `println`, file I/O, `py_*`, in-place assignment, ...), in call order:

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
| Ahead-of-time evaluation of a `macro` function by `std::macro` | 64 evaluator frames (a self-recursion of 32 levels: `count(31)` is evaluated, `count(32)` is not) | no | the call is not evaluated ahead of time; it stays a call and runs with the program. Warning `RuntimeDiag::MacroEvaluationDeferred` at the call |
| Nodes, node blocks and edges of one graph | 65535 each (16-bit) | no | the pass that built the graph fails with `RuntimeDiag::PassInternalError` (`... exceeds the target integral width`) before anything runs |
| Permanent space (types and layouts) | 32 MB | no | the pass fails with `RuntimeDiag::PassInternalError` (`Permanent space exhausted`) before anything runs |

Recursion depth counts frames, not source-level calls: a call in tail position reuses its frame (see `docs/technical/19_tail_call_optimization.md`), so tail recursion of any depth runs, and the frames of `main` and of the entry graph count towards the 256 (in `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)` called from `main`, `depth(253)` is the deepest call that runs on `std::nvm` and `depth(255)` on `std::fvm` / `std::jit`). The limits marked "no" are compile-time constants (`nodevm.h`, `fastvm.h`, `src/execute/macro/eval.cpp`, `generic.h`); there is no command-line option or `.opencmlrc` key for them (`.opencmlrc` only configures the pass list). The recursion limit is not the same under every pipeline today: `std::inline std::jit` and `std::tf` run `depth(300)` to completion where `std::nvm`, `std::fvm` and plain `std::jit` stop with the diagnostic.

`std::opt::simplify` unrolls a recursion whose arguments are constants one level per round (`fib(15)` would become its whole call tree); the growth budget is what stops it. `onnx.export_model` simplifies the function it exports without the growth budget and the inline size cap, because an export has to reach straight-line code; a pipeline that exports through `std::opt::simplify onnx::export` is held to them and may need `CAMEL_OPT_GROWTH_BUDGET` raised for a large model (the export pass then refuses with `TranslationRefused` rather than writing a partial model).

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

