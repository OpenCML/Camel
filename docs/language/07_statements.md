# 7. 语句

本章描述 Camel 的**简单语句**与**复合语句**：表达式语句、return（raise/throw 尚未实现）、let/var、use、using namespace、块、if、match、try（尚未实现）、函数定义、类型定义，并配有完整程序片段示例。

---

## 7.1 简单语句

### 7.1.1 表达式语句

- 一条 **dataExpr** 可作为语句；其值可被丢弃。

**示例**

```cml
println('Hello, Camel!')
println(c)
println('')
render(snake)
game_loop(initial_snake, 'w', 0)
'Start computing fibonacci({})...'->format<idx>->println
'Fibonacci({}) = {} (computed in {:.4f} seconds)'->format<idx, res, duration>->println
'Analyzer: {}'->format<signals>->println
'Game over! You hit the wall after {} steps.'->format<steps>->println
'MergeSorted {} elements in {:.2f} seconds.'->format<len(arr), duration>->println
'output: {:.6f}'->format<output>->println
print('\033[2J\033[H')
set_terminal_raw_mode(true)
seed(s)
```

### 7.1.2 return / raise / throw 语句

- **retStmt**：**`(return | raise | throw) dataList?`**。
- **`raise` / `throw` 未实现**：语法接受，但异常机制没有实现，使用它是编译期错误
  `SemanticDiag::ConstructNotSupported`（它们不再被当作 `return` 编译）。
- **return** 可带多个表达式，或省略表示无值返回；可返回 **null**。

**示例**

```cml
return 0
return null
return n
return result
return res
return if n <= 1 then n else fib(n - 1) + fib(n - 2)
return [res1, res2]
return merge_sorted_arrays(lhs, rhs)
return concat([new_head], snake->slice<0, len(snake) - 1>)
return (signal.0 * 2, signal.1 * 2)
return sigmoid(z)
return dot(x, w) + b
return total_squared_error / float(num_of_samples)
```

### 7.1.3 数据声明（let / var）

- **dataDecl**：**`(let | var) carrier (':' typeList)? '=' dataList`**。
- **carrier**：单个标识符、或括号/花括号/方括号内的标识符列表（解构）。
- **dataList**：一个或多个表达式，与 carrier 结构对应。

**let：不可变绑定**

```cml
let idx = 30
let WIDTH = 20
let HEIGHT = 10
let start = now()
let res = fib(idx)
let duration = now() - start
let n = 100000
let a = 1
let d = 3
let signals: Signal[] = [(1, 2), (10, 20)]
let initial_snake = [{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }]
let input = [1.0, 2.0, 3.0]
let weights = [0.1, 0.2, 0.3]
let bias = 0.5
let cleaned = valid_points(snake)
let rows = range(0, HEIGHT)
let cols = range(0, WIDTH)
let now_head = head(snake)
let new_head = match dir { ... }
let new_snake = move_snake(snake, dir)
let mid = len(arr) / 2
let lhs = arr->slice<0, mid>->mergesort
let rhs = arr->slice<mid, len(arr)>->mergesort
let m = shape(X)[0]
let n = shape(X)[1]
```

**var：可变变量**

```cml
var c = 0
var result: Point[] = []
```

### 7.1.4 use 语句

- **useDecl**：**`use (identDef '=')? identRef`**；将某符号（或模块）以可选别名引入当前作用域（具体语义依实现）。

### 7.1.5 using namespace 语句

- **usingNamespaceDecl**：**`using namespace identRef`**；将已导入命名空间中的导出符号注入当前作用域。

```cml
import tensor
using namespace tensor
```

---

## 7.2 复合语句

### 7.2.1 块

- **stmtBlock**：**`sync? '{' stmtList? '}'`**。
- **blockStmt** 可为 **`wait? stmtBlock`**，即块前可带 **wait**。
- 带 `sync` 的块是**同步块**：块内表达式按书写顺序执行。不带 `sync` 的块按依赖驱动求值：结果未被使用的效应调用不执行（见第 4 章 4.4.1、4.4.2）。

**普通块**

```cml
{
    let a = 1
    let b = 2
    return a + b
}
```

**同步块**

```cml
sync {
    println('Hello')
    return 0
}
```

**函数体块（sync）**

```cml
func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

### 7.2.2 if 语句

- **`if ... then blockExpr (else blockExpr)?`**；then/else 可为块或单表达式。
- 见 [6.9 控制表达式](06_expressions.md#69-控制表达式)。

**示例**

```cml
if hit_wall(now_head) then sync {
    'Game over! You hit the wall after {} steps.'->format<steps>->println
    return null
} else sync {
    sleep(300)
    let d = if has_input() then sync {
        let ch = get_char()
        clear_input_buffer()
        return if ['w', 'a', 's', 'd']->contains<ch> then ch else dir
    } else dir
    game_loop(new_snake, d, steps + 1)
}
```

### 7.2.3 match 语句

- **`match identRef '{' matchCase+ '}'`**；见 [6.9.2 match](06_expressions.md#692-match)。

**示例**

```cml
match dir {
    case 'w' => { x: now_head.x, y: now_head.y - 1 }
    case 'a' => { x: now_head.x - 1, y: now_head.y }
    case 's' => { x: now_head.x, y: now_head.y + 1 }
    case 'd' => { x: now_head.x + 1, y: now_head.y }
    case _ => now_head
}
```

### 7.2.4 try 语句（未实现）

> **未实现。** 语法接受该写法，但异常机制（抛出、传播、捕获）没有实现：使用它是编译期错误 `SemanticDiag::ConstructNotSupported`（定位在该语句或 `try` 处）。

- **`try stmtBlock catchClause+ (finally stmtBlock)?`**；**catchClause**：**`catch identDef ':' typeExpr stmtBlock`**。
- 见 [6.9.3 try](06_expressions.md#693-try)。

### 7.2.5 函数定义

- **funcDecl**：**`(with angledParams)? export? implMark? modifiers? func identRef parentParams (':' typeExpr)? stmtBlock`**。
- **implMark**：**inner** | **outer**。**未实现**：能被解析并记录，编译器不读取，没有语义。
- **modifiers**：**atomic**、**shared**、**sync**、**macro** 等组合。其中 **atomic**、**shared** **未实现**：能被
  解析并记录在函数类型上，没有任何语义；有语义的只有 **sync**（函数修饰符）和 **macro**。
- 写在 `func` 之前的 **`sync`** 是函数修饰符：声明函数有副作用、需要被同步（带 `var` 形参的函数必须如此声明）；它**不会**使函数体成为同步块。要让函数体按书写顺序执行，把函数体写成同步块：`func f(): T sync { ... }`，或两者并用 `sync func f(): T sync { ... }`（见第 4 章 4.4.2、4.4.3）。
- `var` 形参是引用；把 `let` 对象传给 `var` 形参是编译期错误（见第 4 章 4.4.4）。
- **parentParams**：**`'(' pairedParams? ','? ')'`**；**keyParamPair**：**`var? identDef ':' typeExpr ('=' dataExpr)?`**。

**无参、带返回类型与 sync**

```cml
func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

**单参数、返回值**

```cml
func fib(n: int): int {
    return if n <= 1 then n else fib(n - 1) + fib(n - 2)
}
```

**多参数、返回数组**

```cml
func move_snake(snake: Point[], dir: string): Point[] {
    let now_head = head(snake)
    let new_head = match dir { ... }
    return concat([new_head], snake->slice<0, len(snake) - 1>)
}
```

**with 参数、sync、可变引用参数**

```cml
with <predicate: (p: Point) => bool>
func my_filter(points: Point[]): Point[] sync {
    var result: Point[] = []
    points->foreach<(p: Point) => sync {
        if predicate(p) then sync {
            result->append<p>
            return null
        }
    }>
    return result
}
```

**with 多个参数、sync、var 参数**

```cml
with <X: Tensor, y: Tensor, lr: float, its: int>
sync func train(var weights: Tensor): void {
    let m = shape(X)[0]
    let n = shape(X)[1]
    let ones_col = ones([m, 1])
    let X_bias = concat(X, ones_col, 1)
    // ...
}
```

**无返回类型（void）**

```cml
func render(snake: Point[]): void sync {
    print('\033[2J\033[H')
    let rows = range(0, HEIGHT)
    rows->foreach<(y: int) => sync {
        let cols = range(0, WIDTH)
        cols->map<(x: int): string => {
            let p = { x: x, y: y }
            return if snake->contains<p> then "O" else "."
        }>->join<"">->println
    }>
    println("")
}
```

### 7.2.6 类型定义

- **typeDecl**：**`implMark? type identRef '=' (typeExpr | STRING)`**。

**结构体类型别名**

```cml
type Point = {
    x: int,
    y: int
}
```

**元组类型别名**

```cml
type Signal = (int, int)
```

---

## 7.3 完整程序片段示例

**最小程序（hello.cml）**

```cml
module main

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

**斐波那契（fib.cml 精简）**

```cml
module fib

import { now } from time

func fib(n: int): int {
    return if n <= 1 then n else fib(n - 1) + fib(n - 2)
}

func main(): int sync {
    let idx = 30
    'Start computing fibonacci({})...'->format<idx>->println
    let start = now()
    let res = fib(idx)
    let duration = now() - start
    'Fibonacci({}) = {} (computed in {:.4f} seconds)'->format<idx, res, duration>->println
    return 0
}
```

**元组与管道（index.cml 精简）**

```cml
type Signal = (int, int)

func process(signals: Signal[]) sync {
    let res1 = signals[0]->comp1
    let res2 = signals[1]->comp2
    return [res1, res2]
}

func comp1(signal: Signal): Signal {
    return (signal.0 * 2, signal.1 * 2)
}

func main() {
    let signals: Signal[] = [(1, 2), (10, 20)]
    signals->process->analyzer
}
```

**归并排序主逻辑（msort.cml 片段）**

```cml
func mergesort(arr: int[]): int[] sync {
    let start = now()
    let res = if len(arr) <= 1e5 then sorted(arr) else {
        let mid = len(arr) / 2
        let lhs = arr->slice<0, mid>->mergesort
        let rhs = arr->slice<mid, len(arr)>->mergesort
        return merge(lhs, rhs)
    }
    let duration = now() - start
    println('MergeSorted {} elements in {:.2f} seconds.'->format<len(arr), duration>)
    return res
}
```
