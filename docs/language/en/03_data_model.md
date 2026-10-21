# 3. Data model

English | [中文](../03_data_model.md)

This chapter describes Camel's types and values: basic types, compound types, `let` and `var`, the types that may be null and may fail, maps and sets.

---

## 3.1 Overview of types

Every expression has a definite type at compile time; the graph contains only concrete types, and there is no type information at run time (a program cannot ask at run time what type a value has).

| Type | Form | Notes |
|---|---|---|
| Integer | `int` (that is, `i64`), `i32` | 64-bit and 32-bit signed |
| Floating-point | `float` (that is, `f64`), `f32` | Double and single precision |
| Complex | `c64`, `c128` | Single and double precision complex: a real and an imaginary floating-point part |
| Boolean | `bool` | `true`, `false` |
| String | `string` | Sequence of bytes, immutable |
| Array | `T[]` | Elements of the same type; length determined at run time |
| Tuple | `(T1, T2, ...)` | By position, accessed with `.0`, `.1` |
| Struct | `{ x: T1, y: T2 }`, or the name a `type` declares | By name, accessed with `.x`; `type Name = { ... }` declares a named type, see 3.3.3 |
| Function | `(x: T) => R` | See [6.9](06_expressions.md#69-lambdas-and-function-types) |
| May be null | `T?` | See 3.4 |
| May fail | `T!` | See 3.4 |
| Map, set | `Map<K, V>`, `Set<T>` | See 3.6 |
| Enum | `Shape`, `Option<int>` | See 3.5 |
| No value | `void` | Return type of a function that returns no value; its only value prints as `null` |
| Module types | e.g. `Tensor`, `Tensor<f32, const [2, 3]>` | Introduced by modules; the arguments in angle brackets are interpreted by the type itself |

There is no type `any`: after compilation every value has a definite type (design principles 5.10). Writing `any` in any type position is, like any other unknown type name, the compile-time error `SemanticDiag::UnresolvedTypeReference`; a parameter that takes several types uses a type parameter (see [7.5](07_statements.md#75-generic-functions)), and values of different types side by side go in a tuple or a struct.

Reading JSON: for a known shape use `json::decode_as(text, shape)`, whose result is a `T!`; for an unknown shape use `json::decode(text)`, which gives a `Json!`: an error value `JsonParseError` when the text is not JSON, and `JsonTooDeep` when the document nests deeper than the limit (neither is a fault, design principles 11.2; the limit is 10000 by default and `CAMEL_JSON_MAX_DEPTH` sets it), otherwise a `Json`, whose type is definite and whose content is known only at run time; take parts out with `get`, `as_int`, `as_array` and the like, each of which gives a value of a definite type (null when there is none) and also takes a `Json?`, `Json!` or `Json?!` so that calls chain, as in `j->get<'server'>->get<'port'>->as_int ?? 8080`; an accessor of a `Json!` gives the two-layer result (`Json?!` and the like) with the error of the document passed on unchanged, taken apart with `??` and `!!`; `as_float` takes an integer too, and `get<index>` out of range gives `null` (the accessor design settled in 11.2).

The `T` and `U...` in the signatures of the standard library (as in `println: (x: T, ...rest: U...) => T`) are a notation of the reference only: these operators check their arguments with type rules, and the graph holds concrete types only.

Use `type` to give a type a name, see [7.7](07_statements.md#77-type-declarations):

```cml
type Point = { x: int, y: int }
type Pair<T> = (T, T)
```

## 3.2 Numbers

- **No implicit conversions between numbers.** Two *variables* of different types do not mix (`i32` with `int`, `int` with `float`), and `let y: float = n` with `n` an `int` is `SemanticDiag::DeclaredTypeMismatch`: both are compile-time errors (`SemanticDiag::NoMatchingFunction` for the former). Convert explicitly where needed. **Text is not a number either**: there is no implicit text-to-number conversion, so `let x: int = '42'` is `SemanticDiag::DeclaredTypeMismatch` (the message says a `string` cannot be used as an `int`) and `'42' as int` is `SemanticDiag::DynamicCastForbidden` (`as` does not convert numbers). Read a number out of text with `int(s)` or `parse_int(s)`, and a float with `float(s)` or `parse_float(s)`. What moves with the context is a **literal**, not a variable: `1 + 2.0` is `1.0 + 2.0`, and the `1` in `let x: f32 = 1` is an `f32` (see the end of this section and design principles 5.1).
- Conversion functions: `int(x)`, `i64(x)`, `i32(x)`, `float(x)`, `f64(x)`, `f32(x)`, `c64(x)`, `c128(x)`, `string(x)`. `c64` and `c128` take a real type or the other complex type: a real becomes the real part with an imaginary part of 0, and a `c128` becomes a `c64` with both parts rounded to f32. A complex value does not become a real one by itself and there is no conversion function for it; use `re()` and `im()`.
- Arithmetic on complex numbers: `+`, `-`, `*`, `/` and unary `-`, by complex arithmetic (`i * i` is `-1+0i`); `==` and `!=` compare both parts. There is no ordering and no `%`: complex numbers have no order. `^` is defined (an integer exponent is exact; see the end of 3.2). `abs(z)` is the magnitude (`f32` for a `c64`, `float` for a `c128`), `re(z)` and `im(z)` are the two parts, `conj(z)` is the conjugate, and `exp(z)` is the complex exponential; they come from the `math` module. A complex number cannot be a `Map` key or a `Set` element (no order). The printed form and the format specifications are in [2.5.3](02_lexical.md#253-imaginary-literals).
- `i64` and `f64` are only other spellings of `int` and `float`. Diagnostics, graphs (`std::rgir`, `inspect`) and editors always show `int` and `float`: an instance of a generic function is `first<int>`, not `first<i64>`.

  | Form | Result |
  |---|---|
  | `float(3)` | `3.0` |
  | `int(3.99)`, `int(-3.99)` | `3`, `-3` (truncation towards zero) |
  | `int(1.0e30)`, `int(nan)`, `int(inf)`, `i32(5000000000)` | The fault `RuntimeDiag::ConversionOutOfRange`: the value does not fit the target type (a float that is NaN, infinite, or whose truncation is outside the integer type; an `int` outside `i32`). The message gives the value and the target type |
  | `f32(1.0e39)` | `inf`: a float converted to a narrower float is rounded as IEEE says, and beyond the range that is an infinity, not a fault |
  | `int('42')`, `float('1.5')` | `42`, `1.5`. **The whole string must be one number**: it may carry one sign (`'-12'`, `'+12'`), an integer is decimal digits only, a float may have a fraction and an exponent (`'1e3'`, `'.5'`) or be `inf` or `nan`. Blanks around it (`' 7'`), anything after the number (`'12abc'`), `'1.5'`, `'0x10'` or `'1_000'` for an integer are the fault `RuntimeDiag::InvalidNumberText` (it is not a number) |
  | `int('99999999999999999999')`, `float('1e999')`, `float('1e-9999')` | They are numbers the target type cannot hold (overflow or underflow): the fault `RuntimeDiag::ConversionOutOfRange`, whose message quotes the text and gives the target and the range, distinct from text that is not a number |
  | `parse_int('42')`, `parse_float('x')` | `int!`, `float!`: read by the same rule; text that is not one number gives an error value (`ParseError`), not a fault (see 3.4) |
  | `string(42)`, `string(true)`, `string([1, 2])` | `'42'`, `'true'`, `'[1, 2]'` |
  | `string(3.5)`, `string(2.0)` | `'3.5'`, `'2.0'`: the text `println` and a `{}` of `format` write, the shortest that reads back as the same number ([2.5.2](02_lexical.md#252-floating-point-numbers)); `float(string(x))` gives `x` back |

- **A conversion whose argument is a literal is checked at compile time.** `int(1.0e30)` and `i32(5000000000)` are `SemanticDiag::LiteralNotRepresentable` (reason: out of range), `int(1.9)` is the same diagnostic (reason: the value would be truncated) and so is `int('12abc')` (reason: the text is not a number), while `int('99999999999999999999')` and `float('1e999')` are the same diagnostic with the reason out of range and the range, just as a literal declared with a type that cannot hold it is; the message quotes what was written and the reason. With a computed argument the faults of the table above still happen at run time.
- **A literal takes its type from the context** (design principles 5.1): in order, the expected type (a typed `let`/`var`, a parameter, a return position, a `match` pattern taking the scrutinee's type, a `return` inside a branch arm taking the expected type of the arm's position), the type of the other operand of a binary operation or comparison, then the default type (`int` for integers, `float` for decimals and exponent forms, `c128` for imaginary literals and folded values with an imaginary part). The elements of an array literal and the fields of a struct literal are such positions too, so `let xs: float[] = [1, 2.0]` is a `float[]` and the `1` in `let p: P = { a: 1, b: 2.0 }` (with `P = { a: float, b: float }`) is a `float`. With no expected type, the literals in one array take the widest kind (integer < decimal): `[1, 2.0]` is a `float[]`. `let a: i32 = 3`, `let b: f32 = 0.1`, `f(3)` with `f(x: f32)`, and `x * 2` with `x: f32` all give `3`, `0.1` and `2` the target type. An operation between literals is computed with its exact value before it is put into the type: `0.1 + 0.2` is the double nearest to 0.3, `1 / 2` is `0` (integer division), `1.0 / 2` is `0.5`, and `9223372036854775807 + 1` does not fit an `int` and is `SemanticDiag::LiteralNotRepresentable` (a run-time addition wraps around instead). `as` is not used for conversions between numbers (`1 as float` is a compile-time error); use the conversion functions above.
- **`0.1 + 0.2` and `let a = 0.1; a + 0.2` are not the same thing, and that is intended.** Between literals the sum is computed exactly at compile time, so `0.1 + 0.2` is `0.3` (the double nearest to 0.3), while in `let a = 0.1; a + 0.2` the value `a` is already a double and the two are added in IEEE double precision, which gives `0.30000000000000004`. They differ because the first has no "add two doubles" step: the exact sum `0.3` becomes the double `0.3` directly. For run-time floating-point behaviour, put the literal in a variable; for the exact decimal value computed at compile time, write the literals out (design principles 5.1).
- There is no `bool(x)`, and no conversion from `bool` to an integer; use a comparison to get a boolean (`n != 0`), and `if` to get an integer.
- **Integer arithmetic wraps around without an error**: `9223372036854775807 + 1` gives `-9223372036854775808`, but both numbers must be in variables for that (written between literals it is a compile-time error, see above). `i32(n)` truncates a computed `n` to 32 bits: `i32(3000000000)` gives `-1294967296`; as a literal it is `SemanticDiag::LiteralNotRepresentable` (`let b: i32 = 3000000000` likewise).
- **Integer division truncates towards zero**, and the remainder has the sign of the dividend: `7 / 2` is `3`, `-7 / 2` is `-3`, `-7 % 3` is `-1`. `%` is for integers only.
- **A zero divisor is a fault** (`RuntimeDiag::DivisionByZero`), for integers and floating-point numbers alike: `1.0 / 0.0` does not give infinity. Floating-point overflow gives `inf`.
- `^` is power: `7 ^ 2` is `49`, `2.0 ^ 0.5` is `1.41421`; a negative exponent on integers gives `0`. Complex numbers have `^` too: `z ^ n` with an integer type `n` is repeated squaring, so `z ^ 2` is bit for bit `z * z`, and a negative `n` takes the reciprocal (no fault, as a negative exponent on a real number does not fault); `z ^ w` with a complex `w` uses the platform's `pow` and promises only the class of accuracy the platform's real `pow` has (not correct rounding). A whole-valued complex exponent (imaginary part 0, `|real| <= 2^53`) also takes the repeated squaring, so `z ^ (2+0i)` is `z ^ 2`. `%` is never defined for complex numbers.

### 3.2.1 The numeric contract of floating point

The result of floating-point arithmetic is fixed by a **canonical algorithm**, not by the implementation of some machine (design principles 7.1):

- Addition, subtraction, multiplication, division and square root are each rounded correctly as IEEE 754 says, strictly in the order written, and `a * b + c` is not contracted into one fused multiply-add. Within this scope every back end and every device gives the same bits.
- **`max` and `min` are IEEE 754-2019 `maximum` and `minimum`**: a NaN on either side gives NaN, and `-0` is below `+0`, whichever side each value is on. This holds for `math::max`/`math::min`, the tensor `maximum`/`minimum`, the `max`/`min` reductions along an axis, max pooling and the maximum inside `softmax`. A maximum or minimum reduction gives the same bits in any order.
- **`relu(x)` is `maximum(x, +0)`**: `relu(NaN)` is NaN, `relu(-0)` is `+0`.
- **The canonical algorithm of matrix multiplication** is one chain of fused multiply-adds per element, in ascending k: `acc = +0`, then `acc = fma(a[i][k], b[k][j], acc)` for each k. A fused multiply-add is itself one correctly rounded operation, so this is reproducible bit for bit as well. `linear` and `x @ w + b` add the bias after the chain.
- **The definition of an operator is canonical**: `softmax(x)` is `exp(x - m) / sum(exp(x - m))` (`m` the maximum, the sum accumulated in f64 and rounded once to f32), and the fused implementation gives the same bits.
- Transcendental functions (`exp`, `log`, ...) are not required to give the same bits everywhere, only to stay within a stated error bound (`exp` and `log`: 2 ulp), with NaN, infinities and zeros exact. `camel inspect --ops` shows the semantics each operator declares.
- **Complex numbers**: `re`, `im` and `conj` are exact (reading one part and flipping one sign bit are not arithmetic); `abs` is `hypot` and `exp` is the platform's complex exponential (its magnitude is its `exp` and its direction its `sin`/`cos`, a composition of the two), so both promise only the platform's error bound (4 ulp), not correct rounding, and neither always propagates a NaN (`hypot(inf, nan)` is `inf`, `exp(-inf+nan i)` is `0`). `+`, `-`, `*` and `/` of complex numbers are the IEEE operations on the parts in the written order and agree bit for bit across backends (`/` faults when the divisor is the complex zero); `^` with an integer exponent is a fixed sequence of those multiplications, so it is reproducible too.

Faster computation whose results may differ is granted explicitly by the program, with permission tags (`#reassociate`, `#contract`, ...) on a function or a block ([7.6.1](07_statements.md#761-numeric-permission-tags)); `reassociate` and `contract` are the defaults of 7.3, and `#strict` takes every permission away. Without a grant, the canonical algorithm is used; there is no module-wide default and no command-line switch.

## 3.3 Compound values

### 3.3.1 Arrays

```cml
let xs = [1, 2, 3]
let names: string[] = []          // an empty array needs its type written out
let grid = [[1, 2], [3]]          // int[][]
println(xs[0], len(xs), grid[0][1])
```

```text
1 3 2
```

- **The elements are positions with an expected type** (design principles 5.1): the expected type is the array's element type, and a literal element takes it. `let xs: float[] = [1, 2.0]` and `let xs: float[] = [1, 2]` are both a `float[]`, and so is `[1, 2] as float[]`.
- With no expected type, the literals in one array take the widest kind (integer < decimal): `[1, 2.0]` is a `float[]`, that is `[1.0, 2.0]`.
- An element that **already has a type** (a variable, the result of a call) is not converted implicitly, and a different type is an error: `let n = 1; let xs: float[] = [n, 2.0]` and `let xs: float[] = [n]` are `SemanticDiag::ElementTypeMismatch`, reported at the element (`Cannot insert data of type 'int' into a(n) array with element type 'float'`). Elements whose types have no common type and are not literals are `SemanticDiag::ElementTypeMismatch` too.
- Subscripts start at 0; an out-of-range subscript (including a negative one) is the fault `RuntimeDiag::IndexOutOfRange`. Several positions at once are written `xs[idx]` with an `int[]` (that is `take`), a slice is written `xs[a:b]` (a view sharing the data block), see [6.3](06_expressions.md#63-access-and-subscripts).
- Built-in functions:

  | Form | Result |
  |---|---|
  | `len(xs)` | Length |
  | `head(xs)`, `tail(xs)` | The first element (of an empty array: the fault `RuntimeDiag::IndexOutOfRange`, as for `xs[0]`); the array without its first element |
  | `last(xs)` | The last element (of an empty array: the fault `RuntimeDiag::IndexOutOfRange`). A negative subscript does not count from the end, so use this for the last element, not `xs[-1]` |
  | `range(a, b)` | `[a, a+1, ..., b-1]`; empty when `a >= b` |
  | `xs->slice<a, b>` | A new array of subscripts `a` to `b-1`. `a` and `b` are positions from 0 to the length; a bound outside the array (a negative one included) is the fault `RuntimeDiag::SliceOutOfRange`, as an index out of range is a fault. With both bounds inside the array and `b < a` the result is empty. To take "at most n" use `take` and `drop` of the `array` module, which cut by definition |
  | `concat(xs, ys)` | Concatenation into a new array |
  | `xs->contains<v>` | Whether an element equals `v`, by the `==` of the element type (arrays, tuples and structs compare by content, see [6.7.3](06_expressions.md)) |
  | `zip(xs, ys)` | An array of `(x, y)` tuples; three or more arrays give tuples of that many elements (`zip(xs, ys, zs)`); the lengths must be equal |
  | `strs->join<sep>` | Joins a `string[]` into one string |
  | `map`, `filter`, `reduce`, `foreach` | See [6.10](06_expressions.md#610-higher-order-operators) |
  | `xs->append<v>`, `xs->extend<ys>` | A **new array** with `v` (or the elements of `ys`) at the end; `xs` is unchanged. To append to a variable write `xs = xs->append<v>` (see 3.7) |

- More array functions are in the `array` module, which has to be imported (`import { reverse, sum } from array`): `reverse`, `take`, `drop`, `enumerate`, `flatten`, `unique`, `index_of`, `find`, `any`, `all`, `count`, `sum`, `min`, `max`, `flat_map`, `sort_by`, `zip_with`; see the [standard library reference](../../stdlib-reference.md).

### 3.3.2 Tuples

```cml
let pair = (1, 'one')
let (n, s) = pair              // destructuring
println(pair.0, pair.1, n, s)
```

```text
1 one 1 one
```

A member index is a literal; one beyond the length is a compile-time error (`SemanticDiag::InvalidAccessIndex`). Indices of nested tuples chain: `t.0.1` is `(t.0).1`. A one-element tuple is written `(x,)`, the empty tuple `()`.

### 3.3.3 Structs

```cml
type Point = { x: int, y: int }

let p: Point = { x: 3, y: 4 }
let q = { x: p.x + 1, y: p.y }     // "modifying" means building a new one
println(p, q.x)
```

```text
{ x: 3, y: 4 } 4
```

A struct type is written in one of two ways, and they are two different kinds of type (design principles 5.2):

- **An anonymous struct type** writes its fields, as in `{ x: int, y: int }`. Its identity is exactly what is written: the field names, their types and their **written order** are all part of the type, so `{ x: int, y: int }` and `{ y: int, x: int }` are two different types (`==`, overloads and `Map` keys follow this identity).
- **A named struct type** is declared by `type Point = { x: int, y: int }`. Its identity is **the declaration itself** (the module that declares it plus the name, plus the type arguments for a generic type). `Point` and another `type Spot = { x: int, y: int }` with exactly the same fields are two different types; printing, diagnostics and graphs show the name, `Point`. `type P = Other` is not such a declaration — it is only another name for `Other`.

A literal must give all the fields the type requires. The fields of a declared type are in the order the declaration writes them: they are accessed by name, printed in declaration order, and that is the order of the value's layout. An anonymous struct type keeps its written order.

A value of an anonymous struct type may be used directly where a named struct type is expected (the shapes match):

- when the field names and types correspond one to one (the order may differ) it *enters* that type: it is copied field by field when a copy is needed, and the resulting value has the named type;
- when the field sets do not correspond (a field is missing, extra, or has the wrong type) it is a compile-time error — `SemanticDiag::DeclaredTypeMismatch` at a declaration, `SemanticDiag::NoMatchingFunction` at a call.

Nothing converts between two named types implicitly, even when their fields are identical: `let q: Spot = p` is a compile-time error, and `p as Spot` is required. `as` matches the fields by name; when the field sets do not correspond it is `SemanticDiag::DynamicCastForbidden`, and when they correspond but in another order the value is copied into the new order (no copy is made when the order is the same). A generic parameter does not become more permissive because of this: `Box<Point>` and `Box<{ x: int, y: int }>` are different types (design principles 5.8).

### 3.3.4 Strings

A string is immutable text and is **always valid UTF-8**; length and subscripts count code points: `len('中文')` is 2 and `'中'[0]` is `'中'`. Binary data is the built-in type `Bytes` (a type name like `string`, not imported), converted explicitly: `bytes::encode`, `bytes::decode` (malformed UTF-8 gives an error value); making a string from invalid bytes, as in `str::from_bytes([255])`, is a fault right there. The built-in operations:

| Form | Result |
|---|---|
| `len(s)` | Number of code points |
| `s->contains<sub>` | Whether `sub` occurs in `s` (compared byte by byte, which gives the same result as by code point); the empty string always does: `'hello'->contains<'ell'>` is `true`. Written like `contains` on an array |
| `a + b` | Concatenation |
| `==`, `!=`, `<`, `>`, `<=`, `>=` | Comparison in code point order (the same as UTF-8 byte order); a proper prefix comes first |
| `string(x)`, `int(s)`, `float(s)`, `parse_int(s)`, `parse_float(s)` | Conversion, see 3.2 |
| `fmt->format<a, b, ...>` | Formatting: `{}` takes the arguments in order; `{:.2f}`, `{:5d}`, `{:.17g}` and the like specify a format; too few arguments is a fault |
| `` `x = ${x}` `` | A formatted string, shorthand for `'x = {}'->format<x>` ([2.5.4.1](02_lexical.md#2541-formatted-strings)) |
| `strs->join<sep>` | Joins an array of strings |
| `s[i]` | The `i`-th code point; the result is a string containing just that one character; an out-of-range subscript is a fault |

The remaining string functions are in the `str` module ([standard library reference](../../stdlib-reference.md)): `split`, `trim`, `to_upper`, `to_lower`, `replace`, `find`, `substr` (index and length in code points), `starts_with`, `ends_with`, `repeat`, `bytes`, `from_bytes`, `codepoints`, `from_codepoints`.

```cml
import { split, trim, to_upper } from str

func main(): int sync {
    println('a, b ,c'->split<','>->map<(w: string): string => trim(w)>)
    println('héllo'->to_upper, len('héllo'), 'héllo'[1], 'hello'[1])
    return 0
}
```

```text
[a, b, c]
HéLLO 5 é e
```

The result of `s[i]` is a one-character string, not a number; use `str::codepoints` for the numbers. In a string literal the escape `\xHH` can only build valid UTF-8 (`'\xE4\xB8\xAD'` is `'中'`; `'\xE4'` alone is a compile-time error); `\u{4E2D}` writes a code point directly.

### 3.3.5 Values that cannot be compared

`==` and `!=` apply to numbers, `bool`, `string`, `Map` and `Set`, and to arrays, tuples and structs: these three compare by content, element by element, and their element types must themselves be comparable ([6.7.3](06_expressions.md#673-comparison)). Functions, composite values with a function in them, and a `T?`/`T!` by itself have no `==` (compile-time error). For `===` see [6.7.3](06_expressions.md#673-comparison).

## 3.4 May be null `T?` and may fail `T!`

Two dedicated built-in forms, each carrying a one-bit tag:

- **`T?`**: the value is either `null` or a `T`.
- **`T!`**: the value is either a `T` or an error. An error is a fixed built-in struct `{ kind: string, message: string, location: string }` (kind name, message, and the `file:line:column` where it occurred); all fallible functions share this one kind.

```cml
func positive(x: int): int? {
    return if x > 0 then x else null
}

func safe_div(a: int, b: int): int! {
    return if b == 0 then { throw error('DivByZero', 'the divisor is zero') } else a / b
}

func main(): int sync {
    println(positive(4) ?? 0, positive(-4) ?? 0)
    println(safe_div(9, 3) !! -1, safe_div(9, 0) !! -1)
    println(safe_div(9, 0))
    return 0
}
```

```text
4 0
3 -1
error DivByZero: the divisor is zero
```

Rules:

- `T?`, `T!` and `T` are different types. Values of these two kinds can be stored, passed, returned and printed, but **cannot be used as a `T`**: operators such as arithmetic and comparison, and functions that accept a `T`, do not accept them, and their members cannot be accessed. They can only be unwrapped with `??`, `!!`, `?->` ([6.6](06_expressions.md#66-operators-for-null-and-errors)) or with the fixed patterns of `match` ([6.8.2](06_expressions.md#682-match)).
- **A `T?` can be compared with `null`**: `x == null` and `x != null` give a `bool`. Where such a comparison is the condition of an `if`, `x` is a `T` in the arm that excludes null ([6.8.1](06_expressions.md#681-if)). Two `T?` values are not compared with each other; a value that is never null (an `int`, a `T!`) may be compared with `null`, with a fixed result and a warning ([6.7.3](06_expressions.md#673-comparison)).
- **A `T!` that is evaluated and that nothing uses is a warning** (`SemanticDiag::UnusedFallibleValue`): the error it may hold would be lost without a trace. See [7.1](07_statements.md#71-expression-statements).
- **A value that enters a `T?` or `T!` position is put into the form by itself**, wherever the target type is known; `null` likewise. The positions are:
  - an argument of a call: with a parameter `x: int?`, write `f(5)` or `f(null)`. Among overloads an exact match wins: `f(5)` takes `f(x: int)` when both it and `f(x: int?)` exist; when nothing matches exactly, a literal enters the parameter type, and several candidates in that pass are an ambiguity (`SemanticDiag::AmbiguousCall`): write the literal's type (e.g. `2 as f32`) or add a cast;
  - a declaration with a type, `let x: T? = v`;
  - the elements of an array: `let xs: (int?)[] = [1, null, 3]`; without a written type, an array literal with a `null` among its elements is an array of `T?` (`[1, null, 3]` is an `(int?)[]`);
  - the two arms of a branch: when one arm is a `T` and the other is `null` or a `T?`, the branch is a `T?` (`if c then 1 else null` is an `int?`); likewise `T` and `T!`. An arm that has no value at all (`void`: a call of a function that returns nothing, an empty block) counts as `null`: the branch is a `T?` and that arm gives `null`;
  - `return v` in a function whose return type is declared as `T?`/`T!` (`throw e` gives an error).

  `v as T?` can still be written. Several levels are entered at once: `let x: int?! = 5`. On the graph each level is a conversion node (`CAST`); the compiler adds the node a program would write with `as`, and the graph is the same.
- **Never the other way.** A `T?` or `T!` used as a `T` must be opened; an `int?` does not become a `float?` either, and numbers are still not converted implicitly. The arguments of built-in operators (`append`, `println`, ...) are not such a position: their parameter types are computed from the arguments by C++ type rules, so there is no target type to enter; write `as` there.
- **The empty array literal `[]` takes its element type from where it stands**: a declared type (`let xs: int[] = []`), a parameter (`count([])`), the other arm of a branch, the left side of `??`/`!!` (`attempt(f) !! []`), a declared return type, `as` (`[] as Map<string, int>`). Binding `[]` to a name without a type (`let xs = []`) is the compile-time error `SemanticDiag::EmptyArrayWithoutType`, which says to write the type.
- The two tags can be combined: `T?!`, `T!?`. The same one cannot be repeated. Positions such as array elements need parentheses: `(int?)[]`.
- With generics: `(typeas T)?` binds `T` from a nullable argument ([7.5](07_statements.md#75-generic-functions)).
- In `let x: int! = v` there must be a space between `!` and `=`.
- Built-in fallible functions: `parse_int(s)`, `parse_float(s)`, `try_read_text(path)`, `remove(path)` and `remove_dir(path)` of the `fs` module, `decode` and `decode_as` of the `json` module. `error(kind, message)` constructs an error value and records the location of the call.

**Errors and faults are two different things.** Only an operation declared to return `T!` produces an error value. Division by zero, an out-of-range subscript and too deep a recursion are faults: the program terminates with a location, and `!!` cannot catch it (see [4.6](04_execution_model.md#46-faults-exit-and-resource-limits)).

**Behaviour of the current implementation, not yet decided**: `?->` is defined for `T!` only; a `match` on these two forms has exactly two cases; they can be printed but, apart from a `T?` with `null`, not compared with `==`.

## 3.5 Enums

An `enum` is a **nominal type**: a value is exactly one of the variants the declaration lists, and a variant carries no data, a tuple or a struct. Two enums with the same shape are still different types.

```cml
type Shape = enum {
    Circle(float),
    Rect { w: float, h: float },
    Empty,
}

type Option<T> = enum { Some(T), None }
type Tree<T> = enum { Leaf, Node(Tree<T>, T, Tree<T>) }
```

**Declaration.** `type Name = enum { variant, ... }`; a trailing comma is optional. A variant has one of three shapes: `Empty` (no data), `Circle(float)` (one or more positional fields), `Rect { w: float, h: float }` (named fields, separated by `,`). A variant name occurs once in its enum (`DuplicateEnumVariant`), and a field name occurs once in a struct variant (`DuplicateEnumField`). The type may take parameters (`Option<T>`), a field type may name the enum itself (`Tree<T>`) or another enum, and `enum` is a keyword, so it cannot be a name.

**Construction.** A variant is written after the type name, qualified with `::`: `Shape::Circle(1.0)`, `Shape::Rect { w: 2.0, h: 3.0 }`, `Shape::Empty`. A struct variant takes every field it declares, in any order; a tuple variant takes its fields by position. A variant without data is the value itself, with no parentheses. The value has type `Shape`; the arguments of a generic instance are inferred from the field arguments (`Option::Some(42)` is `Option<int>`), or, when they cannot be (`Option::None`), from the type the position expects (`let x: Option<int> = Option::None`, an argument position, a return type), and failing both is a compile-time error (`EnumTypeArgumentNotInferred`). A field argument must have the declared type (`EnumFieldTypeMismatch`), and the number and names of the fields must agree with the declaration (`EnumVariantFieldCount`, `EnumConstructionMismatch`). Automatic wrapping into a field follows the usual rule: an `int` does not become a `float` by itself.

**`match`.** An enum is taken apart by `match`; its fields are never read directly (`b.w` is `EnumFieldAccess`, and the diagnostic shows the `match`):

```cml
func area(s: Shape): float {
    return match s {
        case Shape::Circle(r) => 3.14159 * r * r
        case Shape::Rect { w, h } => w * h
        case Shape::Empty => 0.0
    }
}
```

- A tuple variant's pattern gives every position, with `_` for a field it does not need (`case Shape::Circle(_)`); a struct variant's pattern writes the fields it binds, ignores the others, may rename (`{ w: width }`) and may abbreviate (`{ w }` binds a variable of that name); a variant without data is just its name (`case Shape::Empty`); `case _` matches every remaining value.
- **Exhaustiveness**: naming all variants means no `case _` is needed; a missing variant without `case _` is the compile-time error `EnumMatchNotExhaustive`, whose diagnostic lists the missing variants. A variant already covered by an earlier case is a warning (`EnumVariantCaseRepeated`), and a case after `case _` is never taken and warns (`MatchCaseAfterWildcard`). A `case _` written after every variant is already named can never be taken and warns (`MatchWildcardRedundant`).
- A pattern names a variant of the type being matched: a wrong prefix (`Shape::`) or a wrong variant name is `EnumPatternMismatch`, which lists the variants of that type.
- **A name without `::` in a pattern is a binding** (`case v` over `T?` / `T!`), not a variant: a unit variant is written with its qualifier, `case Shape::Empty`. Writing `case Empty` is `EnumPatternMismatch`, whose diagnostic says that `Empty` is one of the variants and shows `case Shape::Empty`.
- **The first version has no nested patterns**: a pattern in the position of a field, such as `case Shape::Circle(0.0)`, gives "not supported yet" (`ConstructNotSupported`) and asks for a name or `_` per field, matched inside the arm.
- The subject may be any expression, including a call that returns an enum; the arms unify as in the existing `match` ([6.8.2](06_expressions.md#682-match)).

**Equality and printing.** Two values of the same enum instance can be compared with `==` and `!=`: the variant first, then each field in declaration order; when a field type cannot be compared, the enum cannot either (a compile-time error). Printing writes the source form: `Shape::Circle(1.0)`, `Shape::Rect { w: 2.0, h: 3.0 }`, `Shape::Empty`; a generic instance does not print its type arguments (`Option::Some(42)`, not `Option::Some<int>(42)`).

**Generics.** `Option<int>` and `Option<float>` are different types with different values. A field type may use the parameters again: `type Pair<A, B> = enum { Both(A, B), Neither }`, and `Pair<int, string>`'s `Both` takes an `int` and a `string`.

**On the graph.** No node kind is added: construction stores a tag and the data, `match` reads the tag, branches on it and takes the data out in the arm, as for `T?` and `T!`. Reading the tag and a field are ordinary internal operators (`__enum_tag__`, `__enum_field__`), and the schedulers do not special-case them by name. `std::nvm`, `std::fvm`, `std::jit`, `std::prl`, `std::macro` and `std::cpp` all support enums; ONNX export and autodiff refuse one with a located diagnostic for now.

## 3.6 Maps and sets

`Map<K, V>` and `Set<T>` are built-in types and need no `import`. There is no dedicated literal; construct them from an array with `as`:

```cml
func main(): int sync {
    let ages = [('bob', 31), ('alice', 28)] as Map<string, int>
    let more = ages->put<'carol', 45>->remove<'bob'>
    println(ages)
    println(more, len(more), keys(more))
    println(ages['alice'] ?? 0, ages['dave'] ?? 0)

    let seen = [3, 1, 2, 3, 1] as Set<int>
    println(seen, seen->contains<2>, union(seen, [9] as Set<int>))
    println([] as Map<string, int>)
    return 0
}
```

```text
{alice: 28, bob: 31}
{alice: 28, carol: 45} 2 [alice, carol]
28 0
{1, 2, 3} true {1, 2, 3, 9}
{}
```

- **Values are immutable**: every operation returns a new value and the original is unchanged.
- **`m[k]` is a lookup**; its type is `V?`, it is `null` when the key does not exist, and the value is taken out with `??`. Sets have no subscript.
- **Element types must match exactly**: `[('a', 1)] as Map<string, float>` is an error; write `1.0`. A typed declaration is not a conversion; `as` must be written. `Map` and `Set` must be given type arguments.
- Writing two identical constant keys in the array literal is the compile-time error `SemanticDiag::DuplicateCollectionKey`; duplicate elements in a set are dropped.

| Form | Result | Notes |
|---|---|---|
| `m[k]`, `get<k>(m)` | `V?` | `null` when not found |
| `get_or<k, d>(m)` | `V` | `d` when not found |
| `put<k, v>(m)` | `Map<K, V>` | Replaces if the key already exists |
| `remove<k>(m)`, `remove<x>(s)` | Same type | Contents unchanged when absent |
| `has<k>(m)`, `has<x>(s)`, `contains<x>(s)` | `bool` | |
| `len(m)`, `len(s)` | `int` | |
| `keys(m)`, `values(m)`, `items(m)` | `K[]`, `V[]`, `(K, V)[]` | In key order |
| `merge(m1, m2)` | `Map<K, V>` | For equal keys the one from `m2` is taken |
| `add<x>(s)` | `Set<T>` | |
| `union(a, b)`, `intersect(a, b)`, `difference(a, b)` | `Set<T>` | |
| `is_subset(a, b)` | `bool` | |
| `items(s)` | `T[]` | Ascending |
| `a == b`, `a != b` | `bool` | Equal when the entries are the same |

An operation with `with` arguments has one calling form, the convention of the standard library: the map or set that is worked on is the ordinary argument and the rest goes in angle brackets, `put<k, v>(m)` or, as a pipe, `m->put<k, v>` (the same call); an operation on two operands of equal standing takes both as ordinary arguments (`merge(m1, m2)`, `union(a, b)`). Writing everything as ordinary arguments (`put(m, k, v)`, `get(m, k)`, `has(m, k)`) is the compile-time error `SemanticDiag::NoMatchingFunction`, and the diagnostic names the arguments that belong in angle brackets and gives the call as it should be written; the same holds for the other library functions (`split('a,b', ',')` is written `'a,b'->split<','>`). Two prefix-form calls in one argument list (`f(has<a>(m), has<b>(m))`) are currently parsed as comparisons; use the pipe form there, `f(m->has<a>, m->has<b>)`.

To iterate, first take an array, then use a higher-order operator:

```cml
func count_words(words: string[]): Map<string, int> {
    return words->reduce<(acc: Map<string, int>, w: string): Map<string, int> => acc->put<w, (acc[w] ?? 0) + 1>, [] as Map<string, int>>
}

func main(): int sync {
    println(count_words(['a', 'b', 'a']))
    return 0
}
```

```text
{a: 2, b: 1}
```

`put` copies the whole table each time; when building a very large map entry by entry, first obtain the array of entries and then convert once with `as`. Types named `Map` or `Set` declared by the program itself, and functions with the same names as the operators above, take precedence over the built-in names.

**Behaviour of the current implementation, not yet decided**:

- The key type can only be `int`, `i32`, `bool`, `string` or a tuple made of these (other types are `SemanticDiag::CollectionKeyNotAllowed`).
- The order is sorted by key (integers by value, `false` before `true`, strings by byte, tuples element by element); `keys`, `values`, `items` and printing all follow this order.
- When an array known only at run time contains duplicate keys, later entries overwrite earlier ones.
- When the value type is itself nullable (`Map<string, int?>`), the type of `m[k]` is just `int?`: a missing key and a `null` value cannot be told apart; use `has` where needed.
- The coexistence of the three call forms; the print format; `json::decode_as` using the type of a value to specify the target type of decoding.

For the implementation and trade-offs see [technical document 31](../../technical/31_map_and_set.md).

## 3.7 `let`, `var` and references

- `let` introduces an immutable name, `var` a mutable variable. The same name cannot be declared twice in the same scope (`SemanticDiag::Redeclaration`).
- **A variable can be assigned to, and so can a path from one.** A path is the variable itself followed by any number of fields and indices: `p.x = 5`, `xs[0] = 5`, `m[i].f = 5`, `xs[0].values[1].n = 5` are all legal, and so are their compound forms (`xs[0] += 1`). They rebind the whole variable: `xs[i].f = e` is `xs = set(xs, i, set(xs[i], f, e))` ([6.11](06_expressions.md#611-assignment-and-wait)). Only a root that is not a variable (`f().x = 5`, `{ x: 1 }.x = 5`) is the compile-time error `SemanticDiag::AssignmentTargetNotVariable`; an index out of range is the fault `IndexOutOfRange`. In a `sync` body, writing one element of a `var` array does not copy it — unless the array has been read elsewhere (`let a = xs`), in which case it is copied first and `a` is unchanged.
- Assigning to a `let`, or passing a `let` to a `var` parameter, is the compile-time error `SemanticDiag::ImmutablePassedToVarParam`. This is a compile-time check, not a lock on memory.
- **All values are passed by reference.** A `var` parameter is a reference: a write to it by the function is a write to the caller's variable. Captures of variables by lambdas and branches are references too.

```cml
func main(): int sync {
    var count = 0
    count = count + 1
    count += 10
    println(count)

    var xs = [1, 2]
    xs = concat(xs, [3])       // give the whole variable a new array
    let ys = xs->append<4>     // a new array; xs is unchanged
    xs = xs->append<5>         // append to the variable: bind it to the new array, as x = x + 1 does
    println(xs, ys)
    return 0
}
```

```text
11
[1, 2, 3, 5] [1, 2, 3, 4]
```

Camel favours immutability: `var` and assignment are only an opening left for convenience and should be used as little as possible.

**The operators of the standard library return new values and do not change their arguments** (design principles 11.3): `append`, `extend`, `apply`, the `sort` of the `algo` module and the others; after `let ys = xs->append<5>`, `xs` is unchanged. To "append to a variable" write `acc = acc->append<5>`, as with `x = x + 1`; there is no separate explicit in-place operator.

- **An unused result of a pure operation is a compile-time error** (`SemanticDiag::UnusedPureResult`): `xs->append<5>` written as a statement does nothing, and the diagnostic suggests `xs = xs->append<5>` (when `xs` is not a variable, it suggests using the result, or `let _ = ...` to drop it on purpose). "Pure operation" means an operator declared pure in the operator table (the built-in operators and those of C++ modules; `camel inspect --ops` lists the purity) written as a statement whose value nothing takes; calls of the program's own functions are not covered. The last statement of a block is the value of the block: it is not reported when that value is used (returned, bound, passed on); where the block's value is dropped (a body declared `: void`, an element function written in place for `foreach`, the arm of an `if` without `else`, a block written as a statement), its last statement is reported as well.
- **Appending in place is an implementation detail, not part of the meaning.** When `acc = acc->append<v>` is a statement, `acc` is a variable and `append` is the built-in one, the compiler writes it with another implementation of the same rebinding: the array is grown where it is when the variable is the only reference to it (nothing has read `acc` since the previous in-place append), and copied (with room to grow) otherwise. So appending in a loop stays linear. A read of `acc` in between whose value may be kept (`let snap = acc`, passing it to a function, an element that is itself an array) makes the next append copy, and `snap` keeps its value; a read whose value only goes to operations that cannot keep the array (`len(acc)`, a numeric element `acc[i]`, a comparison, `format`), in a sync block and before the next write, leaves the in-place append as it is.
- **`xs = xs->sort` likewise** (design principles 11.3): `sort` returns a new array, and returning a new value versus changing in place is no longer told apart by two names (there is no `sorted`, and no explicit in-place sort). When `xs = xs->sort` (or `xs = sort(xs)`, `merge_sort` and the others) is a statement of a sync block and `xs` a variable, the array is sorted where it is when the variable is its only holder, and copied first otherwise; a value kept before (`let before = xs`) keeps its value. This is an implementation detail, enabled by an operator declaring an in-place form (`OperatorTable::setInPlaceForm`), which no program writes or sees. Tensors have explicit in-place operators (11.3); arrays do not.

For the relation between assignment and `wait` see [4.4](04_execution_model.md#44-var-references-and-wait).
