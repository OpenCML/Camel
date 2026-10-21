# 2. Lexical structure

English | [中文](../02_lexical.md)

This chapter describes the lexical structure of source code. The token table is `include/camel/parse/syntax/token_kinds.def`, the lexer `src/parse/syntax/lexer.cpp`.

---

## 2.1 Lines, whitespace and separators

- Spaces and tabs are only separators. **A line break ends a statement**, unless the line is visibly unfinished:
  - a round bracket, a square bracket, an angle-bracket argument list or the braces of a struct literal are still open;
  - the line ends with a binary operator, `,`, `=`, `=>`, `->`, `:`, `then`, `else`, `return` or another token that must be followed by something;
  - the next line starts with an operator that **can only be binary**: `->` `?->` `.` `..` `?..` `??` `!!` `*` `/` `%` `^` `&` `|` `&&` `||` `==` `!=` `===` `!==` `<` `>` `<=` `>=` `as` `is` and the assignment operators; in a type also `\`. **A symbol of two roles (`+`, `-`, `@`) that starts a line has the role that starts something new**: `-x` is a new statement, `@name` an annotation. To continue a line, leave it at the end of the line before (a matrix product: `a @`, line break, `b`);
  - the next line is the next part of the same construct: `else`, `then`, `catch`, `finally`, `=>`, `from`.

  A next line that starts with `-`, `+`, `!`, `~`, `@`, `(`, `[`, `{`, a name, a literal or a statement keyword is a **new statement**. To write an addition or a subtraction on two lines, leave the operator at the end of the first (`a +`, then `b`); a line that starts with `+` is not a statement and gives `SyntaxDiag::LineStartsWithOperator`.
- A `;` still separates statements on one line. Two statements on one line may also have nothing between them (the first ends where its expression cannot go on), but `;` is clearer.
- **A call, a subscript or angle-bracket arguments must directly follow the expression before them**, with no whitespace in between: `f(1)`, `xs[0]`, `scale<3>`. `f (1)` is the two expressions `f` and `(1)`.
- **Angle brackets and comparisons.** A `<` that touches a name, a literal, `)`, `]`, `}` or `>` before it opens an angle-bracket argument list; every other `<` is less-than. So **the comparison `<` needs a space before it**: `i < n`. `i<n` is read as "`i` with angle-bracket arguments", the parse then fails, and the diagnostic is `SyntaxDiag::ComparisonNeedsSpace`. `<=` is always the comparison (`<=>` is the two-way arrow of a rule declaration, see [8](08_top_level_and_grammar.md)). Inside an angle-bracket list (not inside a bracket or a block within it), a `>` that touches the token before it, or starts its line, closes the list; a `>` after a space is greater-than. `xs->filter<(x: int): bool => x > 1>` therefore reads as written, and `a<b<c>>` closes two lists. A closing `>` written after a space (`f<a, b >`) gives `SyntaxDiag::AngleCloseAfterSpace`. Outside angle brackets `>` is the comparison with or without a space.
- Source files are read as UTF-8.

```cml
let a = 1; let b = 2
let c = a + b
```

## 2.2 Comments

- `//` to the end of the line is a line comment.
- **A line that starts with `///` is a documentation comment**: the consecutive `///` lines right above a declaration (`func`, `let`/`var`, `type`, with the annotations, `with` and `export` before it), without an empty line in between, are the documentation of that declaration. An editor shows it on hover, `camel inspect --ast` lists it on the declaration, and the formatter keeps it in place. A line that starts with `////` is an ordinary comment.
- `/* ... */` is a block comment; it may span lines and cannot be nested.
- `#!` on the first line of the file, to the end of that line, is also ignored, so the first line of a script can be `#!/usr/bin/env camel`. Elsewhere `#` is not a comment: it opens a tag (design principles 6.2, see [8.1](08_top_level_and_grammar.md)).

## 2.3 Identifiers

An identifier starts with a letter or an underscore, followed by letters, digits and underscores: `[a-zA-Z_][a-zA-Z_0-9]*`. Identifiers are case-sensitive. A lone `_` is not a name: it is the wildcard pattern of `match`, and in `let _ = e` it says that the value of `e` is dropped on purpose ([7.2](07_statements.md#72-let-and-var)).

A reference to a name may be qualified with `::`: `math::sqrt`, `Tensor::ones` (see chapter 5).

## 2.4 Keywords

The following words are reserved and cannot be used as names (using one gives `SyntaxDiag::KeywordAsIdentifier`):

| Category | Keywords |
|---|---|
| Declarations | `let` `var` `func` `type` `with` `module` `import` `export` `from` `namespace` `using` |
| Control | `if` `then` `else` `match` `case` `return` `throw` `wait` `sync` `loop` `while` `do` `for` `in` `static` |
| Types and conversion | `as` `typeas` `const` `constas` `enum` |
| Literals | `null` `true` `false` |
| Reserved, not implemented | `is` `of` `try` `catch` `finally` `typeof` `keyof` `inner` `outer` `atomic` `shared` |

For the status of each word in the last row see [8.6](08_top_level_and_grammar.md#86-constructs-reserved-but-not-implemented).

Names of the form `__x__` (two underscores on each side) are the compiler's own: the operators are functions of such names (`a + b` is `__add__(a, b)`). A variable, a parameter or a function cannot take one (`SemanticDiag::ReservedIdentifier`). To define an operator for a type of the program, write `func operator +(a: V, b: V): V` ([7.4.5](07_statements.md#745-operator-overloading)); `operator` is not a keyword, and is read this way only right after `func` and before an operator.

## 2.5 Literals

### 2.5.1 Integers

| Form | Example | Value |
|---|---|---|
| Decimal | `0`, `42`, `-7` | |
| Hexadecimal | `0xff` | 255 |
| Binary | `0b1010` | 10 |
| Octal (`0o` followed by octal digits) | `0o77` | 63 |

- The default type of an integer literal is `int` (64-bit signed), with range -9223372036854775808 to 9223372036854775807. Where the context asks for another integer type it takes that one ([3.2](03_data_model.md#32-numbers)): the `1` in `let b: i32 = 1` is an `i32`, and in `f(1)` with `f(x: f32)` it is an `f32`.
- **A literal out of range is a compile-time error** (`SemanticDiag::LiteralNotRepresentable`; the message quotes what was written and gives the reason, such as `out of range (-9223372036854775808 to 9223372036854775807)`): `9223372036854775808`, `1e400` and `0x8000000000000000` all are. `-9223372036854775808` is in range. A declaration with a type that puts a literal into a narrower type is checked in the same way: `let b: i32 = 3000000000` and `let c: f32 = 1.0e39` are errors. A floating-point literal larger than the largest `float` (`1.0e400`) is this error too; **a nonzero real too small to be represented, one that rounds to zero** (`1e-9999`, or `let c: f32 = 1e-50`), is also a value the type cannot hold, with the reason that the nearest value is zero, and is never read as zero. An operation between literals is computed with its exact value before it is put into a type, so `9223372036854775807 + 1` is this error as well (a run-time addition wraps around instead); when the folded value does not fit, the position is the **whole expression**, not its last operand. A decimal put into an integer type (`let x: int = 1.9`) is the same diagnostic, with the reason that the value would be truncated.
- **A literal with an exponent or a decimal point has `float` as its default type** ([2.5.2](#252-floating-point-numbers)): with no context `1e6` is a `float` with the value `1000000.0`, and `1e-2` is `0.01`; where the context asks for `f32` it takes `f32`. An integer literal has only the four forms of the table above.
- **A decimal that starts with `0` and is followed by another digit is not a number token**: `09` and `077` are lexical errors (`LexicalDiag::InvalidNumberFormat`, the range covering the whole written run) and are no longer split into `0` and `9` without a word. Octal is written `0o77`, and `0o` must be followed by octal digits. A form with a point (`077.5`) is a real number; a leading zero is unambiguous there.
- An exponent is for decimal numbers only: `0b1e5` is `SemanticDiag::InvalidLiteral` (in a hexadecimal number `e` is a digit); `07e2` is the lexical error above.
- There are no digit separators (`1_000` is a syntax error).
- **A number token carries no sign, and a minus is always an operator.** Between two operands `+` and `-` are always the binary operators, whatever the spacing: `n - 1`, `n-1` and `n -1` are all subtractions. Where an operand is expected (no value precedes) `-` is the unary minus, and it reads the same before a literal and before a name: it binds less tightly than `^` and more tightly than `->` and the other binary operators ([6.12](06_expressions.md#612-operator-precedence)). `-2 ^ 2` is `-(2 ^ 2)`, that is `-4`; `-2->f` is `(-2)->f`; `-x->f` is `(-x)->f`. A minus applied directly to a number gives the negative literal, so `-1`, `-2.5`, `[-1, 2]`, `f(-1)`, `-9223372036854775808` and `let b: i32 = -2147483648` are written as ever; a `match` pattern may be `case -1 =>`. A `+` before a number is read as its sign (`+2.5`). The sign of an exponent belongs to the number token itself: `1.0e-2`.
- A line break ends a statement ([2.1](#21-lines-whitespace-and-separators)), so a line that starts with `-x` or `-1` is a new statement and is not joined to the line before it as a subtraction.

### 2.5.2 Floating-point numbers

- The form is "digits `.` digits", optionally with an exponent (`1.0`, `0.5`, `2.5e+3`, `1.0e-2`), or decimal digits directly followed by an exponent (`1e6`, `1E5`, `1e-2`). There must be digits on both sides of a decimal point (`.5` and `5.` are syntax errors).
- The default type is `float` (64-bit double precision); where the context asks for `f32` it is an `f32` ([3.2](03_data_model.md#32-numbers)). The value of a literal is the double-precision number nearest to the decimal number written (round to even), the same value that `float('...')` gives for the same text.
- **A float has one text: the shortest that reads back as the same number.** `println`, `print`, `string(x)`, a `{}` of `format`, and a float element of an array, tuple, struct or `Map` all write the same text:

  | Value | Text | Note |
  |---|---|---|
  | `2.0`, `100000.0` | `2.0`, `100000.0` | A float with a whole value keeps `.0`, so it reads back as a float |
  | `let a = 0.1; a + 0.2`, `1.0 / 3.0` | `0.30000000000000004`, `0.3333333333333333` | No longer cut to 6 significant digits. `0.1 + 0.2` between literals is computed exactly when the program is compiled and is `0.3` (design principles 5.1) |
  | `0.1`, `3.14159265358979` | `0.1`, `3.14159265358979` | The shortest one |
  | `1.0e21`, `1.0e-7` | `1e+21`, `1e-07` | With a decimal exponent below -4 or of 16 and more the exponent form is used, with a sign and at least two digits |
  | `1.0e15`, `0.0001` | `1000000000000000.0`, `0.0001` | Otherwise the plain decimal form |
  | The two zeros | `0.0`, `-0.0` | |
  | Infinities, NaN | `inf`, `-inf`, `nan` | A NaN has no sign (`-nan` is never printed) |

  A `f32` is written as the shortest text that reads back as that `f32` (`f32(0.1)` is `0.1`). `float(s)` and `parse_float(s)` read every text above. For another form give the placeholder of `format` a specification: `'{:.2f}'->format<2.5>` is `2.50`, `'{:.17g}'->format<0.1>` is `0.10000000000000001`.

  **Data text has exactly two supplements over a literal** (H15i). An entry argument reads every base of an integer literal (`0x`, `0b`, `0o`); the integer text of a conversion function is decimal digits (`int('0x10')` and `int('0o77')` are not integer texts). Beyond what the two share:

  1. `inf`, `-inf` and `nan`: these are the forms `floatText` prints for the infinities and NaN, and a value a program wrote has to read back (source uses `math::inf` and `math::nan`; there is no literal). A float parameter and `float(s)` / `parse_float(s)` read them; an integer parameter and `int(s)` / `parse_int(s)` have an integer target and do not. A NaN carries no sign; `+inf` and `+nan` take a `+` sign.
  2. **A leading-zero decimal**: `077` is 77, and `09` and `007` read as usual; an entry argument and all four conversion functions read it so. In source that form is a lexical error and octal is written `0o77`; data text has no leading-zero octal, `0o77` is still 63 as an entry argument, and it is not an integer text for a conversion function.

  There is no other supplement: a real has digits on both sides of its point (`.5` and `5.` do not read), there is no hexadecimal float, no `Infinity`, and no digit separators. **A nonzero real text that rounds to zero is refused in all three places, never read as zero**: a literal reports `SemanticDiag::LiteralNotRepresentable`, an entry argument and a conversion function report a fault (a conversion folds it into `ConversionOutOfRange`, the same refusal as an overflow).

  **A NaN has no sign under a format specification either**: `'{:.2f}'` and `'{:>8}'` write a negative NaN as `nan`, and neither a width nor a precision changes that text (an uppercase type letter writes `NAN`, `'{:E}'`). `{:+}` is `+nan`: the `+` is the sign flag added to that text, not the NaN's sign.

### 2.5.3 Imaginary literals

- A number directly followed by `i`, where no identifier character follows the `i`, is an **imaginary literal**: `4i`, `4.5i`, `1e3i`. `4if` is `4` and `if`; `0xFFi` is `0xFF` and the name `i` (hexadecimal, binary and octal forms take no imaginary suffix).
- Its value is a complex number: the real part is 0 and the imaginary part is the number written. The default type is `c128` (double-precision complex, design principles 5.1); where the context asks for `c64` it is a `c64` (both parts rounded to f32): in `let z: c64 = 2i` the `2i` is a `c64`.
- A minus sign is still an operator: `-2i` is `-(2i)`, real part 0 and imaginary part -2, printed as `0-2i`.
- Imaginary literals fold with each other and with real literals exactly: `(1+2i)*(3+4i)` is `-5+10i`. A literal expression with an imaginary part defaults to `c128`; putting it into a real type is `SemanticDiag::LiteralNotRepresentable`, whose help names `re()` and `im()`.
- The printed form is the real part, `+` or `-`, the magnitude of the imaginary part, and `i`: `3+4i`, `0-2i`, `-1+0i`. A part with a whole value writes no `.0`; each part of a `c64` is the shortest text that reads back as the same `f32`.
- **A format specification for a complex number writes both parts, as it does in Python.** The number part of the specification (sign, `#`, precision, type letter) applies to each part: `'{:.2f}'` writes `3+4i` as `3.00+4.00i`, `'{:e}'` as `3.000000e+00+4.000000e+00i`, and `'{:#.0f}'` as `3.+4.i`. A `+` or space sign is written on the real part only, the sign of the imaginary part is the separator between the parts, so `{:+}` is `+3+4i` and `{: .2f}` is ` 3.00+4.00i`. The layout part (fill, alignment, width) applies once, to the whole text, with the default alignment at the right: `'{:>12.2f}'` is `  3.00+4.00i`, `'{:*>12.2f}'` is `**3.00+4.00i`, and `'{:^13.2f}'` is ` 3.00+4.00i  `. Zero padding and `=` alignment are errors (`SemanticDiag::ArgumentsRejected`), as they are in Python: `{:08.2f}`, `{:=12.2f}`. The type letter is checked against the parts, which are numbers: `{:s}` is an error. A placeholder with no specification writes the text above (`{}` of `3+4i` is `3+4i`, no parentheses). **When a part is a NaN that part is written `nan`, never `-nan` (the same rule as 2.5.2), with and without a specification.**

### 2.5.4 Strings

- Single or double quotes: `'Hello'`, `"Hello"`; the two are equivalent, and one kind of quote can be written directly inside the other.
- Triple-quoted multi-line strings: `'''...'''` or `"""..."""`, which may contain newlines.
- A backquoted **formatted string** `` `...` `` interpolates with `${expression}`; the format specification of `${expression:spec}` is the one of `format` ([2.5.4.1](#2541-formatted-strings)).
- **Every string shares one escape table**: single-quoted, double-quoted, triple-quoted and the backquoted formatted string. The same backslash pair gives the same character in whichever kind of quotes it is written, so a reader never has to know which string kind they are in.

| Escape | Meaning |
|---|---|
| `\n` | line feed U+000A |
| `\t` | tab U+0009 |
| `\r` | carriage return U+000D |
| `\a` | bell U+0007 |
| `\b` | backspace U+0008 |
| `\f` | form feed U+000C |
| `\v` | vertical tab U+000B |
| `\\` | backslash |
| `\'` | single quote |
| `\"` | double quote |
| `` \` `` | backtick |
| `\$` | dollar sign `$` |
| `\0`, `\0oo` | a `0` after the backslash followed by 0 to 2 more octal digits, one byte (0 to 0o77); `\0` is the null byte, `\012` is a line feed |
| `\xHH` | one byte written with 1 or 2 hexadecimal digits; with no hexadecimal digit after it, `\x` is kept as written |
| `\u{H…}` | one Unicode code point written with 1 to 6 hexadecimal digits, e.g. `\u{4E2D}` is “中”; when it is not a scalar value, `\u` is kept as written |
| anything else | the backslash and the character are kept as written: `\q` is `\q` |

- `\$` is a `$` in a formatted string too. The one addition is `\${` (a dollar sign immediately followed by a left brace): in a formatted string it is the **structural escape** that writes a literal `${` and starts no interpolation, see [2.5.4.1](#2541-formatted-strings); an ordinary or a triple-quoted string has no `${` meaning, so `\${` there is the character escape `\$` followed by `{`, which is also `${`.
- A string is a sequence of bytes. `len` gives the number of bytes: `len('héllo')` is 6, `len('中文')` is 6.

#### 2.5.4.1 Formatted strings

- In a backquoted string `${` starts an **interpolation** that ends at its matching `}`; inside is an arbitrary expression ([6.2](06_expressions.md#62-literals-and-construction)). Strings, comments, braces and backquoted strings may appear again inside it, each closing where it should: `` `point ${({ x: 1 }).x}` ``, `` `${'{'}${'}'}` ``.
- After the expression an optional `:spec` gives a format specification; the colon must be at the top level of the interpolation (not inside any brackets). The specification is exactly that of a placeholder of `format` (`{:5d}`, `{:.2f}`, `{:>8}`). A specification the type of the value does not take is a compile-time error `SemanticDiag::ArgumentsRejected`, located at that interpolation.
- **An ordinary `{` or `}` is a literal character and needs no escape**: `` `{ }` `` prints `{ }`. Only `${` together starts an interpolation.
- `\${` is a literal `${` and starts no interpolation. It is the structural escape of a formatted string (it decides whether `${` starts an interpolation), not a character escape; every other backslash pair uses the one table of [2.5.4](#254-strings). So `\$` is a `$` in a formatted string too, and `\${` prints `${` in a formatted string as it does in an ordinary one.
- It may span lines, which are kept as written; the escapes inside a line are those of a string ([2.5.4](#254-strings)), and `` \` `` is a literal backtick (it does not close the string either).
- When an interpolation holds no token that is neither white space nor a comment it is empty, the error `SyntaxDiag::EmptyInterpolation`: `${}`, `${ }` and `${/*c*/}` are all empty, `${/**/ 1}` is not. It is reported separately from an ordinary syntax error inside the interpolation.
- It is shorthand for `'...'->format<...>`: the same tree and the same meaning ([6.2](06_expressions.md#62-literals-and-construction)).

```cml
let name = 'Camel'
let n = 42
println(`hello, ${name}!`)                    // the same as 'hello, {}!'->format<name>
println(`n = ${n}, twice = ${n * 2}`)
println(`pi = ${3.14159:.2f}`)                // the same as 'pi = {:.2f}'->format<3.14159>
println(`literal { } and \${not} interpolated`)
```

```text
hello, Camel!
n = 42, twice = 84
pi = 3.14
literal { } and ${not} interpolated
```

The long form of the same call is still written `'{} + {} = {}'->format<1, 2, 1 + 2>`.

### 2.5.4 Booleans and null

`true` and `false` have type `bool`. `null` is the null value of every nullable type `T?` (see [3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)).

## 2.6 Operators and delimiters

| Token | Use |
|---|---|
| `+` `-` `*` `/` `%` `^` `@` | Arithmetic: addition, subtraction, multiplication, division, remainder, power, matrix multiplication |
| `==` `!=` `<` `>` `<=` `>=` | Comparison |
| `===` `!==` | Reserved, not supported for now (compile-time error) |
| `&&` `\|\|` `!` | Logical and, or, not; `&&` and `\|\|` are branches (the right side is evaluated only when needed), reserved, and cannot be overloaded |
| `&` `\|` | And, or with both sides evaluated; ordinary operators, which can be overloaded |
| `~` | Bitwise not |
| `=` `+=` `-=` `*=` `/=` `%=` `^=` `@=` `&=` `\|=` | Assignment and compound assignment |
| `??` `!!` `?->` | Unwrap a nullable value, unwrap a fallible value, pass the error on if there is one |
| `->` | Pipe: the value on the left becomes an argument of the function on the right |
| `..` `?..` | Function composition (see [6.5](06_expressions.md#65-pipes)) |
| `.` | Field and tuple member access |
| `::` | Qualified name |
| `=>` | Lambda and function type, the arrow of `case`, the one-way arrow of a rule |
| `<=>` | The two-way arrow of a rule |
| `?` `!` (after a type) | `T?` may be null, `T!` may fail |
| `@` (before a function declaration) | Annotation, see [7.6](07_statements.md#76-annotations) |
| `( )` `[ ]` `{ }` `< >` | Grouping, tuples and arguments; arrays and subscripts; structs and blocks; `with` arguments and type arguments |
| `,` `:` `;` | Separators |

For the meaning and precedence of each operator see chapter 6. `<` and `>` are both comparisons and angle brackets; [2.1](#21-lines-whitespace-and-separators) says how they are told apart. Note that `!=` is one token: in `let x: int! = v` there must be a space between `!` and `=`.
