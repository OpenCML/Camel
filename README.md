# Camel: A Graph-Native Programming Language

[中文简体](docs/README.cn.md) | English

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/GetDefaultLogger()o_text.png' width=800>
</p>

Camel is a **graph-native general-purpose programming language**; AI is its primary application domain.

A Camel program expresses computational intent only: what is to be computed, and what depends on what. The graph is not captured or recovered from code written in some other language; what the source says *is* the graph. Every element of the language corresponds directly to a piece of that graph, and compiling, optimizing, differentiating, exporting and running a program are all transformations of the graph, called passes, which you compose on the command line.

## The language and the graph

| In the source | On the graph | What follows |
|---|---|---|
| An expression, an operator call | Nodes and data edges | Evaluation follows dependencies; nodes that do not depend on each other may run in any order or in parallel |
| A function | A subgraph; a call is a node | A function's behaviour is decided by the function itself |
| A `sync` block | Control edges between adjacent expressions | Statements run in the order written |
| `wait e` | Control edges around one expression | Local order where a variable is modified |
| `if`, `match`, `a ?? b`, `a !! b` | A branch | Errors and nulls are values, handled by branches |
| A generic function | A subgraph template, instantiated per set of concrete types | Only concrete types are on the graph |
| A pass | A program that turns one graph into another, or consumes it | An open pipeline |

The rules behind this table are in the [design principles](docs/design-principles.en.md).

## A first look

```camel
module stats

func mean(xs: float[]): float {
    return xs->reduce<(acc: float, x: float): float => acc + x, 0.0> / float(len(xs))
}

func parse_all(texts: string[]): int[] {
    return texts->map<(t: string): int => parse_int(t) !! 0>   // a text that is not a number counts as 0
}

func main(): int sync {
    let xs = parse_all(['3', '4', 'five', '6'])
    println(xs)
    println('mean = {:.2f}'->format<mean(xs->map<(x: int): float => float(x)>)>)
    return 0
}
```

```text
[3, 4, 0, 6]
mean = 3.25
```

- `->` pipes a value into a function; the function given to `map` and `reduce` is written in angle brackets because it configures the operator, while the array is the data that flows through it.
- `parse_int` returns an `int!`, a value that is either an `int` or an error; `!!` supplies a substitute. There are no exceptions.
- The body of `main` is a `sync` block, so its statements run in the order written. Elsewhere evaluation follows dependencies: outside a sync block, an effect whose result nothing uses does not run (the compiler warns about it).
- There is no loop syntax yet; repetition is recursion (tail calls take no stack) and the higher-order operators `map`, `filter`, `reduce`, `foreach`.

## One source, different passes

What comes after the file name on the command line is the sequence of passes to run:

```bash
camel prog.cml                          # run on the node VM (the default)
camel prog.cml std::fvm                 # run on the bytecode VM
camel prog.cml std::jit                 # run with JIT compilation
camel prog.cml std::prl                 # run with parallel scheduling
camel prog.cml std::opt std::fvm        # optimize, then run
camel prog.cml std::gir                # print the graph (Graphviz DOT) instead of running
camel prog.cml std::macro std::opt::simplify onnx::export    # translate `main` to an ONNX model
```

Automatic differentiation (`grad` from the `autodiff` module) and ONNX export are libraries built on the same pass mechanism. The [tutorial](docs/tutorial/tutorial.en.md) walks through looking at the graph, changing the pipeline, exporting a small model and taking a gradient.

## Getting started

> [!WARNING]
> Camel is a work in progress. The language and the command line still change, and parts of the grammar are reserved but not implemented; the [language reference](docs/language/en/README.md) says which.

### Build

Camel is built from source; see [docs/setup.en.md](docs/setup.en.md) for the toolchain.

```bash
npm install
npm run build        # the executable is out/latest/bin/camel
```

Put `out/latest/bin` on your `PATH` (and set `CAMEL_HOME` to `out/latest`). A Python wheel named `camel-lang` can be built from the same tree; see [docs/pypi-packaging.md](docs/pypi-packaging.md).

### VS Code

Install [OpenCML-Ext from the VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=Kogler7.open-cml-extension) for syntax highlighting, language services, interactive GIR browsing, and debugging. Configure `opencmlLanguageServer.camelPath` to point to your built `camel` executable; the extension discovers `camel-ls` and `camel-db` alongside it. Recent runtime builds also support stepping through compiler passes, inspecting the paused GIR, and viewing memory. See the [extension README](https://github.com/OpenCML/OpenCML-Ext#readme) for setup and the [debugging guide](docs/debugging.en.md) for runtime capabilities.

### Hello, Camel

```camel
// hello.cml
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

## Learn more

- [Tutorial](docs/tutorial/tutorial.en.md) ([中文](docs/tutorial/tutorial.cn.md)): from the first program to graphs, passes, export and gradients, with runnable examples
- [Examples](examples/README.md): short task-oriented programs (JSON, files, maps, sorting, a command-line tool, parallel `map`, a simulation, a training step)
- [Language reference](docs/language/en/README.md) ([中文](docs/language/README.md))
- [Command line](docs/cli.md) and the [catalog of diagnostics](docs/diagnostics.md) (`camel --explain <name>`)
- [Debugging](docs/debugging.en.md): breakpoints, stepping and variables in an editor through `camel-db --dap`, and what they mean in a graph language
- [Design principles](docs/design-principles.en.md) ([中文, authoritative](docs/design-principles.cn.md)): what Camel is and why
- [Technical documentation](docs/technical/README.md): the compilation pipeline, the graph IR, the runtime
- [Build from source](docs/setup.en.md) and [CMake build options](docs/build-options.en.md)
- [Python integration](modules/python/README.md): embedding Python through the `python` module
- [Roadmap](ROADMAP.md) (Chinese)

## Contributing

- [Contributing Guide](CONTRIBUTING.md)
- [Code of Conduct](CODE_OF_CONDUCT.md)
- [Security Policy](SECURITY.md)
- [Issue Tracker](https://github.com/OpenCML/Camel/issues)

## License

Code written by this project's contributors is licensed under the [MIT License](LICENSE).

