# 第 2 章 · 值、类型与 `let`

[English](02_values_and_let.en.md) | 中文 · [返回目录](tutorial.cn.md)

第 1 章那个 `x * 2 + x * 2` 里，两个 `2` 是同一个值。你写下的每个值，编译器都要知道它是什么类型：多数时候
它自己推得出来，偶尔推不出来，或者推出来不是你想要的——那时你把类型写下来。这一章回答一个问题：**写下一个值，
编译器怎么看它？** 最后落到一条会一直影响你的规矩上：**数值之间不做隐式转换**。

## 值的各种写法

[`examples/02_values.cml`](examples/02_values.cml) 把这一章要讲的值写了一遍：

```cml
// Values, types, let and var.
// Run: camel docs/tutorial/examples/02_values.cml

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

```bash
camel docs/tutorial/examples/02_values.cml
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

对着输出读这份程序：

- `42` 是 `int`（64 位整数），`0.75` 是 `float`（64 位浮点），`true`、`false` 是 `bool`，`'camel'` 是 `string`。
  字符串用单引号还是双引号没有区别。
- `[1, 2, 3]` 是数组，类型写 `int[]`；`(1, 'one')` 是元组，类型写 `(int, string)`，取分量写 `pair.0`、`pair.1`。
- `{ x: 3, y: 4 }` 是结构体。类型先起个名字：`type Point = { x: int, y: int }`，然后 `let p: Point = ...`。
  它们和映射、集合一起在第 6 章讲。
- `println` 的实参用逗号分开，同一行里两个值之间空一格；写 `println(p)` 就把结构体本身打印出来。
- 转换必须写出来：`float(n)`、`int(3.99)`、`string(n)`。`int(3.99)` 是 `3`（向零取整）。

## 类型大多不用写

`let` 后面的类型几乎总可以省掉，编译器从初值推出来。需要时再写下来，例如 `let p: Point = ...` 里的 `Point`：
初值是一个结构体字面量，推不出你想要的那个**名字**。类型是静态的，一个名字的类型在它的一生里不会变。

**字面量跟着上下文取类型，变量不跟着。** 这一条把两种写法分开：

[`examples/02_literals.cml`](examples/02_literals.cml)：

```cml
// A literal takes the type its context asks for; a variable keeps its own type.
// Run: camel docs/tutorial/examples/02_literals.cml

func scale(x: int): float {
    return float(x) * 1.5
}

func main(): int sync {
    let a: float = 1 + 2.0      // the declared type is float, so both literals are read as float
    let b: f32 = 1              // the literal is read as the f32 one
    println(a, b, scale(3), 1.0 + 2)
    return 0
}
```

```bash
camel docs/tutorial/examples/02_literals.cml
```

```text
3.0 1.0 4.5 3.0
```

`1 + 2.0` 里的 `1` 按 `2.0` 读成 `1.0`，`let b: f32 = 1` 里的 `1` 读成 `f32` 的 `1`，`1.0 + 2` 里的 `2`
跟着左边读成 `2.0`——**字面量没有自己的类型，它按上下文取**。`scale` 里的 `x` 相反：它是一个变量，
类型是 `int`，所以要写 `float(x) * 1.5`。把它写成 `x * 1.5`，编译器不会替你转：

<!-- camel-snippet: counter-example: 反例：一个 int 变量乘一个 float 字面量，编译器拒绝它 -->

```cml
func scale(x: int): float {
    return x * 1.5
}
```

```text
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (int, float); operator '*' is not defined for these operand types.
```

**为什么这么设计**：类型是程序的一部分，读代码的人不该为了知道 `x` 是什么而回头数它被赋过几次；把转换写出来，
图上就多一个明确的节点，谁把什么变成什么都写在明面上。

## 写错类型会看到什么

把 `int` 的变量放进 `float` 的位置，编译器同样不转，它指出两边的类型：

<!-- camel-snippet: counter-example: 反例：页面展示编译器给出的诊断，而不是一段能跑的程序 -->

```cml
func scale(x: int): float {
    let n = 3
    let y: float = n        // the declared type is float, the value is int
    return x * y
}
```

```text
error[SemanticDiag::DeclaredTypeMismatch]: This value has type 'int', which is not the declared type 'float'.
help: A declaration written with a type takes a value of that type; nothing is converted on the way (a number does not become a float by itself, and a text does not become a number). Convert the value explicitly: 'float(n)' for a number of another type, 'int(s)' or 'parse_int(s)' for a text, 'string(n)' for a number written as text (the same for 'i32', 'f32', 'f64' and the complex types), 'v as T' for a module type; or change the declared type.
```

诊断里的 `help` 就是改法：写 `float(n)`。**文本也不是数**：`let x: int = '42'` 给的是同一个
`DeclaredTypeMismatch`，取数要写 `int('42')` 或 `parse_int('42')`。

## `let` 不能再绑，`var` 可以

<!-- camel-snippet: fragment: 两行是从函数体里摘出来的语句，不是一份完整的程序 -->

```cml
let a = 1
a = 2                    // 错误：let 绑定的名字不能再赋值
```

给 `let` 绑定的名字再赋值是编译期错误（`SemanticDiag::ImmutablePassedToVarParam`，第 10 章会看到完整的一条）。

<!-- camel-snippet: fragment: 三行是从函数体里摘出来的语句，不是一份完整的程序 -->

```cml
var count = 0
count = count + 1
count += 10
```

Camel 里绝大多数程序用不到 `var`：需要"改过的值"时，直接造一个新值（`{ x: 5, y: p.y }`、`concat(xs, [4])`）
往往比改一个变量更好读。`var` 真正要小心的是**顺序**：读到的是哪一次写的值？第 10 章回答。

## 在图上

每个字面量在图上是**一个 `DATA` 节点**：`42` 是一个节点，`[1, 2, 3]` 是一个节点，`{ x: 3, y: 4 }` 也是一个
节点。`let` 不产生节点，它只是给那个节点起个名字——这也是为什么"给 `let` 再赋一次值"在图上是无话可说的。
`var` 不一样：它是一个可以被写的格子，读和写之间有边连起来，顺序就写在图上（第 9、10 章会看到这些边）。

## 练习

摄氏转华氏：写一个函数 `to_fahrenheit(c: float): float`，返回 `c * 9.0 / 5.0 + 32.0`，在 `main` 里打印
`to_fahrenheit(100.0)`；再打印 `37.0` 的结果，只保留一位小数（用 `${...:.1f}`）。最后写 `let n = 3`、
`let ratio = 1.5`，把 `n` 和 `ratio` 相乘打印出来——注意先想清楚要写什么才能编译。

参考答案 [`examples/02_exercise.cml`](examples/02_exercise.cml)：

```cml
// The answer to the exercise of chapter 2: a value of one type is not another type.
// Run: camel docs/tutorial/examples/02_exercise.cml

func to_fahrenheit(c: float): float {
    return c * 9.0 / 5.0 + 32.0
}

func main(): int sync {
    let c = 100.0
    println(to_fahrenheit(c))
    println(`${to_fahrenheit(37.0):.1f}`)

    // Two variables of different types do not meet: the conversion is written out.
    let n = 3
    let ratio = 1.5
    println(float(n) * ratio)
    return 0
}
```

```bash
camel docs/tutorial/examples/02_exercise.cml
```

```text
212.0
98.6
4.5
```

## 参考

- [语言参考 3.1 类型一览](../language/03_data_model.md#31-类型一览)与 [3.2 数值](../language/03_data_model.md#32-数值)
- [语言参考 7.2 `let` 与 `var`](../language/07_statements.md#72-let-与-var)
- [`SemanticDiag::DeclaredTypeMismatch`](../diagnostics.md#declaredtypemismatch)（诊断目录里的原话与改法）