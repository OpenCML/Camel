# 7. Statements and declarations

English | [中文](../07_statements.md)

A block is made up of statements. A statement is: an expression, a `let`/`var` declaration, `return`/`throw`, a nested block, or a declaration of a function or a type.

---

## 7.1 Expression statements

Any expression can be a statement. In a sync block it executes in order; outside a sync block, an expression whose value is not used does not execute ([4.2](04_execution_model.md#42-evaluation-by-dependency)).

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
- A `let` at the module top level is a global constant. A top-level `var` can be read and written by functions, and a function that writes it should be declared `sync func`; this usage is not fully implemented and is best avoided.

## 7.3 `return` and `throw`

- `return e` gives the value of **the block it is in**. A function body is a block, so a `return` written at the top level of the function body gives the return value of the function; a `return` written in the block of a branch arm gives the value of that branch, and does not leave the function ([6.8.1](06_expressions.md#681-if)).
- A `return` without a value and `return null` are used in blocks that have no value.
- `throw e` gives an error value and has the same structure as `return`: it does not unwind the call stack, and there is no "propagation" or "catching".
  - The enclosing function must declare its return type as `T!`; otherwise it is `SemanticDiag::ThrowOutsideFallible`.
  - `e` must be an error value: one constructed by `error(kind, message)`, or one bound by `case error e`; otherwise it is `SemanticDiag::ThrowNotAnError`.
- Whether `raise` stays or goes is undecided; using it is `SemanticDiag::ConstructNotSupported`.

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

- A generic function is a **subgraph template**. Each call with a set of concrete types instantiates a subgraph in which all types are concrete (the same set of types is instantiated only once; in `std::rgir` one can see subgraphs named `first<i64>` and `first<string>`). The graph contains only concrete types, and no type information is needed at run time.
- **Binding happens only at the positions of `typeas`**: the type variable takes the type the argument has at that position. The other positions are ordinary parameters, checked against the type after substitution, with no implicit conversion: `pick(true, 1, 2.5)` is an error (`T` is already bound to `int`).
- `typeas` can appear in these positions: array elements, tuple elements, struct fields, the parameters and return value of a function type, the inner type of `T?`/`T!`, the arguments of `Map`/`Set` and of type functions. It cannot appear in a union type, nor as an argument of a module type.
- `typeas` can only be written in the parameters (including the `with` parameters) of a function declaration. A type variable must be bound before it is used (in the order written, `with` parameters first); binding the same variable twice, or giving it the name of an existing type, is an error.
- **The function body is checked at instantiation.** Type errors in the function body are reported at instantiation; the diagnostic gives the position of the error in the function body and states which instance it is and which call instantiated it. A generic function that is never called is not checked.
- Recursive generic functions can be used (the return type must be written out). A function that switches to a larger type at every recursion cannot terminate instantiation, and an error is reported when the nesting reaches 8 levels.
- `T` can be used in the function body: the parameter types of a lambda, `let z: T = 0`, `e as T`.
- **Export**: a generic function can be `export`ed, and after an `import` another module calls it as usual (the qualified name `lib::f(x)` can also be written). What is exported is the template; instances are built by the module that calls it, and every module that imports it has its own instances. Names in the function body are resolved in the module that declares it, so it can use functions of that module that are not exported, other generic functions and its own imports. There is one restriction: a function body instantiated by another module cannot use top-level values (`let`/`var`) of the declaring module; write the constants it needs as functions. "In which module an instance is built" is behaviour of the current implementation, not yet decided.

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

**Current restrictions**: a generic function can only be called directly; it cannot be used as a value (where needed, wrap it in a lambda that spells out the parameter types: `xs->map<(x: int) => id(x)>`) and cannot be annotated; type arguments cannot be given explicitly in a call (whether to allow this is not yet decided).

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
