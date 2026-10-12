# 2. Lexical structure

English | [中文](../02_lexical.md)

This chapter describes the lexical structure of source code. The exact rules are in `antlr/OpenCMLLex.g4`.

---

## 2.1 Lines, whitespace and separators

- Spaces, tabs and newlines are only separators and have no syntactic meaning. Statements may be separated by a newline or by `;`; use `;` when writing several statements on one line.
- **A call, a subscript or angle-bracket arguments must directly follow the expression before them**, with no whitespace in between: `f(1)`, `xs[0]`, `scale<3>`. `f (1)` is a syntax error, because `(1)` is taken as the start of another expression.
- A newline by itself does not end a statement: the parser always tries to absorb the following tokens into the current expression. When the previous statement ends with `>` (`with` arguments) and the next line starts with a literal, the two may be read as one comparison; when the next line has angle brackets of its own, the compiler gives the warning `SyntaxDiag::ComparisonAcrossLines` at that `>`, saying that two statements may have been joined; in that case write `;` between the two statements, or write the next line in the form of a function call (`println(...)`).
- Source files are read as UTF-8.

```cml
let a = 1; let b = 2
let c = a + b
```

## 2.2 Comments

- `//` to the end of the line is a line comment.
- `/* ... */` is a block comment; it may span lines and cannot be nested.
- `#` to the end of the line is also ignored, so the first line of a script can be `#!/usr/bin/env camel`.

## 2.3 Identifiers

An identifier starts with a letter or an underscore, followed by letters, digits and underscores: `[a-zA-Z_][a-zA-Z_0-9]*`. Identifiers are case-sensitive. A lone `_` is the wildcard pattern of `match`.

A reference to a name may be qualified with `::`: `math::sqrt`, `Tensor::ones` (see chapter 5).

## 2.4 Keywords

The following words are reserved and cannot be used as names (using one gives `SyntaxDiag::KeywordAsIdentifier`):

| Category | Keywords |
|---|---|
| Declarations | `let` `var` `func` `type` `with` `module` `import` `export` `from` `namespace` `using` |
| Control | `if` `then` `else` `match` `case` `return` `throw` `wait` `sync` `macro` |
| Types and conversion | `as` `typeas` `const` |
| Literals | `null` `true` `false` |
| Reserved, not implemented | `is` `use` `enum` `for` `in` `of` `try` `catch` `finally` `raise` `typeof` `keyof` `inner` `outer` `atomic` `shared` |

For the status of each word in the last row see [8.6](08_top_level_and_grammar.md#86-constructs-reserved-but-not-implemented).

## 2.5 Literals

### 2.5.1 Integers

| Form | Example | Value |
|---|---|---|
| Decimal | `0`, `42`, `-7` | |
| Hexadecimal | `0xff` | 255 |
| Binary | `0b1010` | 10 |
| Octal (`0` followed by octal digits) | `077` | 63 |
| With exponent | `1e6`, `1e3` | 1000000, 1000 |

- The type of an integer literal is `int` (64-bit signed), with range -9223372036854775808 to 9223372036854775807.
- **A literal out of range is a compile-time error** (`SemanticDiag::LiteralOutOfRange`): `9223372036854775808`, `1e400` and `0x8000000000000000` all are. `-9223372036854775808` is in range. A declaration with a type that puts a literal into a narrower type is checked in the same way: `let b: i32 = 3000000000` and `let c: f32 = 1.0e39` are errors. A floating-point literal larger than the largest `float` (`1.0e400`) is this error too.
- **An integer with an exponent is still an integer**: `1e6` is an `int`, not a `float`; a negative exponent truncates towards zero, so `1e-2` is `0` and `15e-1` is `1`, with the warning `SemanticDiag::IntegerLiteralTruncated` (none when the result is exact, as for `1500e-2`). For a floating-point number write a decimal point: `1.0e-2`. Whether a literal with a negative exponent should be a float or an error has not been decided.
- An exponent is for decimal numbers only: `0b1e5` and `07e2` are `SemanticDiag::InvalidLiteral` (in a hexadecimal number `e` is a digit).
- There are no digit separators (`1_000` is a syntax error) and no `0o` prefix.
- **A number token carries no sign.** Between two operands `+` and `-` are always the binary operators, whatever the spacing: `n - 1`, `n-1` and `n -1` are all subtractions. Where an operand is expected, a `-` or `+` directly before a number is the sign of the literal: `-1`, `+2.5`, `[-1, 2]`, `f(-1)`, `case -1 =>`, `-9223372036854775808`. A signed literal is one operand, so `-2->f` passes `-2` to `f`, while `-a->f` is `-(a->f)` ([6.12](06_expressions.md#612-operator-precedence)). The sign of an exponent belongs to the number token: `1.0e-2`.
- A newline does not end a statement, so a line that starts with `-` or `+` joins the expression of the line before as a subtraction or an addition (as a line starting with `-x` does); write `;` to keep them apart.

### 2.5.2 Floating-point numbers

- The form is "digits `.` digits", optionally with an exponent: `1.0`, `0.5`, `2.5e+3`, `1.0e-2`. There must be digits on both sides of the decimal point (`.5` and `5.` are syntax errors).
- The type is `float` (64-bit double precision). The value of a literal is the double-precision number nearest to the decimal number written (round to even), the same value that `float('...')` gives for the same text.
- Printing keeps 6 significant digits by default and drops trailing zeros: `1.0` prints as `1`, `0.1 + 0.2` prints as `0.3`, `3.14159265358979` prints as `3.14159`. For another precision use `format`: `'{:.17g}'->format<0.1>` gives `0.10000000000000001`.

### 2.5.3 Strings

- Single or double quotes: `'Hello'`, `"Hello"`; the two are equivalent, and one kind of quote can be written directly inside the other.
- Escapes: `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\xHH`.
- Triple-quoted multi-line strings: `'''...'''` or `"""..."""`, which may contain newlines.
- A string is a sequence of bytes. `len` gives the number of bytes: `len('héllo')` is 6, `len('中文')` is 6.
- The backquoted format string `` `...` `` is reserved, not implemented (`SemanticDiag::ConstructNotSupported`); use `format`:

```cml
println('{} + {} = {}'->format<1, 2, 1 + 2>)
println(format<3.14159>('pi is about {:.2f}'))
```

```text
1 + 2 = 3
pi is about 3.14
```

### 2.5.4 Booleans and null

`true` and `false` have type `bool`. `null` is the null value of every nullable type `T?` (see [3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)).

## 2.6 Operators and delimiters

| Token | Use |
|---|---|
| `+` `-` `*` `/` `%` `^` `@` | Arithmetic: addition, subtraction, multiplication, division, remainder, power, matrix multiplication |
| `==` `!=` `<` `>` `<=` `>=` | Comparison |
| `===` `!==` | Identity comparison |
| `&&` `\|\|` `!` | Logical and, or, not |
| `~` | Bitwise not |
| `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | Assignment and compound assignment |
| `??` `!!` `?->` | Unwrap a nullable value, unwrap a fallible value, pass the error on if there is one |
| `->` | Pipe: the value on the left becomes an argument of the function on the right |
| `..` `?..` | Function composition (see [6.5](06_expressions.md#65-pipes)) |
| `.` | Field and tuple member access |
| `::` | Qualified name |
| `=>` | Lambda and function type, the arrow of `case` |
| `?` `!` (after a type) | `T?` may be null, `T!` may fail |
| `@` (before a function declaration) | Annotation, see [7.6](07_statements.md#76-annotations) |
| `( )` `[ ]` `{ }` `< >` | Grouping, tuples and arguments; arrays and subscripts; structs and blocks; `with` arguments and type arguments |
| `,` `:` `;` | Separators |

For the meaning and precedence of each operator see chapter 6. Note that `!=` is one token: in `let x: int! = v` there must be a space between `!` and `=`.
