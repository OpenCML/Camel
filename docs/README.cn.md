# Camel：一种用于统一 AI 开发的图原生编程语言

[English](../README.md) | 中文简体

[![许可证：MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/GetDefaultLogger()o_text.png' width=800>
</p>

Camel是一个图原生的、多阶段的、类型驱动的特定领域语言（DSL），旨在弥合AI研究与生产部署之间的差距。它结合了函数式编程的优雅和声明式编程的表达力。它提供了天生的异步语义和高度可定制的图操作，使开发者能够编写高级代码，该代码编译后具有接近原生的性能。

## 🚀 为什么选择 Camel？

AI 社区早已知道计算图才是正确的计算模型。PyTorch 在运行时动态构建它；TensorFlow 将其静态编译；JAX 通过追踪 Python 函数来重建它。每一个主流框架最终都有一套图——因为这正是硬件和数学所需要的结构。

问题在于，这些图都是**在一门对图一无所知的语言之上，用库手工搭建出来的产物**。Python 依然是宿主语言：动态类型、受 GIL 限制、按语句顺序执行——而这些特性恰恰与框架试图表达的结构背道而驰。由此产生了永久性的张力：`torch.compile`、`@tf.function`、`jax.jit` 这些逃逸口的存在，本质上是在弥合 Python 语义与运行时实际需求之间的裂缝。

Camel 采取了一种不同的思路：**让图成为程序本身**。每个函数都被编译为一张图中间表示（GIR），节点是操作，边是数据依赖。语言与执行模型从一开始就说同一种语言，没有任何需要弥合的裂缝。

这对以下三个 Python 生态在语言层面无法彻底解决的问题，产生了切实的影响：

1. **顺序问题**
   在 Python 中，每条语句都隐含着顺序——即使两条语句之间毫无依赖关系。框架只能在事后通过追踪或图构建 API 来恢复并行性。在 Camel 中，顺序由依赖产生：没有依赖关系的语句可以同时执行，调度器自动发现并行机会。只有在显式需要顺序语义时，你才需要写 `sync`。

2. **执行耦合问题**
   在 Python 中，从 eager 模式切换到编译执行，需要标注函数、管理追踪缓存，并理解追踪器能捕获什么、不能捕获什么。在 Camel 中，同一份源程序可以运行在字节码解释器（FastVM）上、以图节点直接遍历（NodeVM）、通过 JIT 编译为原生机器码，或作为并行 DAG 分发到线程池（Taskflow）——无需修改任何一行源代码。

3. **变换局限问题**
   想要重写计算图的框架，不得不在 Python 之上自建 IR 和变换 Pass——`torch.fx`、XLA HLO、StableHLO 皆是如此。在 Camel 中，图 IR 本身就是程序。编译器 Pass 原生地跨越函数边界进行检查与重写，宏在编译期直接操作图，使得自动微分、算子融合等变换可以作为语言的第一公民来表达，而不再是框架层的特殊魔法。

## ✨ 关键特性

### 1. 基于图的语法

```camel
// 使用直观的操作符构建图
func forward(x: Tensor) {
    let layer1 = dense<w1, b1>..relu..dropout
    let layer2 = dense<w2, b2>..relu..dropout
    let layer3 = dense<w3, b3>..softmax
    return x->layer1->layer2->layer3
}
```

### 2. 多阶段编程

```camel
// 编译时图优化
inner macro func apply_gradients(g: functor): functor {
    // 内部实现的宏，自动添加给定图的反向传播部分
}
// 用法
let train = apply_gradients(forward<w, b>..loss)
```

### 3. 天生的异步语义

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


## ⚙️ 入门

> [!WARNING]
> Camel 目前正处于开发阶段。它只支持一些基本命令，特性不稳定，可能会经常更改，文档也有待完善。

### 安装

```bash
# 通过pip安装（需要Python工具链）
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

运行：

```bash
camel hello.cml
```

---

### 进阶用法

计算等差数列的和并统计执行时间：
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

## 🧠 设计理念

### 面向 AI 研究人员

- **类 Python 的原型设计**：使用直观的运算符和自然的语法构建计算图
- **所见即所得的执行模型**：代码即计算图——没有 JIT 魔法或隐藏的控制流
- **自文档化架构**：显式的图结构降低了遗留代码的复杂性

### 面向 AI 工程师

- **编译时优化**：静态图分析支持内存复用和算子融合
- **单源部署**：一次编写，可优化运行于服务器 CPU 到边缘 TPU，无需更改代码
- **从设计上保障可维护性**：强类型系统消除张量维度错误，显式图结构减少技术债务

### 面向框架开发者

- **无需再使用追踪技巧**：一等图中间表示（IR）通过语言语义直接捕捉用户意图
- **可插拔的优化机制**：通过可组合的函数对象扩展编译器优化通道，避免脆弱的 AST 操作
- **统一后端支持**：基于共享的图表示为多种目标生成优化后的代码

## 模块与扩展

### Python 集成

使用 `python` 模块嵌入 Python。支持 `py_call`、`py_eval`、`py_run`、`py_wrap`/`py_unwrap` 等。详见 [modules/python/README.md](../modules/python/README.md) 的 API 参考。

**类型转换** — `py_unwrap` 将 Python 对象转换回 Camel。因为 `py_eval`/`py_call` 返回裸的 `PyObject`，你必须在 `py_unwrap` 之前使用 `as PyObject<T>` 指定目标类型：

```camel
import { PyObject, py_eval, py_unwrap } from python

func main(): int sync {
    let res = py_eval("1 + 1") as PyObject<int>
    println(py_unwrap(res))
    return 0
}
```

## 📚 了解更多

- [自行构建](setup.cn.md) - 环境设置和安装指南
- [CMake 编译选项](build-options.cn.md) - FastVM、Python 嵌入模块、`npm run config` 与 `CAMEL_SKIP_PYTHON`
- [WIP] [文档](https://docs.opencml.com/) - 语言规范和API参考
- [WIP] [示例](../examples/) - 从MNIST训练到分布式管道
- [WIP] [白皮书](https://arxiv.org/abs/xxxx.xxxx) - 深入了解编译器架构

## 🤝 贡献

欢迎贡献！请查看：

- [贡献指南](CONTRIBUTING.cn.md) - 如何贡献、开发环境与风格指南
- [行为准则](CODE_OF_CONDUCT.cn.md) - 社区标准与行为规范
- [安全政策](SECURITY.cn.md) - 如何报告安全漏洞
- [问题跟踪](https://github.com/OpenCML/Camel/issues) - 标有 `beginner-friendly` 的入门友好 Issue
- [路线图](../ROADMAP.md) - 计划功能（开发中）

## 📜 许可证

本项目贡献者编写的代码根据 [MIT 许可证](../LICENSE) 授权。

来自 antlr4 的第三方代码根据 BSD-3-Clause 许可证授权。

---

**加入我们的队伍** 🌍🐪 – 与我们一起构建AI基础设施的未来！

---

**尽情享受吧！🐪 Camel Riders!**
