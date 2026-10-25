# Chapter 1 · Five minutes to see the graph

English | [中文](01_five_minutes.cn.md) · [Back to the contents](tutorial.en.md)

By the end of this chapter you will have run your first Camel program and seen two things other languages do not
show you: **your function is a graph**, and **the compiler rewrites that graph without changing the result**.
The question this book keeps asking starts here: **what is this code on the graph?**

First get `camel`: build it from source as [setup.en.md](../setup.en.md) describes, put `out/latest/bin` on
`PATH`, and check that `camel --version` prints a version. The commands below all run from the repository root.

## The first program

[`examples/01_hello.cml`](examples/01_hello.cml):

```cml
// The smallest program: `main` is the entry, and its body is a sync block.
// Run: camel docs/tutorial/examples/01_hello.cml
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

- One `.cml` file is one module, and `module hello` names it (the name may be left out).
- The program starts at `main`. The `int` it returns is the process exit code, so this one returns `0`.
- The **`sync`** after the return type says the body is a **sync block**: the statements written directly in it
  run in the order they are written. Chapter 10 explains why it is needed; for now remember "a function that
  prints has `sync` in front of its body".
- A newline separates statements; no semicolons are needed.

## Your function is a graph

Put `std::gir` at the end of the command line: the program does not run, and the compiler prints the graph it
built. What you write after the file name is a **pass**, and `std::gir` is the pass that prints the graph.

<!-- camel-snippet: graph-dump: the text block is std::gir's DOT, not the program's stdout -->

```cml
// The smallest program: `main` is the entry, and its body is a sync block.
// Run: camel docs/tutorial/examples/01_hello.cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```bash
camel docs/tutorial/examples/01_hello.cml std::gir
```

```text
digraph GraphIR {
    subgraph cluster_F01 {
        label="main";
        O01 [label="EXIT", type="EXIT"];
        N00 [label="Hello, Camel!", type="DATA", origin=35, span="5:12-5:27", srcfile="docs/tutorial/examples/01_hello.cml", value_type="string"];
        N01 [label=":io/println", type="OPER", origin=36, span="5:4-5:11", srcfile="docs/tutorial/examples/01_hello.cml", value_type="string"];
        N02 [label="0", type="DATA", origin=37, span="6:11-6:12", srcfile="docs/tutorial/examples/01_hello.cml", value_type="int"];
        N03 [label="GATE", type="GATE", value_type="int"];
        N03 -> O01 [label="0", type="EXIT"];
        N00 -> N01 [label="0|0", type="NORM"];
        N02 -> N03 [label="0|0", type="NORM"];
        N01 -> N03 [label="0|0", type="CTRL"];
    }
    O00 [label="EXIT", type="EXIT"];
    N04 [label="Function(graph=main, tupleSlots=0)", type="DATA", origin=33, span="4:0-7:1", srcfile="docs/tutorial/examples/01_hello.cml", value_type="() => int"];
    N05 [label="CALL", type="CALL", value_type="int"];
    N05 -> O00 [label="0", type="EXIT"];
    N04 -> N05 [label="0|0", type="WITH"];
}
```

Reading this against the source:

- `cluster_F01` is the **subgraph of `main`**. There is no "what line 5 does" in a graph, only nodes and edges.
- `N00` is the string constant, `N01` is the `:io/println` operator, and the **data edge** `N00 -> N01` says
  "this string is its argument".
- `N02` is `0` and `N03` is the exit gate `GATE`: `N02 -> N03` says the value of `main` is `0`.
- The edge `N01 -> N03` is of type **`CTRL`**: **a sync block is a sync edge between effects on the graph**.
  `println` is an effect, and this edge puts it before the exit — that is, before `return 0` — which is exactly
  what `sync` does. Take `sync` away and neither the edge nor `println` is there (chapters 9 and 10).
- The last three lines are the entry itself: `Function(graph=main)` is the subgraph `main`, `CALL` is the entry
  calling it, and the `WITH` edge joins the two.

## The compiler rewrites the graph

The second program writes a little more than it needs: the same multiplication twice.
[`examples/01_rewrite.cml`](examples/01_rewrite.cml):

```cml
// The same shape as 01_hello.cml, with one expression that has more than one writing.
// Run: camel docs/tutorial/examples/01_rewrite.cml
module rewrite_hook

func twice(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    println(twice(3))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_rewrite.cml
```

```text
12
```

The answer is right, but the program says more than it needs to. Add `std::opt` (the optimization pass) and look
at the graph again:

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The same shape as 01_hello.cml, with one expression that has more than one writing.
// Run: camel docs/tutorial/examples/01_rewrite.cml
module rewrite_hook

func twice(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    println(twice(3))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_rewrite.cml std::opt std::gir
```

```text
        subgraph cluster_F02 {
            label="twice";
            F02 [label="ARGS", type="ARGS"];
            O02 [label="EXIT", type="EXIT"];
            N00 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N01 [label="4", type="DATA", value_type="int"];
            N02 [label=":op/mul_l", type="OPER", origin=92, span="5:17-5:18", srcfile="docs/tutorial/examples/01_rewrite.cml", value_type="int"];
            F02 -> N00 [label="0", type="NORM_PORT"];
            N02 -> O02 [label="0", type="EXIT"];
            N00 -> N02 [label="0|0", type="NORM"];
            N01 -> N02 [label="0|1", type="NORM"];
        }
```

That is the `twice` part of `main` in the output (an excerpt). `x * 2 + x * 2` is gone: there is one
multiplication node and one constant `4`. The compiler merged the two multiplications at the graph level, and
**the result did not change** — `camel docs/tutorial/examples/01_rewrite.cml` still prints `12`.

A program is not a list that says "do this, then that"; it is a graph of **who needs whose result**. And a graph
can be rewritten, before you ever run it. That is what makes Camel different from other languages, and it is the
thing every later chapter comes back to.

## On the graph

The one sentence to take away: **a function is a subgraph and a call is a node on the graph**; a sync block is a
sync edge between effects; optimization replaces the graph with a smaller graph that computes the same thing.

## Exercise

Write a function `area(w: int, h: int): int` that returns their product, and print `area(3, 4)` from `main`. Then
add `std::gir` and find in your graph: the two `PORT` nodes `w` and `h`, the one `OPER` node that multiplies, and
the two data edges that join them.

Answer [`examples/01_exercise.cml`](examples/01_exercise.cml):

```cml
// The answer to the exercise of chapter 1: a function of two parameters, and its value.
// Run: camel docs/tutorial/examples/01_exercise.cml
module exercise

func area(w: int, h: int): int {
    return w * h
}

func main(): int sync {
    println(area(3, 4))
    return 0
}
```

```bash
camel docs/tutorial/examples/01_exercise.cml
```

```text
12
```

## Reference

- [Language reference 1, Introduction](../language/en/01_introduction.md)
- [Language reference 4.1, Program and entry](../language/en/04_execution_model.md#41-program-and-entry)
- [Command line: passes and pipelines](../cli.md)