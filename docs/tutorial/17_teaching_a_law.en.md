# Chapter 17 · Teaching the compiler a law

English | [中文](17_teaching_a_law.cn.md) · [Back to the contents](tutorial.en.md)

In chapter 1 the compiler turned `x * 2 + x * 2` into `x * 4` without changing the result. How dare it? Because
it holds a set of **laws**: "two multiplications by the same value are one multiplication". Those laws are
written in the standard library and they are finite; **the laws of your own field it does not know, and you have
to tell it**. This chapter writes one law, has the compiler check it, and then watches it being used on your
program.

## Writing a law

[`examples/17_rule.cml`](examples/17_rule.cml):

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_rule.cml
```

```text
sampled negate: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: (x * -1) = (0 - x)

-7 56 28
```

A `rule` has three parts: a **name**, a **pattern** (left of `=>`) and **what it may be replaced by** (right of
`=>`).

- A parameter written `x: int` is "any expression here"; a parameter written `const c: int` is "a constant must
  be here". `x * c1 + x * c2 => x * (c1 + c2)` is how the standard library writes its combining law (the
  `c1 + c2` on the right is computed at compile time).
- A `rule` is a **value**, of type `Rule`. It can be passed to a function, put in an array, or handed to the
  rewriting engine.
- `check` draws instances for it at random, evaluates both sides, and compares. The line above says the law was
  tried 8 times and the two sides agreed all 8 times, and `(x * -1) = (0 - x)` is how it read the law.
- `check` decides by **sampling**: a counter-example proves the law wrong, but no counter-example only means none
  was found.

## Making the rewriting engine use it

For a law to actually change a program it has to reach the rewriting engine. `rewrite(p, rules)` in the `rewrite`
module is "run the laws in `rules` once over the whole program"; it takes a `Program`, so it needs a **pass**
around it ([`examples/laws.cml`](examples/laws.cml); writing your own pass is chapter 18's subject):

```cml
// The law of chapter 17 as a pass the pipeline can run: same rule, wrapped so a pipeline can
// apply it to a program.
// Run: camel -I docs/tutorial/examples docs/tutorial/examples/17_rule.cml laws::apply std::gir
module laws

import { Program } from graph
import { rewrite } from rewrite

// Multiplying by -1 is negating.
rule negate(x: int) { x * -1 => 0 - x }

// `rewrite` applies the rules it is given, once, over the whole program.
export func apply(p: Program): Program! {
    return rewrite(p, [negate])
}
```

`-I` tells the compiler where to find the module `laws`, and `laws::apply` is that pass. Put it in front of
`std::gir`:

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::gir's DOT, not the program's stdout -->

```cml
// The law of chapter 17 as a pass the pipeline can run: same rule, wrapped so a pipeline can
// apply it to a program.
// Run: camel -I docs/tutorial/examples docs/tutorial/examples/17_rule.cml laws::apply std::gir
module laws

import { Program } from graph
import { rewrite } from rewrite

// Multiplying by -1 is negating.
rule negate(x: int) { x * -1 => 0 - x }

// `rewrite` applies the rules it is given, once, over the whole program.
export func apply(p: Program): Program! {
    return rewrite(p, [negate])
}
```

```bash
camel -I docs/tutorial/examples docs/tutorial/examples/17_rule.cml laws::apply std::gir
```

```text
            label="negated";
            F04 [label="ARGS", type="ARGS"];
            O04 [label="EXIT", type="EXIT"];
            N06 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N07 [label="0", type="DATA", value_type="int"];
            N08 [label=":op/sub_l", type="OPER", origin=1930, span="10:13-10:14", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            F04 -> N06 [label="0", type="NORM_PORT"];
            N08 -> O04 [label="0", type="EXIT"];
            N07 -> N08 [label="0|0", type="NORM"];
            N06 -> N08 [label="0|1", type="NORM"];
        }
```

The source of `negated` says `x * -1`, and the graph now says `0 - x` (`N07` is the constant `0`, `N08` is the
subtracting `:op/sub_l`): **your law has been used on your program**. The result does not change (`negated(7)` is
still `-7`), which is the other half of chapter 1's sentence: a rewrite can come from a law you wrote.

## The compiler's own laws

`std::opt` uses the same engine, with its laws taken from the standard library. In the same program, the
`combined` part of `camel docs/tutorial/examples/17_rule.cml std::opt std::gir` reads (an excerpt):

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_rule.cml std::opt std::gir
```

```text
            label="combined";
            F05 [label="ARGS", type="ARGS"];
            O05 [label="EXIT", type="EXIT"];
            N09 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N0A [label="8", type="DATA", value_type="int"];
            N0B [label=":op/mul_l", type="OPER", origin=1800, span="16:17-16:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            F05 -> N09 [label="0", type="NORM_PORT"];
            N0B -> O05 [label="0", type="EXIT"];
            N09 -> N0B [label="0|0", type="NORM"];
            N0A -> N0B [label="0|1", type="NORM"];
        }
```

`x * 3 + x * 5` is down to one multiplication: `3 + 5` is folded into the constant `8` first, and then the two
multiplications become one. That law is not in your file; it is in the standard library, and `std::opt` uses it
because that library is what the pipeline holds.

## Equivalent graphs: several writings of one value

Optimization does not always decide at once which writing to keep. `std::opt::saturate` applies the standard
library's laws until nothing changes and **keeps every writing of a value**, leaving the choice to a later stage.
In the same program, the `doubled` part of
`camel docs/tutorial/examples/17_rule.cml std::opt::saturate std::gir` reads (an excerpt):

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt::saturate std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) { x * -1 => 0 - x }

func negated(x: int): int {
    return x * -1
}

// Laws the compiler's own library already knows: a sum of two scaled copies of one value is one
// scaled copy, so this is one multiplication.
func combined(x: int): int {
    return x * 3 + x * 5
}

// The same value written twice: `std::opt::saturate` keeps both writings in one class.
func doubled(x: int): int {
    return x * 2 + x * 2
}

func main(): int sync {
    // `check` draws random instances and compares the two sides of every rule it is given.
    println(check([negate], 100, 1))
    println(negated(7), combined(7), doubled(7))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_rule.cml std::opt::saturate std::gir
```

```text
            label="doubled";
            writings_cost="weighted";
            F06 [label="ARGS", type="ARGS"];
            O06 [label="EXIT", type="EXIT"];
            N13 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            subgraph cluster_class_C01 {
            label="class c1";
            style=dashed;
            C01 [label="class c1", type="CLASS"];
                N14 [label="2", type="DATA", origin=1804, span="21:15-21:16", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c1", chosen="true"];
                N15 [label="2", type="DATA", origin=1807, span="21:23-21:24", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c1"];
            }
            subgraph cluster_class_C02 {
            label="class c2";
            style=dashed;
            C02 [label="class c2", type="CLASS"];
                N16 [label=":op/mul_l", type="OPER", origin=1805, span="21:13-21:14", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c2", chosen="true"];
                N17 [label=":op/mul_l", type="OPER", origin=1808, span="21:21-21:22", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c2"];
            }
            subgraph cluster_class_C03 {
            label="class c5";
            style=dashed;
            C03 [label="class c5", type="CLASS"];
                N18 [label=":op/add_l", type="OPER", origin=1809, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5"];
                N19 [label=":op/mul_l", type="OPER", origin=1809, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5", chosen="true"];
                N1A [label=":op/mul_l", type="OPER", origin=1809, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5"];
                N1B [label=":op/mul_l", type="OPER", origin=1809, span="21:17-21:18", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int", class="c5"];
            }
```

Each dashed box is **one class of equivalent values** (a `class`): the nodes inside are different writings of the
same value, and the one marked `chosen="true"` is the one extraction would keep. So `x * 2 + x * 2` in `doubled`
still has `x * (2 + 2)`, `x * 4` and others — the laws have been applied, and the choice is left for later. That
way an optimization does not lose a possibility just because some law happened to fire first.

## Knowledge of a field: `fact`

A law says "these two writings are equal". A program also holds knowledge that is not an equation but a
**property of values** — that the number on some port is never negative, say, or that a value lies between 0 and
1. Such knowledge is declared with `fact`: it gives a type an initial value, a rule for joining two parts of it,
and the condition under which it holds; a `when` in a `rule` can read it, so the rewrite happens only where the
fact holds.

`fact` and `rule` together are the two doors through which "what is true in my field" reaches the compiler:
`rule` gives equalities, `fact` gives premises. Their full rules are in chapter 8 of the language reference.

## On the graph

- **A `rule` is a small pattern on the graph**: the left of `=>` is a small pattern graph and the right is the
  graph that replaces it; applying a law means finding a match and swapping the subgraph. A rewritten node keeps
  the source span it had, so the graph still points back at your source.
- **After `saturate` one value can have several nodes**: they are grouped into equivalence classes, and `chosen`
  marks the one extraction would keep. The graph at that point is not "the program" but "the set of all
  equivalent writings".
- Whether a law comes from the standard library or from your file makes no difference at the graph level; the
  difference is only in what the pipeline holds.

## Exercise

Write a law of your own: `x * c1 + x * c2 => x * (c1 + c2)` (two multiplications of one value by constants become
one), have `check` give its verdict, and then answer one question: **why must the right-hand side be
`x * (c1 + c2)` and not `x * c1 + x * c2`?** (The first folds one multiplication at compile time; the second
only rewrites the expression.)

Answer [`examples/17_exercise.cml`](examples/17_exercise.cml):

```cml
// The answer to the exercise of chapter 17: a law written as a `rule`, and the verdict `check`
// gives it.
// Run: camel docs/tutorial/examples/17_exercise.cml
module exercise

import { check } from rewrite

rule fold_scales(x: int, const c1: int, const c2: int) { x * c1 + x * c2 => x * (c1 + c2) }

func main(): int sync {
    println(check([fold_scales], 100, 1))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_exercise.cml
```

```text
sampled fold_scales: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: ((x * c1) + (x * c2)) = (x * (c1 + c2))
```

## Reference

- [Language reference 4.7, Passes and pipelines](../language/en/04_execution_model.md#47-passes-and-pipelines)
- [Language reference 8.5, The complete grammar](../language/en/08_top_level_and_grammar.md#85-the-complete-grammar)
- [Design principles 9.3, Pass parameters](../design-principles.en.md#93-pass-parameters)