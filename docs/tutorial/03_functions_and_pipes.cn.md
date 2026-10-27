# 第 3 章 · 函数与管道

[English](03_functions_and_pipes.en.md) | 中文 · [返回目录](tutorial.cn.md)

第 1 章你已经看到：函数是一张子图，调用是图上的一个节点。这一章把函数写全——名字、参数、返回类型、
递归、没有名字的函数（lambda），以及给函数做配置的 `with` 参数。要带走的一句话是：**`a->f->g` 这样的
链是数据流，它在图上就是一条路径**。

## 用名字代替重复

[`examples/03_functions.cml`](examples/03_functions.cml)：

```cml
// Functions, recursion, pipelines and `with` parameters.
// Run: camel docs/tutorial/examples/03_functions.cml

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

```bash
camel docs/tutorial/examples/03_functions.cml
```

```text
49
6765
5000050000
15
50
```

- **返回类型写在参数表后面**：`func square(x: int): int`。它是函数值类型的一部分：`square` 是
  `(x: int) => int` 这样的一个值。
- **函数体的值就是最后一个表达式的值**，所以 `return x * x` 与最后一行写 `x * x` 意思一样。第 4 章会
  看到这条规则在不写 `return` 时救了谁的命。
- **重复的计算有三种写法**：递归（`fib`）、`loop` 表达式（第 5 章）、高阶算子（第 5 章）。尾位置的调用
  不占栈帧，所以 `sum_to(100000, 0)` 递归十万层也没有问题；非尾递归（`fib`）同时打开的调用默认不超过
  1000 层，`--max-recursion-depth` 可以调。

## 管道把数据写成一条链

`5->square->scale<2>` 与 `scale<2>(square(5))` 是同一个调用，得到 `50`。左边的值成为右边函数的实参，
一串 `->` 就是"先算这个，再交给那个"。这不是语法糖：**图上的一条路径本来就是"谁要谁的结果"**，链条把
这句话写在了源码里。

<!-- camel-snippet: fragment: 一行链式调用，xs 来自上下文，不是一份完整的程序 -->

```cml
let ys = xs->map<(x) => x * x>->filter<(x) => x % 2 == 0>
```

## lambda：没有名字的函数

函数是值，所以可以没有名字。`(参数): 返回类型 => 表达式` 就是一个 lambda：

[`examples/03_lambdas.cml`](examples/03_lambdas.cml)：

```cml
// A function with no name is written `(x: int): int => ...`; it can also be kept in a name.
// Run: camel docs/tutorial/examples/03_lambdas.cml

func main(): int sync {
    let twice = (x: int): int => x * 2
    println(twice(21))

    println([1, 2, 3]->map<(x) => x + 10>)
    println([1, 2, 3, 4]->filter<(x) => x % 2 == 0>)
    return 0
}
```

```bash
camel docs/tutorial/examples/03_lambdas.cml
```

```text
42
[11, 12, 13]
[2, 4]
```

`twice` 这个名字里装的就是一个函数；`map`、`filter`、`reduce` 要的正是这样的一个函数。第 5 章系统讲
它们，第 11 章讲 lambda 能抓到什么、抓到的值什么时候变。

## `with` 参数：函数的配置

`with <factor: int>` 声明的参数写在**尖括号**里，调用时写 `scale<3>(5)`。圆括号里的参数是函数要处理的
数据，`with` 里的是函数被配置的属性：别的语言里放在对象成员上的那些配置，在 Camel 里是 `with` 参数。
它们不是数据流的一部分，所以不出现在管道的链条上。第 11 章展开。

## 在图上

- 一次调用是图上的一个 **`CALL` 节点**；被调用的函数是它自己的子图，形参在子图入口是 `PORT` 节点
  （第 1 章那张图里的 `N00 [label="x", type="PORT"]`）。
- `5->square->scale<2>` 在图上就是 `5` → `square` 的调用 → `scale` 的调用，两条数据边接成的一条路径。
- 递归没有特殊节点：`fib` 的调用点就在 `fib` 自己的子图里。
- `with` 参数是函数值上的一部分，不是一个数据节点；图上读不出来，它在程序里就被绑定了。

## 练习

写一个 `max_of(a: int, b: int): int`，返回较大的那个（写成一个 `if` 表达式）。在 `main` 里打印
`max_of(3, 7)`；然后用 `reduce` 和它一起求 `[3, 9, 4]` 里的最大值。

参考答案 [`examples/03_exercise.cml`](examples/03_exercise.cml)：

```cml
// The answer to the exercise of chapter 3: a function of two parameters, used from a pipe.
// Run: camel docs/tutorial/examples/03_exercise.cml

func max_of(a: int, b: int): int {
    return if a > b then a else b
}

func main(): int sync {
    let xs = [3, 9, 4]
    println(max_of(3, 7))
    println(xs->reduce<(acc: int, x: int): int => max_of(acc, x), xs[0]>)
    return 0
}
```

```bash
camel docs/tutorial/examples/03_exercise.cml
```

```text
7
9
```

## 参考

- [语言参考 6.4 调用](../language/06_expressions.md#64-调用)与 [6.5 管道](../language/06_expressions.md#65-管道)
- [语言参考 6.9 lambda 与函数类型](../language/06_expressions.md#69-lambda-与函数类型)
- [语言参考 7.4 函数](../language/07_statements.md#74-函数)与 [7.4.2 `with` 形参](../language/07_statements.md#742-with-形参)
- [语言参考 4.6 故障、退出与资源上限](../language/04_execution_model.md#46-故障退出与资源上限)（递归深度上限）