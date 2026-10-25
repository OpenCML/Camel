# 第 6 章 · 结构体、元组、映射与集合

[English](06_aggregates.en.md) | 中文 · [返回目录](tutorial.cn.md)

到第 5 章为止，程序里的值都是零散的：一个数、一条字符串、一个数组。要给真实的东西建模——一条记录、一个
键到值的表、一堆去过重的项——就要能把几个值**组成一个值**。这一章讲四种组合方式，以及一条让它们用起来省心的
规则：`==` 比的是内容，不是位置。

## 结构体与元组：给数据起名字

[`examples/06_collections.cml`](examples/06_collections.cml) 前半：

```cml
// Structs, tuples, Map and Set: the shapes a program's data takes.
// Run: camel docs/tutorial/examples/06_collections.cml

type Point = { x: int, y: int }

func main(): int sync {
    // A struct names its fields; a tuple is positional and carries no names.
    let a: Point = { x: 3, y: 4 }
    println(a, a.x + a.y)
    let pair = (1, 'one')
    println(pair, pair.0, pair.1)

    // Map<K, V> and Set<T> are built in; both are made from an array with `as`.
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

```bash
camel docs/tutorial/examples/06_collections.cml
```

```text
{ x: 3, y: 4 } 7
(1, one) 1 one
{alice: 28, bob: 31}
28 0
{alice: 28, carol: 45} 2
{alice: 28, bob: 31}
[alice, carol] [28, 45]
{1, 2, 3} true false
{1, 2, 3, 9} {1}
```

- **结构体**用字段名读：`a.x`、`a.y`。类型要先起名字（`type Point = ...`），构造时写 `{ x: 3, y: 4 }`，
  字段名要写全。
- **元组**按位置读：`pair.0`、`pair.1`。它没有名字，适合"临时把两个值放在一起"。
- 两种都可以嵌套，也都可以放进数组和映射里——`[('bob', 31), ('alice', 28)]` 就是一个元组的数组。

## 映射与集合：键到值、去过重的项

`Map<K, V>` 与 `Set<T>` 是内建类型，用 `as` 从一个数组造出来：

```cml
let ages = [('bob', 31), ('alice', 28)] as Map<string, int>
let seen = [3, 1, 2, 3, 1] as Set<int>          // {1, 2, 3}
```

- 读一个键写 `m['alice']`，得到的是 `int?`——键可能不在。`?? 0` 给一个替代值，第 7 章讲这个 `?`。
- `put`、`remove`、`contains` 都是**返回新值**，原来的映射不变：`more` 是改过的那个，`ages` 还是原来的。
  这和第 2 章的 `let` 是一条思路：值不会被偷偷改掉，改动写在返回值里。
- 映射**按键排序打印**，`keys`、`values` 也按这个顺序给出：同一份数据每次打印都一样，测试和比较才靠得住。
- 集合去重，`union`、`intersect` 做并集与交集。

## `==` 比内容

两个值相等，看的是它们内容一样，不是它们是不是同一个位置：

[`examples/06_equality.cml`](examples/06_equality.cml)：

```cml
// `==` compares two values by what they hold, not by where they are.
// Run: camel docs/tutorial/examples/06_equality.cml

type Point = { x: int, y: int }

func main(): int sync {
    let a: Point = { x: 3, y: 4 }
    let b: Point = { x: 3, y: 4 }
    println(a == b)
    println((1, 'one') == (1, 'one'))
    println([1, 2] == [1, 2])
    println('ab' == 'ab')
    return 0
}
```

```bash
camel docs/tutorial/examples/06_equality.cml
```

```text
true
true
true
true
```

四个 `true` 说的都是同一件事：`a` 与 `b` 是两个分别构造出来的结构体，内容一样，所以相等。要问"是不是
同一块数据"，那是另一个运算符 `===`，第 13 章讲数组时用它。

## 在图上

- 结构体与元组的构造在图上是**一个节点**：它的输入是各个字段的值。取字段（`a.x`）也是一个节点，输入是
  那个结构体。所以"造一个结构体再取一个字段"在图上是可以被看穿并化简的。
- 映射与集合的构造、`put`、`contains` 同样各是一个节点；它们返回新值，所以在图上没有"被改过的那个映射"
  这种状态——每一次改动都是一条新的路径。
- 这也是为什么 `==` 可以是纯计算：图上的相等节点只读两个输入的内容。

## 练习

数一数这段词里每个词出现了几次：`['fig', 'pear', 'fig', 'apple', 'fig']`。用 `reduce` 从一个空映射出发，
每遇到一个词就把它加一——`counts` 就是第一部分那个词频统计小工具的核心（第 8 章把它写完）。

参考答案 [`examples/06_exercise.cml`](examples/06_exercise.cml)：

```cml
// The answer to the exercise of chapter 6: counting with a map is a fold over `put`.
// Run: camel docs/tutorial/examples/06_exercise.cml

func tally(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(): int sync {
    let counts = tally(['fig', 'pear', 'fig', 'apple', 'fig'])
    println(counts, counts['fig'], counts['cherry'] ?? 0)
    return 0
}
```

```bash
camel docs/tutorial/examples/06_exercise.cml
```

```text
{apple: 1, fig: 3, pear: 1} 3 0
```

## 参考

- [语言参考 3.3 复合值](../language/03_data_model.md#33-复合值)与 [3.6 映射与集合](../language/03_data_model.md#36-映射与集合)
- [语言参考 6.7.3 比较](../language/06_expressions.md#673-比较)（`==` 与 `===`）