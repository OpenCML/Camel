# Camel 语言参考

[English](en/README.md) | 中文

> 语言的根本定位与执行模型由 [设计原则](../design-principles.cn.md) 规定，它是规范性的；本参考与它冲突时以它为准。

本目录是 Camel 的语言参考，按主题分章。文中的示例都在当前实现上运行过，`text` 代码块是真实输出。初次接触 Camel 请先读[入门教程](../tutorial/tutorial.cn.md)。

| 章 | 文档 | 内容 |
|---|---|---|
| 1 | [引言](01_introduction.md) | Camel 是什么、记法约定 |
| 2 | [词法](02_lexical.md) | 注释、标识符、关键字、字面量（范围与舍入）、运算符 |
| 3 | [数据模型](03_data_model.md) | 类型、数值与转换、数组、元组、结构体、字符串、`T?` 与 `T!`、`Map` 与 `Set`、`let`/`var` |
| 4 | [执行模型](04_execution_model.md) | 入口、按依赖求值、`sync`、`var` 与 `wait`、`macro`、故障与 `exit`、递归上限、遍 |
| 5 | [模块](05_modules.md) | `module`、`import`、`export`、`namespace`、随附的模块 |
| 6 | [表达式](06_expressions.md) | 访问、调用、管道、运算符、`??`/`!!`/`?->`、`if`、`match`、lambda、高阶算子、优先级 |
| 7 | [语句与声明](07_statements.md) | `let`/`var`、`return`/`throw`、函数、`with` 形参、重载、泛型、标注、类型声明 |
| 8 | [程序与文法](08_top_level_and_grammar.md) | 顶层结构、文法概要、保留但尚未实现的写法一览 |

## 相关文档

- [入门教程](../tutorial/tutorial.cn.md) 与 [示例集](../../examples/README.cn.md)
- [命令行](../cli.cn.md)、[诊断目录](../diagnostics.md)
- [技术文档](../technical/README.md)：编译流水线、图的中间表示、运行时
- 文法：[8.5 完整文法](08_top_level_and_grammar.md#85-完整文法)，由解析器源码（`src/parse/syntax/`）中每个函数上方的产生式生成；记号表在 `include/camel/parse/syntax/token_kinds.def`
