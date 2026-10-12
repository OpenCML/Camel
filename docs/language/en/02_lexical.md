# 2. Lexical structure

English | [中文](../02_lexical.md)

This chapter describes the lexical structure of source code. The token table is `include/camel/parse/syntax/token_kinds.def`, the lexer `src/parse/syntax/lexer.cpp`.

---

## 2.1 Lines, whitespace and separators

- Spaces and tabs are only separators. **A line break ends a statement**, unless the line is visibly unfinished:
  - a round bracket, a square bracket, an angle-bracket argument list or the braces of a struct literal are still open;
  - the line ends with a binary operator, `,`, `=`, `=>`, `->`, `:`, `then`, `else`, `return` or another token that must be followed by something;
  - the next line starts with an operator that **can only be binary**: `->` `?->` `.` `..` `?..` `??` `!!` `*` `/` `%` `^` `&&` `||` `==` `!=` `===` `!==` `<` `>` `<=` `>=` `as` `is` and the assignment operators; in a type also `|` `&` `\`;
  - the next line is the next part of the same construct: `else`, `then`, `catch`, `finally`, `=>`, `from`.

  A next line that starts with `-`, `+`, `!`, `~`, `@`, `(`, `[`, `{`, a name, a literal or a statement keyword is a **new statement**. To write an addition or a subtraction on two lines, leave the operator at the end of the first (`a +`, then `b`); a line that starts with `+` is not a statement and gives `SyntaxDiag::LineStartsWithOperator`.
- A `;` still separates statements on one line. Two statements on one line may also have nothing between them (the first ends where its expression cannot go on), but `;` is clearer.
- **A call, a subscript or angle-bracket arguments must directly follow the expression before them**, with no whitespace in between: `f(1)`, `xs[0]`, `scale<3>`. `f (1)` is the two expressions `f` and `(1)`.
- **Angle brackets and comparisons.** A `<` that touches a name, a literal, `)`, `]`, `}` or `>` before it opens an angle-bracket argument list; every other `<` is less-than. So **the comparison `<` needs a space before it**: `i < n`. `i<n` is read as "`i` with angle-bracket arguments", the parse then fails, and the diagnostic is `SyntaxDiag::ComparisonNeedsSpace`. `<=` is always the comparison. Inside an angle-bracket list (not inside a bracket or a block within it), a `>` that touches the token before it, or starts its line, closes the list; a `>` after a space is greater-than. `xs->filter<(x: int): bool => x > 1>` therefore reads as written, and `a<b<c>>` closes two lists. A closing `>` written after a space (`f<a, b >`) gives `SyntaxDiag::AngleCloseAfterSpace`. Outside angle brackets `>` is the comparison with or without a space.
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
| Reserved, not implemented | `is` `use` `enum` `for` `in` `of` `try` `catch` `finally` `typeof` `keyof` `inner` `outer` `atomic` `shared` |

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
- **A number token carries no sign.** Between two operands `+` and `-` are always the binary operators, whatever the spacing: `n - 1`, `n-1` and `n -1` are all subtractions. Where an operand is expected (no value precedes), a `-` that **touches** a number is part of that number: `-1`, `-2.5`, `[-1, 2]`, `f(-1)`, `case -1 =>`, `-9223372036854775808`. A signed literal is one operand, so `-2->f` passes `-2` to `f` and `-2 ^ 2` is `(-2) ^ 2`; `-a->f` is `-(a->f)`, and so is `- 2->f` (white space after the minus) ([6.12](06_expressions.md#612-operator-precedence)). A `+` before a number is read as its sign too (`+2.5`). The sign of an exponent belongs to the number token itself: `1.0e-2`.
- A line break ends a statement ([2.1](#21-lines-whitespace-and-separators)), so a line that starts with `-x` or `-1` is a new statement and is not joined to the line before it as a subtraction.

### 2.5.2 Floating-point numbers

- The form is "digits `.` digits", optionally with an exponent: `1.0`, `0.5`, `2.5e+3`, `1.0e-2`. There must be digits on both sides of the decimal point (`.5` and `5.` are syntax errors).
- The type is `float` (64-bit double precision). The value of a literal is the double-precision number nearest to the decimal number written (round to even), the same value that `float('...')` gives for the same text.
- **A float has one text: the shortest that reads back as the same number.** `println`, `print`, `string(x)`, a `{}` of `format`, and a float element of an array, tuple, struct or `Map` all write the same text:

  | Value | Text | Note |
  |---|---|---|
  | `2.0`, `100000.0` | `2.0`, `100000.0` | A float with a whole value keeps `.0`, so it reads back as a float |
  | `0.1 + 0.2`, `1.0 / 3.0` | `0.30000000000000004`, `0.3333333333333333` | No longer cut to 6 significant digits |
  | `0.1`, `3.14159265358979` | `0.1`, `3.14159265358979` | The shortest one |
  | `1.0e21`, `1.0e-7` | `1e+21`, `1e-07` | With a decimal exponent below -4 or of 16 and more the exponent form is used, with a sign and at least two digits |
  | `1.0e15`, `0.0001` | `1000000000000000.0`, `0.0001` | Otherwise the plain decimal form |
  | The two zeros | `0.0`, `-0.0` | |
  | Infinities, NaN | `inf`, `-inf`, `nan` | A NaN has no sign (`-nan` is never printed) |

  A `f32` is written as the shortest text that reads back as that `f32` (`f32(0.1)` is `0.1`). `float(s)` and `parse_float(s)` read every text above. For another form give the placeholder of `format` a specification: `'{:.2f}'->format<2.5>` is `2.50`, `'{:.17g}'->format<0.1>` is `0.10000000000000001`.

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

For the meaning and precedence of each operator see chapter 6. `<` and `>` are both comparisons and angle brackets; [2.1](#21-lines-whitespace-and-separators) says how they are told apart. Note that `!=` is one token: in `let x: int! = v` there must be a space between `!` and `=`.
