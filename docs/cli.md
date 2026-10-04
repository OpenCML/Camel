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

