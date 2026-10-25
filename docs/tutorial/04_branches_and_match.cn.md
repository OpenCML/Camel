# 第 4 章 · 分支与 `match`

[English](04_branches_and_match.en.md) | 中文 · [返回目录](tutorial.cn.md)

到这一章为止，程序还只会一直往下算。真实程序要按情况走不同的路，而 Camel 与别的语言有一处不一样：
**分支是一个有值的表达式，不是一条语句**。这一章讲两件事：`if` 与 `match` 怎么写，以及为什么"提前返回"
在 Camel 里不存在——顺带看看编译器在你不小心按别的语言的直觉写时会说什么。

## `if` 是表达式

[`examples/04_branches.cml`](examples/04_branches.cml)：

```cml
// if is an expression; match compares a name with literal patterns.
// Run: camel docs/tutorial/examples/04_branches.cml

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

```bash
camel docs/tutorial/examples/04_branches.cml
```

```text
positive negative zero
vowel space other
A B D
```

`if 条件 then 值 else 值` 里的 `then` 与 `else` 都是必须的（作为表达式用时），两臂的类型必须相同。

## `match`：拿一个值对照几种写法

<!-- camel-snippet: fragment: 语法骨架，说明 match 的写法，不是一份完整的程序 -->

```cml
match 被匹配的表达式 {
    case 模式 | 模式 ... when 条件 => 值
    case _ => 值
}
```

- **模式是字面量**（整数、浮点数、字符串、`true`/`false`）或通配 `_`；一个 `case` 可以用 `|` 列出多个。
  按书写顺序取第一个匹配的臂。
- **`case _` 必须写**（除非每个值都被列到了，比如 `bool` 写全 `true` 和 `false`）。这不是风格问题，
  编译器会拒绝没有兜底的 `match`，理由很直接：被匹配的值不在你列的几种里时，这个表达式的值就不存在了。
- **`when` 给一个 `case` 加条件**：`case 1 when n > 0 => ...` 只在模式匹配**并且**条件成立时取这一臂。

[`examples/04_guards.cml`](examples/04_guards.cml)：

```cml
// A `when` guard adds a condition to a case; the first arm that matches is taken.
// Run: camel docs/tutorial/examples/04_guards.cml

func label(n: int): string {
    return match n {
        case 1 when n > 0 => 'one, positive'
        case 1 => 'one'
        case _ => 'other'
    }
}

func main(): int sync {
    println(label(1), label(2), label(0))
    return 0
}
```

```bash
camel docs/tutorial/examples/04_guards.cml
```

```text
one, positive other other
```

## 写错了会看到什么：漏了 `case _`

反例——这个 `match` 只列了 `1`：

<!-- camel-snippet: counter-example: 反例：这个 match 没有兜底，编译器拒绝它，页面展示诊断 -->

```cml
func a(n: int): int {
    return match n {
        case 1 when n > 0 => 10
    }
}
```

```text
error[SemanticDiag::MatchWithoutDefault]: This 'match' on a value of type 'int' has no 'case _'.
help: A 'match' must have a default case: add 'case _ => ...' for the values the cases do not list. A
      'match' on a bool that has both 'case true' and 'case false' is complete, and so is one over 'T?'
      or 'T!' with its two fixed patterns.
```

`match` 的臂类型也必须一致，否则是 `SemanticDiag::BranchReturnTypeMismatch`；在 `string` 上写 `case 1`
是 `SemanticDiag::MatchCaseTypeMismatch`（模式里的字面量按被匹配值的类型读，`string` 不接受 `1`）。

## 最容易掉进去的坑：块里的 `return`

在图上，分支的每个臂是**它自己的一张子图**，所以臂的块里写的 `return` 给出的是**这个分支的值**，
不是离开函数。按别的语言的直觉写，就会掉进这个坑：

[`examples/04_block_return.cml`](examples/04_block_return.cml)：

```cml
// `return` inside a block gives the value of that block; it does not leave the function.
// Run: camel docs/tutorial/examples/04_block_return.cml

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

```bash
camel docs/tutorial/examples/04_block_return.cml
```

```text
reached the end
negative
```

`wrong(-1)` 得到 `'reached the end'`：那个 `if` 的值没有人用，所以两个 `return` 对函数没有影响。编译器
看得出这种写法，对两个 `return` 各给一条警告：

```text
warning[SemanticDiag::ExitInUnusedBranch]: This 'return' gives the value of the 'if' it is in, and nothing uses that value: it does not leave the function.
  --> docs/tutorial/examples/04_block_return.cml:6:21
   |
 6 |     if x < 0 then { return 'negative' } else { return 'not negative' }
   |                     ^^^^^^^^^^^^^^^^^
note: the value of this 'if' is not used
```

Camel 里没有"提前返回"：要提前结束，就让分支成为函数体最后那个表达式，或者像 `right` 那样在它前面写
`return`。这也是"函数体的值是最后一个表达式的值"（第 3 章）那条规则的直接后果。

## 在图上

- `if` 与 `match` 在图上是**分支节点**：条件是它的一个输入，每个臂是它自己的一张子图，臂的值汇到出口。
- 这也解释了上面那个坑：`if` 的出口没人接，臂里的 `return` 就只是给那个没人要的出口送了个值。
- `match` 的几个臂和图上的分支是同一回事；`when` 条件进的是分支的条件。

## 练习

写一个 `describe(n: int): string`：`0` 是 `'zero'`，负数（`0` 以外）是 `'negative'`，其余按奇偶给出
`'even'` 或 `'odd'`。用 `if` 表达式串起来，`match` 收尾，并想清楚哪里必须写 `case _`。

参考答案 [`examples/04_exercise.cml`](examples/04_exercise.cml)：

```cml
// The answer to the exercise of chapter 4: an `if` is an expression, and a `match` needs its default.
// Run: camel docs/tutorial/examples/04_exercise.cml

func describe(n: int): string {
    return if n == 0 then 'zero'
        else if n < 0 then 'negative'
        else match n % 2 {
            case 0 => 'even'
            case _ => 'odd'
        }
}

func main(): int sync {
    println(describe(0), describe(-3), describe(7), describe(8))
    return 0
}
```

```bash
camel docs/tutorial/examples/04_exercise.cml
```

```text
zero negative odd even
```

## 参考

- [语言参考 6.8.1 `if`](../language/06_expressions.md#681-if)与 [6.8.2 `match`](../language/06_expressions.md#682-match)
- [语言参考 6.8 `match type`](../language/06_expressions.md#683-match-type)（第 12 章用到）
- [`SemanticDiag::MatchWithoutDefault`](../diagnostics.md#matchwithoutdefault)（诊断目录里的原话与改法）