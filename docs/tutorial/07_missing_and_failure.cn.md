# 第 7 章 · 缺失与失败

[English](07_missing_and_failure.en.md) | 中文 · [返回目录](tutorial.cn.md)

查一个键、解析一段文本、除一个数——这些都可能不成功，而写这类代码最容易犯的错是忘掉那一次不成功。
Camel 把"可能为空"和"可能出错"写进类型：`T?` 与 `T!`。这一章讲怎么造出它们、怎么拆开它们，以及为什么
"除数为零"不属于它们——那叫**故障**，是另一回事。

## 可能为空：`T?`

[`examples/07_nullable.cml`](examples/07_nullable.cml)：

```cml
// T? may be null; open it with ?? or with match.
// Run: camel docs/tutorial/examples/07_nullable.cml

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

```bash
camel docs/tutorial/examples/07_nullable.cml
```

```text
1
-1
at 2 not found
null
```

- `int?` 的值要么是 `null`，要么是一个 `int`。`find` 找不到时返回 `null`，找得到时返回下标。
- 拆开一个 `T?` 有两种写法：**`a ?? b`** 说"是 `null` 就取 `b`"，**`match`** 的 `case null` 与 `case v`
  （`v` 绑定成里面的 `T`）。没有第三种：`int?` 不能当 `int` 用，`println(v + 1)` 编译不过。
- 第 6 章那条 `m['alice']` 也是这个类型：键可能不在，所以读一个键得到的是 `V?`。
- `null` 本身可以打印成 `null`。

## 可能出错：`T!`

`T?` 说的是"可能没有值"，`T!` 说的是"可能有值，也可能带着一个错误"。错误是一个**值**，不是控制流：

[`examples/07_errors.cml`](examples/07_errors.cml)：

```cml
// A function that may fail returns T!: an error value is handed out by `return`.
// Run: camel docs/tutorial/examples/07_errors.cml

func safe_div(a: int, b: int): int! {
    return if b == 0 then {
        return error('DivByZero', 'the divisor is zero')
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
    println(describe(safe_div(8, 2) -> inc))
    println(describe(safe_div(8, 0) -> inc))
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(safe_div(1, 0))
    return 0
}
```

```bash
camel docs/tutorial/examples/07_errors.cml
```

```text
3
-1
ok 5
DivByZero: the divisor is zero
12 0
error DivByZero: the divisor is zero
```

- **交出一个错误值用 `return e`**：它不展开调用栈，调用方拿到的是一个 `int!`。
  错误值有三个字段：`e.kind`、`e.message`、`e.location`。
- 拆开的三种写法：`a !! b`（出错取 `b`）、`x -> f`（出错就把错误原样传下去，否则调用 `f`，可以连成一串）、
  `match` 的 `case error e` 与 `case v`。
- 最后一行 `println(safe_div(1, 0))` 打印的是错误本身。它同时让编译器给出一条警告——**一个 `int!` 被
  丢掉时，里面的错误就没了**：

```text
warning[SemanticDiag::UnusedFallibleValue]: This value has type 'int!' and nothing uses it: an error it holds is lost.
```

诊断的 `help` 会给出改法：要么用 `!!` 给一个替代值，要么用 `match` 处理它。**为什么要有这条警告**：
`T!` 的全部价值就在于那个错误被看见；把它丢掉，就等于悄悄回到了"出错了当没出错"。

## 故障不是错误值

除数为零、下标越界、递归过深是**故障**（fault）：程序带位置终止，`!!` 接不住。读一个三元素数组的
`xs[5]`：

[`examples/07_fault.cml`](examples/07_fault.cml)：

```cml
// A fault is not an error value: reading past the end ends the program, with a position.
// Run: camel docs/tutorial/examples/07_fault.cml

func main(): int sync {
    let xs = [1, 2, 3]
    println('before')
    println(xs[5])
    println('after')
    return 0
}
```

先打印的 `before` 出来了，`after` 永远不会：

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> docs/tutorial/examples/07_fault.cml:7:15
   |
 7 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```

**为什么要分两种**：错误值是程序要处理的正常情况（一个键不在、一段文本不是数），所以它是值的一部分，
类型逼着你看它；故障是程序写错了（下标算错、递归没有出口），所以它带着位置停下来，让你去修。

## 在图上

- 在图上，`int`、`int?`、`int!` **是同一种值**：那个"可能是 `null`、可能带着错误"是一位标记，附着在值上。
- `??`、`!!`、`->`、`match` 拆开它时，图上都是一个**分支节点**：条件是那一位，一个臂走"有值"，另一个臂
  走"没有值/有错误"。所以"处理失败"不是控制流魔法，就是普通的分支。
- 一个 `int!` 被丢掉（没有任何东西读它）时，图上那条路径就断了——编译器正是从图上看出 `UnusedFallibleValue`。

## 练习

把 `['12', 'x', '7']` 里能读成数的加起来，读不出的当 `0`。用 `parse_int`（它返回 `int!`）和一个 `!!`。

参考答案 [`examples/07_exercise.cml`](examples/07_exercise.cml)：

```cml
// The answer to the exercise of chapter 7: the values that are there, and 0 for the ones that are not.
// Run: camel docs/tutorial/examples/07_exercise.cml

func sum_ints(words: string[]): int {
    return words->map<(s) => parse_int(s) !! 0>->reduce<(acc: int, x: int): int => acc + x, 0>
}

func main(): int sync {
    println(sum_ints(['12', 'x', '7']))
    return 0
}
```

```bash
camel docs/tutorial/examples/07_exercise.cml
```

```text
19
```

## 参考

- [语言参考 3.4 可能为空 `T?` 与可能出错 `T!`](../language/03_data_model.md#34-可能为空-t-与可能出错-t)
- [语言参考 6.6 空值与错误的运算符](../language/06_expressions.md#66-空值与错误的运算符)
- [语言参考 7.3 `return`](../language/07_statements.md#73-return)