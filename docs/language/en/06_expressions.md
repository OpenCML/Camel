# 6. Expressions

English | [中文](../06_expressions.md)

Almost everything in Camel is an expression: `if`, `match`, blocks and assignments all have values. This chapter covers them kind by kind.

---

## 6.1 Evaluation

An expression is a node on the graph, and its operands are its input edges. Whether and when an expression is evaluated is decided by dependencies ([4.2](04_execution_model.md#42-evaluation-by-dependency)); operator precedence and associativity decide only the shape of the graph ([6.12](#612-operator-precedence)). `&&`, `||`, `??`, `!!`, `?->` and `if`, `match` are branches: the side that is not selected is not evaluated.

## 6.2 Literals and construction

```cml
42          3.14        'text'      true        null
[1, 2, 3]                   // array
(1, 'one')                  // tuple; a one-element tuple is written (x,)
{ x: 1, y: 2 }              // struct
(x: int): int => x + 1      // lambda, see 6.9
```

- The elements of an array literal must have the same type. The element type of the empty array `[]` comes from the context: `let xs: int[] = []`, `[] as Map<string, int>`.
- The keys of a struct literal are identifiers.
- **A list comprehension** `[e for x in xs if c]` is `xs->filter<(x) => c>->map<(x) => e>`: `x` has the element type of `xs`, `if c` may be left out, and the result is an array of `e`. On the graph it is exactly what the two calls written out are (two operator nodes, each with the subgraph of its element function), so it has their properties: nothing is promised about the order of the elements (a parallel scheduler may compute them concurrently), and effects in `e` and `c` are treated as in the element functions of `map` and `filter` ([6.10](#610-higher-order-operators)). It nests, and it may stand wherever an expression may. A source that is not an array is `SemanticDiag::ComprehensionSourceRefused`.
- The spread `...xs` is reserved and not implemented (`SemanticDiag::ConstructNotSupported`). Write `concat(xs, ys)`.

## 6.3 Access and subscripts

| Form | Meaning |
|---|---|
| `p.x` | Struct field. A field that does not exist is the compile-time error `SemanticDiag::InvalidAccessIndex` |
| `t.0` | Tuple member; the index is a literal, and out of range is a compile-time error |
| `xs[i]` | Array element. Out of range (including negative) is the fault `RuntimeDiag::IndexOutOfRange` |
| `m[k]` | Map lookup; the type is `V?` ([3.6](03_data_model.md#36-maps-and-sets)) |
| `mod::name` | Qualified name |

- `.` cannot be used on an array (`xs.1` is `SemanticDiag::TypeNotIndexable`).
- A subscript directly follows the expression being accessed, with no whitespace.
- The slice forms `xs[a:b]` and `xs[a:b:c]` are reserved, not implemented: there is no dedicated diagnostic for them, and the compiler reports `SemanticDiag::NoMatchingFunction`, as "the subscript operator has no overload that accepts these arguments". Use `xs->slice<a, b>`.
- **String subscripts are by code point**: `s[i]` is the `i`-th code point, and the result is a string containing just that one character (`'中文'[0]` is `'中'`); an out-of-range subscript is likewise a fault. For the numbers use `codepoints` of the `str` module (see the [standard library reference](../../stdlib-reference.md)).

## 6.4 Calls

```cml
with <factor: int>
func scale(x: int): int { return x * factor }

func main(): int sync {
    println(scale<3>(5))        // with arguments go in angle brackets
    println(5->scale<3>)        // pipe: the value on the left becomes an argument
    return 0
}
```

```text
15
15
```

- Ordinary arguments are written in parentheses, `with` arguments in angle brackets; both must directly follow the function name. When the function has no `with` parameters, no angle brackets are written.
- Arguments correspond to parameters by position, and the types must match: numbers are not converted implicitly; only a value that enters a `T?` or `T!` parameter is put into the form by itself ([3.4](03_data_model.md)). A mismatch is `SemanticDiag::NoMatchingFunction` or `SemanticDiag::ArgumentsMismatch`, and the diagnostic lists the available signatures.
- When there are several functions of the same name, one is selected by the overloading rules ([7.4.3](07_statements.md#743-overloading)).
- **`f<w>` is a partial application.** Without parentheses, `f<w>` is a value: a function that already has its context arguments. After `let g = inc<1>`, `g(2)` is `inc<1>(2)`; `w` is evaluated once, where `f<w>` is written. It can be bound to a name, passed, returned, and handed to a higher-order operator directly (`xs->map<inc<10>>`).
  - When only the first context parameters are given, the rest remain context parameters of the result: after `let above = clamp<0>`, write `above<5>(9)`. A function that has all of them cannot be given more (`g<2>` is an error).
  - `f` must be one function that these arguments determine. Among several functions of one name, the only one that takes these context arguments is meant; if several do, the function type of the position (a parameter) decides, and when nothing does it is `SemanticDiag::PartialApplicationRefused`.
  - On the graph it is a function value that carries the captured arguments, the shape a closure has: a subgraph of its own which the bound arguments enter through ports. No node kind is added.
  - Not supported yet: functions with a `var` context parameter (`SemanticDiag::ConstructNotSupported`) and built-in operators whose type rule depends on their arguments (such as `len`; `SemanticDiag::FunctionNameAsValue`). Write the call in a lambda there.
- Keyword arguments `f(a: 1)` are reserved, not implemented (`SemanticDiag::FeatureNotSupported`).
- Two calls with angle brackets in one argument list (`f<a>(x), g<b>(y)`) are read as comparisons; a call with angle brackets followed by a pipe with angle brackets (`f<a>(x)->g<b>`) may be too. Write them as two statements.
- A lambda written in parentheses can be called directly: `((x: int): int => x + 1)(3)`; it can also be written on the right of `->` and `?->`: `3->(x: int): int => x + 1`, `parse_int(s) ?-> (x: int): int => x * 2`.

## 6.5 Pipes

`a->f` makes `a` an argument of `f`: `a->f` is `f(a)`, and `a->f<w>` is `f<w>(a)`. It is left-associative and suits writing a data flow as one chain:

```cml
func main(): int sync {
    let total = range(1, 6)
        ->map<(x: int): int => x * x>
        ->filter<(x: int): bool => x % 2 == 1>
        ->reduce<(acc: int, x: int): int => acc + x, 0>
    println('total = {}'->format<total>)
    return 0
}
```

```text
total = 35
```

- For `x ?-> f` see [6.6](#66-operators-for-null-and-errors).
- `f..g` (function composition) and `?..` are reserved: they are not implemented and their meaning is not decided, and writing one is `SemanticDiag::ConstructNotSupported`. Use a lambda: `(x: int): int => g(f(x))`.

## 6.6 Operators for null and errors

For `T?` and `T!` see [3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t). On the graph each of the three operators below is a branch whose condition is a one-bit tag on the value.

| Form | Left side | Right side | Result |
|---|---|---|---|
| `a ?? b` | `T?` | `T` | `b` when `a` is null, otherwise the value inside `a` |
| `a !! b` | `T!` | `T` | `b` when `a` is an error, otherwise the value inside `a` |
| `x ?-> f` | `T!` | A function that accepts `T` | When `x` is an error the error is passed on unchanged, otherwise `f` is called; the result is `U!` (`f` may return either `U` or `U!`) |

```cml
func half(x: int): int! {
    return if x % 2 == 0 then x / 2 else { throw error('Odd', 'not even') }
}

func main(): int sync {
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(parse_int('12') ?-> half ?-> half !! -1)
    println(parse_int('10') ?-> half ?-> half !! -1)
    return 0
}
```

```text
12 0
3
-1
```

- The right side is evaluated only when it is selected.
- **The right-hand side may be nullable.** In `a ?? b`, when `b` is a `T` the result is a `T`; when `b` is a `T?` the result is a `T?`. So `a ?? b ?? 9` tries `a`, then `b`, then gives the default, and the result is the same however it is parenthesized. Likewise `!!` (when `b` is a `T!` the result is a `T!`). On the graph each `??` is still one branch. A right-hand side `[]` takes the element type of the left-hand side.
- The right side of `??` and `!!` extends to the end of the expression: `x ?? 0 + 1` is `x ?? (0 + 1)`. To take the value first and then compute, add parentheses: `(x ?? 0) + 1`.
- A left side that is not of the required form (`??` on a plain `int`, `!!` or `?->` on a `T?`), or a right side of the wrong type, is `SemanticDiag::TaggedOperandMismatch`.

## 6.7 Other operators

The operators of the following sections (except `&&`, `||` and `as`) are calls of functions: `a + b` is `__add__(a, b)`, and the tables list the built-in overloads. A program can add overloads for its own types (`func operator +`, [7.4.5](07_statements.md#745-operator-overloading)); they form one set with the built-in overloads and those of C++ modules, and the types of the operands choose among them.

### 6.7.1 Arithmetic

| Operator | Types | Notes |
|---|---|---|
| `+` | The four numeric types, `string` | Concatenation for strings |
| `-` `*` `/` | The four numeric types | Integer division truncates towards zero; a zero divisor is a fault |
| `%` | `int`, `i32` | The remainder has the sign of the dividend; a zero divisor is a fault |
| `^` | The four numeric types | Power |
| `@` | Defined by modules | `tensor` module: matrix multiplication |
| Unary `-` | The four numeric types | |
| `~` | `int`, `i32` | Bitwise not: `~5` is `-6` |

The two operands must have the same type. There are no bitwise and, or, xor or shift operators; use `bit_and`, `bit_or`, `bit_xor`, `bit_not`, `shl` and `shr` of the `math` module.

**Two places where mistakes are easy:**

- `^` binds one level tighter than `*` and `/` and associates from right to left: `2 * 3 ^ 2` is `18`, and `2 ^ 3 ^ 2` is `2 ^ (3 ^ 2)`, that is `512`.
- Unary `-` binds less tightly than `^` and more tightly than `->` and the other binary operators, before a literal as before a name: `-n ^ 2` is `-(n ^ 2)`, `-2 ^ 2` is `-4` (for `4` write `(-2) ^ 2`); `-n->f` is `(-n)->f`; `-a * b` is `(-a) * b`. The right operand of `^` may carry a sign: `2.0 ^ -1.0`. `!` and `~` are at the level of the unary `-` (design principles 4.2): `!x->f` is `(!x)->f` and `!a ^ b` is `!(a ^ b)`; to negate the result of a chain write `!(xs->empty)`.

### 6.7.2 Logic

`a && b`, `a || b`, `!a`; the operands are `bool`.

**`&&` and `||` are branches, not operators.** `a && b` is `if a then b else false`, and `a || b` is `if a then true else b`: the right side is an arm, evaluated only when the left side does not decide. Its effects do not happen and its faults are not reached otherwise (`n != 0 && 10 / n > 1` is safe). On the graph each is the `BRCH`/`JOIN` pair of that `if`, with the right side as a subgraph of its own. Both are reserved and cannot be overloaded; an operand that is not a `bool` is `SemanticDiag::LogicalOperandNotBool`. Because `a && b` is that `if`, a test against null on the left narrows the right side ([6.8.1](#681-if)): `x != null && x > 0`.

```cml
func noisy(tag: string, v: bool): bool sync {
    println(tag)
    return v
}

func main(): int sync {
    println(noisy('l1', false) && noisy('r1', true))
    println(noisy('l2', false) & noisy('r2', true))
    return 0
}
```

```text
l1
false
l2
r2
false
```

**`a & b` and `a | b` evaluate both sides**: ordinary binary operators (a function each, like the others), which can be overloaded. Use them where a little logic needs no branch; element-wise logic on tensors and the operators of a type of your own are this pair as well. Precedence: `&` above `|`, both above `&&` and below the comparisons, so `a == b & c == d` is `(a == b) & (c == d)`.

### 6.7.3 Comparison

- `==`, `!=`: numbers, `bool`, `string`, `Map`, `Set`, and arrays, tuples and structs, with the same type on both sides (anything else is the compile-time error `SemanticDiag::NoMatchingFunction`; nothing is converted).
  - **Arrays, tuples and structs compare by content**, element by element and recursively: `[1, 2] == [1, 2]`, `(1, 'a') == (1, 'a')` and two structs whose fields are equal are all `true`; arrays of different lengths are not equal.
  - The element type must itself be comparable: numbers, `bool`, `string`, `Map`, `Set`, `T?`, `T!`, and arrays, tuples and structs of those. A value with a function in it (`[f] == [f]`) cannot be compared, which is a located compile-time error; tensors are not covered.
  - Float elements follow the rules of floats: an array that holds a `nan` does not equal itself, and `[0.0] == [-0.0]`.
  - A `T?` or `T!` by itself (not inside an array, tuple or struct) still has no `==`; open it first with `??`, `!!` or `match`.
- **`x == null` and `x != null`** are defined for an `x` of type `T?`, in either order, and give a `bool`: the test of the tag of `x`, not a comparison of two values (the same node `??` branches on). It is an ordinary expression: `let has = x != null`. A nullable function value is compared with `null` the same way. A value that is never null (an `int`, a `T!`, a name already narrowed to a `T`) may be compared with `null` too: the result is fixed (`==` is always `false`, `!=` always `true`), the compiler warns (`SemanticDiag::NullTestNeverNull`), and the value itself is not evaluated. Two `T?` values compared with each other stay `SemanticDiag::NoMatchingFunction`; two function values cannot be compared.
- `<`, `>`, `<=`, `>=`: numbers and `string` (in code point order). Comparisons cannot be chained: `1 < 2 < 3` is a type error.
- `===` and `!==` are not supported for now: the lexer knows the two tokens, and writing one is a located compile-time error, `SemanticDiag::ConstructNotSupported`. Compare values with `==` and `!=`.

### 6.7.4 Type conversion `as`

`e as T` is used for:

- entering the nullable or fallible form: `5 as int?`, `null as int?`, `v as int!` (not needed where the target type is known, see [3.4](03_data_model.md));
- constructing maps and sets: `[...] as Map<K, V>`, `[...] as Set<T>`;
- checked conversion of module types: `t as Tensor<f32, const [3]>`, where the shape is checked at the conversion.

`as` never changes the type of a value unchecked. The result of `json::decode` is a `Json`, which does not convert to another type with `as`; JSON of a known shape is decoded with `json::decode_as(s, [0])`, which checks the shape while decoding and gives a `T!`.

Conversions between numbers use functions such as `float(x)` ([3.2](03_data_model.md#32-numbers)). `e is T` is reserved, not implemented (there is no type information at run time; `SemanticDiag::ConstructNotSupported`). The postfix non-null assertion `x!` is likewise reserved and not implemented; use `x ?? default`.

## 6.8 `if` and `match`

### 6.8.1 `if`

```
if condition then arm else arm
```

`if` is an expression. An arm can be an expression or a block (`{ ... }` or `sync { ... }`); the value of a block is given by the `return` in the block. The two arms must have the same type, or one must enter the form of the other ([3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)): an arm of type `T` and an arm that is `null` or has no value make the branch a `T?`.

```cml
func sign(x: int): string {
    return if x > 0 then 'positive' else if x < 0 then 'negative' else 'zero'
}
```

- **An `if` without `else` has no value.** It can only be a statement; its arm may be of any type, and what the arm gives is dropped: `if debug then println('checking')` is legal, and an arm no longer needs `return null` to make the types fit. Using it as a value (`let x = if debug then 1`, as an argument, as the operand of `return`) is the compile-time error `SemanticDiag::IfWithoutElseAsValue`. Nothing changes on the graph: a branch with two arms, the `else` not written being an empty arm, and a join that takes no value; the arm is evaluated as usual, so an arm that does something with side effects usually has to be written in a sync block. An `else {}` written and left empty is the same as none.
- The type of the condition is `bool`; there is no "truthiness" conversion.
- **Narrowing.** In `if x != null then A else B`, `x` is a `T` inside `A`; in `if x == null then B else A` likewise. It is the same branch as `match x { case null => B  case v => A }`: the arm that excludes null starts by taking the value out of `x`, and inside that arm the name `x` stands for the value.

  ```cml
  func len_or_zero(s: string?): int {
      return if s != null then len(s) else 0
  }

  func sum_or_minus(a: int?, b: int?): int {
      return if a != null && b != null then a + b else -1
  }
  ```

  The rule is deliberately small:
  - only a name bound by `let` or a parameter is narrowed, not a `var` (it may be rewritten between the test and the use; bind its value with `let` first);
  - only the direct forms count, in either order (`x != null`, `null != x`): a test that is the whole condition, or one of the operands `&&` joins, narrows the `then` arm (`if x != null && ready then ...`). The `else` arm is narrowed only by a whole condition `x == null`, never by a conjunction. A test under `||` or `!` (`x != null || ready`, `!(x == null)`) or kept in a `let` narrows nothing; nest the `if`s, or use `match` or `??`;
  - only inside the arm: after the `if`, `x` is a `T?` again. Declaring the name again inside the arm (`let x = ...`) is a redeclaration.
  - after narrowing, the arm may still apply to `x` the constructs written for a `T?`, `??` and a `match` with `case null`: they work on the value put back into `T?`, checked as they were before narrowing, only their null arm is never taken; the compiler warns (`SemanticDiag::KnownNotNull`). Testing `x == null` or `x != null` again in the arm is the comparison of a value that is never null (above): a warning, and a fixed result.

  Since `a && b` is `if a then b else false`, the right side of `x != null && ...` is such an arm, and so is the right side of `x == null || ...`. When a name is tested and then used as a `T` where narrowing does not apply, the error carries a note that says why.

> **A `return` in a block gives the value of that block; it does not leave the function.**
>
> Each arm of a branch is a subgraph of its own. A `return v` in an arm ends that arm, and `v` becomes the value of the `if` (or `match`); the same goes for `throw e`. If the `if` is only a statement and its value is not used, then the `return` in the arm has done nothing, and the function goes on to execute the statements that follow.

```cml
// wrong: the if is a statement whose value nobody uses, and neither return has any effect
func wrong(x: int): string {
    if x < 0 then { return 'negative' } else { return 'not negative' }
    return 'reached the end'
}

// right: the value of the if is the return value of the function
func right(x: int): string {
    return if x < 0 then { return 'negative' } else { return 'not negative' }
}

func main(): int sync {
    println(wrong(-1))
    println(right(-1))
    return 0
}
```

```text
reached the end
negative
```

The compiler gives the warning `SemanticDiag::ExitInUnusedBranch` for each `return` in `wrong`:

```text
warning[SemanticDiag::ExitInUnusedBranch]: This 'return' gives the value of the 'if' it is in, and nothing uses that value: it does not leave the function.
  --> ret.cml:3:21
   |
 3 |     if x < 0 then { return 'negative' } else { return 'not negative' }
   |                     ^^^^^^^^^^^^^^^^^
note: the value of this 'if' is not used
```

So there is no "early return" in Camel: write the branch as the last expression of the function body, or write `return` before it. When several conditions are tested in turn, join `if ... else if ... else ...` into one expression. The warning can be turned off with `-Wno-exit-in-unused-branch`. `return null` and a `return` without a value are reported too: an arm does not need one to make the types fit, so one written there was most likely meant to leave the function.

### 6.8.2 `match`

```
match name {
    case pattern | pattern ... => arm
    ...
}
```

- What is matched may be any expression (`match f(x) { ... }`), not only a name.
- A pattern is a literal (integer, floating-point number, string, `true`/`false`) or the wildcard `_`. One `case` can list several patterns with `|`, and is entered when any of them matches. **Each pattern must be a value the type of the matched value accepts** (design principles 3.7): it is compared with that value by the comparison of its type, so `case 1` on a `string` is `SemanticDiag::MatchCaseTypeMismatch`.
- The first matching case in the order written is taken. All arms must have the same type. A `return` in an arm, as in an arm of `if`, gives the value of the `match`.

```cml
func kind(c: string): string {
    return match c {
        case 'a' | 'e' | 'i' | 'o' | 'u' => 'vowel'
        case ' ' => 'space'
        case _ => 'other'
    }
}

func main(): int sync {
    println(kind('e'), kind(' '), kind('x'))
    return 0
}
```

```text
vowel space other
```

**Fixed patterns for `T?` and `T!`.** When the matched value is a `T?` or a `T!`, the `match` has exactly two cases:

- `T?`: `case null`, plus `case v` (which binds the unwrapped value as `v`, of type `T`) or `case _`.
- `T!`: `case error e` (which binds the error as `e`, with `e.kind`, `e.message`, `e.location`), plus `case v` or `case _`.

```cml
func show(x: int?): string {
    return match x {
        case null => 'nothing'
        case v => 'value ' + string(v)
    }
}

func report(r: int!): string {
    return match r {
        case error e => e.kind + ': ' + e.message
        case v => 'ok ' + string(v)
    }
}

func main(): int sync {
    println(show(5), show(null))
    println(report(parse_int('7')), report(parse_int('x')))
    return 0
}
```

A missing case, an extra case or a literal case mixed in is `SemanticDiag::TaggedMatchCases`.

**Patterns on an `enum`.** When the matched value is an enum, a pattern names one of its variants ([3.5](03_data_model.md#35-enums)): `case Shape::Circle(r)`, `case Shape::Rect { w, h }`, `case Shape::Empty`, `case _`. A tuple variant gives every position; a struct variant writes the fields it binds (it may rename and abbreviate); a variant without data is written by its qualified name (`case Shape::Empty`; a bare name is a binding, and `case Empty` is `EnumPatternMismatch`). Naming every variant means no `case _` is needed; a missing variant without `case _` is `SemanticDiag::EnumMatchNotExhaustive`, whose diagnostic lists the missing variants; a variant written twice warns (`EnumVariantCaseRepeated`), a case after `case _` warns (`MatchCaseAfterWildcard`), and a `case _` written after every variant is already named warns (`MatchWildcardRedundant`). The first version has no nested patterns (a pattern in the position of a field) and says "not supported yet" (`ConstructNotSupported`).

**A `match` must have a default case.** A `match` without `case _` is the compile-time error `SemanticDiag::MatchWithoutDefault`, located at the `match`. Three shapes need none: a `match` on a `bool` that writes out both `case true` and `case false`, a `match` on a `T?` or `T!` with the fixed patterns above, and a `match` on an enum that names every variant. The compiler adds no arm the program does not write, so the arms on the graph correspond one to one to the `case`s in the source.

Tuple patterns, struct patterns, and identifier patterns on values of ordinary types (`case n => ...`) are reserved, not implemented (`SemanticDiag::ConstructNotSupported`); use an `if` chain, or take the members first and then match. Tuple and struct patterns of enum variants are implemented, but a field position takes only a name or `_`.

`try`/`catch`/`finally` is not part of Camel (it has no stable mapping onto the graph): writing it gives `SemanticDiag::TryCatchNotInLanguage`. An operation that may fail returns `T!`, handled with `!!`, `?->` and `case error e`.

### 6.8.3 `loop`

A loop is an expression with a state (design principles 3.10). The state is written in the head of the loop: each name is declared once with its first value. The expression after `do` is the body; its value is the state of the next round, and the value of the whole loop is the last state.

```
loop name = init (, name = init)* (for name in sequence)? (while condition)? do expression
```

```cml
let total    = loop acc = 0 for x in xs do acc + x * x
let (i, acc) = loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)
let p        = loop p = { x: 0 } for d in ds do { x: p.x + d }
let found    = loop hit = -1 for x in xs while hit < 0 do if x > 9 then x else hit
```

- **State.** With one name the body's value and the loop's value are that value; with several names (separated by commas) the body gives the tuple of them in the order of the head, and the loop's value is that tuple, taken apart with `let (a, b) = loop ...` or `.0`, `.1`. The state has the type of its first value; the body's value must have that type (or enter it, as an `int` enters an `int?`), otherwise `SemanticDiag::LoopBodyTypeMismatch`. `null` and the empty array `[]` have no type of their own as first values: write `null as T?`, or bind the empty array with its type first (`let none: int[] = []`) and start from that name (`SemanticDiag::LoopStateWithoutType`).
- **`for x in xs`** takes one element of the sequence `xs` per round, in order; `xs` is an array, a string, a map or a tensor, and any other type is `SemanticDiag::LoopSequenceNotIterable`.
  - **Array**: the element itself; `range(a, b)` is one.
  - **String**: by code point, each element the one-character string of that code point, as `s[i]` gives it (11.1): the two rounds of `for c in '中文'` are `'中'` and `'文'`, and a multi-byte character counts once.
  - **Map**: the `(key, value)` pairs in key order (the traversal order of 11.3, as `items(m)` gives them), not insertion order: in `for kv in m` the keys are strictly ascending, and `kv.0` and `kv.1` take the pair apart.
  - **Tensor**: along the first dimension, each element the sub-tensor with the first dimension dropped (sharing that slice of the data, not copied): a rank-2 `Tensor<f32, const [2, 3]>` gives two `Tensor<f32, const [3]>`, and a rank-1 tensor gives rank-0 sub-tensors (`sum(x)` is its number). The element type of a `Tensor` whose shape is unknown is `Tensor`. A rank-0 tensor has no first dimension and is not a sequence.
  An empty array, empty string, empty map, or a tensor whose first extent is 0 runs no round, and the loop's value is the first state. **`while c`** goes on as long as the condition holds; the condition is a `bool` (`SemanticDiag::LoopConditionNotBool`). With both, each round looks at the condition first and then takes the next element: the loop ends when the condition fails or the elements are used up. At least one of them is written.
- **No `break` and no `continue`**: ending early is written as the `while` condition, skipping the rest of a round as an `if`.
- **Scope.** The first values and the sequence are evaluated once, where the loop is, and do not see the names of the head; the condition sees the names of the state, the body those and the name of `for`. A name of the head is given once (`SemanticDiag::LoopNameRepeated`). The condition and the body see the names around the loop (a `var` is read anew in every round).
- **After `do`** comes an expression, read as after `then`: `{ x: 1 }` is a struct, `{ 5 }` and `sync { ... }` are blocks. A `return` in such a block ends the block with the next state; it does not leave the function.
- **Effects.** Whether the body is a sync block is up to the body (3.2). Written `do sync { println(x); acc + x }`, its effects run in order every round. A loop in a block that is not a sync block, whose value nothing uses, does not run, with `EffectNotExecuted`, as for an unused call.
- **Order and depth.** The rounds run strictly one after the other. A loop is not a recursion: no number of rounds takes call depth, `--max-recursion-depth` does not count them, and a loop that does not end runs on.
- **Numeric permissions.** `@numeric(...)` on a function or a block covers the bodies and conditions of the loops written in it (7.5).
- `loop`, `while`, `do`, `for` and `in` are keywords.

On the graph a loop is one `LOOP` node in the graph it is written in; the condition and the body are a subgraph each, sharing one table of captured values (design principles 3.10; `docs/design/graph-model` 3.5). A string, a map and a tensor are turned into their array of elements (the code points, the key-value pairs, the first-dimension sub-tensors) once, where the loop is; the `LOOP` node itself still iterates an array, which is the only form the back ends and the lowering handle.

## 6.9 Lambdas and function types

A lambda is written `(parameters): return-type => body`; the return type can be omitted, and the body is an expression or a block:

```cml
(x: int) => x + 1
(x: int): int => { let y = x * 2; return y + 1 }
(s: string) => sync { println(s) }              // a sync block as the function body
sync (x: int) => sync { total = total + x }     // writes a captured variable: add the function modifier sync
```

A function type is written `(x: T) => R`, and the parameters must be named: `(x: int) => int`, `(acc: int, x: int) => int`.

**Functions are values.** The name of a function can be used wherever a value can be written: as an argument, as a `with` argument, in `let g = f`, in `return f`, as an element of an array or tuple, as the value of a branch arm.

```cml
func inc(x: int): int { return x + 1 }
func dbl(x: int): int { return x * 2 }

func apply_twice(f: (x: int) => int, x: int): int {
    return f(f(x))
}

func adder(n: int): (x: int) => int {
    return (x: int): int => x + n          // captures n
}

func main(): int sync {
    let g = inc
    let add5 = adder(5)
    println(g(1), apply_twice(dbl, 3), add5(10))
    println([inc, dbl]->map<(f: (x: int) => int): int => f(10)>)
    return 0
}
```

```text
2 12 15
[11, 20]
```

- A lambda captures variables of the enclosing scope, and a capture is a reference: after an outer `var` is modified the lambda sees the new value, and the outer scope also sees the lambda's writes to it.
- A library operator with one signature that does not vary with its arguments is a function value like a user function: `xs->map<trim>`, `let t = trim`.
- A name that stands for a group of overloads (several user functions of one name, or an operator with several signatures such as `abs`) is, where the callee has one signature and its parameter there is a function type, the one whose signature is exactly that type: the call `on_int(abs, -3)` of `func on_int(f: (x: int) => int, v: int)`. Anywhere else it has no single function value, and using it as a value is `SemanticDiag::FunctionNameAsValue`, which lists the signatures; write a lambda that spells out the parameter types. An operator whose signature varies with its arguments (`println`, `len`, `map`, `append`) has no function value either. The same goes for the name of a generic function (`SemanticDiag::GenericFunctionAsValue`).
- When such a name is handed to a built-in higher-order operator (`xs->map<abs>`, `xs->reduce<max, 0>`, `xs->filter<is_even>`), the operator cannot say which function it expects, so the compiler asks it about each: when exactly one is accepted, that one is meant; when none or several are, the name has no value (`SemanticDiag::FunctionNameAsValue`, which lists the candidates).
- A declaration with a function type, `let f: (x: int) => int = inc`, is currently unavailable (`SemanticDiag::FeatureNotSupported`); just omit the type.

## 6.10 Higher-order operators

| Form | Result | Order |
|---|---|---|
| `xs->map<f>` | A new array of each element passed through `f` | No order between elements, may run in parallel; results arranged by position |
| `xs->filter<p>` | The elements for which `p` is true | Same as above |
| `xs->reduce<f, init>` | The final value of `f(acc, x)` applied in turn, starting from `init` | In element order |
| `xs->foreach<f>` | None; calls `f` for each element | In element order |

```cml
func main(): int sync {
    let xs = range(1, 6)
    println(xs->map<(x: int): int => x * x>)
    println(xs->filter<(x: int): bool => x % 2 == 0>)
    println(xs->reduce<(acc: int, x: int): int => acc + x, 0>)
    xs->filter<(x: int): bool => x > 3>->foreach<(x: int) => sync { println('item', x) }>
    return 0
}
```

```text
[1, 4, 9, 16, 25]
[2, 4]
15
item 4
item 5
```

If the element function of `foreach` is to produce side effects, its body must be a sync block (`=> sync { ... }`); for the reason see [4.3.2](04_execution_model.md#432-a-functions-behaviour-is-decided-by-the-function-itself). A function with `var` parameters cannot be handed to these operators.

## 6.11 Assignment and `wait`

- `x = e`, `x += e` and so on are expressions whose value is the value assigned. The target must be a `var` variable or a `var` parameter itself ([3.7](03_data_model.md#37-let-var-and-references)).
- Types for which compound assignment is available:

  | Operator | Types |
  |---|---|
  | `+=` | The four numeric types, `string` (concatenation) |
  | `-=`, `*=`, `/=`, `^=` | The four numeric types |
  | `%=` | `int`, `i32` |
  | `&=`, `\|=` | `bool` (`a = a & b`, `a = a \| b`: the right side is always evaluated) |
  | `@=` | None of the built-in types |

- **A compound assignment changes nothing in place.** `x op= e` computes the new value and binds `x` to it. A type defined by a module (a tensor), and a type for which the program overloads an operator ([7.4.5](07_statements.md#745-operator-overloading)), has no compound-assignment operators of its own, so for it `t += u` is `t = t + u` (likewise `-=`, `*=`, `/=`, `%=`, `@=`, `^=`, `&=`, `|=`, wherever the binary operator is defined for the types): after `let a = t`, `t += u` leaves `a` unchanged. To change a value in place, use an explicit operator.

- For `wait e` see [4.4](04_execution_model.md#44-var-references-and-wait).

## 6.12 Operator precedence

From lowest to highest:

| Level | Operators | Notes |
|---|---|---|
| 1 | `wait` | Prefix; applies to the whole assignment expression |
| 2 | `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | Cannot be chained |
| 3 | `\|\|` | Left-associative; a branch, the right side evaluated only when needed |
| 4 | `&&` | Left-associative; a branch, the right side evaluated only when needed |
| 4a | `\|` | Left-associative, both sides evaluated |
| 4b | `&` | Left-associative, both sides evaluated |
| 5 | `==` `!=` (`===` `!==` not supported for now) | |
| 6 | `<` `>` `<=` `>=` | |
| 7 | `+` `-` | Left-associative |
| 8 | `*` `/` `%` `@` | Left-associative |
| 8a | `^` | Right-associative, one level above the multiplicative operators |
| 9 | `??` `!!` | The right side is a complete expression |
| 10 | `as` | |
| 11 | `->` `?->` | Left-associative |
| 11a | Unary `-` `!` `~` | Tighter than `->`, looser than `^`: `-a->f` is `(-a)->f`, `!a->f` is `(!a)->f`, `-a ^ 2` is `-(a ^ 2)`; a literal and a name read alike |
| 12 | `..` `?..` | |
| 13 | Postfix: `.field`, `[subscript]`, `(arguments)`, `<with arguments>` | Must directly follow |

`if` and `match` can appear wherever an expression is required; their arms extend to the end of the expression, so add parentheses when they are embedded in a larger expression.
