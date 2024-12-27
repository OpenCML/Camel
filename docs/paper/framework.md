# WHY is Camel

讲清楚核心语言设计/出发点，明确其存在的意义以及发展的前景

Camel三大创新点：

基于图的半开放前端语言

经典语言与函数式编程结合

天生异步的高并发支持

静态类型的脚本语言（高性能上限，解释效率低，编译上限高）

## Python is NOT for AI

在AI时代，Python的优点不再明显，短板依旧存在。

作为一门动态语言，没有很高的静态分析能力。

动态图和静态图之争/hack困难

GIL

静态类型vs动态类型

命令式vs声明式

古老设计vs现代设计：缩进语法、并发支持

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

选取经典案例展开，不要罗列概念

从一个简单的AImodel出发

## Execution Model

底层调度插件

执行顺序

异步并发

## Extensions

底层插件接口设计

FFI设计讲解

以TF Ext为案例重点讲解



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

