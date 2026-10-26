# 第 7 章 · 缺失与失败

[English](07_missing_and_failure.en.md) | 中文 · [返回目录](tutorial.cn.md)

查一个键、解析一段文本、读一个文件——这些都可能不成功，而写这类代码最容易犯的错是忘掉那一次不成功。别的语言
有两种办法：返回一个哨兵值（`-1`、空串），或者抛异常。前者让你忘了检查，后者让控制流跳到你看不见的地方。
Camel 的办法是把"可能为空"和"可能出错"写进类型：`T?` 与 `T!`。它们是普通的值，类型逼着你看见它们，
而一条管道只需要写顺利的那条路——失败沿着旁边的轨道一路滑到末尾，在那里处理一次。这一章讲怎么造出它们、
怎么拆开它们、怎么把它们串成流水线，以及为什么"除数为零"不属于它们——那叫**故障**，是另一回事。

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
- 拆开一个 `T?` 有三种写法：**`a ?? b`** 说"是 `null` 就取 `b`"，**`match`** 的 `case null` 与 `case v`
  （`v` 绑定成里面的 `T`），以及下面要讲的管道 `->`。`int?` 不能当 `int` 用，`println(v + 1)` 编译不过。
- 第 6 章那条 `m['alice']` 也是这个类型：键可能不在，所以读一个键得到的是 `V?`。
- `null` 本身可以打印成 `null`。

## 可能出错：`T!`

`T?` 说的是"可能没有值"，`T!` 说的是"可能有值，也可能带着一个错误"。错误是一个**值**，不是控制流：

[`examples/07_errors.cml`](examples/07_errors.cml)：

```cml
// A function that may fail returns T!; `error(...)` builds the error value and `return` hands it over.
// Run: camel docs/tutorial/examples/07_errors.cml

func safe_div(a: int, b: int): int! {
    return if b == 0 then error('DivByZero', 'the divisor is zero') else a / b
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
    println(describe(safe_div(8, 2)->inc))
    println(describe(safe_div(8, 0)->inc))
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

- **`error('DivByZero', '…')` 造出一个错误值，`return` 把它交出去。** 它不展开调用栈，也没有"抛"与"接"：
  调用方拿到的就是一个 `int!`。错误值有三个字段：`e.kind`、`e.message`、`e.location`（位置由编译器填成造出它的地方）。
  语言里没有 `throw`、`try`、`catch`。
- `return` 只给出它所在的那个块的值，从不离开外层的块。想在缺失时给出错误，把它写成分支的两条臂：`return match m[k] { case null => error('NotFound', k) case v => v }`；或者不在中间处理，让失败沿 `->` 传到末尾，再用 `??`、`!!` 处理一次（下一节）。
- 拆开的写法有三种：`a !! b`（出错取 `b`）、`match` 的 `case error e` 与 `case v`，以及 `->`：
  `safe_div(8, 2)->inc` 里 `inc` 只接受 `int`，而左边是 `int!`，于是出错时错误原样往下带，否则调用 `inc`。
- 最后一行 `println(safe_div(1, 0))` 打印的是错误本身。它同时让编译器给出一条警告——**一个 `int!` 被
  丢掉时，里面的错误就没了**：

```text
warning[SemanticDiag::UnusedFallibleValue]: This value has type 'int!' and nothing uses it: an error it holds is lost.
```

诊断的 `help` 会给出改法：要么用 `!!` 给一个替代值，要么用 `match` 处理它。**为什么要有这条警告**：
`T!` 的全部价值就在于那个错误被看见；把它丢掉，就等于悄悄回到了"出错了当没出错"。

## 流水线：只写顺利的那条路

现在回到开头的问题：一件事要分好几步做，每一步都可能失败。读一个年龄：把文本变成数，检查它合不合理，再换成
"几十岁"。如果每一步后面都跟一个 `if`，真正的逻辑就淹没在检查里。Camel 的写法是把它们用 `->` 连起来，
最后只处理一次：

[`examples/07_pipeline.cml`](examples/07_pipeline.cml)：

```cml
// A pipeline writes only the good path. An error slips past every step after the one that made
// it, and is handled once, at the end.
// Run: camel docs/tutorial/examples/07_pipeline.cml

func check_age(age: int): int! {
    return if age < 0 || age > 150
        then error('OutOfRange', 'age ' + string(age) + ' is not possible')
        else age
}

func decade(age: int): string { return string(age / 10 * 10) + 's' }

func label(text: string): string {
    return text->parse_int->check_age->decade !! 'unknown'
}

func why(text: string): string {
    return match text->parse_int->check_age {
        case error e => e.kind + ': ' + e.message
        case v => 'ok ' + string(v)
    }
}

func main(): int sync {
    println(label('34'), label('200'), label('abc'))
    println(why('34'))
    println(why('200'))
    println(why('-5'))
    return 0
}
```

```bash
camel docs/tutorial/examples/07_pipeline.cml
```

```text
30s unknown unknown
ok 34
OutOfRange: age 200 is not possible
OutOfRange: age -5 is not possible
```

`label` 的那一行是这一章的中心：`text->parse_int->check_age->decade !! 'unknown'`。

- **每一步只写顺利的情形。** `decade` 只接受 `int`，它不知道前面可能出过错；`check_age` 也只接受 `int`。
- **规则很短。** `x->f` 中，如果 `x` 是 `T?` 或 `T!`，而 `f` 只接受 `T`：`x` 为空就得到空，出错就把错误原样带下去，
  否则调用 `f`，结果是 `U?` 或 `U!`。`'abc'->parse_int` 已经是一个错误，于是 `check_age` 和 `decade` 根本没有被调用，
  错误一路带到最后一步，被 `!! 'unknown'` 接住。
- **`check_age` 自己也可能产生错误**：`'200'` 能读成数，在第二步才失败；`why` 用 `match` 把那个错误的种类和消息读出来。
  失败发生在哪一步，错误就从那一步开始往后滑，之后的步骤都被跳过。
- **是否拆开由被调函数的签名决定。** 如果 `f` 的形参本来就是 `int!`（或者是接受它的泛型），错误就原样交给它，让它自己处理；
  只有 `f` 只接受 `int` 时才由 `->` 代为拆开。所以像 `describe(r: int!)` 这样的函数仍然能放进管道里当作末端的处理者。
- **处理一次，放在末尾。** 末尾用 `!!` 给一个替代值，用 `??` 对付 `null`，或者用 `match` 读出错误。`T?` 与 `T!` 叠起来
  （`int?!`）时由外往里，需要几层拆几层。
- 这些在图上都是普通的分支，条件是值上的那一位标记；"管道里的失败"没有任何控制流魔法。

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
类型逼着你看它；故障是程序写错了（下标算错、递归没有出口、调用者违反了函数的前提），所以它带着位置停下来，
让你去修。

## 自己报故障：`fail`

你自己写的函数也会有前提。调用者传来违反前提的实参，是调用者的错，而不是一种要处理的正常情况——这时用
`fail<'种类'>('消息')` 报一个故障，它与内建的除零、越界是同一种故障。`fail` 的类型是 `never`，所以它可以做分支的
一条臂（用法见下面的例子）。

## 在边界上隔离：`attempt`

有时你确实需要在程序里拦住一个故障——比如一批输入里有一条坏数据，不想让整批作废。`attempt(f)` 是一个**函数变换**：
它返回一个新函数，参数与 `f` 相同，结果类型是 `f` 的结果加上 `!`；`f` 里发生的故障在这个新函数的边界上变成一个错误值，
再用 `!!`、`->` 或 `match` 处理：

[`examples/07_attempt.cml`](examples/07_attempt.cml)：

```cml
// fail reports a broken precondition and attempt turns a fault into an error value, at the edge of a call.
// Run: camel docs/tutorial/examples/07_attempt.cml

func share(whole: int): int {
    return if whole <= 0 then fail<'InvalidArgument'>('whole must be positive') else 100 / whole
}

func main(): int sync {
    println(share(4))
    println(attempt(share)(0) !! -1)
    let all = [4, 0, 10]->map<attempt(share)>
    println(all->map<(r: int!): int => r !! -1>)
    println(match attempt(share)(0) {
        case error e => e.kind + ': ' + e.message
        case v => 'ok ' + string(v)
    })
    return 0
}
```

```bash
camel docs/tutorial/examples/07_attempt.cml
```

```text
25
-1
[25, -1, 10]
InvalidArgument: whole must be positive
```

- 没有 `try`/`catch`：边界必须是**一次函数调用**，而不是一段书写的范围。图上的计算按需求值、会被编译器移动、会并行执行，
  只有一张子图的边界是稳定的。想隔离一段代码，就把它放进一个函数（或 lambda）。
- 三条边界规则：**结果在边界内算完**（返回的结构体字段、数组元素都在边界里求值，其中的故障都算在里面）；
  **流逐次隔离**（`f` 返回流时每一次拉取都在边界内，元素类型变成 `T!`）；**效应不回滚**（故障之前已经发生的打印、
  写入不会撤销）。
- `[4, 0, 10]->map<attempt(share)>` 里每个元素各自隔离，所以只有中间那个变成错误，其余照常算出。
- `exit(n)` 不是故障，不被隔离。

## 三件事，三种写法

| 发生了什么 | 它是 | 怎么写、怎么处理 |
|---|---|---|
| 可能没有（查不到） | 值 `T?` | 返回 `null`；用 `??`、`->`、`match` 处理 |
| 环境或输入造成的失败（文本不是数、文件不存在） | 值 `T!` | `return error('种类', '消息')`；用 `!!`、`->`、`match` 处理 |
| 调用者违反了前提（越界、除零、`fail`） | 故障 | 程序带位置终止；需要时用 `attempt(f)` 在边界上变成 `T!` |

## 在图上

- 在图上，`int`、`int?`、`int!` **是同一种值**：那个"可能是 `null`、可能带着错误"是一位标记，附着在值上。
- `??`、`!!`、`->`（对 `T?`、`T!` 的传递）、`match` 拆开它时，图上都是一个**分支节点**：条件是那一位，一个臂走"有值"，
  另一个臂走"没有值/有错误"。所以"处理失败"不是控制流魔法，就是普通的分支。
- 一个 `int!` 被丢掉（没有任何东西读它）时，图上那条路径就断了——编译器正是从图上看出 `UnusedFallibleValue`。
- `attempt(f)` 在图上是对子图的一次带标记的调用：故障的边界就是子图的边界。

## 练习

1. 把 `['12', 'x', '7']` 里能读成数的加起来，读不出的当 `0`。用 `parse_int`（它返回 `int!`）和一个 `!!`。
2. 把 `['50', 'x', '700', '7', '-3']` 里"能读成数、并且在 0 到 100 之间"的加起来，其余的当 `0`。写一个
   返回 `int!` 的 `in_range`，用 `s->parse_int->in_range !! 0` 做成每个元素的函数。

参考答案 [`examples/07_exercise.cml`](examples/07_exercise.cml)：

```cml
// The answer to the exercises of chapter 7: the values that are there, and 0 for the ones that are not.
// Run: camel docs/tutorial/examples/07_exercise.cml

func sum_ints(words: string[]): int {
    return words->map<(s: string): int => parse_int(s) !! 0>->reduce<(acc: int, x: int): int => acc + x, 0>
}

func in_range(n: int): int! {
    return if n < 0 || n > 100 then error('OutOfRange', 'not between 0 and 100') else n
}

func score(s: string): int {
    return s->parse_int->in_range !! 0
}

func main(): int sync {
    println(sum_ints(['12', 'x', '7']))
    println(['50', 'x', '700', '7', '-3']->map<score>->reduce<(acc: int, x: int): int => acc + x, 0>)
    return 0
}
```

```bash
camel docs/tutorial/examples/07_exercise.cml
```

```text
19
57
```

## 参考

- [语言参考 3.4 可能为空 `T?` 与可能出错 `T!`](../language/03_data_model.md#34-可能为空-t-与可能出错-t)
- [语言参考 6.6 空值与错误的运算符](../language/06_expressions.md#66-空值与错误的运算符)
- [语言参考 6.10.1 故障隔离 `attempt`](../language/06_expressions.md#6101-故障隔离-attempt)
- [语言参考 7.3 `return` 与 `fail`](../language/07_statements.md#73-return-与-fail)
