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

  Struct destructuring `let { x, y } = p` and array destructuring `let [a, b] = xs` are reserved, not implemented (`SemanticDiag::FeatureNotSupported`); use `p.x`, `xs[0]`.
- **`let _ = e` drops the value of `e` on purpose.** `_` is not a name: nothing is bound, so it can be written any number of times in one block, and it cannot be read. The expression is evaluated like the initial value of any `let` (in a sync block, in order). It is the way to say that a `T!` is ignored deliberately ([7.1](#71-expression-statements)). Only this form exists: `var _`, and `_` among several names, are syntax errors.
- A `let` at the module top level is a global constant; a top-level `var` can be read and written by functions (a function that writes it is declared `sync func`), also by an imported function of the module that declares it ([5.3](05_modules.md#53-exports)). **The top-level declarations do not depend on the order they are written in, only on what they use**: `let area = pi * r * r` may come before `let pi`; top-level values that depend on each other are `SemanticDiag::ModuleValueCycle` ([5.6](05_modules.md#56-loading-modules)). A local `let` in a function body is still declared before it is used.

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

- Each parameter is written `name: type`. A parameter preceded by `var` is a mutable reference ([4.4](04_execution_model.md#44-var-references-and-wait)).
- The return type can be omitted and is inferred from the function body; a recursive function must write out its return type. When the body has no `return`, the value of the function is the value of the last statement of the body: that value when the statement is an expression with one, `void` otherwise. Note that the value of `println(x)` is `x` (so that `x->println->f` can go on), so `func main() sync { println('a') }` returns the string `'a'`, and the default pipeline writes the result of `main` to the output: the line appears twice. Declare the return type `: void`, or end with `return 0`.
- A `sync` before the function body makes the function body a sync block; a `sync` before `func` is a function modifier. The two have different meanings ([4.3](04_execution_model.md#43-sync)).
- A function can be declared at the module top level, in a namespace, or in a block (a local function). Functions can be mutually recursive, and need not be declared before use.
- Parameter default values `b: int = 5` are reserved, not implemented (`SemanticDiag::FeatureNotSupported`); use overloading or `with` parameters.

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

### 7.4.3 Overloading

Functions in the same block with the same name and different parameter types are overloads, each with its own subgraph. A call takes the first one, in the order written, whose parameters accept the arguments.

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

- A second function of the same name with exactly the same parameter types is `SemanticDiag::Redeclaration` (the return type and the parameter names do not take part in the distinction).
- A generic function cannot share its name with other functions.
- A name that stands for a group of overloads cannot be used as a value ([6.9](06_expressions.md#69-lambdas-and-function-types)).

### 7.4.4 Modifiers

| Modifier | Meaning |
|---|---|
| `sync` | The function has side effects and needs to be synchronised ([4.3.3](04_execution_model.md#433-sync-func-function-modifier)) |

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
- An operator with the wrong number of parameters, with with-parameters or with a `var` parameter is `SemanticDiag::OperatorOverloadInvalid`. A generic overload writes `typeas` in a parameter's type (`func operator +(a: (typeas T)[], b: T[]): T[]`), not in angle brackets: angle brackets are with-parameters.
- **The overloads of every layer form one set.** Every other name takes the nearest layer only (local > this module > imported > built-in); operators are the exception (design principles 10.2): the overloads of this module, those exported by imported modules, and the built-in and C++ module ones are one set, and a call takes the one that accepts its operands.
- **The more specific overload wins** (design principles 4.3). "More specific" means: an overload of the program whose parameter types are exactly the operands' types (without a value entering `T?` or `T!`), while every other one that accepts the operands takes them only as members of a family of types: a built-in or C++ module operator whose type rule accepts several types (comparing arrays, tuples and structs by content, arithmetic over tensors), or a generic overload. The call then takes the program's overload. So a struct can have an equality of its own: `func operator ==(a: V, b: V): bool` makes `p == q` (both `V`) call it instead of comparing by content; other struct types are still compared by content. A built-in operator declared for exactly those types (`int + int`, `float == float`) is as specific: declaring such an overload is `SemanticDiag::OperatorOverloadConflict`.
- Otherwise two that accept the operands are `SemanticDiag::AmbiguousCall`, which lists them (two generic overloads, or two modules each giving one for the same parameter types); two overloads with the same parameter types in one module are `SemanticDiag::Redeclaration`. An ordinary call, and a partial application (`f<w>` used as a value), report the same diagnostic when several candidates fit in the pass being used.
- **Export**: `export func operator +(...)`. Importing a module (`import { V } from m`, or `import m`) brings all the operator overloads it exports, as for the operators of a C++ module; they are not, and cannot be, imported by name.
- **An operator declared in a namespace block belongs to the module** (design principles 4.3): its name carries no namespace (an operator has no name to qualify), it is exported and joins the overloads as one declared at the top level does, and its signature and body look names up in the block, so they may use the block's types without a prefix.
- On the graph an operator overload is an ordinary function call. Names of the form `__x__` remain reserved; programs do not write them.

## 7.5 Generic functions

At the position in the parameter types where a type variable first appears, write `typeas T`, which binds the type at that position as `T`; afterwards (the remaining parameters, the return type, the function body) write `T` directly.

```cml
func first(xs: (typeas T)[]): T {
    return xs[0]
}

func pick(c: bool, a: typeas T, b: T): T {
    return if c then a else b
}

func my_map(xs: (typeas T)[], f: (x: T) => typeas U): U[] {
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
- **A type variable takes its type from the arguments.** First the type of the argument at its `typeas` position; when the argument there says nothing (it is `null`, or a generic function that is itself waiting for its types), the type found at another occurrence. The constraints of all arguments are collected before any is checked: every parameter is then checked against its type with the variables replaced, without implicit conversion (except that a value enters a `T?`/`T!` parameter, see [3.4](03_data_model.md)). `pick(true, 1, 2.5)` is an error (`T` is bound to `int`); a variable that cannot be determined is a compile-time error, and the diagnostic says which.
- `typeas` can appear in these positions: array elements, tuple elements, struct fields, the parameters and return value of a function type, the inner type of `T?`/`T!`, the arguments of `Map`/`Set` and of type functions. It cannot appear as an argument of a module type.
- `typeas` can appear in the parameters (including `with` parameters) or result type of a function declaration. A variable must be declared before use (`with` parameters first); declaring it twice or using an existing type name is an error.
- A result-only variable is inferred from an expected type: `let xs: int[] = empty()`, a known parameter type, or a declared return position. Without sufficient context, provide explicit type arguments: `empty::<int>()`. Types follow their first occurrence in the signature (`with` parameters first) or can be given by name, which does not depend on position: `pair::<A = int, B = string>(..)`. One list is all positional or all named, never mixed; by name every type variable must be given, and a wrong or repeated name is a compile-time error; built-in operators take their type arguments by position only. Type arguments are consumed at compile time. Value context arguments remain `f<w>`; combine them as `f::<T><w>(x)`. An explicit instance can also be a function value: `let f = id::<int>`.
- **The function body is checked at instantiation.** Type errors in the function body are reported at instantiation; the diagnostic gives the position of the error in the function body and states which instance it is and which call instantiated it. A generic function that is never called is not checked.
- Recursive generic functions can be used (the return type must be written out). A function that switches to a larger type at every recursion cannot terminate instantiation, and an error is reported when the nesting reaches 8 levels.
- `T` can be used in the function body: the parameter types of a lambda, `let z: T = 0`, `e as T`.
- **Export**: a generic function can be `export`ed, and after an `import` another module calls it as usual (the qualified name `lib::f(x)` can also be written). What is exported is the template; there is one instance per set of types in the whole program, whichever module calls it. Names in the function body are resolved in the module that declares it, so it can use functions of that module that are not exported, other generic functions, its own imports and the values of its top level.

Binding is equally possible in `with` parameters:

```cml
with <f: (acc: typeas U, x: typeas T) => U, init: U>
func my_fold(xs: T[]): U {
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
func apply(f: (x: typeas T) => typeas U, x: T): U { return f(x) }
func id(x: typeas A): A { return x }
func pair(x: typeas A): (A, A) { return (x, x) }

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
    return static loop acc = 0 for (j) in indices ts do acc + ts.[j]
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
import { Tensor, ones, sum } from tensor

func total(constas n: int, x: Tensor<f32, const [n]>): float {
    return sum(x)
}

func main(): int sync {
    println(total(3, ones([3])))
    println(total(5, ones([5])))
    return 0
}
```

```text
3.0
5.0
```

- **The value enters the instance key.** Every distinct constant instantiates a subgraph (`total<3>`, `total<5>`); the same types and value instantiate once. Types and values together are the key: two calls that differ only in a string are two instances (`label<"w">`, `label<"h">`).
- **Any number, any position.** A declaration may take several `constas` parameters, at any position among the ordinary ones; the call writes each value at its parameter's position (`place(1, 2)` binds `x = 1`, `n = 2`). Each value is its own part of the instance key.
- **The type is not limited to `int`.** `string`, `int`, `float`, `bool` and any other type with compile-time constants work; the value must be compatible with the parameter's type. The type must be written (`SemanticDiag::ConstasTypeRequired`). An `enum` member cannot be passed as a compile-time value yet (`SemanticDiag::ConstasValueNotConstant`) until `enum` values have a compile-time representation.
- **The body reads the value.** Inside an instance `n` is a compile-time constant: it can be a named dimension in a type (`Tensor<f32, const [n]>`, `[n]`), a field name (`t.[n]`), or an ordinary value in arithmetic, conditions and printing. No type has to read it: a body that is the only reader is legal.
- **It is not a runtime parameter.** The call gives the constant, but it does not enter the call node: the instance's parameter list has no `n`, and the call passes only the other arguments (`total(3, xs)` has one argument, `xs`).
- **A value can be handed on.** A `constas` value may be passed to another `constas` parameter, and it may come from a compile-time name such as the field name `static for` binds or a position of `indices`. `func columnOf(constas n: string, ..ts) { return column(n, ..ts) }` is legal: this is how `tree_map` hands the field name and the trees to `column`.
- **It combines with a rest parameter.** `func column(constas n: string, ..ts)` is legal: the value is bound before the expansion and `ts` takes the arguments left over. The `column` of design note 3.1.1 can be written today as

  ```cml
  func column(constas n: string, ..ts): (int, int) {
      return ( static for (j) in indices ts => ts.[j].[n] )
  }
  ```

  Two calls that differ only in the string are two graphs, each reading the field its value names.
- A declaration with a `constas` parameter **has no function value**: the constant must be written at the call, so it cannot be assigned or passed on (`SemanticDiag::ConstasFunctionNeedsCall`). A body-less declaration (the declaration of a C++ operator) cannot take one (`SemanticDiag::ConstasInNativeDeclaration`).
- The value must be a compile-time constant (`SemanticDiag::ConstasValueNotConstant`): a runtime value cannot select an instance. Constants today are literals, another `constas` value, and compile-time names such as the `n` of `static for` and the `j` of `indices`.
- `n` is bound only inside the instance body; a type position in the body (`let z: Tensor<f32, const [n]>`) has no such scope yet. Array dimensions such as `f32[n]` have not landed either (see the restrictions below); what works today is `Tensor<f32, const [n]>`.

**Current restrictions**: a generic function cannot be decorated; a generic function with context arguments bound (`f<w>`) cannot be a value; a function with a `constas` parameter cannot be a value, cannot be combined with a body-less declaration, `f32[n]` array dimensions are not implemented yet, and an `enum` value cannot yet be a `constas` argument. The design note's `column(constas n, ts)` omits the type; today it is written `column(constas n: string, ..ts)`.

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
- **A permission allows, it never requires.** The CPU back ends (`std::nvm`, `std::fvm`, `std::jit`, `std::prl`, `std::cpp`) always give the canonical results; later back ends (Tiles, for example) may use other algorithms on the nodes that have a permission. `std::opt` uses the defaults of 7.3; `std::opt(safe_fp=true)` works under the same policy as `#strict`.
- There is no module-wide default and no command-line or pipeline switch; a back end's entry point (such as `tiles.run`) takes no permissions.
- The permissions are recorded on every node of the graph: `std::rgir` and `std::gir` show `numeric="..."` on the nodes that have any. `#strict` is a tag of the code and appears as `attrs="strict"` on its nodes. Common-subexpression merging and tensor fusion do not merge nodes with different permissions.
- **The old `@numeric(permission, ...)` spelling is gone**: it is now `SemanticDiag::InvalidAnnotation` at the annotation, naming the tag form that replaced it (`#[reassociate]`); an empty `@numeric()` and any `@name(args)` are rejected the same way.

### 7.6.2 Tags: `#`

`#` no longer starts a comment (the `#!` of the first line aside): it writes a **tag** (design principles 6.2). A tag is a name in the same namespace as functions and types: declared in this module, imported by name, or qualified as `module::tag`.

A tag is declared at the top level of a module; after `on` come the positions it may be written at:

```cml
export tag kernel on nodes                  // before a block, function or type: the nodes in it
export tag fast on func, nodes              // also the function's own declaration
export tag width<w: int> on type, field     // with parameters, which are compile-time values
tag finite = [kernel, fast]                 // a group: its members apply together
```

The positions are `func` (a function declaration), `type` (a type declaration), `field` (a struct field), `nodes` (the nodes written in the region) and `param` (reserved). `read_by` says which passes read the tag; when none of them is in the pipeline a warning is reported where the tag is written. Only a pass that declares itself a reader of tag facts counts: the core dump passes and `std::opt`, and module passes such as `tiles::compile` and `tensor::fuse`; a pass written in Camel can declare the tags it reads with `#reads<tag, ...>` on its function, which counts like `read_by`. A program may not declare a tag with the name of a core permission (nor `finite_math` or `strict`; `tag reassociate`, for one): that name always means the core's tag, so the declaration would never be read, and `SemanticDiag::TagShadowsPermission` refuses it at the declaration.

The core carries a set of **numeric permission tags** (design principles 7.2), written without an import and `on nodes`: `reassociate`, `contract`, `approx_math`, `change_precision`, `no_nans`, `no_infs`, `no_signed_zeros`, `flush_subnormals`, `nondeterministic` (allows results that vary with the schedule, such as floating-point atomics), and the group `finite_math` that holds the first four of those assumptions; `strict` (7.4) is a core tag too, written `on func, nodes`. `#contract` lands in the node's permission column (the `numeric="..."` of a dump) and `#strict` in its attribute set (`attrs="strict"`); readers get permissions through one query (`permissionsOf` / `effectivePermissions` in `camel/core/tags.h`), and `effectivePermissions` returns the empty set for `#strict`. The defaults are those of 7.3: `reassociate` and `contract` are allowed anyway, the others still have to be written.

A rule declaration may be preceded by `#requires<permission, ...>`: the arguments are permission names, the core's group `finite_math`, or a group the module declares with `tag name = [permission, ...]` (members are expanded recursively, and the group name means the union of its permissions). The engine applies the rule only where the numeric policy of the nodes allows those permissions ([46 Rules](../../technical/46_rules.md)). A requirement that names an ordinary tag, an undeclared name, or `#requires` outside a rule is a located error, never a silent pass.

There are three forms of `#`, written before a function, a type declaration, a struct field or a block:

<!-- camel-snippet: fragment: the three tag forms are illustrative; width is a tag a module declares, and the bodies are elided -->

```cml
#kernel
func matmul(a: T, b: T): T { ... }

#width<32>
type Packed = {
    #width<8> a: int,
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

`#name`, `#name<arguments>` and `#[a, b<v>]` (a group). A tag's arguments are compile-time values: a literal, a top-level constant or a function name. Each argument must be a constant of the kind its parameter declares — `int`, `float`, `bool`, `string`, or a function name for `func`; a real is carried as a real attribute by its bits and `1.5` prints as `1.5`. Writing a tag where its declaration does not list it is an error, located at the tag. So are a wrong number of arguments, an argument that is not a compile-time value, an argument whose kind is not the declared one (`SemanticDiag::TagArgumentType`), and a parameter of a kind a tag cannot carry yet (`SemanticDiag::TagParameterType`).

A tag of `on nodes` is a **node attribute** (`camel/core/node_attrs.h`): a tag written before a function body or a block holds for the nodes written in it, it accumulates with same-name tags outside by the attribute's combine rule, and an inner same-name tag with a parameter takes the inner value. A tag of `on func`, `on type` and `on field` belongs to the declaration itself: a C++ module reads it in one query, `tag::factsOf` (tags, permissions and readers; `tag::ofDeclaration` / `tag::ofField` remain); reading it from Camel code waits for the first stage of generics.

A parameter may also be declared `func`: its argument is then a function name, and the tag's fact is the **graph reference** of that function (`attr::graphRef`), not a function value. The GIR builder keeps that graph in the reference table of the function's graph (`kGraphRefRule`), so the frozen graph names the function directly; it is not resolved by name again at run time and no closure captures it. A `func` argument is therefore a static function of the program, never a closure over the surrounding scope. The autodiff module declares `export tag vjp<rule: func> on func` this way: `#vjp<rule>` before a function declaration makes calls of that function use the rule instead of the derivative of its body ([26 Autodiff](../../technical/26_autodiff_as_graph_rewrite.md)). The old `@vjp<rule>` annotation is gone: it is reported at the annotation as `SemanticDiag::InvalidAnnotation`, naming `#vjp<rule>`.

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

func swap(p: Pair<typeas T>): Pair<T> {       // combined with typeas
    return (p.1, p.0)
}

func key_of(e: Entry<typeas K, typeas V>): K {
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
- The angle brackets of a module type (such as `Tensor<f32, const [2, 3]>`) are interpreted by the type itself, and the arguments can be types or `const data`.

The parts of type expressions that are reserved and not implemented: the type operations `&`, `\`, `+`, `-`, `^`, and `typeof`, `keyof`, `name of T` (`SemanticDiag::FeatureNotSupported` or a syntax error); their meaning is not yet decided.

## 7.8 Blocks

```
sync? { statement ... }
```

A block is an expression: its value is given by the `return` in the block, and without a `return` it has no value. A block introduces a scope. A block with `sync` executes its statements in the order written ([4.3.1](04_execution_model.md#431-sync-blocks-block-modifier)); a block without `sync` is evaluated by dependency.

A block is used as a function body, a branch arm or a lambda body, and can also stand alone as a statement.
