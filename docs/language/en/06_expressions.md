# 6. Expressions

English | [中文](../06_expressions.md)

Almost everything in Camel is an expression: `if`, `match`, blocks and assignments all have values. This chapter covers them kind by kind.

---

## 6.1 Evaluation

An expression is a node on the graph, and its operands are its input edges. Whether and when an expression is evaluated is decided by dependencies ([4.2](04_execution_model.md#42-evaluation-by-dependency)); operator precedence and associativity decide only the shape of the graph ([6.12](#612-operator-precedence)). `&&`, `||`, `??`, `!!`, `->` and `if`, `match` are branches: the side that is not selected is not evaluated.

## 6.2 Literals and construction

<!-- camel-snippet: fragment: a list of literal and lambda forms, not a program -->

```cml
42          3.14        'text'      true        null
[1, 2, 3]                   // array
(1, 'one')                  // tuple; a one-element tuple is written (x,)
{ x: 1, y: 2 }              // struct
(x: int): int => x + 1      // lambda, see 6.9
```

- The elements of an array literal must have the same type. An element is **a position with an expected type** ([3.3.1](03_data_model.md#331-arrays)): with a context every element takes that type, so the elements of `let xs: float[] = [1, 2.0]` are `float`; with none, the number literals of one array take the widest kind (integer < decimal), so `[1, 2.0]` is a `float[]` (as Go's untyped constants do). The element type of the empty array `[]` comes from the context: `let xs: int[] = []`, `[] as Map<string, int>`.
- The keys of a struct literal are identifiers. A field is such a position too: in `let p: P = { a: 1, b: 2.0 }` (`P = { a: float, b: float }`) the `1` is a `float`. A field written as a name is a variable, not converted: a type it does not fit is `SemanticDiag::DeclaredTypeMismatch`.
- **A list comprehension** `[e for x in xs if c]` is `xs->filter<(x) => c>->map<(x) => e>`: `x` has the element type of `xs`, `if c` may be left out, and the result is an array of `e`. On the graph it is exactly what the two calls written out are (two operator nodes, each with the subgraph of its element function), so it has their properties: nothing is promised about the order of the elements (a parallel scheduler may compute them concurrently), and effects in `e` and `c` are treated as in the element functions of `map` and `filter` ([6.10](#610-higher-order-operators)). It nests, and it may stand wherever an expression may. A source that is not an array is `SemanticDiag::ComprehensionSourceRefused`.
- **A spread is not in the language**: `...xs` written in a literal is an ordinary syntax error (`...` is written only in a relative module name, `import ...module`). To join arrays write `concat(xs, ys)`.

### 6.2.1 Formatted strings

In a backquoted string `` `...` ``, `${expression}` interpolates, optionally with a format specification, `${expression:spec}` (the lexical rules are in [2.5.4.1](02_lexical.md#2541-formatted-strings)). It is **the same tree** as `'...'->format<...>`: each `${...}` becomes a placeholder of `format` (`{}` or `{:spec}`), a `{` or `}` of the literal text is written `{{` or `}}`, and `format` is called with the interpolated expressions as its with-arguments and that text as its ordinary argument.

```cml
func main(): int sync {
    let name = 'Camel'
    let n = 42
    println(`hello, ${name}!`)          // 'hello, {}!'->format<name>
    println(`n = ${n}, twice = ${n * 2}`)
    println(`pi = ${3.14159:.2f}`)      // 'pi = {:.2f}'->format<3.14159>
    return 0
}
```

- An interpolated expression is evaluated where it is written, in the lexical scope there; name lookup, type checking and effects are exactly those of the same expression outside the string. They are the with-arguments of `format`, so when they are evaluated follows the dependency and effect rules ([4.2](04_execution_model.md#42-evaluation-by-dependency)), exactly as if the same expressions were written outside the string.
- A specification the value's type does not take is a **compile-time** error `SemanticDiag::ArgumentsRejected`, located at that interpolation (the same check `format` makes for a constant format string).
- A syntax error in an interpolated expression is reported at its position in the file; being inside a string does not swallow it.
- Being the same tree, every execution mode (`std::nvm`, `std::fvm`, `std::jit`, `std::prl`) and the `std::cpp` translator see a `format` call: their output is byte-identical.
- The formatter does not change the text of a backquoted string (the interpolated expressions are kept as written); to format code inside an interpolation, write the `format` call or move the expression out.

## 6.3 Access and subscripts

| Form | Meaning |
|---|---|
| `p.x` | Struct field. A field that does not exist is the compile-time error `SemanticDiag::InvalidAccessIndex` |
| `t.0` | Tuple member; the index is a literal, and out of range is a compile-time error |
| `xs[i]` | Array element. Out of range (including negative) is the fault `RuntimeDiag::IndexOutOfRange` |
| `xs[idx]` | `idx` is an `int[]`: the elements at those positions, in the order of `idx` (that is `take`), see below |
| `xs[a:b]`, `xs[a:b:s]`, `xs[:b]`, `xs[a:]`, `xs[:]` | A slice, see below |
| `m[k]` | Map lookup; the type is `V?` ([3.6](03_data_model.md#36-maps-and-sets)) |
| `mod::name` | Qualified name |

- `.` cannot be used on an array (`xs.1` is `SemanticDiag::TypeNotIndexable`).
- A subscript directly follows the expression being accessed, with no whitespace.
- **Subscripts start at 0, and a negative one does not count from the end**: `xs[-1]` is the fault `RuntimeDiag::IndexOutOfRange`. The last element is written `last(xs)`.
- **`xs[idx]` is `take`**: `idx` must have the type `int[]`, and the result is the elements at the positions of `idx`, in the order of `idx`; a position that appears twice is taken twice. Out of range and negative are the fault `RuntimeDiag::IndexOutOfRange`. The result is a new array, not a view ([5.4](../../design-principles.en.md)).
- **A slice is along the first dimension, left-closed right-open**: `xs[a:b]` is the positions `a` to `b-1`. An omitted `a` means from the start, an omitted `b` to the end, and `xs[:]` is the whole array; `s` is the step. A slice is a **view sharing the data block** ([5.4](../../design-principles.en.md)); it does not copy the elements.
  - Both bounds are positions from 0 to `len(xs)` (`b` may equal the length, which takes up to the last element); a negative or out-of-range bound is the fault `RuntimeDiag::SliceOutOfRange`, as an index out of range is a fault ([3.9](../../design-principles.en.md)). `b < a` is the empty array, not a fault. When the length is known at compile time and the bounds are constants, an out-of-range bound is a compile-time error ([3.9](../../design-principles.en.md)).
  - The step is written after a second colon: `xs[a:b:s]`, and the result is still a view sharing the data block; for a copy write `slice(xs, 0, a, b, s)`.
  - An array whose elements are objects (`[[1, 2], [3, 4]]` and the like) is copied row by row: a slot array has no data block to share.
- **Multidimensional subscripts**: `xs[:, 0]` and `xs[a:b, c]` take coordinates along each dimension and give a view; `:` takes a whole dimension ([5.4](../../design-principles.en.md)).
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
- Arguments correspond to parameters by position, and the types must match: numeric *variables* are not converted implicitly, while **a literal takes the parameter's type** (`f(2)` with `f(x: float)` is `2.0`, see [3.2](03_data_model.md#32-numbers)); only a value that enters a `T?` or `T!` parameter is put into the form by itself ([3.4](03_data_model.md)). A mismatch is `SemanticDiag::NoMatchingFunction` or `SemanticDiag::ArgumentsMismatch`, and the diagnostic lists the available signatures.
- When there are several functions of the same name, one is selected by the overloading rules ([7.4.3](07_statements.md#743-overloading)).
- **`f<w>` is a partial application.** Without parentheses, `f<w>` is a value: a function that already has its context arguments. After `let g = inc<1>`, `g(2)` is `inc<1>(2)`; `w` is evaluated once, where `f<w>` is written. It can be bound to a name, passed, returned, and handed to a higher-order operator directly (`xs->map<inc<10>>`).
  - When only the first context parameters are given, the rest remain context parameters of the result: after `let above = clamp<0>`, write `above<5>(9)`. A function that has all of them cannot be given more (`g<2>` is an error).
  - `f` must be one function that these arguments determine. Among several functions of one name, the only one that takes these context arguments is meant; if several do, the function type of the position (a parameter) decides, and when nothing does it is `SemanticDiag::PartialApplicationRefused`.
  - On the graph it is a function value that carries the captured arguments, the shape a closure has: a subgraph of its own which the bound arguments enter through ports. No node kind is added.
  - A function whose `with` parameter is declared `var` can be partially applied too: `f<w>` captures the variable itself and not its value, so a call of it makes the write the function makes to that variable land on the variable it was bound to ([3.5](03_data_model.md)); such a call is a call with an effect, and runs only in a `sync` block.
  - A name that stands for more than one function has no function value: an overloaded built-in operator (such as `len`, which has three signatures) is one, and the refusal is `SemanticDiag::FunctionNameAsValue`. Write the call in a lambda there.
- Arguments may be given by name (`f(x: 1)`): positional arguments first, named ones after; a name the callee does not declare as a parameter is `SemanticDiag::ParamArgumentUnknown`.
- Two calls with angle brackets in one argument list (`f<a>(x), g<b>(y)`) are read as comparisons; a call with angle brackets followed by a pipe with angle brackets (`f<a>(x)->g<b>`) may be too. Write them as two statements.
- A lambda written in parentheses can be called directly: `((x: int): int => x + 1)(3)`; it can also be written on the right of `->`: `3->(x: int): int => x + 1`, `parse_int(s) -> (x: int): int => x * 2`.

## 6.5 Pipes

`a->f` makes `a` an argument of `f`: `a->f` is `f(a)`, and `a->f<w>` is `f<w>(a)`. It is left-associative and suits writing a data flow as one chain:

```cml
func main(): int sync {
    let total = 1..6
        ->map<(x) => x * x>
        ->filter<(x) => x % 2 == 1>
        ->reduce<(acc: int, x: int): int => acc + x, 0>
    println(`total = ${total}`)
    return 0
}
```

```text
total = 35
```

- A pipe `x -> f` over a `T?` or a `T!` carries a null or an error along, see [6.6](#66-operators-for-null-and-errors).
- `f..g` is function composition: `(f..g)(x)` is `g(f(x))`, the same direction as the pipeline `x->f->g`; with integers on both sides `a..b` is the half-open interval, giving `int[]`, and a step is written `range(a, b, step)`. `for i in 0..n` is fused by the compiler and materialises no array. `..` binds looser than `+` and `-` (6.12): `1..n+1` is `1..(n+1)`, as it reads in other languages; it is also looser than `->`, so `x->f..g` reads as `(x->f)..g`; in a pipeline write `x->f->g`, which is the same function.

## 6.6 Operators for null and errors

For `T?` and `T!` see [3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t). On the graph each of the three operators below is a branch whose condition is a one-bit tag on the value.

| Form | Left side | Right side | Result |
|---|---|---|---|
| `a ?? b` | `T?` | `T` | `b` when `a` is null, otherwise the value inside `a` |
| `a !! b` | `T!` | `T` | `b` when `a` is an error, otherwise the value inside `a` |
| `x -> f` | `T?` or `T!` | A function that does not accept the `T?` / `T!` itself | A null `x` gives null and an error is passed on unchanged, otherwise `f` is called with the value; the result is `U?` or `U!` (`f` may return either `U` or a value already of that form) |

```cml
func half(x: int): int! {
    return if x % 2 == 0 then x / 2 else { return error('Odd', 'not even') }
}

func main(): int sync {
    println(parse_int('12') !! 0, parse_int('twelve') !! 0)
    println(parse_int('12') -> half -> half !! -1)
    println(parse_int('10') -> half -> half !! -1)
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
- **`->` asks the overloads of `f` first, with the value as it is.** `f` is looked for among the functions of that name by the type of `x` itself (`T?` or `T!`), their `when` conditions included: one that takes it (a parameter of type `T?`, or a generic `T` its condition allows to be a `T?`) is called with `x` whole. Only when none does, the call takes `x` apart: a null `x` gives null, a failed `x` gives its error as it is, and anything else calls `f` with the value inside. The other arguments of the call belong to the call that is taken and are evaluated only then. A value that is tagged twice (`T?!`) is taken apart one tag at a time, the same way.
- The right side of `??` and `!!` extends to the end of the expression: `x ?? 0 + 1` is `x ?? (0 + 1)`. To take the value first and then compute, add parentheses: `(x ?? 0) + 1`.
- A left side that is not of the required form (`!!` on a `T?`, `??` on a `T!`), or a right side of the wrong type, is `SemanticDiag::TaggedOperandMismatch`. `??` on a value that cannot be null (a plain `int`, or a name an `if` already narrowed to a `T`) is allowed: its null arm is never taken and the result is the value itself; the compiler warns (`SemanticDiag::KnownNotNull`, design principles 3.8).

## 6.7 Other operators

The operators of the following sections (except `&&`, `||` and `as`) are calls of functions: `a + b` is `__add__(a, b)`, and the tables list the built-in overloads. A program can add overloads for its own types (`func operator +`, [7.4.5](07_statements.md#745-operator-overloading)); they form one set with the built-in overloads and those of C++ modules, and the types of the operands choose among them.

### 6.7.1 Arithmetic

| Operator | Types | Notes |
|---|---|---|
| `+` | The four real numeric types, `c64`, `c128`, `string` | Concatenation for strings |
| `-` `*` `/` | The four real numeric types, `c64`, `c128` | Integer division truncates towards zero; a zero divisor is a fault (the complex zero for complex numbers) |
| `%` | `int`, `i32` | The remainder has the sign of the dividend; a zero divisor is a fault. Never defined for complex numbers |
| `^` | The four real numeric types, `c64`, `c128` | Power: an integer exponent of a complex number is repeated squaring (exact), a complex exponent is the platform's `pow` ([3.2](03_data_model.md#32-numbers)) |
| `@` | Defined by modules | `tensor` module: matrix multiplication |
| Unary `-` | The four real numeric types, `c64`, `c128` | |
| `~` | `int`, `i32` | Bitwise not: `~5` is `-6` |

The two operands must have the same type. There are no bitwise and, or, xor or shift operators; use `bit_and`, `bit_or` and `bit_xor` of the `math` module and `x->shl<n>`, `x->shr<n>` (the count in angle brackets); the complement is `~x`.

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

- `==`, `!=`: numbers, `bool`, `string`, `Map`, `Set`, and arrays, tuples, structs and tensors, with the same type on both sides (anything else is the compile-time error `SemanticDiag::NoMatchingFunction`; nothing is converted). The result is a `bool`.
  - **Arrays, tuples and structs compare by content**, element by element and recursively: `[1, 2] == [1, 2]`, `(1, 'a') == (1, 'a')` and two structs whose fields are equal are all `true`; arrays of different lengths are not equal.
  - **The whole comparison is the element-wise one, reduced to a `bool`**: `a == b` is "the shapes are the same and the elements are equal everywhere" (see [4.2](../../design-principles.en.md)). Numeric arrays (`f32[]` and `f32[..]`) answer a `bool` the same way.
  - The element type must itself be comparable: numbers, `bool`, `string`, `Map`, `Set`, `T?`, `T!`, and arrays, tuples and structs of those. A value with a function in it (`[f] == [f]`) cannot be compared, which is a located compile-time error.
  - Float elements follow the rules of floats: an array that holds a `nan` does not equal itself, and `[0.0] == [-0.0]`.
  - A `T?` or `T!` by itself (not inside an array, tuple or struct) still has no `==`; open it first with `??`, `!!` or `match`.
- **`===` and `!==` are the element-wise equality and inequality**: `a === b` compares two arrays element by element and gives a `bool` array of the same shape: `[1, 2, 3] === [1, 2, 4]` is `[true, true, false]`; `a !== b` is its element-wise negation. The shape rules are those of every element-wise operation (a shape that does not broadcast is a located run-time error). On a scalar `===` is `==` (`1 === 1`, `'a' === 'a'`).
  - Like `<`, `>`, `<=` and `>=`, it is defined on numeric arrays: `f32[]`, `f32[..]`, `int[]`, an array whose element type is a number. The whole comparison (the previous item) is defined on every array whose element type is comparable.
- **`x == null` and `x != null`** are defined for an `x` of type `T?`, in either order, and give a `bool`: the test of the tag of `x`, not a comparison of two values (the same node `??` branches on). It is an ordinary expression: `let has = x != null`. A nullable function value is compared with `null` the same way. A value that is never null (an `int`, a `T!`, a name already narrowed to a `T`) may be compared with `null` too: the result is fixed (`==` is always `false`, `!=` always `true`), the compiler warns (`SemanticDiag::NullTestNeverNull`), and the value itself is not evaluated. Two `T?` values compared with each other stay `SemanticDiag::NoMatchingFunction`; two function values cannot be compared.
- `<`, `>`, `<=`, `>=`: numbers and `string` (in code point order); **on a numeric array they compare element by element**, and give a `bool` array of the same shape. Comparisons cannot be chained: `1 < 2 < 3` is a type error.

### 6.7.4 Type conversion `as`

`e as T` is used for:

- entering the nullable or fallible form: `5 as int?`, `null as int?`, `v as int!` (not needed where the target type is known, see [3.4](03_data_model.md));
- constructing maps and sets: `[...] as Map<K, V>`, `[...] as Set<T>`;
- checked conversion of module types: the type's own condition is checked at the conversion.
- checked conversion of arrays: `a as f32[4]`, where the shape is checked at the conversion; `f32[3]` is another spelling of a numeric array (kept during the transition), and `t as f32[3]` is the same conversion.
- converting a value to its own type, which does nothing (`n as int`).

`as` does not convert between numbers, for a literal as for a variable: `5 as float` and `n as float` are both `SemanticDiag::DynamicCastForbidden`, and so are `true as int` and `n as bool`. A number of another type is made by the conversion function of the target (`float(n)`, `int(x)`, `i32(n)`, `f32(x)`), whose loss rule (a value that does not fit is a fault) is in design principles 5.1.

`as` never changes the type of a value unchecked. The result of `json::decode` is a `Json`, which does not convert to another type with `as`; JSON of a known shape is decoded with `json::decode_as::<int[]>(s)`, which checks the shape while decoding and gives a `T!`.

Conversions between numbers use functions such as `float(x)` ([3.2](03_data_model.md#32-numbers)). `is` is not a keyword: the `is` of `e is T` is an ordinary name, and writing it is a syntax error (there is no type information at run time). A postfix `!` is not a form of the language: the `!` of `T!` belongs to the type, and the value of a `T?` is taken out with `x ?? default`, so `x!` is an ordinary syntax error (at the `!`).

## 6.8 `if` and `match`

### 6.8.1 `if`

```
if condition then arm else arm
```

`if` is an expression. An arm can be an expression or a block (`{ ... }` or `sync { ... }`); the value of a block is given by the `return` in the block. The two arms must have the same type, or one must enter the form of the other ([3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)): an arm of type `T` and an arm that is `null` or has no value make the branch a `T?`.

The value of an arm is itself **a position with an expected type** (design principles 5.1): when the whole `if` has one (a declaration written with a type, a function's return position, and so on), a literal in an arm takes that type, so both arms of `let x: float = if c then 1 else 2` are `float`. An arm written as a name is a variable, not converted; a type that does not fit the other arm is still reported as a mismatch of the arms.

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
  - after narrowing, the arm may still apply to `x` the constructs written for a `T?`, `??` and a `match` with `case null`: they work on the value put back into `T?`, checked as they were before narrowing, only their null arm is never taken; the compiler warns (`SemanticDiag::KnownNotNull`; `??` on a value that was never nullable in the first place is allowed with the same warning). Testing `x == null` or `x != null` again in the arm is the comparison of a value that is never null (above): a warning, and a fixed result.

  Since `a && b` is `if a then b else false`, the right side of `x != null && ...` is such an arm, and so is the right side of `x == null || ...`. When a name is tested and then used as a `T` where narrowing does not apply, the error carries a note that says why.

> **A `return` in a block gives the value of that block; it does not leave the function.**
>
> Each arm of a branch is a subgraph of its own. A `return v` in an arm ends that arm, and `v` becomes the value of the `if` (or `match`); the same goes for a `return e` that hands out an error value. If the `if` is only a statement and its value is not used, then the `return` in the arm has done nothing, and the function goes on to execute the statements that follow.

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

A `return` in an arm takes its type from **the position the arm stands at**, like any other expression in the arm ([3.3.1](03_data_model.md#331-arrays), design principles 5.1): in `let v: float = if c then { return 1 } else { 2.0 }` the `1` is a `float` (the position is the declaration of `v`), while in `let v = if c then { return 1 } else { 2 }` it is an `int` (the position has no type, so the arms decide). The declared return type of the function is **not** the expected type of such a `return`: in `func f(c: bool): float { let v = if c then { return 2 } else { 3 } … }` the `v` is an `int`, and the function converts where it needs to. The last expression of a function body (a whole `if`/`match` included) is the position of the function's return value: its type is the declared return type, and a `return` in its arms reads it from there.

### 6.8.2 `match`

```
match name {
    case pattern | pattern ... => arm
    ...
}
```

- What is matched may be any expression (`match f(x) { ... }`), not only a name.
- A pattern is a literal (integer, floating-point number, string, `true`/`false`) or the wildcard `_`. One `case` can list several patterns with `|`, and is entered when any of them matches. **A pattern literal takes the type of the matched value** (it is a position with an expected type, design principles 5.1): with `x: f32`, the `3` of `case 3` is an `f32`; with `x: int`, `case 5000000000` is `SemanticDiag::LiteralNotRepresentable`. **Each pattern must be a value the type of the matched value accepts** (design principles 3.7): it is compared with that value by the comparison of its type, so `case 1` on a `string` is `SemanticDiag::MatchCaseTypeMismatch`.
- The first matching case in the order written is taken. All arms must have the same type. A `return` in an arm, as in an arm of `if`, gives the value of the `match`, and takes its type from the position of the arm (see 6.8.1); the value of an arm is such a position too, and takes the expected type of the whole `match`.

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

**Patterns on an `enum`.** When the matched value is an enum, a pattern names one of its variants ([3.5](03_data_model.md#35-enums)): `case Shape::Circle(r)`, `case Shape::Rect { w, h }`, `case Shape::Empty`, `case _`. A tuple variant gives every position; a struct variant writes the fields it binds (it may rename and abbreviate); a variant without data is written by its qualified name (`case Shape::Empty`; a bare name is a binding, and `case Empty` is `EnumPatternMismatch`). Naming every variant means no `case _` is needed; a missing variant without `case _` is `SemanticDiag::EnumMatchNotExhaustive`, whose diagnostic lists the missing variants; a variant written twice warns (`EnumVariantCaseRepeated`), a case after `case _` warns (`MatchCaseAfterWildcard`), and a `case _` written after every variant is already named warns (`MatchWildcardRedundant`). A field position takes a name or `_` only: a nested pattern (`case Shape::Circle(Shape::Square(x))`) is an ordinary syntax error.

**A `match` must have a default case.** A `match` without `case _` is the compile-time error `SemanticDiag::MatchWithoutDefault`, located at the `match`. Three shapes need none: a `match` on a `bool` that writes out both `case true` and `case false`, a `match` on a `T?` or `T!` with the fixed patterns above, and a `match` on an enum that names every variant. The compiler adds no arm the program does not write, so the arms on the graph correspond one to one to the `case`s in the source.

Tuple patterns and struct patterns are not in the language: `case (...)` and `case {...}` are ordinary syntax errors. An identifier pattern on a value of an ordinary type (`case n => ...`), and `error e` over an ordinary type, report `SemanticDiag::IdentifierPatternNeedsNullableOrErrorType`: only the two marks `T?` and `T!` can be taken apart (3.8), so to read the value or to catch the error write `??`, `!!`, or a `match` with a literal pattern and `_`. Tuple and struct patterns of enum variants are implemented, but a field position takes only a name or `_`.

`try`/`catch`/`finally` is not part of Camel (it has no stable mapping onto the graph), and none of the three is a keyword: they are ordinary names, so a `try { ... }` written with them is an ordinary syntax error. An operation that may fail returns `T!`, handled with `!!`, `->` and `case error e`.

### 6.8.3 `match type`

`match type` selects a branch by the **shape or the identity of a type**, for generic functions (design principles 5.5): `T` is concrete when the function is instantiated, the compiler builds only the selected branch, and the other branches are neither built nor type-checked (their names are still resolved).

```
match type type {
    case struct => branch
    case array  => branch
    case nullable => branch
    case fallible => branch
    case map    => branch
    case set    => branch
    case tuple  => branch
    case func   => branch
    case #leaf  => branch
    case int    => branch
    case MyStruct => branch
    case T      => branch
    case _      => branch
}
```

- `struct` selects a structure; `tuple` selects an **ordinary** tuple; `array` selects every array type, whatever its elements (still a leaf for `#leaf` when no earlier case takes it); `func` selects a function type; `nullable` selects every `T?`, `fallible` every `T!`, `map` every map and `set` every set (each still a leaf for `#leaf` when no earlier case takes it); `#leaf` selects every other type (scalars, `string`, arrays, functions, `T?`, `T!`, cells, ...). `struct` and `tuple` are the two shapes of an [R3 inner node](../../design/generic-structure/README.cn.md): `T?` and `T!` are tagged tuples and, like mutable cells, fall into `#leaf`, not `tuple`.
- A type written in the pattern is an **identity pattern**: `case int`, `case Json`, `case MyStruct`, `case T`. Identity is **exact**: `case float` does not select `int` (`float` and `f64` are the same type, and so are `int` and `i64`; `int` and `i32` are not); `case f32` does not select `f64`; `case MyStruct` selects the declared name, not another structure with the same fields; `case T` substitutes the binding of `T` at the instantiation, so it selects exactly that type. Shapes and identities share one case and one `|` (`case tuple | MyStruct`), judged in the order written: a type an earlier case already selects is `SemanticDiag::StaticMatchPatternCannotMatch` in a later identity pattern.
- `_` selects everything, like `#leaf`, and is the fallback spelling; both must be the last case. `case struct | tuple` selects either shape. `case int` before `case #leaf` is normal; after `#leaf`/`_` it is `SemanticDiag::StaticMatchUnreachableCase`.
- The type after `match type` is a type position: a `(` there is always the start of a type, so `match type () { ... }` matches the empty tuple type; a function type takes one more pair of parentheses, `match type ((x: int) => int) { ... }`. The pattern after `case` is a type position too, with the same rule: `case (int, int)` and `case ()` are tuples, and a function type is written `case ((x: int) => int)`.
- The type name in an identity pattern is resolved here: an unknown name is `SemanticDiag::UnresolvedTypeReference`; `void` and a `typeof` expression are not a type identity, and are `SemanticDiag::StaticMatchPatternCannotMatch`. Writing the same type twice in one case is also `StaticMatchPatternCannotMatch`.
- With no matching case the error is `SemanticDiag::StaticMatchNotExhaustive`; a case after `#leaf`/`_` is `SemanticDiag::StaticMatchUnreachableCase`. The construct compares concrete type names: `case int` or `case MyStruct` is part of it, and a scalar is named directly.
- The selected branch becomes straight-line code at that position; the graph has no branch node. Its value is the value of the `match type`, and an empty branch has type `void`.
- A generic function may omit its return type: each instance gets it from the selected branch (return-type inference). A recursive call that comes back to the **same type instance** while that instance's return type is still unknown reports `SemanticDiag::GenericInstantiationCycle`; recursion with strictly smaller type arguments finishes the child instance first as usual.

**Type functions.** A type is also computed from a type (design principles 5.6): a type function is declared with `type`, `type F<T> = body`, and called `F<...>` where a type is written; its body is a type expression, and a `match type` whose cases give types is one: `type ElemOrSelf<T> = match type T { case array => ElementOf<T>  case _ => T }`. `types` declares the five that take a type apart or build one where `match type` cannot: `ElementOf<T>` is the element type of an array or a set, `PayloadOf<T>` the `U` of `U?` or `U!`, `KeyOf<T>` and `ValueOf<T>` the key and value types of a map, and `WithElement<A, E>` the array type `A` with its element type changed to `E` and its shape kept (the result of `f32(xs)` is `WithElement<A, f32>`). A call is a type like any other: `is_integer::<ElementOf<T>>()` is a type argument, and `func first::<T>(xs: T): ElementOf<T>` has a result type that is one. While `T` is a type variable the call waits; when the instance binds `T` it is replaced by the type it computes, and a type the function does not accept (`ElementOf<int>`, or a `match type` no case selects) is `SemanticDiag::TypeFunctionNotApplicable`, at the type when it is written with concrete arguments and at the call that makes the instance otherwise.

**Type packs.** `ParamsOf<F>` and `WithOf<F>` are the ordinary and the `with` parameters of a function type, as a type pack; `ResultOf<F>` is its result type. `..` written before a type expression expands it once for each member of the packs it uses: `(..ParamsOf<F>)` is the tuple of the parameters of `F`, and `(..ElementOf<A>)` takes the element type of each member of `A`. The packs one expansion uses must have the same length, else it is `SemanticDiag::TypeFunctionNotApplicable`, whose message names each pack and its length. A declaration writes `::<..A>` at its head and its rest parameter as `..xs: ..A`; the call binds `A` to the pack of the types of the rest arguments, and `when ..pred::<A>()` says that every member of the pack satisfies `pred`. `zip` is declared this way: `zip::<..A>(..xs: ..A): (..ElementOf<A>)[] when ..is_array::<A>()`.

```cml
func duplicate::<T>(t: T) {
    return match type T {
        case struct => (t, t)
        case tuple  => (t, t)
        case #leaf  => (t, t)
    }
}
```

### 6.8.4 `static for` and `t.[n]`

`static for n, F in fields T => block` expands the **field list** of `T` when the function is instantiated (design principles 5.5): the block runs once per field, and the expanded code becomes straight-line code at that position. The graph has no loop node.

- `T` must be concrete and an [R3 inner node](../../design/generic-structure/README.cn.md): a struct or an **ordinary** tuple. `T?`, `T!`, a mutable cell, a scalar, an array and a function are leaves and report `SemanticDiag::StaticForNotComposite`; handle them with `match type` first.
- `n` is the field name; for a tuple it is the decimal index (`"0"`, `"1"`, ...), in declaration order. `F` is the field type. Both are visible in the block only; `F` may be written in a type position (`let x: F`) but is not a value. Writing `_` instead of either name binds nothing for that round, as `_` does in `match`: `static for n, _ in fields T => e` takes the field name only, `static for _, F in fields T => e` the type only (`_` is not a name, so writing it in the block stays undefined).
- The type after `fields` is a type position too: a `(` there is always the start of a type, so the empty tuple is written `fields ()` without a `type EmptyTuple = ()` first; a function type takes one more pair of parentheses, `fields ((x: int) => int)`.
- Fields are expanded in declaration order, each with its own block scope. An empty struct or tuple expands zero times and the block is not built at all.
- As a statement, `static for` always drops the value of the block, and the construct has type `void`; where a value is needed, write the comprehension form (below).
- `t.[n]` takes a struct field or a tuple element; `n` must be a compile-time constant: a string or integer literal, the `n`/`j` of an enclosing `static for`, a `let` name bound to a constant (`let first = ...; t.[first]`; a `var` does not count, since it can be assigned another key before the access), an expression whose value is known at compile time (such as `( static for n, F in fields S => n ).[0]`), or a call of a pure function whose arguments are compile-time constants (`t.[first_field::<S>()]`), which the compiler folds to its value. A call whose body cannot be read at build time is refused by its own name (`SemanticDiag::FoldCallNotEvaluable`, and the other `FoldCall*` diagnostics for the reason it stopped). Only the selected field is built into the graph (one `ACCS`); no field is chosen at run time, so a run-time value cannot be the key (`SemanticDiag::StaticAccessNotConstant`).
- A struct without that field reports `SemanticDiag::StaticAccessUnknownField`; a tuple index past the end (or a key that is not an index) reports `SemanticDiag::StaticAccessIndexOutOfRange`; a target that is not a struct or an ordinary tuple reports `SemanticDiag::StaticAccessNotInternal`. For a tuple, `t.["0"]` and `t.[0]` are the same element.
- When the field's value is dropped (a `t.[n];` statement on its own), the field is checked but no `ACCS` is built; the target expression is still evaluated, so its effects remain.

```cml
func show::<T>(t: T) {
    static for n, F in fields T => {
        print(n, t.[n])
    }
}

func first(t: (int, float)) {
    return t.[0]
}
```

**`indices`: expanding by tuple position.** `static for j in indices ts => block` expands the **positions** of the tuple value `ts` when the function is instantiated: the block runs once per position, `j` is this round's position (an `int`), and the trip count is the tuple's arity.

- `ts` must be of tuple type (an ordinary tuple; `T?`, `T!` and a mutable cell are leaves). A type that is not a tuple reports `SemanticDiag::StaticIndicesNotTuple`; a type that is not concrete reports `SemanticDiag::StaticTypeNotConcrete`. The count is decided by `ts`'s type at instantiation.
- **The operand only supplies its type.** When the operand is a compile-time tuple (a rest parameter's `ts`, or a tuple comprehension), no container node is built: `indices ts` builds no `FILL`, and `ts.[j]` in the body is the element node itself, with no `ACCS`. A runtime tuple variable or parameter already has its own node; `indices` does not build another.
- `j` may be used as a value (`print(j)`, or in arithmetic) and as the key of `t.[j]`: it is a compile-time constant in every round. A local of the same name still shadows it, and using that as a key reports `SemanticDiag::StaticAccessNotConstant` (the same rule as `n` of `fields`).
- `indices` binds one name only (there is no `F`); the element types come from `ts`'s type and are not given a name yet. `static for j, F in indices ts` is a syntax error.
- `indices` walks tuples only: a struct has field names, so use `fields T`. `{ static for j in indices ts => e }` has no field names to give and reports `SemanticDiag::StructComprehensionWithoutFields`; where a value is needed, write `( static for j in indices ts => e )`.
- The graph is the hand-written expansion: the generator leaves no node of its own. `ts.[j]` over a compile-time tuple is the element node itself, the same field selection `ts.[0]`, `ts.[1]` make; over a runtime tuple value it still builds one `ACCS`.

```cml
func sum(t: (int, int, int)): int {
    return static loop acc = 0 for j in indices t do acc + t.[j]
}

func positions(t: (int, string)): (int, int) {
    return ( static for j in indices t => j )
}
```

**Comprehensions: packing the rounds back into the same shape.** A `static for` may also be written where a value is needed: `{ static for n, F in fields T => e }` gives a struct and `( static for n, F in fields T => e )` gives a tuple, with the fields or elements of `T`, in the same names and order, each from one round's value.

- The **value of the block is the round's value**: unlike the statement form, it is not dropped. The block may use `let`, and its last statement's value is the field's value; a block that gives none (an empty block, or one ending in a statement without a value) reports `SemanticDiag::StaticComprehensionVoidElement`.
- Each field's or element's **type comes from that round's value**, not from `F`: with `F` an `int` and the value a `float`, the result field is a `float`. The field names and their order still come from `T` in declaration order, which is what lets an expansion such as `tree_map` change the leaves and keep the shape.
- **The same shape is written `T { ... }`**: `T { static for n, F in fields T => e }` takes both the field names and the kind (struct or tuple) from the one type `T`, so one `case struct | tuple` branch is enough. `T` must be the type the generator walks, or the comprehension reports `SemanticDiag::SameShapeComprehensionMismatch`; a leaf `T` (no fields) reports `SemanticDiag::SameShapeComprehensionNotComposite`. `{ ... }` and `( ... )` still pack a struct and a tuple only.
- An empty struct or tuple has no round; the value is `{}` / `()` (a `FILL` with no slot).
- The graph is straight-line code: each round builds the `ACCS` it reads, in declaration order, and the comprehension leaves no node of its own. A tuple comprehension first stays a compile-time tuple: used only by `indices`, `..` or `.[j]`, its elements stay separate and no `FILL` is built; used as a value (printed, returned, assigned to a runtime position), one `FILL` at the use packs the rounds' values into the same shape. A struct comprehension always packs one `FILL`. In `t.[n].[m]` the parent is read once and all inner fields share that `ACCS`, so the graph is the one the C++ `tree_map` builds for the same tree.
- `{ static for ... }` in a position that needs a block (`case struct => { ... }`, `if c then { ... }`) is a comprehension too, so the statement form is not wrapped in another pair of braces: write `static for n, F in fields T => block`.
- To fold the rounds' values into one result (rather than pack them back into the same shape), use the `static loop` of [6.8.5](#685-static-loop).

```cml
func doubled(t: {a: int, b: int}): {a: int, b: int} {
    return { static for n, F in fields {a: int, b: int} => t.[n] * 2 }
}

func rebuild::<T>(t: T) {
    return match type T {
        case struct | tuple => T { static for n, F in fields T => rebuild(t.[n]) }
        case _ => t
    }
}
```

### 6.8.5 `static loop`

`static loop state = init, ... for n, F in fields T do expression` is the fold form of `static for`: the field list is expanded when the function is instantiated, each field advances the state one round, and the graph has no loop node.

```
static loop name = init (, name = init)* for name (, name)* in (fields type | indices value | sequence) do expression

head : n, F in fields T       // by field: n is the field's name, F its type
     | n, F, tags in fields T // three names: the field's tags as well
     | j in indices ts        // by tuple position: j is the position (an `int`)
     | x in sequence          // by the sequence's elements, see below
```

The head is the same one `static for` writes (one thing, one spelling): both are the ordinary `loop`'s `for name in sequence` part, without parentheses; `fields` and `indices` bind the names they bind there, as below.

- The head is the same as the `loop` of [6.8.6](#686-loop): with one name the loop's value is that value; with several names the body gives a tuple in the order written and the loop's value is that tuple. The state names are visible in the body only; the first values are evaluated once where the loop is, and the state names are not visible in them. A name may not be repeated (`SemanticDiag::LoopNameRepeated`).
- Each round's body value is the next state, and its type must be the state's type (except for the implicit conversions, an `int` into an `int?`); otherwise it is `SemanticDiag::LoopBodyTypeMismatch`. An empty struct or tuple has no rounds and the value is the first value.
- The walk is written like the head of the `loop` of [6.8.6](#686-loop), without parentheses: `for n, F in fields T` (by field), `for j in indices ts` (by tuple position, with the rules of `indices` in 6.8.4), or `for x in sequence` (by the sequence's elements). There is no `while` and no `break`/`continue`. When it is a type parameter of an enclosing generic, the trip count is decided by the type at instantiation — that is the count known at instantiation.
- The sequence must be worked out at compile time, decided by **one rule**, not by a list of accepted spellings: when the expression after `in` is built only from compile-time constants (literals, names bound to constants, the round variables of an enclosing `static` expansion) through operations that can be **freely scheduled** (pure and unable to fail, the one predicate of design principles 3.4), the compiler works it out and unrolls it. So all of these walk: a constant interval `a..b` (half-open, the reading `..` has everywhere, so `0..5` is 0…4; both ends are `int`, and `..` builds no float or character interval), `range(a, b, step)` (all three arguments constant, the step must be written out, the rounds follow the interval rule of design principles 4.2, and a step of 0 is `SemanticDiag::RangeStepZero`), an array literal or a constant array (by element), and a compile-time tuple `ts` (by element: each round binds the **element** at that position, `ts.[i]`, and its type is the type of the i-th position — which is exactly what tells a `static loop` from an ordinary `loop`: an ordinary `loop` gives every round an element of one type, while here it varies with the position; write `typeof x` where the type is wanted, or `fields T` to get name and type together; `indices ts` walks positions and gives an `int`). `fields T` / `indices ts` follow the rules of 6.8.4 (`T` / `ts` must be a concrete struct or ordinary tuple); what the compiler cannot work out is `SemanticDiag::StaticLoopSequenceNotKnown`, naming the expression that was written — a count that is not fixed (a sequence given by a parameter, say) is that case. There is no `for` over a runtime sequence.
- How many names the head may write is decided by the element of the sequence: a head may name as many of the element's parts as the element has — the element of `fields T` has three parts (the field's name, its type and its tags), so `n, F` and `n, F, tags` are both legal, while the element of `indices ts` and of an ordinary sequence has one, so a second name there is `SemanticDiag::StaticHeadNameCount`. Naming fewer parts is legal (`for n in fields T` takes the field's name alone); naming more is that diagnostic; the names are read positionally, so `_` takes a position like any other name.
- It expands to straight-line code: each round builds the `ACCS` it reads, in field order, the previous round's state is directly the value the next round reads, and `static loop` has no node of its own. A `return` in the body leaves the function, as in an ordinary function; it does not leave the loop.
- Its effects are those of straight-line code: whether the body is a sync block is decided by the body itself (3.2), and each round's effects run in field order.

```cml
func count::<T>(t: T): int {
    return static loop n = 0 for name, F in fields T do n + 1
}

func sum3(t: (int, int, int)): int {
    return static loop acc = 0 for i in 0..3 do acc + t.[i]
}

func total_and_max(t: {a: int, b: int}): (int, int) {
    return static loop total = 0, best = 0
        for n, F in fields {a: int, b: int} do {
            (total + t.[n], if best < t.[n] then t.[n] else best)
        }
}

func sum_to(): int {
    let n = 4
    return static loop acc = 0 for i in 0..n do acc + i
}

func stepped(): int {
    return static loop acc = 0 for i in range(0, 10, 3) do acc + i
}

func from_array(): int {
    let xs = [1, 2, 3]
    return static loop acc = 0 for x in xs do acc + x
}

func sum_args(..ts): int {
    return static loop acc = 0 for x in ts do acc + x
}
```

### 6.8.6 `loop`

A loop is an expression with a state (design principles 3.10). The state is written in the head of the loop: each name is declared once with its first value. The expression after `do` is the body; its value is the state of the next round, and the value of the whole loop is the last state.

```
loop name = init (, name = init)* (for name in sequence)? (while condition)? do expression
```

<!-- camel-snippet: fragment: the loop forms; xs, n and ds are the surrounding example's values -->

```cml
let total    = loop acc = 0 for x in xs do acc + x * x
let (i, acc) = loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)
let p        = loop p = { x: 0 } for d in ds do { x: p.x + d }
let found    = loop hit = -1 for x in xs while hit < 0 do if x > 9 then x else hit
```

- **State.** With one name the body's value and the loop's value are that value; with several names (separated by commas) the body gives the tuple of them in the order of the head, and the loop's value is that tuple, taken apart with `let (a, b) = loop ...` or `.0`, `.1`. The state has the type of its first value; the body's value must have that type (or enter it, as an `int` enters an `int?`), otherwise `SemanticDiag::LoopBodyTypeMismatch`. `null` and the empty array `[]` have no type of their own as first values: write `null as T?`, or bind the empty array with its type first (`let none: int[] = []`) and start from that name (`SemanticDiag::LoopStateWithoutType`).
- **`for x in xs`** takes one element of the sequence `xs` per round, in order; `xs` is an array (a tensor is a numeric array), a string or a map, and any other type is `SemanticDiag::LoopSequenceNotIterable`.
  - **Array**: the element itself; `a..b` is one.
  - **String**: by code point, each element the one-character string of that code point, as `s[i]` gives it (11.1): the two rounds of `for c in '中文'` are `'中'` and `'文'`, and a multi-byte character counts once.
  - **Map**: the `(key, value)` pairs in key order (the traversal order of 11.3, as `items(m)` gives them), not insertion order: in `for kv in m` the keys are strictly ascending, and `kv.0` and `kv.1` take the pair apart.
  - **Tensor**: a tensor is a numeric array (`f32[2, 3]` is one such array type), and it is walked by the array's own rule: along the first dimension, each element the sub-array with the first dimension dropped (sharing that slice of the data, not copied): a rank-2 `f32[2, 3]` gives two `f32[3]`, and a rank-1 array gives the element itself (`sum(x)` is its number). An array whose element type is not written gives elements of that same type; the element type of an `f32[..]` whose rank is unknown is still `f32[..]`. A rank-0 array has no first dimension and is not a sequence.
  An empty array, empty string, empty map, or an array whose first extent is 0 runs no round, and the loop's value is the first state. **`while c`** goes on as long as the condition holds; the condition is a `bool` (`SemanticDiag::LoopConditionNotBool`). With both, each round looks at the condition first and then takes the next element: the loop ends when the condition fails or the elements are used up. At least one of them is written.
- **No `break` and no `continue`**: ending early is written as the `while` condition, skipping the rest of a round as an `if`.
- **Scope.** The first values and the sequence are evaluated once, where the loop is, and do not see the names of the head; the condition sees the names of the state, the body those and the name of `for`. A name of the head is given once (`SemanticDiag::LoopNameRepeated`). The condition and the body see the names around the loop (a `var` is read anew in every round).
- **After `do`** comes an expression, read as after `then`: `{ x: 1 }` is a struct, `{ 5 }` and `sync { ... }` are blocks. A `return` in such a block ends the block with the next state; it does not leave the function.
- **Effects.** Whether the body is a sync block is up to the body (3.2). Written `do sync { println(x); acc + x }`, its effects run in order every round. A loop in a block that is not a sync block, whose value nothing uses, does not run, with `EffectNotExecuted`, as for an unused call.
- **Order and depth.** The rounds run strictly one after the other. A loop is not a recursion: no number of rounds takes call depth, `--max-recursion-depth` does not count them, and a loop that does not end runs on.
- **Numeric permissions.** Permission tags (`#reassociate`, `#contract`, ...) on a function or a block cover the bodies and conditions of the loops written in it (7.2, 7.5); `#strict` covers them too.
- `loop`, `while`, `do`, `for` and `in` are keywords.

On the graph a loop is one `LOOP` node in the graph it is written in; the condition and the body are a subgraph each, sharing one table of captured values (design principles 3.10; `docs/design/graph-model` 3.5). A string and a map are turned into their array of elements (the code points, the key-value pairs) once, where the loop is; a tensor is already a numeric array, so nothing is converted and the `LOOP` node iterates it directly. The `LOOP` node iterates an array, which is the only form the back ends and the lowering handle.

## 6.9 Lambdas and function types

A lambda is written `(parameters): return-type => body`; the return type can be omitted, and the body is an expression or a block:

<!-- camel-snippet: fragment: a list of lambda forms; total comes from the surrounding example -->

```cml
(x: int) => x + 1
(x: int): int => { let y = x * 2; return y + 1 }
(x) => x + 1                                    // the parameter type comes from where it stands (below)
(s: string) => sync { println(s) }              // a sync block as the function body
(x: int) => sync { total = total + x }          // writes a captured variable: the body is a sync block
```

A function type is written `(x: T) => R`, and the parameters must be named: `(x: int) => int`, `(acc: int, x: int) => int`.

**Where a parameter type comes from.** A lambda parameter whose type is not written takes it, and an omitted return type too, from the function type of the position the lambda stands in. There are three such positions:

- An argument: a call of a function whose parameter is of function type, and a `with` argument in the same way. The built-in functions are the same: their signatures are written in the standard library's `stdlib/prelude.cml`, so the `x` of `xs->filter<(x) => x > 0>` and of `xs->foreach<(x) => println(x)>` is the element type of `xs`. A lambda written right after `->` takes the type of the value on its left (`x->(v) => v + 1`).

  <!-- camel-snippet: fragment: a call shown inside the block it would be written in -->

  ```cml
  func apply_twice(f: (x: int) => int, x: int): int { return f(f(x)) }
  apply_twice((x) => x + 1, 3)         // x: int
  ```

- A declared binding: a `let` or `var` whose type is a function type, and an assignment of a new value to that variable.

  <!-- camel-snippet: fragment: bindings shown inside the block they would be written in -->

  ```cml
  let g: (x: int) => int = (x) => x + 1    // x: int
  var h: (x: int) => int = (x) => x - 1
  h = (x) => x + 100                       // x: int
  ```

- A returned result: the result type of the function is a function type.

  ```cml
  func adder(n: int): (x: int) => int {
      return (x) => x + n              // x: int, and the result is int
  }
  ```

- The lambda after `->`: the parameter is the receiver, so its type is the receiver's, or the value taken out of a `T?` or a `T!` (the pipeline of 3.9):

  ```cml
  func main(): int sync {
      let a = 5 -> (y) => y + 1                  // y: int
      println(a)
      return 0
  }
  ```

A parameter that the expected type does not determine is an error (`SemanticDiag::LambdaParameterWithoutType`, located at that parameter): the lambda does not stand in such a position (`let g = (x) => x + 1`, `((x) => x)(1)`), the expected type is not a function type, the number of parameters differs, or the expected type still holds a type variable that this position does not bind. With no expected type it does not become an `any` (5.10): write the type.

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
- A binding with a function type, `let f: (x: int) => int = inc`, checks its value against the type written; a lambda's parameter types can come from that type too (above).
- The inference does not change overload resolution: a typed lambda still picks the overload its own type picks, and an untyped one facing a group of overloads has no single expected type, so it is the error above rather than a choice of overload.
- A generic higher-order function (`tree_reduce`, `map`: a parameter type that mentions a type variable) does not infer yet: the expected type of the argument waits for the generic instantiation, and the lambda is always lowered before it. Write the parameter types there (`tree_reduce<(acc: int, x: int) => acc + x>(...)`); this is still undecided.

## 6.10 Higher-order operators

| Form | Result | Order |
|---|---|---|
| `xs->map<f>` | A new array of each element passed through `f` | No order between elements, may run in parallel; results arranged by position |
| `xs->filter<p>` | The elements for which `p` is true | Same as above |
| `xs->reduce<f, init>` | The final value of `f(acc, x)` applied in turn, starting from `init` | In element order |
| `xs->foreach<f>` | None; calls `f` for each element | In element order |

```cml
func main(): int sync {
    let xs = 1..6
    println(xs->map<(x) => x * x>)
    println(xs->filter<(x) => x % 2 == 0>)
    println(xs->reduce<(acc: int, x: int): int => acc + x, 0>)
    xs->filter<(x) => x > 3>->foreach<(x) => sync { println('item', x) }>
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

### 6.10.1 Fault isolation `attempt`

Errors and faults are two different things: only an operation declared to return `T!` produces an error value; division by zero, an out-of-range subscript and too deep a recursion are faults: the program terminates with a location, and `!!` cannot catch one. `attempt` is a **function transform**: `attempt(f)` gives a function whose parameters are those of `f` and whose result type is the result of `f` plus `!`, and calling it is `attempt(f)(a, b)`. There is no `try`/`catch`: the boundary must be one function call and not a stretch of written text — the computation on the graph is evaluated on demand, moved by passes and run in parallel, and only the boundary of a subgraph is stable. A fault in that call (including everything it calls, directly or indirectly) becomes an error value at the boundary, to be handled with `!!`, by carrying it on with `->`, or with `match ... case error e`. Wrap any expression in a closure: `attempt(() => expr)()`. The value is a function value like any other: it can be passed on and written in angle brackets, `rows->map<attempt(parse_row)>`.

```cml
func risky(d: int): int { return 100 / d }

func main(): int sync {
    println(attempt(risky)(4) !! -1)
    println(attempt(risky)(0) !! -1)
    println(attempt(() => [1, 0, 4]->map<risky>)() !! [-1])
    let r = attempt(risky)(0)
    println(match r {
        case error e => e.kind + ' at ' + e.location
        case v => 'no fault'
    })
    return 0
}
```

```text
25
-1
[-1]
DivisionByZero at main.cml:1:38
```

- Type: `f: (A...) => T` gives `(A...) => T!`; when `f` itself returns `U!` the result is `U!` (the error value it returns comes out unchanged). A function with `with` parameters or a `var` parameter cannot be handed in directly — the marked call passes no `with` argument and gives the parameters values — so use a closure.
- **Three boundary rules**:
  - **the result is finished inside the boundary**: the result of `f`, the fields of a struct and the elements of an array included, is evaluated completely inside it, and the faults in them count;
  - **a stream is isolated pull by pull**: when `f` returns a stream, every pull happens inside the boundary, the element type becomes `T!..`, and a pull that faults hands out an error;
  - **effects are not rolled back**: the effects that happened before the fault stay (lines printed, `var`s written, files written), and there are no transactions on the graph.
- The `kind` of the error value is the name of the fault (such as `DivisionByZero`, `IndexOutOfRange` or `MaxRecursionDepthExceeded`), `message` is the message of the fault, and `location` is the `file:line:column` where it happened.
- The boundary is that one call: the arguments are evaluated outside it.
- `exit(n)` is not a fault and is not isolated; running out of memory and the errors of the compiler itself are not isolated either.
- Under the parallel scheduler, when one element of a `map` faults, the elements already started run to the end and those not yet started do not start; `attempt` gives the error value after all that were started have finished.
- On the graph the value holds a small graph of its own whose body is one `OPER` node (`:mark/isolate`), whose first input is the captured `f` and whose other inputs are the arguments. It is a marked call of a subgraph, so its structure does not change with what the subgraph contains. For the implementation and the questions still to be ruled see the [technical note](../../technical/33_fault_isolation.md).

## 6.11 Assignment

- `x = e`, `x += e` and so on are expressions; their value is described below. The target is a `var` variable or a `var` parameter itself, or a **path** that starts at one: fields, indices, any combination ([3.7](03_data_model.md#37-let-var-and-references)). `p.x = e`, `xs[0] = e` and `m[i].f = e` all rebind the whole of `p`, `xs`, `m`: `xs[i].f = e` is `xs = set(xs, i, set(xs[i], f, e))`, where `set` is a pure operator that returns the value with one place changed. A path assignment may therefore appear in any expression position; when the root of the target is not a variable (`f().x = e`, `{ x: 1 }.x = e`) the diagnostic is `AssignmentTargetNotVariable`, and an index out of range is the fault `IndexOutOfRange`.
- The value of a path assignment is the **whole value rebound**: `xs = xs->append<…>` gives the new array, and `xs[0] = 5` gives that `xs` as it is now, not `5`. Only when the target is the variable itself is the value the value assigned.
- In a `sync` body, writing one element of a `var` array **does not copy** it: the store back asks the same "not shared" criterion as `xs = xs->sort` ([10.5](../../design-principles.en.md#105-library-boundary)), that is `:var/take` hands out a copy only after the variable has been read elsewhere. So after `let a = xs`, writing `xs[0]` copies and `a` stays as it was; when `xs` has not been read, its own storage is written.
- Types for which compound assignment is available:

  | Operator | Types |
  |---|---|
  | `+=` | The four real numeric types, `c64`, `c128`, `string` (concatenation) |
  | `-=`, `*=`, `/=`, `^=` | The four real numeric types, `c64`, `c128` |
  | `%=` | `int`, `i32` |
  | `&=`, `\|=` | `bool` (`a = a & b`, `a = a \| b`: the right side is always evaluated) |
  | `@=` | None of the built-in types |

- **A compound assignment computes the new value first, then rebinds.** `x op= e` computes the new value and binds `x` to it, so numerically it is `x = x op e`. A compound assignment on a path (`xs[0] += 1`, `p.n += 1`) writes the new value back to that place, and the write back follows the in-place rule above: after `let a = xs`, `xs[0] += 1` copies and `a` is unchanged. A type defined by a module (a tensor), and a type for which the program overloads an operator ([7.4.5](07_statements.md#745-operator-overloading)), has no compound-assignment operators of its own, so for it `t += u` is `t = t + u` (likewise `-=`, `*=`, `/=`, `%=`, `@=`, `^=`, `&=`, `|=`, wherever the binary operator is defined for the types).

## 6.12 Operator precedence

From lowest to highest:

| Level | Operators | Notes |
|---|---|---|
| 2 | `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | Cannot be chained |
| 3 | `\|\|` | Left-associative; a branch, the right side evaluated only when needed |
| 4 | `&&` | Left-associative; a branch, the right side evaluated only when needed |
| 4a | `\|` | Left-associative, both sides evaluated |
| 4b | `&` | Left-associative, both sides evaluated |
| 5 | `==` `!=` `===` `!==` | Whole and element-wise comparison (see [6.7.3](#673-comparison)) |
| 6 | `<` `>` `<=` `>=` | |
| 6a | `..` | Range (between numbers) and function composition (between functions); looser than `+` and `-`, so `1..n+1` is `1..(n+1)` |
| 7 | `+` `-` | Left-associative |
| 8 | `*` `/` `%` `@` | Left-associative |
| 8a | `^` | Right-associative, one level above the multiplicative operators |
| 9 | `??` `!!` | The right side is a complete expression |
| 10 | `as` | |
| 10a | Unary `-` `!` `~` | Looser than `->` and `^`: `!xs->is_empty()` is `!(xs->is_empty())`, `-a->f` is `-(a->f)`, `-a ^ 2` is `-(a ^ 2)`; a literal and a name read alike (`-1->abs` is `-(1->abs)`; to negate first write `(-1)->abs`) |
| 11 | `->` | Left-associative |
| 13 | Postfix: `.field`, `[subscript]`, `(arguments)`, `<with arguments>` | Must directly follow |

`if` and `match` can appear wherever an expression is required; their arms extend to the end of the expression, so add parentheses when they are embedded in a larger expression.
