# Camel 入门教程

[English](tutorial.en.md) | 中文

本教程写给会一门主流编程语言、没见过 Camel 的读者。读完之后你能写出并运行通用的 Camel 程序，知道它与别的语言最不一样的地方在哪里，并且会用命令行上的遍去看图、优化、换后端、导出与求梯度。

每一节的程序都在 [`examples/`](examples/) 目录里，文中的输出是它们的真实输出。命令都从仓库根目录执行。

Camel 是图原生的通用编程语言：程序只表达计算意图——要算什么，谁依赖谁；语言里的每个元素直接对应计算图上的一块结构。读的时候请一直带着这个问题：**这段代码在图上是什么？**

目录：
[1 第一个程序](#1-第一个程序) ·
[2 值与类型](#2-值与类型) ·
[3 函数](#3-函数) ·
[4 分支与 match](#4-分支与-match) ·
[5 数组与高阶算子](#5-数组与高阶算子) ·
[6 按依赖求值、sync 与 wait](#6-按依赖求值sync-与-wait) ·
[7 闭包与函数值](#7-闭包与函数值) ·
[8 泛型](#8-泛型) ·
[9 错误处理](#9-错误处理) ·
[10 可空的值](#10-可空的值) ·
[11 映射与集合](#11-映射与集合) ·
[12 模块与标准库](#12-模块与标准库) ·
[13 看图](#13-看图) ·
[14 换遍](#14-换遍) ·
[15 张量与模型的导出](#15-张量与模型的导出) ·
[16 求梯度](#16-求梯度) ·
[接下来](#接下来)

---

## 1. 第一个程序

先得到 `camel` 可执行文件：按 [setup.cn.md](../setup.cn.md) 从源码构建，并把 `out/latest/bin` 加入 `PATH`。`camel --version` 能打印版本就可以开始了。

[`examples/01_hello.cml`](examples/01_hello.cml)：

```cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel docs/tutorial/examples/01_hello.cml
```

```text
Hello, Camel!
```

- 一个 `.cml` 文件是一个模块，`module hello` 给它起名（可以省略）。
- 程序从 `main` 开始。`main` 返回的 `int` 是进程的退出码。
- 返回类型后面的 **`sync`** 表示函数体是同步块：里面的语句按书写顺序执行。现在先记住“要打印的函数，函数体前写 `sync`”，第 6 节解释为什么。
- 语句之间换行即可，不需要分号。

## 2. 值与类型

[`examples/02_values.cml`](examples/02_values.cml)：

```cml
module values

type Point = { x: int, y: int }

func main(): int sync {
    let n = 42                      // int (64-bit)
    let ratio = 0.75                // float (64-bit)
    let ok = true                   // bool
    let name = 'camel'              // string
    let xs = [1, 2, 3]              // int[]
    let pair = (1, 'one')           // (int, string)
    let p: Point = { x: 3, y: 4 }   // a struct

    println(n, ratio, ok, name)
    println(xs, xs[0], len(xs))
    println(pair, pair.0, pair.1)
    println(p, p.x + p.y)

    // No implicit numeric conversion: write float(n) or int(ratio).
    println(float(n) * ratio)
    println(int(3.99), string(n) + '!')

    // let cannot be assigned again; var can.
    var count = 0
    count = count + 1
    count += 10
    println(count)
    return 0
}
```

```text
42 0.75 true camel
[1, 2, 3] 1 3
(1, one) 1 one
{ x: 3, y: 4 } 7
31.5
3 42!
11
```

要点：

- 类型是静态的，通常能推出来，不用写。基本类型是 `int`、`float`、`bool`、`string`；复合类型是数组 `T[]`、元组 `(A, B)`、结构体 `{ x: A }`。`type` 给类型起名字。
- **数值类型之间不做隐式转换。** `n * ratio` 是编译期错误，要写 `float(n) * ratio`。
- `let` 是不可变的名字，`var` 是可变的变量。Camel 崇尚不可变：大多数程序几乎用不到 `var`。
- **可以给变量赋值，也可以给它的一个字段或元素赋值**（`p.x = 5`、`xs[0] = 5` 都对）：这是给整个变量换一个改好的新值，`let a = p` 之后再改 `p`，`a` 不变。不想用 `var` 时也可以直接造新值：`{ x: 5, y: p.y }`、`concat(xs, [4])`。
- 字符串用单引号或双引号都行。字符串按字节：`len('中文')` 是 6。切分、修剪、查找等函数在 `str` 模块里（第 12 节）。
- 两个书写上的细节：调用的括号要紧贴函数名（`f(1)`，不是 `f (1)`）；一条语句写不下要换行时，把运算符留在行尾，或让下一行以 `->`、`.`、`*`、`&&` 这类只能是二元的运算符开头（以 `-`、`+` 开头的一行是新语句）。

## 3. 函数

[`examples/03_functions.cml`](examples/03_functions.cml)：

```cml
module functions

func square(x: int): int {
    return x * x
}

func fib(n: int): int {
    return if n <= 1 then n else fib(n - 1) + fib(n - 2)
}

// A tail-recursive sum: this is how Camel writes a loop.
func sum_to(n: int, acc: int): int {
    return if n == 0 then acc else sum_to(n - 1, acc + n)
}

// `with` parameters configure a function; ordinary parameters flow through it.
with <factor: int>
func scale(x: int): int {
    return x * factor
}

func main(): int sync {
    println(square(7))
    println(fib(20))
    println(sum_to(100000, 0))
    println(scale<3>(5))
    println(5->square->scale<2>)     // the same as scale<2>(square(5))
    return 0
}
```

```text
49
6765
5000050000
15
50
```

- **函数是一张子图，调用是图上的一个节点。** 第 13 节会亲眼看到。
- **Camel 目前没有循环语法**（没有 `for`、`while`）。重复的计算写成递归，或者用下一节的高阶算子。尾位置的调用不占栈帧，所以 `sum_to` 递归十万层没有问题；非尾递归（如 `fib`）同时打开的调用默认不能超过 1000 层（`--max-recursion-depth` 可调）。
- **两种参数。** 圆括号里的是函数要处理的输入；`with <...>` 里的是函数被配置的属性（上下文参数），调用时写在尖括号里。别的语言里放在对象成员里的配置，在 Camel 里是 `with` 参数。
- **管道 `->`** 把左边的值作为右边函数的实参，`a->f->g` 就是 `g(f(a))`。数据流写成一条链，正好就是图上的一条路径。

## 4. 分支与 match

[`examples/04_branches.cml`](examples/04_branches.cml)：

```cml
module branches

func sign(x: int): string {
    return if x > 0 then 'positive' else if x < 0 then 'negative' else 'zero'
}

func kind(c: string): string {
    return match c {
        case 'a' | 'e' | 'i' | 'o' | 'u' => 'vowel'
        case ' ' => 'space'
        case _ => 'other'
    }
}

func grade(score: int): string {
    let band = score / 10
    return match band {
        case 10 | 9 => 'A'
        case 8 => 'B'
        case 7 => 'C'
        case _ => 'D'
    }
}

func main(): int sync {
    println(sign(5), sign(-2), sign(0))
    println(kind('e'), kind(' '), kind('x'))
    println(grade(95), grade(81), grade(42))
    return 0
}
```

```text
positive negative zero
vowel space other
A B D
```

- `if ... then ... else ...` 是表达式，有值；两臂的类型必须相同。
- `match` 匹配的是一个**名字**（所以 `grade` 里先 `let band = ...`），模式是字面量或 `_`，一个 `case` 可以用 `|` 列多个模式。`case _` 必须写：没有默认分支的 `match` 是编译期错误（对 `bool` 写全 `true` 与 `false` 时除外）。

### 最容易掉进去的坑：块里的 `return`

在图上，分支的每个臂是它自己的一张子图。所以**臂的块里写的 `return` 给出的是这个分支的值，不是离开函数**。[`examples/04_block_return.cml`](examples/04_block_return.cml)：

```cml
module block_return

// The `if` is a statement whose value nobody uses, so the two `return`s have no effect.
func wrong(x: int): string {
    if x < 0 then { return 'negative' } else { return 'not negative' }
    return 'reached the end'
}

// The value of the `if` is what the function returns.
func right(x: int): string {
    return if x < 0 then { return 'negative' } else { return 'not negative' }
}

func main(): int sync {
    println(wrong(-1))
    println(right(-1))
    return 0
}
```

```text
reached the end
negative
```

`wrong(-1)` 得到的是 `'reached the end'`。编译器看得出这种写法，对两个 `return` 各给一条警告：

```text
warning[SemanticDiag::ExitInUnusedBranch]: This 'return' gives the value of the 'if' it is in, and nothing uses that value: it does not leave the function.
  --> docs/tutorial/examples/04_block_return.cml:7:21
   |
 7 |     if x < 0 then { return 'negative' } else { return 'not negative' }
   |                     ^^^^^^^^^^^^^^^^^
note: the value of this 'if' is not used
```

Camel 里没有“提前返回”。让分支成为函数体的最后一个表达式，或者像 `right` 那样在它前面写 `return`。

## 5. 数组与高阶算子

[`examples/05_arrays.cml`](examples/05_arrays.cml)：

```cml
module arrays

func main(): int sync {
    let xs = range(1, 11)                                     // [1, ..., 10]
    let squares = xs->map<(x: int): int => x * x>
    let evens = squares->filter<(x: int): bool => x % 2 == 0>
    let total = evens->reduce<(acc: int, x: int): int => acc + x, 0>

    println(xs)
    println(squares)
    println(evens)
    println(total)

    // Arrays are values: slice and concat return new arrays.
    let ys = concat(xs->slice<0, 3>, [99])
    println(ys, len(ys), head(ys))

    // foreach runs its function for each element, in order.
    ['a', 'b', 'c']->foreach<(s: string) => sync { println('item', s) }>
    return 0
}
```

```text
[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
[1, 4, 9, 16, 25, 36, 49, 64, 81, 100]
[4, 16, 36, 64, 100]
220
[1, 2, 3, 99] 4 1
item a
item b
item c
```

- `(x: int): int => x * x` 是 lambda。传给 `map` 的函数写在尖括号里，因为它是 `map` 的 `with` 参数：数组是流过的数据，函数是配置。
- `map`、`filter` 的各个元素之间没有先后，并行的后端可以同时算它们，结果仍按位置排列；`reduce`、`foreach` 按元素顺序执行。
- `foreach` 里的 lambda 写的是 `=> sync { ... }`：要产生效应的函数体必须是同步块。下一节解释。

### 反例：`map` 的元素函数里不要放有状态的调用

`map` 的各个元素之间**没有执行顺序的约定**。元素函数是纯的时候这无关紧要；一旦它调用了有状态的东西，结果就取决于哪个元素先算。[`examples/05_map_order.cml`](examples/05_map_order.cml) 用随机数发生器演示（`randint` 每调用一次，隐藏的状态前进一步）：

```cml
module map_order

import { seed, randint } from random

func main(): int sync {
    seed(42)
    // Wrong tool: randint advances a hidden state, so each element depends on which ran first.
    let drawn = range(0, 8)->map<(i: int): int => randint(0, 99)>
    println(drawn)

    seed(42)
    // reduce runs in element order on every back end.
    let ordered = range(0, 8)->reduce<(acc: int[], i: int): int[] => concat(acc, [randint(0, 99)]), [0]->slice<0, 0>>
    println(ordered)
    return 0
}
```

在顺序执行的后端上（默认的 `std::nvm`，以及 `std::fvm`、`std::jit`），两行相同，每次运行都一样：

```text
[75, 63, 75, 13, 90, 9, 57, 37]
[75, 63, 75, 13, 90, 9, 57, 37]
```

换成并行调度 `camel docs/tutorial/examples/05_map_order.cml std::prl`，第一行的顺序就不再有保证，每次运行可能不同；下面是其中一次的输出：

```text
[37, 57, 9, 90, 13, 75, 63, 75]
[75, 63, 75, 13, 90, 9, 57, 37]
```

这是语言规定的行为（设计原则 3.6），不是缺陷：正因为元素之间没有顺序，`map` 才能并行。第二行用的 `reduce` 按元素顺序执行，在所有后端上结果相同。规则很简单：**传给 `map`、`filter` 的函数保持纯；必须按顺序做的事用 `foreach`、`reduce` 或尾递归。**

## 6. 按依赖求值、sync 与 wait

这是 Camel 最不同于其他语言的地方，值得慢慢读。

### 6.1 没有被依赖的效应不执行

在大多数语言里，语句一条接一条执行。在 Camel 里，默认情况下**求值由依赖驱动**：一个表达式被求值，是因为有人要用它的结果。互不依赖的表达式没有先后，可以并行。

这带来一个推论：**在同步块之外，一个结果没人用的调用不会执行**，哪怕它有副作用。[`examples/06_unused_effect.cml`](examples/06_unused_effect.cml)：

```cml
module unused_effect

func greet(): int {
    println('hello from greet')     // not used, not in a sync block: never runs
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```bash
camel docs/tutorial/examples/06_unused_effect.cml
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> docs/tutorial/examples/06_unused_effect.cml:6:5
   |
 6 |     println('hello from greet')     // not used, not in a sync block: never runs
   |     ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the
      variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync
      block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
```

只打印了 `1`。`greet` 的返回值 `1` 不依赖那个 `println`，所以它在图上是一个没有人要的节点。这不是错误，是语言的规则；编译器给出警告，是为了让忘写的 `sync` 被看见。

### 6.2 同步块

写在块前的 `sync` 使它成为同步块：块里的表达式按书写顺序执行，相当于编译器在相邻的表达式之间加了控制边。[`examples/06_sync_block.cml`](examples/06_sync_block.cml) 只把 `greet` 的函数体改成同步块：

<!-- camel-snippet: compile-only: the fence is the changed function only; the text block is the output of examples/06_sync_block.cml -->

```cml
func greet(): int sync {
    println('hello from greet')
    return 1
}
```

```text
hello from greet
1
```

在同步块里调用什么、有没有副作用都无所谓，因为已经有顺序了。输入输出就写在同步块里。

### 6.3 函数的行为由它自己决定

`main` 的函数体是同步块，那它调用的函数里的 `println` 会执行吗？不会。函数编译成它自己的子图，调用只是把这个子图节点放进更大的图里，不改变它的结构。[`examples/06_callee_decides.cml`](examples/06_callee_decides.cml)：

```cml
module callee_decides

func log(x: int): int {
    println(x)          // result unused, body is not a sync block: not executed
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    println(b)          // prints 3, and nothing else
    return 0
}
```

```text
3
```

两次 `log` 调用按顺序执行了（`b` 是 3），但 `println(x)` 属于 `log` 自己的函数体，那里不是同步块（编译器照样给出 `EffectNotExecuted` 警告）。要打印，把 `log` 声明为 `func log(x: int): int sync { ... }`。

### 6.4 `sync func` 是另一回事

`sync` 还可以写在 `func` **之前**。那是函数修饰符：声明这个函数有副作用、需要被同步。它**不会**使函数体成为同步块。[`examples/06_sync_func.cml`](examples/06_sync_func.cml)：

```cml
module sync_func

sync func f(): int {
    println('f')        // still not executed
    return 1
}

sync func g(): int sync {
    println('g')        // executed: the body is a sync block
    return 2
}

func main(): int sync {
    println(f() + g())
    return 0
}
```

```text
g
3
```

前缀的 `sync` 修饰函数，后缀的 `sync` 修饰函数体这个块；两者含义不同，可以并用。

### 6.5 `var`、引用与 `wait`

Camel 里一切值按引用传递。形参前写 `var` 表示函数会修改它；修改就是写到调用方的那个变量上。把 `let` 传给 `var` 形参是编译期错误（[`examples/06_let_to_var.cml`](examples/06_let_to_var.cml)）：

```text
error[SemanticDiag::ImmutablePassedToVarParam]: 'k' is immutable and cannot be passed to a var parameter of 'bump'.
   --> docs/tutorial/examples/06_let_to_var.cml:11:10
    |
 11 |     bump(k)
    |          ^
help: Only a variable declared with 'var' (or a var parameter) can be modified. Declare it with
      'var', or bind the new value to a new name.
```

有了可变的变量，就有了顺序问题：读要排在哪次写之后？同步块是一种回答。另一种更局部的回答是 **`wait`**：它作用于后面的**一个**表达式，让用到这个表达式的值、或用到被它修改的变量的前后操作与它有序。[`examples/06_wait.cml`](examples/06_wait.cml)：

```cml
module wait_demo

sync func bump(var counter: int) {
    wait counter += 1
}

// Not a sync block: the order comes from `wait` and from the data dependencies.
func twice(): int {
    var c = 0
    wait bump(c)
    wait bump(c)
    return c
}

func main(): int sync {
    println(twice())
    return 0
}
```

```text
2
```

`twice` 的函数体不是同步块，但两次 `bump` 都执行了，并且在 `return c` 读 `c` 之前：`wait` 让编译器按对变量 `c` 的使用插入了控制边。几点补充：

- `wait` 靠 `var` 的使用来排序。被等待的表达式不涉及任何 `var`（比如只是打印）时，`wait` 没有可依据的东西；这类调用写在同步块里。
- 在同步块里写不写 `wait` 都一样。
- `wait modify(i)` 之后如果再没有人读 `i`，这次修改不执行——还是按依赖求值。

日常的建议很简单：计算写成 `let` 与纯函数；输入输出放进同步块；`var` 与 `wait` 留给确实需要原地更新的少数地方。

## 7. 闭包与函数值

[`examples/07_closures.cml`](examples/07_closures.cml)：

```cml
module closures

func inc(x: int): int { return x + 1 }
func dbl(x: int): int { return x * 2 }

func apply_twice(f: (x: int) => int, x: int): int {
    return f(f(x))
}

func adder(n: int): (x: int) => int {
    return (x: int): int => x + n
}

func main(): int sync {
    let g = inc                              // a function name is a value
    println(g(1), apply_twice(dbl, 3))

    let add5 = adder(5)                      // a closure that captured n
    println(add5(10))

    let ops = [inc, dbl]                     // functions in an array
    println(ops->map<(f: (x: int) => int): int => f(10)>)

    var total = 0                            // captured by reference
    [1, 2, 3]->foreach<sync (x: int) => sync { total = total + x }>
    println(total)
    return 0
}
```

```text
2 12
15
[11, 20]
6
```

- 函数是值：可以赋给名字、作实参、作返回值、放进数组。函数类型写作 `(x: int) => int`。
- lambda 捕获外层的变量，捕获是引用。最后一个 lambda 写入了被捕获的 `total`，所以它前面有函数修饰符 `sync`（声明有副作用），函数体又是同步块（`=> sync { ... }`）。
- 同名而形参类型不同的函数是重载；代表一组重载的名字只有在期望的函数类型能选出其中一个的位置上才能当值用（传给形参类型写明的函数），其他位置写一个 lambda 包一层。只有一个签名的库算子（如 `trim`）可以直接当值：`xs->map<trim>`。

## 8. 泛型

[`examples/08_generics.cml`](examples/08_generics.cml)：

```cml
module generics

type Pair<T> = (T, T)

func first::<T>(xs: T[]): T {
    return xs[0]
}

func swap::<T>(p: Pair<T>): Pair<T> {
    return (p.1, p.0)
}

func my_map::<T, U>(xs: T[], f: (x: T) => U): U[] {
    return xs->map<f>
}

func main(): int sync {
    println(first([1, 2, 3]))
    println(first(['a', 'b']))
    println(swap((1, 2)), swap(('x', 'y')))
    println(my_map([1, 2, 3], (x: int): string => string(x) + '!'))
    return 0
}
```

```text
1
a
(2, 1) (y, x)
[1!, 2!, 3!]
```

- 类型参数没有单独的表，写在函数名后的 `::<…>` 里；函数名后有它就是泛型，没有就不是。签名里（形参、返回类型、函数体）直接写 `T`。函数头声明的名字在函数里优先于同名的类型。
- `type Pair<T> = (T, T)` 是带参数的类型声明：`Pair<int>` 就是 `(int, int)`。
- 在图上，泛型函数是一张**子图模板**。每用一组具体类型调用，就实例化出一张类型全部具体的子图：上面的程序里有 `first<int>` 与 `first<string>` 两张子图（`camel docs/tutorial/examples/08_generics.cml std::rgir` 可以看到）。图上只有具体类型，运行时不需要类型信息。
- 泛型函数也可以出现在任何期望一个函数的位置（`apply(id, 3)`、`xss->map<reverse>`、`let f: (x: int) => int = id`）：编译器在所在位置确定了类型之后才实例化它。什么都确定不了时（`let g = id`）是编译期错误，提示写出类型。

声明还可以说明**自己在哪里适用**：签名后的 `when` 是对声明类型参数的编译期条件。下面的 `double_it` 只适用于数值；一次调用若所有候选的条件都不成立，调用处会报错，并列出每个候选写下的条件。`is_numeric` 不是编译器内建的名字，而是用 `match type` 写出来的普通函数（标准库的七个类型谓词在 `types` 里）；条件调用的是这个模块看得到的函数，所以片段把它写在同一个模块。

```cml
module applicability

func is_numeric::<T>(): bool {
    return match type T {
        case int | i32 | float | f32 => true
        case _ => false
    }
}

func double_it::<T>(x: T): T when is_numeric::<T>() {
    return x + x
}

func main(): int sync {
    println(double_it(21), double_it(1.5))
    return 0
}
```

## 9. 错误处理

Camel 没有异常，没有 `try`/`catch`。可能失败的函数把返回类型声明为 **`T!`**：它的值要么是一个 `T`，要么是一个错误。[`examples/09_errors.cml`](examples/09_errors.cml)：

```cml
module errors

func safe_div(a: int, b: int): int! {
    return if b == 0 then {
        throw error('DivByZero', 'the divisor is zero')
    } else {
        return a / b
    }
}

func inc(x: int): int { return x + 1 }

func describe(r: int!): string {
    return match r {
        case error e => e.kind + ': ' + e.message
        case v => 'ok ' + string(v)
    }
}

func main(): int sync {
    println(safe_div(9, 3) !! -1)            // 3
    println(safe_div(9, 0) !! -1)            // -1: the substitute
    println(describe(safe_div(8, 2) ?-> inc))
    println(describe(safe_div(8, 0) ?-> inc))
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(safe_div(1, 0))
    return 0
}
```

```text
3
-1
ok 5
DivByZero: the divisor is zero
12 0
error DivByZero: the divisor is zero
```

- **`throw e` 是返回一个错误值**，与 `return` 是同一种结构。它不展开调用栈；调用方拿到的就是一个 `int!` 的值。
- `int!` 不能当 `int` 用，必须先拆开：
  - `a !! b`：出错取 `b`，否则取里面的值；
  - `x ?-> f`：出错就把错误原样传下去，否则调用 `f`，可以连成一串；
  - `match` 的 `case error e`（`e.kind`、`e.message`、`e.location`）与 `case v`。
- 在图上，这三种写法都是一个分支，条件是值上的一位标记，与 `if` 是同一种结构。

**错误与故障是两回事。** 除数为零、下标越界、递归过深是故障，不是错误值：程序带位置终止，`!!` 接不住。[`examples/09_fault.cml`](examples/09_fault.cml) 读 `xs[5]`：

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> docs/tutorial/examples/09_fault.cml:8:15
   |
 8 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```

## 10. 可空的值

**`T?`** 的值要么是 `null`，要么是一个 `T`。[`examples/10_nullable.cml`](examples/10_nullable.cml)：

```cml
module nullable

func find(xs: int[], target: int, i: int): int? {
    return if i >= len(xs) then null
        else if xs[i] == target then i
        else find(xs, target, i + 1)
}

func show(x: int?): string {
    return match x {
        case null => 'not found'
        case v => 'at ' + string(v)
    }
}

func main(): int sync {
    let xs = [10, 20, 30]
    println(find(xs, 20, 0) ?? -1)
    println(find(xs, 99, 0) ?? -1)
    println(show(find(xs, 30, 0)), show(find(xs, 31, 0)))
    let maybe: int? = null
    println(maybe)
    return 0
}
```

```text
1
-1
at 2 not found
null
```

- `a ?? b`：为空取 `b`。`match` 用 `case null` 与 `case v`。
- 普通的 `int` 进入 `int?` 的位置时自动装入：`find` 的两条臂一条是 `null`、一条是 `i`，分支就是 `int?`；实参（`show(5)`）、带类型的声明（`let maybe: int? = 5`）、`return` 同理。反过来不行：`int?` 要当 `int` 用，必须用 `??` 或 `match` 拆开。

## 11. 映射与集合

`Map<K, V>` 与 `Set<T>` 是内建类型。没有专门的字面量：写一个数组，再用 `as` 转换。[`examples/11_collections.cml`](examples/11_collections.cml)：

```cml
module collections

func main(): int sync {
    let ages = [('bob', 31), ('alice', 28)] as Map<string, int>
    println(ages)                              // printed in key order
    println(ages['alice'] ?? 0, ages['carol'] ?? 0)

    let more = ages->put<'carol', 45>->remove<'bob'>
    println(more, len(more))
    println(ages)                              // unchanged: every operation returns a new value
    println(keys(more), values(more))

    let seen = [3, 1, 2, 3, 1] as Set<int>
    println(seen, seen->contains<2>, seen->contains<9>)
    println(union(seen, [9] as Set<int>), intersect(seen, [1, 9] as Set<int>))
    return 0
}
```

```text
{alice: 28, bob: 31}
28 0
{alice: 28, carol: 45} 2
{alice: 28, bob: 31}
[alice, carol] [28, 45]
{1, 2, 3} true false
{1, 2, 3, 9} {1}
```

- 所有操作返回新值，原来的映射与集合不变。
- `m[k]` 是查找，结果是 `V?`，所以后面跟 `?? 默认值`。
- 遍历：先 `keys(m)`、`items(m)` 取出数组，再用 `map`、`reduce`、`foreach`。

## 12. 模块与标准库

[`examples/shapes.cml`](examples/shapes.cml) 是一个库模块，用 `export` 标出对外可见的名字：

```cml
module shapes

import { sqrt } from math

export type Vec = { x: float, y: float }

export func hypot(v: Vec): float {
    return sqrt(v.x * v.x + v.y * v.y)
}

export func area(w: float, h: float): float {
    return w * h
}
```

[`examples/12_modules.cml`](examples/12_modules.cml) 使用它和标准模块 `math`：

```cml
module modules_demo

import { sqrt } from math
import { hypot, Vec } from shapes
import shapes

func main(): int sync {
    println(sqrt(2.0))
    let v: Vec = { x: 3.0, y: 4.0 }
    println(hypot(v))
    println(shapes::area(2.0, 5.0))
    return 0
}
```

```text
1.4142135623730951
5.0
10.0
```

- `import { a, b } from m` 引入列出的名字；`import m` 只引入命名空间，用 `m::name` 访问。
- 同目录下的 `.cml` 文件按文件名被找到。

### 标准库

随 Camel 提供的模块有 `str`（字符串）、`array`（数组）、`math`、`statistics`、`random`、`time`、`fs`、`json`、`process`、`env`、`re`、`hash`、`para`、`this`，张量一系的 `tensor`、`nn`、`autodiff`、`onnx`，以及嵌入 Python 的 `python`（[语言参考 5.2.1](../language/05_modules.md#521-随-camel-提供的模块)）。[标准库参考](../stdlib-reference.md)逐个列出前十四个模块的函数：签名、是否纯、失败时的行为、例子。[`examples/12_stdlib.cml`](examples/12_stdlib.cml)：

```cml
module stdlib_demo

import { split, trim, to_upper, replace, find } from str
import { sort_by, take, sum, reverse, unique } from array
import { abs, hypot, bit_and, pi } from math
import { mean } from statistics

func main(): int sync {
    // Strings: the value goes in on the left, what configures the call goes in angle brackets.
    let words = '  pear, fig ,apple, fig '->trim->split<','>->map<(w: string): string => trim(w)>
    println(words)
    println(words->unique->sort_by<(w: string): int => len(w)>->take<2>)
    println('hello'->to_upper, 'a-b-c'->replace<'-', '+'>, len('héllo'))

    // A result that may not exist is a T?.
    let at_l = 'hello'->find<'l'> ?? -1
    let at_z = 'hello'->find<'z'> ?? -1
    println(at_l, at_z)

    // Arrays and numbers.
    let xs = [3, -1, 4, -1, 5]
    println(sum(xs), reverse(xs), xs->map<(x: int): int => abs(x)>)
    println(mean([1.0, 2.0, 4.0]), hypot(3.0, 4.0), bit_and(6, 3))
    println('{:.4f}'->format<pi>)
    return 0
}
```

```text
[pear, fig, apple, fig]
[fig, pear]
HELLO a+b+c 5
2 -1
10 [5, -1, 4, -1, 3] [3, 1, 4, 1, 5]
2.3333333333333335 5.0 2
3.1416
```

- **调用的约定**：函数要处理的值是它的普通参数，配置这次调用的值是 `with` 参数。所以库函数都能接在管道上：`s->trim->split<','>`、`xs->unique->sort_by<key>->take<2>`。
- 字符串总是合法的 UTF-8，长度与下标按码点：`len('héllo')` 是 5，`s[i]` 取出第 `i` 个字符。二进制数据是 `Bytes`。
- 可能没有结果的函数返回 `T?`（如 `find` 找不到时），用 `??` 取出；越界、不在定义域内的实参是故障。
- 一个书写上的细节：尖括号要紧贴它前面的名字（`find<x>`），而比较运算符 `<` 前面要有空格（`a < b`）。这样两者不会混淆，同一个实参表里可以有几个带尖括号的调用：`f<a>(x), g<b>(y)`。
- `array` 模块是用 Camel 写的泛型函数，在图上是看得见的子图；`str`、`math` 的函数是 C++ 算子，在图上是一个节点。

想知道一个程序能用哪些算子、各自是否声明为纯，问编译器：

```bash
camel inspect --ops docs/tutorial/examples/12_stdlib.cml
```

每个重载打印一行，例如：

```text
OPERATOR {"module":"str","name":"split","uri":"str:split","with":1,"norm":1,"purity":"pure","signature":"<string> (string) => string[]"}
```

纯的算子可以被优化遍提前求值、合并、删除，可以被并行调度并发执行；有副作用的算子只在同步块里、或结果被用到时才执行。

## 13. 看图

到这里一直在说“函数是子图，调用是节点”。现在亲眼看一看。[`examples/13_graph.cml`](examples/13_graph.cml)：

```cml
module graph_demo

func add(a: int, b: int): int {
    return a + b
}

func main(): int sync {
    let x = add(1, 2)
    println(x)
    return 0
}
```

在文件名后面写上遍 `std::rgir`，程序不运行，而是把图打印成 Graphviz 的 DOT 文本：

```bash
camel docs/tutorial/examples/13_graph.cml std::rgir
```

下面是输出（为了易读，去掉了每个节点上的 `origin`、`span`、`srcfile` 源码位置属性）：

```text
digraph GraphIR {
    subgraph cluster_F01 {
        label="main";
        subgraph cluster_F02 {
            label="add";
            F02 [label="ARGS", type="ARGS"];
            O02 [label="EXIT", type="EXIT"];
            N00 [label="a", type="PORT"];
            N01 [label="b", type="PORT"];
            N02 [label=":op/add_l", type="OPER"];
            F02 -> N00 [label="0", type="NORM_PORT"];
            F02 -> N01 [label="1", type="NORM_PORT"];
            N02 -> O02 [label="0", type="EXIT"];
            N00 -> N02 [label="0|0", type="NORM"];
            N01 -> N02 [label="0|1", type="NORM"];
        }
        O01 [label="EXIT", type="EXIT"];
        N03 [label="1", type="DATA"];
        N04 [label="2", type="DATA"];
        N05 [label="add", type="FUNC"];
        N06 [label=":io/println", type="OPER"];
        N07 [label="0", type="DATA"];
        N08 [label="GATE", type="GATE"];
        N08 -> O01 [label="0", type="EXIT"];
        N03 -> N05 [label="0|0", type="NORM"];
        N04 -> N05 [label="0|1", type="NORM"];
        N05 -> N06 [label="0|0", type="NORM"];
        N07 -> N08 [label="0|0", type="NORM"];
        N06 -> N08 [label="0|0", type="CTRL"];
    }
    O00 [label="EXIT", type="EXIT"];
    N09 [label="Function(graph=main, tupleSlots=0)", type="DATA"];
    N0A [label="CALL", type="CALL"];
    N0A -> O00 [label="0", type="EXIT"];
    N09 -> N0A [label="0|0", type="WITH"];
}
```

对着源码读：

- **`add` 是一张子图**（`cluster_F02`）：两个 `PORT` 节点是形参 `a`、`b`，一个 `OPER` 节点 `:op/add_l` 是 64 位整数的加法，它的结果连到 `EXIT`。
- **`main` 也是一张子图**（`cluster_F01`）。两个 `DATA` 节点是常量 `1`、`2`；**`FUNC` 节点 `add` 就是那次调用**，两条 `NORM`（数据）边把实参送进去；它的结果又经一条数据边送给 `:io/println`。
- **同步块是控制边。** `println` 的结果没有人用，`return 0` 并不依赖它。让它仍然执行的是那条 `CTRL` 边：`N06 -> N08`，从 `println` 指向 `GATE`，`GATE` 把返回值 `0` 放行到 `EXIT`。把 `main` 的 `sync` 去掉再看一次，这条边和 `println` 节点的可达性就都没有了——第 6 节的规则在图上就是这个样子。

装了 Graphviz 的话，`camel ... std::rgir | dot -Tsvg -o graph.svg` 得到图片；`std::gir` 输出带样式的版本。

## 14. 换遍

`std::rgir` 是一个**遍**。Camel 把编译、优化、执行、导出都建模为对图的变换，文件名后面写的就是要依次运行的遍。同一份源码，换一组遍，做不同的事。用 [`examples/14_passes.cml`](examples/14_passes.cml)：

```cml
module passes_demo

func square(x: int): int {
    return x * x
}

func sum_squares(n: int, acc: int): int {
    return if n == 0 then acc else sum_squares(n - 1, acc + square(n))
}

func main(): int sync {
    println(sum_squares(1000, 0))
    return 0
}
```

```bash
camel docs/tutorial/examples/14_passes.cml                      # 默认：节点虚拟机 std::nvm
camel docs/tutorial/examples/14_passes.cml std::fvm             # 字节码虚拟机
camel docs/tutorial/examples/14_passes.cml std::jit             # 即时编译
camel docs/tutorial/examples/14_passes.cml std::prl             # 并行调度
camel docs/tutorial/examples/14_passes.cml std::opt std::fvm    # 先优化，再运行
```

每一条都打印：

```text
333833500
```

**看每一遍对图做了什么。** `std::stats` 打印图的统计数字并把图原样传给下一个遍：

```bash
camel docs/tutorial/examples/14_passes.cml std::stats std::opt std::stats std::fvm
```

```text
GRAPH_STATS {"graphs":6,"nodes":26,"edges":28,"opers":5,"direct_calls":5,"indirect_calls":1,"closures":0,"branches":1}
GRAPH_STATS {"graphs":7,"nodes":70,"edges":85,"opers":18,"direct_calls":4,"indirect_calls":0,"closures":0,"branches":3}
333833500
```

两行数字不同：`std::opt` 改了图（直接调用少了，算子节点多了——函数被内联到了调用处），而程序的结果不变。要逐遍保存整张图，用 `--dump-after-each-pass`：

```bash
camel docs/tutorial/examples/14_passes.cml --dump-after-each-pass snap std::opt::inline std::opt::fold std::fvm
```

目录 `snap` 里得到 `00_input.rgir.dot`、`01_std.opt.inline.rgir.dot`、`02_std.opt.fold.rgir.dot` 以及对应的 `.stats.json`：输入的图，和每一遍之后的图。

几条规则：

- 遍按书写顺序运行。执行图的遍（`std::nvm`、`std::fvm`、`std::jit`、`std::prl`）和转译的遍（`std::rgir`、`onnx::export`）消费掉图，写在它们后面的遍不会运行。
- 不写任何遍，或者写的遍都没有消费掉图，最后运行默认的 `std::nvm`。
- 语言不限制遍的顺序，而顺序可以影响结果：遍会改图，可以有副作用。标准库的优化遍以不改变程序结果为目标，这由测试保证，不是语言层面的约定。

## 15. 张量与模型的导出

张量来自 `tensor` 模块。[`examples/15_tensor.cml`](examples/15_tensor.cml)：

```cml
module tensor_demo

import { Tensor, ones, zeros, shape, sum, transpose } from tensor

func main(): int sync {
    let a = ones([2, 3])
    let b = ones([3, 1])
    let c = a @ b                    // matrix product: 2x3 @ 3x1
    println(shape(a), shape(c))
    println(c)
    println(sum(a * 2.0 + 1.0))
    println(shape(transpose(a)))
    return 0
}
```

```text
[2, 3] [2, 1]
tensor([[      3.],
        [      3.]], dtype=float32)
18.0
[3, 2]
```

**一个最小的模型。** Camel 程序的本来面目是只表达计算：`main` 就是模型，它的参数就是模型的输入。[`examples/15_model.cml`](examples/15_model.cml)：

```cml
module model

import { Tensor, ones, relu, sum } from tensor

func main(x: Tensor<f32, [2, 3]>): Tensor {
    let w = ones([3, 1])
    return relu(x @ w)
}
```

`main` 没有打印任何东西，也没有 `sync`：它是纯的。参数 `x` 的类型写明了元素类型与形状。

**运行它**：参数的值从命令行给，张量来自 `.npy` 文件（[`examples/x.npy`](examples/x.npy) 是 `[[1, -2, 3], [-4, -5, 6]]`）；`main` 返回的值打印到标准输出：

```bash
camel docs/tutorial/examples/15_model.cml --arg x=docs/tutorial/examples/x.npy
```

```text
tensor([[      2.],
        [      0.]], dtype=float32)
```

**导出它**：同一份源码，换一组遍。程序里没有为导出写任何东西，连 `import onnx` 都不需要：

```bash
camel docs/tutorial/examples/15_model.cml std::macro std::opt 'onnx::export(out="model.onnx")'
```

```text
onnx::export: wrote model.onnx
```

得到的 `model.onnx` 有一个形状为 `[2, 3]` 的输入 `x` 和一个输出 `output`；用 ONNX Runtime 喂同一个 `x.npy`，得到同样的 `[[2.], [0.]]`。

导出遍能翻译就翻译，翻译不了（程序里有打印、读文件等效应）就报错并指出位置。既要计时、打印又要导出的程序，改为在程序里调用 `onnx.export_model` 导出自己的某个函数（[cli.cn.md](../cli.cn.md)“以遍的形式导出 ONNX”）。

## 16. 求梯度

自动微分是建立在遍机制之上的库：`grad(f)` 把 `f` 的图改写成它的导数的图。`f` 的第一个 `with` 参数是被求导的权重，普通参数是数据。[`examples/16_grad.cml`](examples/16_grad.cml)：

```cml
module grad_demo

import { grad, with_grad } from autodiff

with <x: float>
func f(y: float): float {
    return x * x * y + y
}

func main(): int sync {
    println(f<3.0>(2.0))
    println(grad(f)<3.0>(2.0))                 // df/dx = 2xy = 12
    println(with_grad(f)<3.0>(2.0))            // (值, 梯度) = (20, 12)
    return 0
}
```

```bash
camel docs/tutorial/examples/16_grad.cml std::macro
```

```text
20.0
12.0
(20.0, 12.0)
```

- `grad(f)` 返回一个函数，它对 `f` 的第一个 `with` 参数求导；其余 `with` 参数当作常量，普通参数是数据。
- 要对某个数据求导（例如对抗样本），把它写成一个 `with` 参数：`grad(<x: Tensor> (m: Model): float => loss(m, x))<x>`。
- `with_grad(f)` 同时返回值和梯度。循环里推荐把变换先绑定一次，再带着权重调用：`let loss_with_grad = with_grad(loss)`，循环内 `let (l, g) = loss_with_grad<model>(x, y)`。
- `grad` 是 macro 算子。流水线里的 `std::macro` 在运行之前就把它展开成导数的图，后面的优化遍可以继续优化这张图。这是这个库的前提，不是语言的约束。
- 被求导的函数用 `let` 写成纯的数据流；梯度需要经过 `var` 的读写时会被直接拒绝。第一个 `with` 参数是 `var`、没有切向量，或函数没有 `with` 参数时，编译期以 `SemanticDiag::NotDifferentiable` 拒绝并说明原因。
- 训练一步就是“梯度 + 更新”：模型的权重写成 `with` 参数，`grad(loss)<model>(x, y)` 对权重求导，`nn` 模块的 `sgd` 给出更新后的权重。完整的例子见 [`examples/ml/train_linear.cml`](../../examples/ml/train_linear.cml)。

## 接下来

- [示例集](../../examples/README.cn.md)：按任务组织的短程序——JSON、文件、词频统计、排序、命令行工具、并行、仿真、训练。
- [语言参考](../language/README.md)：每个构造的精确规则，以及哪些写法保留而尚未实现。
- [标准库参考](../stdlib-reference.md)：每个模块的每个函数。
- [命令行](../cli.cn.md)：全部选项、遍、诊断格式、资源上限；`camel --explain <诊断名>` 解释一条诊断。
- [设计原则](../design-principles.cn.md)：语言为什么是这个样子。
