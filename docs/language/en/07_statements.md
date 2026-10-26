# 7. Statements and declarations

English | [中文](../07_statements.md)

A block is made up of statements. A statement is: an expression, a `let`/`var` declaration, `return`/`throw`, a nested block, or a declaration of a function or a type.

---

## 7.1 Expression statements

Any expression can be a statement. In a sync block it executes in order; outside a sync block, an expression whose value is not used does not execute ([4.2](04_execution_model.md#42-evaluation-by-dependency)).

**A `T!` that is evaluated and not used is a warning.** In a sync block a statement runs whether or not anything takes its value; when that value is a `T!` ([3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)), an error it holds is lost without a trace, and the compiler says so (`SemanticDiag::UnusedFallibleValue`):

<!-- camel-snippet: fragment: the body of save is elided; the four uses of a fallible value are the subject -->

```cml
func save(path: string): int! sync { ... }

func main(): int sync {
    save('a.txt')              // warning: the int! is not used
    let r = save('b.txt')      // warning: r is never used
    let _ = save('c.txt')      // dropped on purpose: no warning
    return save('d.txt') !! -1 // used
}
```

**An unused result of a pure operation is an error.** When a statement is a call of an operator declared pure (`xs->append<5>`, `sort(xs)`, `a + b`) and its value is not used (it is not the last statement of its block, or the block's value is dropped: a body declared `: void`, an element function written in place for `foreach`, the arm of an `if` without `else`, a block written as a statement), it does nothing: such an operator returns a new value and changes none of its arguments (design principles 11.3). This is the compile-time error `SemanticDiag::UnusedPureResult`; when `xs` is a variable, the diagnostic suggests the rebinding `xs = xs->append<5>` ([3.7](03_data_model.md#37-let-var-and-references)).

Using the value means taking it apart with `!!`, `?->` or `match`, passing it on, or returning it. To drop it on purpose write `let _ = ...` ([7.2](#72-let-and-var)). The same warning is given for what the arm of an `if` without `else` gives, and for the value a `main` **without a declared return type** ends with: only a `main` that declares its return type has a result ([4.1](04_execution_model.md#41-program-and-entry)), so an error value at the end of an undeclared one goes nowhere. Outside a sync block there is nothing to warn about: a value nothing uses is not evaluated at all. The warning can be turned off with `-Wno-unused-fallible-value`.

## 7.2 `let` and `var`

```
(let | var) name (: type)? = expression
```

```cml
let n = 42
let ratio: float = 0.75
let names: string[] = []
var count = 0
```

- An initial value is required. The type can be omitted and is inferred from the initial value; when the type is written, the initial value must be of that type (a literal is interpreted according to the type written: `let a: i32 = 3`, `let b: f32 = 1`, `let c: float = 0.1 + 0.2`; an operation between literals is computed with its exact value before it is put in, see [3.2](03_data_model.md#32-numbers)). Any other expression must already have the declared type, or a type it accepts: `var acc: V = p` with `p: V` is fine, and nothing is converted on the way (`let x: float = n` with `n: int` is `SemanticDiag::DeclaredTypeMismatch`; write `float(n)`). **Text is not converted either**: `let x: int = '42'` is `DeclaredTypeMismatch` (the message says a `string` cannot be used as an `int`); write `int('42')` or `parse_int('42')`. A declared `T?` or `T!` takes a `T` by putting it into that form ([3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)).
- The scope is the enclosing block (or the module top level). The same name cannot be declared again in the same scope; inner blocks and lambdas can shadow outer names.
- **Destructuring**: a tuple can be destructured, and several names can be declared at once:

  ```cml
  let (q, r) = (7 / 2, 7 % 2)
  let a, b = 1, 'x'
  ```

  Struct destructuring `let { x, y } = p` and array destructuring `let [a, b] = xs` are not in the language: a `{` or a `[` where a name stands is an ordinary syntax error. Use `p.x`, `xs[0]`.
- **`let _ = e` drops the value of `e` on purpose.** `_` is not a name: nothing is bound, so it can be written any number of times in one block, and it cannot be read. The expression is evaluated like the initial value of any `let` (in a sync block, in order). It is the way to say that a `T!` is ignored deliberately ([7.1](#71-expression-statements)). Only this form exists: `var _`, and `_` among several names, are syntax errors.
- A `let` at the module top level is a global constant; a top-level `var` can be read and written by functions, also by an imported function of the module that declares it ([5.3](05_modules.md#53-exports)). **The top-level declarations do not depend on the order they are written in, only on what they use**: `let area = pi * r * r` may come before `let pi`; top-level values that depend on each other are `SemanticDiag::ModuleValueCycle` ([5.6](05_modules.md#56-loading-modules)). A local `let` in a function body is still declared before it is used.

## 7.3 `return` and `throw`

- `return e` gives the value of **the block it is in**. A function body is a block, so a `return` written at the top level of the function body gives the return value of the function; a `return` written in the block of a branch arm gives the value of that branch, and does not leave the function ([6.8.1](06_expressions.md#681-if)).
- A `return` without a value and `return null` are used in blocks that have no value.
- `throw e` gives an error value and has the same structure as `return`: it does not unwind the call stack, and there is no "propagation" or "catching".
  - The enclosing function must declare its return type as `T!`; otherwise it is `SemanticDiag::ThrowOutsideFallible`.
  - `e` must be an error value: one constructed by `error(kind, message)`, or one bound by `case error e`; otherwise it is `SemanticDiag::ThrowNotAnError`.
- `raise` is not part of the language and is not a reserved word (it is an ordinary name); an error value is returned with `throw`.

```cml
func check_age(age: int): int! {
    return if age < 0 then {
        throw error('InvalidAge', 'age is negative')
    } else {
        return age
    }
}

func main(): int sync {
    println(check_age(30) !! 0, check_age(-1) !! 0)
    println(check_age(-1))
    return 0
}
```

```text
30 0
error InvalidAge: age is negative
```

### 7.3.1 `yield`

- `yield e` gives **one element** of a stream, and is written in a function body only. The function that writes it produces a stream and not a value, so its head must write the return type as the stream type `T..` (design principles 5.4).
- A round may `yield` several times, and the resume point is the position of the `yield`: a loop, a branch, a `match` arm or a `sync` block in the region may all write one, nested to any depth. There is no delegation: handing an inner stream out is written `for x in inner do yield x`.
- Statements after a `yield` run only when the consumer pulls another element. The stream gives one element at a time and its values are linear; a generator function may be called several times, and each call gets its own stream. `return` ends the stream, without a value; a stream that produces no element at all is a legal empty stream.
- A fault happens at the pull that produces that element (a fault is not a value), and elements already handed out are unaffected.
- The syntax reads it (`yield` is a keyword and `retStmt` reads it, see [8.2](08_top_level_and_grammar.md)); **the generator state machine is not built yet** ([`../../design/io-concurrency-streams/plan.cn.md`](../../design/io-concurrency-streams/plan.cn.md), slice E): compiling a function body that writes `yield` reports `SemanticDiag::FeatureNotSupported` with `'generators'` in the message -- it does not run the `yield` as a `return`.

#### 7.3.2 The value of a stream, and one pull

- The value of a stream **is its step function together with its state**: a value of `T..` is a zero-argument function value of the signature `() => (T, T..)?` -- calling it answers the next element together with **the rest of the stream**, and `null` when the stream is over. The state lives in what the function value captured, so a stream needs no runtime representation of its own and is an ordinary function value: it writes in a signature, is passed and returned (design principles 5.4).
- The other reading holds too: **exactly that** signature makes a function value a `T..` (`let s: int.. = step(3)`), any other signature does not, and a `T..` is not converted from anything else. `T..` and its step signature are two writings of one runtime value, and the conversion moves nothing.
- Consuming is a pull through that signature: `s()` pulls one element. A stream is a value and is pure, so pulling the same stream value twice answers the same element, and **the consumer holds the rest it was handed and passes it on** -- an empty stream is one that answers `null` at once (see [7.3.1](#731-yield)).
- `s()` takes no arguments: the step signature has none, and `s(1)` is an ordinary mismatch report.
- On each execution path a stream value has **one consumer at most**: a `s()` and a `for x in s` are both consumption, reading one value twice is a compile error, and the diagnostic names **both places** and writes out the primitive `tee(s, n)` (which is not in the standard library yet); two arms of one branch that each consume once **count as one**, because one path takes one of them (design principles 5.4).
- A stream writes directly in a `for`: every round of `for x in s` is one pull, expanded on the graph into the `while` that already exists (no new node kind, no new driver, no new loop form; a back end sees the LOOP node it always saw). The state of the head is the state of the loop, and `x` is the element of this round's pull. Every round **judges its condition first and takes its element afterwards**, the order an array takes: `for x in s while c` asks `c` first and calls the step function only where `c` holds, and a pull that answers `null` ends the loop -- so `for line in read_lines(f) while n < 10` does not read line 11. An impure call in the condition without `sync` is not executed and is warned about, exactly as in a statement. An empty stream asks `c` once, pulls nothing and runs no round, and its value is the initial state (see [7.3.1](#731-yield)).

## 7.4 Functions

### 7.4.1 Declaration

```
annotation* (with <with parameters>)? export? modifier* func name(parameters): return-type sync? { statements }
```

```cml
func add(a: int, b: int): int {
    return a + b
}

func hello(name: string) sync {      // no return value; a sync function body
    println('hello', name)
}
```

- Each parameter is written `name: type`. A parameter preceded by `var` is a mutable reference ([4.4](04_execution_model.md#44-var-and-the-order-of-writes)).
- The return type can be omitted and is inferred from the function body; a recursive function must write out its return type. When the body has no `return`, the value of the function is the value of the last statement of the body: that value when the statement is an expression with one, `void` otherwise. Note that the value of `println(x)` is `x` (so that `x->println->f` can go on), so `func main() sync { println('a') }` returns the string `'a'`, and the default pipeline writes the result of `main` to the output: the line appears twice. Declare the return type `: void`, or end with `return 0`.
- A `sync` before the function body makes the function body a sync block; a `sync` before `func` is a function modifier. The two have different meanings ([4.3](04_execution_model.md#43-sync)).
- A function can be declared at the module top level, in a namespace, or in a block (a local function). Functions can be mutually recursive, and need not be declared before use.
- An ordinary parameter may write a default, and a call may give its ordinary arguments by position or by name. The rules are the `with` rules ([7.4.2](#742-with-parameters)):

```cml
func scale(x: int, by: int = 2, rounds: int = 1): int {
    return x * by * rounds
}

func main(): int sync {
    println(scale(3))                    // both defaults: 6
    println(scale(3, 4))                 // by by position: 12
    println(scale(3, rounds: 5))         // skip by, give rounds by name: 10
    println(scale(3, by: 4, rounds: 5))  // both by name: 60
    return 0
}
```

- **A default is a compile-time constant expression**, evaluated once where the declaration is and filled into each call: it is not evaluated again at a call. So it cannot read a parameter of the same declaration (`ParamDefaultNotConstant`), nor be a value computed at run time; a type that does not fit is `ParamDefaultWrongType`.
- **A parameter after one with a default must have a default too** (`ParamAfterDefault`), so that giving the parameters by position leaves no hole.
- **A named argument comes after the positional ones**, spelled as in a struct literal: `scale(3, by: 4)`. A name that is not a parameter of the function is `ParamArgumentUnknown`, the same parameter given twice is `ParamArgumentDuplicate`, a parameter with no default left without a value is `ParamArgumentMissing`, and a positional argument after a named one is `ParamArgumentPositionalAfterNamed`. A name is resolved at the call: a callee that is one known declaration answers with its parameter names, and a callee that is several overloads answers when the parameter at a position carries the same name in every one of them, so that `opt: ...` holds for each of them — a position an overload names differently, or a name that lands at different positions in different overloads, is `OverloadParameterNames`. A call of several overloads fills no default: a default belongs to one declaration. A callee that is not a known declaration (a function value, or an operator group whose entries have different signatures) gives an ordinary argument by name as `ParamArgumentCallee`.
- **A function value carries no defaults**: after `let g = scale`, every parameter of `g` has to be given. A default belongs to the declaration, not to the type.

### 7.4.2 `with` parameters

Camel distinguishes two kinds of parameter: the **inputs** a function is to process (ordinary parameters, the data flowing through the function) and the **attributes** a function is configured with (`with` parameters, that is, context parameters). Configuration and state that other languages hold in the members of an object are, in Camel, the `with` parameters of a function; the weights of a model are suited to being written as `with` parameters.

```cml
with <lo: int, hi: int>
func clamp(x: int): int {
    return if x < lo then lo else if x > hi then hi else x
}

func main(): int sync {
    println(clamp<0, 10>(42))
    println([-5, 3, 99]->map<(x: int): int => x->clamp<0, 10>>)
    return 0
}
```

```text
10
[0, 3, 10]
```

`with` parameters are not generic parameters and cannot be used to declare type parameters.

A `with` parameter may write a default, and a call may give its `with` arguments by position or by name:

```cml
with <rounds: int = 8, strict: bool = false>
func repeat(x: int): int {
    return if strict then x * rounds else x + rounds
}

func main(): int sync {
    println(repeat(10))                 // both defaults: 18
    println(repeat<2, true>(10))        // both by position: 20
    println(repeat<strict: true>(10))   // rounds skipped, strict by name: 80
    println(repeat<16>(10))             // rounds by position: 26
    return 0
}
```

- **A default is a compile-time constant expression**, evaluated once where the declaration is and filled into the graph at each call: it is not re-evaluated per call. It therefore cannot read a `with` parameter of the same declaration (`WithDefaultNotConstant`), nor be a value computed at run time; a value of the wrong type is `WithDefaultWrongType`.
- **A parameter after one with a default must also have a default** (`WithParameterAfterDefault`), so that a positional call never leaves a gap before a value.
- **A named argument comes after the positional ones**, spelled as in a struct literal: `f<rounds: 16>`, `f<strict: true>`. A name that is not one of the function's `with` parameters is `WithArgumentUnknown`, giving one parameter twice is `WithArgumentDuplicate`, and leaving out a parameter with no default is `WithArgumentMissing`. A named argument may only be used in a call; the function value `f<rounds: 8>` binds the arguments it is given in order and applies no default.
- **A function value carries no defaults**: after `let g = repeat`, every `with` parameter of `g` has to be given. A default belongs to the declaration, not to the type.

### 7.4.3 Overloading

Functions in the same block with the same name and different parameter types are overloads, each with its own subgraph. A call takes the one whose parameters accept the arguments and whose `when` condition holds; when more than one candidate is left it is `SemanticDiag::AmbiguousCall`, and declaration order never decides (design principles 4.4, 5.1).

```cml
func show(x: int): string { return 'int ' + string(x) }
func show(x: string): string { return 'string ' + x }

func main(): int sync {
    println(show(1), show('a'))
    return 0
}
```

```text
int 1 string a
```

- A second function of the same name with exactly the same parameter types is `SemanticDiag::Redeclaration` (the return type and the parameter names do not take part in the distinction), unless **both declarations write `when` and the two conditions exclude each other**: then they are overloads, one family of types each. Mutual exclusion is decided type by type: for every type the compiler registers (the scalar types, and one sample of each of the shapes a value has -- an array, a tuple, a function and a struct) the type variables of each condition are bound to that type and both conditions are answered; **a type that satisfies both** is the overlap, reported as `SemanticDiag::WhenDeclarationOverlap`, whose message names that type and both declarations (each with its condition). When a condition has a part that cannot be evaluated at compile time (it cannot be answered), the pair is refused by the older rule rather than taken for disjoint. The check is the same for every same-named overload that writes `when`; it is not a special case of `#impl`.
- A generic function cannot share its name with other functions.
- A name that stands for a group of overloads cannot be used as a value ([6.9](06_expressions.md#69-lambdas-and-function-types)).

### 7.4.4 Modifiers

| Modifier | Meaning |
|---|---|
| `sync` | The function's body is a sync block ([4.3.3](04_execution_model.md#433-sync-on-a-function-body-function-modifier)) |

There are no other modifiers. A function implemented in C++ is a body-less `export func` declaration (design principles 6.4); a helper used only inside its module is a plain `func` without `export`, since `func` is module-private already.

`#macro` is not a modifier but a standard tag written before a function declaration ([4.5](04_execution_model.md#45-macro-functions)).

### 7.4.5 Operator overloading

```
func operator <operator>(<parameters>): <return type> { <statements> }
```

(The notation is provisional.) Operators are calls of functions (`a + b` is `__add__(a, b)`, [2.4](02_lexical.md#24-keywords)), and a program can add overloads of those functions for its own types:

```cml
type Vec = { x: float, y: float }
type Mask = { bits: int }

func operator +(a: Vec, b: Vec): Vec { return { x: a.x + b.x, y: a.y + b.y } }
func operator -(a: Vec): Vec { return { x: 0.0 - a.x, y: 0.0 - a.y } }    // unary
func operator *(k: float, v: Vec): Vec { return { x: k * v.x, y: k * v.y } }
func operator [](v: Vec, i: int): float { return if i == 0 then v.x else v.y }
func operator &(a: Mask, b: Mask): Mask { return { bits: a.bits * b.bits } }

func main(): int sync {
    let v: Vec = { x: 1.0, y: 2.0 }
    var acc: Vec = { x: 0.0, y: 0.0 }
    acc += v                         // acc = acc + v
    let w = 2.0 * (v + acc)
    println(w.x, w[1], (-v).x, 1 + 2)
    return 0
}
```

```text
4.0 8.0 -1.0 3
```

- **Can be overloaded**: the binary `+` `-` `*` `/` `%` `^` `@` `==` `<` `<=` `>` `>=` `&` `|`, indexing `[]` (the indexed value and at least one index), and the unary `-` `!` `~`. `-` with one parameter is negation, with two subtraction.
- **Overloading `==` gives `!=`** (design principles 4.3): an `a != b` that uses that overload of `==` is `!(a == b)`, so the two cannot disagree. The overload must return a `bool`, otherwise the `!=` that uses it is `SemanticDiag::OperatorOverloadInvalid`. `!=` cannot be overloaded by itself (`SemanticDiag::OperatorNotOverloadable`).
- **Cannot be overloaded**: `&&`, `||` (branches), `??`, `!!`, `?->` (branches on a null or an error), `->` (passes a value to a function), `..`, `as`, assignment, `!=` (it follows `==`), and `===`, `!==`. Declaring one is `SemanticDiag::OperatorNotOverloadable`, with the reason. A compound assignment has no overload of its own: `a += b` is `a = a + b` and follows the overload of `+`.
- An operator with the wrong number of parameters, with with-parameters or with a `var` parameter is `SemanticDiag::OperatorOverloadInvalid`. A generic overload declares its type parameters after the operator's name (`func operator +::<T>(a: T[], b: T[]): T[]`), not in a parameter's type.
- **The overloads of every layer form one set.** Every other name takes the nearest layer only (local > this module > imported > built-in); operators are the exception (design principles 10.2): the overloads of this module, those exported by imported modules, and the built-in and C++ module ones are one set, and a call takes the one that accepts its operands.
- **The more specific overload wins** (design principles 4.3). "More specific" means: an overload of the program whose parameter types are exactly the operands' types (without a value entering `T?` or `T!`), while every other one that accepts the operands takes them only as members of a family of types: a built-in or C++ module operator whose type rule accepts several types (comparing arrays, tuples and structs by content, arithmetic over tensors), or a generic overload. The call then takes the program's overload. So a struct can have an equality of its own: `func operator ==(a: V, b: V): bool` makes `p == q` (both `V`) call it instead of comparing by content; other struct types are still compared by content. A built-in operator declared for exactly those types (`int + int`, `float == float`) is as specific: declaring such an overload is `SemanticDiag::OperatorOverloadConflict`.
- Otherwise two that accept the operands are `SemanticDiag::AmbiguousCall`, which lists them (two generic overloads, or two modules each giving one for the same parameter types); two overloads with the same parameter types in one module are `SemanticDiag::Redeclaration` (when both write `when` and the conditions exclude each other they are arms of one overload, see [7.4.3](#743-overloading)). An ordinary call, and a partial application (`f<w>` used as a value), report the same diagnostic when several candidates fit in the pass being used.
- **Export**: `export func operator +(...)`. Importing a module (`import { V } from m`, or `import m`) brings all the operator overloads it exports, as for the operators of a C++ module; they are not, and cannot be, imported by name.
- **An operator declared in a namespace block belongs to the module** (design principles 4.3): its name carries no namespace (an operator has no name to qualify), it is exported and joins the overloads as one declared at the top level does, and its signature and body look names up in the block, so they may use the block's types without a prefix.
- On the graph an operator overload is an ordinary function call. Names of the form `__x__` remain reserved; programs do not write them.

## 7.5 Generic functions

A generic function declares its type parameters at its head, after its name in `::<…>`. A function with `::<…>` is generic, one without is not.

```cml
func first::<T>(xs: T[]): T {
    return xs[0]
}

func pick::<T>(c: bool, a: T, b: T): T {
    return if c then a else b
}

func my_map::<T, U>(xs: T[], f: (x: T) => U): U[] {
    return xs->map<f>
}

func main(): int sync {
    println(first([1, 2, 3]), first(['a', 'b']))
    println(pick(false, 1.5, 2.5))
    println(my_map([1, 2, 3], (x: int): string => string(x) + '!'))
    return 0
}
```

```text
1 a
2.5
[1!, 2!, 3!]
```

- A generic function is a **subgraph template**. Each call with a set of concrete types instantiates a subgraph in which all types are concrete (the same set of types is instantiated only once; in `std::rgir` one can see subgraphs named `first<int>` and `first<string>`). The graph contains only concrete types, and no type information is needed at run time.
- **A type variable takes its type from the arguments.** Each argument is unified with its parameter's type; when the argument there says nothing (it is `null`, or a generic function that is itself waiting for its types), the type found at another occurrence. The constraints of all arguments are collected before any is checked: every parameter is then checked against its type with the variables replaced, without implicit conversion (except that a value enters a `T?`/`T!` parameter, see [3.4](03_data_model.md)). `pick(true, 1, 2.5)` is an error (`T` is bound to `int`); a variable that cannot be determined is a compile-time error, and the diagnostic says which.
- A name declared at the head can be written in any type position of the signature: array elements, tuple elements, struct fields, the parameters and return value of a function type, the inner type of `T?`/`T!`, the arguments of `Map`/`Set` and of type functions. It cannot be an argument of a module type.
- Only a name declared at the head is a type variable. A plain name in a parameter type that denotes no type and is not declared at the head is an unknown type (`SemanticDiag::UnresolvedTypeReference`); it is not bound automatically. One name declared twice at the head is `SemanticDiag::TypeVariableRedeclared`; one declared but never written in the signature is the warning `SemanticDiag::TypeVariableUnused`, and the declaration stays usable. When a head name also denotes a type, it is the type parameter inside its declaration: the head declaration wins.
- A result-only variable is inferred from an expected type: `let xs: int[] = empty()`, a known parameter type, or a declared return position. Without sufficient context, provide explicit type arguments: `empty::<int>()`. Positional type arguments follow the order of the head declaration, and they can be given by name, which does not depend on position: `pair::<A = int, B = string>(..)`. One list is all positional or all named, never mixed; by name every type variable must be given, and a wrong or repeated name is a compile-time error; built-in operators take their type arguments by position only. Type arguments are consumed at compile time. Value context arguments remain `f<w>`; combine them as `f::<T><w>(x)`. An explicit instance can also be a function value: `let f = id::<int>`.
- **The function body is checked at instantiation.** Type errors in the function body are reported at instantiation; the diagnostic gives the position of the error in the function body and states which instance it is and which call instantiated it. A generic function that is never called is not checked.
- Recursive generic functions can be used (the return type must be written out). A function that switches to a larger type at every recursion cannot terminate instantiation, and an error is reported when the nesting reaches 8 levels.
- `T` can be used in the function body: the parameter types of a lambda, `let z: T = 0`, `e as T`.
- **Export**: a generic function can be `export`ed, and after an `import` another module calls it as usual (the qualified name `lib::f(x)` can also be written). What is exported is the template; there is one instance per set of types in the whole program, whichever module calls it. Names in the function body are resolved in the module that declares it, so it can use functions of that module that are not exported, other generic functions, its own imports and the values of its top level.

The types of `with` parameters use the names declared at the head too:

```cml
with <f: (acc: U, x: T) => U, init: U>
func my_fold::<T, U>(xs: T[]): U {
    return xs->reduce<f, init>
}

func main(): int sync {
    println(['a', 'bb']->my_fold<(acc: int, s: string) => acc + len(s), 0>)
    return 0
}
```

```text
3
```

**A generic function may stand wherever a function is expected**, also as an argument of another generic function. The compiler delays its instantiation until the type variables can be determined from the context; the delay exists at compile time only, and the graph holds the concrete instances.

```cml
func apply::<T, U>(f: (x: T) => U, x: T): U { return f(x) }
func id::<A>(x: A): A { return x }
func pair::<A>(x: A): (A, A) { return (x, x) }

func main(): int sync {
    println(apply(id, 3), apply(pair, 'a'))   // T comes from 3: id becomes id<int>, so U = int
    println([[1, 2], [3, 4]]->map<pair>)      // handed to a built-in higher-order operator
    let f: (x: int) => int = id               // the written type chooses the instance
    println(f(4))
    return 0
}
```

```text
3 (a, a)
[([1, 2], [1, 2]), ([3, 4], [3, 4])]
4
```

- The context may be: a parameter of another function (a function type), the other arguments of a generic function, a declared type (`let f: (x: int) => int = id`), a declared return type (`return id`), a built-in higher-order operator (`map`, `filter`, `reduce`, ...). The type rule of a built-in operator is C++ code and cannot say which function it expects, so the compiler tries the element types of the other arguments and takes the instance the operator accepts.
- When nothing determines the variables it is the compile-time error `SemanticDiag::GenericFunctionAsValue` (or, for an argument, `SemanticDiag::GenericArgumentsMismatch`), which names them: `let g = id` has no context (write the type); in `apply(id, id)` each argument waits for the other.
- An imported generic function can be a value too (`xss->map<reverse>`, with `reverse` from the `array` module).

**Rest parameters**: the last parameter of a function declaration may be written `..ts`. It has no type and stands for every argument left over at a call. The fixed arguments written at the call bind the fixed parameters in order, and the remaining arguments become the elements of `ts`; `ts` is a tuple known at compile time and can be read with `indices ts`, `ts.[j]` and `..ts`.

```cml
func sum(..ts): int {
    return static loop acc = 0 for j in indices ts do acc + ts.[j]
}

func main(): int sync {
    println(sum(1, 2, 3))
    println(sum(..(4, 5)))
    return 0
}
```

```text
6
9
```

- **The call is expanded at compile time.** A function with a rest parameter has no runtime arity, so no instance graph is built for it and no call node is made: every direct call expands its body into the caller's graph, with the fixed parameters bound to the argument nodes and `ts` bound to the remaining nodes. `ts` may have 0 elements.
- **`ts` is a compile-time tuple, not a value.** It has three uses only: `indices ts` reads its positions, `ts.[j]` reads one element, and `..ts` spreads the elements as arguments. None of the three builds a `FILL` for `ts` or an `ACCS` for an element. Putting `ts` itself where a runtime value is needed (printing it, returning it, assigning it, passing it as an ordinary argument) reports `SemanticDiag::StaticTupleEscapes`; it is not silently materialized into a runtime tuple. Write a tuple literal `(a, b)` when a runtime tuple is wanted.
- **A rest argument** is written `..e` and must be the last argument of the call. `e` must be a compile-time tuple: a tuple literal, a tuple comprehension, or the `..ts` of an enclosing rest function. A runtime tuple value has no fixed arity and is `SemanticDiag::RestArgumentNotTuple`; an argument after `..e` is `SemanticDiag::RestArgumentNotLast`.
- A rest parameter must be last (`SemanticDiag::RestParameterNotLast`), cannot write a type (`SemanticDiag::RestParameterTypeNotSupported`), and can appear only in a function declaration (writing `..` in a lambda, a function type or a rule is a syntax error). Too few arguments for the fixed parameters is `SemanticDiag::RestArgumentsMismatch`.
- A function with a rest parameter is **not a value**: it cannot be assigned, passed as an argument, or exported and used as a function value (`SemanticDiag::RestFunctionNeedsExpansion`); it can only be called directly. A call may be in another module: the body's names are still resolved in the module that declares it (as for a generic instance), while the expansion happens in the caller's graph. A body that names a top-level **value** of its own module has no node for it in the caller's graph and reports `SemanticDiag::RestFunctionNeedsExpansion`; functions and the call's own arguments are unaffected.
- A `with`-argument that names a generic function or a set of overloads (`tree_map<twice>`, `tree_map<bump>`) is resolved inside the callee for each call's argument types; this needs a callee with a rest parameter (the instance is expanded at the call). An ordinary generic function still takes one function value, chosen by the parameter types.

**Constant value parameters (`constas`)**: among the ordinary parameters a declaration may write `constas n: int`. Its value is known at compile time and is part of the declaration's identity, not a runtime argument.

```cml
func total(constas n: int, x: float[n]): float {
    return loop acc = 0.0 for v in x do acc + v
}

func main(): int sync {
    println(total(3, [1.0, 2.0, 3.0]))
    println(total(5, [1.0, 2.0, 3.0, 4.0, 5.0]))
    return 0
}
```

```text
6.0
15.0
```

- **The value enters the instance key.** Every distinct constant instantiates a subgraph (`total<3>`, `total<5>`); the same types and value instantiate once. Types and values together are the key: two calls that differ only in a string are two instances (`label<"w">`, `label<"h">`).
- **Any number, any position.** A declaration may take several `constas` parameters, at any position among the ordinary ones; the call writes each value at its parameter's position (`place(1, 2)` binds `x = 1`, `n = 2`). Each value is its own part of the instance key.
- **The type is not limited to `int`.** `string`, `int`, `float`, `bool` and any other type with compile-time constants work; the value must be compatible with the parameter's type. The type must be written (`SemanticDiag::ConstasTypeRequired`). An `enum` member cannot be passed as a compile-time value yet (`SemanticDiag::ConstasValueNotConstant`) until `enum` values have a compile-time representation.
- **The body reads the value.** Inside an instance `n` is a compile-time constant: it can be a named dimension in a type (`f32[n]`), a field name (`t.[n]`), or an ordinary value in arithmetic, conditions and printing. No type has to read it: a body that is the only reader is legal.
- **It is not a runtime parameter.** The call gives the constant, but it does not enter the call node: the instance's parameter list has no `n`, and the call passes only the other arguments (`total(3, xs)` has one argument, `xs`).
- **A value can be handed on.** A `constas` value may be passed to another `constas` parameter, and it may come from a compile-time name such as the field name `static for` binds or a position of `indices`. `func columnOf(constas n: string, ..ts) { return column(n, ..ts) }` is legal: this is how `tree_map` hands the field name and the trees to `column`.
- **It combines with a rest parameter.** `func column(constas n: string, ..ts)` is legal: the value is bound before the expansion and `ts` takes the arguments left over. The `column` of design note 3.1.1 can be written today as

  ```cml
  func column(constas n: string, ..ts): (int, int) {
      return ( static for j in indices ts => ts.[j].[n] )
  }
  ```

  Two calls that differ only in the string are two graphs, each reading the field its value names.
- A declaration with a `constas` parameter **has no function value**: the constant must be written at the call, so it cannot be assigned or passed on (`SemanticDiag::ConstasFunctionNeedsCall`). A body-less declaration (the declaration of a C++ operator) cannot take one (`SemanticDiag::ConstasInNativeDeclaration`).
- The value must be a compile-time constant (`SemanticDiag::ConstasValueNotConstant`): a runtime value cannot select an instance. Constants today are literals, another `constas` value, compile-time names such as the `n` of `static for` and the `j` of `indices`, and a call of a pure function whose arguments are compile-time constants (`label(first_field::<S>())`), which the compiler folds to its value; a call it cannot read at build time is refused by its own name (`SemanticDiag::FoldCallNotEvaluable`, and the other `FoldCall*` diagnostics).
- `n` is bound only inside the instance body. A named dimension is written in the declared type of a parameter (`x: f32[n]`); a type position in the body reads a dimension name as bound by the first plain occurrence in a parameter type, so `let z: f32[n]` there does not bring the instance's `n` in (`SemanticDiag::DimensionNameNotBound`).

**Current restrictions**: a generic function cannot be decorated; a generic function with context arguments bound (`f<w>`) cannot be a value; a function with a `constas` parameter cannot be a value, cannot be combined with a body-less declaration, and an `enum` value cannot yet be a `constas` argument. The design note's `column(constas n, ts)` omits the type; today it is written `column(constas n: string, ..ts)`.

## 7.6 Annotations

Before a function declaration one can write `@name` or `@name<with arguments>`. An annotation is a function that accepts a function and returns a function; the declared name is bound to the function the annotation returns.

```cml
with <n: int>
func times(f: (x: int) => int): (x: int) => int {
    return (x: int): int => f(x) * n
}

@times<3>
func inc(x: int): int { return x + 1 }

func main(): int sync {
    println(inc(1))        // (1 + 1) * 3
    return 0
}
```

```text
6
```

A call of an annotated function is checked against the parameters of the function the annotation returns. Annotated functions can take part in overloading.

An annotation may also stand before a block. The block is then a function of no parameters, passed to the annotation, and the function the annotation returns is called right where the block is written. Otherwise the block behaves as without the annotation: its names are not visible after it, a `return` in it gives the block's value, and when nothing takes its value, its statements run only as a block without the annotation would run them ([7.8](#78-blocks)).

```cml
func doubled(f: () => int): () => int {
    return () => f() * 2
}

func next_doubled(x: int): int {
    @doubled
    {
        return x + 1
    }
}

func main(): int sync {
    println(next_doubled(4))    // (4 + 1) * 2
    return 0
}
```

```text
10
```

A library can declare an annotation that **grants an attribute** to the code it is written on, instead of wrapping it: written with constant arguments, it is applied as the graph is built, and the nodes of that function or block (with the branch arms, loop bodies and lambdas written in it, not the functions they call) carry the attribute, which passes and back ends of that library read; the library declares the kind of the attribute's values and how they combine and merge (`camel/core/node_attrs.h`). Such an annotation does not change what the code computes.

### 7.6.1 Numeric permission tags

Numeric permissions are tags (design principles 7.2, 7.5): `#reassociate`, `#contract` and the others, written before a function declaration or a block, grant that code permission to change floating-point results. They are not decorators: they do not change the function's value, they only relax the canonical algorithm ([3.2.1](03_data_model.md#321-the-numeric-contract-of-floating-point)).

`reassociate` and `contract` are the default permissions of 7.3: they are allowed without a tag, and the tag only states them. Every other permission needs its tag. `#strict` (7.4) is the opposite: the code it marks is computed with the canonical algorithm bit for bit and uses no permission at all, the default grants included.

| Tag | Allows | Default |
|---|---|---|
| `#reassociate` | reordering or regrouping a reduction (a chain of additions or multiplications, `sum`, the inner products of a matrix multiplication) | allowed |
| `#contract` | contracting a multiplication and an addition into one fused multiply-add | allowed |
| `#approx_math` | approximate math functions (approximate division and square root included) | needs the tag |
| `#change_precision` | lower or higher input or accumulation precision (TF32, bf16, or an f64 accumulator for f32) | needs the tag |
| `#finite_math` | assuming no NaN, no infinity, no distinction of the sign of zero, subnormals may become zero; the four can also be written apart: `#no_nans`, `#no_infs`, `#no_signed_zeros`, `#flush_subnormals` | needs the tag |
| `#nondeterministic` | results that vary with the schedule (floating-point atomics, for example) | needs the tag |

```cml
import { sqrt } from math

#[reassociate, contract]
func dot(a: float, b: float, c: float, d: float): float {
    return a * b + c * d          // may be evaluated as fma(a, b, c * d)
}

func norm(x: float, y: float): float {
    let s = x * x
    #approx_math
    {
        return sqrt(s + y * y)    // the code of the block may use an approximate square root
    }
}

#strict
func canonical(x: float, y: float): float {
    return x * y + 1.0            // the canonical algorithm: no default grant, no permission used
}
```

- **It applies to the code it is written on**: the function body or the block, with the branch arms, loop bodies and lambdas lexically inside it. A block's permissions join those of the function around it.
- **A called function does not inherit them**: the functions `dot` calls keep the canonical algorithm unless they carry their own permission tags. After inlining every node keeps its own permissions.
- **`#strict` takes everything away**: the function or block it marks (with the branch arms, loop bodies and lambdas inside it) has the empty set as its effective permissions -- the default `reassociate` and `contract` are gone, and an inner grant has no effect. When both are written, `#strict` wins.
- **A permission allows, it never requires.** The CPU back ends (`std::nvm`, `std::fvm`, `std::jit`, `std::prl`, `std::cpp`) always give the canonical results; later back ends (Tiles, for example) may use other algorithms on the nodes that have a permission. `std::opt` uses the defaults of 7.3; `std::opt safe_fp=true` works under the same policy as `#strict`.
- There is no module-wide default and no command-line or pipeline switch; a back end's entry point (such as `tiles::run`) takes no permissions.
- The permissions are recorded on every node of the graph: `std::rgir` and `std::gir` show `numeric="..."` on the nodes that have any. `#strict` is a tag of the code and appears as `attrs="strict"` on its nodes. Common-subexpression merging and tensor fusion do not merge nodes with different permissions.
- **`@numeric(permission, ...)` is `SemanticDiag::InvalidAnnotation` at the annotation**, naming the tag form to write instead (`#[reassociate]`); an empty `@numeric()` and any `@name(args)` are rejected the same way.

### 7.6.2 Tags: `#`

`#` writes a **tag** (design principles 6.2); the `#!` of the first line is ignored to the end of the line. A tag is a name in the same namespace as functions and types: declared in this module, imported by name, or qualified as `module::tag`.

A tag is declared at the top level of a module; after `on` come the positions it may be written at:

```cml
export tag kernel on nodes                  // before a block, function or type: the nodes in it
export tag fast on func, nodes              // also the function's own declaration
export tag width(w: int) on type, field     // with parameters, which are compile-time values
export tag vjp::<F>(rule: F) on func when is_function::<F>()   // a function parameter: the argument is a function, and the fact restores to that function
tag finite = [kernel, fast]                 // a group: its members apply together
```

The positions are `func` (a function declaration), `type` (a type declaration), `field` (a struct field), `nodes` (the nodes written in the region) and `param` (reserved). `read_by` says which passes read the tag; when none of them is in the pipeline a warning is reported where the tag is written. Only a pass that declares itself a reader of tag facts counts: the core dump passes and `std::opt`, and module passes such as `tiles::compile` and `tensor::fuse`; a pass written in Camel can declare the tags it reads with `#reads(tag, ...)` on its function, which counts like `read_by`. A program may not declare a tag with the name of a core permission (nor `finite_math` or `strict`; `tag reassociate`, for one): that name always means the core's tag, so the declaration would never be read, and `SemanticDiag::TagShadowsPermission` refuses it at the declaration.

The core carries a set of **numeric permission tags** (design principles 7.2), written without an import and `on nodes`: `reassociate`, `contract`, `approx_math`, `change_precision`, `no_nans`, `no_infs`, `no_signed_zeros`, `flush_subnormals`, `nondeterministic` (allows results that vary with the schedule, such as floating-point atomics), and the group `finite_math` that holds the first four of those assumptions; `strict` (7.4) is a core tag too, written `on func, nodes`. `#contract` lands in the node's permission column (the `numeric="..."` of a dump) and `#strict` in its attribute set (`attrs="strict"`); readers get permissions through one query (`permissionsOf` / `effectivePermissions` in `camel/core/tags.h`), and `effectivePermissions` returns the empty set for `#strict`. The defaults are those of 7.3: `reassociate` and `contract` are allowed anyway, the others still have to be written.

A rule declaration may be preceded by `#requires(permission, ...)`: the arguments are permission names, the core's group `finite_math`, or a group the module declares with `tag name = [permission, ...]` (members are expanded recursively, and the group name means the union of its permissions). The engine applies the rule only where the numeric policy of the nodes allows those permissions ([46 Rules](../../technical/46_rules.md)). A requirement that names an ordinary tag, an undeclared name, or `#requires` outside a rule is a located error, never a silent pass.

There are three forms of `#`, written before a function, a type declaration, a struct field or a block:

<!-- camel-snippet: fragment: the three tag forms are illustrative; width is a tag a module declares, and the bodies are elided -->

```cml
#kernel
func matmul(a: T, b: T): T { ... }

#width(32)
type Packed = {
    #width(8) a: int,
    b: int,
}

func main(): int {
    #fast
    {
        // the nodes written here carry fast
    }
    return 0
}
```

`#name`, `#name(arguments)` and `#[a, b(v)]` (a group). A tag's arguments are compile-time values: a literal, a top-level constant or a function name. Each argument must be a constant of the kind its parameter declares — `int`, `float`, `bool`, `string`, or a function (a parameter that is a type parameter satisfying `is_function`, or a concrete function type); a real is carried as a real attribute by its bits and `1.5` prints as `1.5`. Writing a tag where its declaration does not list it is an error, located at the tag. So are a wrong number of arguments, an argument that is not a compile-time value, an argument whose kind is not the declared one (`SemanticDiag::TagArgumentType`), and a parameter of a kind a tag cannot carry yet (`SemanticDiag::TagParameterType`).

A tag of `on nodes` is a **node attribute** (`camel/core/node_attrs.h`): a tag written before a function body or a block holds for the nodes written in it, it accumulates with same-name tags outside by the attribute's combine rule, and an inner same-name tag with a parameter takes the inner value. A tag of `on func`, `on type` and `on field` belongs to the declaration itself: a C++ module reads it in one query, `tag::factsOf` (tags, permissions and readers; `tag::ofDeclaration` / `tag::ofField` remain); reading it from Camel code waits for the first stage of generics.

A parameter may also be a function: it is declared as a type parameter with the condition `when is_function::<F>()`, the same spelling as a pass's signature `opt::<F>(f: F): F! when is_function::<F>()`; when the signature is fixed, write the concrete function type `(x: T) => R`. Its argument is a function, and the tag's fact is that function itself (the argument restores to the function), not its name and not an internal handle; whether its signature fits is checked by the tag's reader where the tag is written. The autodiff module declares `export tag vjp::<F>(rule: F) on func when is_function::<F>()` this way: `#vjp(rule)` before a function declaration makes calls of that function use the rule instead of the derivative of its body ([26 Autodiff](../../technical/26_autodiff_as_graph_rewrite.md)).

**`#impl(f)`: the Camel body is the semantics, the native kernel is the execution** (design principles 10.4). It is written before a function declaration that has a body:

```cml
#op(':op/add_l')
func add_kernel(a: int, b: int): int          // a body-less declaration of this module, the kernel

#impl(add_kernel)
export func add(a: int, b: int): int {
    return a - (0 - b)                        // the semantics (the Camel definition)
}
```

- The syntax is `#impl(kernel name)`: one argument, a name rather than a value, because a body-less declaration has no function value to pass.
- `f` must be a body-less declaration of **the same module**. By design it is a private declaration of that module (not exported: the kernel is an implementation detail, not part of the module interface), written beside the function. A declaration of another module (`SemanticDiag::ImplKernelForeign`), a name no declaration brings (`ImplKernelUnknown`), a function that has a body (`ImplKernelNotBodyless`), and `#impl` on the body-less declaration itself (`ImplOnBodylessDeclaration`) are all refused, and `#impl` cannot be written on a lambda, a type, a field or a block (tag-position error). A module that owns a `.cmo` (as `stdlib/tensor.cml` does for `tensor.cmo`) may declare its kernel and write the definition just the same: a `.cml` and a `.cmo` of the same name are **one module** (H94), and a declaration module may hold bodies (design principles 10.4: a declaration module may hold bodies, and `DeclarationModuleHasBody` is gone).
- **A generic function may carry `#impl`**: the kernel is a body-less declaration with the function's own type parameters (`sum_kernel::<T>`), and by H228 ("one operator, per-type executors") the executor is chosen at instantiation — a fixed rule of the declaration matches by signature, and a rule declared with type parameters is solved with the instance's types. When a `T` has no executor, that instance runs the function body: the body is the semantics, so the result is the same, and `std::profile` shows which path it took.
- **The signatures must agree**: the same number of `with` and normal parameters, the same types and `var` marks, the same result type, and no `constas` parameter on the kernel; otherwise `ImplSignatureMismatch`.
- **Which types a kernel covers is what the kernel declaration's own `when` says** (design principle 4.4: on a rule, `when` means "this rule applies where …"; 10.4; H228). A kernel declaration that carries the function's own type parameters solves for every type the function can be instantiated with, so which instances it implements has to be written down separately: a `when condition` on the body-less declaration is evaluated at instantiation with the instance's argument types (they bind the declaration's type parameters, and the predicate is resolved in the declaring module). A condition that is false makes that rule inapplicable to this instance, and overload selection tries the kernel's other rules (a fixed rule is still the more specific one, design principle 4.3); when no rule applies, the instance has no executor and runs the function body — the same path as "a `T` with no executor", and `std::profile` shows which path it took just the same. A condition that cannot be answered from the types alone (it reads the declaration's own `with` parameter, it is not a compile-time query, …) is reported with the existing diagnostics (`WhenConditionNotConstant` and the rest) rather than silently taken for an arm that applies. For example:

  ```cml
  func is_armed::<T>(): bool {
      return match type T {
          case i32 => true
          case _ => false
      }
  }

  #op(':op/add_i')
  func add_kernel::<T>(a: T, b: T): T when is_armed::<T>()   // implements the i32 instances alone

  #impl(add_kernel)
  func add::<T>(a: T, b: T): T { return a - (0 - b) }        // an int instance has no executor: it runs the body
  ```

  The reader of this condition is the `#impl` path that chooses the executor (`Builder::declaredRuleApplies`); elsewhere a body-less declaration is still matched by signature alone (ordinary call resolution, function values), so a kernel split into arms is chosen by arm only through its `#impl` function: a call written with the kernel's own name, or a function value taken from it, has no condition to choose between same-named kernels.
- **Same-named kernel declarations are split into arms by `when`**: one kernel name may be declared several times, and when the conditions exclude each other the declarations are overloads, one family of types each (design principles 4.4: same-named declarations split into arms by mutually exclusive `when` conditions are overloads, which is why the run-time `switch (codeOf(norm))` in `array_reduce.cpp` is gone). One declaration per family's executor is all it takes, with no union condition and no dispatch on the type inside the body:

  ```cml
  func is_armed::<T>(): bool {
      return match type T {
          case i32 => true
          case _ => false
      }
  }

  func is_f32::<T>(): bool {
      return match type T {
          case f32 => true
          case _ => false
      }
  }

  #op(':op/add_i')
  func add_kernel::<T>(a: T, b: T): T when is_armed::<T>()   // an i32 instance takes :op/add_i

  #op(':op/add_f')
  func add_kernel::<T>(a: T, b: T): T when is_f32::<T>()     // an f32 instance takes :op/add_f

  #impl(add_kernel)
  export func add::<T>(a: T, b: T): T {
      return a + b                                           // an instance outside both families runs this
  }
  ```

  An array reduction split by element type is written the same way: `sum_kernel::<T>(xs: T[])` once with `when is_integer::<T>()` and once with `when is_float::<T>()`, each naming its own family's operator of the module, and the run-time switch on the element type in the executor goes away.
  The exclusion is checked where the declarations are, by the rule [7.4.3](#743-overloading) states: a type that satisfies both conditions is `SemanticDiag::WhenDeclarationOverlap`, naming that type and the two declarations, and a condition that cannot be answered leaves the pair refused by the older rule (`SemanticDiag::NativeDeclarationOverlap`).
- **Not inlined by default**: an ordinary call calls the kernel operator (one `OPER` node, labelled with the kernel's URI). Ordinary passes and executors do not reach the body, and the body is not inlined into callers; compiling and running it costs what calling an atomic operator costs. The semantic graph is still built and kept, out of the ordinary program's reachable graphs.
- **Expanded on demand**: a consumer that must look inside takes the semantic graph the tag kept. A function value (`let g = add`, `grad(add)`) is that graph, and at run time `Program::implBodies` returns the graphs that carry `kGraphFlagImplBody`. No production pass expands `#impl` bodies yet; the tag leaves that to autodiff, fusion, export and rules as they need it. The test-only `test::impl` pass is the demonstration: it reads `Program::implBodies`, expands the body of the tagged function, and compares it node for node with the same Camel body written without the tag. A differential test keeps the kernel path and the body path in agreement (a test that cannot be exact states its tolerance).

`@name(arguments)` is not how a tag is written: it is reported as an annotation, and the message points at `#name`.

## 7.7 Type declarations

```
export? type name (<parameter, ...>)? = type
```

```cml
type Point = { x: int, y: int }
type Signal = (int, int)
type Pred = (p: Point) => bool
```

`type` gives a type a name; the name and the type it stands for can be used interchangeably.

**A type declaration with parameters is a type function.** "Calling" it with angle brackets in a type position gives the type on the right-hand side with the parameters replaced by the arguments. It is evaluated when the graph is built and leaves no node on the graph.

```cml
type Pair<T> = (T, T)
type Entry<K, V> = { key: K, value: V }
type PairList<T> = Pair<T>[]

func sum_pair(p: Pair<int>): int { return p.0 + p.1 }

func swap::<T>(p: Pair<T>): Pair<T> {          // combined with the head type parameters
    return (p.1, p.0)
}

func key_of::<K, V>(e: Entry<K, V>): K {
    return e.key
}

func main(): int sync {
    println(sum_pair((1, 2)), swap(('x', 'y')))
    println(key_of({ key: 'id', value: 7 }))
    return 0
}
```

```text
3 (y, x)
id
```

- Arguments can only be types, and their number must equal the number of parameters; using it without arguments is an error.
- The right-hand side can only refer to types already declared, so a type function cannot refer to itself.
- It can be `export`ed, and is used in another module as `module::Pair<int>`.
- The angle brackets of a module type are interpreted by the type itself, and the arguments can be types or `const data`; the angle brackets of `f32[2, 3]`, another spelling of a numeric array, are interpreted by the array type itself (a shape argument is written without `const`: `f32[2, 3]` is `SemanticDiag::InvalidTypeArgument`).

A type expression has no type operations: `A & B`, `A \ B`, `A + B` and `A ^ B` are ordinary syntax errors (`&`, `\`, `+`, `-`, `^` mean nothing in a type position; they are the operators of an expression, so `x as T + U` is `(x as T) + U`). `keyof` and `of` are not keywords: the ones in `keyof T` and `name of T` are ordinary names, and writing them is a syntax error. `typeof e` is a whole type, the type of the value of `e`, expanded by the compiler where the type is used. It cannot be nested inside a larger type (`typeof(1)[]` is `SemanticDiag::TypeOfNotWholeType`), and an operand with no value (`void`) is `SemanticDiag::TypeOfNotAValue`.

## 7.8 Blocks

```
sync? { statement ... }
```

A block is an expression: its value is given by the `return` in the block, and without a `return` it has no value. A block introduces a scope. A block with `sync` executes **the statements it writes directly** in the order written ([4.3.1](04_execution_model.md#431-sync-blocks-block-modifier)); the branch arms, loop bodies and nested blocks inside it follow their own spelling (a sync block does not inherit inward, [design principles 3.4](../../design-principles.en.md#34-sync-blocks)). A block without `sync` is evaluated by dependency.

A block is used as a function body, a branch arm or a lambda body, and can also stand alone as a statement.


