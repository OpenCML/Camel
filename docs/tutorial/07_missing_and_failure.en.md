# Chapter 7 · Missing and failing

[中文](07_missing_and_failure.cn.md) | English · [Back to the contents](tutorial.en.md)

Looking a key up, parsing a piece of text, dividing one number by another — none of these has to succeed, and the
easiest mistake in code like this is forgetting the time it does not. Camel writes "may be nothing" and "may fail"
into the type: `T?` and `T!`. This chapter covers how they are made, how they are opened, and why "the divisor is
zero" is not one of them — that is a **fault**, which is a different thing.

## May be nothing: `T?`

[`examples/07_nullable.cml`](examples/07_nullable.cml):

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

- A value of type `int?` is either `null` or an `int`. `find` returns `null` when it does not find the target and the
  index when it does.
- There are two ways to open a `T?`: **`a ?? b`** says "if it is `null`, take `b`", and **`match`** with `case null`
  and `case v` (where `v` binds the `T` inside). There is no third way: an `int?` is not an `int`, so
  `println(v + 1)` does not compile.
- Chapter 6's `m['alice']` has this type too: the key may not be there, so reading a key gives a `V?`.
- `null` itself prints as `null`.

## May fail: `T!`

`T?` says "there may be no value"; `T!` says "there may be a value, or it may carry an error". An error is a **value**,
not a control flow:

[`examples/07_errors.cml`](examples/07_errors.cml):

```cml
// A function that may fail returns T!; `throw` returns an error value.
// Run: camel docs/tutorial/examples/07_errors.cml

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

- **`throw e` returns an error value** and is the same structure as `return`: it does not unwind the stack, and the
  caller receives an `int!`. The error has three fields: `e.kind`, `e.message` and `e.location`.
- The three ways to open one: `a !! b` (take `b` on failure), `x -> f` (pass the error on unchanged, otherwise call
  `f`; the operators chain), and `match` with `case error e` and `case v`.
- The last line, `println(safe_div(1, 0))`, prints the error itself. It also makes the compiler give a warning —
  **when an `int!` is dropped, the error inside it is lost**:

```text
warning[SemanticDiag::UnusedFallibleValue]: This value has type 'int!' and nothing uses it: an error it holds is lost.
```

Its `help` gives the fix: either supply a substitute with `!!` or handle it with `match`. **Why that warning
exists**: the whole value of `T!` is that the error is seen; dropping it is quietly going back to "something went
wrong and nobody noticed".

## A fault is not an error value

Division by zero, an index out of range and recursion that runs too deep are **faults**: the program stops, with a
position, and `!!` cannot catch them. Here is `xs[5]` on a three-element array:

[`examples/07_fault.cml`](examples/07_fault.cml):

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

The `before` that was printed comes out; `after` never does:

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> docs/tutorial/examples/07_fault.cml:7:15
   |
 7 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```

**Why the two are kept apart**: an error value is a normal case the program has to handle (a key that is not there,
a text that is not a number), so it is part of the value and the type makes you look at it; a fault is the program
being wrong (an index that is miscounted, recursion with no way out), so it stops with a position and you go and fix
it.

## On the graph

- On the graph `int`, `int?` and `int!` are **the same kind of value**: the "may be `null`, may carry an error" is one
  bit of a mark carried on the value.
- Opening it with `??`, `!!`, `->` or `match` is a **branch node**: the condition is that bit, one arm takes the
  value and the other takes the nothing-or-error. Handling failure is not control-flow magic, it is an ordinary
  branch.
- When an `int!` is dropped — nothing reads it — that path on the graph ends in nothing, and that is how the compiler
  sees `UnusedFallibleValue`.

## Exercise

Add up the numbers in `['12', 'x', '7']`, counting a text that is not a number as `0`. Use `parse_int` (which returns
an `int!`) and one `!!`.

The answer is [`examples/07_exercise.cml`](examples/07_exercise.cml):

```cml
// The answer to the exercise of chapter 7: the values that are there, and 0 for the ones that are not.
// Run: camel docs/tutorial/examples/07_exercise.cml

func sum_ints(words: string[]): int {
    return words->map<(s: string): int => parse_int(s) !! 0>->reduce<(acc: int, x: int): int => acc + x, 0>
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

## References

- [Language reference 3.4 May be null `T?` and may fail `T!`](../language/en/03_data_model.md#34-may-be-null-t-and-may-fail-t)
- [Language reference 6.6 Operators for null and errors](../language/en/06_expressions.md#66-operators-for-null-and-errors)
- [Language reference 7.3 `return` and `throw`](../language/en/07_statements.md#73-return-and-throw)