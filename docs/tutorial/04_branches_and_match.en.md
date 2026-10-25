# Chapter 4 · Branches and `match`

[中文](04_branches_and_match.cn.md) | English · [Back to the contents](tutorial.en.md)

Up to this chapter a program only ever computes straight on. A real program takes a different road depending on the
case, and here Camel differs from other languages in one place: **a branch is an expression with a value, not a
statement**. This chapter covers the two writings, `if` and `match`, and why an early `return` does not exist in
Camel — including what the compiler says when the habit of another language slips in.

## `if` is an expression

[`examples/04_branches.cml`](examples/04_branches.cml):

```cml
// if is an expression; match compares a name with literal patterns.
// Run: camel docs/tutorial/examples/04_branches.cml

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

```bash
camel docs/tutorial/examples/04_branches.cml
```

```text
positive negative zero
vowel space other
A B D
```

In `if condition then value else value` both `then` and `else` are required when the `if` is used as an expression,
and the two arms must have the same type.

## `match`: one value against several writings

<!-- camel-snippet: fragment: the shape of a match, not a program -->

```cml
match the expression being matched {
    case pattern | pattern ... when condition => value
    case _ => value
}
```

- **A pattern is a literal** (an integer, a float, a string, `true`/`false`) or the wildcard `_`; one `case` may
  list several patterns with `|`. The first arm that matches is the one taken.
- **`case _` has to be written** (unless every value is listed, as a `bool` with both `true` and `false` is). This is
  not a matter of style: the compiler refuses a `match` without a fallback, for a direct reason — when the value is
  none of the ones you listed, the expression has no value to be.
- **`when` puts a condition on a `case`**: `case 1 when n > 0 => ...` takes that arm only when the pattern matches
  **and** the condition holds.

[`examples/04_guards.cml`](examples/04_guards.cml):

```cml
// A `when` guard adds a condition to a case; the first arm that matches is taken.
// Run: camel docs/tutorial/examples/04_guards.cml

func label(n: int): string {
    return match n {
        case 1 when n > 0 => 'one, positive'
        case 1 => 'one'
        case _ => 'other'
    }
}

func main(): int sync {
    println(label(1), label(2), label(0))
    return 0
}
```

```bash
camel docs/tutorial/examples/04_guards.cml
```

```text
one, positive other other
```

## What a mistake looks like: no `case _`

A counter-example — this `match` lists only `1`:

<!-- camel-snippet: counter-example: the page shows the diagnostic the compiler gives, not a program that runs -->

```cml
func a(n: int): int {
    return match n {
        case 1 when n > 0 => 10
    }
}
```

```text
error[SemanticDiag::MatchWithoutDefault]: This 'match' on a value of type 'int' has no 'case _'.
help: A 'match' must have a default case: add 'case _ => ...' for the values the cases do not list. A
      'match' on a bool that has both 'case true' and 'case false' is complete, and so is one over 'T?'
      or 'T!' with its two fixed patterns.
```

The arms of a `match` must also agree in type, or it is `SemanticDiag::BranchReturnTypeMismatch`; `case 1` on a
`string` is `SemanticDiag::MatchCaseTypeMismatch` (a literal in a pattern is read as the type being matched, and a
`string` does not accept `1`).

## The easiest trap to fall into: `return` inside a block

On the graph, each arm of a branch is **a subgraph of its own**, so a `return` written inside an arm's block gives
**the value of that branch** and does not leave the function. Written out of the habit of another language, it lands
in this trap:

[`examples/04_block_return.cml`](examples/04_block_return.cml):

```cml
// `return` inside a block gives the value of that block; it does not leave the function.
// Run: camel docs/tutorial/examples/04_block_return.cml

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

```bash
camel docs/tutorial/examples/04_block_return.cml
```

```text
reached the end
negative
```

`wrong(-1)` gives `'reached the end'`: nothing uses the value of that `if`, so its two `return`s have no effect on
the function. The compiler can see this, and gives one warning for each `return`:

```text
warning[SemanticDiag::ExitInUnusedBranch]: This 'return' gives the value of the 'if' it is in, and nothing uses that value: it does not leave the function.
  --> docs/tutorial/examples/04_block_return.cml:6:21
   |
 6 |     if x < 0 then { return 'negative' } else { return 'not negative' }
   |                     ^^^^^^^^^^^^^^^^^
note: the value of this 'if' is not used
```

There is no early `return` in Camel: to end a function early, let the branch be the last expression of the body, or
write `return` in front of it as `right` does. This follows directly from "a function body's value is the value of
its last expression" (chapter 3).

## On the graph

- `if` and `match` are **branch nodes** on the graph: the condition is one of their inputs, each arm is a subgraph of
  its own, and the arms' values meet at the exit.
- That also explains the trap above: nobody reads the `if`'s exit, so what the arms' `return`s do is hand a value to
  an exit no one wants.
- The arms of a `match` are the same thing as a branch on the graph; a `when` condition goes into the branch's
  condition.

## Exercise

Write `describe(n: int): string`: `0` is `'zero'`, a negative number other than `0` is `'negative'`, and the rest is
`'even'` or `'odd'` by parity. Build it out of `if` expressions and finish with a `match`, and work out where
`case _` has to be written.

The answer is [`examples/04_exercise.cml`](examples/04_exercise.cml):

```cml
// The answer to the exercise of chapter 4: an `if` is an expression, and a `match` needs its default.
// Run: camel docs/tutorial/examples/04_exercise.cml

func describe(n: int): string {
    return if n == 0 then 'zero'
        else if n < 0 then 'negative'
        else match n % 2 {
            case 0 => 'even'
            case _ => 'odd'
        }
}

func main(): int sync {
    println(describe(0), describe(-3), describe(7), describe(8))
    return 0
}
```

```bash
camel docs/tutorial/examples/04_exercise.cml
```

```text
zero negative odd even
```

## References

- [Language reference 6.8.1 `if`](../language/en/06_expressions.md#681-if) and
  [6.8.2 `match`](../language/en/06_expressions.md#682-match)
- [Language reference 6.8.3 `match type`](../language/en/06_expressions.md#683-match-type) (used in chapter 12)
- [`SemanticDiag::MatchWithoutDefault`](../diagnostics.md#matchwithoutdefault) (the catalogue: the compiler's own
  words and the fix)