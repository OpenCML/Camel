# Camel 语言参考（详细版）

本目录是 Camel 语言的**详细语言参考**，按主题分章撰写，每章配有语法说明与可运行示例。概要版见 [../technical/00_language_reference.md](../technical/00_language_reference.md)。

---

## 文档目录

### 第一部分：基础

| 章节 | 文档 | 内容概要 |
|------|------|----------|
| **1** | [01_introduction.md](01_introduction.md) | 引言、设计定位、记法约定、其它实现 |

### 第二部分：词法与类型

| 章节 | 文档 | 内容概要 |
|------|------|----------|
| **2** | [02_lexical.md](02_lexical.md) | 行结构、注释、标识符与关键字、字面量、运算符与分隔符（含示例） |
| **3** | [03_data_model.md](03_data_model.md) | 对象/值/类型、类型层次、let/var、算子与特殊方法（含类型与数据示例） |

### 第三部分：执行与模块

| 章节 | 文档 | 内容概要 |
|------|------|----------|
| **4** | [04_execution_model.md](04_execution_model.md) | 程序结构、命名与绑定、异常、sync 与 wait（含控制流与同步示例） |
| **5** | [05_modules.md](05_modules.md) | 模块声明、import、export、模块名与路径（含多文件示例） |

### 第四部分：表达式与语句

| 章节 | 文档 | 内容概要 |
|------|------|----------|
| **6** | [06_expressions.md](06_expressions.md) | 原子、访问/下标/调用/泛型、管道、一元与类型断言、运算优先级、if/match/try、Lambda（含大量表达式示例） |
| **7** | [07_statements.md](07_statements.md) | 简单语句（表达式、return、let/var、use）、复合语句（块、if、match、try、函数定义、类型定义）（含完整程序片段） |

### 第五部分：程序与文法

| 章节 | 文档 | 内容概要 |
|------|------|----------|
| **8** | [08_top_level_and_grammar.md](08_top_level_and_grammar.md) | 顶层组成、完整程序、文件输入、文法概要与 ANTLR 对应关系 |

---

## 示例代码来源

文档中的示例多来自项目内可运行用例：

- **test/run/linear/**：hello.cml、fib.cml、var.cml、lambda.cml、snake.cml、index.cml、acc.cml、neuro.cml、zen.cml、render.cml 等
- **test/run/para/**：msort.cml（归并排序、管道与 slice）
- **test/run/nn/**：lr.cml、mlp.cml、svm.cml（张量、with 参数、sync 函数）
- **stdlib/**：标准库签名与用法

运行示例（在项目根目录）：

```bash
camel run test/run/linear/hello.cml
camel run test/run/linear/fib.cml
```

---

## 相关文档

- [技术文档索引](../technical/README.md)：编译流水线、GIR、运行时、JIT 等
- [语言规范摘要](../technical/02_language_spec.md)：简短语法与语义归纳
- [ANTLR 文法](../../antlr/)：OpenCML.g4、OpenCMLLex.g4
- [AST 定义](../Camel.asdl)：抽象语法树结构
