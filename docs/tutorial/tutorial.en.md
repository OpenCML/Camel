# Camel Tutorial

English | [中文](tutorial.cn.md)

This tutorial is for readers who know one mainstream programming language and have never seen Camel. When you have finished it you will be able to write and run general-purpose Camel programs, you will know where Camel differs most from other languages, and you will be able to use passes on the command line to look at the graph, optimise it, change the back end, export it and take gradients.

The program in every section is in the [`examples/`](examples/) directory, and the outputs shown are their real outputs. All commands are run from the repository root.

Camel is a graph-native general-purpose programming language: a program expresses only computational intent — what to compute, and what depends on what; every element of the language corresponds directly to a piece of structure in the computation graph. As you read, keep asking this question: **what is this code in the graph?**

Contents:
[1 Your first program](#1-your-first-program) ·
[2 Values and types](#2-values-and-types) ·
[3 Functions](#3-functions) ·
[4 Branches and match](#4-branches-and-match) ·
[5 Arrays and higher-order operators](#5-arrays-and-higher-order-operators) ·
[6 Evaluation by dependency, sync and the function body](#6-evaluation-by-dependency-sync-and-the-function-body) ·
[7 Closures and function values](#7-closures-and-function-values) ·
[8 Generics](#8-generics) ·
[9 Error handling](#9-error-handling) ·
[10 Nullable values](#10-nullable-values) ·
[11 Maps and sets](#11-maps-and-sets) ·
[12 Modules and the standard library](#12-modules-and-the-standard-library) ·
[13 Looking at the graph](#13-looking-at-the-graph) ·
[14 Changing the passes](#14-changing-the-passes) ·
[15 Tensors and exporting a model](#15-tensors-and-exporting-a-model) ·
[16 Taking gradients](#16-taking-gradients) ·
[Where to go next](#where-to-go-next)

Chapters already written to the new outline (Chinese and English together, each on its own page):
[1 Five minutes to see the graph](01_five_minutes.en.md) ·
[2 Values, types and `let`](02_values_and_let.en.md) ·
[5 Arrays and loops](05_arrays_and_loops.en.md) ·
[9 Evaluation by dependency](09_evaluation_by_dependency.en.md) ·
[17 Teaching the compiler a law](17_teaching_a_law.en.md)

---

## 1. Your first program

First get the `camel` executable: build it from source by following [setup.en.md](../setup.en.md), and add `out/latest/bin` to your `PATH`. Once `camel --version` prints a version, you are ready to start.

[`examples/01_hello.cml`](examples/01_hello.cml):

```cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel docs/tutorial/examples/01_hello.cml
```

```text
Hello, Camel!
```

- One `.cml` file is one module, and `module hello` gives it a name (this can be omitted).
- The program starts at `main`. The `int` that `main` returns is the exit code of the process.
- The **`sync`** after the return type means that the function body is a sync block: the statements it writes directly run in the order they are written (a sync block does not inherit inward; section 6 explains). For now, just remember "a function that prints has `sync` before its body"; section 6 explains why.
- A line break is enough between statements; no semicolons are needed.

## 2. Values and types

[`examples/02_values.cml`](examples/02_values.cml):

```cml
module values

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

```text
42 0.75 true camel
[1, 2, 3] 1 3
(1, one) 1 one
{ x: 3, y: 4 } 7
31.5
3 42!
11
```

Key points:

- Types are static and can usually be inferred, so you do not have to write them. The basic types are `int`, `float`, `bool` and `string`; the compound types are arrays `T[]`, tuples `(A, B)` and structs `{ x: A }`. `type` gives a type a name.
- **There is no implicit conversion between numeric types.** `n * ratio` is a compile-time error; write `float(n) * ratio`.
- `let` is an immutable name, and `var` is a mutable variable. Camel favours immutability: most programs hardly ever need `var`.
- **A variable can be assigned to, and so can one of its fields or elements** (`p.x = 5` and `xs[0] = 5` both work): that gives the whole variable a new value with the change in it, and after `let a = p`, changing `p` leaves `a` as it was. Without `var` you can also build a new value directly: `{ x: 5, y: p.y }`, `concat(xs, [4])`.
- Strings can use either single or double quotes. Strings are measured in bytes: `len('中文')` is 6. Functions for splitting, trimming, searching and so on are in the `str` module (section 12).
- Two details of how code is written: the parenthesis of a call must directly follow the function name (`f(1)`, not `f (1)`); and when a statement is continued on the next line, leave the operator at the end of the line, or start the next line with an operator that can only be binary such as `->`, `.`, `*`, `&&` (a line that starts with `-` or `+` is a new statement).

## 3. Functions

[`examples/03_functions.cml`](examples/03_functions.cml):

```cml
module functions

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

```text
49
6765
5000050000
15
50
```

- **A function is a subgraph, and a call is a node in the graph.** You will see this with your own eyes in section 13.
- **Repeated computation has three forms**: the `loop` expression, recursion, and the higher-order operators of the next section. A `loop` writes its state in its head, and the body's value is the next round's state, as in `loop acc = 0 for x in xs do acc + x * x`; it can also take a `while` condition ([language reference 6.8.6](../language/en/06_expressions.md#686-loop)), and its rounds do not count toward the call depth. A call in tail position does not take a stack frame, so `sum_to` recursing a hundred thousand levels deep is no problem; for non-tail recursion (such as `fib`), the number of calls open at the same time may not exceed 1000 by default (adjustable with `--max-recursion-depth`).
- **Two kinds of parameters.** The ones in parentheses are the inputs the function processes; the ones in `with <...>` are the properties the function is configured with (context parameters), and at a call they are written in angle brackets. Configuration that other languages put in the members of an object is a `with` parameter in Camel.
- **The pipe `->`** passes the value on its left as the argument of the function on its right; `a->f->g` is `g(f(a))`. Data flow written as a chain is exactly a path in the graph.

## 4. Branches and match

[`examples/04_branches.cml`](examples/04_branches.cml):

```cml
module branches

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

```text
positive negative zero
vowel space other
A B D
```

- `if ... then ... else ...` is an expression and has a value; the two arms must have the same type.
- `match` matches on a **name** (which is why `grade` first writes `let band = ...`). A pattern is a literal or `_`, and one `case` can list several patterns with `|`. `case _` is required: a `match` without a default case is a compile-time error (except on a `bool` that lists both `true` and `false`).

### The easiest trap to fall into: `return` in a block

In the graph, each arm of a branch is a subgraph of its own. So **a `return` written in the block of an arm gives the value of that branch; it does not leave the function**. [`examples/04_block_return.cml`](examples/04_block_return.cml):

```cml
module block_return

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

```text
reached the end
negative
```

`wrong(-1)` gives `'reached the end'`. The compiler can see this pattern and gives one warning for each of the two `return`s:

```text
warning[SemanticDiag::ExitInUnusedBranch]: This 'return' gives the value of the 'if' it is in, and nothing uses that value: it does not leave the function.
  --> docs/tutorial/examples/04_block_return.cml:7:21
   |
 7 |     if x < 0 then { return 'negative' } else { return 'not negative' }
   |                     ^^^^^^^^^^^^^^^^^
note: the value of this 'if' is not used
```

There is no "early return" in Camel. Make the branch the last expression of the function body, or write `return` in front of it as `right` does.

## 5. Arrays and higher-order operators

[`examples/05_arrays.cml`](examples/05_arrays.cml):

```cml
module arrays

func main(): int sync {
    let xs = 1..11    // [1, ..., 10]
    let squares = xs->map<(x: int): int => x * x>
    let evens = squares->filter<(x: int): bool => x % 2 == 0>
    let total = evens->reduce<(acc: int, x: int): int => acc + x, 0>

    println(xs)
    println(squares)
    println(evens)
    println(total)

    // Arrays are values: slice and concat return new arrays.
    let ys = concat(xs->slice<0, 3>, [99])
    println(ys, len(ys), head(ys))

    // foreach runs its function for each element, in order.
    ['a', 'b', 'c']->foreach<(s: string) => sync { println('item', s) }>
    return 0
}
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

- `(x: int): int => x * x` is a lambda. The function passed to `map` is written in angle brackets because it is a `with` parameter of `map`: the array is the data flowing through, and the function is the configuration.
- The elements of a `map` or a `filter` have no order among themselves, so a parallel back end can compute them at the same time, and the results are still arranged by position; `reduce` and `foreach` run in element order.
- The lambda in `foreach` is written `=> sync { ... }`: a function body that is to produce an effect must be a sync block. The next section explains.

### Counter-example: do not put stateful calls in the element function of `map`

There is **no agreed order of execution** between the elements of a `map`. When the element function is pure this does not matter; once it calls something stateful, the result depends on which element is computed first. [`examples/05_map_order.cml`](examples/05_map_order.cml) demonstrates this with the random number generator (each call of `randint` advances the hidden state by one step):

```cml
module map_order

import { seed, randint } from random

func main(): int sync {
    seed(42)
    // Wrong tool: randint advances a hidden state, so each element depends on which ran first.
    let drawn = 0..8->map<(i: int): int => randint(0, 99)>
    println(drawn)

    seed(42)
    // reduce runs in element order on every back end.
    let ordered = 0..8->reduce<(acc: int[], i: int): int[] => concat(acc, [randint(0, 99)]), [0]->slice<0, 0>>
    println(ordered)
    return 0
}
```

On the sequential back ends (the default `std::nvm`, and also `std::fvm` and `std::jit`), the two lines are the same, and the same on every run:

```text
[75, 63, 75, 13, 90, 9, 57, 37]
[75, 63, 75, 13, 90, 9, 57, 37]
```

Switch to parallel scheduling, `camel docs/tutorial/examples/05_map_order.cml std::prl`, and the order of the first line is no longer guaranteed and may differ from run to run; below is the output of one such run:

```text
[37, 57, 9, 90, 13, 75, 63, 75]
[75, 63, 75, 13, 90, 9, 57, 37]
```

This is behaviour laid down by the language (design principles, 3.6), not a defect: it is precisely because there is no order between the elements that `map` can run in parallel. The `reduce` used for the second line runs in element order, and its result is the same on all back ends. The rule is simple: **keep the functions passed to `map` and `filter` pure; do what must be done in order with `foreach`, `reduce` or tail recursion.**

## 6. Evaluation by dependency, sync and the function body

This is where Camel differs most from other languages, and it is worth reading slowly.

### 6.1 An effect that nothing depends on does not run

In most languages, statements run one after another. In Camel, by default, **evaluation is driven by dependencies**: an expression is evaluated because someone needs its result. Expressions that do not depend on each other have no order and can run in parallel.

This has a consequence: **outside a sync block, a call whose result nobody uses does not run**, even if it has side effects. [`examples/06_unused_effect.cml`](examples/06_unused_effect.cml):

```cml
module unused_effect

func greet(): int {
    println('hello from greet')     // not used, not in a sync block: never runs
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```bash
camel docs/tutorial/examples/06_unused_effect.cml
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> docs/tutorial/examples/06_unused_effect.cml:6:5
   |
 6 |     println('hello from greet')     // not used, not in a sync block: never runs
   |     ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the
      variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync
      block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
```

Only `1` is printed. The return value `1` of `greet` does not depend on that `println`, so in the graph it is a node that nobody needs. This is not an error; it is a rule of the language. The compiler gives a warning so that a forgotten `sync` gets seen.

### 6.2 Sync blocks

A `sync` written before a block makes it a sync block: **the statements it writes directly** run in the order they are written, as if the compiler had added control edges between adjacent statements. [`examples/06_sync_block.cml`](examples/06_sync_block.cml) changes only the body of `greet` into a sync block:

<!-- camel-snippet: compile-only: the fence is the changed function only; the text block is the output of examples/06_sync_block.cml -->

```cml
func greet(): int sync {
    println('hello from greet')
    return 1
}
```

```text
hello from greet
1
```

In a sync block it does not matter what you call or whether it has side effects, because there is already an order. Input and output are written in sync blocks.

**A sync block does not inherit inward.** It orders only the statements it writes directly: the branch arms, loop bodies and nested blocks written inside it follow their own spelling, and to synchronise one of them, write `sync` there ([design principles 3.4](../design-principles.en.md#34-sync-blocks)). This is the same rule as "a function's behaviour is decided by the function itself" in the next section, one level in:

```cml
func f(c: bool): int sync {
    if c then { println(1); println(2) }   // the arm is not a sync block: only println(2) executes
    return 0
}
```

The `if` itself is on the sync chain, its arm is not: the arm is evaluated by dependency, the `println(1)` whose value nobody uses does not execute, and `println(2)` is the arm's value, which is the `if`'s value, so it executes. To print both, write `sync` on the arm: `if c then sync { println(1); println(2) }`.

### 6.3 A function's behaviour is decided by the function itself

The body of `main` is a sync block, so will a `println` inside a function it calls run? No. A function is compiled into a subgraph of its own, and a call only places that subgraph node into a larger graph; it does not change its structure. [`examples/06_callee_decides.cml`](examples/06_callee_decides.cml):

```cml
module callee_decides

func log(x: int): int {
    println(x)          // result unused, body is not a sync block: not executed
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    println(b)          // prints 3, and nothing else
    return 0
}
```

```text
3
```

The two calls of `log` did run in order (`b` is 3), but `println(x)` belongs to the body of `log` itself, and that is not a sync block (the compiler gives the `EffectNotExecuted` warning all the same). To print, declare `log` as `func log(x: int): int sync { ... }`.

### 6.4 `sync` and the function body

When `sync` is written in the **function body** position (`func g(): int sync { ... }`), it modifies the block that is the function body: the statements of the block execute in the order they are written. A function body without `sync` is not a sync block, and effects such as `println` in it do not run (evaluation by dependency). [`examples/06_sync_body.cml`](examples/06_sync_body.cml):

```cml
module sync_body

func f(): int {
    println('f')        // not executed: the body is not a sync block
    return 1
}

func g(): int sync {
    println('g')        // executed: the body is a sync block
    return 2
}

func main(): int sync {
    println(f() + g())
    return 0
}
```

```text
g
3
```

A `sync` written before a lambda (`sync (x: int) => ...`) is a different use: it modifies the function type, and is unchanged.

### 6.5 `var`, references and order

In Camel every value is passed by reference. Writing `var` before a parameter means that the function will modify it; the modification is a write to the caller's variable. Passing a `let` to a `var` parameter is a compile-time error ([`examples/06_let_to_var.cml`](examples/06_let_to_var.cml)):

```text
error[SemanticDiag::ImmutablePassedToVarParam]: 'k' is immutable and cannot be passed to a var parameter of 'bump'.
   --> docs/tutorial/examples/06_let_to_var.cml:11:10
    |
 11 |     bump(k)
    |          ^
help: Only a variable declared with 'var' (or a var parameter) can be modified. Declare it with
      'var', or bind the new value to a new name.
```

Once there are mutable variables, there is a question of order: which write must a read come after? The answer is the sync block: the statements written in one sync block execute in the order they are written, so two writes and a later read have a determined order. [`examples/06_var_order.cml`](examples/06_var_order.cml):

```cml
module var_order

func bump(var counter: int): int sync {
    counter += 1
    return counter
}

func twice(): int sync {
    var c = 0
    bump(c)
    bump(c)
    return c
}

func main(): int sync {
    println(twice())
    return 0
}
```

```text
2
```

The body of `twice` is a sync block, so the two calls of `bump` and `return c` execute in the order they are written. A few more points:

- A write that is not in a sync block has no order relative to earlier reads; the compiler gives the warning `SemanticDiag::IgnoredSideEffect`.
- If nobody reads the variable any more after the write, that modification does not run — this is still evaluation by dependency.

The everyday advice is simple: write computation as `let`s and pure functions; put input and output in sync blocks; keep `var` for the few places that really need in-place updates.

## 7. Closures and function values

[`examples/07_closures.cml`](examples/07_closures.cml):

```cml
module closures

func inc(x: int): int { return x + 1 }
func dbl(x: int): int { return x * 2 }

func apply_twice(f: (x: int) => int, x: int): int {
    return f(f(x))
}

func adder(n: int): (x: int) => int {
    return (x: int): int => x + n
}

func main(): int sync {
    let g = inc                              // a function name is a value
    println(g(1), apply_twice(dbl, 3))

    let add5 = adder(5)                      // a closure that captured n
    println(add5(10))

    let ops = [inc, dbl]                     // functions in an array
    println(ops->map<(f: (x: int) => int): int => f(10)>)

    var total = 0                            // captured by reference
    [1, 2, 3]->foreach<sync (x: int) => sync { total = total + x }>
    println(total)
    return 0
}
```

```text
2 12
15
[11, 20]
6
```

- Functions are values: they can be bound to a name, passed as arguments, returned, and put in an array. A function type is written `(x: int) => int`.
- A lambda captures variables from the enclosing scope, and the capture is by reference. The last lambda writes to the captured `total`, so it has the function modifier `sync` in front of it (declaring that it has side effects), and its body is also a sync block (`=> sync { ... }`).
- Functions with the same name and different parameter types are overloads; a name that stands for a set of overloads can be used as a value only where the expected function type selects one of them (passed to a function whose parameter type is written out); elsewhere wrap it in a lambda. A library operator with one signature (such as `trim`) is a value directly: `xs->map<trim>`.

## 8. Generics

[`examples/08_generics.cml`](examples/08_generics.cml):

```cml
module generics

type Pair<T> = (T, T)

func first::<T>(xs: T[]): T {
    return xs[0]
}

func swap::<T>(p: Pair<T>): Pair<T> {
    return (p.1, p.0)
}

func my_map::<T, U>(xs: T[], f: (x: T) => U): U[] {
    return xs->map<f>
}

func main(): int sync {
    println(first([1, 2, 3]))
    println(first(['a', 'b']))
    println(swap((1, 2)), swap(('x', 'y')))
    println(my_map([1, 2, 3], (x: int): string => string(x) + '!'))
    return 0
}
```

```text
1
a
(2, 1) (y, x)
[1!, 2!, 3!]
```

- There is no separate type parameter list: type parameters are declared after the function's name in `::<…>`, and a function with it is generic, one without is not. The signature (parameters and return type) and the body then write `T` directly. A name declared at the head takes precedence over a type of the same name inside the function.
- `type Pair<T> = (T, T)` is a type declaration with a parameter: `Pair<int>` is `(int, int)`.
- In the graph, a generic function is a **subgraph template**. Each time it is called with a set of concrete types, a subgraph whose types are all concrete is instantiated: the program above has the two subgraphs `first<int>` and `first<string>` (you can see them with `camel docs/tutorial/examples/08_generics.cml std::rgir`). The graph contains only concrete types, and no type information is needed at run time.
- A generic function may also be passed wherever a function is expected (`apply(id, 3)`, `xss->map<reverse>`, `let f: (x: int) => int = id`): the compiler builds the instance once the position fixes its types. Where nothing does (`let g = id`), it is a compile-time error that says to write the type.

A declaration can also say **where it applies**: `when` after the signature is a compile-time condition over the declaration's type parameters. `double_it` below applies to numbers and to nothing else; when no candidate of a call meets its condition, the error at the call site lists the condition each candidate wrote. `is_numeric` is not a name the compiler knows: it is an ordinary function written with `match type` (the standard library's seven type predicates live in `types`), and a condition calls a function the module can see, so the snippet writes it in the same module.

```cml
module applicability

func is_numeric::<T>(): bool {
    return match type T {
        case int | i32 | float | f32 => true
        case _ => false
    }
}

func double_it::<T>(x: T): T when is_numeric::<T>() {
    return x + x
}

func main(): int sync {
    println(double_it(21), double_it(1.5))
    return 0
}
```

## 9. Error handling

Camel has no exceptions and no `try`/`catch`. A function that may fail declares its return type as **`T!`**: its value is either a `T` or an error. [`examples/09_errors.cml`](examples/09_errors.cml):

```cml
module errors

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
    println(describe(safe_div(8, 2) ?-> inc))
    println(describe(safe_div(8, 0) ?-> inc))
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(safe_div(1, 0))
    return 0
}
```

```text
3
-1
ok 5
DivByZero: the divisor is zero
12 0
error DivByZero: the divisor is zero
```

- **`throw e` returns an error value**; it is the same kind of construct as `return`. It does not unwind the call stack; what the caller gets is simply a value of type `int!`.
- An `int!` cannot be used as an `int`; it must be unwrapped first:
  - `a !! b`: takes `b` on an error, otherwise the value inside;
  - `x ?-> f`: on an error passes the error on unchanged, otherwise calls `f`; these can be chained;
  - `match` with `case error e` (`e.kind`, `e.message`, `e.location`) and `case v`.
- In the graph, all three forms are a branch whose condition is a one-bit tag on the value; it is the same kind of structure as `if`.

**Errors and faults are two different things.** Division by zero, an index out of range and recursion that is too deep are faults, not error values: the program terminates with a source location, and `!!` cannot catch them. [`examples/09_fault.cml`](examples/09_fault.cml) reads `xs[5]`:

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> docs/tutorial/examples/09_fault.cml:8:15
   |
 8 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```

## 10. Nullable values

A value of type **`T?`** is either `null` or a `T`. [`examples/10_nullable.cml`](examples/10_nullable.cml):

```cml
module nullable

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

```text
1
-1
at 2 not found
null
```

- `a ?? b`: takes `b` when `a` is null. `match` uses `case null` and `case v`.
- An ordinary `int` is put into an `int?` wherever one is expected: the two arms of `find` are `null` and `i`, so the branch is an `int?`; an argument (`show(5)`), a declaration with a type (`let maybe: int? = 5`) and `return` work the same way. Not the other way round: to use an `int?` as an `int`, open it with `??` or `match`.

## 11. Maps and sets

`Map<K, V>` and `Set<T>` are built-in types. There are no dedicated literals: write an array, then convert it with `as`. [`examples/11_collections.cml`](examples/11_collections.cml):

```cml
module collections

func main(): int sync {
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

```text
{alice: 28, bob: 31}
28 0
{alice: 28, carol: 45} 2
{alice: 28, bob: 31}
[alice, carol] [28, 45]
{1, 2, 3} true false
{1, 2, 3, 9} {1}
```

- Every operation returns a new value; the original map or set is unchanged.
- `m[k]` is a lookup and its result is a `V?`, so it is followed by `?? default`.
- Iteration: first take out an array with `keys(m)` or `items(m)`, then use `map`, `reduce` or `foreach`.

## 12. Modules and the standard library

[`examples/shapes.cml`](examples/shapes.cml) is a library module; it marks the names visible from outside with `export`:

```cml
module shapes

import { sqrt } from math

export type Vec = { x: float, y: float }

export func hypot(v: Vec): float {
    return sqrt(v.x * v.x + v.y * v.y)
}

export func area(w: float, h: float): float {
    return w * h
}
```

[`examples/12_modules.cml`](examples/12_modules.cml) uses it and the standard module `math`:

```cml
module modules_demo

import { sqrt } from math
import { hypot, Vec } from shapes
import shapes

func main(): int sync {
    println(sqrt(2.0))
    let v: Vec = { x: 3.0, y: 4.0 }
    println(hypot(v))
    println(shapes::area(2.0, 5.0))
    return 0
}
```

```text
1.4142135623730951
5.0
10.0
```

- `import { a, b } from m` brings in the listed names; `import m` brings in only the namespace, and names are accessed as `m::name`.
- `.cml` files in the same directory are found by file name.

### The standard library

The modules that come with Camel are `str` (strings), `array` (arrays), `math`, `statistics`, `random`, `time`, `fs`, `json`, `process`, `env`, `re`, `hash`, `para`, `this`, the tensor family `tensor`, `nn`, `autodiff`, `onnx`, and `python`, which embeds Python ([language reference 5.2.1](../language/en/05_modules.md#521-modules-provided-with-camel)). The [standard library reference](../stdlib-reference.md) lists the functions of the first fourteen modules one by one: signature, whether it is pure, behaviour on failure, examples. [`examples/12_stdlib.cml`](examples/12_stdlib.cml):

```cml
module stdlib_demo

import { split, trim, to_upper, replace, find } from str
import { sort_by, take, sum, reverse, unique } from array
import { abs, hypot, bit_and, pi } from math
import { mean } from statistics

func main(): int sync {
    // Strings: the value goes in on the left, what configures the call goes in angle brackets.
    let words = '  pear, fig ,apple, fig '->trim->split<','>->map<(w: string): string => trim(w)>
    println(words)
    println(words->unique->sort_by<(w: string): int => len(w)>->take<2>)
    println('hello'->to_upper, 'a-b-c'->replace<'-', '+'>, len('héllo'))

    // A result that may not exist is a T?.
    let at_l = 'hello'->find<'l'> ?? -1
    let at_z = 'hello'->find<'z'> ?? -1
    println(at_l, at_z)

    // Arrays and numbers.
    let xs = [3, -1, 4, -1, 5]
    println(sum(xs), reverse(xs), xs->map<(x: int): int => abs(x)>)
    println(mean([1.0, 2.0, 4.0]), hypot(3.0, 4.0), bit_and(6, 3))
    println('{:.4f}'->format<pi>)
    return 0
}
```

```text
[pear, fig, apple, fig]
[fig, pear]
HELLO a+b+c 5
2 -1
10 [5, -1, 4, -1, 3] [3, 1, 4, 1, 5]
2.3333333333333335 5.0 2
3.1416
```

- **The calling convention**: the value a function works on is its ordinary parameter, and the values that configure the call are `with` parameters. So every library function can be attached to a pipe: `s->trim->split<','>`, `xs->unique->sort_by<key>->take<2>`.
- A string is always valid UTF-8 and is measured in code points: `len('héllo')` is 5, and `s[i]` takes out the `i`-th character. Binary data is `Bytes`.
- A function that may have no result returns `T?` (such as `find` when nothing is found), taken out with `??`; out-of-range arguments and arguments outside the domain are faults.
- A detail of writing: angle brackets touch the name before them (`find<x>`), and the comparison `<` has a space before it (`a < b`). The two are never confused, and one argument list may hold several calls with angle brackets: `f<a>(x), g<b>(y)`.
- The `array` module consists of generic functions written in Camel, which are visible subgraphs on the graph; the functions of `str` and `math` are C++ operators, each a single node on the graph.

To find out which operators a program can use, and whether each is declared pure, ask the compiler:

```bash
camel inspect --ops docs/tutorial/examples/12_stdlib.cml
```

Each overload prints one line, for example:

```text
OPERATOR {"module":"str","name":"split","uri":"str:split","with":1,"norm":1,"purity":"pure","signature":"<string> (string) => string[]"}
```

Pure operators can be evaluated ahead of time, merged and removed by optimisation passes, and can be executed concurrently by parallel scheduling; operators with side effects execute only in a sync block, or when their result is used.

## 13. Looking at the graph

Up to here we have kept saying "a function is a subgraph, a call is a node". Now let us see it with our own eyes. [`examples/13_graph.cml`](examples/13_graph.cml):

```cml
module graph_demo

func add(a: int, b: int): int {
    return a + b
}

func main(): int sync {
    let x = add(1, 2)
    println(x)
    return 0
}
```

Write the pass `std::rgir` after the file name, and the program does not run; instead the graph is printed as Graphviz DOT text:

```bash
camel docs/tutorial/examples/13_graph.cml std::rgir
```

Here is the output (for readability, the source-location attributes `origin`, `span` and `srcfile` on each node have been removed):

```text
digraph GraphIR {
    subgraph cluster_F01 {
        label="main";
        subgraph cluster_F02 {
            label="add";
            F02 [label="ARGS", type="ARGS"];
            O02 [label="EXIT", type="EXIT"];
            N00 [label="a", type="PORT"];
            N01 [label="b", type="PORT"];
            N02 [label=":op/add_l", type="OPER"];
            F02 -> N00 [label="0", type="NORM_PORT"];
            F02 -> N01 [label="1", type="NORM_PORT"];
            N02 -> O02 [label="0", type="EXIT"];
            N00 -> N02 [label="0|0", type="NORM"];
            N01 -> N02 [label="0|1", type="NORM"];
        }
        O01 [label="EXIT", type="EXIT"];
        N03 [label="1", type="DATA"];
        N04 [label="2", type="DATA"];
        N05 [label="add", type="FUNC"];
        N06 [label=":io/println", type="OPER"];
        N07 [label="0", type="DATA"];
        N08 [label="GATE", type="GATE"];
        N08 -> O01 [label="0", type="EXIT"];
        N03 -> N05 [label="0|0", type="NORM"];
        N04 -> N05 [label="0|1", type="NORM"];
        N05 -> N06 [label="0|0", type="NORM"];
        N07 -> N08 [label="0|0", type="NORM"];
        N06 -> N08 [label="0|0", type="CTRL"];
    }
    O00 [label="EXIT", type="EXIT"];
    N09 [label="Function(graph=main, tupleSlots=0)", type="DATA"];
    N0A [label="CALL", type="CALL"];
    N0A -> O00 [label="0", type="EXIT"];
    N09 -> N0A [label="0|0", type="WITH"];
}
```

Read it side by side with the source:

- **`add` is a subgraph** (`cluster_F02`): the two `PORT` nodes are the parameters `a` and `b`, the one `OPER` node `:op/add_l` is 64-bit integer addition, and its result is connected to `EXIT`.
- **`main` is also a subgraph** (`cluster_F01`). The two `DATA` nodes are the constants `1` and `2`; **the `FUNC` node `add` is that call**, and two `NORM` (data) edges carry the arguments into it; its result is in turn carried by a data edge to `:io/println`.
- **A sync block is control edges.** Nobody uses the result of `println`, and `return 0` does not depend on it. What makes it run all the same is that `CTRL` edge: `N06 -> N08`, from `println` to `GATE`, and `GATE` lets the return value `0` through to `EXIT`. Remove the `sync` from `main` and look again: this edge is gone, and so is the reachability of the `println` node — this is what the rules of section 6 look like in the graph.

If you have Graphviz installed, `camel ... std::rgir | dot -Tsvg -o graph.svg` gives you a picture; `std::gir` outputs a styled version.

## 14. Changing the passes

`std::rgir` is a **pass**. Camel models compilation, optimisation, execution and export all as transformations of the graph, and what you write after the file name is the passes to run, in order. The same source, with a different set of passes, does a different thing. Using [`examples/14_passes.cml`](examples/14_passes.cml):

```cml
module passes_demo

func square(x: int): int {
    return x * x
}

func sum_squares(n: int, acc: int): int {
    return if n == 0 then acc else sum_squares(n - 1, acc + square(n))
}

func main(): int sync {
    println(sum_squares(1000, 0))
    return 0
}
```

```bash
camel docs/tutorial/examples/14_passes.cml                      # default: the node VM, std::nvm
camel docs/tutorial/examples/14_passes.cml std::fvm             # bytecode VM
camel docs/tutorial/examples/14_passes.cml std::jit             # JIT compilation
camel docs/tutorial/examples/14_passes.cml std::prl             # parallel scheduling
camel docs/tutorial/examples/14_passes.cml std::opt std::fvm    # optimise first, then run
```

Every one of them prints:

```text
333833500
```

**Seeing what each pass did to the graph.** `std::stats` prints statistics about the graph and passes the graph on unchanged to the next pass:

```bash
camel docs/tutorial/examples/14_passes.cml std::stats std::opt std::stats std::fvm
```

```text
GRAPH_STATS {"graphs":6,"nodes":26,"edges":28,"opers":5,"direct_calls":5,"indirect_calls":1,"closures":0,"branches":1}
GRAPH_STATS {"graphs":7,"nodes":70,"edges":85,"opers":18,"direct_calls":4,"indirect_calls":0,"closures":0,"branches":3}
333833500
```

The two lines of numbers differ: `std::opt` changed the graph (there are fewer direct calls and more operator nodes — functions were inlined at their call sites), while the result of the program is unchanged. To save the whole graph pass by pass, use `--dump-after-each-pass`:

```bash
camel docs/tutorial/examples/14_passes.cml --dump-after-each-pass snap std::opt::inline std::opt::fold std::fvm
```

The directory `snap` then contains `00_input.rgir.dot`, `01_std.opt.inline.rgir.dot`, `02_std.opt.fold.rgir.dot` and the corresponding `.stats.json` files: the input graph, and the graph after each pass.

A few rules:

- Passes run in the order they are written. The passes that execute the graph (`std::nvm`, `std::fvm`, `std::jit`, `std::prl`) and the passes that transpile it (`std::rgir`, `onnx::export`) consume the graph, and passes written after them do not run.
- If you write no pass at all, or none of the passes you write consumes the graph, the default `std::nvm` runs at the end.
- The language does not restrict the order of passes, and the order can affect the result: passes change the graph and can have side effects. The optimisation passes of the standard library aim not to change the result of the program; this is ensured by tests, and is not a guarantee at the level of the language.

## 15. Tensors and exporting a model

Tensors come from the `tensor` module. [`examples/15_tensor.cml`](examples/15_tensor.cml):

```cml
module tensor_demo

import { Tensor, ones, zeros, shape, sum, transpose } from tensor

func main(): int sync {
    let a = ones([2, 3])
    let b = ones([3, 1])
    let c = a @ b                    // matrix product: 2x3 @ 3x1
    println(shape(a), shape(c))
    println(c)
    println(sum(a * 2.0 + 1.0))
    println(shape(transpose(a)))
    return 0
}
```

```text
[2, 3] [2, 1]
tensor([[      3.],
        [      3.]], dtype=float32)
18.0
[3, 2]
```

**A minimal model.** In its true form, a Camel program expresses only computation: `main` is the model, and its parameters are the inputs of the model. [`examples/15_model.cml`](examples/15_model.cml):

```cml
module model

import { Tensor, ones, relu, sum } from tensor

func main(x: Tensor<f32, [2, 3]>): Tensor {
    let w = ones([3, 1])
    return relu(x @ w)
}
```

`main` prints nothing and has no `sync`: it is pure. The type of the parameter `x` states the element type and the shape.

**Running it**: the values of the parameters are given on the command line, and a tensor comes from a `.npy` file ([`examples/x.npy`](examples/x.npy) is `[[1, -2, 3], [-4, -5, 6]]`); the value that `main` returns is printed to standard output:

```bash
camel docs/tutorial/examples/15_model.cml --arg x=docs/tutorial/examples/x.npy
```

```text
tensor([[      2.],
        [      0.]], dtype=float32)
```

**Exporting it**: the same source, with a different set of passes. Nothing in the program was written for the export; not even `import onnx` is needed:

```bash
camel docs/tutorial/examples/15_model.cml std::macro std::opt 'onnx::export out="model.onnx"'
```

```text
onnx::export: wrote model.onnx
```

The resulting `model.onnx` has one input `x` of shape `[2, 3]` and one output `output`; feeding the same `x.npy` to it with ONNX Runtime gives the same `[[2.], [0.]]`.

The export pass translates whatever it can translate, and where it cannot (the program has effects such as printing or reading files) it reports an error and points to the location. A program that needs to time things and print as well as export should instead call `onnx.export_model` inside the program to export one of its own functions ([cli.md](../cli.md), "ONNX export as a pass (`onnx::export`)").

## 16. Taking gradients

Automatic differentiation is a library built on top of the pass mechanism: `grad(f)` rewrites the graph of `f` into the graph of its derivative. The first `with` parameter of `f` is the weight it differentiates; ordinary parameters are data. [`examples/16_grad.cml`](examples/16_grad.cml):

```cml
module grad_demo

import { grad, with_grad } from autodiff

with <x: float>
func f(y: float): float {
    return x * x * y + y
}

func main(): int sync {
    println(f<3.0>(2.0))
    println(grad(f)<3.0>(2.0))                 // df/dx = 2xy = 12
    println(with_grad(f)<3.0>(2.0))            // (value, gradient) = (20, 12)
    return 0
}
```

```bash
camel docs/tutorial/examples/16_grad.cml std::macro
```

```text
20.0
12.0
(20.0, 12.0)
```

- `grad(f)` returns a function that differentiates the first `with` parameter of `f`; the remaining `with` parameters are constants and ordinary parameters are data.
- To differentiate data (an adversarial example, say), make it a `with` parameter: `grad(<x: Tensor> (m: Model): float => loss(m, x))<x>`.
- `with_grad(f)` returns the value together with the gradient. In a loop, bind the transform once and call it with the weights: `let loss_with_grad = with_grad(loss)`, then `let (l, g) = loss_with_grad<model>(x, y)` inside the loop.
- `grad` is a macro operator. The `std::macro` in the pipeline expands it into the graph of the derivative before anything runs, and the optimisation passes after it can go on to optimise that graph. This is a precondition of this library, not a constraint of the language.
- The function being differentiated is written as pure data flow with `let`; when a gradient would have to pass through reads and writes of a `var`, it is rejected outright. When the first `with` parameter is a `var`, has no tangent, or the function has no `with` parameter, compilation fails with `SemanticDiag::NotDifferentiable` naming the reason.
- One training step is "gradient + update": the weights of the model are written as `with` parameters, `grad(loss)<model>(x, y)` differentiates with respect to the weights, and `sgd` from the `nn` module gives the updated weights. For a complete example see [`examples/ml/train_linear.cml`](../../examples/ml/train_linear.cml).

## Where to go next

- [Examples](../../examples/README.md): short programs organised by task — JSON, files, word-frequency counting, sorting, command-line tools, parallelism, simulation, training.
- [Language reference](../language/en/README.md): the precise rules for every construct.
- [Standard library reference](../stdlib-reference.md): every function of every module.
- [Command line](../cli.md): all options, passes, diagnostic formats, resource limits; `camel --explain <diagnostic name>` explains one diagnostic.
- [Design principles](../design-principles.en.md): why the language is the way it is.
