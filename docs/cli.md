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
- `--passes <list>`: Comma-separated pass list (e.g. `--passes std::gir,other`; commas inside a pass's parameters do not split it). If omitted, first positional is file, rest are passes. A pass may take parameters: `std::opt(safe_fp=true)` (see "Pass parameters")  
- `--no-module-passes`: Do not add the passes that the program's imported modules register for the default pipeline (see "Passes of modules and the default pipeline")  
- `--timeit <pass>/<N>`: Measure one pass `N` times from a fresh graph; text mode prints samples plus a compact summary table, while `--output-format json` emits structured benchmark data  
- `--dump-after-each-pass <dir>`: Write the graph before the first pass (`00_input`) and after each pass that returns a graph (`NN_<pass>`, `::` written as `.`) into `<dir>`: a readable DOT (`.rgir.dot`, the `std::rgir` format with `origin`/`span`/`srcfile` per node) and the `std::stats` counters (`.stats.json`). Scheduling and translation passes consume the graph and leave no snapshot. Compare two snapshots with `python tools/graph-diff/graph_diff.py BEFORE AFTER`  
- `--time-phases`, `--time-phases=<file>`: Write the wall-clock time of each phase of the run (start-up, parse, GCT, GIR, each pass, teardown; one entry per module and per pass) as one JSON object to standard error, or to `<file>` (the file is joined to the option with `=`, so that the program after it is never taken for the file). With the counting allocator shim preloaded (`test/tools/allocshim`), each phase also carries its heap allocations. The environment variable `CAMEL_TIME_PHASES=<file>` does the same for every command. Off by default and free when off; the schema and the benchmark built on it are in `docs/technical/38_compile_pipeline_cost.md`  
- `--arg <name>=<value>`: Value of a parameter of `main` (repeatable; see "Programs whose `main` takes parameters")  
- `-o`, `--output <file>`: Where the value `main` returns is written (default: standard output; same section)  
- `-P`, `--profile`: Record time spent in each stage and write trace files to `profile_reports/`. Effective in debug builds only; a release build accepts the option and produces no report  
- `-S`, `--scheduler <type>`: Specify the scheduler type  
- `-t`, `--threads <num>`: Maximum number of threads  
- `--max-recursion-depth <n>`: Limit on the calls that may be open at once without being tail calls (1 to 1000000; see "Resource limits"). Also `CAMEL_MAX_RECURSION_DEPTH=<n>`; the option wins  
- `--max-macro-depth <n>`: Limit on nested calls while `std::macro` evaluates a call ahead of time (default 64; same range). Also `CAMEL_MAX_MACRO_DEPTH=<n>`  
- `--max-macro-rounds <n>`: Limit on the rounds one loop may run while `std::macro` evaluates it ahead of time (default 1000000, 1 to 2^40); a loop past it fails the evaluation. Also `CAMEL_MAX_MACRO_ROUNDS=<n>`; the option wins  
- `--gc <collector>`: Collector of run-time values: `generational` (default; young objects are copied and die cheaply) or `mark-sweep` (an object keeps its address; see "The collector"). Also `CAMEL_GC=<collector>`; the option wins  
- `-n`, `--no-cache`: Disable cache modules  
- `-I`, `--include <dir>`: Add module search path (can be used multiple times)  
- `-L`, `--stdlib <path>`: Standard library path, added to the module search path. When omitted, the environment variable `CAMEL_STD_LIB` is used; with neither, the standard library is looked up in the `stdlib` directory next to the executable and under the installation root (`CAMEL_HOME`)  
- `-E`, `--error-format`, `--output-format <text|short|json>`: Format of diagnostics (default: `text`), see "Diagnostics" below; `--timeit` reuses this for its own output
- `--color`, `--max-errors`, `-N`/`--max-warning`, `-W...`: see "Diagnostics" below

### Diagnostics

A compile error or a run-time fault is printed on standard output, a warning on standard error. An error value nobody handled that reaches the exit of `main` is printed on standard error as well (`UnhandledError`, see "Programs whose `main` takes parameters or returns a value"). See [diagnostics.md](diagnostics.md) for every diagnostic and `camel --explain <name or code>` for one of them.

Both streams carry the same bytes on every platform: a line ends with a line feed (`\n`), also on Windows, where the C runtime would otherwise write `\r\n`. The same holds for files a program writes with `fs.write_text`: the file holds the bytes of the string.

- `-E`, `--error-format`, `--output-format <text|short|json>`: how diagnostics are written (default: `text`; `--timeit` reuses the option for its own output).
  - `text`: the message, the source line with the range underlined, secondary locations (for example the call that instantiated a generic function) each with its own excerpt, then `note:` and `help:` lines.

        error[SemanticDiag::UnresolvedReference]: Unresolved reference: 'undefined_name'.
          --> prog.cml:3:13
           |
         3 |     let x = undefined_name + 1
           |             ^^^^^^^^^^^^^^
        help: Make sure the name is declared and in scope.

        camel: 1 error

    Compile errors are followed by a line with their count (`camel: 1 error`, `camel: 3 errors`); a run-time fault is not.

  - `short`: one line per diagnostic, `(module)path:line:col: [Error]: message suggestion (name=..., code=0x...)`. This is the form scripts match; the test runner uses it.
  - `json`: an array of objects with `range`, `severity`, `code`, `message`, `relatedInformation` (the secondary locations: `location.path`, `location.range`, `message`) and `data` (`stage`, `name`, `moduleName`, `modulePath`, `suggestion`, `notes`).
  - The environment variable `CAMEL_ERROR_FORMAT` gives the format when the option is absent.
  - Lines and columns start at 1 in `text` and `short` and at 0 in `json`; a column counts characters, not bytes.
- `--color <auto|always|never>`: colours in `text` and `short` (default `auto`: only when the stream is a terminal, and never when `NO_COLOR` is set).
- `--max-errors <n>`: compile errors reported before the compiler gives up (default 20; 0: no limit). When the limit is reached, the count line of the `text` format says so: `camel: 2 errors; stopped at the limit of 2 errors (--max-errors <n> raises it, 0 removes it)`. One run reports every independent error it finds; what follows from an error already reported is not reported again (see [technical/30_diagnostics_reporting.md](technical/30_diagnostics_reporting.md)). A run-time fault always ends the run at the first one.
- `-N`, `--max-warning <n>`: warnings shown (default 1000); the rest are counted and the count is printed at the end (`camel: 1 more warning(s) not shown (--max-warning 0)`).
- Warnings are named as in [diagnostics.md](diagnostics.md), without the type prefix; case, `-` and `_` do not matter (`ignored-side-effect` is `IgnoredSideEffect`). A name that is not a warning is refused: `Error: -Wno-bogus: no warning is named 'bogus'. The warnings are: ...` on stderr, followed by all warning names, exit code 1, and the program is not compiled.
  - `-Wno-<name>`: do not report the warning `<name>`.
  - `-Wnone`: report no warnings.
  - `-Werror`: report every warning as an error (the program is not run; exit code 1). In the `text` format such an error carries the line `note: this warning is reported as an error because of -Werror`.
  - `-Werror=<name>`: report the warning `<name>` as an error.
  - `-Wno-error=<name>`: keep `<name>` a warning under `-Werror`.
- `--explain <name or code>`: print the catalog entry of a diagnostic (`camel --explain IndexOutOfRange`, `camel --explain 0x04000002`). The name may be written without the type prefix; case, `-` and `_` do not matter here either. An unknown name or code is refused on stderr with exit code 1.

        RuntimeDiag::IndexOutOfRange  (0x04000002, Error)

          message: Index {0} is out of range for {1} of length {2}.
          help:    Ensure the index is within the valid range of the collection.

          {0}, {1}, ... stand for the values a concrete report fills in. A triggering example is in docs/diagnostics.md.

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
  - **Only a `main` that declares its return type has a result** (design principles 2.10). For a `main` without a declared return type (`func main() sync { println('a') }`) the value of the body (here the `'a'` that `println` returns) is not a result: it is neither printed nor written and the exit code is 0; `-o` is then `EntryResultNotWritten`. `println(x)` still returns `x`: the result of `func main(): string sync { println('a') }` is `'a'`, printed once by `println` and once more as the result.
  - `main` declared `T!`: a value that has not failed is treated as its `T` (under `int!` it is the exit code). An **error value nobody handled** is printed on **standard error** as the diagnostic `RuntimeDiag::UnhandledError` (`0x04000051`, stage `run`), the exit code is 1, and standard output holds only the program's own output. The diagnostic is written in the format `--error-format` chose (with `json`, one array on standard error) and located where the error value says it was made: the `error(...)` call, or the fault that `attempt` isolated. An error made by an operator (`parse_int`) records no position; the report is then located at `main` and a `note:` names the operator.
  - `main` returns nothing (declared `: void`): nothing is written. With `-o` that is `RuntimeDiag::EntryResultNotWritten` (`0x04000012`), located at `main`, reported after the program has run, with exit code 1.
  - `main` returns an exit code (`int`, `bool`, `byte`: the types the process exit code is derived from): the value is the exit code, as for any `main`, and is **not** printed on standard output, so a program whose `main` returns its status behaves as it always did. It is written (as text) only when `-o` names a file.
  - `main` returns anything else (a `float`, a `string`, a tensor, a tuple, a struct, ...): that value is the result of the program. Without `-o` it is printed on standard output as `println` prints it, on a line of its own after the program's own output (a tuple or struct in its text form, e.g. `(tensor([...]), tensor([...]))`). With `-o path` it is written in the layout `--arg` reads: a scalar as a text file holding the literal, a tensor as a `.npy` file (`.npy` is appended when `path` has another extension or none), a struct or tuple as the directory `path` with one file per leaf (a tensor leaf is `0.npy`, `1.w.npy`; a scalar leaf is a text file without extension, `0`, `1`).
  - A value that cannot be written to the file (an unwritable path, a type without a file format such as an array or a function) is `EntryResultNotWritten`, located at `main`; the exit status is 1.
  - The rule does not depend on whether `main` has parameters. Translation passes and dumps are not affected: they write what they always wrote.
- `--timeit` binds `--arg` like a plain run; the result of `main` is neither printed nor written there.

### Pass pipelines

The passes given on the command line run in the order written. Each pass transforms the graph, executes it (a scheduling pass) or translates it (a translation pass).

- **Default pipeline.** With no pass given, `std::default` runs; it is an alias of `std::nodevm` (the node VM), and `std::nvm`, used in the rest of this guide, is another alias of the same pass (likewise `std::fvm` is `std::fastvm`, `std::jit` is `std::fastvm::jit`, and `std::tf` and `std::prl` are `std::taskflow`). The same default runs after the given passes when none of them consumed the graph. `.opencmlrc` (`passes.fallback`) can change it.
- **Every pass name is checked before anything runs.** After the program is compiled (passes contributed by modules exist only then) and before the first pass runs or the first `--dump-after-each-pass` snapshot is written, all names of the pipeline and of the fallback are resolved (aliases first). The first unknown one is reported as `RuntimeDiag::UnrecognizedGraphPass` with stage `pass:<name>`, the available passes and aliases are listed on stderr (`Pass <std::no_such_pass> not found, available passes are:`, then `Available aliases are:`), and the exit code is 1. Nothing has executed: `camel f.cml std::nvm std::no_such_pass` does not run the program.
- **Passes written after a pass that consumes the graph never run.** A scheduling pass (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`) executes the program and a translation pass (`std::gir`, `std::rgir`, `std::cpp`, `onnx::export`, the bytecode and JIT dumps) translates it; neither hands a graph on, so the pipeline ends there. `camel f.cml std::nvm std::opt::inline` runs the program on the node VM and never runs `std::opt::inline`. This is accepted, with the warning `RuntimeDiag::PassesNeverRun` (`0x0410000F`, stage `pass:<consuming pass>`) on stderr before anything runs, naming the passes that will not run (see "Warnings").
- **Pass order is not restricted by the language, and it can matter.** A pass does not promise to produce the same graph as another order would: it modifies the graph and may have side effects. "Any pass sequence gives the same behaviour" is not a property of Camel.
- **Standard optimisation passes** (`std::opt::devirtualize`, `std::opt::specialize`, `std::opt::inline`, `std::opt::fold`, `std::opt::cse`, `std::opt::dce`, `std::opt`, `tensor::fuse`) aim to preserve a program's results. That is a quality requirement on those passes, checked by tests, not a language guarantee.
- **`std::macro` is optional.** Without it a `macro` function is an ordinary function that runs with the program.
- **Libraries built on passes can have preconditions.** The `autodiff` library needs `std::macro` to run before `grad` / `value_and_grad` / `vjp` can be expanded: a graph on which macros have not run lacks the information differentiation needs. This is a constraint of that library, not of the language core.

### Pass parameters

Every pass, scheduler and module pass is written `name` or `name(key=value, ...)`. The values are Camel constant expressions: bool, int, float and string literals, and the operators on them (`n=2*8`, `safe_fp=!false`, `out="a" + ".onnx"`); an int may be given where a float is declared. They are read by the language's own parser. A pass declares its parameters (name, type, default, a line of documentation); the pipeline checks what is written against the declaration before anything runs, and a mistake is reported as `RuntimeDiag::InvalidPassParameter` (`0x04000060`, stage `pass:<spec>`) with the column in the specification: an unknown parameter (the message lists the ones the pass takes), a value of the wrong type, a parameter written twice, a value that is not a constant, a missing value. Quote a parameterized pass in a shell (`'std::opt(safe_fp=true)'`), and mind the commas: `--passes 'a,b(x=1,y=2)'` splits only at the commas outside the parentheses and string literals. The unknown-pass listing on stderr shows the parameters of every pass with their defaults.

Settings of a pass are parameters, no longer environment variables:

| Pass | Parameters (default) |
| --- | --- |
| `std::opt`, `std::opt::algebra` | `safe_fp: bool = false` (strictly equivalent float algebra; design principles 2.15) |
| `std::fvm`, `std::jit` | `native_loop: bool = false` (run the program's LOOP nodes natively instead of lowering them to tail recursion; a debugger always does), `stats: string = ""` (append per-graph bytecode and execution counts of the run to this file) |
| `std::jit` | `hot_threshold: int = 1` (calls from one call site before a graph is compiled; 0 compiles at the first call) |
| `std::tf`, `std::prl` | `linear: bool = false` (evaluate node by node), `fanout_all: bool = false` (fan every unordered higher-order call out), `diag: bool = false` (print the decision for every graph), `bytecode: bool = true` (hand serial graphs to the bytecode interpreter), `gc_stats: int = 0` (1: statistics at the end of the run; 2: also one line per collection) |
| `onnx::export` | `out: string = ""` (the model file; see below) |

What stays in the environment is the environment of the process, not of a pass: `CAMEL_HOME`, `CAMEL_STD_LIB`, `CAMEL_PACKAGES`, the logging and diagnostics-format variables, the resource limits and the collector (`CAMEL_GC*`, `CAMEL_*_SPACE_*`, `CAMEL_MAX_RECURSION_DEPTH`), the diagnostic traces and statistics (`CAMEL_EFFECT_TRACE`, `CAMEL_TYPE_STATS`, `CAMEL_STARTUP_PROFILE`), `CAMEL_ENABLE_TEST_PASSES` (it decides which passes exist, for the tests), and `CAMEL_OPT_GROWTH_BUDGET` (a budget of the rewrite engine that the ONNX exporter also uses internally; still to become a parameter of `std::opt`).

### Passes of modules and the default pipeline

The `std::` prefix belongs to the passes of the core. A pass of a module is named by the module: `tiles::compile`, `tensor::fuse`, `onnx::export`. A module may register aliases for its passes (`registerPassAlias`); the core does not know the names of modules.

A module may also register passes that join the pipeline when the program imports the module (`registerDefaultPipelinePass`; Tiles uses this so that `@gpu` takes effect without writing a pass). They run before the first pass that consumes the graph (a scheduling or a translation pass) of every pipeline, also when the pipeline is only `std::fvm`, ordered by an explicit priority and then by name. A pass the user lists himself runs where it is written and not twice. `--no-module-passes` leaves them out.

### Common Passes

- `std::opt::inline`: inline-only rewrite pass; default target strategy is `std::opt::inline::hybrid`
- `std::opt::inline::small` / `std::opt::inline::arm` / `std::opt::inline::hybrid`: explicit inline target strategies
- `std::opt::devirtualize`: runtime-native devirtualization pass
- `std::opt::specialize`: runtime-native specialization pass
- `std::opt`: integrated optimization entry point; repeats call rewriting (`devirtualize`, `specialize`, `inline`), `fold`, `cse`, `algebra`, and `dce` until stable, within shared growth and round limits
- `std::opt::algebra`: bounded local scalar arithmetic simplification, including coefficient combination. Defaults to relaxed local reassociation; use `'std::opt::algebra(safe_fp=true)'` or `'std::opt(safe_fp=true)'` for strictly equivalent float algebra (an ordinary pass parameter). `safe_fp=false` is the default, not permission for lower precision, approximate functions or arbitrary special-value assumptions. Quote parameterized passes in PowerShell; parameters are validated before any pass runs.
- `std::opt::fold` / `std::opt::cse` / `std::opt::dce`: standalone constant folding, common-subexpression elimination, and dead-code elimination
- `std::opt::ctrl`: bounded removal of CTRL edges covered by mandatory data/control paths; `std::opt` runs it once after its other stages finish. Branch/loop boundaries and arm anchors are retained; unselected arms are never treated as mandatory dependencies
- `std::inline` (including `small` / `arm` / `hybrid`): alias of `std::opt::inline`

- `std::fvm`: high-performance bytecode VM scheduler
- `std::jit`: JIT-enabled bytecode VM scheduler. The JIT generates code for x86-64 only. In a build for another processor (arm64 macOS; `ENABLE_FASTVM_JIT` off) `std::jit` is accepted and runs the program on the bytecode interpreter, as `std::fvm` does, with the same results; the build says so once in its log (`-v`: `std::jit: this build has no native code generator ...`). It is a log line and not a warning, so the same command line runs on every platform, also under `-Werror`
- `std::gir`: dump the current graph IR and stop
- `tensor::fuse`, `onnx::export`: passes contributed by modules. Writing `<module>::<pass>` on the command line loads the module `<module>` when the program has not imported it, so a program need not import a module only to be translated by its pass. A module that cannot be found, or that has no such pass, leaves the name unknown (`RuntimeDiag::UnrecognizedGraphPass`)

The former integrated entry `std::opt::simplify` is now `std::opt`. Standalone `std::devirtualize` and `std::specialize` are now `std::opt::devirtualize` and `std::opt::specialize`.

### Pass order: `std::macro` before the inlining passes

Macro operators over functions (`grad`, `value_and_grad`, `vjp` of `autodiff`) transform a function's graph as compiled. Run `std::macro` first (`camel m.cml std::macro std::opt std::fvm`): it expands them ahead of time and the later passes optimize the result. Without `std::macro`, or with it after `std::opt::inline` / `std::opt`, the operator runs with the program; to keep that working, the inlining passes inline nothing into the functions an unexpanded macro operator will transform (every function, when the operator's function argument is not a constant, e.g. a parameter). Such a pipeline gives the same results but leaves those functions unoptimized. See `docs/technical/26_autodiff_as_graph_rewrite.md` §2.

### What `std::macro` reports

`std::macro` replaces a call of a `macro` function or macro operator by its result when every argument is a constant. Two things can keep a call in the program:

- **An argument is not a constant.** This is the normal case and not a failure; nothing is printed. With `-v` a log line on stderr names the call and the argument: `[WARN] <Macro> 'count' at m.cml:11:17 stays a run-time call: argument 1 is not a constant.`
- **The evaluation fails**: an operator raises an error (division by zero, `autodiff: no derivative rule for operator ...`), an exception escapes, or the evaluation needs more nested calls than the evaluator's limit (64 unless `--max-macro-depth` or `CAMEL_MAX_MACRO_DEPTH` says otherwise). This is a compile-time error (design principles 2.7): `RuntimeDiag::MacroEvaluationFailed` (`0x0400000E`, stage `pass:std::macro`), located at the call, with the reason (`short` format; the suggestion and the diagnostic name follow): `(m)m.cml:19:17: [Error]: 'boom' could not be evaluated by std::macro: DivisionByZero: Attempted division by zero. ...` The pipeline stops there with exit code 1; no later pass runs and the program is not executed.

Consequences of the rule:

- A program is rejected for a failing macro call **even if the call would never be reached at run time** (a call in a function nothing calls, or in an arm that is not taken): `std::macro` evaluates every macro call whose arguments are constants.
- A macro recursion deeper than the evaluator's limit (64 frames by default) is a failed evaluation, so a program that runs without `std::macro` can be rejected with it (`count(40)` for `macro func count(n) = if n == 0 then 0 else 1 + count(n - 1)`). The message names the limit and how to raise it: `'count' could not be evaluated by std::macro: the evaluation needs more than 64 nested calls (at 'count'). The limit is set with --max-macro-depth <n> or CAMEL_MAX_MACRO_DEPTH=<n>.`
- Effects that the failed evaluation performed before it failed have happened at pass time (once per round of the pass). They are not repeated, since the program does not run.
- Without `std::macro` in the pipeline nothing changes: the call is an ordinary call and fails, if at all, when it is reached.
- `onnx.export_model` expands macros of the function it exports with the same evaluator while the program runs; a failed evaluation there is the same diagnostic, with stage `run`.

### ONNX export as a pass (`onnx::export`)

    camel m.cml std::macro std::opt 'onnx::export(out="model.onnx")'

`onnx::export` is a translation pass contributed by the `onnx` module. As the last pass of a pipeline it writes an ONNX model and consumes the graph: the program is not executed.

- **What is exported** is declared in the program, by the same call that exports at run time: `onnx.export_model(fn, example, path[, dynamic_axes[, target]])`. The pass looks for that call in the entry graph left by the preceding passes and reads its arguments there, so they must be constants of the graph by then: the function (a closure may capture constants), the example inputs (their dtypes and shapes become the model inputs), `dynamic_axes` and `target`. `dynamic_axes` applies to every tensor argument and cannot name one of them: with a single tensor argument its axis 0 is the symbolic size `batch`; with several (weights passed next to the data) each argument gets its own symbolic size (`<input>_dim0`), so nothing is claimed about their equality. Parameters passed as a struct are never dynamic. Values that only exist at run time (random initial weights, a loaded file) are not available to a pass; such a program exports with `camel m.cml`, which runs `export_model`.
- **Where it is written**: the parameter `out` if given (it then requires exactly one `export_model` call), otherwise the call's `path` argument, which must be a string literal (`get_env(...)` is not evaluated by a pass).
- **What the pass does itself**: it binds the example types (dtype and shape) to the function's parameters, re-infers node types, and then finishes the simplification of its private copy of the function with the two things only an export knows: the shapes of the examples (shape arithmetic folds, a recursion bounded by a shape unrolls) and the split of the gradient of a data-dependent branch into per-arm calls, which is valid only for a consumer that never executes the graph. This is the same simplification `export_model` performs, so the two entries accept the same functions and write equivalent models. The pass does not expand macros (that is `std::macro` before it), and it still needs `std::opt` before it: the export request is only visible once `main` has been inlined into the entry graph.
- **Refusal**: if the graph still needs one of those (the entry graph still calls `main`, a macro operator such as `grad` is not expanded, a call remains in the function that is not a tail recursion: a tail recursion whose depth depends on the model input is exported as an ONNX `Loop`), or contains something ONNX cannot express (an impure operator, an operator without a lowering), the pass writes no file and exits non-zero with the diagnostic `RuntimeDiag::TranslationRefused` (`0x0400000B`, stage `pass:onnx::export`); the message names the pass to run. Every refusal has a source position: the offending node when there is one; the branch, for an `export_model` call written in an arm of a branch whose condition is not known at compile time (`if get_env('MODE') == ... then export_model(...)`: the pass does not run the program, so it cannot tell whether the request is made; the message gives the position of the call); the `export_model` call for a refusal about the request as a whole (a dynamic axis out of range, an unknown target, the number of examples, a result that is not a tensor, a model file that cannot be written; with the parameter `out` and several calls, the second call); the entry function `main` when no call is visible (the entry graph still calls `main` and the request is inside it) or when the refusal is about `main` as the model (a parameter without a shape, `out` not given). When no call is visible although the inliner ran, the message says why the inliner left the call behind which the request sits: its growth budget was exhausted (the message gives the budget, whether it is the default or set by `CAMEL_OPT_GROWTH_BUDGET`, what was spent and what the call needs, and names the variable to raise), or the function that makes the request reaches a recursion and is not small, or it is recursive, or too large to copy to its call sites. Only when no pass that inlines ran does it ask for `std::opt`. An impure operator is refused wherever it is reachable from the function's result, through data or control edges and inside the functions it calls: a `println` statement of a `sync` function is refused at the `println`, not left out of the model. `onnx.export_model` applies the same check.

**A program without an `export_model` call: `main` is the model.** A program that only computes is exported by the same pass composition, with nothing written for the export in the program (not even `import onnx`: naming the pass loads the module):

    camel model.cml std::macro std::opt 'onnx::export(out="model.onnx")'
    camel model.cml --arg p=weights std::macro std::opt 'onnx::export(out="model.onnx")'

When no `export_model` call exists anywhere in the program, the pass translates the entry graph itself, that is the computation of `main`:

- **Inputs**: one per parameter the entry graph still has, `with` parameters first, named like the parameter. A tensor parameter must declare its element type and shape (`Tensor<f32, const [-1, 3]>`); a negative extent is a dynamic axis (symbolic size `<name>_dim<axis>`). A parameter declared `Tensor` without them is refused, naming the parameter. A number is a rank-0 input. A struct or tuple gives one input per leaf, named by its path (`p_fc1_w`, `t_0`). A `main` without parameters gives a model without inputs (a constant model).
- **Weights**: a parameter given a value with `--arg` at export is a constant of the graph and becomes an initializer, named after the parameter or, for a struct, after its fields (`w`, `b`). So the usual split is: data as ordinary parameters (inputs), weights as `with` parameters, which are initializers when their values are supplied at export and inputs when they are not. A training step written as `main` (weights in as `with` parameters, updated weights in the result) is exported without values: the weights are inputs and outputs.
- **Outputs**: the value `main` returns; a tuple or struct is expanded into one output per leaf (`output`, or `output_0`, `output_1_w`, ...).
- **Where it is written**: the parameter `out`, which is required here.
- **Refusal**: the pass translates what it can and refuses where it cannot, with `TranslationRefused` at the construct: an effect (`println`, a file read), an unexpanded macro (run `std::macro`), a call no simplification removes, an operator without a lowering. There is no separate check that the program is "pure"; a program that does more than compute exports one of its functions with `onnx.export_model` instead, and the refusal says so. The pass inlines the call of `main` itself, so it also works without `std::opt` before it; `std::macro` is still needed for programs that use `grad` or `value_and_grad`.
- A program that does contain an `export_model` call is never translated this way, wherever the call is: if the call is not visible in the entry graph (nothing was inlined), the refusal names `std::opt`.

To check an exported model against the program itself, run the program with the same values and compare: `camel model.cml --arg x=x.npy --arg p=weights -o out` writes what `main` returns in files a NumPy script reads.

Running the program without passes (`camel m.cml`) keeps working: `export_model` then expands macros and simplifies the function itself. What it accepts and the pass refuses are programs whose export request or captured values only exist at run time (random initial weights, a path from `get_env`).

### Warnings

A warning (a `[Warn]` diagnostic) does not stop a program. The table lists the compiler's warnings about effects and the warnings of the pipeline; for the others (`InvalidEscapeSequence`, `ExitInUnusedBranch`, `MatchWithoutDefault`) see [diagnostics.md](diagnostics.md).

| Name | Code | Where | Meaning |
|---|---|---|---|
| `SemanticDiag::EffectNotExecuted` | `0x03100004` | the call, assignment or branch | It has side effects but nothing depends on it and it is not in a sync block, so it never runs. Legal (evaluation is driven by dependencies); reported so that a forgotten `sync` is seen. One warning per dropped chain (`println(log(1))` reports the `println`). Never reported inside a sync block |
| `SemanticDiag::IgnoredSideEffect` | `0x03100002` | the call or branch | It writes a variable (a `var` argument, or a captured variable) and is neither waited nor in a sync block: it is not ordered after earlier uses of the variable |
| `SemanticDiag::VarParamInAsyncFunction` | `0x03100001` | the parameter | A `var` parameter in a function without the `sync` modifier |
| `SemanticDiag::CapturedWriteInAsyncFunction` | `0x03100003` | the function | The function writes a captured variable and has no `sync` modifier |
| `RuntimeDiag::PassesNeverRun` | `0x0410000F` | no source position (a command-line matter) | Passes are written after a pass that consumes the graph; they will not run. Printed with the program's warnings (the last element of the stderr array in JSON mode) |
| `RuntimeDiag::ConstantExpressionFaults` | `0x04100015` | the expression | `std::opt::fold` (also inside `std::opt`) met an expression whose operands are all constants and whose evaluation faults (`1 / 0`). It is not folded: the program behaves the same with and without the pass, and the fault is reported with its position if the run reaches the expression. Printed on stderr when the pass has run, once per expression, before the next pass |

What counts as an effect for `EffectNotExecuted`: an operator that is neither pure nor state-only in the operator-traits registry (the same set the effect trace records), a call of a function declared `sync func` or with a `var` parameter, a call of a function whose own result depends on an effect, and a branch with such an arm. A call through a function value is only recognised by its type (`sync` modifier, `var` parameter).

- **A program that compiles without an error** prints its warnings on **stderr**, once, after compilation and before the first pass runs, at the default log level (no `-v` needed). In the default `text` format a warning is written like an error, starting with `warning[SemanticDiag::IgnoredSideEffect]: ...`, with the source line and `help:`; in the `short` format, one line per warning: `(module)file:line:col: [Warn]: text suggestion (name=SemanticDiag::IgnoredSideEffect, code=0x03100002)` (a warning without a source position is written `():?:?: [Warn]: ...`). With `--output-format json`, one JSON array on stderr with the same objects as the error array below (`"severity":"Warn"`). Nothing is written when there is no warning (no empty array).
- **stdout is the same in both modes as without warnings**: the program's output, and the diagnostics array only when there is an error. Tools that parse stdout are not affected.
- **A module that has an error** shows its warnings with the errors on stdout, warnings first (in the same array in JSON mode); they are not repeated on stderr. `EffectNotExecuted` is not reported when the module has an error.
- `camel check` prints them the same way. `--timeit`, `camel inspect`, `camel-cpp` and the debugger do not print warnings.

### JSON diagnostics (`--output-format json`)

Errors are printed on stdout, after any program output, as a JSON array of objects (`[` and `]` each on a line of their own, one object per line):

```json
[
{"range":{"start":{"line":6,"character":13},"end":{"line":6,"character":14}},"severity":"Error","code":"0x04000001","source":"Camel","message":"Attempted division by zero.","data":{"stage":"run","name":"RuntimeDiag::DivisionByZero","moduleName":"m","modulePath":"m.cml","suggestion":"..."}}
]
```

- `range` is 0-based; `data.modulePath` is the file. A diagnostic without a source position (an unknown pass name, passes that never run) has an all-zero range and an empty `modulePath`.
- `data.name` is the stable diagnostic name; `code` is its numeric form.
- Every graph a pass commits (and every graph the front end encodes) is validated structurally, in release builds as well: node, slot and edge references in range, inputs and outputs mirroring each other, `BRCH`/`JOIN` arm counts, a `GATE` with a value and a control input, a direct call passing as many arguments as its callee takes. A violation is a `RuntimeDiag::PassInternalError` naming the pass that committed the graph, before anything runs.
- An exception that escapes a pass without being a diagnostic (graph validation when a rewrite is committed, a cycle met while a scheduler builds its execution order) is reported as `RuntimeDiag::PassInternalError` (`0x0400000C`) with stage `pass:<name>`, the exception text in the message, no source position, and exit code 1. It marks a defect of the compiler, not a refusal of the program. The stage names the pass even when it is a scheduling pass, so it does not tell whether part of the program had already run. An exception outside any pass is reported as `InternalDiag::UnknownInternalError` with stage `internal`.
- `data.stage` tells where in the pipeline the diagnostic was raised: `parse`, `typecheck`, `compile` (see "Resource limits"), `pass:<name>` for a rewrite or translation pass (including an unknown pass name), `run` for a scheduling pass (`std::nvm`, `std::fvm`, `std::jit`, `std::tf`), or `internal`.

### Effect trace (`CAMEL_EFFECT_TRACE`)

Setting the environment variable `CAMEL_EFFECT_TRACE=<file>` makes the process append one JSON line per executed effectful operator (any operator not registered as pure or as state-only in the operator-traits registry: `println`, file I/O, `py_*`, assignment `:op/assn*`, including `:op/assn_append`, the statement `acc = acc->append<v>`, ...), in call order. Creating and reading a `var` variable (`:var/new`, `:var/get`) are state-only and emit no event; a store into one (`:op/assn*`) does:

```json
{"seq":0,"uri":":io/println","stage":"run","origin":{"file":"a.cml","line":12,"col":5},"payload_sha1":"...","payload":"string=hello|||"}
{"seq":1,"uri":":op/assn_l","stage":"run","origin":{"file":"a.cml","line":13,"col":7},"payload_sha1":"...","payload":"var i64=(1)|i64=5|||"}
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
| Growth of `std::opt::inline`, `std::opt::specialize`, `std::opt` (nodes copied by specializing and inlining; one budget for all rounds of `std::opt`) | 4096 nodes | `CAMEL_OPT_GROWTH_BUDGET=<nodes>` | nothing further is specialized or inlined; the remaining calls stay calls. Logged with `-v` |
| Size of a graph that `std::opt::inline` inlines into | 4096 nodes | no | that call is not inlined |
| Rounds of `std::opt` | 16 | no | the program is passed on as it is. Logged with `-v` |
| Type-only specializations of one recursive function (a tensor argument whose shape changes at every level of the recursion) | 8 | no | the call keeps its callee |
| Non-tail recursion, the same on every scheduler: `std::nvm`, `std::fvm`, `std::jit` (interpreted calls and calls compiled to native code count alike) and `std::tf` / `std::prl` (the count follows an element onto the worker thread that runs it) | 1000 open calls | `--max-recursion-depth <n>`, `CAMEL_MAX_RECURSION_DEPTH=<n>` (1 to 1000000) | the program stops with `RuntimeDiag::MaxRecursionDepthExceeded` (`0x04000004`, stage `run`, exit 1) at the call expression that would be open call 1001; what ran before it has run and its output is written. The message gives the limit (`Maximum recursion depth (1000) exceeded when evaluating graph '...'.`), the suggestion the option and the variable |
| Native stack used by recursion in code `std::jit` compiled to native code (a self-recursive function whose branch arms were inlined, e.g. after `std::opt::inline`) | the count of the row above, and the native stack of the thread: a native call is refused when less than a sixteenth of the stack (at least 128 KiB) is left | the option and variable of the row above; the size of the stack itself is the operating system's (`ulimit -s`) | Under any limit the option accepts, a native recursion either finishes or stops with `RuntimeDiag::MaxRecursionDepthExceeded` at the recursive call, as interpreted code does. Where the count is reached first the message is that of the row above (`camel f.cml std::opt::inline std::jit`: `depth(999)` runs, `depth(1000)` is the diagnostic). Where the stack fills first the message says so, as for the nested calls of `std::nvm` (`Maximum recursion depth (70103 open calls: the native stack of the thread, 8188 KiB, is full) exceeded ...`): with a limit of 1000000 and the default 8 MiB stack, a native recursion of `depth` runs to about 70000 levels (112 bytes of stack a level). Compiled code compares its stack pointer with the reserve before every native call; set `CAMEL_NATIVE_STACK_GUARD=0` and that check is off like the interpreters' |
| Native stack of a thread on `std::tf` | as in the next row: apart from the count, one more nested call is refused when fifteen sixteenths of the thread's stack are used | `ulimit -s` and the like (a worker thread's stack is at least 8 MiB: the process's stack limit on Unix and the executable's stack size on Windows where those are larger) | `MaxRecursionDepthExceeded` at the call; the message then gives the number of open calls and the stack size. The default limit (1000) is far from reaching this on the default 8 MiB stack of Linux (measured below) |
| Native stack of a thread, on every back end | the thread's stack (8 MiB for the main thread on Linux by default) | `ulimit -s` and the like | a call that would nest on a nearly full native stack (less than a sixteenth of it, at least 128 KiB, left on Unix; Windows reserves fifteen sixteenths for SEH fault unwinding) is refused with `MaxRecursionDepthExceeded` before the limit above is reached; the message says the native stack is full. This is what keeps a raised limit from ending in a segmentation fault. The stack bounds are read on Linux (glibc), Windows and macOS (Windows is also verified; macOS is implemented but has not been compiled or run); on another platform the limits cannot be raised above their defaults |
| Ahead-of-time evaluation of a `macro` function by `std::macro` | 64 evaluator frames (a self-recursion of 32 levels: `count(31)` is evaluated, `count(32)` is not) | `--max-macro-depth <n>`, `CAMEL_MAX_MACRO_DEPTH=<n>` (1 to 1000000) | the evaluation has failed: error `RuntimeDiag::MacroEvaluationFailed` at the call, the pipeline stops (see "What `std::macro` reports"). The evaluator nests on the native stack (about 630 bytes a frame), and is stopped the same way when the stack is nearly full |
| Nodes, edges and payload words of one graph (a node is a 16-byte record; every edge is one entry of the node's input table; what a node carries beyond its record, a callee, an operator, the positions of a constructed value, a branch table, takes 8-byte payload words) | 65535 edges, 65534 nodes and payload words (16-bit) | no | the pass that built the graph fails before anything runs with `RuntimeDiag::GraphTooLarge` (`0x04000013`, stage `pass:<name>`), located at the function the graph was compiled from (at one of its nodes for a graph a pass created), naming the graph, the quantity, the size it needs and the limit. In `onnx::export` and `onnx.export_model`, which inline without the size cap, it is a refusal at the `export_model` call with the same four facts |
| Graph space (the runtime graphs of the program; a function takes about 1 to 2 KB) | 256 MB in all, reserved by segments as the program needs them | `CAMEL_GRAPH_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted` (`0x04000014`, exit 1), located at the function whose graph was being encoded, naming the space, the usage, the limit and the variable. Stage `compile` when the program itself does not fit, `pass:<name>` when a pass made it grow |
| Permanent space (types and layouts; a function takes about 0.6 to 2 KB, and what a pass creates is never released) | 256 MB in all, by segments | `CAMEL_PERM_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`, as above, without a position unless the function being compiled is known |
| Meta space (constants and function values of the graphs being compiled; about 1.2 KB for every function, lambda or branch arm that owns one) | 256 MB in all, by segments | `CAMEL_META_SPACE_LIMIT_MB=<megabytes>` | `RuntimeDiag::MemoryExhausted`, stage `compile`, located at the function being compiled |
| Auto space (run-time values, garbage collected) | the memory of the host | no | `RuntimeDiag::MemoryExhausted` (stage `pass:<scheduler>`), saying that the host refused the allocation |

**What the recursion limit counts** is the same on every back end: each call of a function that is open and was not a tail call, whether it is written as a call, made through a function value, or made by a higher-order operator (`map`, `filter`, `reduce`, `foreach`) for an element. A call in tail position replaces its caller and is not counted (see `docs/technical/19_tail_call_optimization.md`), so tail recursion of any depth runs under any limit. The arms of an `if` are tail calls of the function they are written in. The one call the program does not make itself, the run-time's call of `main`, is not counted. So with a limit of L, for `func depth(n) = if n == 0 then 0 else 1 + depth(n - 1)` called from `main`, `depth(L - 1)` is the deepest call that runs, on `std::nvm`, `std::fvm`, `std::jit` (interpreted and compiled to native code) and `std::tf`: `depth(999)` with the default. Passes that change which calls exist (`std::opt::inline`) change what is counted, as they change the graph.

**Setting it.** `--max-recursion-depth <n>` on the command line, else the environment variable `CAMEL_MAX_RECURSION_DEPTH=<n>`; `n` is an integer from 1 to 1000000. Anything else (0, text, a larger number) is refused on standard error (`Invalid --max-recursion-depth value: '0'. Expected an integer from 1 to 1000000.`) with exit code 1 before the program is compiled. There is no `.opencmlrc` key. Without option and variable every scheduler stops at 1000, and the parallel scheduler counts like the others (design principles 2.10).

**What a raised limit costs** (measured on Linux x86-64, release build, a function with three slots):

| Back end | Where a call lives | Per open call | Deepest recursion on an 8 MiB stack |
|---|---|---|---|
| `std::nvm` | frame in the frame pool (heap), and one activation of the interpreter loop on the native stack | about 545 bytes of native stack, about 100 bytes of frame | 14441 calls; then the diagnostic (without the stack check the process crashed at 15399) |
| `std::fvm`, a direct call | frame in the frame pool and an entry of the interpreter's own call stack, both on the heap; no native stack | about 100 bytes | the limit (999000 ran under a limit of 1000000) |
| `std::fvm`, a call through a function value or an element call of a higher-order operator | as above, plus a nested activation of the interpreter loop on the native stack | about 1.7 KiB of native stack for the nested activation | 4587 nested activations; then the diagnostic (crash at 4890 without the check) |
| `std::tf` | frame in a per-graph pool (heap), and the evaluator on the native stack | about 520 bytes of native stack | 14875 calls; then the diagnostic (crash at 15858 without the check) |

**The margin of the default limit** (measured on Linux x86-64, release build): the smallest `ulimit -s` with which the deepest recursion the default allows still runs (direct `depth(999)`, through a function value `step(499)`, inside a `map` element `depth(998)`) is 768 KiB on `std::nvm` and `std::tf` for all three; on `std::fvm` and `std::jit` (also `std::opt::inline std::jit`) it is 192 KiB for the direct recursion and inside `map`, and 1024 KiB through a function value. The default 8 MiB stack therefore leaves a factor of 8 or more; with a smaller stack the result is the "native stack is full" diagnostic above, not a crash. On Windows the default stack of a thread is 1 MiB, with which the first run of the tests there met that diagnostic under the default limit on `std::nvm` (and on `std::fvm` through a function value) and a stack overflow on `std::tf`; the tools are therefore linked with a 32 MiB stack (`/STACK`, `tools/libcamel_tool.cmake`), which on Windows is also the stack of every thread the process creates. The worker threads of `std::tf` are created with a stated stack size, 8 MiB at least, on every platform: a thread created without one would have 512 KiB on macOS, and under a host that loads `libcamel` on Windows (`python.exe`) the 1 MiB of that host (`include/camel/utils/big_stack_thread.h`, `cmake/TaskflowWorkerStack.cmake`). The macOS side of this has not been run. A host that loads `libcamel` itself (the Python package) runs with the stack of that host.

The frame pool of `std::nvm` and `std::fvm` is sized with the limit: 4 KiB per call that may be open (about 4 MiB for the default), at most 256 MiB; it is reserved once and only the part in use is touched. A recursion of functions with very large frames can fill it before the count is reached; that is the same located diagnostic, saying the frame pool is full. Running time is not affected by the limit. `CAMEL_NATIVE_STACK_GUARD=0` switches the native-stack check off; it exists to measure where the stack really ends (the crash points above) and is not for normal use.

The three space limits bound the size of the compiled program, not of one graph: they are totals that only guard against a compiler that does not stop allocating (a pass whose unrolling or inlining never ends is stopped by the graph or permanent space filling up, and the time that takes grows faster than the limit, which is why the defaults are not larger). `CAMEL_GC_STATS=1` prints the occupancy of the three spaces at exit (`[camel] spaces: ...`: bytes in use, high-water mark, bytes reserved, segments, allocations, limit).

The other limits marked "no" are compile-time constants (`generic.h`, the inliner's configuration); there is no command-line option or `.opencmlrc` key for them (`.opencmlrc` only configures the pass list).

`std::opt` unrolls a recursion whose arguments are constants one level per round (`fib(15)` would become its whole call tree); the growth budget is what stops it. `onnx.export_model` simplifies the function it exports without the growth budget and the inline size cap, because an export has to reach straight-line code; a pipeline that exports through `std::opt onnx::export` is held to them and may need `CAMEL_OPT_GROWTH_BUDGET` raised for a large model (the export pass then refuses with `TranslationRefused` rather than writing a partial model, and the refusal names the budget, its value and the variable).

---

### The collector (`--gc`, `CAMEL_GC`)

Run-time values (arrays, strings, tuples, structs, function values, maps, tensors) are reclaimed by a collector that runs only at safepoints: calls, the element calls of higher-order operators, pass boundaries. Two collectors exist; which one runs is chosen per process and changes neither what a program prints nor which programs are accepted.

| Collector | How | Choose it when |
|---|---|---|
| `mark-sweep` | Every object stays where it was allocated. Small objects come from a free list in the elder space, large ones (above 4 KB) are separate blocks; a major collection marks what is reachable and sweeps the rest | an object must keep its address, for example while debugging the collector itself; the default of releases up to v0.2.8 |
| `generational` (default) | Small objects are allocated by bumping a pointer in a 4 MB birth space. A minor collection copies the survivors (to a survivor space, to the elder space after four survivals) and frees everything else at once; major collections mark and sweep the elder and large-object spaces as above | the program makes many short-lived small values: allocation is cheaper and the dead cost nothing. A program that builds a large structure and keeps it pays for copying it up to four times |

`--gc mark-sweep` on the command line, or `CAMEL_GC=mark-sweep` in the environment, selects the other collector; `--gc` wins. `CAMEL_GC_ENABLE_YOUNG_COPYING=1`, the older spelling of `CAMEL_GC=generational`, no longer changes anything. Anything else than the two names is refused (`--gc`) or ignored with a line on standard error (`CAMEL_GC`). The collector applies to `std::nvm`, `std::fvm`, `std::tf` / `std::prl` and to the interpreted part of `std::jit`; with `generational`, compiled code reads an object constant (a string literal, a constant array) from the static area each time instead of embedding its address, so `std::jit` compiles such functions under both collectors.

Related settings, all environment variables:

| Variable | Meaning |
|---|---|
| `CAMEL_GC_STATS=1` (`2`) | One line on standard error at exit (`[camel] gc: ...`): minor and major collections, objects moved and promoted, megabytes allocated per space, what the elder and large-object spaces hold and have reserved, the time the program was stopped (`minor_pause_ms`, `major_pause_ms`) and the longest single stop (`..._max_ms`). `2` adds a line per major collection |
| `CAMEL_TYPE_STATS=1` | One line on standard error at exit (`[camel] types: ...`): for each kind of type object in the permanent space (`tuple`, `function`, `array`, `struct`, ...), `objects/bytes` allocated by the core type system, then how many types the type table holds and how many constructions it answered with an existing object (`interned=`, `intern_hits=`). It tells which kind of type a program or a pass keeps allocating; module-defined types are not counted |
| `CAMEL_GC_HEAP_LIMIT_MB=<n>` | Limit on what the elder and large-object spaces hold together. Without it the heap is bounded by the memory of the machine only. An allocation that crosses the limit asks for a major collection at the next safepoint; if what is alive still exceeds the limit after it, or the program allocates past one and a half times the limit before it reaches a safepoint, or one value is larger than the limit, the program stops with `RuntimeDiag::MemoryExhausted`, which names the space (`auto`, run-time values), the request, the usage, the limit and this variable |
| `CAMEL_GC_GROWTH_PERCENT=<n>`, `CAMEL_GC_LARGE_BUDGET_MB=<n>` | Pacing of major collections: allocation allowed between two of them, in percent of what the last one left alive (default 100), and its floor for the large-object space (default 4 MB; the floor of the elder space is its first segment, 32 MB) |
| `CAMEL_GC_PROMOTION_AGE=<n>` | Minor collections a value survives before it is promoted (1 to 15, default 4) |
| `CAMEL_GC_POISON=1`, `CAMEL_GC_VERIFY`, `CAMEL_GC_STRESS_*` | Diagnostic modes for people who write operators in C++: `docs/technical/36_gc_handle_rules.md`, `docs/technical/24_gc_verification_guide.md` |

## Information Display

- `-V`, `--version`: Show version information  
- `-h`, `--help`: Show help information  
- `-d`, `--docs`: Show documentation of package or module  
- `-a`, `--about`: Show copyright and project information  
- `-z`, `--zen`: Show the Zen of Camel
- `--explain <name or code>`: Describe a diagnostic (see "Diagnostics")

---

## check: Code Health Checker

`-i` and `-s` use the frontend directly, without initializing managed runtime heaps, an execution context or built-in modules. Diagnostic formats, positions and error limits remain the same. Plain `check` still performs semantic checks.

    camel check [options] <cml file or directory>

- `-i`, `--lexical-only`: Lexical analysis only: only lexical errors (an illegal character, an unclosed string) are reported, nothing is parsed  
- `-s`, `--syntax-only`: Lexical and syntax analysis only: stops once the file is read into a syntax tree; names, types and imports are not checked  
- `-O`, `--output-format <text|short|json>`: Format of diagnostics (default: `text`)  
- `-N`, `--max-warning <num>`: Warnings shown; `--color`, `--max-errors` and the `-W...` options work as for a run (see "Diagnostics")  
- `-c`, `--config <file>`: Path to rule definition file  
- `-e`, `--ignore`: Ignore definition files  
- `-o`, `--output <file>`: Output file (default: console)  
- Available global options: `-v` / `-vv` / `-vvv` / `-vvvv`, `--verbose`, `--log-level`, `--log-preset`, `--log-include`

Errors are printed on standard output and warnings on standard error, in the same formats as for a run. `camel check` does not run the program. Its exit code is 1 when it found an error and 0 when it found none (warnings alone do not count).

---

## inspect: Intermediate State Viewer

    camel inspect [options] <cml file>

- `-t`, `-T`, `--tok`, `--token-stream`: Print lexical tokens  
- `-s`, `-S`, `--cst`, `--concrete-syntax-tree`: Print Concrete Syntax Tree (CST)  
  Tokens are printed one per line: index, `line:column`, what precedes the token (`n` a line break, `s` white space or a comment, `-` nothing), kind, text; comments are tokens too. The tree names its nodes after the productions of the grammar; a level that only passes on leaves no node (`a + b` is an `AdditiveExpr` directly over two `RefData`).
- `-a`, `-A`, `--ast`, `--abstract-syntax-tree`: Print Abstract Syntax Tree (AST)  
- `-c`, `-C`, `--gct`, `--graph-construct-tree`: Print Graph Construction Tree (GCT)  
- `-g`, `-G`, `--gir`, `--graph-ir`: Print Graph Intermediate Representation (GIR)  
- `--tns`, `--topo-node-seq`: Print topologically sorted node sequence  
- `--ops`, `--operators`: List the operators of the built-in module and of every module the program imports, one line `OPERATOR {json}` per overload: module, name, URI, parameter counts, declared purity (`pure`, `effectful`, `state`, `undeclared`) and signature, e.g. `OPERATOR {"module":"<builtin>","name":"__add__","uri":":op/add_l","with":0,"norm":2,"purity":"pure","signature":"(i64, i64) => i64"}`  
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

- **camel-cpp**: Translate a `.cml` to C++ and build an executable in one step.
  - Usage: `camel-cpp <file.cml> [-o <executable>] [passes before std::cpp ...]`
  - Equivalent to: `camel <file.cml> std::cpp > <name>.cpp`, then `$CXX` (default `c++`, `clang++` on Windows) with `-std=c++20 -O2 -ffp-contract=off -I$CAMEL_HOME/include`. The program does not depend on the Camel runtime; when it uses a module operator with no standalone translation, it is linked with `libcamel`. See [cpp_export_build.md](cpp_export_build.md).

Other tools: `camel-codegen`, `camel-pr` (the profiler), `camel-db` (the debugger), `camel-ls` (the language server). All are built under `build/tools/` and installed with `camel` into `out/latest/bin/`.

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

# Translate to C++ and build an executable (one step)
camel-cpp test/cases/linear/nvm/fib.cml -o fib

# Format code (use camel-format, not camel format)
camel-format -i src/xxx.cml

# Syntax check only and output in JSON
camel check -s -O json src/

# Print intermediate representation
camel inspect --gir main.cml

# View Zen of Camel
camel --zen
```

