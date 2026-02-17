# 5. 模块与导入

本章描述 Camel 的**模块声明**、**导入**、**导出**以及**模块名**的写法，并配有从测试与标准库抽取的示例。

---

## 5.1 模块声明

- **`module <名称>`**：声明当前文件的模块名。
- 模块名为**标识符**或**点分名**（见下节）。

**示例**

```cml
module main
```

```cml
module fib
```

```cml
module arithmetic_sum
```

```cml
module merge_sort
```

```cml
module snake
```

```cml
module lambda
```

```cml
module linear_regression
```

---

## 5.2 模块名（moduleName）

- 文法：**(`.` | `..` | `...`)? IDENTIFIER (`.` IDENTIFIER)***。
- 即：可选前导 **`.`** / **`..`** / **`...`**，后接一个或多个由点连接的标识符。

**常见写法示例**

```cml
time
os
this
tensor
algo
random
math
```

- **`this`** 表示当前模块，用于自引用（如 `import { zen } from this`）。

---

## 5.3 导入（import）

- **`import (moduleName | (identDef | bracedIdents) from moduleName)`**。
  - **bracedIdents**：**`{ identList? ','? }`**，即花括号内的标识符列表。

### 5.3.1 从模块引入多个符号

```cml
import { now } from time
```

```cml
import { set_terminal_raw_mode, has_input, get_char, clear_input_buffer } from os
```

```cml
import { zen } from this
```

```cml
import { seed, rand, shuffle } from random
```

```cml
import { sort, sorted, merge_sorted_arrays } from algo
```

```cml
import {
    Tensor,
    ones, zeros, sum, transpose, shape, concat, random
} from tensor
```

```cml
import { exp } from math
```

### 5.3.2 单符号或默认导入

- **`import identDef from moduleName`**：将模块的某个默认导出或整体以 **identDef** 引入（具体语义依实现）。

---

## 5.4 导出（export）

- **`export (dataDecl | typeDecl | bracedIdents)`**：使符号可被其他模块通过 **import** 使用。
- 可在**声明前**加 **export**，或 **export** 后跟花括号内的名字列表。

**示例（与文法对应）**

```cml
export type Point = { x: int, y: int }
export func fib(n: int): int { ... }
export { foo, bar }
```

（实际测试中多依赖标准库与内置符号，顶层 export 示例较少；上述为语法允许形式。）

---

## 5.5 完整示例：多模块协作

**主模块（入口）**

```cml
module main

import { zen } from this

func main() sync {
    println(zen())
    return 0
}
```

**带时间与格式的脚本**

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

**使用 OS 与时间的应用**

```cml
module snake

import { now } from time
import { set_terminal_raw_mode, has_input, get_char, clear_input_buffer } from os

// ... 类型与函数定义 ...

func main(): int sync {
    set_terminal_raw_mode(true)
    let initial_snake = [{ x: 5, y: 5 }, { x: 4, y: 5 }, { x: 3, y: 5 }]
    game_loop(initial_snake, 'w', 0)
    return 0
}
```

**数值与算法库**

```cml
module merge_sort

import { now } from time
import { seed, rand, shuffle } from random
import { sort, sorted, merge_sorted_arrays } from algo

func merge(lhs: int[], rhs: int[]): int[] sync {
    return merge_sorted_arrays(lhs, rhs)
}

func mergesort(arr: int[]): int[] sync { ... }
func main(): int sync { ... }
```
