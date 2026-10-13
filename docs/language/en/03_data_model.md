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
| Boolean | `bool` | `true`, `false` |
| String | `string` | Sequence of bytes, immutable |
| Array | `T[]` | Elements of the same type; length determined at run time |
| Tuple | `(T1, T2, ...)` | By position, accessed with `.0`, `.1` |
| Struct | `{ x: T1, y: T2 }` | By name, accessed with `.x` |
| Function | `(x: T) => R` | See [6.9](06_expressions.md#69-lambdas-and-function-types) |
| May be null | `T?` | See 3.4 |
| May fail | `T!` | See 3.4 |
| Map, set | `Map<K, V>`, `Set<T>` | See 3.5 |
| No value | `void` | Return type of a function that returns no value; its only value prints as `null` |
| Module types | e.g. `Tensor`, `Tensor<f32, const [2, 3]>` | Introduced by modules; the arguments in angle brackets are interpreted by the type itself |

The type name `any` exists for use in the signatures of built-in operators (for example, `println` accepts any value); user code cannot declare a value as `any`, and a value whose static type is `any` prints as `<any>`.

Use `type` to give a type a name, see [7.7](07_statements.md#77-type-declarations):

```cml
type Point = { x: int, y: int }
type Pair<T> = (T, T)
```

## 3.2 Numbers

- **No implicit conversions.** `1 + 2.0`, `1 < 2.0` and mixing `i32` with `int` are all compile-time errors (`SemanticDiag::NoMatchingFunction`). Convert explicitly where needed.
- Conversion functions: `int(x)`, `i64(x)`, `i32(x)`, `float(x)`, `f64(x)`, `f32(x)`, `string(x)`.
- `i64` and `f64` are only other spellings of `int` and `float`. Diagnostics, graphs (`std::rgir`, `inspect`) and editors always show `int` and `float`: an instance of a generic function is `first<int>`, not `first<i64>`.

  | Form | Result |
  |---|---|
  | `float(3)` | `3.0` |
  | `int(3.99)`, `int(-3.99)` | `3`, `-3` (truncation towards zero) |
  | `int(1.0e30)`, `int(nan)`, `int(inf)`, `i32(5000000000)` | The fault `RuntimeDiag::ConversionOutOfRange`: the value does not fit the target type (a float that is NaN, infinite, or whose truncation is outside the integer type; an `int` outside `i32`). The message gives the value and the target type |
  | `f32(1.0e39)` | `inf`: a float converted to a narrower float is rounded as IEEE says, and beyond the range that is an infinity, not a fault |
  | `int('42')`, `float('1.5')` | `42`, `1.5`. **The whole string must be one number**: it may carry one sign (`'-12'`, `'+12'`), an integer is decimal digits only, a float may have a fraction and an exponent (`'1e3'`, `'.5'`) or be `inf` or `nan`. Blanks around it (`' 7'`), anything after the number (`'12abc'`), `'1.5'`, `'0x10'` or `'1_000'` for an integer, and a number outside the range of the type are the fault `RuntimeDiag::InvalidNumberText` |
  | `parse_int('42')`, `parse_float('x')` | `int!`, `float!`: read by the same rule; text that is not one number gives an error value (`ParseError`), not a fault (see 3.4) |
  | `string(42)`, `string(true)`, `string([1, 2])` | `'42'`, `'true'`, `'[1, 2]'` |
  | `string(3.5)`, `string(2.0)` | `'3.5'`, `'2.0'`: the text `println` and a `{}` of `format` write, the shortest that reads back as the same number ([2.5.2](02_lexical.md#252-floating-point-numbers)); `float(string(x))` gives `x` back |

- **A conversion whose argument is a literal is checked at compile time.** `int(1.0e30)` and `i32(5000000000)` are `SemanticDiag::LiteralOutOfRange`, `int('12abc')` is `SemanticDiag::LiteralStaticCastFailed`, as a literal declared with a type that cannot hold it is. With a computed argument the faults of the table above still happen at run time.
- **A typed declaration gives a literal its type**: `let a: i32 = 3`, `let b: f32 = 0.1`. `as` is not used for conversions between numbers (`1 as float` is a compile-time error); use the conversion functions above.
- There is no `bool(x)`, and no conversion from `bool` to an integer; use a comparison to get a boolean (`n != 0`), and `if` to get an integer.
- **Integer arithmetic wraps around without an error**: `9223372036854775807 + 1` gives `-9223372036854775808`. `i32(3000000000)` and `let b: i32 = 3000000000` give `-1294967296` (truncation to 32 bits).
- **Integer division truncates towards zero**, and the remainder has the sign of the dividend: `7 / 2` is `3`, `-7 / 2` is `-3`, `-7 % 3` is `-1`. `%` is for integers only.
- **A zero divisor is a fault** (`RuntimeDiag::DivisionByZero`), for integers and floating-point numbers alike: `1.0 / 0.0` does not give infinity. Floating-point overflow gives `inf`.
- `^` is power: `7 ^ 2` is `49`, `2.0 ^ 0.5` is `1.41421`; a negative exponent on integers gives `0`.

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

- The elements of a literal must have the same type (`[1, 2.0]` is `SemanticDiag::ElementTypeMismatch`).
- Subscripts start at 0; an out-of-range subscript (including a negative one) is the fault `RuntimeDiag::IndexOutOfRange`.
- Built-in functions:

  | Form | Result |
  |---|---|
  | `len(xs)` | Length |
  | `head(xs)`, `tail(xs)` | The first element (of an empty array: the fault `RuntimeDiag::IndexOutOfRange`, as for `xs[0]`); the array without its first element |
  | `range(a, b)` | `[a, a+1, ..., b-1]`; empty when `a >= b` |
  | `xs->slice<a, b>` | A new array of subscripts `a` to `b-1`. `a` and `b` are positions from 0 to the length; a bound outside the array (a negative one included) is the fault `RuntimeDiag::SliceOutOfRange`, as an index out of range is a fault. With both bounds inside the array and `b < a` the result is empty. To take "at most n" use `take` and `drop` of the `array` module, which cut by definition |
  | `concat(xs, ys)` | Concatenation into a new array |
  | `xs->contains<v>` | Whether an element equals `v`, by the `==` of the element type (arrays, tuples and structs compare by content, see [6.7.3](06_expressions.md)) |
  | `zip(xs, ys)` | An array of `(x, y)` tuples; three or more arrays give tuples of that many elements (`zip(xs, ys, zs)`); the lengths must be equal |
  | `strs->join<sep>` | Joins a `string[]` into one string |
  | `map`, `filter`, `reduce`, `foreach` | See [6.10](06_expressions.md#610-higher-order-operators) |
  | `xs->append<v>`, `xs->extend<ys>` | **Modify in place**; `xs` must be a `var` (see 3.6) |

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

Struct types are compared by the names and types of their fields; `type` only gives a name. A literal must give all the fields the type requires.

### 3.3.4 Strings

A string is an immutable sequence of bytes; both length and subscripts are by byte. The built-in operations:

| Form | Result |
|---|---|
| `len(s)` | Number of bytes |
| `s->contains<sub>` | Whether `sub` occurs in `s` (bytes are compared); the empty string always does: `'hello'->contains<'ell'>` is `true`. Written like `contains` on an array |
| `a + b` | Concatenation |
| `==`, `!=`, `<`, `>`, `<=`, `>=` | Byte-by-byte comparison; a proper prefix comes first |
| `string(x)`, `int(s)`, `float(s)`, `parse_int(s)`, `parse_float(s)` | Conversion, see 3.2 |
| `fmt->format<a, b, ...>` | Formatting: `{}` takes the arguments in order; `{:.2f}`, `{:5d}`, `{:.17g}` and the like specify a format; too few arguments is a fault |
| `strs->join<sep>` | Joins an array of strings |
| `s[i]` | The `i`-th byte; the result is a string containing just that one byte; an out-of-range subscript is a fault |

The remaining string functions are in the `str` module ([standard library reference](../../stdlib-reference.md)): by byte, `split`, `trim`, `to_upper`, `to_lower`, `replace`, `find`, `substr`, `starts_with`, `ends_with`, `repeat`, `bytes`; by code point, `char_count`, `char_at`, `char_substr`, `codepoints`, `from_codepoints`.

```cml
import { split, trim, to_upper, char_count } from str

func main(): int sync {
    println('a, b ,c'->split<','>->map<(w: string): string => trim(w)>)
    println('héllo'->to_upper, len('héllo'), char_count('héllo'), 'hello'[1])
    return 0
}
```

```text
[a, b, c]
HéLLO 6 5 e
```

Whether the result of `s[i]` is a one-byte string or a number is behaviour of the current implementation, not yet decided.

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
- **A value that enters a `T?` or `T!` position is put into the form by itself**, wherever the target type is known; `null` likewise. The positions are:
  - an argument of a call: with a parameter `x: int?`, write `f(5)` or `f(null)`. Among overloads an exact match wins: `f(5)` takes `f(x: int)` when both it and `f(x: int?)` exist;
  - a declaration with a type, `let x: T? = v`;
  - the elements of an array: `let xs: (int?)[] = [1, null, 3]`; without a written type, an array literal with a `null` among its elements is an array of `T?` (`[1, null, 3]` is an `(int?)[]`);
  - the two arms of a branch: when one arm is a `T` and the other is `null` or a `T?`, the branch is a `T?` (`if c then 1 else null` is an `int?`); likewise `T` and `T!`;
  - `return v` in a function whose return type is declared as `T?`/`T!` (`throw e` gives an error).

  `v as T?` can still be written. Several levels are entered at once: `let x: int?! = 5`. On the graph each level is a conversion node (`CAST`); the compiler adds the node a program would write with `as`, and the graph is the same.
- **Never the other way.** A `T?` or `T!` used as a `T` must be opened; an `int?` does not become a `float?` either, and numbers are still not converted implicitly. The arguments of built-in operators (`append`, `println`, ...) are not such a position: their parameter types are computed from the arguments by C++ type rules, so there is no target type to enter; write `as` there.
- **The empty array literal `[]` takes its element type from where it stands**: a declared type (`let xs: int[] = []`), a parameter (`count([])`), the other arm of a branch, the left side of `??`/`!!` (`attempt(f) !! []`), a declared return type, `as` (`[] as Map<string, int>`). Binding `[]` to a name without a type (`let xs = []`) is the compile-time error `SemanticDiag::EmptyArrayWithoutType`, which says to write the type.
- The two tags can be combined: `T?!`, `T!?`. The same one cannot be repeated. Positions such as array elements need parentheses: `(int?)[]`.
- With generics: `(typeas T)?` binds `T` from a nullable argument ([7.5](07_statements.md#75-generic-functions)).
- In `let x: int! = v` there must be a space between `!` and `=`.
- Built-in fallible functions: `parse_int(s)`, `parse_float(s)`, `try_read_text(path)` of the `fs` module, `decode_as` of the `json` module. `error(kind, message)` constructs an error value and records the location of the call.

**Errors and faults are two different things.** Only an operation declared to return `T!` produces an error value. Division by zero, an out-of-range subscript and too deep a recursion are faults: the program terminates with a location, and `!!` cannot catch it (see [4.6](04_execution_model.md#46-faults-exit-and-resource-limits)).

**Behaviour of the current implementation, not yet decided**: `?->` is defined for `T!` only; a `match` on these two forms has exactly two cases; they can be printed but not compared with `==`. The relation between the earlier reserved notation `T ? U` and `T!` is undecided, and using it is a compile-time error (`SemanticDiag::FeatureNotSupported`).

## 3.5 Maps and sets

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

## 3.6 `let`, `var` and references

- `let` introduces an immutable name, `var` a mutable variable. The same name cannot be declared twice in the same scope (`SemanticDiag::Redeclaration`).
- **Only a whole variable can be assigned to.** Fields and elements are not assignable: `p.x = 5` and `xs[0] = 5` are the compile-time error `SemanticDiag::AssignmentTargetNotVariable`. Give the variable a new value: `p = { x: 5, y: p.y }`.
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
    xs->append<4>              // modify in place: requires xs to be a var
    println(xs)
    return 0
}
```

```text
11
[1, 2, 3, 4]
```

Camel favours immutability: `var` and assignment are only an opening left for convenience and should be used as little as possible. The in-place operators (`append`, `extend`) modify the object itself, and places that got the same object through another name see the change too; prefer `concat`, which returns a new value.

For the relation between assignment and `wait` see [4.4](04_execution_model.md#44-var-references-and-wait).
