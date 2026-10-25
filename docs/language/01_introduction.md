# 1. 引言

[English](en/01_introduction.md) | 中文

## 1.1 本参考的用途

本参考描述 Camel 的语法与核心语义。行文按主题分章，示例都可以原样运行。初次接触 Camel 请先读[入门教程](../tutorial/tutorial.cn.md)；命令行见 [cli.cn.md](../cli.cn.md)；每条诊断的含义见 [diagnostics.md](../diagnostics.md) 或 `camel --explain <名字>`。

语言的根本定位与执行模型由 [设计原则](../design-principles.cn.md) 规定，它是规范性的：本参考与它冲突时以它为准。

## 1.2 Camel 是什么

Camel 是图原生的通用编程语言，AI 是它首要的应用领域。

- **程序只表达计算意图**：要算什么，谁依赖谁。源码写下的就是一张计算图，不是从别的语言里捕获或恢复出来的。
- **每个语言元素直接对应图上的一块结构**：表达式是节点与数据边，函数是子图，调用是节点，`sync` 块是控制边，分支是分支节点。其余性质都由这一对应得出：按依赖求值、互不依赖的计算可以并行、同一份源码可以由不同的遍处理。
- **编译、优化、求导、导出、执行都是对图的变换**，称为遍。遍在命令行上组合：

  ```bash
  camel prog.cml                      # 默认：在节点虚拟机上运行
  camel prog.cml std::opt std::fvm    # 先优化，再在字节码虚拟机上运行
  camel prog.cml std::rgir            # 打印图
  ```

最小的程序：

```cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```text
Hello, Camel!
```

## 1.3 记法约定

- **语法**用近似 BNF 的写法：`*` 零次或多次，`?` 可选，`|` 选择，引号内是字面符号。精确的文法在 [8.5](08_top_level_and_grammar.md#85-完整文法)。
- **示例**以 `cml` 代码块给出，`text` 代码块是它的输出。
- **“当前实现的行为，尚待确定”**：实现已经有确定的行为，但语言规则还没有裁定，以后可能改变。写程序时不要依赖这些行为的细节。

## 1.4 章节

| 章 | 内容 |
|---|---|
| [2 词法](02_lexical.md) | 注释、标识符、关键字、字面量、运算符 |
| [3 数据模型](03_data_model.md) | 类型、值、`let`/`var`、可空与可能出错、映射与集合 |
| [4 执行模型](04_execution_model.md) | 按依赖求值、`sync`、引用、故障与退出、遍 |
| [5 模块](05_modules.md) | `module`、`import`、`export`、`namespace` |
| [6 表达式](06_expressions.md) | 字面量、访问、调用、管道、运算符、`if`、`match`、lambda |
| [7 语句与声明](07_statements.md) | `let`/`var`、`return`/`throw`、块、函数、泛型、类型声明 |
| [8 程序与文法](08_top_level_and_grammar.md) | 顶层结构、入口、文法概要、保留未实现的写法 |

**循环。** Camel 目前没有循环语法：`for`、`while` 不能用（`for`、`in` 是保留字）。重复的计算写成递归（尾递归不受深度限制）或高阶算子 `map`、`filter`、`reduce`、`foreach`。
