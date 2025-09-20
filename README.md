# Camel: A Graph-Native Programming Language for Unified AI Development

[中文简体](README.cn.md) | English

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/camel_logo_text.png' width=800>
</p>

Camel is a **graph-based**, **multi-stage**, and **type-driven** domain-specific language (DSL) designed to bridge the gap between AI research and production deployment. It combines the elegance of **functional programming** with the expressiveness of **declarative programming**. It provides **born-async semantics** and highly **customizable graph manipulations**, enabling developers to write **high-level code** that compiles to **near-native performance**.

## 🚀 Why Camel?

Modern AI development faces a dilemma:

1. **Semantic Fragmentation**
   JIT tracing (e.g., TensorFlow) creates gaps between code intent and execution graphs, forcing debug workarounds and control-flow compromises.
2. **Cognitive Overload**
   Framework-specific concepts (gradient tapes, graph phases, staged execution) demand expertise orthogonal to ML theory.
3. **Prototype-Deployment Divide**
   Python's dynamism prohibits deep optimization, while static languages lose high-level ML abstractions.

Camel solves this by:

1. **First-Class Computation Graphs**
   Native graph primitives replace fragile tracing—code directly defines compiler-optimized DAGs.
2. **Phase-Polymorphic Semantics**
   Single codebase executes interactively (Python-like immediacy) or compiles to optimized binaries (C++-level performance).
3. **Type-Driven Automation**
   Tensor shapes/types statically guide memory planning, operator fusion, and parallelization—zero manual tuning.

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

### Hello World in Camel

```camel
// hello.cml
module main

func main() {
    println('Hello, {}!'.format(whoami()))
}
```

Run it:

```bash
camel hello.cml
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

## 📚 Learn More

- [Build Yourself](docs/setup.md) - Environment setup and installation guide
- [WIP] [Documentation](https://docs.opencml.com/) - Language specs and API reference
- [WIP] [Examples](examples/) - From MNIST training to distributed pipelines
- [WIP] [Whitepaper](https://arxiv.org/abs/xxxx.xxxx) - Deep dive into the compiler architecture

## 🤝 Contributing

We welcome contributions! Check out our:

- [WIP] [Issue Tracker](https://github.com/OpenCML/Camel/issues) - Good first issues labeled `beginner-friendly`
- [WIP] [Roadmap](ROADMAP.md) - Planned features like quantum backend support
- [WIP] [Style Guide](CONTRIBUTING.md#style-guide) - Code formatting and design patterns

## 📜 License

Code written by this project’s contributors is licensed under the [MIT License](LICENSE).

Third-party code from antlr4 is licensed under the BSD-3-Clause License.

---

**Join the Herd** 🌍🐪 – Build the future of AI infrastructure with us!

---

**Enjoy! 🐪Camel Riders!**