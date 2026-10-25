<div align="center">

<img src="assets/logo_text.png" alt="Camel" width="560">

### The program is the graph.

**Camel** is a graph-native general-purpose programming language, with AI as its primary application.

[![CI](https://github.com/OpenCML/Camel/actions/workflows/ci.yml/badge.svg)](https://github.com/OpenCML/Camel/actions/workflows/ci.yml)
[![Version](https://img.shields.io/github/v/tag/OpenCML/Camel?label=version&color=blue)](https://github.com/OpenCML/Camel/tags)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey.svg)](docs/setup.en.md)
[![VS Code](https://img.shields.io/visual-studio-marketplace/v/Kogler7.open-cml-extension?label=VS%20Code&logo=visualstudiocode)](https://marketplace.visualstudio.com/items?itemName=Kogler7.open-cml-extension)
[![Stars](https://img.shields.io/github/stars/OpenCML/Camel?style=social)](https://github.com/OpenCML/Camel/stargazers)

[Why Camel](#why-camel) ·
[A taste](#a-taste) ·
[Quick start](#quick-start) ·
[Tutorial](docs/tutorial/tutorial.en.md) ·
[Documentation](#documentation) ·
[Roadmap](ROADMAP.md) ·
[中文](docs/README.cn.md)

</div>

---

## Why Camel

Most languages write a program as instructions for a machine to carry out one step at a time. Yet what compilers, parallel runtimes, automatic differentiation and GPU back ends actually need is a graph: what is to be computed, and what depends on what. So the graph has to be recovered from the code after the fact, by tracing, capturing and analysis. The recovered graph breaks apart, loses what the side effects meant, and the transformations applied to it become a black box the user cannot touch.

In Camel **the program is the graph**. What you write is the intent of the computation and its dependencies; order is written only where you need it. From that:

- **The compiler gets exactly what you wrote**: nothing is traced, the graph does not break, and computations that do not depend on each other are free to run in parallel.
- **Transformations are ordinary functions**: optimizing, differentiating, moving to a GPU and exporting are passes over the graph. You can look at them, swap them, and write your own in Camel.
- **Knowledge about optimization is code you can check**: an algebraic law is a rule, and `check` tells you whether it holds.
- **One program, many ways to run it**: interpreted, JIT-compiled, on a GPU, or exported to ONNX or C++, with the same meaning.

These properties hold for every program where computation matters more than control flow. Training and deploying models is the most pressing of them, which is why AI is Camel's primary application, though not its only one. More in [Why Camel](docs/why.en.md).

## Highlights

| | |
|---|---|
| 🧭 **Graph-native** | Every construct maps to a fixed piece of the graph. Evaluation follows dependencies; independent work runs in parallel without being asked. |
| 🔁 **Passes are functions** | Optimization, differentiation, export and execution are graph-to-graph passes you compose on the command line, or write yourself in Camel. |
| 📐 **Rules you can verify** | Write an algebraic law once, generic over types; the rewrite engine applies it, and `check` proves, refutes or samples it. |
| ∂ **Differentiable by construction** | `grad(f)` rewrites the graph of `f` into the graph of its derivative, at compile time, so every later pass optimizes it too. |
| 🧮 **Arrays with shapes** | `f32[n, k]` is one type for every rank; dimension names are checked across a call, and memory is updated in place whenever nothing else can see it. |
| 🚀 **Many back ends** | A node VM, a bytecode VM, a JIT, a parallel scheduler, GPU kernels, ONNX and standalone C++ — one meaning everywhere. |
| 🐍 **Plays well with Python** | Call Camel modules from Python with zero-copy arrays, and get Camel's diagnostics as Python exceptions with locations. |
| 🛠️ **Real tooling** | A language server, a formatter, a debugger that steps through passes, and diagnostics that come with machine-applicable fixes. |

## A taste

Fit a line by gradient descent. `grad(loss)` is not a library call evaluated step by step: it is a graph rewrite, and the derivative it builds is an ordinary graph the optimizer and the JIT see.

```camel
module fit

import { grad } from autodiff

with <w: float>
func loss(x: float, y: float): float {
    let e = w * x - y
    return e * e
}

func main(): int sync {
    let fitted = loop w = 0.0 for i in 0..100 do w - 0.01 * grad(loss)<w>(3.0, 6.0)
    println(fitted)                  // close to 2.0
    return 0
}
```

The words after the file name are the passes to run, in order:

```bash
camel fit.cml                        # run on the node VM
camel fit.cml std::gir               # print the graph (Graphviz DOT) instead of running
camel fit.cml std::macro std::opt std::jit   # expand grad, optimize, run with the JIT
camel fit.cml std::opt::saturate std::gir    # keep every equivalent form and draw them side by side
```

Optimizations are written in Camel too. A law is stated once, for every type that satisfies its condition:

<!-- camel-snippet: fragment: a rule declaration from the standard library's style, shown on its own -->

```camel
rule add_zero::<T>(x: T) when is_integer::<T>() { x + 0 => x }
```

## The language and the graph

| In the source | On the graph | What follows |
|---|---|---|
| An expression, an operator call | Nodes and data edges | Evaluation follows dependencies; independent nodes may run in any order or in parallel |
| A function | A subgraph; a call is a node | A function's behaviour is decided by the function itself |
| A `sync` block | Order between the effects it contains | Effects run in the order written |
| `if`, `match`, `a ?? b`, `a !! b` | A branch | Errors and nulls are values, handled by branches |
| `loop … do …` | A loop node with its state | Iteration is an expression whose value is the last state |
| A generic function | A subgraph template, instantiated per set of concrete types | Only concrete types are on the graph |
| A pass | A function from a graph to a graph | An open pipeline |

The rules behind this table are in the [design principles](docs/design-principles.en.md).

## Quick start

### Build from source

You need Clang 19+, CMake 3.25+, Ninja, Conan 2.10+ and Node.js 18+. [docs/setup.en.md](docs/setup.en.md) lists the toolchain per platform.

```bash
git clone https://github.com/OpenCML/Camel.git && cd Camel
npm install          # npm packages and the C++ dependencies (needs network access)
npm run build        # Release build into out/latest
```

Put `out/latest/bin` on your `PATH` and set `CAMEL_HOME` to `out/latest`. CI builds and tests Linux x64 and Windows x64.

### Hello, Camel

```camel
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel hello.cml
```

```text
Hello, Camel!
```

### Editor support

Install [OpenCML-Ext](https://marketplace.visualstudio.com/items?itemName=Kogler7.open-cml-extension) for VS Code: highlighting, language services, an interactive graph browser, and a debugger that can pause between passes. Point `opencmlLanguageServer.camelPath` at your `camel` executable. See the [extension README](https://github.com/OpenCML/OpenCML-Ext#readme) and the [debugging guide](docs/debugging.en.md).

### Python

```bash
npm run pypi:rollup  # builds the camel-lang wheel from this tree
```

See [docs/pypi-packaging.md](docs/pypi-packaging.md) and the [Python bindings](docs/technical/57_python_bindings.md).

## Documentation

| | |
|---|---|
| 📘 [Tutorial](docs/tutorial/tutorial.en.md) | From the first program to rules, passes, gradients and export |
| 💡 [Why Camel](docs/why.en.md) | What problem Camel solves and why the program should be the graph |
| 📖 [Language reference](docs/language/en/README.md) | Every construct, precisely |
| 📚 [Standard library](docs/stdlib-reference.md) | Every module and function |
| 🧪 [Examples](examples/README.md) | Short task-oriented programs: files, JSON, a CLI tool, parallel `map`, a training step |
| ⌨️ [Command line](docs/cli.md) · [Diagnostics](docs/diagnostics.md) | Options, passes, and `camel --explain <name>` |
| 🤖 [Camel for AI](docs/reference/camel-for-ai.md) · [`llms.txt`](llms.txt) | What a model needs to write Camel |
| 🧱 [Design principles](docs/design-principles.en.md) | The rules the language is built on ([中文, authoritative](docs/design-principles.cn.md)) |
| ⚙️ [Technical docs](docs/technical/README.md) | The pipeline, the graph IR, the runtime |

## Status

Camel is under active development and has not reached 1.0. The language changes without compatibility shims: when a spelling is replaced, the old one is simply gone. Release notes are in [changes.log](changes.log), and the plan is in the [roadmap](ROADMAP.md).

## Contributing

Issues and pull requests are welcome. Start with the [contributing guide](CONTRIBUTING.md), and please follow the [code of conduct](CODE_OF_CONDUCT.md). Security issues go through the [security policy](SECURITY.md).

## License

Camel is released under the [MIT License](LICENSE).
