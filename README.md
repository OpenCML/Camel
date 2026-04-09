# Camel: A Graph-Native Programming Language for Unified AI Development

[中文简体](docs/README.cn.md) | English

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/GetDefaultLogger()o_text.png' width=800>
</p>

Camel is a **graph-native**, **multi-stage**, and **type-driven** domain-specific language (DSL) designed to bridge the gap between AI research and production deployment. It combines the elegance of **functional programming** with the expressiveness of **declarative programming**. It provides **born-async semantics** and highly **customizable graph manipulations**, enabling developers to write **high-level code** that compiles to **near-native performance**.

## 🚀 Why Camel?

The AI community already knows that computation graphs are the right model. PyTorch builds them dynamically at runtime; TensorFlow compiles them statically; JAX traces Python functions to reconstruct them. Every major framework ends up with a graph—because that's what the hardware and the math actually need.

The problem is that these graphs are **library artifacts built on top of a language that knows nothing about them**. Python remains the host: a dynamically typed, GIL-bound, sequentially-ordered language whose semantics work against the very structure these frameworks are trying to express. The result is a permanent tension—escape hatches like `torch.compile`, `@tf.function`, and `jax.jit` exist precisely to paper over the mismatch between what Python says and what the runtime needs to do.

Camel takes a different approach: **make the graph the program**. Every function compiles to a Graph IR whose nodes are operations and whose edges are data dependencies. There is no mismatch to paper over, because the language and the execution model speak the same language from the start.

This has concrete consequences for three problems that Python-based frameworks cannot fully solve at the language level:

1. **The Ordering Problem**
   In Python, every statement implies an order—even when no dependency exists. Frameworks recover parallelism after the fact, through tracing or graph construction APIs. In Camel, order emerges from dependencies: statements without a dependency between them can execute simultaneously, and the scheduler discovers parallelism automatically. You write `sync` only when you explicitly need sequential behavior.

2. **The Execution Coupling Problem**
   In Python, switching from eager to compiled execution requires annotating functions, managing trace caches, and reasoning about what the tracer can and cannot capture. In Camel, the same source program can run on the bytecode interpreter (FastVM), be traversed directly as a graph (NodeVM), compiled to native code via JIT, or dispatched to a thread pool as a parallel DAG (Taskflow)—without changing a line of source code.

3. **The Transformation Problem**
   Frameworks that want to rewrite computation graphs must build their own IR and transformation passes on top of Python—`torch.fx`, XLA HLO, StableHLO. In Camel, the Graph IR *is* the program. Compiler passes inspect and rewrite across function boundaries natively, and macros operate on graphs at compile time, making transformations like automatic differentiation or operator fusion expressible as first-class language constructs.

## ✨ Key Features

### 1. Graph-Based Syntaxes

```camel
// Build graph with intuitive operators
func forward(x: Tensor) {
    let layer1 = dense<w1, b1>..relu..dropout
    let layer2 = dense<w2, b2>..relu..dropout
    let layer3 = dense<w3, b3>..softmax
    return x->layer1->layer2->layer3
}
```

### 2. Multi-Stage Programming

```camel
// Compile-time graph optimization
inner macro func apply_gradients(g: functor): functor {
    // inner implemented macro functor that auto
    // adds the back-propagation part of the given graph
}
// usage
let train = apply_gradients(forward<w, b>..loss)
```

### 3. Born-async Semantics

```camel
with <var w: Tensor, var b: Tensor, lr: float>
sync func train(x: Tensor, y: Tensor): Tensor {
    let y_hat = forward<w, b>(x)
    let pl_py = y_hat - y
    wait b = b - lr * pl_py
    wait w = w - lr * pl_py * x
    return loss(y_hat, y)
}
```


## ⚙️ Getting Started

> [!WARNING]
> Camel is now WIP. It only supports some basic commands, the features are unstable and may change frequently, the documentation is also not complete.

### Installation

```bash
# Install via pip (Python toolchain required)
pip install camel-lang
```

### Hello World

```camel
// hello.cml
module main

import { whoami } from os

func main() sync {
    let i = whoami()
    println(format<i>('Hello, {}!'))
    return 0
}
```

Run it:

```bash
camel hello.cml
```

---

### Advanced Usage

Calculate the sum of an arithmetic sequence and measure execution time:

```camel
module arithmetic_sum

import { now } from time

func sum(n: int, a: int, d: int, acc: int): int {
    return if n == 0 then acc else sum(n - 1, a + d, d, acc + a)
}

func main(): int sync {
    let n = 100000  // number of terms
    let a = 1       // first term
    let d = 3       // common difference
    'Start computing arithmetic_sum(n={}, a={}, d={})...'->format<n, a, d>->println
    let start = now()
    let res = sum(n, a, d, 0)
    let duration = now() - start
    'Sum of arithmetic series = {} (computed in {} seconds)'->format<res, duration>->println
    return 0
}
```

## 🧠 Design Philosophy

### For AI Researchers

- **Python-like prototyping**: Build graphs using intuitive operators and natural syntax
- **What-you-see-is-what-runs**: Code is the computation graph—no JIT magic or hidden control flow
- **Self-documenting architecture**: Explicit graph structure reduces legacy code complexity

### For AI Engineers

- **Compile-time optimization**: Static graph analysis enables memory reuse and operator fusion
- **Single-source deployment**: Write once, run optimized—from server CPUs to edge TPUs without code changes
- **Maintainability by design**: Strong typing eliminates tensor shape errors, while explicit graph structure reduces technical debt

### For Framework Developers

- **No more tracing hacks**: First-class graph IR captures user intent directly through language semantics
- **Pluggable optimization**: Extend compiler passes via composable functors instead of fragile AST manipulation
- **Unified backend support**: Generate optimized code for multiple targets from shared graph representation

## Modules & Extensions

### Python Integration

Use the `python` module to embed Python. Supports `py_call`, `py_eval`, `py_run`, `py_wrap`/`py_unwrap`, and more. See [modules/python/README.md](modules/python/README.md) for API reference.

**Type conversion** — `py_unwrap` converts Python objects back to Camel. Because `py_eval`/`py_call` return bare `PyObject`, you must use `as PyObject<T>` to specify the target type before `py_unwrap`:

```camel
import { PyObject, py_eval, py_unwrap } from python

func main(): int sync {
    let res = py_eval("1 + 1") as PyObject<int>
    println(py_unwrap(res))
    return 0
}
```

## 📚 Learn More

- [Build Yourself](docs/setup.en.md) - Environment setup and installation guide
- [CMake build options](docs/build-options.en.md) - FastVM, Python embedding, `npm run config`, `CAMEL_SKIP_PYTHON`
- [WIP] [Documentation](https://docs.opencml.com/) - Language specs and API reference
- [WIP] [Examples](examples/) - From MNIST training to distributed pipelines
- [WIP] [Whitepaper](https://arxiv.org/abs/xxxx.xxxx) - Deep dive into the compiler architecture

## 🤝 Contributing

We welcome contributions! Check out our:

- [Contributing Guide](CONTRIBUTING.md) - How to contribute, development setup, and style guide
- [Code of Conduct](CODE_OF_CONDUCT.md) - Community standards and behavior
- [Security Policy](SECURITY.md) - How to report security vulnerabilities
- [Issue Tracker](https://github.com/OpenCML/Camel/issues) - Good first issues labeled `beginner-friendly`
- [Roadmap](ROADMAP.md) - Planned features (WIP)

## 📜 License

Code written by this project’s contributors is licensed under the [MIT License](LICENSE).

Third-party code from antlr4 is licensed under the BSD-3-Clause License.

---

**Join the Herd** 🌍🐪 – Build the future of AI infrastructure with us!

---

**Enjoy! 🐪Camel Riders!**