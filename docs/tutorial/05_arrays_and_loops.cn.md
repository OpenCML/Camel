# 第 5 章 · 数组与循环

[English](05_arrays_and_loops.en.md) | 中文 · [返回目录](tutorial.cn.md)

同一件事要做很多遍时，Camel 有两种写法：**说清楚每一轮怎么算**（`loop`），或者**只说要对每个元素做什么**
（`map`、`filter`、`reduce`、`foreach`）。这一章讲怎么选，以及这两种写法在图上是两件不同的东西。

## 数组与区间

数组是 `T[]`；`a..b` 是从 `a` 到 `b - 1` 的区间，写起来比手写数组短：

```cml
let xs = 1..11        // [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
```

数组是**值**，不是可以被别人改掉的容器：`concat(xs, [11])`、`xs[0:3]` 都返回新数组，`xs` 不变
（第 2 章）。索引越界是故障，程序带位置终止（第 7 章）。

## 高阶算子

[`examples/05_arrays.cml`](examples/05_arrays.cml)：

```cml
// Arrays and the higher-order operators map, filter, reduce and foreach.
// Run: camel docs/tutorial/examples/05_arrays.cml

func main(): int sync {
    let xs = 1..11    // [1, ..., 10]
    let squares = xs->map<(x) => x * x>
    let evens = squares->filter<(x) => x % 2 == 0>
    let total = evens->reduce<(acc: int, x: int): int => acc + x, 0>

    println(xs)
    println(squares)
    println(evens)
    println(total)

    // Arrays are values: slice and concat return new arrays.
    let ys = concat(xs[0:3], [99])
    println(ys, len(ys), ys[0])

    // foreach runs its function for each element, in order.
    ['a', 'b', 'c']->foreach<(s) => sync { println('item', s) }>
    return 0
}
```

```bash
camel docs/tutorial/examples/05_arrays.cml
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

- `->` 是管道：`xs->map<f>` 就是 `map(xs, f)`，写成一条链更好读（第 3 章）。
- 传给 `map`、`filter` 的函数写成 lambda：`<(x: int): int => x * x>`，尖括号里是它的类型。
- `reduce` 需要初值，所以是 `reduce<f, 0>`。
- 这四个算子的函数都保持纯：传给它们的函数不该有状态（下面有个反例）。

## `loop`

有些重复计算有状态，每一轮要用上一轮的结果。这时用 `loop`：名字是状态，`do` 后面的表达式的值是**下一轮的
状态**，循环的值是最后一轮的状态。[`examples/05_loops.cml`](examples/05_loops.cml)：

```cml
// `loop` carries its state from one round to the next; the body's value is the next state.
// Run: camel docs/tutorial/examples/05_loops.cml

func main(): int sync {
    // One round per element of the sequence.
    let total = loop acc = 0 for x in 1..6 do acc + x * x
    println(total)

    // The condition decides when to stop; there is no `break` and no `continue`.
    let halved = loop n = 100 while n > 1 do n / 2
    println(halved)

    // Two names on the loop head: the body's value is the next pair.
    let (i, acc) = loop i = 1, acc = 0 while i <= 4 do (i + 1, acc + i)
    println(i, acc)

    // Both forms: the condition is checked before each round.
    let first_big = loop hit = 0 for x in [2, 7, 9] while hit == 0 do if x > 5 then x else hit
    println(first_big)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_loops.cml
```

```text
55
1
5 10
7
```

- `loop 名字 = 初值 (, 名字 = 初值)* (for 名字 in 序列)? (while 条件)? do 表达式`：两种头可以一起写，
  至少写一个。
- **没有 `break` 与 `continue`**：提前结束写成 `while` 条件，跳过一轮的其余部分写成 `if`。
- 一个名字时，`do` 后面是下一个状态；多个名字时，`do` 后面是下一组状态的元组（上面第三个例子）。
- `for x in xs` 对空数组一轮都不执行，循环的值就是初值。

## 什么时候用哪个

- 需要**按顺序**做、或者每一轮依赖上一轮：`loop`（或 `reduce`、`foreach`）。
- 只是对每个元素各算一份、元素之间没有关系：`map`、`filter`。它们更短，而且**可以并行**——正因为
  元素之间没有顺序。

顺序这件事是语言规定的，不是实现细节。[`examples/05_map_order.cml`](examples/05_map_order.cml) 里同一个
随机数生成器被用在两处：`map` 的元素函数里（错），和 `reduce` 的累加函数里（对）。

```cml
// The elements of `map` have no agreed order of execution: do not put stateful calls in them.
// Run: camel docs/tutorial/examples/05_map_order.cml
//      camel docs/tutorial/examples/05_map_order.cml std::prl

import { seed, randint, next_key } from random

func main(): int sync {
    seed(42)
    // Wrong tool: next_key advances a hidden state, so each element depends on which ran first.
    let drawn = (0..8)->map<(i) => next_key()->randint<0, 100>>
    println(drawn)

    seed(42)
    // reduce runs in element order on every back end.
    let ordered = (0..8)->reduce<(acc: int[], i: int): int[] => concat(acc, [next_key()->randint<0, 100>]), [0][0:0]>
    println(ordered)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_map_order.cml
```

```text
[22, 72, 89, 99, 60, 96, 66, 2]
[22, 72, 89, 99, 60, 96, 66, 2]
```

在顺序执行的后端上（默认的 `std::nvm`，以及 `std::fvm`、`std::jit`），两行一样，每次运行都一样。

<!-- camel-snippet: compile-only: the text block is not printed by this run; the parallel order differs from run to run -->

```cml
// The elements of `map` have no agreed order of execution: do not put stateful calls in them.
// Run: camel docs/tutorial/examples/05_map_order.cml
//      camel docs/tutorial/examples/05_map_order.cml std::prl

import { seed, randint, next_key } from random

func main(): int sync {
    seed(42)
    // Wrong tool: next_key advances a hidden state, so each element depends on which ran first.
    let drawn = (0..8)->map<(i) => next_key()->randint<0, 100>>
    println(drawn)

    seed(42)
    // reduce runs in element order on every back end.
    let ordered = (0..8)->reduce<(acc: int[], i: int): int[] => concat(acc, [next_key()->randint<0, 100>]), [0][0:0]>
    println(ordered)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_map_order.cml std::prl
```

换成并行调度 `std::prl`，第一行的顺序就不再有保证，每次运行可能不同；某一次运行的第一行是
`[66, 22, 96, 72, 89, 2, 99, 60]`，第二行仍然是 `[22, 72, 89, 99, 60, 96, 66, 2]`。这是语言规定的行为
（设计原则 3.6），不是缺陷：正因为元素之间没有顺序，`map` 才能并行。规则很简单：**传给 `map`、`filter`
的函数保持纯；必须按顺序做的事用 `foreach`、`reduce` 或 `loop`。**

## 主线：第 5 步

第一部分的小工具是词频统计。第 5 步把文本切成词，再用 `loop` 把它们数出来
（[`examples/wordcount/step5.cml`](../../examples/wordcount/step5.cml)）：

```cml
// Step 5 of the word counter: the words of the text with their counts, carried by a `loop`.
// Run: camel examples/wordcount/step5.cml

import { split, trim, to_lower, replace } from str
import { any, flat_map, sort_by, take } from array

// The words of one line: lower case, punctuation removed, empty pieces dropped.
func words_of(line: string): string[] {
    let plain = line->to_lower->replace<',', ' '>->replace<'.', ' '>
    return plain->split<' '>->filter<(w) => len(w) > 0>
}

// `counts` with one more for `w`, adding `w` when it is new.
func bump(counts: (string, int)[], w: string): (string, int)[] {
    let seen = counts->any<(e: (string, int)): bool => e.0 == w>
    let rows = if seen then counts else concat(counts, [(w, 0)])
    return rows->map<(e: (string, int)): (string, int) => if e.0 == w then (e.0, e.1 + 1) else e>
}

func show(e: (string, int)): string {
    return e.0 + ' ' + string(e.1)
}

func main(): int sync {
    let text = '
        The quick fox sees the lazy dog.
        The dog sees the fox, and the fox runs.
    '

    // Cut the text into lines, and the lines into words.
    let lines = text->split<'\n'>
        ->map<(l) => trim(l)>
        ->filter<(l) => len(l) > 0>
    let words = lines->flat_map<words_of>
    println('lines:', len(lines), 'words:', len(words))

    // A `loop` carries the counts from one round to the next.
    let counts = loop acc = [] as (string, int)[] for w in words do bump(acc, w)
    println('distinct:', len(counts))

    // sort_by is stable: ordering by word first and by count second leaves the words of one count
    // in alphabetical order. The negated count puts the largest first.
    let ranked = counts
        ->sort_by<(e: (string, int)): string => e.0>
        ->sort_by<(e: (string, int)): int => 0 - e.1>
    println('top 5:')
    println(ranked->take<5>->map<show>->join<'\n'>)
    return 0
}
```

```bash
camel examples/wordcount/step5.cml
```

```text
lines: 2 words: 16
distinct: 8
top 5:
the 5
fox 3
dog 2
sees 2
and 1
```

在第 6 步之前，计数用的是一个 `(string, int)` 的数组——`Map` 是第 6 章的事。到第 6 步这份词表会被换成一个
`Map<string, int>`，程序的行为不变。

## 在图上

- **`loop` 是图上带状态回边的节点**：状态从一轮流回下一轮，每一轮的值又依赖上一轮的值；没有这种回边的
  计算才可能并行。
- **高阶算子的元素之间没有顺序边**：`map` 的每个元素只依赖数组自己，谁也等不到谁，所以能并行。
- 数组是值：`concat`、`slice` 造新节点，原数组的节点不动。

## 练习

用 `loop` 算出 1 到 10 里所有偶数的平方和（答案是 220）。换个写法：先用 `filter` 与 `map` 取出偶数平方
的数组，再用 `reduce` 求和——两种写法在图上的差别是什么？

参考答案 [`examples/05_exercise.cml`](examples/05_exercise.cml)：

```cml
// The answer to the exercise of chapter 5: a `loop` that adds the squares of the even numbers.
// Run: camel docs/tutorial/examples/05_exercise.cml

func main(): int sync {
    let total = loop acc = 0 for x in 1..11 do if x % 2 == 0 then acc + x * x else acc
    println(total)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_exercise.cml
```

```text
220
```

## 参考

- [语言参考 6.8.6 `loop`](../language/06_expressions.md#686-loop)
- [语言参考 6.10 高阶算子](../language/06_expressions.md#610-高阶算子)
- [语言参考 4.8 高阶算子的顺序](../language/04_execution_model.md#48-高阶算子的顺序)
- [设计原则 3.6 高阶算子与并行](../design-principles.cn.md#36-高阶算子与并行)