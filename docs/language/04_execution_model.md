# 4. 执行模型

本章描述 Camel 的**程序结构**、**命名与绑定**、**异常与退出**、以及 **sync** 与 **wait** 的语义。

---

## 4.1 程序结构

- 程序由**模块**组成；每个源文件是一个**模块**。
- 顶层可包含：
  - **module** 声明
  - **import** / **export**
  - **类型定义**（type）
  - **数据声明**（let / var）
  - **函数定义**（func）
  - **use** 声明
  - 以及可执行语句（表达式、return 等）
- **执行入口**由实现约定，通常为名为 **`main`** 的函数；可带参数与返回值，如 **`func main(): int sync { ... }`**。

**示例：典型顶层结构**

```cml
module merge_sort

import { now } from time
import { seed, rand, shuffle } from random
import { sort, sorted, merge_sorted_arrays } from algo

func merge(lhs: int[], rhs: int[]): int[] sync { ... }
func mergesort(arr: int[]): int[] sync { ... }

func main(): int sync {
    let n = 1e8
    seed(s)
    let arr = range(0, n)->shuffle
    let sorted = mergesort(arr)
    return 0
}
```

---

## 4.2 命名与绑定

### 4.2.1 作用域

- **模块顶层**、**函数参数**、**块**（`{ }`）各自引入作用域。
- 内层可**遮蔽**外层同名；同一作用域内同一名字不能重复 **let**（同一名字不能重复声明为 let）。

### 4.2.2 let：不可变绑定

- 在当前作用域引入一个名字，绑定到初值；该绑定**不可重新赋值**。

```cml
let idx = 30
let WIDTH = 20
let start = now()
let res = fib(idx)
let duration = now() - start
let initial_snake = [{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }]
```

### 4.2.3 var：可变变量

- 引入可**重新赋值**的变量；支持复合赋值（如 **`+=`**）。

```cml
var c = 0
var result: Point[] = []
sync func update(var w: Tensor): Tensor {
    ...
    return w = w - lr * dw
}
```

### 4.2.4 import：从模块引入符号

- **`import { a, b } from mod`**：将模块 **mod** 中的 **a**、**b** 引入当前模块。
- **`import x from mod`**：将 **mod** 的默认导出或整体以 **x** 引入（依实现）。
- 引入的名字在**当前模块**可见。

```cml
import { now } from time
import { set_terminal_raw_mode, has_input, get_char, clear_input_buffer } from os
import { zen } from this
import { Tensor, ones, zeros, sum, transpose, shape, concat, random } from tensor
```

### 4.2.5 with：上下文/泛型式参数

- 在函数上声明 **with** 参数，调用时通过 **`func<with_args>`** 传入；函数体内直接使用对应名字。
- 用于“泛型”或“依赖注入”式写法。

```cml
with <predicate: (p: Point) => bool>
func my_filter(points: Point[]): Point[] sync { ... }

// 调用时传入 predicate
points->my_filter<(p: Point): bool => {
    return p.x >= 0 && p.x < WIDTH && p.y >= 0 && p.y < HEIGHT
}>

with <weights: Tensor>
func predict(X: Tensor): Tensor { ... }

with <X: Tensor, y: Tensor, lr: float, its: int>
sync func train(var weights: Tensor): void { ... }
```

---

## 4.3 异常与退出

### 4.3.1 return

- **`return e1, e2, ...`**：返回多个值（dataList）；或 **`return`** 无值返回。
- 可返回 **`null`** 表示“无有效值”（如某些 sync 分支）。

```cml
return 0
return null
return if n <= 1 then n else fib(n - 1) + fib(n - 2)
return result
return [res1, res2]
```

### 4.3.2 raise / throw

- **`raise expr`** / **`throw expr`**：引发异常；语法为 **retStmt**：**RETURN | RAISE | THROW** + **dataList**。
- 具体异常类型与传播由实现定义。

### 4.3.3 try-catch-finally

- **`try stmtBlock catchClause+ (finally stmtBlock)?`**。
- **catchClause**：**`catch identDef ':' typeExpr stmtBlock`**，捕获指定类型的异常并执行对应块。

```cml
try {
    // ...
} catch e: SomeError {
    // 处理
} finally {
    // 清理
}
```

---

## 4.4 同步与等待

### 4.4.1 sync 块

- **`sync { ... }`** 或函数标注 **`sync`**（如 **`func main(): int sync { ... }`**）表示块内**顺序执行、显式同步**。
- 常用于 I/O、终端控制、需要严格顺序的代码。

```cml
func main(): int sync {
    println('Hello, Camel!')
    return 0
}

func render(snake: Point[]): void sync {
    print('\033[2J\033[H')
    let rows = range(0, HEIGHT)
    rows->foreach<(y: int) => sync {
        let cols = range(0, WIDTH)
        cols->map<(x: int): string => { ... }>->join<"">->println
    }>
    println("")
}
```

### 4.4.2 wait

- 在 **sync** 块内，**`wait expr`** 表示**等待该表达式求值完成**再继续。
- 用于与异步或副作用操作配合（如更新可变引用、等待 I/O）。

```cml
sync func update(var target: int) {
    wait target += 1
}

func main(): int sync {
    var c = 0
    wait update(c)
    println(c)
    return 0
}
```

```cml
with <X: Tensor, y: Tensor, lr: float>
sync func update(var w: Tensor): Tensor {
    let pred = X @ w
    let errors = pred - y
    let dw = transpose(X) @ errors
    let m = shape(X)[0]
    wait dw = 2.0 / float(m) * dw
    return w = w - lr * dw
}
```
