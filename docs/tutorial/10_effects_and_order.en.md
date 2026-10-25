# Chapter 10 · Effects and order

[中文](10_effects_and_order.cn.md) | English · [Back to the contents](tutorial.en.md)

Chapter 9's sentence has a price: **an expression is evaluated because somebody needs its result**. So why does
"print a line" happen at all? And which of two prints goes first? This chapter is Camel's answer to both: effects and
order are not defaults, they are **written in the program**, and the graph has edges that say so.

## What nobody needs does not run

[`examples/10_unused_effect.cml`](examples/10_unused_effect.cml):

```cml
// An effect whose result nothing needs: `greet`'s body is not a sync block, so its `println`
// never runs.
// Run: camel docs/tutorial/examples/10_unused_effect.cml
module unused_effect

func greet(): int {
    println('hello from greet')
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_unused_effect.cml
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> docs/tutorial/examples/10_unused_effect.cml:7:5
   |
 7 |     println('hello from greet')
   |     ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
```

Only `1` is printed. The `1` that `greet` returns does not depend on that `println`, so on the graph it is a node nobody
wants: **an effect nobody needs does not run**. That is not an error but a rule of the language; the compiler gives the
warning so that a `sync` you forgot to write is visible.

The rule and its reason: the only order a program can observe comes from **values being passed** and from **order
written down**. Everything else may run in parallel, be reordered, be dropped -- so the compiler assumes nothing. Write
`sync` where order matters.

## `sync` on a function body

`sync` after the return type says that this function body is a **sync block**: the statements written directly in it
run in the order written, and they have all finished when the function hands out its result. The same program as above,
with `greet`'s body made a sync block ([`examples/10_sync_body.cml`](examples/10_sync_body.cml)):

```cml
// The same two statements as 10_unused_effect.cml, in a sync body: now both run.
// Run: camel docs/tutorial/examples/10_sync_body.cml
module sync_body

func greet(): int sync {
    println('hello from greet')
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_sync_body.cml
```

```text
hello from greet
1
```

Both lines appear, and the warning is gone. **A function's behaviour is decided by the function itself**: `main`'s sync
block has no say over the statements inside `greet`, and the other way round -- a call puts one function's subgraph into
a larger graph without changing its structure
([language reference 4.3.2](../language/en/04_execution_model.md#432-a-functions-behaviour-is-decided-by-the-function-itself)).

## `sync` does not reach inward

A sync block orders only **the statements written directly in it**. An `if`, a loop body or a nested block written
inside it behaves as it is written: write `sync` there too if you want order there.
[`examples/10_sync_block.cml`](examples/10_sync_block.cml):

```cml
// A sync function body orders its own statements. What is written inside it -- an arm, a loop
// body -- is ordered only if it says `sync` itself.
// Run: camel docs/tutorial/examples/10_sync_block.cml
module sync_block

func report(second: bool): int sync {
    println('the body is a sync block')
    if second then { println('three'); println('four') }
    if second then sync { println('five'); println('six') }
    return 0
}

func main(): int sync {
    println(report(true))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_sync_block.cml
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> docs/tutorial/examples/10_sync_block.cml:8:22
   |
 8 |     if second then { println('three'); println('four') }
   |                      ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

the body is a sync block
four
five
six
0
```

`three` is not printed: that `if`'s arm is not a sync block, `println('three')`'s result is wanted by nobody, so it does
not run; `println('four')` is the arm's value, which is the `if`'s value, so it runs. The second `if`'s arm says `sync`,
and both of its statements run.

The reason for this rule is that **a block's meaning does not change with where it is written**: if an arm inherited the
outer `sync`, the same lines would mean two different things depending on their neighbours, and a reader would have to
look upward to know whether they are ordered
([design principles 3.4](../design-principles.en.md#34-sync-blocks)). One sentence worth remembering: `sync` says "this
block is in order", not "everything from here on is in order".

## `var` and the order of writes

A `var` is a cell that can be written; the order of its writes and reads also comes from the sync block it is written
in. [`examples/10_var_order.cml`](examples/10_var_order.cml):

```cml
// A `var` and the order of its writes: in a sync body the statements run in the order written, a
// read takes the value of the most recent write before it, and a write nobody reads does not run.
// Run: camel docs/tutorial/examples/10_var_order.cml
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

func latest(): int sync {
    var c = 1
    c = 2
    let before = c
    c = 3
    return before
}

func main(): int sync {
    println(twice())
    println(latest())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_var_order.cml
```

```text
2
2
```

Two rules, one `2` each:

- `twice`: both `bump` calls are in the same sync block, so they run in the order written, the second sees the value the
  first wrote, and the result is `2`.
- `latest`: `let before = c` **reads the most recent write before it**, the version written by `c = 2`; the later
  `c = 3` cannot reach back to it. So `before` is `2`, whatever `c` becomes afterwards.

The third rule is its other side: **a write that nothing reads afterwards does not run**. The same cell, two function
bodies, the same output, different warnings
([`examples/10_write_nobody_reads.cml`](examples/10_write_nobody_reads.cml)):

```cml
// A write nobody reads is not executed, and outside a sync block nothing orders it after the
// earlier uses of the variable. Both programs below print the same `1`: what the sync block adds is
// the guarantee, not the value.
// Run: camel docs/tutorial/examples/10_write_nobody_reads.cml
module write_nobody_reads

func unordered(): int {
    var c = 1
    let before = c
    c = 5
    return before
}

func ordered(): int sync {
    var c = 1
    let before = c
    c = 5
    return before
}

func main(): int sync {
    println(unordered())
    println(ordered())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_write_nobody_reads.cml
```

```text
warning[SemanticDiag::IgnoredSideEffect]: The call of '<__assn__>: (var int, int) => int' is not in a sync block, and it takes a var argument.
   --> docs/tutorial/examples/10_write_nobody_reads.cml:10:5
    |
 10 |     c = 5
    |     ^^^^^
help: Put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'): only then is the write ordered after the earlier uses of the variable. Without one, the write runs only if the variable is read afterwards.

warning[SemanticDiag::EffectNotExecuted]: The assignment has side effects but is never executed: nothing depends on it and it is not in a sync block.
   --> docs/tutorial/examples/10_write_nobody_reads.cml:10:7
    |
 10 |     c = 5
    |       ^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
1
```

Both functions print `1`: the read takes the version from before the write, and the write is read by nobody. The
difference is the guarantee -- the write inside the sync block **certainly happens**, and after the reads written before
it; for the write outside one, the compiler can only tell you that it is unordered
(`SemanticDiag::IgnoredSideEffect`), and since nothing reads the variable afterwards, the whole write does not run
(`SemanticDiag::EffectNotExecuted`).

One sentence to take away: **correct output is not the same as a correct program**. Write `sync` where order matters,
and the compiler may no longer move it.

## Effects on one resource order themselves

What an effect acts on is called a **resource**: standard output is one, the file system is one, a `var` is one.
**Two effects on the same resource that both execute happen in the order written, and need no sync block**
([design principles 3.3](../design-principles.en.md#33-resources-uses-and-pure)). In
[`examples/10_same_resource.cml`](examples/10_same_resource.cml) both prints go to standard output and both results are
used (`println` returns its argument), so they happen in the order written with no warning at all:

```cml
// Two effects on the same resource (stdout) keep the written order without a sync block.
// Run: camel docs/tutorial/examples/10_same_resource.cml
module same_resource

func report(): int {
    return 5->println->println
}

func main(): int sync {
    println(report())
    return 0
}
```

```bash
camel docs/tutorial/examples/10_same_resource.cml
```

```text
5
5
5
```

`5->println->println` is two prints: the first one's result (still `5`) is passed to the second, so both are needed.
There is no `sync` here and no warning -- **the order within one resource comes from the resource itself**.

The other way round: two effects on **different** resources have no order between them unless you write them into one
sync block. So a requirement like "write the file first, then print" is written as a sync block, not left to the
compiler to guess.

## Effects as resources: `uses` in a signature

Which resources a call touches is **part of its type**: `func save(p: Params): unit uses fs`. In Camel you may write it
by hand or leave it out -- the compiler infers it from the callees of the body (taking a fixed point on recursion) and
records it in the function type it displays. This program puts the forms side by side
([`examples/10_resources.cml`](examples/10_resources.cml)):

```cml
// What a call touches: a resource of the module, a group of them, a handle type and `uses` on a
// parameter. The bodies touch nothing -- a clause is a promise the compiler checks where it can.
// Run: camel docs/tutorial/examples/10_resources.cml
module resources

resource db
resource console = [db, stdout]

#resource type Stream = { id: int }

func read_id(s: Stream): int uses s {
    return s.id
}

func tick(): void uses console {
}

func bump(var cell: int): int uses cell sync {
    cell += 1
    return cell
}

func main(): int sync {
    let s: Stream = { id: 7 }
    println(read_id(s))
    tick()
    var cell = 0
    println(bump(cell))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_resources.cml
```

```text
7
1
```

What may follow `uses`: the grammar is `resourceClause` in
[language reference 8.5](../language/en/08_top_level_and_grammar.md#85-the-complete-grammar), the meaning is
[design principles 3.3](../design-principles.en.md#33-resources-uses-and-pure).

- **A resource the module declares**: `resource db`. A module may declare its own resources, and may group several of
  them: `resource console = [db, stdout]`.
- **A resource every program has**: `stdout`, `stderr`, `stdin`, `clock`, `env`, `fs`.
- **A parameter**: `uses s`, `uses cell`. This says "the call reads and writes the value I pass in" -- handle-style,
  like a `var` parameter: operations on one handle keep the written order, different handles do not constrain each
  other.
- **A handle type declared with `#resource type`**: `#resource type Stream = { id: int }`. **Every value** of it is a
  resource of its own, so `read_id` claims "I touch the `Stream` you give me", not "I touch all streams".

Draw the graph of this program and the `tooltip` of a function node is the resource set the compiler inferred:

```text
            tooltip="(s: Stream) => int uses s";
            tooltip="() => void uses db, stdout";
            tooltip="(var cell: int) => int uses cell";
```

All three lines say what a call touches: `read_id` touches the handle it is given, `tick` touches the `console` group
(expanded to `db` and `stdout`), `bump` touches the cell the caller hands it. **That is resource inference**: you need
not write it, and it is visible on the graph; what you write is a promise, and a body that goes beyond it is a compile
error (the errors below).

What a wrong clause looks like (all compile errors):

- a name that is neither a parameter nor a resource: `SemanticDiag::UnknownResource`.
- `pure` together with `uses` in one signature: `SemanticDiag::PureAndUses` -- `pure` says "no resource at all",
  `uses` says "these", and only one of the two may be written.
- a body-less declaration (a native operator) whose clause is not the operator's: `SemanticDiag::NativeResourceMismatch`
  -- such a declaration *is* the whole signature of the operator, resources included.
- a body that goes beyond its own `pure`/`uses` promise: `SemanticDiag::UsesPromiseViolated`. `uses` is an upper bound
  and `pure` is the empty set; the error is at the declaration and the note points at the first use the signature does
  not promise (a `pure` signature with a `var` parameter is reported here too: a `var` parameter is a resource by
  itself).

## `pure`: touching no resource at all

`pure` is the promise that a call touches no resource at all. Written in a **function type** it is a
requirement: whoever passes a function in must pass one whose **inferred** resource set does not go
beyond it -- a function that uses fewer resources fits where more are asked for, and not the other way
round ([design principles 3.3](../design-principles.en.md#33-resources-uses-and-pure)).
**What is written is the promise, and a body that goes beyond it is a compile error**: a named function
is held to the clause it writes (`pure` must come out empty), and an anonymous function has no clause --
its resources are inferred, so a lambda that touches nothing, or a named function whose body touches
nothing, fits a `pure` parameter. `apply` in
[`examples/10_pure_callback.cml`](examples/10_pure_callback.cml) accepts only a pure function:

```cml
// `pure` in a function type is part of the requirement: what is compared is the resources the
// argument's body infers against the empty set the parameter asks for, and `twice` touches nothing.
// Run: camel docs/tutorial/examples/10_pure_callback.cml
module pure_callback

func apply(f: (x: int) => int pure, x: int): int {
    return f(x)
}

func twice(x: int): int pure {
    return x * 2
}

func main(): int sync {
    println(apply(twice, 4))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_pure_callback.cml
```

```text
8
```

Pass a function that touches a resource and the trouble is not a wrong value but a **signature that does
not match**: what is compared is the **resources the argument infers against the resources the parameter
asks for**, and the error is raised at the call
([`examples/10_pure_callback_wrong.cml`](examples/10_pure_callback_wrong.cml)):

<!-- camel-snippet: counter-example: this program deliberately does not compile: the call is a mismatch, so the text block below is the diagnostic, not output -->

```cml
// A counter-example: `noisy` prints, so the resources its body infers do not fit the empty set the
// parameter asks for. The error names the signature that would have matched.
// Run: camel docs/tutorial/examples/10_pure_callback_wrong.cml
module pure_callback_wrong

func apply(f: (x: int) => int pure, x: int): int {
    return f(x)
}

func noisy(x: int): int sync {
    println(x)
    return x * 2
}

func main(): int sync {
    println(apply(noisy, 4))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_pure_callback_wrong.cml
```

```text
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> ((x: int) => int, int).
   --> docs/tutorial/examples/10_pure_callback_wrong.cml:16:18
    |
 16 |     println(apply(noisy, 4))
    |                  ^^^^^^^^^^
help: Check the number and types of arguments to ensure they match one of the available overloaded function signatures. Available overloads:
          apply: (f: (x: int) => int pure, x: int) => int

camel: 1 error
```

Note the `Available overloads` line of the error: it writes the required signature out
(`(f: (x: int) => int pure, x: int) => int`). `pure` in a function type means exactly that it is part of the type --
not a comment, not a convention, but a matching rule.

## On the graph

Draw `twice` of [`examples/10_var_order.cml`](examples/10_var_order.cml) (`camel docs/tutorial/examples/10_var_order.cml std::rgir`; every picture in this book is generated by the script of chapter 1):

![twice's subgraph: `var c` is a cell, and the two writes have a sync edge between them](images/10_var_order.svg)

- `N07` is the **cell** built by `var c = 0` (`:var/new`): on the graph a `var` is "a node that builds a cell plus
  ordinary reads and writes", not a variable that changes by itself.
- `N08` and `N09` are the two `bump` calls, and **there is a sync edge between them** (dashed, `type="CTRL"`): they are
  written in a sync block, so the second comes after the first. That is where the `2` comes from.
- `N0A` (`:var/get`) reads the cell, and `N0B` is a `GATE`: the end of the sync chain hands the value out through it --
  it reads the value on one side, takes the chain's tail on the other, and passes the value on unchanged
  ([design note: the three kinds of edges](../design/effects/edges.cn.md)).

The graph has three kinds of edges, and this chapter has accounted for the last two:

| Edge | What it does | Where it comes from |
|---|---|---|
| data edge | where a value comes from; a needed node passes "needed" to what it reads | values being passed |
| sync edge | order, and the earlier statement is thereby needed | adjacent statements of a sync block |
| order edge | order only, and it does **not** pass "needed" | effects on the same resource |

**Only data edges and sync edges decide whether something runs**; an order edge only says who is first when both run.
That consequence is worth a lot: an effect that nobody needs and that is not in a sync block may not run at all -- the
`1` at the beginning of this chapter is exactly that.

## Exercise

1. Write `sum_upto(n: int): int sync`: add 1 to `n` with a `var`. Why is `sync` needed here (hint: what about the
   statement inside the loop body)?
2. Write `apply(f: (x: int) => int pure, x: int): int` and use it to square `5`. What do you see if you pass
   `println`?
3. The two functions of [`examples/10_write_nobody_reads.cml`](examples/10_write_nobody_reads.cml) print the same
   number. Make `ordered` a **non**-sync body: does the output change? Why?

The answer is [`examples/10_exercise.cml`](examples/10_exercise.cml):

```cml
// The answer to the exercise of chapter 10: a `var` written inside a sync block, and a callback
// that has to be pure.
// Run: camel docs/tutorial/examples/10_exercise.cml
module exercise_effects

func sum_upto(n: int): int sync {
    var total = 0
    loop i = 1 while i <= n do sync {
        total += i
        i + 1
    }
    return total
}

func apply(f: (x: int) => int pure, x: int): int {
    return f(x)
}

func square(x: int): int pure {
    return x * x
}

func main(): int sync {
    println(sum_upto(4))
    println(apply(square, 5))
    return 0
}
```

```bash
camel docs/tutorial/examples/10_exercise.cml
```

```text
10
25
```

1. The accumulation of `sum_upto` is written in the loop body, and **a loop body is a block of its own**: order there
   needs its own `sync` (`loop ... do sync { ... }`). The outer `sync` does not reach it.
2. `apply(square, 5)` is `25`; with `println` it is `SemanticDiag::NoMatchingFunction`, and the error lists the required
   `(x: int) => int pure`.
3. The output is still `1`: the read still takes the version from before the write. What changes is the **guarantee** --
   the write is no longer ordered (one more `SemanticDiag::IgnoredSideEffect`), and since nothing reads the variable
   after it, the whole write does not run (`SemanticDiag::EffectNotExecuted`).

## Reference

- [Language reference 4.2, Evaluation by dependency](../language/en/04_execution_model.md#42-evaluation-by-dependency)
- [Language reference 4.3, `sync`](../language/en/04_execution_model.md#43-sync)
- [Language reference 4.4, `var` and the order of writes](../language/en/04_execution_model.md#44-var-and-the-order-of-writes)
- [Design principles 3.3, Resources, `uses` and `pure`](../design-principles.en.md#33-resources-uses-and-pure)
- [Design principles 3.4, `sync` blocks](../design-principles.en.md#34-sync-blocks)
- [Design note: the three kinds of edges (data, sync, order)](../design/effects/edges.cn.md)