# Camel：一种用于统一 AI 开发的图原生编程语言

[English](README.md) | 中文简体

[![许可证：MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
![WIP](https://img.shields.io/badge/status-WIP-yellow.svg)

<p align="center">
  <img src='https://www.github.com/OpenCML/ImagesHosting/raw/main/camel_logo_text.png' width=800>
</p>

Camel是一个基于图的、多阶段的、类型驱动的特定领域语言（DSL），旨在弥合AI研究与生产部署之间的差距。它结合了函数式编程的优雅和声明式编程的表达力。它提供了天生的异步语义和高度可定制的图操作，使开发者能够编写高级代码，该代码编译后具有接近原生的性能。

## 🚀 为什么选择Camel？

现代AI开发面临的困境：

1. **语义碎片化**
   JIT追踪（例如TensorFlow）在代码意图和执行图之间产生了差距，迫使开发者进行调试和控制流程的妥协。
2. **认知负荷**
   特定框架的概念（梯度带，图阶段，分阶段执行）要求与ML理论正交的专业知识。
3. **原型-部署鸿沟**
   Python的动态性限制了深度优化，而静态语言则失去了高级ML抽象。

Camel通过以下方式解决这些问题：

1. **一流的计算图**
   原生图形原语取代了脆弱的追踪——代码直接定义了编译器优化的DAG。
2. **阶段多态语义**
   单一代码库可以交互执行（类Python的即时性）或编译成优化的二进制文件（具有C++级别的性能）。
3. **类型驱动的自动化**
   张量形状/类型在编译时静态指导内存规划、算子融合和并行化——零手动调整。

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

### Camel中的Hello World

```camel
module main

func main() {
    println('Hello, {}!'.format(whoami()))
}
```

运行：

```bash
camel hello.cml
```

## 🧠 设计理念

### 针对AI研究者

- **类Python的原型制作**：使用直观的操作符和自然语法构建图
- **所见即所得**：代码即计算图——没有JIT魔法或隐藏的控制流
- **自我记录的架构**：明确的图结构减少了遗留代码的复杂性

### 针对AI工程师

- **编译时优化**：静态图分析使得内存重用和算子融合成为可能
- **单源部署**：一次编写，处处优化——从服务器CPU到边缘TPU，无需代码更改
- **设计时的可维护性**：强类型消除了张量形状错误，而明确的图结构减少了技术债务

### 针对框架开发者

- **不再有追踪的黑客攻击**：一流的图IR通过语言语义直接捕获用户意图
- **可插拔的优化**：通过可组合的函数而不是脆弱的AST操作扩展编译器通道
- **统一后端支持**：从共享图表示生成针对多个目标的优化代码

## 📚 了解更多

- [自行构建](docs/setup.cn.md) - 环境设置和安装指南
- [WIP] [文档](https://docs.opencml.com/) - 语言规范和API参考
- [WIP] [示例](examples/) - 从MNIST训练到分布式管道
- [WIP] [白皮书](https://arxiv.org/abs/xxxx.xxxx) - 深入了解编译器架构

## 🤝 贡献

欢迎贡献！请查看我们的：

- [WIP] [问题跟踪](https://github.com/OpenCML/Camel/issues) - 标记为`beginner-friendly`的好的首个问题
- [WIP] [路线图](ROADMAP.md) - 计划的功能，如量子后端支持
- [WIP] [风格指南](CONTRIBUTING.md#style-guide) - 代码格式和设计模式

## 📜 许可证

本项目贡献者编写的代码根据 [MIT 许可证](LICENSE) 授权。

来自 antlr4 的第三方代码根据 BSD-3-Clause 许可证授权。

---

**加入我们的队伍** 🌍🐪 – 与我们一起构建AI基础设施的未来！

---

**尽情享受吧！🐪 Camel Riders!**