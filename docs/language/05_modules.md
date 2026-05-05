# 5. 模块与导入

本章描述 Camel 的**模块声明**、**导入**、**导出**、**命名空间**以及**模块名**的写法，并配有从测试与标准库抽取的示例。

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

### 5.3.2 裸模块导入

- **`import moduleName`**：导入模块命名空间，不把导出符号直接注入当前模块。
- 导入后可通过 **`moduleName::symbol`** 或 **`moduleName::Namespace::symbol`** 访问导出符号。
- 若模块名是点分名，默认命名空间名取最后一段；例如 `import std.math` 后可按 `math::sqrt(...)` 访问。

```cml
import tensor

func main(): int sync {
    let a = tensor::Tensor::ones([2, 2])
    let b = tensor::shape(a)[0]
    return 0
}
```

裸导入不会直接注入符号；例如 `import tensor` 后直接写 `ones([2, 2])` 不是合法引用，除非再显式使用 `using namespace tensor`。

### 5.3.3 使用命名空间（using namespace）

- **`using namespace identRef`**：将已导入命名空间中的导出符号注入当前模块。
- 可用于模块命名空间，也可用于嵌套命名空间。

```cml
import tensor
using namespace tensor

func main(): int sync {
    let a = Tensor::ones([2, 2])
    let b = ones([2, 2])
    return 0
}
```

```cml
import mylib
using namespace mylib::Tools

func main(): int sync {
    return inc(3)
}
```

### 5.3.4 单符号导入

- **`import identDef from moduleName`**：将模块导出的单个符号引入当前模块。
- **`import { a, b } from moduleName`** 是更常用的多符号形式。

---

## 5.4 命名空间（namespace）

- **`namespace Name { ... }`**：声明一个命名空间块。
- 命名空间块内可以声明 `func`、`type`、嵌套 `namespace`、`import`、`export` 和 `using namespace`。
- 命名空间块内声明的函数与类型会降低为限定引用，例如 `namespace Tensor { func new(...) ... }` 对应 `Tensor::new`。
- 用户定义类型和内置类型都可以通过 **`Type::function`** 的形式访问关联构造器或静态工具函数。

```cml
type Foo = {
    x: int
}

namespace Foo {
    func new(x: int): Foo {
        return { x: x }
    }
}

func main(): int sync {
    let f = Foo::new(7)
    return f.x
}
```

内置模块也可以导出类型命名空间：

```cml
import { Tensor } from tensor
import { Parameter } from nn

func main(): int sync {
    let a = Tensor::ones([2, 2])
    let p = Parameter::new(Tensor::zeros([2, 2]))
    return Tensor::shape(Parameter::value(p))[0]
}
```

---

## 5.5 导出（export）

- **`export (dataDecl | typeDecl | bracedIdents)`**：使符号可被其他模块通过 **import** 使用。
- 可在**声明前**加 **export**，或 **export** 后跟花括号内的名字列表。
- 在命名空间内导出的符号保留限定名，例如 `namespace Tools { export func inc(...) ... }` 导出为 `Tools::inc`。

**示例（与文法对应）**

```cml
export type Point = { x: int, y: int }
export func fib(n: int): int { ... }
export { foo, bar }
```

```cml
namespace Models {
    export type Box = {
        x: int
    }

    export func new(x: int): Models::Box sync {
        return { x: x }
    }
}
```

---

## 5.6 完整示例：多模块协作

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
