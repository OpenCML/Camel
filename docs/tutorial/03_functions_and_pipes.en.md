# Chapter 3 · Functions and pipes

[中文](03_functions_and_pipes.cn.md) | English · [Back to the contents](tutorial.en.md)

Chapter 1 already showed you that a function is a subgraph and a call is a node on the graph. This chapter writes
functions out in full — name, parameters, return type, recursion, a function with no name (a lambda) — and the `with`
parameters that configure one. The sentence to take away: **a chain like `a->f->g` is a data flow, and on the graph it
is one path.**

## A name instead of a repetition

[`examples/03_functions.cml`](examples/03_functions.cml):

```cml
// Functions, recursion, pipelines and `with` parameters.
// Run: camel docs/tutorial/examples/03_functions.cml

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

```bash
camel docs/tutorial/examples/03_functions.cml
```

```text
49
6765
5000050000
15
50
```

- **The return type comes after the parameter list**: `func square(x: int): int`. It is part of the function's value
  type: `square` is a value of type `(x: int) => int`.
- **A function body's value is the value of its last expression**, so `return x * x` and a last line of `x * x` mean
  the same thing. Chapter 4 shows whose day that rule saves when the `return` is left out.
- **A repetition has three writings**: recursion (`fib`), the `loop` expression (chapter 5) and the higher-order
  operators (chapter 5). A call in tail position takes no stack frame, so `sum_to(100000, 0)` recurses a hundred
  thousand deep without trouble; a call that is not in tail position (`fib`) is limited to 1000 open calls by
  default, and `--max-recursion-depth` moves that.

## A pipe writes the data as one chain

`5->square->scale<2>` is the same call as `scale<2>(square(5))` and gives `50`. The value on the left becomes the
argument of the function on the right, so a run of `->` reads "work this out, then hand it over". It is not sugar:
**a path on the graph is exactly "who wants whose result"**, and the chain puts that sentence in the source.

<!-- camel-snippet: fragment: one chained call; `xs` comes from the context, it is not a program -->

```cml
let ys = xs->map<(x: int): int => x * x>->filter<(x: int): bool => x % 2 == 0>
```

## A lambda: a function with no name

A function is a value, so it need not have a name. `(parameters): return type => expression` is a lambda:

[`examples/03_lambdas.cml`](examples/03_lambdas.cml):

```cml
// A function with no name is written `(x: int): int => ...`; it can also be kept in a name.
// Run: camel docs/tutorial/examples/03_lambdas.cml

func main(): int sync {
    let twice = (x: int): int => x * 2
    println(twice(21))

    println([1, 2, 3]->map<(x: int): int => x + 10>)
    println([1, 2, 3, 4]->filter<(x: int): bool => x % 2 == 0>)
    return 0
}
```

```bash
camel docs/tutorial/examples/03_lambdas.cml
```

```text
42
[11, 12, 13]
[2, 4]
```

The name `twice` holds a function; `map`, `filter` and `reduce` want exactly such a function. Chapter 5 goes through
them, and chapter 11 covers what a lambda can capture and when the captured value changes.

## `with` parameters: how a function is configured

A parameter declared in `with <factor: int>` is written in **angle brackets** and given at the call as
`scale<3>(5)`. A parameter in the parentheses is data the function works on; a `with` parameter is a property the
function is configured with — the configuration other languages put on an object's members is a `with` parameter in
Camel. It is not part of the data flow, so it does not appear in a pipe. Chapter 11 goes further.

## On the graph

- A call is a **`CALL` node** on the graph; the function called is its own subgraph, and its parameters are `PORT`
  nodes at the subgraph's entry (the `N00 [label="x", type="PORT"]` of chapter 1's figure).
- `5->square->scale<2>` is, on the graph, `5` → the call of `square` → the call of `scale`: two data edges joined
  into one path.
- Recursion has no node of its own: the call site of `fib` sits inside `fib`'s own subgraph.
- A `with` parameter is part of the function value rather than a data node; the graph cannot show it because the
  program binds it.

## Exercise

Write `max_of(a: int, b: int): int` returning the larger of the two (as an `if` expression). Print `max_of(3, 7)` from
`main`, then use `reduce` with it to find the largest of `[3, 9, 4]`.

The answer is [`examples/03_exercise.cml`](examples/03_exercise.cml):

```cml
// The answer to the exercise of chapter 3: a function of two parameters, used from a pipe.
// Run: camel docs/tutorial/examples/03_exercise.cml

func max_of(a: int, b: int): int {
    return if a > b then a else b
}

func main(): int sync {
    let xs = [3, 9, 4]
    println(max_of(3, 7))
    println(xs->reduce<(acc: int, x: int): int => max_of(acc, x), xs[0]>)
    return 0
}
```

```bash
camel docs/tutorial/examples/03_exercise.cml
```

```text
7
9
```

## References

- [Language reference 6.4 Calls](../language/en/06_expressions.md#64-calls) and
  [6.5 Pipes](../language/en/06_expressions.md#65-pipes)
- [Language reference 6.9 Lambdas and function types](../language/en/06_expressions.md#69-lambdas-and-function-types)
- [Language reference 7.4 Functions](../language/en/07_statements.md#74-functions) and
  [7.4.2 `with` parameters](../language/en/07_statements.md#742-with-parameters)
- [Language reference 4.6 Faults, exit and resource limits](../language/en/04_execution_model.md#46-faults-exit-and-resource-limits)
  (the recursion limit)