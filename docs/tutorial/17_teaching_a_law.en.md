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
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

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

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
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

## Handing the law to the optimizer

A `rule` is a **value** and does not enter any pass by itself: the rules written in a module are not collected
([language reference 9.4](../language/en/09_rules_and_facts.md)). `std::opt` uses the standard library's laws
(`stdlib/rules.cml`); to hand it your own, the rule set is passed **explicitly**: in Camel
`f->std::opt<rules: [negate]>`, on the command line `std::opt rules=[law::negate]`. `std::opt` has no `rules`
parameter yet (writing it gives `SemanticDiag::ParamArgumentUnknown`), so to make your own law take effect today
write a pass of your own and hand that to the command line — `check([negate], …)` above is the example of
handing it over explicitly. Given this file, `std::opt` leaves the `negated` half **unchanged**: `x * -1` stays
as it is, because that law is yours and the optimizer does not know it.

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

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

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
```

```bash
camel docs/tutorial/examples/17_rule.cml std::opt std::gir
```

```text
            label="negated";
            F0D [label="ARGS", type="ARGS"];
            O0D [label="EXIT", type="EXIT"];
            N33 [label="x", type="PORT", param_name="x", param_kind="normal", param_index="0", value_type="int"];
            N34 [label="-1", type="DATA", origin=2078, span="10:15-10:17", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            N35 [label=":op/mul_l", type="OPER", origin=2079, span="10:13-10:14", srcfile="docs/tutorial/examples/17_rule.cml", value_type="int"];
            F0D -> N33 [label="0", type="NORM_PORT"];
            N35 -> O0D [label="0", type="EXIT"];
            N33 -> N35 [label="0|0", type="NORM"];
            N34 -> N35 [label="0|1", type="NORM"];
        }
```

`negated` is still `x * -1` (`:op/mul_l`, the constant `-1`), and the result does not change (`negated(7)` is
still `-7`): that is the reading of "the law did not go in by itself". To make it take effect on the graph, hand
it over explicitly — `check` works that way, and so does a rule set for `std::opt` (`std::opt rules=[…]`).

This law is used only where the fact holds: `x * -1` may be written `0 - x` when `x` is a count, so it is never
negative. That fact is declared at the end of the file, and the next section is about it.

## The compiler's own laws

`std::opt` holds the standard library's laws and nothing else (the one you wrote is not among them): a `x * 3 + x * 5` is a shape the library already knows.
In the same program, the
`combined` part of `camel docs/tutorial/examples/17_rule.cml std::opt std::gir` reads (an excerpt):

<!-- camel-snippet: graph-dump: the text block is one excerpt of std::opt std::gir's DOT, not the program's stdout -->

```cml
// The law of this program's arithmetic, written the way the compiler reads laws.
// Run: camel docs/tutorial/examples/17_rule.cml
module law

import { check } from rewrite

// Multiplying by -1 is negating. A `rule` names a pattern and the writing it may be replaced by.
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

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

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
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
rule negate(x: int) when nonnegative(x) { x * -1 => 0 - x }

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

// What the compiler may rely on: the values this program works with are counts, so a count is
// never negative and a difference of two counts may be. `join` says how the two writings of one
// value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x - y   => false
    case _       => true
} join (lhs: bool, rhs: bool) => lhs | rhs
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
**property of values**: a value is never negative, or lies between 0 and 1, or is a count of some range. Such
knowledge is declared with `fact`, a `rule`'s `when` reads it, and the rewrite happens only where the fact
holds.

[`examples/17_fact.cml`](examples/17_fact.cml):

```cml
// The knowledge of your field that is not an equation, handed to the compiler.
// Run: camel docs/tutorial/examples/17_fact.cml
module domain

import { check } from rewrite
import { abs } from math

// A `fact` says what the compiler may rely on for a value: a square is never negative. The match
// reads how the value was built; `join` says how the two writings of one value are merged.
fact nonnegative(v: int): bool match v {
    case const c => c >= 0
    case x * x   => true
    case _       => false
} join (lhs: bool, rhs: bool) => lhs | rhs

// The law holds only where the fact does: on a value that is never negative, `abs` changes nothing.
rule abs_of_nonnegative(x: int) when nonnegative(x) { abs(x) => x }

func squared(x: int): int {
    return abs(x * x)
}

func main(): int sync {
    // `check` samples the law together with its premise, so the fact decides where it may fire.
    println(check([abs_of_nonnegative], 100, 1))
    println(squared(-3))
    return 0
}
```

```bash
camel docs/tutorial/examples/17_fact.cml
```

```text
sampled abs_of_nonnegative: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: abs(x) = x

9
```

The head of a `fact` says which type it gives which piece of knowledge for (here `int`'s `nonnegative`); the
body is a `match`, but what it reads is **how the value was built on the graph**: a square is never negative, a
constant can be decided at compile time, and everything else is `case _`. `join` says how the knowledge of two
writings of one value is merged (here `(lhs, rhs) => lhs | rhs`: either writing saying it is
non-negative is enough).

`fact` and `rule` together are the two doors through which "what is true in my field" reaches the compiler:
`rule` gives equalities, `fact` gives premises. Their full rules are in
[chapter 9 of the language reference](../language/en/09_rules_and_facts.md).

## On the graph

- **A `rule` is a small pattern on the graph**: the left of `=>` is a small pattern graph and the right is the
  graph that replaces it; applying a law means finding a match and swapping the subgraph. A rewritten node keeps
  the source span it had, so the graph still points back at your source.
- **After `saturate` one value can have several nodes**: they are grouped into equivalence classes, and `chosen`
  marks the one extraction would keep. The graph at that point is not "the program" but "the set of all
  equivalent writings".
- Whether a law comes from the standard library or from your file makes no difference at the graph level; the
  difference is only in what the pipeline holds.
- **A `fact` adds no node to the graph**: it is a pass's claim about the values on the graph, and it is what decides
  whether a law may be used.

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