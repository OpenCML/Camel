# 7. Statements and declarations

English | [中文](../07_statements.md)

A block is made up of statements. A statement is: an expression, a `let`/`var` declaration, `return`/`throw`, a nested block, or a declaration of a function or a type.

---

## 7.1 Expression statements

Any expression can be a statement. In a sync block it executes in order; outside a sync block, an expression whose value is not used does not execute ([4.2](04_execution_model.md#42-evaluation-by-dependency)).

**A `T!` that is evaluated and not used is a warning.** In a sync block a statement runs whether or not anything takes its value; when that value is a `T!` ([3.4](03_data_model.md#34-may-be-null-t-and-may-fail-t)), an error it holds is lost without a trace, and the compiler says so (`SemanticDiag::UnusedFallibleValue`):

```cml
func save(path: string): int! sync { ... }

func main(): int sync {
    save('a.txt')              // warning: the int! is not used
    let r = save('b.txt')      // warning: r is never used
    let _ = save('c.txt')      // dropped on purpose: no warning
    return save('d.txt') !! -1 // used
}
```

**An unused result of a pure operation is an error.** When a statement is a call of an operator declared pure (`xs->append<5>`, `sort(xs)`, `a + b`) and it is not the last statement of its block, it does nothing: such an operator returns a new value and changes none of its arguments (design principles 2.12). This is the compile-time error `SemanticDiag::UnusedPureResult`; when `xs` is a variable, the diagnostic suggests the rebinding `xs = xs->append<5>` ([3.6](03_data_model.md#36-let-var-and-references)).

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

- An initial value is required. The type can be omitted and is inferred from the initial value; when the type is written, the initial value must be of that type (a literal is interpreted according to the type written: `let a: i32 = 3`).
- The scope is the enclosing block (or the module top level). The same name cannot be declared again in the same scope; inner blocks and lambdas can shadow outer names.
- **Destructuring**: a tuple can be destructured, and several names can be declared at once:

  ```cml
  let (q, r) = (7 / 2, 7 % 2)
  let a, b = 1, 'x'
  ```

  Struct destructuring `let { x, y } = p` and array destructuring `let [a, b] = xs` are reserved, not implemented (`SemanticDiag::FeatureNotSupported`); use `p.x`, `xs[0]`.
- **`let _ = e` drops the value of `e` on purpose.** `_` is not a name: nothing is bound, so it can be written any number of times in one block, and it cannot be read. The expression is evaluated like the initial value of any `let` (in a sync block, in order). It is the way to say that a `T!` is ignored deliberately ([7.1](#71-expression-statements)). Only this form exists: `var _`, and `_` among several names, are syntax errors.
- A `let` at the module top level is a global constant; a top-level `var` can be read and written by functions (a function that writes it is declared `sync func`), also by an imported function of the module that declares it ([5.3](05_modules.md#53-exports)). **The top-level declarations do not depend on the order they are written in, only on what they use**: `let area = pi * r * r` may come before `let pi`; top-level values that depend on each other are `SemanticDiag::ModuleValueCycle` ([5.7](05_modules.md#57-loading-modules)). A local `let` in a function body is still declared before it is used.

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
| `macro` | Can be evaluated ahead of time by the `std::macro` pass ([4.5](04_execution_model.md#45-macro-functions)) |
| `atomic`, `shared`, `inner`, `outer` | Reserved: they are parsed and have no semantics at all |

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
- `typeas` can appear in these positions: array elements, tuple elements, struct fields, the parameters and return value of a function type, the inner type of `T?`/`T!`, the arguments of `Map`/`Set` and of type functions. It cannot appear in a union type, nor as an argument of a module type.
- `typeas` can only be written in the parameters (including the `with` parameters) of a function declaration. A type variable must be bound before it is used (in the order written, `with` parameters first); binding the same variable twice, or giving it the name of an existing type, is an error.
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

**Current restrictions**: a generic function cannot be decorated; a generic function with context arguments bound (`f<w>`) cannot be a value; type arguments cannot be written at a call (whether to allow that is undecided).

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

The parts of type expressions that are reserved and not implemented: the type operations `&`, `\`, `+`, `-`, `^`, and `typeof`, `keyof`, `name of T`, `T ? U` (`SemanticDiag::FeatureNotSupported` or a syntax error); their meaning is not yet decided. A union type `A | B` can be written, but values of the member types cannot actually enter it (an `int` cannot be passed to a parameter of type `int | string`), and it has no practical use at present. `enum` is a reserved word with no corresponding syntax.

## 7.8 Blocks

```
sync? { statement ... }
```

A block is an expression: its value is given by the `return` in the block, and without a `return` it has no value. A block introduces a scope. A block with `sync` executes its statements in the order written ([4.3.1](04_execution_model.md#431-sync-blocks-block-modifier)); a block without `sync` is evaluated by dependency.

A block is used as a function body, a branch arm or a lambda body, and can also stand alone as a statement.
