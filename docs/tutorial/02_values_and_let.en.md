# Chapter 2 · Values, types and `let`

[中文](02_values_and_let.cn.md) | English · [Back to the contents](tutorial.en.md)

In chapter 1's `x * 2 + x * 2` the two `2`s are one value. The compiler has to know the type of every value you
write down: most of the time it works it out, sometimes it cannot, or it works out one you did not mean — then you
write the type down. This chapter answers one question: **when you write a value down, how does the compiler read
it?** It ends on a rule that will follow you everywhere: **numbers are not converted for you**.

## The shapes a value takes

[`examples/02_values.cml`](examples/02_values.cml) writes down one of each:

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

Reading the program against its output:

- `42` is an `int` (a 64-bit integer), `0.75` is a `float` (64-bit), `true` and `false` are `bool`, `'camel'` is a
  `string`. Single and double quotes mean the same thing.
- `[1, 2, 3]` is an array, written `int[]`; `(1, 'one')` is a tuple, written `(int, string)`, and its parts are
  `pair.0` and `pair.1`.
- `{ x: 3, y: 4 }` is a struct. Its type gets a name first — `type Point = { x: int, y: int }` — and then
  `let p: Point = ...`. Structs, maps and sets are chapter 6.
- `println` takes its arguments separated by commas and leaves one space between two values on a line; `println(p)`
  prints the struct itself.
- A conversion is always written out: `float(n)`, `int(3.99)`, `string(n)`. `int(3.99)` is `3` (it truncates
  towards zero).

## The type is usually left out

The type after `let` can almost always be left out; the compiler infers it from the initial value. Write it when it
is needed — `Point` in `let p: Point = ...` — because the initial value is a struct literal and does not say the
**name** you meant. Types are static: the type of a name never changes during its life.

**A literal takes its type from the context; a variable does not.** That is the whole difference between these two:

[`examples/02_literals.cml`](examples/02_literals.cml):

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

The `1` in `1 + 2.0` is read as `1.0` because of `2.0`, the `1` in `let b: f32 = 1` is read as the `f32` one, and
the `2` in `1.0 + 2` is read as `2.0` after the `float` on its left — **a literal has no type of its own, it takes
the one its context asks for**. The `x` in `scale` is the opposite: it is a variable of type `int`, so the
conversion is written as `float(x) * 1.5`. Written `x * 1.5`, the compiler does not convert for you:

<!-- camel-snippet: counter-example: an int variable times a float literal, which the compiler refuses -->

```cml
func scale(x: int): float {
    return x * 1.5
}
```

```text
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (int, float); operator '*' is not defined for these operand types.
```

**Why it is designed this way**: the type is part of the program, and a reader should not have to count assignments
to know what `x` is; writing the conversion out also puts an explicit node in the graph, so what turns into what is
on the page rather than in the compiler's head.

## What a wrong type looks like

Put an `int` variable where a `float` is expected and the compiler does not convert it either; it names both types:

<!-- camel-snippet: counter-example: the page shows the diagnostic the compiler gives, not a program that runs -->

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

The `help` line is the fix: write `float(n)`. **Text is not a number either**: `let x: int = '42'` gives the same
`DeclaredTypeMismatch`, and reading a number out of text is `int('42')` or `parse_int('42')`.

## A `let` is bound once, a `var` is not

<!-- camel-snippet: fragment: two statements taken out of a function body, not a program of their own -->

```cml
let a = 1
a = 2                    // an error: a name bound by let cannot be assigned again
```

Assigning to a name bound by `let` is a compile-time error (`SemanticDiag::ImmutablePassedToVarParam`; chapter 10
shows the whole diagnostic).

<!-- camel-snippet: fragment: three statements taken out of a function body, not a program of their own -->

```cml
var count = 0
count = count + 1
count += 10
```

Almost no Camel program needs `var`: when you want a changed value, making a new one (`{ x: 5, y: p.y }`,
`concat(xs, [4])`) usually reads better than changing a variable. What `var` really needs care about is **order**:
which write is the read seeing? Chapter 10 answers that.

## On the graph

Every literal is **one `DATA` node**: `42` is a node, `[1, 2, 3]` is a node, `{ x: 3, y: 4 }` is a node as well. A
`let` makes no node at all — it only gives that node a name, which is why "assign to a `let` again" has nothing to
say on the graph. A `var` is different: it is a cell that can be written, reads and writes are joined by edges, and
the order is drawn (chapters 9 and 10 show those edges).

## Exercise

Celsius to Fahrenheit: write `to_fahrenheit(c: float): float` returning `c * 9.0 / 5.0 + 32.0`, print
`to_fahrenheit(100.0)` from `main`, then print the result for `37.0` with one decimal (`${...:.1f}`). Finally write
`let n = 3` and `let ratio = 1.5` and print their product — think first about what has to be written for that to
compile.

The answer is [`examples/02_exercise.cml`](examples/02_exercise.cml):

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

## References

- [Language reference 3.1 Overview of types](../language/en/03_data_model.md#31-overview-of-types) and
  [3.2 Numbers](../language/en/03_data_model.md#32-numbers)
- [Language reference 7.2 `let` and `var`](../language/en/07_statements.md#72-let-and-var)
- [`SemanticDiag::DeclaredTypeMismatch`](../diagnostics.md#declaredtypemismatch) (the catalogue: the compiler's own
  words and the fix)