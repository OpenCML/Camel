# Camel for AI

You are about to write Camel. Read this page whole; it is short on purpose. It teaches the language
as it is meant to be, not every corner of it: for a rule's exact wording see the language reference
(`docs/language/`), for a name's signature the standard library reference (`docs/stdlib-reference.md`),
for a diagnostic's message `camel-for-ai-diagnostics.md`. Fenced `camel` blocks on this page compile.

## 1. Why Camel is the way it is

Most languages write a program as instructions for a machine, one step after another. What
compilers, parallel runtimes, automatic differentiation and GPU back ends need is different: *what
is computed, and what depends on what*. In other languages that graph is recovered afterwards, by
tracing or analysis, and it breaks at every effect, every dynamic branch and every library boundary.

In Camel **the program is the graph.** You write the intent of a computation and its dependencies;
order is written only where you need it. What that buys you, and why the rules below look as they do:

- **Nothing is traced.** The compiler sees exactly what you wrote, so independent work runs in
  parallel without being asked, and differentiation, GPU placement and export (ONNX, C++) are
  *passes over the graph*, ordinary functions you compose, not magic.
- **A value is a value.** No hidden state, no aliasing you must track: a function of the same inputs
  gives the same result, so results can be reused, fused, moved to a device or differentiated.
- **What touches the outside world is declared.** Effects (files, clock, console) are named
  resources, ordered when they must be and otherwise free, so "pure" is something the compiler
  knows and can exploit.
- **A model is a function with its weights bound.** What is bound once goes in angle brackets, what
  flows through is the ordinary argument, and `->` passes one value, so code reads as a pipeline:
  `x->linear<w, b>->relu`.
- **Failure is explicit.** Anything that can fail for reasons outside the program returns a value
  that says so (`T!`); a broken precondition is a located fault. There are no exceptions to catch, because an exception
  has no stable place on a graph; a fault can be isolated at a function boundary with `attempt(f)`,
  a function transform (the boundary is a call because a block has no stable place on a graph).

Designing code in Camel means asking: *what are the values, what depends on what, and where does the
outside world enter?* If a construct has no clear place on the graph, it is not in the language.

## 2. The mental model

- **A node runs if and only if this run needs its result.** A function call is a node that points at
  the callee's own subgraph. Two expressions that do not depend on each other have no order.
- **An effect nobody needs does not run.** `println(x)` returns `x`; a call whose result is unused
  is dropped (warning `EffectNotExecuted`). `sync` puts the effects written *directly* in a block
  into written order, and makes them happen:

```camel
func main(): int sync {
    println('first')
    println('second')
    return 0
}
```

  `sync` does not reach into an `if` arm or a `loop` body written inside it: write `sync` there too.
  Pure computation inside a `sync` block has no order; the compiler schedules it freely. Faults
  (division by zero, an index out of range) are observable, so they stay where they are written.
- **A program is a module; a file is a module named like the file.** Entry point `func main`; with a
  written return type, an `int` or `bool` is the exit code. Parameters of `main` are the program's
  inputs (`--arg name=value`).
- **Values do not change.** `let` binds once; `var` can be rebound (`x = x + 1`, `p.f = 3`,
  `xs[i] = v` all mean "replace the variable by a new value"). Library functions return new values
  (`xs->append<5>` leaves `xs` alone); the compiler updates in place when nobody else holds the value.
- **Passes are functions.** `camel prog.cml std::macro std::opt tiles::compile` composes stages.

## 3. Syntax essentials

```camel
import { sqrt } from math                      // names from a module; the file is itself a module

type Point = { x: float, y: float }            // structural; also (int, string), int[], f32[2, 3]
type Shape = enum { Circle(float), Rect { w: float, h: float }, Empty }

export func dist(a: Point, b: Point): float {  // `export` makes it importable
    let dx = a.x - b.x                         // let: bound once
    let dy = a.y - b.y
    return sqrt(dx * dx + dy * dy)
}

func area(s: Shape): float {
    return match s {                           // a match needs a default (or every variant)
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Rect { w, h } => w * h
        case Shape::Empty => 0.0
    }
}

func main(): int sync {
    let p: Point = { x: 0.0, y: 0.0 }
    println(`dist ${dist(p, { x: 3.0, y: 4.0 })}`)   // backticks interpolate
    let sign = if p.x > 0.0 then 1 else -1            // if is an expression; no else means no value
    return sign + 1
}
```

- Comments are `//` and `/* */`; doc comments `///`.
- Lambdas always have parentheses: `(x: int): int => x + 1`. Parameter types are inferred where a
  function type is expected: `xs->map<(x) => x * 2>`.
- Tuples `(1, 'a')` with `t.0`; destructuring only for tuples: `let (q, r) = (7 / 2, 7 % 2)`.
  Struct and array destructuring (`let { x } = p`) does not exist: write `p.x`, `xs[0]`.
- `a..b` is the integer interval `[a, b)`. `&&` and `||` are branches (the right side runs only if
  needed). `==` compares contents; on arrays it gives one `bool`, `===` compares element by element.
- Strings are UTF-8 text; length and indices count code points. Binary data is `Bytes`. They convert
  only through `bytes::encode` / `bytes::decode`.
- There is no `break`/`continue`, `class`, `try`/`catch` or `any`.

## 4. Types

**Numbers do not convert implicitly.** `int` is 64-bit, `float` is 64-bit, `i32`, `f32`, `bf16`,
`f16`, `c64`, `c128` exist. `n + 1.5` with `n: int`, `i32` with `int`, `f32` with `float`: all errors.
Convert with the type as a function, and it applies to every element of an array: `float(n)`,
`f32(xs)`. Only **literals** adapt to where they stand (`let x: f32 = 1`, `x * 2` for `x: f32`).

**Arrays are one type, with shapes in it.** `int[]` is a growable list (`int[?]`); `f32[3, 4]`,
`f32[n, 784]` are fixed-rank arrays; `f32[..]` is any rank and `T[..]` any element type. There is
no `Tensor` type: a tensor is a numeric array. Names in a shape bind on first use and are checked
(`x @ w` needs matching inner dimensions). Array arithmetic is element-wise (`xs * 2.0`, `a + b`);
`@` is the matrix product. An array of unknown shape enters a declared shape with `as`.
Indexing: `xs[i]`, `xs[a:b]` (a slice of the first axis), `xs[idx]` with an `int[]`. **No negative
indices**: they are faults. The last element is `last(xs)`.

**Absence and failure are in the type.** `T?` may be absent, `T!` may have failed; they are two
different marks (`int?!` is allowed). A value goes *into* `T?`/`T!` automatically and *out* only
explicitly: `a ?? default` for absence, `a !! default` for failure, `match` for both:

```camel
func port(text: string): int {
    return parse_int(text) !! 80                          // int! -> int
}

func first_even(xs: int[]): int? {
    return xs->find<(x: int): bool => x % 2 == 0>           // may be absent
}

func describe(xs: int[]): string {
    return match first_even(xs) {
        case null => 'none'
        case v => `first even: ${v}`
    }
}
```

Never invent `-1`, `''` or `0` to mean "missing": that is what `T?` is for. In `if x != null` the
`x` is a `T` inside the arm.

**Structs, tuples, enums** are values with structure; the type of a parameter tree (`{ w: f32[..],
b: f32[..] }`) is exactly what `grad` returns a gradient of. Maps and sets are built-in types made
from array literals: `[('a', 1)] as Map<string, int>`; keys are int, bool, string and tuples of them.

**Streams** `T..` are values that produce elements on demand, possibly forever: `for x in s` pulls
one element per round, `yield` in a function whose result is written `T..` produces one, `unfold`
builds one from a state. A stream has one consumer per path (`tee(s)` gives two to share). Only the
source (reading a file, a socket, a model's output) is an effect; `map`/`filter` on a stream are
pure and lazy. `fs::read_lines(path)` is a `string!..`.

## 5. Functions, generics, `with`, pipelines

```camel
import { is_float } from types

with <lo: int, hi: int>                              // configuration, bound before the data
func clamp(x: int): int {
    return if x < lo then lo else if x > hi then hi else x
}

func first::<T>(xs: T[]): T? {                       // ::<T> declares a type parameter
    return if len(xs) == 0 then null else xs[0]
}

func halve::<T>(x: T): T when is_float::<T>() {      // `when`: a compile-time condition on types
    return x / 2
}

func main(): int sync {
    let to_unit = clamp<0, 10>                      // partial application: a function value
    println([-5, 3, 99]->map<to_unit>)              // [0, 3, 10]
    println(first::<int>([4, 5]) ?? -1)
    return 0
}
```

- **Two kinds of parameters.** *Ordinary* parameters are the data that flows through; *`with`*
  parameters are the function's configuration: weights, shapes, flags, hyperparameters, the function
  to call. Think of a functor: `with` is its `this`, bound once; the ordinary parameter is what is
  fed to it each time. `logits<model>(x)` is f_θ(x). `with` goes *before* `func`, `when` *between*
  the result and the body. Both may have defaults and be passed by name: `f<rounds: 16>`.
- **`->` passes exactly one value**, as the first ordinary argument: `x->f` is `f(x)`, `x->f<w>` is
  `f<w>(x)`, `x->f(a, b)` is `f(x, a, b)`. So a model reads as a pipeline:
  `x->linear<w1, b1>->relu->linear<w2, b2>`. The thing being *processed* is the ordinary parameter;
  two equal partners (`merge(m1, m2)`) are both ordinary.
- **`->` carries absence and failure along.** If `x` is a `T?` or `T!` and `f` takes only a `T`,
  `x->f` gives null (or passes the error on unchanged) when there is nothing to call it on, and
  calls `f` otherwise; the result is `U?`/`U!`. Overloads (their `when` conditions included) are
  matched against the wrapped value first, and only when none matches is it taken apart. So a
  pipeline is written for the normal path and settled once at the end with `??`, `!!` or `match`:
  `text->parse_int->clamp<0, 10> !! 0`. There is no `?->`.
- Generic functions are instantiated per concrete type; `is_float::<T>()`, `is_numeric::<T>()`,
  `is_ordered::<T>()` (from `types`) are the usual conditions. Write one definition for all element
  types (`T[..]`), not one per type. Explicit type arguments: `f::<int>(x)`, and
  `f::<T><w>(x)` when both are given. There is no `any`, and no runtime type test.
- Gradients: `grad(f)` and `with_grad(f)` differentiate with respect to the **first `with`
  parameter**: `with_grad(loss)<model>(x, y)` is `(loss, gradient of the model's type)`.
  An optimizer is a pure step `(params, grads, state) => (params, state)` with its hyperparameters
  in `with`: `opt = adam<schedule: constant<rate: 0.001>>`, then `opt(model, grads, state)`.

## 6. Errors and faults

Two different things, never mixed:

- **An error value** is for operations that are *declared* fallible because the world can refuse:
  reading a file, parsing user text, the network, a missing device. It is an ordinary value of type
  `T!` with a kind, a message and a source position. Make one with `error('Kind', 'message')` and
  **`return` it**; there is no `throw`.
- **A fault** is a broken precondition: division by zero, an index out of range, too deep a
  recursion, `fail<'InvalidArgument'>('q > 1')` in your own code. It ends the program with a
  position, unless `attempt` isolates it.

```camel
import { read_text } from fs

func port(path: string): int {
    let text = path->read_text !! '80'                       // string! -> string: the world may refuse
    return parse_int(text) !! 80                             // int! -> int: the text may not be a number
}

func quantile(q: float): float {
    return if q >= 0.0 && q <= 1.0 then q else fail<'InvalidArgument'>('q is outside [0, 1]')   // a fault
}

func run(f: (x: int) => int, x: int): int {
    return attempt(f, x) !! -1                               // a fault inside f becomes an error value
}
```

The kinds come from the catalog (`NotFound`, `PermissionDenied`, `IOError`, `EncodingError`,
`ParseError`, ...; `docs/stdlib-conventions.cn.md` 5.1): use the kind that says what happened, not
a new name. Division by zero is a fault, never an error value.

Rules: an operation that can fail from the environment returns `T!`, and "may be absent" returns
`T?`; never both for one failure. An unhandled error reaching `main` is printed with its position
and exits non-zero. A `T!` that is evaluated and never used is a warning; write `let _ = f()` to
drop it on purpose. `return` gives the value of the block it is in and never leaves an outer block; write an early
error as the two arms of a branch (`return match m[k] { case null => error('NotFound', k) case v => v }`)
or let `->` carry the failure to a final `??`/`!!`. `fail` and `exit` have the type `never`.

## 7. Effects and resources

An effect is an access to a **resource**: the console, `fs`, `clock`, `env`, a `var`, a handle
(a stream, a device). Accesses to the *same* resource are ordered as written; accesses to different
resources are independent unless you write `sync`. The compiler **infers** the resources of every
function. A hand-written clause is a checked promise, for public contracts:

```camel
func log_line(s: string): string uses stdout {       // may touch only stdout
    return println(s)
}

func square(x: int): int pure {                      // touches nothing: the compiler checks it
    return x * x
}
```

A function-typed parameter may use any resources; the caller then uses them too. Do not write
`uses` on ordinary code; do not write comments like `@pure` (the checker is the source of truth).
Bodiless declarations are native operators (I/O, kernels): that is the only place the standard
library is not Camel, and each carries its `uses`/`pure`.

## 8. Loops

`loop` is an expression with state: the states are declared once, the body's value is the next
state, the loop's value is the final state. There is no `break`; stop with a `while` condition.

```camel
func main(): int sync {
    let sum_sq = loop acc = 0 for x in [1, 2, 3] do acc + x * x                   // 14
    let (i, fib) = loop i = 0, fib = 1 while fib < 100 do (i + 1, fib + fib)    // two states
    let first_big = loop hit = -1 for x in [3, 9, 4, 12] while hit < 0 do
        if x > 8 then x else hit                                                 // stops early
    println(sum_sq)
    return 0
}
```

`for` walks arrays, strings (by code point), maps (by key), numeric arrays (along the first axis) and
streams. To collect per-round values, carry an array in the state (`xs->append<v>`). Prefer
`map`/`filter`/`reduce` and `a..b` pipelines when you do not need a state.

## 9. The standard library map

Everything is imported by name: `import { read_text } from fs`, or used qualified (`fs::read_text`).
The prelude (no import): `print`, `println`, `eprintln`, `input`, `len`, `map`, `filter`, `reduce`,
`foreach`, `range`, `string`, the numeric conversions, `error`, `attempt`, `fail`. Data comes first
and configuration in angle brackets: `s->split<','>`, `xs->take<3>`, `xs->sort_by<key>`.

| Module | What it is for |
|---|---|
| `array`, `str`, `bytes`, `re` | arrays (sort, find, `sum`/`mean`/`max` and their `<axis>` forms), text, binary data, regular expressions |
| `math`, `statistics`, `random` | scalar and element-wise math; statistics; keyed random numbers (`key(seed)`, `fold`, `uniform`, `normal`: pure, reproducible) |
| `tensor` | creation, `reshape<[..]>`, joins, `@`, element-wise operators between arrays |
| `nn`, `optim`, `autodiff`, `ckpt` | layers (`conv2d`, `linear<w, b>`, `layer_norm`, `softmax`, ...), optimizers and schedules, `grad`/`with_grad`/`stop_gradient`, `save_tree`/`load_tree` |
| `json`, `fs`, `io`, `env`, `process`, `time`, `hash` | the world: `Json` values and `decode_as::<T>`, files (text and bytes are separate: `read_text`, `read_bytes`), terminal keys, environment, `sleep`/`exit`, `time::now` and `timed`, `sha256` |
| `onnx`, `tiles`, `python` | export to ONNX, CUDA placement (`#gpu` tags, `tiles::compile`), calling Python (arrays cross by DLPack) |
| `graph`, `rewrite`, `rules`, `opt`, `tree` | looking at and rewriting graphs, rules, parameter trees (`tree_map`, `tree_paths`) |

Failure is by `T!`: `read_text(path): string!`, `parse_int(text): int!`, `decode(text): Json!`.
Time is UTC seconds as a `float`. Writers take the path in angle brackets: `text->write_text<path>`.

## 10. The mistakes an AI makes most

1. **Implicit numeric conversion.** `n * 0.5`, `xs[i] + 1.0` with an `int` in sight, `f32` with `float`.
   Convert explicitly: `float(n) * 0.5`, `f32(xs)`.
2. **`println` without `sync`.** Outside a `sync` block an unused effect does not run. Put effects in
   `sync` blocks, one block per thing that must happen in order.
3. **`try`, `catch`, `throw`, `raise`.** None exist. Return `error(...)` for failures the world causes,
   `fail<'Kind'>(...)` for a broken precondition, settle with `!!`/`match`, isolate with `attempt`.
4. **Negative indices and out-of-range slices.** `xs[-1]` is a fault; use `last(xs)`. A slice end past
   the length is a fault too (`take`/`drop` truncate).
5. **Writing `Tensor` or calling tensor methods.** There is no such type; write `f32[n, m]` or
   `T[..]`. Operations are functions with configuration in angle brackets: `x->reshape<[2, 3]>`,
   `x->sum<1>`.
6. **Positional configuration.** `conv2d(x, k, b, 1, 0)`, `write_text(path, text)`, `clamp(x, 0, 10)`.
   Weights, shapes, paths and flags that are fixed before the data arrives go in `with`:
   `x->conv2d<k, b, stride: 1, padding: 0>`, `text->write_text<path>`, `x->clamp<0, 10>`.
7. **Sentinel values.** Returning `-1`, `''`, `0.0` or `null` for "not found". Return `T?`; for a
   failure return `T!`.
8. **Index loops and `break`.** `for i in 0..n { ... }` is a syntax error (`for` is part of `loop`),
   and `break`/`continue` do not exist. Use `loop acc = ... for ... while ... do ...` or
   `map`/`filter`/`reduce`.
9. **Discarding a pure result.** `xs->append<5>` as a statement is an error: the result is a new
   array. Write `xs = xs->append<5>` (for a `var`) or bind it.
10. **`?->`, `x => e`, `let { x } = p`.** `?->` is gone (`->` propagates `T?`/`T!`); a lambda is
    `(x) => e`; destructure only tuples.
11. **A `match` without a default.** Add `case _ =>` (not needed for `bool`, a fully listed `enum`,
    or the two sides of `T?`/`T!`).
12. **Comparing what cannot be compared.** Functions cannot be compared; `T?` and `T!` cannot be
    compared before they are opened (`x == null` is fine); `==` on arrays is one `bool`.
13. **Mutating through a library call.** Nothing mutates its argument; only a `var` parameter can be
    updated.
14. **`with` and `when` in the wrong place.** `with <...>` before `func`; `when cond` after the
    result type and before the body; `::<T>` after the name, `<w>` for `with` arguments.
15. **Inventing names.** Search the stdlib reference before writing a helper: sorting, `sum`, `mean`,
    `softmax`, `one_hot`, `tree_map`, `timed`, `sha256` already exist, and one concept has one name.

## 11. Where to look next

- Rules and their reasons: `docs/design-principles.cn.md` (normative: when anything disagrees, it
  wins); the language reference `docs/language/`; the tutorial `docs/tutorial/`.
- Every standard library name and signature: `docs/stdlib-reference.md`
  (`camel-for-ai-stdlib-signatures.md`); every diagnostic: `camel-for-ai-diagnostics.md`.
- See the graph the compiler made: `camel prog.cml std::gir`.
