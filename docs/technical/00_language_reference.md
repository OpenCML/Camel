# Camel 语言参考

本参考手册描述 Camel 语言的**语法**与**核心语义**。行文简练，力求准确、完整。非必要的内置对象类型、内置函数与模块的语义见标准库文档。关于语言的非正式入门，可参阅项目内其他技术文档。实现与编译流水线见《编译流水线》《图中间表示》等文档。

---

## 1. 引言

### 1.1 设计定位

Camel 是一种**以图为核心**的编程语言：程序在编译期被转换为图中间表示（GIR），运行时通过图调度执行。执行顺序由数据依赖决定，并行性由编译器与运行时从图中发现。

### 1.2 其它实现

本参考以当前官方实现（C++ 前端 + GIR + FastVM/NodeVM/Taskflow）为语义基准。其它实现若与本文描述一致，可视为同一语言；若有差异，应以本文与官方实现为准。

### 1.3 记法约定

- **语法**：规则名与产生式采用近似 BNF 的写法；`*` 表示零次或多次，`?` 表示可选，`|` 表示选择。
- **语义**：用自然语言描述求值顺序、绑定、可见性等；与实现一致处以当前源码与 `test/` 下用例为准。

---

## 2. 词法分析

### 2.1 行与语句分隔

- **语句分隔符**：分号 `;` 可作为语句之间的分隔；同一逻辑行内多条语句需用 `;` 分隔。
- **换行**：词法上空白（含换行、空格、制表符）通常被忽略，仅影响分隔；字符串与注释内的换行按字面处理。
- **空白**：` `、`\t`、`\r`、`\n`、`\f` 等由词法分析器跳过（除在字符串、多行字符串内）。

### 2.2 注释

- **行注释**：`//` 至行末，内容放入 channel，不参与语法分析。
- **块注释**：`/*` … `*/`，可跨行，同样放入 channel。
- **命令/预处理行**：`#` 至行末，放入 channel(2)，由词法器识别但不参与语法。

### 2.3 标识符与关键字

- **标识符（identDef）**：以字母或下划线开头，后接零个或多个字母、数字、下划线。即 `[a-zA-Z_][a-zA-Z_0-9]*`。
- **引用名（identRef）**：用于命名空间限定时可含 `::`，如 `mod::name`；否则同标识符。
- **关键字**（保留，不可作普通标识符）：  
  `as`, `let`, `var`, `use`, `from`, `func`, `type`, `enum`, `with`, `wait`, `const`, `keyof`,  
  `return`, `module`, `import`, `export`, `typeas`, `typeof`, `namespace`,  
  `if`, `then`, `else`, `match`, `case`, `for`, `in`, `of`, `try`, `catch`, `finally`, `throw`, `raise`,  
  `sync`, `macro`, `inner`, `outer`, `atomic`, `shared`,  
  `null`, `true`, `false`。  
- **多字符记号**：`...`（省略号）在词法中优先于 `.`；`..` 为双点。

### 2.4 字面量

- **整数（INTEGER）**：十进制 `0`、`30`；八进制 `0` 后接 `[0-7]+`；十六进制 `0x…`；二进制 `0b…`。可带前导 `+`/`-`，可接指数部分 `e`/`E`。
- **实数（REAL）**：带小数点的数字，可选指数部分，如 `1.0`、`1e-2`。
- **字符串（STRING）**：双引号 `"…"` 或单引号 `'…'`，内可转义 `\"`、`\'`、`\\` 等。
- **多行字符串（MULTI_STR）**：`'''…'''` 或 `"""…"""`，中间可含换行。
- **F 字符串（FSTRING）**：反引号 `` `…` ``，用于格式化等（具体占位与库约定相关）。
- **布尔**：`true`、`false`。
- **空值**：`null`。

### 2.5 运算符与分隔符

- **比较**：`===`、`!==`、`==`、`!=`、`<`、`>`、`<=`、`>=`。
- **逻辑**：`&&`、`||`。
- **算术/位**：`+`、`-`、`*`、`/`、`%`、`^`（幂/异或依上下文）、`@`（如矩阵乘）、`~`（按位取反）、`!`（逻辑非）。
- **赋值**：`=`、`+=`、`-=`、`*=`、`/=`、`%=`、`^=`、`@=`、`&=`、`|=`。
- **可空**：`??`（空则取右）、`!!`（非空断言或错误则取右，依实现）。
- **其它**：`->`（管道）、`?->`（可选管道）、`..`、`?..`（范围等）、`.`（成员/元组下标）、`..`/`...`（范围/展开）。
- **括号与结构**：`( )`、`[ ]`、`{ }`、`< >`（泛型/显式参数用 `<>`）。

---

## 3. 数据模型

### 3.1 对象、值与类型

- 程序操作的是**值**；值具有**类型**。类型在编译期用于检查，在运行时有对应表示（如 `core/type/`、`core/data/`）。
- **不可变绑定**：`let` 引入的名字绑定到初值，该绑定在其作用域内不可重新赋值。
- **可变变量**：`var` 引入的名字可被重新赋值（含复合赋值）。

### 3.2 类型层次概览

- **基本类型**：如 `int`、`float`、`bool`、`string` 等（名称与实现一致）。
- **复合类型**：  
  - 数组：`T[]`、`T[][]` 等。  
  - 结构体：字面形式 `{ x: int, y: int }` 或类型别名。  
  - 元组：`(T1, T2, ...)`。  
  - 函数类型：`(参数列表) => 返回类型`，可带 `with` 参数与修饰符（如 `sync`）。
- **泛型/参数化类型**：`Ident<T1, T2, ...>`，如 `Point[]`、`(int, int)`。
- **可空**：类型后可跟 `?` 表示可空联合（语法见 `typeExpr`）。
- **类型运算**：联合 `|`、交集 `&`、差 `\`、键联合等（见类型表达式文法）。

### 3.3 特殊方法名与算子

- 运算符在 GIR 中对应 **OperNode**，绑定到 `operator_t`；运行时按操作数类型分发（如 `__add__`、`__le__` 等），具体名与实现一致。

---

## 4. 执行模型

### 4.1 程序结构

- 程序由**模块**组成；每个源文件是一个模块。  
- 顶层可包含：`module` 声明、`import`、`export`、类型定义、数据声明（`let`/`var`）、函数定义、以及可执行语句（表达式、`return` 等）。
- 执行入口由实现约定（如名为 `main` 的函数，或显式指定）。

### 4.2 命名与绑定

- **作用域**：模块顶层、函数参数、块（`{ }`）各自引入作用域；内层可遮蔽外层同名。
- **let**：在当前作用域引入不可变绑定；同一作用域内同一名字不能重复 `let`。
- **var**：引入可变变量；可重复赋值。
- **import**：从其他模块引入符号，如 `import { a, b } from mod`、`import x from mod`；引入的名字在当前模块可见。
- **with**：在函数上声明「上下文/泛型式」参数，调用时通过 `func<with_args>` 传入，函数体内使用对应名字。

### 4.3 异常与退出

- **return**：从当前函数返回，可带值表 `return e1, e2, ...` 或 `return`（无值）。
- **raise / throw**：引发异常（语法为 `retStmt`：`RETURN | RAISE | THROW` + `dataList`）；具体异常类型与传播由实现定义。
- **try-catch-finally**：`try { ... } catch id: Type { ... } ... (finally { ... })?`；语义为捕获指定类型异常并执行相应块。

### 4.4 同步与等待

- **sync 块**：`sync { ... }` 或函数标注 `sync`，表示块内顺序执行、显式同步；常用于 I/O 或需要严格顺序的代码。
- **wait**：在 sync 块内，`wait expr` 表示等待该表达式求值完成再继续；用于与异步或副作用操作配合。

---

## 5. 模块与导入

- **模块声明**：`module <名称>`，如 `module main`、`module fib`。模块名为标识符或点分名。
- **导入**：`import (moduleName | (identDef | bracedIdents) from moduleName)`。  
  - 例：`import { now } from time`、`import { set_terminal_raw_mode, has_input, get_char } from os`、`import { zen } from this`。
- **导出**：`export (dataDecl | typeDecl | bracedIdents)`，使符号可被其他模块导入。
- **模块名**：可选前导 `.`/`..`/`...`，后接 `IDENTIFIER ('.' IDENTIFIER)*`，如 `time`、`os`、`this`。

---

## 6. 表达式

### 6.1 求值顺序

- 表达式由子表达式按运算符优先级与结合性求值；短路求值适用于 `&&`、`||`（以及实现定义的 `??`/`!!`）。
- 管道 `a->b`：先求值 `a`，再以该值为参数调用 `b`（或传入 b 的某一参数位）。
- 泛型/显式参数：`expr<args>` 先确定被调用对象与类型/值参数，再按调用语义求值。

### 6.2 原子（primaryData）

- **标识符**：变量或函数名。
- **字面量**：整数、实数、字符串、多行字符串、F 字符串、`true`、`false`、`null`。
- **数组字面量**：`[ indexValues ]` 或 `[ dataExpr for identRef in dataExpr (if dataExpr)? ]`（列表推导）。
- **结构体字面量**：`{ key: value, ... }`，键为标识符。
- **元组字面量**：`( dataList?, )`。
- **括号表达式**：`( dataExpr )`。
- **Lambda**：`(参数) => 块或表达式`，见 6.14。

### 6.3 主表达式与访问

- **访问**：`primaryData ('.' (IDENTIFIER | INTEGER))*`，如 `p.x`、`signal.0`（元组下标）、`obj.field`。
- **下标**：`expr [ dataExpr ]` 或带切片 `[ start : end : step? ]`（若实现支持）。
- **调用**：`expr ( argumentList? )`；参数可为位置参数与/或键值对。
- **泛型/显式参数**：`expr < argumentList? >`，如 `format<idx>`、`slice<0, len(arr)>`。
- **相邻后缀**：词法要求下标、圆括号参数、尖括号参数、`!` 等与前面的 `annoExpr` 相邻（无空白），由文法 `annoExpr` 的 `isAdjacent()` 保证。

### 6.4 管道与组合

- **管道**：`compExpr ( '->' | '?->' ) compExpr`，左结合；`a->b` 表示将左侧结果作为参数传给右侧（方法或函数）。
- **范围/组合**：`annoExpr ('..' | '?..') annoExpr`（若实现支持）。

### 6.5 一元与类型断言

- **一元**：`!`、`-`、`~` 后接 `linkExpr`。
- **类型**：`linkExpr (as | is) typeExpr`，表示类型转换或类型判断。

### 6.6 可空

- **nullableExpr**：`unaryExpr (('??' | '!!') dataExpr)?`，即空合并或非空断言等。

### 6.7 乘性、加性、关系、相等、逻辑

- **乘性**：`*`、`/`、`^`、`@`、`%`，左结合。
- **加性**：`+`、`-`，左结合。
- **关系**：`<`、`>`、`<=`、`>=`。
- **相等**：`===`、`!==`、`==`、`!=`。
- **逻辑与**：`&&`，短路。**逻辑或**：`||`，短路。

### 6.8 赋值

- **assignExpr**：`logicalOrExpr ( (= | += | -= | *= | /= | %= | ^= | @= | &= | |= ) logicalOrExpr )?`。赋值为表达式，其值为所赋之值（依实现）。

### 6.9 控制表达式

- **if-then-else**：`if logicalOrExpr then blockExpr (else blockExpr)?`。  
  - `blockExpr` 可为块 `sync? '{' stmtList '}'` 或单一 `dataExpr`。  
  - 例：`return if n <= 1 then n else fib(n - 1) + fib(n - 2)`；块形式：`if c then sync { ... } else sync { ... }`。
- **match**：`match identRef '{' matchCase+ '}'`。  
  - **matchCase**：`case pattern ('|' pattern)* '=>' blockExpr`。  
  - **pattern**：`identRef`、字面量、`( dataList | identList )`、`{ pairedValues | identList }`、`_`（通配符）。  
  - 例：`match dir { case 'w' => { x: now_head.x, y: now_head.y - 1 }; case _ => now_head }`。
- **try**：`try stmtBlock catchClause+ (finally stmtBlock)?`；`catchClause` 为 `catch identDef ':' typeExpr stmtBlock`。

### 6.10 wait 表达式

- **waitExpr**：可选前缀 `wait` + **assignExpr**；在 sync 块内 `wait expr` 表示等待该表达式完成再继续。

### 6.11 Lambda 与函数类型

- **Lambda（funcData）**：`modifiers? angledParams? parentParams (':' typeExpr)? '=>' blockExpr`。  
  - **blockExpr**：`stmtBlock` 或 `dataExpr`。  
  - 例：`(p: Point) => bool` 风格的类型、`(x: int): string => { ... }`、`(y: int) => sync { ... }`。
- **函数类型（funcType）**：`modifiers? angledParams? parentParams '=>' typeExpr`，用于类型注解与 with 参数。

### 6.12 运算符优先级（从低到高）

1. 赋值（`=`, `+=`, …）  
2. 逻辑或 `||`  
3. 逻辑与 `&&`  
4. 相等 `===`、`!==`、`==`、`!=`  
5. 关系 `<`、`>`、`<=`、`>=`  
6. 加性 `+`、`-`  
7. 乘性 `*`、`/`、`^`、`@`、`%`  
8. 可空 `??`、`!!`  
9. 一元 `!`、`-`、`~`；类型 `as`、`is`  
10. 管道 `->`、`?->`；范围 `..`、`?..`  
11. 后缀：访问 `.`、下标 `[]`、调用 `()`、泛型 `<>`、`!`

---

## 7. 简单语句

### 7.1 表达式语句

- 一条 **dataExpr** 可作为语句；其值可被丢弃。例：`println('Hello')`、`render(snake)`。

### 7.2 赋值语句

- 赋值既可作表达式，也可作语句；`var x = e` 在声明中已涵盖。

### 7.3 return / raise / throw 语句

- **retStmt**：`(return | raise | throw) dataList?`。  
  - `return` 可带多个表达式（dataList），或省略表示无值返回；可返回 `null`。

### 7.4 数据声明（let / var）

- **dataDecl**：`(let | var) carrier (':' typeList)? '=' dataList`。  
  - **carrier**：单个标识符、括号/花括号/方括号内的标识符列表（解构）。  
  - **dataList**：一个或多个表达式，与 carrier 结构对应。  
  - 例：`let idx = 30`、`var c = 0`、`let signals: Signal[] = [(1, 2), (10, 20)]`。

### 7.5 use 语句

- **useDecl**：`use (identDef '=')? identRef`；用于将某符号（或模块）以可选别名引入当前作用域（具体语义依实现）。

---

## 8. 复合语句

### 8.1 块

- **stmtBlock**：`sync? '{' stmtList? '}'`。  
  - 可选前导 `sync` 表示同步块；**blockStmt** 可为 `wait? stmtBlock`，即块前可带 `wait`。

### 8.2 if 语句

- 见 6.9：`if ... then blockExpr (else blockExpr)?`；then/else 分支可为块或单表达式。

### 8.3 match 语句

- 见 6.9：`match identRef '{' matchCase+ '}'`；每个 case 为 `case pattern ('|' pattern)* '=>' blockExpr`。

### 8.4 try 语句

- 见 6.9：`try stmtBlock catchClause+ (finally stmtBlock)?`。

### 8.5 函数定义

- **funcDecl**：`(with angledParams)? export? implMark? modifiers? func identDef parentParams (':' typeExpr)? stmtBlock`。  
  - **implMark**：`inner` | `outer`。  
  - **modifiers**：`atomic`、`shared`、`sync`、`macro` 等组合。  
  - **parentParams**：`'(' pairedParams? ','? ')'`；**keyParamPair**：`var? identDef ':' typeExpr ('=' dataExpr)?`。  
  - 例：`func fib(n: int): int { ... }`、`func main(): int sync { ... }`、`with <predicate: (p: Point) => bool> func my_filter(...)`。

### 8.6 类型定义

- **typeDecl**：`implMark? type identDef '=' (typeExpr | STRING)`。  
  - 例：`type Point = { x: int, y: int }`、`type Signal = (int, int)`。

---

## 9. 顶层组成

### 9.1 程序与文件

- **program**：`SEP? (decl SEP?)* EOF`。  
- **decl**：`moduleDecl | importDecl | exportDecl | dataDecl | funcDecl | typeDecl | useDecl`。  
- 完整程序由若干声明组成；执行从入口（如 `main`）开始，由调度器（线性或并行）执行图或字节码。

### 9.2 文件输入与交互

- 实现可支持从文件读入源码、或交互式输入；具体由 CLI 与运行时约定。

---

## 10. 文法概要

以下与 `antlr/OpenCML.g4`、`OpenCMLLex.g4` 对应，仅作提纲；精确语法以文法文件为准。

- **程序**：`program : SEP? (decl SEP?)* EOF`。  
- **声明**：`decl`：moduleDecl | importDecl | exportDecl | dataDecl | funcDecl | typeDecl | useDecl。  
- **语句**：`stmt`：dataDecl | funcDecl | typeDecl | dataExpr | useDecl | retStmt | blockStmt。  
- **块**：`stmtBlock : SYNC? '{' stmtList? '}'`；`blockStmt : WAIT? stmtBlock`。  
- **表达式层次**（从高到低）：  
  waitExpr → assignExpr → logicalOrExpr → logicalAndExpr → equalityExpr → relationalExpr  
  → additiveExpr → multiplicativeExpr → nullableExpr → unaryExpr → linkExpr → compExpr  
  → annoExpr → accessExpr → primaryData。  
- **主表达式**：identRef | literal | arrayData | structData | '(' dataExpr ')' | tupleData | funcData。  
- **类型**：typeExpr（含 union、inter、diff、可空 `?` 等）；arrayType；structType；tupleType；funcType；specType（含泛型 `<>`）。  
- **模式**：identRef | literal | '(' ... ')' | '{' ... '}' | '_'。  

关键字与字面量见 2.3、2.4；运算符与分隔符见 2.5。

---

## 参考文献与相关文档

- **详细语言参考**：[docs/language/](../language/README.md) — 按主题分章、含大量示例的详细文档目录。
- **技术文档**：01_overview.md（总览）、02_language_spec.md（规范摘要）、03_trees_and_structures.md（CST/AST/GCT/GIR）、04_compilation_pipeline.md（流水线）、05_graph_ir.md（GIR）、06_runtime_and_execution.md（运行时与调度）。
- **文法与 AST**：antlr/OpenCML.g4、OpenCMLLex.g4；docs/Camel.asdl。
- **用例**：test/run/linear/、test/run/para/、test/run/nn/ 等目录下的 `.cml` 文件。
