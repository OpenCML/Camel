# Why Camel

English | [中文](why.cn.md)

Camel is a graph-native general-purpose programming language, and AI is its primary application.

Most languages write a program as instructions for a machine to carry out one step at a time. Yet what compilers, parallel runtimes, automatic differentiation and GPU back ends actually need is a graph: what is to be computed, and what depends on what. So the graph has to be recovered from the code after the fact, by tracing, capturing and analysis. The recovered graph breaks apart, loses what the side effects meant, and the transformations applied to it become a black box the user cannot touch.

In Camel **the program is the graph**. What you write is the intent of the computation and its dependencies; order is written only where you need it. From that:

- **The compiler gets exactly what you wrote**: nothing is traced, the graph does not break, and computations that do not depend on each other are free to run in parallel.
- **Transformations are ordinary functions**: optimizing, differentiating, moving to a GPU and exporting are passes over the graph. You can look at them, swap them, and write your own in Camel.
- **Knowledge about optimization is code you can check**: an algebraic law is a rule, and `check` tells you whether it holds.
- **One program, many ways to run it**: interpreted, JIT-compiled, on a GPU, or exported to ONNX or C++, with the same meaning.

These properties hold for every program where computation matters more than control flow. Training and deploying models is the most pressing of them, which is why AI is Camel's primary application, though not its only one.
