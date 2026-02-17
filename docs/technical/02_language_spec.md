# Camel 语言规范

本文档描述 Camel 语言的**语法与语义**，从用户可见的源码与行为角度归纳，不涉及编译器内部树结构。示例与用法参考项目内 **test/** 下的用例（如 `test/run/linear/`）。

---

## 1. 模块与顶层结构

- 文件以**模块**为单位。可声明模块名：`module <名称>`（如 `module fib`、`module main`）。
- 顶层可包含：
  - **import**：从其他模块引入符号，如 `import { now } from time`、`import { set_terminal_raw_mode, has_input, get_char } from os`。
  - **export**：导出符号（可选）。
  - **类型定义**、**常量/变量**、**函数**（见下）。

---

## 2. 类型定义

- **类型别名**：`type <名称> = <类型>`。
  - 结构体字面类型：`type Point = { x: int, y: int }`。
  - 元组类型：`type Signal = (int, int)`。
- 类型可在函数签名、`let`/`var` 声明中用作注解。

---

## 3. 数据声明

- **不可变绑定**：`let <名称> = <表达式>` 或 `let <名称>: <类型> = <表达式>`。
  - 例：`let idx = 30`、`let WIDTH = 20`、`let start = now()`。
- **可变变量**：`var <名称> = <表达式>` 或 `var <名称>: <类型> = <表达式>`。
  - 例：`var c = 0`、`var result: Point[] = []`。
- 顶层常量常用于配置（如 `WIDTH`、`HEIGHT`）。

---

## 4. 函数

- **声明**：`func <名称>(<参数列表>): <返回类型> <块>` 或无返回类型（void）。
  - 参数：`<名称>: <类型>`，多个用逗号分隔，如 `func fib(n: int): int { ... }`、`func move_snake(snake: Point[], dir: string): Point[]`。
- **sync 块**：在函数签名后或块前写 `sync`，表示该块内顺序执行、显式同步，如 `func main(): int sync { ... }`、`func render(snake: Point[]): void sync { ... }`。
- **with 参数**（上下文/泛型式参数）：在函数前用 `with <名称: 类型, ...>` 声明，函数体内使用该名称。
  - 例：`with <predicate: (p: Point) => bool> func my_filter(points: Point[]): Point[] sync { ... }`。
- **return**：`return <表达式>` 或 `return`（void）；可返回 `null` 表示无值（如某些 sync 分支）。

---

## 5. 表达式概览

### 5.1 字面量与引用

- 整数：`0`、`30`；字符串：`'Hello, Camel!'`、`'Fibonacci({}) = {} (computed in {:.4f} seconds)'`。
- 标识符：变量或函数名，如 `n`、`println`、`fib`、`now`、`format`。

### 5.2 调用与管道

- **普通调用**：`<函数或值>(<参数列表>)`，如 `println('Hello, Camel!')`、`now()`、`fib(idx)`。
- **管道**：`<左>-><右>` 表示将左侧作为参数传给右侧（方法或函数）。
  - 例：`'Start computing fibonacci({})...'->format<idx>->println`、`cols->map<...>->join<"">->println`。
- **泛型/显式参数**：`<名><<类型或值参数>>`，如 `format<idx>`、`format<idx, res, duration>`、`my_filter<(p: Point): bool => { ... }>`。

### 5.3 条件与分支

- **if-then-else**：`if <条件> then <表达式> else <表达式>`，或单分支。
  - 例：`return if n <= 1 then n else fib(n - 1) + fib(n - 2)`。
- **块内 if**：`if <条件> then sync { ... } else sync { ... }`，sync 块内可有多条语句。
- **match**：`match <表达式> { case <模式> => <表达式>; case _ => <表达式> }`。
  - 例：`match dir { case 'w' => { x: now_head.x, y: now_head.y - 1 }; case 'a' => ...; case _ => now_head }`。

### 5.4 算术与比较

- 二元运算：`+`、`-`、`*`、`/` 等；比较：`<=`、`<`、`>=`、`>`、`==` 等。
- 例：`n - 1`、`fib(n - 1) + fib(n - 2)`、`now() - start`、`n <= 1`、`p.x >= 0 && p.x < WIDTH`。

### 5.5 复合数据

- **结构体字面量**：`{ <字段>: <值>, ... }`，如 `{ x: 5, y: 5 }`、`{ x: now_head.x, y: now_head.y - 1 }`。
- **元组/列表**：`[(1, 2), (10, 20)]`、`[{ x: 5, y: 5 }, { x: 4, y: 5 }]`、`[]`。
- **下标**：`signals[0]`、`snake->slice<0, len(snake) - 1>`；**字段访问**：`signal.0`、`signal.1`、`p.x`、`now_head.x`。

### 5.6 异步与同步

- **wait**：在 sync 块内 `wait <表达式>` 表示等待该表达式完成再继续，如 `wait update(c)`。
- **sync 块**：用 `sync { ... }` 或函数标注 `sync`，块内顺序执行，便于 I/O 或显式控制顺序。

### 5.7 Lambda 与高阶

- **Lambda**：`(参数) => <表达式>` 或 `(参数) => sync { ... }`，类型可写为 `(p: Point) => bool`、`(y: int) => sync { ... }`。
- 例：`points->foreach<(p: Point) => sync { ... }>`、`cols->map<(x: int): string => { ... }>`、`points->my_filter<(p: Point): bool => { return p.x >= 0 && ... }>`。

---

## 6. 语句与块

- **表达式语句**：单独一行表达式，如 `println(...)`、`render(snake)`、`game_loop(...)`。
- **return**：`return <表达式>` 或 `return null`。
- **块**：`{ <语句列表> }`；可冠以 `sync` 表示同步块。

---

## 7. 注释与风格

- 单行注释：`// ...`。
- 字符串可用单引号；管道与泛型调用使链式调用简洁（如 `'...'->format<...>->println`）。

---

## 8. 与实现的对应关系（简述）

- 上述语法经**词法/语法分析**得到**具象语法树（CST）**，再得到**抽象语法树（AST）**。
- AST 经**图构造树（GCT）** 转为**图中间表示（GIR）**；函数体对应图，表达式与控制流对应节点与边。
- 类型与算子信息用于类型检查与运行时分发。各树与 GIR 的详细结构属实现细节，见《各种树与中间结构》文档。
