# Camel for AI

The page an AI should read before writing Camel: why the language is what it is, the model that makes
its rules predictable, the idioms, how to read what the compiler answers, and where to look the rest
up. It is deliberately not a rule book: a language is learned by understanding why, by examples and
by listening to the compiler.

Read it as the language of this tree. It is under 20,000 characters, and
`node scripts/camel-for-ai.mjs --check` regenerates the two generated tables from their sources and
checks every diagnostic named here against the registry, while `node scripts/check-doc-snippets.mjs`
compiles every `camel` block. A block marked `camel-snippet: counter-example` is wrong on purpose and
says what the compiler answers.

## Why Camel

Camel is a graph-native general-purpose programming language, and AI is its primary application.

Most languages write a program as instructions for a machine to carry out one step at a time. Yet
what compilers, parallel runtimes, automatic differentiation and GPU back ends actually need is a
graph: what is to be computed, and what depends on what. So the graph has to be recovered from the
code after the fact, by tracing, capturing and analysis. The recovered graph breaks apart, loses what
the side effects meant, and the transformations applied to it become a black box the user cannot
touch.

In Camel **the program is the graph**. What you write is the intent of the computation and its
dependencies; order is written only where you need it. From that:

- **The compiler gets exactly what you wrote**: nothing is traced, the graph does not break, and
  computations that do not depend on each other are free to run in parallel.
- **Transformations are ordinary functions**: optimizing, differentiating, moving to a GPU and
  exporting are passes over the graph. You can look at them, swap them, and write your own in Camel.
- **Knowledge about optimization is code you can check**: an algebraic law is a rule, and `check`
  tells you whether it holds.
- **One program, many ways to run it**: interpreted, JIT-compiled, on a GPU, or exported to ONNX or
  C++, with the same meaning.

These properties hold for every program where computation matters more than control flow. Training
and deploying models is the most pressing of them, which is why AI is Camel's primary application,
though not its only one.

Copied whole from `docs/why.en.md`, which the README, the tutorial and the language reference also
open with.

## The mental model

Four facts decide almost every rule here, and each is stated with what it is for.

### A program is a graph, not a sequence

An expression is a node, a value flows along an edge, and a node runs when its result is needed: when
it reaches the return of its function along a data or a control edge. Nothing else decides order, so
independent expressions have no order between them and the parallel scheduler may run them side by
side. A function is a subgraph of its own, so a call is one node pointing at it and there is no call
stack except for recursion depth.

*Why:* the compiler must have the graph, not recover it — because the program **is** the graph,
nothing is traced and the passes have a real object to transform.

### Evaluation is by dependency, and nothing else runs

`let` binds a value and `var` allows rebinding; an expression whose result nothing needs is not
computed, so a whole subgraph can disappear. Values do not change: the standard library never mutates
its argument, it returns a new value, and in-place update is an optimization the compiler applies
when a variable is the only holder of the value, so only speed observes it.

*Why:* "nothing runs that nothing needs" is the freedom the compiler uses to delete, move and
parallelize.

### Effects and order

An effect is a node whose point is that it happened. A **sync block** orders the statements written
*directly* in it and only those: `sync { ... }`, `func f(): T sync { ... }`, `if c then sync { ... }`.
Outside one, an effect whose result nothing uses is dropped, with the warning
`SemanticDiag::EffectNotExecuted`. The rule does not reach inward — an arm or a loop body written
inside a sync block is an ordinary subgraph and needs its own `sync`. A `var` parameter is a mutable
reference to the caller's variable, so a write in the callee is a write the caller sees, ordered
against everything that reads or writes that variable.

*Why:* order is written only where it is needed. If ordering propagated inward, every inner
expression would be pinned and the scheduler would lose the freedom it exists for.

### Types

Every value has one concrete type, and numeric types never convert implicitly: `i32` with `int`, or
`int` with `float`, is `SemanticDiag::NoMatchingFunction` in an expression and
`SemanticDiag::DeclaredTypeMismatch` in a typed `let`. Only a **literal** takes the type its context
asks for (`let x: f32 = 1`); a variable never does. Convert explicitly with `float(n)`, `int(x)`,
`string(v)`.

`T?` and `T!` are types of their own and not `T`: open them (`??`, `!!`, `?->`, `match`) before
arithmetic, comparison or a function that takes a `T` will accept them. Generics are declared after
the name, `func first::<T>(xs: T[]): T?`, and `when <condition>` is a compile-time predicate over the
type parameters, which is how one generic function gets one body per type. A `match` over an `enum`
is checked for exhaustiveness, so listing every variant means no `case _` is needed. An empty literal
takes its element type from its position too, so `let xs = []` has nothing to take it from. A stream,
`T..`, says what does not fit in memory; its form is the language reference's.

*Why:* the passes act on types and the graph holds only concrete types, so a conversion the
programmer did not write would be a node nobody asked for. Absence and failure are part of the type
so that "what can go wrong here" is in the signature rather than in a sentinel value.

Sources: `docs/language/en/04_execution_model.md`, `docs/language/en/03_data_model.md`,
`docs/design-principles.cn.md` §§2–4, 11.3.

## Idioms

Written by someone who knows Camel, a program looks like this.

### Data flows through the pipe; configuration sits in the angle brackets

`x->f` is `f(x)` and `x->f(a, b)` is `f(x, a, b)`, so a computation reads top to bottom. `f<w>(x)` is
a call with its configuration, and `f<w>` alone is the configured function as a value.

```camel
func inc(x: int): int {
    return x + 1
}

func main(): int sync {
    let total = 1..6->map<inc>->reduce<(a: int, b: int): int => a + b, 0>
    println(total)
    return 0
}
```

```text
20
```

The rule has a reason: `map` may give `inc` its elements in any order, so `inc` must depend on the
element alone. Whatever the element does not decide is configuration, written in `with` — which is
what makes a model a configured function.

### Ranges and higher-order operators instead of index loops

`1..n` is an `int[]`; `map`, `filter`, `reduce` and `foreach` say what happens to each element. When
a loop really is a loop, write the language's own form and let it carry the state.

```camel
func sum(xs: int[]): int {
    return loop acc = 0 for x in xs do acc + x
}

func main(): int sync {
    println(sum(1..5), [1, 2, 3]->reduce<(a: int, b: int): int => a + b, 0>)
    return 0
}
```

```text
10 6
```

`loop` states are written `name = start`; `for` and `while` may each be left out, and the body's
value is the next state. The higher-order operators may take their elements in any order where order
is not needed; `loop` is what carries a value from one round to the next.

### A model is a configured function

Weights are `with` parameters, and `grad` reads the first of them.

```camel
import { grad } from autodiff

with <x: float>
func f(y: float): float {
    return x * x * y + y
}

func main(): int sync {
    println(f<3.0>(2.0), grad(f)<3.0>(2.0))
    return 0
}
```

```text
20.0 12.0
```

A parameter that is configuration rather than data is what a weight is, and it is the parameter `grad`
differentiates with respect to: a function with none is `SemanticDiag::NotDifferentiable`.

### Let a value that may be missing say so

A function that can miss returns `T?` and one that can fail returns `T!`, and both are opened where
the decision is made.

```camel
func half(n: int): int? {
    return if n % 2 == 0 then n / 2 else null
}

func main(): int sync {
    println(half(10) ?? -1, half(7) ?? -1)
    return 0
}
```

```text
5 -1
```

`??` is a node in the graph, so what happens when the value is absent is written where the absence
can be seen; a sentinel like `-1` travels on and is checked nowhere.

### `match` for a value that is one of several things

```camel
type Shape = enum { Circle(float), Rect { w: float, h: float } }

func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Rect { w, h } => w * h
    }
}

func main(): int sync {
    println(area(Shape::Rect { w: 2.0, h: 3.0 }))
    return 0
}
```

```text
6.0
```

The compiler checks that every variant is handled, so a variant added later cannot be forgotten.

Sources: `docs/language/en/06_expressions.md`, `docs/language/en/07_statements.md`,
`docs/design-principles.cn.md` §11.

## How to use what the compiler answers

Its answer is half of learning the language.

- **A diagnostic is named in full.** The compiler prints `warning[SemanticDiag::EffectNotExecuted]`,
  and [Appendix A](camel-for-ai-diagnostics.md) holds every diagnostic with its message template and
  its code. `camel --explain <name or code>` prints that entry on the spot; the section prefix may be
  left out, and case, `-` and `_` do not matter.
- **A known fix is printed with the diagnostic**: as `-`/`+` lines in the text form, as `fixes` with
  a `replacement` in JSON, and as a code action in the language server. A code that carries no fix
  says why in the registry's `edit` column, so "no fix" is a statement about the diagnostic.
- **A warning is the compiler telling you what the graph does.** `SemanticDiag::EffectNotExecuted`
  and `SemanticDiag::UnusedFallibleValue` are usually right, and the answer is a `sync` or a use
  rather than `-Wno-`; `-Werror` makes every warning an error for a run that must be clean.
- **The editor has the same information.** `camel-ls --stdio` is a language server — diagnostics with
  their fixes, completion, hover, and the graph of a function — and `editors/vscode` wraps it.
- **`camel check prog.cml`** reports the same diagnostics without running the program, and
  `camel prog.cml std::gir` prints the graph instead of running it.

Sources: `docs/cli.md`, `docs/editor-support.en.md`, `docs/diagnostics.md`.

## Common errors

The first drafts that say something about the language. Each is compiled by
`node scripts/camel-for-ai.mjs --verify-errors`, which checks that the diagnostic named is the one
the compiler reports.

### An effect nothing needs does not run

`println` is an effect, and an effect whose result nothing uses is dropped: the program compiles,
prints nothing from that line, and warns `SemanticDiag::EffectNotExecuted`. **Wrong:**

<!-- camel-snippet: counter-example: the effect is dropped because nothing needs its result -->

```camel
func greet(): int {
    println('hello')
    return 1
}
```

Nothing in that graph needs the `println`; making it run would mean some unused nodes are special.
Say that it must happen: a `sync` body orders the calls written in it.

```camel
func greet(): int sync {
    println('hello')
    return 1
}

func main(): int sync {
    greet()
    return 0
}
```

```text
hello
```

### `match` over an `enum` must handle every variant

`SemanticDiag::EnumMatchNotExhaustive` names the variants no `case` handles. **Wrong:**

<!-- camel-snippet: counter-example: the Empty variant is not handled -->

```camel
type Shape = enum { Circle(float), Empty }

func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
    }
}
```

Exhaustiveness is the reason to prefer an `enum` to a flag: a variant added later breaks the build
here instead of falling through at run time. **Write instead** — add the arm, or `case _ => 0.0` when
a default really is what is meant.

```camel
type Shape = enum { Circle(float), Empty }

func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Empty => 0.0
    }
}
```

### Two numeric types never mix

No value converts implicitly, so `i32` and `int` in one expression are
`SemanticDiag::NoMatchingFunction`. **Wrong:**

<!-- camel-snippet: counter-example: i32 and int are different types and do not mix -->

```camel
func add(a: i32, b: int): int {
    return a + b
}
```

A conversion would be a node in the graph that the programmer did not write, and the passes act on
types, so the conversion has to be written. **Write instead** — convert explicitly:

```camel
func add(a: i32, b: int): int {
    return int(a) + b
}
```

### `as` is not a conversion

`as` restricts a value to a type it already has; it never converts, so `s as int` on a string is
`SemanticDiag::DynamicCastForbidden`. **Wrong:**

<!-- camel-snippet: counter-example: as does no numeric conversion -->

```camel
func n(s: string): int {
    return s as int
}
```

Conversion is an operation with its own nodes and its own rules, and a cast is not one; if `as`
converted, a value could change its meaning without a pass being able to see it. **Write instead** —
`int(s)`, or `parse_int(s)` when the text may be junk and a `T!` is wanted.

Sources: `diagnostics/registry.tsv`, and the language reference for each rule broken here.

## Where to look

- **Appendix A — every diagnostic**: [camel-for-ai-diagnostics.md](camel-for-ai-diagnostics.md)
  gives each one's section-qualified name, code, message template and whether it carries a fix. A
  document of its own, because the registry grows and must not push this page past one read.
- **Appendix B — the standard library**: [camel-for-ai-stdlib-signatures.md](camel-for-ai-stdlib-signatures.md)
  gives every exported name with its signature, for the same reason.
- **The language reference**, `docs/language/en/`: the same rules in depth, by topic. This page
  distils it; where the two disagree, the reference is right.
- **The standard library reference**, `docs/stdlib-reference.md`: those signatures with the prose
  around them, generated from the declarations.
- **The design principles**, `docs/design-principles.cn.md`: normative — when it disagrees with
  anything, including this page, it wins — and it lists what is not decided yet.
- **The command line** `docs/cli.md`, and the editor `docs/editor-support.en.md`.

## The two generated tables

These two come from the sources the compiler itself reads, so they cannot drift from it.

### Keywords and operators (generated)

<!-- camel-for-ai region: lexical (generated from docs/language/02_lexical.md §2.4 and include/camel/parse/syntax/token_kinds.def; node scripts/camel-for-ai.mjs --write) -->
| Group | Words |
|---|---|
| Declaration | `let` `var` `func` `type` `with` `import` `export` `from` `namespace` `using` |
| Control | `if` `then` `else` `match` `case` `return` `throw` `yield` `sync` `loop` `while` `do` `for` `in` `static` |
| Type and conversion | `as` `const` `constas` `enum` `typeof` |
| Literal | `null` `true` `false` |

**Operators and separators** (54): `;` `,` `:` `::` `.` `..` `...` `(` `)` `[` `]` `{` `}` `<` `<` `>` `<=` `<=>` `>=` `==` `!=` `===` `!==` `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `|=` `+` `-` `*` `/` `%` `^` `@` `#` `&` `|` `&&` `||` `!` `!!` `~` `?` `??` `->` `?->` `=>` `_`
<!-- camel-for-ai end: lexical -->

### Precedence (generated)

<!-- camel-for-ai region: precedence (generated from src/parse/syntax/parser_expr.cpp; node scripts/camel-for-ai.mjs --write) -->
```text
 1  dataExpr            -                                  -
 2  ctrlExpr            if then else                       construct
 3  assignExpr          = += -= *= /= %= ^= @= &= |=       one
 4  logicalOrExpr       ||                                 repeat, left
 5  logicalAndExpr      &&                                 repeat, left
 6  orExpr              |                                  repeat, left
 7  andExpr             &                                  repeat, left
 8  equalityExpr        === !== == !=                      repeat, left
 9  relationalExpr      < > <= >=                          repeat, left
10  additiveExpr        + -                                repeat, left
11  multiplicativeExpr  * / @ %                            repeat, left
12  powerExpr           ^                                  right, one
13  nullableExpr        ?? !!                              right (a whole expression on the right)
14  unaryExpr           as                                 one
15  linkExpr            -> ?->                             repeat, left
16  negExpr             - ! ~                              prefix
17  negPower            - ! ~ ^                            prefix, right
18  compExpr            ..                                 repeat, left
19  annoExpr            -                                  repeat, left
```

The production of each level, as `src/parse/syntax/parser_expr.cpp` writes it above the
function that parses it:

```text
dataExpr           : ctrlExpr | loopExpr
ctrlExpr           : 'if' logicalOrExpr 'then' blockExpr ('else' blockExpr)?
assignExpr         : logicalOrExpr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOrExpr)?
logicalOrExpr      : logicalAndExpr ('||' logicalAndExpr)*
logicalAndExpr     : orExpr ('&&' orExpr)*
orExpr             : andExpr ('|' andExpr)*
andExpr            : equalityExpr ('&' equalityExpr)*
equalityExpr       : relationalExpr (('===' | '!==' | '==' | '!=') relationalExpr)*
relationalExpr     : additiveExpr (('<' | '>' | '<=' | '>=') additiveExpr)*
additiveExpr       : multiplicativeExpr (('+' | '-') multiplicativeExpr)*
multiplicativeExpr : powerExpr (('*' | '/' | '@' | '%') powerExpr)*
powerExpr          : nullableExpr ('^' powerExpr)?
nullableExpr       : unaryExpr (('??' | '!!') dataExpr)?
unaryExpr          : linkExpr ('as' typeExpr)?
linkExpr           : negExpr (('->' | '?->') compExpr)*
negExpr            : ('-' | '!' | '~') negPower | compExpr
negPower           : ('-' | '!' | '~') negPower | compExpr ('^' negPower)?
compExpr           : annoExpr ('..' annoExpr)*
annoExpr           : accessExpr (indices | parentArgues | structData | angledValues | typeArguments | memberAccess)*
```
<!-- camel-for-ai end: precedence -->