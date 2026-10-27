# Chapter 5 · Arrays and loops

English | [中文](05_arrays_and_loops.cn.md) · [Back to the contents](tutorial.en.md)

When the same thing has to be done many times, Camel has two ways to write it: **say how each round is
computed** (`loop`), or **say only what is to be done to each element** (`map`, `filter`, `reduce`, `foreach`).
This chapter is about choosing between them, and about the fact that the two are different things on the graph.

## Arrays and ranges

An array is `T[]`; `a..b` is the range from `a` to `b - 1`, which is shorter to write than an array literal:

```cml
let xs = 1..11        // [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
```

An array is a **value**, not a container someone else can change: `concat(xs, [11])` and `xs[0:3]` both
return a new array and leave `xs` alone (chapter 2). An out-of-range index is a fault, and the program stops with
a location (chapter 7).

## Higher-order operators

[`examples/05_arrays.cml`](examples/05_arrays.cml):

```cml
// Arrays and the higher-order operators map, filter, reduce and foreach.
// Run: camel docs/tutorial/examples/05_arrays.cml

func main(): int sync {
    let xs = 1..11    // [1, ..., 10]
    let squares = xs->map<(x) => x * x>
    let evens = squares->filter<(x) => x % 2 == 0>
    let total = evens->reduce<(acc: int, x: int): int => acc + x, 0>

    println(xs)
    println(squares)
    println(evens)
    println(total)

    // Arrays are values: slice and concat return new arrays.
    let ys = concat(xs[0:3], [99])
    println(ys, len(ys), ys[0])

    // foreach runs its function for each element, in order.
    ['a', 'b', 'c']->foreach<(s) => sync { println('item', s) }>
    return 0
}
```

```bash
camel docs/tutorial/examples/05_arrays.cml
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

- `->` is the pipe: `xs->map<f>` is `map(xs, f)`, and a chain of them reads better (chapter 3).
- The function handed to `map` or `filter` is a lambda: `<(x: int): int => x * x>`, with its type in the angle
  brackets.
- `reduce` needs a starting value, so it is `reduce<f, 0>`.
- The functions handed to these four operators stay pure: they should not carry state (there is a counter-example
  below).

## `loop`

Some repetitions carry state, and each round needs the previous round's result. That is what `loop` is for: the
names on the head are the state, the value of the expression after `do` is the **next state**, and the value of
the loop is the last state. [`examples/05_loops.cml`](examples/05_loops.cml):

```cml
// `loop` carries its state from one round to the next; the body's value is the next state.
// Run: camel docs/tutorial/examples/05_loops.cml

func main(): int sync {
    // One round per element of the sequence.
    let total = loop acc = 0 for x in 1..6 do acc + x * x
    println(total)

    // The condition decides when to stop; there is no `break` and no `continue`.
    let halved = loop n = 100 while n > 1 do n / 2
    println(halved)

    // Two names on the loop head: the body's value is the next pair.
    let (i, acc) = loop i = 1, acc = 0 while i <= 4 do (i + 1, acc + i)
    println(i, acc)

    // Both forms: the condition is checked before each round.
    let first_big = loop hit = 0 for x in [2, 7, 9] while hit == 0 do if x > 5 then x else hit
    println(first_big)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_loops.cml
```

```text
55
1
5 10
7
```

- `loop name = init (, name = init)* (for name in sequence)? (while condition)? do expression`: either head may
  be written, and at least one must be.
- **There is no `break` and no `continue`**: to stop early, write the `while` condition; to skip the rest of a
  round, write an `if`.
- With one name, what follows `do` is the next state; with several, it is a tuple of the next states (the third
  example above).
- `for x in xs` over an empty array runs no round at all, and the value of the loop is the initial state.

## Which one to use

- If the work has to happen **in order**, or a round depends on the previous one: `loop` (or `reduce`,
  `foreach`).
- If each element is computed on its own, with no relation between elements: `map`, `filter`. They are shorter,
  and they **can run in parallel** — precisely because the elements have no order between them.

The order is fixed by the language, not by the implementation. In
[`examples/05_map_order.cml`](examples/05_map_order.cml) the same random number generator is used in two places:
in the element function of a `map` (wrong) and in the accumulating function of a `reduce` (right).

```cml
// The elements of `map` have no agreed order of execution: do not put stateful calls in them.
// Run: camel docs/tutorial/examples/05_map_order.cml
//      camel docs/tutorial/examples/05_map_order.cml std::prl

import { seed, randint, next_key } from random

func main(): int sync {
    seed(42)
    // Wrong tool: next_key advances a hidden state, so each element depends on which ran first.
    let drawn = 0..8->map<(i) => next_key()->randint<0, 100>>
    println(drawn)

    seed(42)
    // reduce runs in element order on every back end.
    let ordered = 0..8->reduce<(acc: int[], i: int): int[] => concat(acc, [next_key()->randint<0, 100>]), [0][0:0]>
    println(ordered)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_map_order.cml
```

```text
[22, 72, 89, 99, 60, 96, 66, 2]
[22, 72, 89, 99, 60, 96, 66, 2]
```

On the sequential back ends (the default `std::nvm`, and `std::fvm` and `std::jit`) the two lines are the same,
and the same on every run.

<!-- camel-snippet: compile-only: the text block is not printed by this run; the parallel order differs from run to run -->

```cml
// The elements of `map` have no agreed order of execution: do not put stateful calls in them.
// Run: camel docs/tutorial/examples/05_map_order.cml
//      camel docs/tutorial/examples/05_map_order.cml std::prl

import { seed, randint, next_key } from random

func main(): int sync {
    seed(42)
    // Wrong tool: next_key advances a hidden state, so each element depends on which ran first.
    let drawn = 0..8->map<(i) => next_key()->randint<0, 100>>
    println(drawn)

    seed(42)
    // reduce runs in element order on every back end.
    let ordered = 0..8->reduce<(acc: int[], i: int): int[] => concat(acc, [next_key()->randint<0, 100>]), [0][0:0]>
    println(ordered)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_map_order.cml std::prl
```

Change the last word to the parallel scheduler `std::prl` and the order of the first line is no longer promised;
it may differ from run to run. One such run printed `[66, 22, 96, 72, 89, 2, 99, 60]` on the first line, and
still `[22, 72, 89, 99, 60, 96, 66, 2]` on the second. That is the language behaving as specified (design
principles 3.6), not a defect: the elements have no order, which is exactly what lets `map` run in parallel. The
rule is short: **keep the functions you hand to `map` and `filter` pure; anything that has to happen in order
goes in `foreach`, `reduce` or `loop`.**

## Main line: step 5

The small tool of part one is a word counter. Step 5 cuts the text into words and counts them with a `loop`
([`examples/wordcount/step5.cml`](../../examples/wordcount/step5.cml)):

```cml
// Step 5 of the word counter: the words of the text with their counts, carried by a `loop`.
// Run: camel examples/wordcount/step5.cml

import { split, trim, to_lower, replace } from str
import { any, flat_map, sort_by, take } from array

// The words of one line: lower case, punctuation removed, empty pieces dropped.
func words_of(line: string): string[] {
    let plain = line->to_lower->replace<',', ' '>->replace<'.', ' '>
    return plain->split<' '>->filter<(w) => len(w) > 0>
}

// `counts` with one more for `w`, adding `w` when it is new.
func bump(counts: (string, int)[], w: string): (string, int)[] {
    let seen = counts->any<(e: (string, int)): bool => e.0 == w>
    let rows = if seen then counts else concat(counts, [(w, 0)])
    return rows->map<(e: (string, int)): (string, int) => if e.0 == w then (e.0, e.1 + 1) else e>
}

func show(e: (string, int)): string {
    return e.0 + ' ' + string(e.1)
}

func main(): int sync {
    let text = '
        The quick fox sees the lazy dog.
        The dog sees the fox, and the fox runs.
    '

    // Cut the text into lines, and the lines into words.
    let lines = text->split<'\n'>
        ->map<(l) => trim(l)>
        ->filter<(l) => len(l) > 0>
    let words = lines->flat_map<words_of>
    println('lines:', len(lines), 'words:', len(words))

    // A `loop` carries the counts from one round to the next.
    let counts = loop acc = [] as (string, int)[] for w in words do bump(acc, w)
    println('distinct:', len(counts))

    // sort_by is stable: ordering by word first and by count second leaves the words of one count
    // in alphabetical order. The negated count puts the largest first.
    let ranked = counts
        ->sort_by<(e: (string, int)): string => e.0>
        ->sort_by<(e: (string, int)): int => 0 - e.1>
    println('top 5:')
    println(ranked->take<5>->map<show>->join<'\n'>)
    return 0
}
```

```bash
camel examples/wordcount/step5.cml
```

```text
lines: 2 words: 16
distinct: 8
top 5:
the 5
fox 3
dog 2
sees 2
and 1
```

Until step 6 the counts live in an array of `(string, int)` — `Map` is chapter 6's subject. At step 6 this word
list becomes a `Map<string, int>` and the program behaves the same.

## On the graph

- **A `loop` is a node on the graph with a state edge back to itself**: the state flows from one round into the
  next, and each round's value depends on the previous one; only computations without such an edge can run in
  parallel.
- **The elements of a higher-order operator have no order edges between them**: each element of a `map` depends
  only on the array itself, none of them waits for another, and that is what allows them to run in parallel.
- Arrays are values: `concat` and `slice` build new nodes and leave the nodes of the original array alone.

## Exercise

Use a `loop` to add the squares of the even numbers from 1 to 10 (the answer is 220). Then write it the other
way: take the array of even squares with `filter` and `map`, and sum it with `reduce` — what is the difference
between the two writings on the graph?

Answer [`examples/05_exercise.cml`](examples/05_exercise.cml):

```cml
// The answer to the exercise of chapter 5: a `loop` that adds the squares of the even numbers.
// Run: camel docs/tutorial/examples/05_exercise.cml

func main(): int sync {
    let total = loop acc = 0 for x in 1..11 do if x % 2 == 0 then acc + x * x else acc
    println(total)
    return 0
}
```

```bash
camel docs/tutorial/examples/05_exercise.cml
```

```text
220
```

## Reference

- [Language reference 6.8.6, `loop`](../language/en/06_expressions.md#686-loop)
- [Language reference 6.10, Higher-order operators](../language/en/06_expressions.md#610-higher-order-operators)
- [Language reference 4.8, Order in higher-order operators](../language/en/04_execution_model.md#48-order-in-higher-order-operators)
- [Design principles 3.6, Higher-order operators and parallelism](../design-principles.en.md#36-higher-order-operators-and-parallelism)