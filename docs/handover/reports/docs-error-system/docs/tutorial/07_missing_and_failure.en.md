# Chapter 7 · Missing and failing

[中文](07_missing_and_failure.cn.md) | English · [Back to the contents](tutorial.en.md)

Looking a key up, parsing a piece of text, reading a file — none of these has to succeed, and the easiest mistake in
code like this is forgetting the time it does not. Other languages have two answers: return a sentinel (`-1`, an empty
string), or throw an exception. The first lets you forget to check; the second sends control somewhere you cannot see.
Camel writes "may be nothing" and "may fail" into the type: `T?` and `T!`. They are ordinary values, the type makes you
look at them, and a pipeline only has to write the road where everything goes well — a failure slides along the track
beside it to the end, where it is handled once. This chapter covers how they are made, how they are opened, how they
are chained into a pipeline, and why "the divisor is zero" is not one of them — that is a **fault**, which is a
different thing.

## May be nothing: `T?`

[`examples/07_nullable.cml`](../../../../../tutorial/examples/07_nullable.cml):

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

- An `int?` is either `null` or an `int`. `find` returns `null` when it finds nothing, and the index when it does.
- There are three ways to open a `T?`: **`a ?? b`** says "take `b` if it is `null`", **`match`** has `case null` and
  `case v` (`v` is bound to the `T` inside), and the pipe `->`, described below. An `int?` cannot be used as an `int`:
  `println(v + 1)` does not compile.
- The `m['alice']` of chapter 6 is this type too: the key may be missing, so reading a key gives a `V?`.
- `null` itself prints as `null`.

## May fail: `T!`

`T?` says "there may be no value"; `T!` says "there may be a value, or it may carry an error". An error is a **value**,
not a control flow:

[`examples/07_errors.cml`](examples/07_errors.cml):

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

- **`error('DivByZero', '…')` makes an error value, and `return` hands it over.** It does not unwind the stack and there
  is no "throw" and "catch": the caller receives an `int!`. The error has three fields: `e.kind`, `e.message` and
  `e.location` (the compiler fills in the place that made it). The language has no `throw`, `try` or `catch`.
- `return` gives the value of the block it is in and never leaves an outer block. To give an error when something is missing, write it as the two arms of a branch: `return match m[k] { case null => error('NotFound', k) case v => v }`; or do not handle it in the middle, let the failure travel along `->` to the end and handle it once with `??` or `!!` (next section).
- There are three ways to open one: `a !! b` (take `b` on failure), `match` with `case error e` and `case v`, and
  `->`: in `safe_div(8, 2)->inc`, `inc` accepts only an `int` while the left side is an `int!`, so on failure the error
  is carried on unchanged, and otherwise `inc` is called.
- The last line, `println(safe_div(1, 0))`, prints the error itself. It also makes the compiler give a warning —
  **when an `int!` is dropped, the error inside it is lost**:

```text
warning[SemanticDiag::UnusedFallibleValue]: This value has type 'int!' and nothing uses it: an error it holds is lost.
```

Its `help` gives the fix: either supply a substitute with `!!` or handle it with `match`. **Why that warning
exists**: the whole value of `T!` is that the error is seen; dropping it is quietly going back to "something went
wrong and nobody noticed".

## A pipeline: write only the road where it goes well

Back to the problem at the start: a job has several steps and every step may fail. Reading an age: turn the text into
a number, check that it makes sense, turn it into "which decade". With an `if` after every step the real logic drowns
in checks. Camel's way is to join them with `->` and handle the failure once, at the end:

[`examples/07_pipeline.cml`](examples/07_pipeline.cml):

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

The line in `label` is the centre of this chapter: `text->parse_int->check_age->decade !! 'unknown'`.

- **Every step writes only the case where things go well.** `decade` accepts only an `int`; it does not know that
  anything before it could have failed. `check_age` accepts only an `int` too.
- **The rule is short.** In `x->f`, if `x` is a `T?` or a `T!` and `f` accepts only a `T`: a null `x` gives null, an
  error gives that same error carried on, and otherwise `f` is called; the result is a `U?` or a `U!`. `'abc'->parse_int`
  is already an error, so `check_age` and `decade` are never called, the error is carried to the last step, and
  `!! 'unknown'` catches it.
- **`check_age` can make an error itself**: `'200'` reads as a number and fails at the second step; `why` uses `match`
  to read the kind and the message of that error. Wherever the failure happens, the error slides on from that step
  and every later step is skipped.
- **Whether `->` opens the value is decided by the signature of the function.** If the parameter of `f` is already an
  `int!` (or a generic that accepts it) the error is handed to it as it is, so it can deal with it itself; only when
  `f` accepts just an `int` does `->` open the value for it. So a function like `describe(r: int!)` still fits at the
  end of a pipeline as the one who handles it.
- **Handle it once, at the end.** At the end use `!!` to give a substitute, `??` for `null`, or `match` to read the
  error. With `T?` and `T!` stacked (`int?!`) it goes from the outside in, opening as many levels as are needed.
- On the graph these are ordinary branches whose condition is the bit on the value; there is no control-flow magic in
  "failure in a pipeline".

## A fault is not an error value

Division by zero, an index out of range and recursion that runs too deep are **faults**: the program stops, with a
position, and `!!` cannot catch them. Here is `xs[5]` on a three-element array:

[`examples/07_fault.cml`](../../../../../tutorial/examples/07_fault.cml):

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

The `before` printed first comes out, and `after` never does:

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
being wrong (an index that is miscounted, recursion with no way out, a caller that broke the function's precondition),
so it stops with a position and you go and fix it.

## Reporting a fault yourself: `fail`

Your own functions have preconditions too. A caller that passes an argument that breaks one is wrong; it is not a normal
case to handle. Report it with `fail<'Kind'>('message')`: it is the same fault as the built-in division by zero and
index out of range. The type of `fail` is `never`, so it can be one arm of a branch (see the example below).

## Isolating at a boundary: `attempt`

Sometimes you do need to stop a fault inside the program — a batch of input has one bad row and you do not want the
whole batch lost. `attempt(f)` is a **function transform**: it returns a new function with the same parameters as `f`
whose result type is the result of `f` plus `!`; a fault inside `f` becomes an error value at the boundary of that new
function, and you handle it with `!!`, `->` or `match`:

[`examples/07_attempt.cml`](examples/07_attempt.cml):

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

- There is no `try`/`catch`: the boundary must be **one function call**, not a stretch of written text. The
  computation on the graph is evaluated on demand, moved by the compiler, and run in parallel; only the boundary of a
  subgraph is stable. To isolate a piece of code, put it in a function (or a lambda).
- Three boundary rules: **the result is finished inside the boundary** (the fields of a returned struct and the
  elements of a returned array are evaluated inside, and faults in them count); **a stream is isolated pull by pull**
  (when `f` returns a stream each pull is inside the boundary, and the element type becomes `T!`); **effects are not
  rolled back** (what was printed or written before the fault stays).
- In `[4, 0, 10]->map<attempt(share)>` each element is isolated on its own, so only the middle one becomes an error and
  the others are computed as usual.
- `exit(n)` is not a fault and is not isolated.

## Three things, three ways to write them

| What happened | It is | How to write and handle it |
|---|---|---|
| There may be nothing (not found) | A value `T?` | Return `null`; handle with `??`, `->`, `match` |
| Failure caused by the environment or the input (text is not a number, file is missing) | A value `T!` | `return error('Kind', 'message')`; handle with `!!`, `->`, `match` |
| The caller broke a precondition (out of range, division by zero, `fail`) | A fault | The program stops with a position; when needed `attempt(f)` turns it into a `T!` at a boundary |

## On the graph

- On the graph `int`, `int?` and `int!` are **the same kind of value**: the "may be `null`, may carry an error" is one
  bit of a mark carried on the value.
- Opening it with `??`, `!!`, `->` (carrying a `T?` or `T!` on) or `match` is a **branch node**: the condition is that
  bit, one arm takes the value and the other takes the nothing-or-error. Handling failure is not control-flow magic, it
  is an ordinary branch.
- When an `int!` is dropped — nothing reads it — that path on the graph ends in nothing, and that is how the compiler
  sees `UnusedFallibleValue`.
- `attempt(f)` on the graph is one marked call of a subgraph: the boundary of the fault is the boundary of the subgraph.

## Exercises

1. Add up the numbers in `['12', 'x', '7']`, counting a text that is not a number as `0`. Use `parse_int` (which
   returns an `int!`) and one `!!`.
2. Add up the entries of `['50', 'x', '700', '7', '-3']` that read as a number between 0 and 100, counting the rest as
   `0`. Write an `in_range` that returns an `int!`, and make the function for each element with
   `s->parse_int->in_range !! 0`.

The answer is [`examples/07_exercise.cml`](examples/07_exercise.cml):

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

## Reference

- [Language reference 3.4 May be null `T?` and may fail `T!`](../language/en/03_data_model.md#34-may-be-null-t-and-may-fail-t)
- [Language reference 6.6 Operators for null and errors](../language/en/06_expressions.md#66-operators-for-null-and-errors)
- [Language reference 6.10.1 Fault isolation `attempt`](../language/en/06_expressions.md#6101-fault-isolation-attempt)
- [Language reference 7.3 `return` and `fail`](../language/en/07_statements.md#73-return-and-fail)
