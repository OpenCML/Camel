# GN

GN is a statically typed language for tensor programs whose source is the dataflow graph. Every function is one graph, and a program is never run as host code in order to capture a graph.

This package holds the core of GN's compiler and runtime, the part the study uses:
- the front end;
- the graph construction, with its type and effect checks;
- the optimiser and macro expansion;
- the node-by-node interpreter, the bytecode virtual machine with its just-in-time compiler, and the parallel scheduler;
- the ONNX exporter;
- the standard library.

The GPU back end, the C++ translator, the Python embedding and binding, the formatter and the editor tooling are not part of it.

## Layout

| Path | Contents |
|---|---|
| `src/`, `include/` | compiler and runtime: `parse` (lexer, parser, syntax tree), `compile` (the intermediate tree and the typed graph, with the type and effect diagnostics), `passes` (optimiser, schedulers, graph dumps), `execute`, `runtime`, `core` |
| `modules/` | native modules of the standard library (`io`, `math`, `random`, `tensor`, `nn`, `autodiff`, `transforms`, `onnx`, ...) |
| `stdlib/` | the standard library written in GN |
| `diagnostics/registry.tsv` | every diagnostic, with its name, message and help |
| `test/` | test programs (`cases/`), test plans (`plans/`) and the runner |
| `docs/` | the design principles and the language reference (`docs/language/`) |
| `tools/gn-cli` | the command-line driver |

## Building

Requirements:
- Linux x86-64;
- clang 19 or later;
- CMake 3.25 or later and Ninja;
- Conan 2;
- Node.js 20 or later.

```sh
node scripts/build.js
```

The script runs `conan install`, configures with CMake and builds the Release configuration into `build/`. It then installs the driver, the runtime library and the standard library into `out/latest/`. Run the driver as `out/latest/bin/gn`.

## Running

```sh
gn prog.gn                                   # the default pipeline: the node-by-node interpreter
gn --passes std::macro,std::nvm prog.gn      # macro expansion, then the node-by-node interpreter
gn --passes std::macro,std::fvm prog.gn      # ... the bytecode virtual machine
gn --passes std::macro,std::jit prog.gn      # ... the bytecode virtual machine with its JIT
gn --passes std::macro,std::opt,std::fvm prog.gn
gn --passes std::macro,std::opt,std::jit prog.gn
gn --passes 'std::macro,std::opt,onnx::export out=model.onnx' prog.gn   # export main to ONNX
gn inspect --gir prog.gn                     # the graph of the program, as Graphviz
gn --explain EffectNotExecuted               # what a diagnostic means
```

A pipeline is a comma-separated list of passes, each optionally followed by `key=value` parameters, for example `std::opt safe_fp=true`. `gn --help` lists the options. The first six lines above are the six pipelines of the study.

## Tests

```sh
node scripts/test.js test/plans --tier fast
```

The runner reads the plans in `test/plans/`. Each plan names its programs in `test/cases/` and what each must print or report, and the runner runs every plan under the back ends it names. A few plans check timing margins and can fail on a heavily loaded machine.

On the build used for the study, the fast tier runs 7,787 tests: 7,710 pass, 30 are known failures that their plans record, and 21 are skipped. Of the 26 that fail, 22 are open defects of this version, and 4 are timing-margin checks that failed under load.
