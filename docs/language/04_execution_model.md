# 4. 执行模型

本章描述 Camel 的**程序结构**、**命名与绑定**、**异常与退出**，以及**执行模型**：依赖驱动求值、**sync**（块修饰符与函数修饰符）、引用与 **var** / **let**、**wait**、**macro**（见 4.4）。

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
- `var` 标记可变对象，`let` 标记不可变对象；一切值按引用传递，`var` 形参是引用（见 4.4.4）。

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
- **`import mod`**：导入模块命名空间，但不把模块导出的符号直接注入当前模块；应通过 **`mod::name`** 或 **`mod::Namespace::name`** 访问。
- **`using namespace mod`**：将已导入的模块命名空间中的导出符号注入当前模块。
- **`using namespace mod::Namespace`**：只注入某个嵌套命名空间下的导出符号。
- 显式列出的名字在**当前模块**可见；裸 `import mod` 本身不使 `name` 直接可见。

```cml
import { now } from time
import { set_terminal_raw_mode, has_input, get_char, clear_input_buffer } from os
import { zen } from this
import { Tensor, ones, zeros, sum, transpose, shape, concat, random } from tensor
import tensor
let a = tensor::Tensor::ones([2, 2])
using namespace tensor
let b = Tensor::ones([2, 2])
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

## 4.4 执行模型：依赖驱动、sync、引用与 wait

本节是语言规则。当前实现与规则不一致之处在各小节末尾以“实现现状”标出，并随实现跟进而删除。

### 4.4.1 依赖驱动求值

- 表达式被求值，当且仅当它的结果被需要：经数据依赖或控制依赖到达函数的返回。
- 因此，**同步块之外、结果未被使用的效应调用不会执行**。这是合法的，是依赖驱动求值的自然结果；
  编译器在能够检测到时给出告警。

```cml
func f(): int {
    println("hello")   // 结果未被使用，且不在同步块内：不执行（可能有告警）
    return 1
}
```

> 实现现状：行为与规则一致。告警尚未实现为对所有不纯算子给出，且没有 Error 的模块目前不显示告警。

### 4.4.2 sync 块（块修饰符）

- **`sync { ... }`** 是同步块：块内表达式**按书写顺序执行**，等价于编译器在相邻表达式之间插入控制边。
- 把同步块用作函数体写作 **`func f(...): T sync { ... }`**（`sync` 在返回类型之后、函数体之前）。
- 常用于 I/O、终端控制等需要严格顺序的代码。

```cml
func main(): int sync {
    println('Hello, Camel!')   // 同步块内：执行，且先于 return
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

### 4.4.3 sync func（函数修饰符）

- **`sync func f(...)`** 中的 `sync` 修饰的是**函数**：它声明该函数有副作用、需要被同步，调用方可能需要
  `wait` 它。带 `var` 形参的函数必须这样声明。
- 它与 4.4.2 的块修饰符是**两个不同的东西**：`sync func` **不会**使函数体成为同步块。函数体若不是同步块，
  体内仍按 4.4.1 的依赖驱动规则求值。
- 需要函数体按书写顺序执行时，把函数体写成同步块；二者可以同时使用。

```cml
// 只有函数修饰符：函数体不是同步块，println 不执行（设计如此）
sync func f(): int {
    println("hello")
    return 1
}

// 函数修饰符 + 同步块函数体：println 执行
sync func g(): int sync {
    println("hello")
    return 1
}
```

### 4.4.4 引用、var 与 let

- Camel 中一切值**默认按引用传递**。
- **`var`** 标记可变对象，**`let`** 标记不可变对象。
- `var` 形参是引用：函数对它的写入就是对调用方变量的写入。
- 被 `if` 分支或 lambda 捕获的变量按引用捕获：在其中的写入作用于外层变量。
- 把 `let` 对象传给 `var` 形参是**编译期错误**。
- 同一个变量传给两个 `var` 形参时，两个形参是同一个对象的别名。

```cml
sync func update(var target: int) {
    wait target += 1
}

func main(): int sync {
    var c = 0
    wait update(c)
    println(c)        // 1
    return 0
}
```

```cml
let k = 0
wait update(k)        // 错误：k 是 let（不可变），不能传给 var 形参
```

> 实现现状：`var` 形参目前以“调用返回时回写”实现，带 `var` 形参的函数因此不做尾调用、不进入 JIT；
> 捕获变量的写入尚未穿透到外层变量；`let` 对象传给 `var` 形参尚未被拒绝；同一变量传给两个 `var` 形参的
> 行为尚无保证。以上均为待实现项，规范以本节正文为准。

### 4.4.5 wait

- **`wait`** 作用于它后面的**那一个表达式**。
- 含义：`wait` 前后的操作，如果用到了该表达式的值，或者用到了被它修改的值（也就是用到了它的副作用），
  就与它**局部有序**。编译器自动跟踪带可变输入参数（`var` 形参）的调用的对外副作用，并插入控制依赖。
  “默认按引用”的设计正是为此。
- 在同步块内写不写 `wait` 都一样：块内本来就有顺序（见 4.4.2）。副作用只在默认的无序执行下才需要考虑，
  因为它限制了哪些操作不能被随意调换顺序。

```cml
A(i)
wait modify(i)     // 编译器插入控制依赖：A → modify → B
B(i)
```

> 实现现状：“之后”的依赖（`modify → B`）经 `var` 实参的修饰链建立；“之前”的依赖（`A → modify`）是否在所有
> 情形下都被建立，尚待按本例逐项核对并补充测试。副作用的管理与追踪仍在研究中，按引用捕获、`wait`
> 跟踪与各个遍之间的相互作用尚不完备。

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

### 4.4.6 let 是编译期约束

- `let` 对象严格不可修改。这是**编译期校验**，不是对内存加锁：会修改变量的操作本身是 `sync` 的，并且必须
  明确声明哪个形参是 `var`；`let` 变量不能传给 `var` 形参，所以无从修改。运行期不做额外检查。
- 闭包（lambda）与 `if` 分支对变量的捕获同样是按引用。

### 4.4.7 遍流水线与默认流水线

- 程序由命令行上的**遍序列**处理：`camel file.cml pass1 pass2 ...`。每个遍是对图做变换、调度执行或转译的
  程序。
- **语言核心对遍的顺序不做限制**。不同的顺序**可以**给出不同的结果：遍并不承诺产出同一张图，它会修改图，
  并且可以有副作用。“任意遍序列下行为相同”不是语言的性质。
- 不指定任何遍时，执行 `std::default`，它是 `std::nodevm`（节点虚拟机）的别名；可由 `.opencmlrc` 的
  `passes.fallback` 改变。指定的遍没有消费掉图时，同样接着执行这个默认遍。
- 随 Camel 发布的**标准优化遍**（`std::devirtualize`、`std::specialize`、`std::inline`、`std::opt::fold`、
  `std::opt::cse`、`std::opt::dce`、`std::opt::simplify`、`tensor::fuse` 等）以保持程序结果为目标；这是对
  这些遍的质量要求，由测试检验，不是语言层面的约定。
- 建立在遍机制之上的库可以有自己的前提。例如自动微分库（`autodiff`）的 `grad` 需要流水线先运行
  `std::macro`：没有执行过 macro 的图信息不足以做微分。这是该库的约束，不是语言核心的约束；前提不满足时
  库应当给出清楚的、带源码位置的诊断。

### 4.4.8 macro 函数

- 标记为 **`macro`** 的函数可以被可选的 **`std::macro`** 遍提前求值，结果物化进图；不使用该遍时，
  它与普通函数一样在运行期执行。
- 带效应的 macro 函数体与普通函数完全一样：它的效应在它被求值的时候发生（使用 `std::macro` 时即在该遍
  执行期间）。这是设计如此。
- **求值失败的 macro 调用**（暂定规则，尚未最终确定）：在使用 `std::macro` 时是编译期错误，直接报告调用
  位置与原因。后果是：一个失败的 macro 调用即使在运行期永远不会被执行到，程序也会被拒绝。

> 实现现状：目前求值失败只给出告警（`MacroEvaluationDeferred`）并推迟到运行期；改为编译期错误是待实现项。
