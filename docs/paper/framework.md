[TOC]

# Abstract

TensorFlow Eager is a multi-stage, Python-embedded domain-specific language for hardware-accelerated machine learning, suitable for both interactive research and production.

Camel is a graph-based, multi-stage, born-async modeling language front-end for AI compiliers, suitable for both interactive research and production.

nolonger needs to trade deployment performance for interactive dynamism.

# Introduction

An ideal DSL would offer the flexibility and accessibility of imperative execution along with the many benefits of declarative programming, without either of their costs. It is with this motivation in mind that we present TensorFlow Eager, a Python-embedded DSL for differentiable programming that lets developers interpolate between imperative and staged computations in a single package.

要引入多阶段编程的概念Multi-stage  Programming

# Related Works

# WHY is Camel

讲清楚核心语言设计/出发点，明确其存在的意义以及发展的前景

Camel三大创新点：

基于图的半开放前端语言

经典语言与函数式编程结合

天生异步的高并发支持

静态类型的脚本语言（高性能上限，解释效率低，编译上限高）

## Python is NOT for AI

虽然AI编译器领域的理论与实践在近年来已经有了长足的发展，只有很少部分工作聚焦于AI编译器的前端，人们往往优先考虑使用Python作为AI编程的首选，却对Python自身的局限性闭口不谈，许多关于如何优化AI编译器前端设计以及在何种情况下需要重新设计前端的关键问题仍然悬而未决。
Although the theory and practice of AI compilers have made significant progress in recent years, only a small portion of the work focuses on the frontend of AI compilers. People often prioritize using Python as the preferred choice for AI programming while remaining silent about the limitations of Python itself. Many key questions regarding how to optimize the design of AI compiler frontends and under what circumstances a redesign of the frontend is necessary remain unanswered. 

在AI时代，Python的优点不再明显，短板依旧存在。

作为一门动态语言，没有很高的静态分析能力。

动态图和静态图之争/hack困难

GIL

静态类型vs动态类型

命令式vs声明式

古老设计vs现代设计：缩进语法、并发支持
上世纪九十年代发明的脚本语言已经无法满足新时代的AI变成需求了，现在人们被迫花费大量的心思用各种hack手段为python打补丁，只是出于无奈的路径依赖罢了。

Dataflow-driven concurrency without locks or callbacks

## ML/FP benifits a lot

传统的FP语言语法的设计对于众多工业界开发者和学生来说是陌生的、难以理解的

现代的许多语言逐步引入函数式编程范式

例如JS/C++函子/python

传统的函数式编程语言语法相比于其他语言显得太过陌生

## Handle the graph as you wish

Based on DAG models

Customizable Runtime

## born for async and parallel

Openness

交出底层控制权

# WHAT is Camel

## Language Design



图设计/类型系统设计/其他设计

选取经典案例展开，不要罗列概念

Graph Intermediate Representation, GraphIR, or Graph

Graph-based Modeling Syntax

（sync的介绍，图依赖介绍，数据依赖0，1，阻塞依赖2，wait，控制依赖3）不同依赖使用不同颜色画图

介绍子图、操作、数据，介绍节点与连接

大致就是三种结点（数据节点DataNode，算子节点OperatorNode，子图节点SubgraphNode）和三种依赖（画一张示意图，左边是函子定义，右边是子图结构

讲明白结点构成，函子和操作，操作分为内部和外部

讲明白建立依赖所需要的关键字和运算符：let var . -> .. with sync wait等等(**Side Effect Versioning**)

点出camel执行默认是异步并发的，是与源码顺序无关（不敏感）的

先给出一段代码，然后画出其所表示的图，介绍依赖关系，讲明白图的大体结构的表示

这里只讲关系示意图，用不同颜色区分依赖类型，实现图的结构放在实现部分讲，在这里注明，示意图和实现图略有差别

同步控制，赋值语句，wait和sync，引入控制依赖

神经网络的结构并不仅限于多层全连接，在深度学习领域，存在局部连接、[权值共享](https://zhida.zhihu.com/search?content_id=103501337&content_type=Article&match_order=1&q=权值共享&zhida_source=entity)、跳跃连接等丰富多样的神经元连接方式，多层全连接仅仅是其中的一种。每一种都要有所体现



Fully-advanced Type Sys

接下来类型系统，原子类型，泛型，静态分析，函子重载，不可变性，



Other Features as A Language

其他高阶特性

模块系统

异常处理











本质上要讲清楚图的设计以及构造过程

从一个简单的AImodel出发

ADT type system，鸭子类型与接口

超参与参数设计（传参设计）

## Execution Model

执行顺序

查一查控制依赖有无必要

异步并发

**compile-time macro functors**

定义图-》完全图（macro-free graph）

这里要进一步思考如何在图中表示子图作为参数

例如：

```
let f = m(g..h) // m 为macro函数
```

该怎样表达在图中呢？还是说，在编译期就立刻执行了？

宏函数的超参需要是编译期常量吗？这就需要论证动态条件编译有无必要。

这里要展示从前向计算图到反向计算图的应用（附简单图）

也可以展示条件编译（比如不同设备，toCPU, toGPU，附图

```
with <device: string>
macro func toDevice(g: functor): functor {
	return if device == 'GPU' then applyToCuda(g) else g
}

macro outer func applyToCuda(g: functor): functor => 'tf://xxx/xx::apply_to_cuda'
```

![../_images/graph.png](https://openmlsys.github.io/_images/graph.png)

**Multi-stage graph reduction**

编译时图规约 Compile-time graph reduction

运行时图调度 Runtime Graph Scheduling

Camel将代码执行的过程视为多阶段图规约的过程。在Camel中，对输入子图进行规约（优化）并返回规约后的图的宏函子被称为图规约函子，其中，对输入子图进行部分调度和执行，产生副作用并返回简化后的图的函子称为图调度函子，图调度函子是图规约函子的一种特殊情况。将编译阶段得到的完全图经过一系列图规约函子和图调度函子处理后得到空图并产生一系列副作用的过程构成了Camel代码的执行过程。Camel内置了将图完全调度执行的图优化器，以确保得到充分执行。

这里也有多阶段编程的概念，注意引用Multi-stage  Programming（打破传统编译时和运行时的界限，可以在运行时生成并编译代码，使得程序能动态适应运行环境）

举例说明

附图说明

完全图-（图优化）》优化图-（图调度）》部分执行图-（保底机制）》空图

甚至可以是流式循环执行，因为调度完全由调度算法决定

中间图缓存机制

举例说明（可以以保存ckpt为例）

自由的调度选择，比如并发执行，比如缓存执行，比如条件执行（根据不同的目标机器规约出不同的图），懒惰执行（本质上仍然是异步执行，某些表达式消除算法可以让未用到的图节点免于计算，这也有点像条件执行），以及重解释执行（re-interpreter execution）

**operator executor as plugins(FFI)**

可拓展性

互操作性

生态复用

# HOW does Camel work

## Core dev

讲讲IR和编译流程

讲讲图结构和图优化

## Case studies

- 语言优势
- 拓展能力

## Experiments

- 性能潜力
  - 在并发场景下对python的优势
  - 在AI关键模型上编译后性能几乎持平
- 生态复用
  - 对Python和C++的自然调用



# Conclusion

Discussion and Future Work

虽然camel是专门为AI设计的DSL，但其又是一个通用的DSL框架，因为其本质是为DAG服务，而DAG在诸多领域都有基石的作用。因此，camel可以被很容易地移植到其他领域并作为其他DSL的基石，当越来越多的领域生态丰富之后，Camel将在事实上成为一个通用编程语言。
