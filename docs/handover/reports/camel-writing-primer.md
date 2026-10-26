# Writing Camel: a short primer

Build 8394a0eaa (pin `/work/pins/8394a0eaa`), 2026-10-26. Every snippet below was run on that build and the output shown is copied from the run. Target-version only: where the pin has an old form that the stdlib drafts drop, it is not shown.

Rules of thumb. A program is `func main(): int sync { ...; return 0 }`. There are no `for`/`while` statements and no `+=` on a value you did not declare `var`. Loops are the `loop` expression, ranges `a..b` with `map`/`filter`/`reduce`, or comprehensions. Library functions that take a function or a setting take it **in angle brackets**: `xs->map<f>`, `xs->count<pred>`, `xs->append<v>`. Numbers never convert implicitly: `int`, `float`, `f32` are different types, and you convert by writing the type as a function (`f32(i)`, `float(n)`, `int(x)`). `//` starts a comment (it is not integer division; `7 / 2` is `3`).

## 1. Loop over `0..n-1`, accumulate a scalar

`loop acc = start for i in range do next_acc` is the loop; the body value is the next state. Or build an array and reduce it.

```camel
import { sum } from array

func sum_sq(n: int): int {
    return loop acc = 0 for i in 0..n do acc + i * i
}

func main(): int sync {
    let n = 5
    println(sum_sq(n), [i * 2 for i in 0..n]->sum)
    return 0
}
```
```text
30 20
```

## 2. Build an `f32` array of runtime length, append, concatenate

A float literal takes the type its other operand has (`f32(i) * 0.5` is `f32`), a variable never does. `append<v>` wants a value of the element type, so write `f32(9)`, not `9.0`. Put no type annotation on a `let` that holds a comprehension (see 11). `f32[...]` is not a literal; write `[f32(1), f32(2)]`, or `let xs: f32[] = [1.0, 2.5]` (literals adapt to an annotation).

```camel
func main(): int sync {
    let n = 4
    let xs = [f32(i) * 0.5 for i in 0..n]      // f32[]: the literal 0.5 takes f32
    let ys = xs->append<f32(9)>                // a new array; xs is unchanged
    let zs = concat(xs, ys)
    println(xs, ys, zs)
    return 0
}
```
```text
[0.0, 0.5, 1.0, 1.5] [0.0, 0.5, 1.0, 1.5, 9.0] [0.0, 0.5, 1.0, 1.5, 0.0, 0.5, 1.0, 1.5, 9.0]
```

## 3. Index, length, slice, copy with replacements

`xs[i]` from 0; `xs[-1]` is a fault, so use `last(xs)`. Slices are `xs[a:b]`, `xs[a:]`, `xs[:b]`. Values never change under you: assigning into a path of a `var` rebinds the whole variable, and the original is untouched.

```camel
func main(): int sync {
    let xs = [f32(1), f32(2), f32(3), f32(4)]
    println(xs[2], len(xs), last(xs), xs[1:3], xs[1:], xs[:2])
    var w = xs
    w[0] = f32(100)
    w[1] += f32(0.5)
    println(w, xs)
    return 0
}
```
```text
3.0 4 4.0 [2.0, 3.0] [2.0, 3.0, 4.0] [1.0, 2.0]
[100.0, 2.5, 3.0, 4.0] [1.0, 2.0, 3.0, 4.0]
```

## 4. Reductions: sum, and count/filter

`sum` and `count` are not built in: `import { sum, count } from array`. `filter`, `map`, `reduce`, `len`, `concat`, `last` need no import. A function argument goes in `<...>`, not in parentheses.

```camel
import { sum, count } from array

func main(): int sync {
    let xs = [3.0, -1.0, 4.0, -1.0, 5.0]
    let pos = xs->filter<(x: float): bool => x > 0.0>
    println(xs->sum, pos->sum, len(pos))
    println(xs->count<(x) => x > 0.0>)
    return 0
}
```
```text
10.0 12.0 3
3
```

## 5. Elementwise map with a lambda

A lambda is `(x: T): R => body`; types may be left out when they can be inferred (`(x) => ...`). There is no `sync` before `=>`; a body that has effects is `=> sync { ... }`, and its last expression is its value. A plain `{ ... }` body drops the effect with `warning[SemanticDiag::EffectNotExecuted]`.

```camel
func main(): int sync {
    let xs = [1.5, 2.5]
    let ys = xs->map<(x: float): float => x * 2.0>
    let zs = xs->map<(x) => sync { println('see', x)
        x + 1.0 }>
    xs->foreach<(x: float): void => sync { println(x) }>
    println(ys, zs)
    return 0
}
```
```text
see 1.5
see 2.5
1.5
2.5
[3.0, 5.0] [2.5, 3.5]
```

## 6. Random numbers

Pin: `import { seed, rand } from random` gives a seeded global stream and `rand()` is a scalar `float` in [0, 1). The draft `docs/handover/stdlib/random.cml` also has keyed draws (`key(42)->uniform::<f32><shape: [3, 4]>`, `next_key()`), which the pin does not have, so there is no way on this build to draw an array of a given shape; `import { uniform }` fails with `ImportNameNotExported`. The only form I can show:

```camel
import { seed, rand } from random

func main(): int sync {
    seed(42)
    let a = rand()
    let b = rand()
    println(a, b, a != b)
    return 0
}
```
```text
0.755155532954539 0.6390313938546974 true
```

## 7. State that persists across calls

Camel is value-semantic; the intended forms are (i) state in the loop (`loop count = 0 ... do count + 1`, last two lines below) or in arguments and results, and (ii) when a function must own state, a `var` captured by a closure and written inside a `sync` body (a captured write is tracked as a write to that variable). There is no `global`/`nonlocal`; a `var` captured by a closure is the counter. A `var` of type `T?` (`null` until the first call) is the "created on first call" form. A plain `best = hi` should compile (a `float` enters `float?`) but does not on this build, so the snippet writes `hi as float?` (a known defect, listed at the end).

```camel
func make_counter(): () => int {
    var n = 0
    return (): int => sync {
        n = n + 1
        n
    }
}

func make_best(): (x: float) => float {
    var best: float? = null
    return (x: float): float => sync {
        let m = best ?? x
        let hi = if x > m then x else m
        best = hi as float?
        hi
    }
}

func main(): int sync {
    let next = make_counter()
    let a = next()
    let b = next()
    let best = make_best()
    let p = best(3.0)
    let q = best(1.0)
    let r = best(5.0)
    println(a, b, p, q, r)
    let total = loop count = 0 for i in 0..3 do count + 1
    println(total)
    return 0
}
```
```text
1 2 3.0 3.0 5.0
3
```

## 8. A module-level setting changed between two calls

Do not rely on a module-level `var`: the design principles (3.5) say a function reading and writing one is "not fully implemented; leave it alone". The intended forms are a `with` parameter (configuration, written `f<2>(x)`), or a `var` parameter, which is a reference to the caller's variable.

```camel
with <scale: int>
func apply(x: int): int {
    return x * scale
}

func bump(var c: int): void sync {
    c = c + 1
}

func main(): int sync {
    println(apply<2>(5), apply<10>(5))
    var c = 0
    bump(c)
    bump(c)
    println(c)
    return 0
}
```
```text
10 50
2
```

## 9. A function with no result

Return type `void`, and `sync` so its effects are kept and ordered. Without `sync` the statements that nothing needs are dropped (a lone final `println` survives as the block value, but do not count on it).

```camel
func report(msg: string): void sync {
    println(msg)
    println('done')
}

func main(): int sync {
    report('hello')
    return 0
}
```
```text
hello
done
```

## 10. Printing

`println(a, b, ...)` joins arguments with one space. Interpolation is a **backquote** string with `${expr}` and an optional `${expr:spec}` (`.2f`, `5d`, as in `format`); there is no `f"..."`. `'...'->format<a, b>` is the same with `{}` placeholders (its arguments also go in `<>`). Floats print the shortest text that reads back, `f32` with `f32` precision, an integral float as `3.0`, arrays as `[1.0, 2.5]`.

```camel
func main(): int sync {
    let x = f32(2.5)
    let n = 3
    let xs = [f32(1), f32(2.5)]
    println(`x=${x} n=${n} xs=${xs}`)
    println(`pi=${3.14159:.2f} n=${n:5d}|`)
    println('x', x, n, xs)
    println(1.0 / 3.0, f32(1) / f32(3), 3.0)
    return 0
}
```
```text
x=2.5 n=3 xs=[1.0, 2.5]
pi=3.14 n=    3|
x 2.5 3 [1.0, 2.5]
0.3333333333333333 0.33333334 3.0
```

## 11. Common mistakes (each checked on the pin)

| Python habit | Compiler says | Write instead |
|---|---|---|
| `for i in 0..n { s = s + i }`, `while c { }` | `SyntaxDiag::UnexpectedToken` at `for`/`while` | `loop s = 0 for i in 0..n do s + i`; `loop i = 0 while i < n do i + 1` (several states: `loop i = 0, acc = 0 while i < n do (i + 1, acc + i)`) |
| `a ++ b` | `UnexpectedToken` at `+` | `concat(a, b)` |
| `sum(xs)` with no import | `UnresolvedReference: 'sum'` | `import { sum } from array` (also `count`, `min`, `max`, `mean`, ...) |
| `-> unit` | `UnresolvedTypeReference: 'unit'` | `: void` |
| `(x: T): T sync => { ... }` | `MissingToken: Expected '=>' before 'sync'` | `(x: T): T => sync { ... }` |
| `f32[1.0, 2.0]` | `FunctionNameAsValue: 'f32'` | `[f32(1), f32(2)]` |
| `cast(x, "f32")` | `UnresolvedReference: 'cast'` | `f32(x)` |
| `xs.append(2)`, `xs.len()` | `TypeNotIndexable` | `xs = xs->append<2>`, `len(xs)`; a bare `xs->append<2>` statement is `UnusedPureResult` |
| `x = 2` after `let x = 1` | `ImmutablePassedToVarParam` | declare `var x = 1`; prefer new `let` names |
| `n * 0.5`, `add(1, 2.0)`, `f32 * float` | `NoMatchingFunction` | `float(n) * 0.5`; convert: `f32(x)`, `float(n)`, `int(x)` |
| `x as int`, `s as float` | `DynamicCastForbidden` | `int(x)` (truncates: `int(-2.5)` is `-2`), `float(s)`, `parse_int(s)` |
| `xs->map(f)`, `count(xs, f)`, `format(s, a)` | `NoMatchingFunction`, "takes `f` in angle brackets" | `xs->map<f>`, `xs->count<f>`, `s->format<a>` |
| `xs[-1]` | `RuntimeDiag::IndexOutOfRange` | `last(xs)` |
| `f"x={x}"` | `MissingComma` at the string | `` `x=${x}` `` |
| `x ** 2`, `a and b`, `not a` | `UnexpectedToken` / `MissingComma` | `x ^ 2`, `a && b`, `!a` |
| `def f(x): ...`, `func f(x: int) -> int` | `UnexpectedToken` at `def` / `->` | `func f(x: int): int { return x }` (the last expression of a block is also its value) |
| `println` inside a function without `sync` | `warning[EffectNotExecuted]`, nothing printed | `func f(): int sync { println(..); return 0 }` |
| `func main(args: string[])` | `RuntimeDiag::EntryArgumentMissing` | `func main(): int sync { ...; return 0 }`; a `main` whose last expression is not an int is `ReturnTypeMismatch` |
| `let xs = []` | `EmptyArrayWithoutType` | `let xs: int[] = []` |

Known defects on this build (not language rules): `let xs: f32[] = [f32(i) for i in 0..n]` (a comprehension bound to a `let` with a written type) silently gives `[]`; leave the annotation off, or `return` the comprehension from a function with the declared type, or use `0..n->map<...>`. Assigning a `T` to a `var` of type `T?` is refused (`NoMatchingFunction`); write `v = x as T?` until it is fixed. Not shown because the drafts drop them: `head`, `tail`, `slice<a, b>` (use `xs[0]`, `xs[1:]`, `xs[a:b]`), `extend`.
