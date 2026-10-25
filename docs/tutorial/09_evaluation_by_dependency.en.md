# Chapter 9 · Evaluation by dependency

English | [中文](09_evaluation_by_dependency.cn.md) · [Back to the contents](tutorial.en.md)

Camel's execution model is one sentence long: **an expression is evaluated because someone needs its result**.
This chapter shows what that sentence looks like on the graph, and how it differs from "one thing after another".

## Who needs whose result

[`examples/09_dependency.cml`](examples/09_dependency.cml):

```cml
// Who needs whose result decides what runs, and what can run at the same time.
// Run: camel docs/tutorial/examples/09_dependency.cml
//      camel docs/tutorial/examples/09_dependency.cml std::gir
module dependency

func price(n: int): int {
    return n * 3
}

func tax(n: int): int {
    return n / 10
}

// Neither call needs the other's result, and the addition needs both.
func total(n: int): int {
    return price(n) + tax(n)
}

func spare(n: int): int {
    return n + 1
}

// The result of `spare` is used by nothing, so the call is not part of what this function
// computes.
func total_with_a_spare(n: int): int {
    spare(n)
    return total(n)
}

func main(): int sync {
    println(total(10), total_with_a_spare(10))
    return 0
}
```

```bash
camel docs/tutorial/examples/09_dependency.cml
```

```text
31 31
```

## Dependencies on the graph

Now print the graph. Below is the `total` part of what
`camel docs/tutorial/examples/09_dependency.cml std::gir` prints (an excerpt):

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::gir's DOT, not the program's stdout -->

```cml
// Who needs whose result decides what runs, and what can run at the same time.
// Run: camel docs/tutorial/examples/09_dependency.cml
//      camel docs/tutorial/examples/09_dependency.cml std::gir
module dependency

func price(n: int): int {
    return n * 3
}

func tax(n: int): int {
    return n / 10
}

// Neither call needs the other's result, and the addition needs both.
func total(n: int): int {
    return price(n) + tax(n)
}

func spare(n: int): int {
    return n + 1
}

// The result of `spare` is used by nothing, so the call is not part of what this function
// computes.
func total_with_a_spare(n: int): int {
    spare(n)
    return total(n)
}

func main(): int sync {
    println(total(10), total_with_a_spare(10))
    return 0
}
```

```bash
camel docs/tutorial/examples/09_dependency.cml std::gir
```

```text
        subgraph cluster_F02 {
            label="total";
            subgraph cluster_F03 {
                label="price";
                F03 [label="ARGS", type="ARGS"];
                O03 [label="EXIT", type="EXIT"];
                N00 [label="n", type="PORT", param_name="n", param_kind="normal", param_index="0", value_type="int"];
                N01 [label="3", type="DATA", origin=227, span="6:15-6:16", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
                N02 [label=":op/mul_l", type="OPER", origin=228, span="6:13-6:14", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
                F03 -> N00 [label="0", type="NORM_PORT"];
                N02 -> O03 [label="0", type="EXIT"];
                N00 -> N02 [label="0|0", type="NORM"];
                N01 -> N02 [label="0|1", type="NORM"];
            }
            subgraph cluster_F04 {
                label="tax";
                F04 [label="ARGS", type="ARGS"];
                O04 [label="EXIT", type="EXIT"];
                N03 [label="n", type="PORT", param_name="n", param_kind="normal", param_index="0", value_type="int"];
                N04 [label="10", type="DATA", origin=231, span="10:15-10:17", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
                N05 [label=":op/div_l", type="OPER", origin=232, span="10:13-10:14", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
                F04 -> N03 [label="0", type="NORM_PORT"];
                N05 -> O04 [label="0", type="EXIT"];
                N03 -> N05 [label="0|0", type="NORM"];
                N04 -> N05 [label="0|1", type="NORM"];
            }
            F02 [label="ARGS", type="ARGS"];
            O02 [label="EXIT", type="EXIT"];
            N06 [label="n", type="PORT", param_name="n", param_kind="normal", param_index="0", value_type="int"];
            N07 [label="price", type="FUNC", origin=236, span="15:11-15:19", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
            N08 [label="tax", type="FUNC", origin=238, span="15:22-15:28", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
            N09 [label=":op/add_l", type="OPER", origin=239, span="15:20-15:21", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
            F02 -> N06 [label="0", type="NORM_PORT"];
            N09 -> O02 [label="0", type="EXIT"];
            N06 -> N07 [label="0|0", type="NORM"];
            N06 -> N08 [label="1|0", type="NORM"];
            N07 -> N09 [label="0|0", type="NORM"];
            N08 -> N09 [label="0|1", type="NORM"];
        }
```

- `N07` and `N08` are the two calls, and the edges `N07 -> N09` and `N08 -> N09` say "the addition wants both
  results".
- **There is no edge between `N07` and `N08`.** Neither waits for the other, so they can be computed at the same
  time and in either order — the language requires the result to be right, not that they happen in the order they
  were written. That is "evaluation by dependency".
- `n` (`N06`) feeds both calls because both calls need it, not because one comes first.

The other half is in the same file. The graph of `total_with_a_spare` has exactly one call node:

```text
            N0E [label="total", type="FUNC", origin=248, span="26:11-26:19", srcfile="docs/tutorial/examples/09_dependency.cml", value_type="int"];
```

The line `spare(n)` left no node at all: **nothing needs its result, so it is not one of the things to
compute**.

## Sync blocks: sync edges between effects

`main` is a sync block, and its statements do have an edge between them:

```text
        N10 -> N12 [label="0|0", type="CTRL"];
        N13 -> N15 [label="0|0", type="CTRL"];
```

**A sync block is a sync edge between effects on the graph**: `N10` (`total(10)`) to `N12`
(`total_with_a_spare(10)`), and `N13` (`println`) to `N15` (the `GATE`). Writing `sync` says "the order here
matters": `println` is an effect and it has to happen before the `return`. Take `sync` away and those `CTRL`
edges are gone, while `println(total(10), ...)` still computes — which of the two is computed first is decided by
dependency, not by the order they are written in (chapter 10).

## What it looks like without evaluation by dependency

`var` and assignment are where "order matters" shows up most often: write `x = 1` and then `x = 2`, and the graph
holds two writes to the same place, ordered by their sync relation
([language reference 4.4](../language/en/04_execution_model.md#44-var-and-the-order-of-writes)). Camel does not
forbid order; it merely does not pretend that order is the default: **order is stated where order is meant**.

## What this is worth

A slow chain — here, counting the Collatz steps of every start value from 1 to 16000 — can be moved to another
back end whenever each element is computed on its own, without changing a character of the program.
[`examples/parallel/parallel_map.cml`](../../examples/parallel/parallel_map.cml):

```cml
// The same `map` runs sequentially or in parallel depending on the passes chosen on the
// command line; the program does not change and the results keep the order of the elements.
// Run: camel examples/parallel/parallel_map.cml
//      camel examples/parallel/parallel_map.cml std::prl
module parallel_map

// Number of steps the Collatz sequence takes from `n` down to 1.
func collatz_steps(n: int, steps: int): int {
    return if n == 1 then steps
        else if n % 2 == 0 then collatz_steps(n / 2, steps + 1)
        else collatz_steps(3 * n + 1, steps + 1)
}

// The start value in [lo, hi) with the longest sequence, as (start, steps).
func longest_in(lo: int, hi: int): (int, int) {
    return lo..hi->reduce<
        (best: (int, int), n: int): (int, int) => longer(best, (n, collatz_steps(n, 0))),
        (lo, 0)
    >
}

func longer(a: (int, int), b: (int, int)): (int, int) {
    return if b.1 > a.1 then b else a
}

func main(): int sync {
    // Eight independent blocks of 2000 start values; each block is one element of the map.
    let block = 2000
    let results = 0..8->map<(i: int): (int, int) => longest_in(i * block + 1, (i + 1) * block + 1)>

    println(results)
    let best = results->reduce<longer, (1, 0)>
    println(format<8 * block, best.0, best.1>('longest up to {}: start {} takes {} steps'))
    return 0
}
```

```bash
camel examples/parallel/parallel_map.cml
```

```text
[(1161, 181), (3711, 237), (5567, 235), (6171, 261), (9257, 259), (10971, 267), (13255, 275), (14695, 270)]
longest up to 16000: start 13255 takes 275 steps
```

Change the last word of the command to `std::prl` (the parallel scheduler) and the output is the same, word for
word: the dependencies on the graph did not change, only who computes first did.

## On the graph

- **The dependency edges are everything**: a data edge means "this side wants that side's result". Where there is
  no edge, there is no order.
- **A `CTRL` edge is what a sync block leaves behind**: a sync block is a sync edge between effects; it appears
  between effects only, and never ties pure computation together.
- A result nobody needs has no node; a function is a subgraph, and a call is a node on the graph.

## Exercise

Make the two calls in `total(n)` depend on each other — for instance `price(tax(n))` — and print the graph again.
There is now an edge between `price` and `tax`, so they can no longer be computed at the same time; dependency
decides which of the two comes first. Look at the output again: the value is still 30, and the graph has changed.

Answer [`examples/09_exercise.cml`](examples/09_exercise.cml):

```cml
// The answer to the exercise of chapter 9: one call now needs the other's result, so the graph
// has an edge between them and they no longer stand side by side.
// Run: camel docs/tutorial/examples/09_exercise.cml
module exercise

func price(n: int): int {
    return n * 3
}

func tax(n: int): int {
    return n / 10
}

func total(n: int): int {
    return price(tax(n))
}

func main(): int sync {
    println(total(100))
    return 0
}
```

```bash
camel docs/tutorial/examples/09_exercise.cml
```

```text
30
```

## Reference

- [Language reference 4.2, Evaluation by dependency](../language/en/04_execution_model.md#42-evaluation-by-dependency)
- [Language reference 4.3, `sync`](../language/en/04_execution_model.md#43-sync)
- [Language reference 4.4, `var` and the order of writes](../language/en/04_execution_model.md#44-var-and-the-order-of-writes)