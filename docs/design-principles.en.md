# Camel Design Principles and Semantic Rulings

> **This is a translation. The authoritative text is [`design-principles.cn.md`](design-principles.cn.md).**
> Where the two differ, the Chinese file is right and this file is to be corrected.

This document records the language author's explicit statements about what Camel fundamentally is and about several semantic questions. It is normative: when the implementation, another document or a test result conflicts with this document, this document prevails, and the conflict is to be recorded as a defect or a to-do, not used as a reason to change the semantics.

**Before changing Camel's semantics, passes, schedulers, exporter or automatic differentiation, read this document first.** Questions that it does not cover or still marks as "undecided" go to the language author; do not choose on your own.

---

## 1. What Camel is: graph-native

Camel's most central characteristic is that it is graph-native. Its other properties are natural consequences of this position, not features added on top.

- **It models computational intent only.** A Camel program describes "what is to be computed and what depends on what". It does not itself carry CPU-intensive work, nor configuration of all kinds; operators, the runtime and passes handle those. That is exactly why it is free and flexible.
- **The graph is native, not a product.** The graph is not captured or recovered from code in another language; what the source says is the graph.
- **Language elements correspond directly to the graph.** To understand or design any language construct, first answer what it is on the graph.
- **Not limited to AI.** What is modelled on the graph is the intent and dependencies of a computation; HPC and simulation workloads of all kinds fit equally.

The eight lines of `ZEN` outline this position: the first two say the graph is fundamental, the other six are corollaries.

### Language elements and the graph

| Language element | What it is on the graph | Resulting behaviour |
|---|---|---|
| Expression, operator call | Nodes and data edges | Evaluated by dependency; a node nothing depends on does not execute; nodes that do not depend on each other may run out of order or in parallel |
| `sync` block | Control edges between adjacent expressions | Executes in written order inside the block |
| `wait` expression | Control edges between that expression and the related operations before and after it | Locally ordered |
| Function | Subgraph | Its behaviour is decided by the function itself; a call only places the subgraph node in a larger graph and does not change its structure |
| `var x = v`, reading, assigning | Three kinds of operator node: make a cell, read it, write it; the name `x` is bound to the cell | What is mutable is the cell, not the value |
| Lambda, function as a value | A function constant; with captures, one more node that puts the captured values in | A function value is "graph + captured values" |
| `f<w>` as a value (partial application) | A function value that captured `w`; its graph hands the remaining arguments and `w` to `f` | The same structure as a lambda |
| `macro` function | A subgraph that can be evaluated early, its result materialized as a constant of the graph | Builds a static graph dynamically |
| `if`, `match` | A branch, one subgraph per arm | Only the chosen arm is evaluated |
| `a ?? b`, `a !! b`, `x ?-> f` | A branch: the condition is a one-bit tag on the value (null / error), and the two arms are the original value and the substitute | The same structure as `if` |
| `a && b`, `a \|\| b` | A branch: `a && b` is `if a then b else false`, `a \|\| b` is `if a then true else b` | The right side is an arm, evaluated only when needed; cannot be overloaded |
| `a & b`, `a \| b` | An ordinary operator node | Both sides are evaluated; can be overloaded |
| `if x != null then A else B` | The same graph as a `match` on `x`: the non-null arm is entered with the opened value | Narrowing is not a separate mechanism |
| A value entering `T?`, `T!` | One entering node per level | Entering happens only where it is needed |
| `throw e` | Returning an error value | The same structure as `return` |
| Fault isolation (`attempt`) | One tagged call of a subgraph | The boundary of the fault is the boundary of the subgraph; the structure does not vary with the content of the subgraph |
| `map`, `filter`, `reduce`, `foreach` | One operator node; the element function is passed in as a function value | Higher-order operators are not a new kind of node |
| Generic function | A subgraph template; each call with a set of concrete types instantiates a subgraph whose types are all concrete | Only concrete types are on the graph |
| Type function | Evaluated while the graph is built; leaves no node on the graph | A value of kind "type" does not exist at run time |
| `loop` expression | One `LOOP` node with a condition subgraph, a body subgraph and state edges | The state is decided by the loop header, not by what the body contains |
| A `let` at the top level of a module | A node of the common root graph | Evaluated when depended on; importing executes no code |
| A function using a top-level value of a module | A capture port of the function graph, bound to "module + name"; the value is passed down along the calls | No node reference across graphs |
| `import { f } from m`, `import { v } from m` | No node is built: calling `f` is an ordinary call; `v` is an ordinary edge on the root and a capture port inside a function | Importing executes no code |
| Importing a generic function | No node is built; one instance per set of types, shared by the whole program, belonging to the module that declares it | Only concrete types are on the graph |
| `namespace`, `using namespace`, `m::f` | No trace on the graph; they only decide which "module + qualified name" a name resolves to | A namespace is part of the name |
| Pass | A program that turns one graph into another (or consumes a graph) | An open pipeline |

---

## 2. Execution model

### 2.1 Evaluation by dependency

By default evaluation is driven by dependencies; there is no fixed execution order.

**A call with side effects that is outside a sync block and whose result is not used does not execute.** This is the natural result of evaluation by dependency. It is legal, expected behaviour, not an error, and needs no error report. A sufficiently smart compiler may give a warning.

### 2.2 `sync` modifying a block

A `sync` written before a block (including the suffix form of a function body, `func f(): T sync { ... }`, and a standalone `sync { ... }`) says the block is a sync block: all expressions in it execute in written order, which can be understood as the compiler automatically inserting control edges between each consecutive pair.

Inside a sync block it does not matter what is called, nor whether it has side effects, because there is already an order. Side effects only need to be considered under the default unordered execution, because they limit what may be reordered.

### 2.3 `sync func` modifying a function

The prefix `sync func f(...)` modifies the function: it says the function has side effects and needs to be synchronized, that is, callers may need to `wait` it.

**Its meaning differs from the suffix `sync`. This is by design; do not equate the two.** Declaring `sync func` does not mean the function body is a sync block. To have the body execute in order, write `sync func f(): int sync { ... }`.

### 2.4 A function's behaviour is decided by the function itself

A function compiles to its own subgraph. A call from outside only arranges this subgraph node inside a larger graph; it does not change the subgraph's own structure.

```camel
func log(x: int): int {
    println(x)          // result unused, and the body is not a sync block: not executed
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    return b            // returns 3, prints nothing
}
```

The body of `main` is a sync block, so the two calls of `log` execute in order. But `println` belongs to `log`'s own body, which is not a sync block, so it does not execute. To print, `log` should be declared `func log(x: int): int sync { ... }`.

### 2.5 References, `var` and `let`

- Everything in Camel is a reference by default. A closure's capture of a variable is also by reference.
- `var` marks a mutable object, `let` an immutable one.
- `let` is immutable in the strict sense: an operation that modifies a variable is itself `sync`, and must explicitly declare which parameter is `var`; passing a `let` variable to a `var` parameter is not allowed, so in principle modification is impossible. This is a **compile-time check**, not a lock on memory.
- Passing a `let` object to a `var` parameter is a compile-time error.

The reason for this design lies in `wait` (below): for a function whose input parameter is mutable, the compiler automatically tracks its outward side effects and automatically inserts control edges.

### 2.6 `wait`

The scope of `wait` is the **one expression** that follows it.

Meaning: the operations before and after the `wait`, if they use the value of that expression or use a value modified by that expression (which is equivalent to using the side effect it produces), are naturally ordered locally.

```camel
A(i)
wait modify(i)
B(i)
```

The compiler automatically inserts the control dependencies A → modify → B.

**The declaration of side effects is a closed loop.** If `modify` wants to change the value of `i` internally, it must use a function that itself has a side effect: assignment in Camel is a function with a side effect; or it uses another built-in C++ operator, and the declarations of those operators also mark the side effect explicitly and require the corresponding input parameter to be mutable. So for `modify` to change its parameter, that parameter must be declared `var` (`modify(var i: ...)`); otherwise it has no effective way to change it.

It follows that:
- Mutability is passed upward from the declarations of the lowest-level operators, and every layer must declare it explicitly; the compiler only has to examine the functions that explicitly declare side effects to track every modification.
- A function that does not declare a parameter `var` cannot modify it, so the compiler does not, and need not, insert a control dependency for it.
- Assigning to a non-`var` parameter inside a function body, or passing it to an operator that requires a mutable input, is a compile-time error.
- When the behaviour of `wait` is tested, the `modify` in the test program must declare `var i` and really modify it.

**`wait` is not mechanical ordering; it orders by how the `var` is used.** What is placed before the modification are the nodes that have directly used the value of the variable so far. For example, in `A(i + 1); wait modify(i)` the direct user of `i` is `+`, so `modify` is placed after `+`, with no guarantee that it comes after `A`; this is correct behaviour. Likewise, if the waited expression involves no `var` at all (for example it has only input/output side effects), `wait` has nothing to go by and is meaningless; such calls belong in a sync block, which is exactly why sync blocks exist. For that reason leaving out `wait` is not made an error either.

The tracking of modifications should be complete: a function that writes a `var` through a capture (including a variable at the top level of a module) is the same as one that writes through a `var` parameter, and the call site should be recorded as one write of that variable.

Inside a sync block it does not matter whether `wait` is written.

### 2.7 Supplementary rulings (2026-10-06)

- **Compile-time checking of tensor shapes is necessary.** It is not implemented at present. The way to implement it: Camel lets a type (especially a type brought in by a third-party C++ module) define its own type resolver, and the shape check can be done with C++ logic inside the resolver; the language core does not need to change.
- **Out-of-bounds access**: when the length is known at compile time (especially for types of fixed length), an out-of-bounds `slice` or subscript should be a compile-time error, not silently truncated or left to run time.
- **Nothing reads after a `wait`**: if nothing reads `i` again after `wait modify(i)`, the modification does not execute. This is the result of evaluation by dependency and is acceptable.
- **The order of `map` and other higher-order operators** (corrected on 2026-10-06 after checking the code; the earlier version was wrong): a sync block does not affect the order of execution inside `map`. Between the elements of `map` and `filter` there is no agreed order of execution; the parallel scheduler may execute them concurrently, and the results are arranged by element position. This is exactly where parallel speed-up comes from (see `test/cases/para/core/monte_carlo_pi.cml`). `foreach` and `reduce` execute in element order; their unordered parallel versions are `unordered_foreach` and `unordered_reduce` of the `para` module. There is no `ordered_map` or `unordered_map` operator in the repository. The order of side effects in an element function under a parallel `map` is not guaranteed and is the user's responsibility.
- **A tensor of unknown shape entering a position that declares a shape**: it is not let through implicitly; convert explicitly with the `as` keyword (for example `t as Tensor<const [3]>`), and the check is made at the conversion.
- **Numeric types are not converted implicitly.** An integer does not automatically become a floating-point number (the same for parameters and return values); write `float(x)` explicitly where needed.
- **Camel favours immutable variables.** `var` and the assignment operators are only an opening left for convenience and should be used as little as possible. The computation of a model should be written with `let` as pure data flow.
- **Differentiation and variables**: in a function being differentiated, when the gradient would have to pass through a read or write of a `var`, refuse directly with a clear diagnostic; do not support it. There is no need to write `var` just for differentiation.
- **A top-level `var` of a module read and written directly by a function** (such as the top-level `cnt` in `fib1.cml`): not fully implemented; leave it unless it is necessary.
- **Failure of macro evaluation**: report a compile-time error directly (provisional before, now decided).
- **Entry and export**: all passes share `main` as the entry. Export has two cases, depending on what the program expresses:
  - **The program expresses pure computational intent only** (this is what a Camel program naturally is): the program itself should not care how it is exported internally or what is exported. In this case a combination of passes is enough to export it, for example `camel x.cml std::macro std::opt onnx::export`, and what is exported is the computation that `main` expresses.
  - **The program also attends to intent beyond the computation** (for example timing, or running a benchmark): exporting by a combination of passes is then not appropriate. It is better to go all the way and have the program itself call an operator (such as `onnx.export_model`) to export one of its own subgraphs.
  Passing arguments to passes (pass args) is a general mechanism that will be implemented sooner or later; it is not a feature opened for export alone.
- **`main` may take parameters** (decided 2026-10-06). `main` is a subgraph like any other function, and its parameters are the inputs of that graph: at export they are the inputs of the model (shapes taken from tensor types that carry a shape), at run time their values are given from outside (the command line; when one is missing, a clear error with a location is given, and zeros or random numbers are not substituted), and when gradients are taken they are the quantities that can be differentiated with respect to. When `main` takes no parameters, what is exported is a constant model without inputs, which is allowed. Returning a single value is one output; returning a tuple or a struct expands into several outputs.
- **Camel distinguishes two kinds of parameters at the language level.** One kind is the attributes a function is configured with (`with` parameters, that is, context parameters); the other is the inputs the function processes, that is, what flows through the function (ordinary parameters). In a sense this makes up for the limitation that Camel does not support object orientation: the configuration and state that other languages hold in the members of an object are, in Camel, the context parameters of a function.
- **The return value of a pure `main`**: it is output through the existing command-line option `-o` / `--output <file>`, standard output by default. No dedicated option is added.
- **Granularity of parallel scheduling**: do not go down to the atomic level. Creating a task for every very small function call is pointless and only adds burden.
- (Superseded by 2.12: `any` is being tightened.) **A value whose static type is `any` printing as `<any>` is normal**: Camel has no ability to determine a type at run time, and to prevent serious errors this is the safest course.
- **`with` parameters are suited to expressing the parameters and weights of a model**: ordinary parameters are the data inputs, `with` parameters are the weights. Besides `f<w>(x)`, a call can also be written `x->f<w>`, which is convenient for chaining and for connecting the inputs and outputs of the layers of a model.
- **When a pass of some module (such as `onnx::export`) is written on the command line, the compiler loads that module automatically**; a pure program does not have to `import` a module it does not use in order to be exported.
- **An export pass does not have to check much**: translate what can be translated, and report an error when something cannot. No academic fastidiousness.
- **Export passes**: they may do the simplification needed for adaptation, but that should not be the main force; the main evaluation and simplification should be completed before them by `std::macro` and the optimization passes.

### 2.8 Known shortcomings

Camel still has defects in the management and tracking of side effects, and they need further study. Problems that appear when capture by reference and the tracking of `wait` interact with the various passes should be recorded truthfully as findings to be studied, with a minimal reproduction; fix only clear implementation errors, and do not cover problems with ad hoc rules.

### 2.9 Supplementary rulings (2026-10-11)

This group of rulings addresses the direction of "Camel as a general-purpose programming language". From the viewpoint of the paper, Camel does not itself carry CPU-intensive work; from the viewpoint of a general-purpose language, it must still have a certain generality. The two viewpoints coexist.

**The criterion.** A syntactic element must have a stable mapping to the graph; that is what being graph-native means. Every item below is measured against this criterion; if what a construct looks like on the graph depends on what is written inside it, the construct should not enter the language.

Code marked "sketch" only illustrates the intent; the exact form is subject to confirmation again at implementation time.

#### Null, errors and faults

- **The two operators are separate** (as in section 6.6 of the language reference): `a ?? b` handles null, `a !! b` handles errors. `x ?-> f` passes the error on unchanged when `x` is an error, and calls `f` otherwise. On the graph all of them are branches.
- **"May be null" and "may fail" are two dedicated built-in forms**, written `T?` and `T!`. Each carries a one-bit tag and can be taken apart only with `??`, `!!`, `?->` and the fixed patterns of `match`. There is no run-time discrimination of general union types; "Camel has no ability to determine a type at run time" in 2.7 is unchanged. `T?` is at present silently treated as `T`, which is a defect.
- **The error value is one fixed built-in structure**: a kind name, a message, and the source location where it arose. All functions that may fail share this one error.
- **`throw` is kept, and it means returning an error value**, the same structure as `return`. `try`/`catch` does not enter the language: it has no stable mapping on the graph. Writing it should produce a diagnostic that points to `!!`, `?->` and fault isolation.
- **Errors and faults are two different things.** Only operations declared as able to fail (reading a file, parsing, the network and so on) return an error value. Division by zero, out-of-bounds access and recursion that is too deep are faults; they do not become error values, otherwise every division and every subscript would have to handle an error.
- **A fault terminates with a location by default, but can be isolated at a subgraph boundary.** An isolation operator is provided (name provisional, `attempt`): a fault that occurs in the function it wraps becomes an error value at the boundary, to be handled with `!!`. Clean-up is guaranteed before termination: output is flushed and external resources are released.

  ```camel
  let a = risky(0)                 // fault: the whole program terminates with a location
  let b = attempt(risky, 0) !! -1  // the fault is isolated, b = -1 (sketch)
  ```

- **A constant expression that is certain to fault** (such as `1 / 0`): the folding pass does not fold it, gives a warning with a location, and leaves it to run time. The program behaves the same whether or not optimization is on.
- **Recursion depth**: tail recursion is to be optimized, and programs should as far as possible be written as tail recursion or as loops; for non-tail recursion a configurable limit is enough.

#### Data and control

- **Loops**: both the `for`/`while` syntax and a loop node on the graph may be added. A loop node means changes to all passes, every execution back end, automatic differentiation and the exporters; it is a change at the lowest level, so a design draft comes first, and code is touched only after the language author has looked at it.
- **Maps and sets are made built-in types.** No new literal is added; the existing array literal is used with an `as` conversion (the same form as `t as Tensor<...>`). Every operation returns a new value; a lookup that finds nothing gives null.

  ```camel
  let m = [('a', 1), ('b', 2)] as Map<string, int>   // sketch
  let v = m['a'] ?? 0
  ```

- **Fields and elements cannot be assigned** (decided earlier, restated here): only a whole variable can be assigned, so operations such as `append` return a new value and do not modify in place.
- **Strings**: stored as bytes underneath, with length and subscripts by byte; operators that read and write by code point are provided in addition.

#### Types, generics and type functions

At present all complicated type computation is implemented directly by operators at the C++ level, and it is hard for users to write general generic operators. This item has very high priority.

- **Generic functions are instantiated by the types of their arguments.** A generic function is a subgraph template; each call with a set of concrete types instantiates a subgraph whose types are all concrete, and the function body is checked at that time. Only concrete types are on the graph, and no type information is needed at run time.
- **A type variable is bound in place with `typeas T`**: write `typeas T` at the position where it first appears in the signature, which binds the type at that position as `T`, and use `T` directly afterwards.

  ```camel
  func first(xs: (typeas T)[]): T { return xs[0] }   // sketch
  ```

- **Type functions are written in dedicated type expressions**, and called with angle brackets in type position. A type is always written after `:`, so the angle brackets here are not ambiguous with the angle brackets of `with` parameters. A type declaration with parameters is a type function.

  ```camel
  type Pair<T> = (T, T)                    // sketch
  func swap(p: Pair<typeas T>): Pair<T>
  ```

- **`with` parameters are not generic parameters.** They are context parameters (see 2.7); do not use them to declare type parameters or compile-time parameters. `macro` likewise has nothing to do with type computation; do not build type functions on top of `macro`.
- **Computation with numbers inside types** (such as the shape of a tensor): users should be able to write it too, alongside the C++ type resolver. "The shape check can be done with C++ logic inside the resolver" in 2.7 still holds; it says that C++ may be used, not that only C++ may be used. When a dimension is unknown the result is unknown too, and `as` is used to check where needed. How such numeric parameters are declared has not been decided (see section 6).

#### Standard library, modules and runtime

- **The boundary of the standard library**: primitives that must touch the system or are performance-sensitive are C++ operators; compositional logic built on the primitives is written in Camel. A library written in Camel is a subgraph that passes can see into; a C++ operator is an opaque node on the graph.
- **The purity of an operator must be declared.** An operator without a declaration is treated as having side effects (at worst it is optimized less, never computed wrongly); the standard-library modules in the repository must declare every operator, and a third-party module that leaves one out gets a hint.
- **Module interface**: a layer of public C++ interface is to be sorted out of the internal headers, with compatibility promised by version; no promise is made for the remaining internal headers.
- **Garbage collection**: Camel's native small objects can be managed generationally (and may be moved); where an external interface is involved, a native Camel object manages the external pointer, and external memory does not enter the movable heap. A module must not keep a raw pointer to a Camel object across calls. The order of realization: first audit the modules and establish the handle rule, then enable generational collection on the interpreting back ends and validate it under stress, and turn it on for JIT compilation and the like after stack maps are done.
- **The capacity limit of a single graph (65,535) is not relaxed.** It concerns the efficiency of the underlying data structures and the executors. A function is a subgraph; when one graph cannot hold something, some pass has flattened subgraphs too far, and that pass should be made to work within the capacity rather than the addressing widened.

#### Tools and outward statement

- **The debugger** offers two views, source lines and graph nodes, and stepping likewise by line and by node.
- **The outward statement of what Camel is**: Camel is a graph-native general-purpose programming language, and AI is its primary application domain; it no longer calls itself a domain-specific language for AI.

### 2.10 Supplementary rulings (2026-10-12)

**An assumption.** There must be no node reference across graphs: a node of one graph does not point at a node of another, and the only relation between graphs is "a graph refers to a graph" (a call). Every design has to pass this first.

**A criterion for choices.** Camel is meant to differ from the traditional control-flow languages. No syntactic sugar imitates traditional control flow; the mapping between syntax elements and graph nodes stays simple. (This does not exclude concise notations whose meaning is unambiguous, such as the list comprehension.)

#### Optimization and behaviour

- **"The program behaves the same with and without optimization" is not a design principle of Camel**, and the language core cannot enforce such agreement. What a pass keeps and what it removes is the pass's own decision. Example: a pure computation in a sync block whose result is not used but which would fault (`let q = 1 / z`) faults without passes, while a simplification pass may delete it and the program goes on. That is not a defect.

#### Syntax

- **Angle brackets are tight.** The angle brackets of `with` arguments and of type arguments must touch the token before them: `f<a>` is `f` with an argument, `f < a` is a comparison. The comparison operator `<` needs white space before it; `i<n` is an error, and the diagnostic should say why.
- **A line end ends a statement**, in the spirit of JavaScript's automatic semicolons, with intuition as the measure. In detail: a line continues while a bracket is open, when it ends in a binary operator, a comma or another token after which something must follow, and when the next line starts with an operator that can only be binary (`->`, `.`, `??`, ...); a next line that starts with a token that can begin an expression (`-`, `+`, `(`, `[`, ...) is a new statement. Semicolons may still be written.
- **The parser** is a hand-written lexer and parser, no longer depending on ANTLR, for efficiency and determinism.
- **`raise` is removed.** It is no longer part of the grammar and not a reserved word; an error value is returned with `throw`.
- (Superseded by 2.11.) **A minus sign that touches a number literal and is not preceded by a value is part of that number**: `-2->f` is `(-2)->f`. `a-1` is still a subtraction.
- **`^` binds one level tighter than multiplication and division, and associates from right to left.** `2 * 3 ^ 2` is 18.
- **A literal with an exponent or a decimal point is always a floating-point number.** `1e5` is `100000.0`.
- **`===` is not supported for now.**
- **The list comprehension** `[e for x in xs if c]` means `filter` then `map`, that is `xs->filter<c>->map<e>`; nothing is promised about the order of the elements.
- **An import can rename**: `import { find as str_find } from str`. Two imported names that collide are a compile-time error; one must not be taken silently.
- **Documentation comments** are a convention of the language: the comment lines starting with `///` directly above a declaration are its documentation.

#### Loops

- **A `LOOP` node is introduced.** Most modern computation-graph standards have such a node, and connecting to them needs it. It is to be done gracefully: first the syntax, the node and a lowering "from `LOOP` to a tail-recursive subgraph", with every subsystem lowering at its entry; then the subsystems are changed to native handling one by one. The loop body is a subgraph of its own and is not merged into the enclosing graph.
- **A loop is an expression with state.** The state is written in the loop header, the value of the body is the state of the next round, and the value of the whole loop is the final state. No statement form is added for "a `var` outside the loop as the state"; iterating for effects only uses `foreach` and the other existing means. The notation is provisionally (superseded by 2.13):

  ```camel
  let (i, acc) = loop (i, acc) = (1, 0) while i <= n { (i + 1, acc + i) }
  let total    = loop acc = 0 for x in xs { acc + x * x }
  ```

- **No `break` and no `continue`.** Ending early is written as a continuation condition in the loop header: `loop found = -1 for x in xs while found < 0 { ... }`.

#### Branches and types

- **An `if` without `else` has no value of its own.** Its arm may be of any type and the value of the arm is dropped: `if debug then println('checking')` is legal, `let x = if debug then 1` is a compile-time error. Nothing changes on the graph: a branch with two arms, the `else` not written being an empty arm, and a join that takes no value. An arm no longer needs `return null` to make the types fit.
- **`==` and `!=` compare arrays, tuples and structs by content, element by element.** Both sides must have the same type and the element types must themselves be comparable (not those holding function values), otherwise it is a compile-time error; floating-point elements follow the floating-point rule (an array containing `nan` is not equal to itself). Tensors are not included. On the graph it is an ordinary operator node. Strings have `contains` too, written as for arrays: `'hello'->contains<'ell'>`.
- **The right side of `a ?? b` may be nullable**: when it is a `T?` the result is a `T?`, when it is a `T` the result is a `T`; it associates from right to left, so `a ?? b ?? 9` is "try a, then b, then give the default". Likewise `!!`. On the graph it is still one branch.
- **`t += u` on a tensor computes a new tensor and rebinds**, as for a number; nothing is changed in place. Changing in place takes an explicit operator.
- **A `match` must have a default case.** Without `case _` it is a compile-time error, except over a `bool` with both literals listed and with the fixed patterns of `T?` and `T!`. The compiler adds no arm the user cannot see.
- **A value that enters a `T?` or `T!` position is put into the form by itself**, wherever the target type is known (an argument, a `let` with a type, an array element, the two arms of a branch); `null` likewise. Never the other way: a `T?` or `T!` used as a `T` must be opened with `??`, `!!`, `?->` or `match`. The element type of the empty array literal `[]` is taken from the target position in the same way.
- **A generic function may stand wherever a function is expected**, also as an argument of another generic function. The compiler delays its instantiation until the type variables can be determined from the context; when they cannot, it is a compile-time error that says which variable stays undetermined. The delay exists at compile time only; the graph holds concrete instances only.
- **`f<w>` is a partial application**: `let g = f<w>` gives a function that already has its context arguments.
- **Diagnostics and graphs use the type names of source text** (`first<int>`, not `first<i64>`).
- **`throw` takes an error value only**: `throw error('Kind', 'message')`, with the position filled in by the compiler. `int?!` is allowed; `int??` and `int!!` are not. The fault-isolation operator is named `attempt`.

#### Numbers

- `int('12abc')` is a fault: the whole string must be one number. To parse safely use `parse_int`, which returns an `int!`.
- A conversion whose value does not fit the target type is a fault: `int(1e30)`, `int(nan)`, `i32(5000000000)`.
- Overflow of integer addition, subtraction and multiplication, and the least integer divided by -1, keep wrapping around.
- There is one rule for writing a floating-point number as text: the shortest text that reads back as the same number. `string`, `println` and `format` agree.
- `&=` and `|=` are the boolean and / or; bit operations are functions (`bit_and`, ...).

#### Effects, errors and parallelism

- **An effectful function returns a value too.** `println(x)` returns `x`, so that an effect can be threaded into the data flow: when its result is used, it is certainly executed, and before the use.
- **Only a `main` with an explicitly declared return type has its return value count as the result of the program.**
- **An error value that reaches the exit of `main` unhandled** is printed on standard error, and the exit code is non-zero.
- **A `slice` out of bounds at run time is a fault**, as an index out of bounds is. `take` and `drop` truncate by definition and are not affected.
- **When one element of a parallel `map` faults**, the elements that have started run to their end and those that have not are not started. When several elements fault at once, the one with the smallest index is reported.

#### Modules and standard library

- **Code at the top level of a module is not executed by default**: Camel is not an imperative scripting language. Top-level statements are evaluated by dependency like any others.
- **A module can export values.** A module is itself a `__root__` graph; when imported, the roots of the modules are merged under one common root, and a top-level `let` of a module is a node of that root, evaluated when depended on. So there is no node reference across graphs. How exactly the roots are merged is to be set out in a design note first.
- **The calling style of the standard library**: the value being processed is the ordinary argument, everything else is a `with` argument (`'a,b'->split<','>`, `xs->take<3>`); when two arguments are on an equal footing both are ordinary arguments (`merge(m1, m2)`). The operations on maps and sets keep one form accordingly.
- A map is traversed and printed in key order; the types that can be keys are integers, booleans, strings and tuples of them; `'abc'[1]` is the string of that one byte.

#### Defaults

- The default limit for non-tail recursion is 1000 on every scheduler, and the parallel scheduler counts likewise.
- The instruction-set baseline of release packages is `x86-64-v2`.
- The graph, type and metadata spaces each have a total limit of 256 MB; an expansion that does not stop is halted by the exporter's own budget of rounds.

### 2.11 Supplementary rulings (2026-10-13)

#### Operators

- **`&&` and `||` are reserved operators that cannot be overloaded, and they are translated into a branch.** `a && b` is `if a then b else false`, `a || b` is `if a then true else b`: the right side is an arm and is evaluated only when needed. Both sides are `bool`.
- **`&` and `|` are ordinary logical operations that evaluate both sides, and they can be overloaded.** They are for a simple logical operation that needs no branch; the element-wise logical operations of tensors and the overloads of user types are this pair too. `a &= b` is `a = a & b`.
- **The unary minus is always an operator and is never part of a literal**; it binds less tightly than `^` and more tightly than `->`. So `-2 ^ 2` and `-x ^ 2` are both -4, and `-2->f` and `-x->f` both negate first and then pass the value on: a literal and a variable read alike. `a-1` is still a subtraction. (This supersedes "a minus sign that touches a number literal is part of that number" in 2.10.)
- **A symbol with two roles that stands at the start of a line takes the role that begins something new**: `@` at the start of a line is an annotation, `+` and `-` at the start of a line are unary. To write a matrix product or a sum over two lines, leave the operator at the end of the first line.

#### Null and errors

- **`x == null` and `x != null` may be written**; the result is a `bool`. Function values cannot be compared.
- **In the non-null arm of `if x != null then ... else ...`, `x` is a `T`.** This is equivalent to a `match` on the nullable value: the port that enters that arm carries the opened value. It holds for `let` bindings and parameters only (a `var` may be rewritten elsewhere); only the direct forms count (`x != null`, `x == null`, and several of them joined with `&&`); it holds inside the arm only.
- **A `T!` value that is not used in a sync block is a compile-time warning.** To drop it on purpose write `let _ = ...`. A `main` without a declared return type that ends in an error value is a special case of this.
- **When one arm of a branch has no value (`void`) and the other is a `T`, the branch is a `T?`**, and the arm without a value gives `null`.

#### Types

- **`any` is only a shorthand**, an opening made for the time before the type system matures. No type should remain undetermined at run time: after compilation every value has a determined type.

#### Operators and libraries

- **A `slice` whose start lies after its end (`xs->slice<2, 1>`) gives the empty array**, not a fault; this is ordinary operator behaviour. A bound outside the array is still a fault.

#### Modules

- **How roots are merged**: the whole program has one `__root__`, the top-level nodes of every module are on it, and functions take values through capture ports; a capture is bound by "module + name", not looked up by name at the call site.
- **Top-level values are evaluated before `main` starts, on the basis of static dependence**: every top-level value used by a function statically reachable from `main` along the call chain is evaluated, whether or not the arm that uses it is taken at run time.
- **Modules may import each other.** Loading therefore has two phases: first the declarations of all modules are collected, then the function bodies are built. A cyclic dependence among top-level values is a compile-time error.
- **A `var` cannot be exported**; export functions that read and write it. A `main` in an imported module is an ordinary function.
- **A compiled module can be cached** (`.cmo`, cached module object), so that a module need not be compiled again. The cache of a module is one graph rooted at that module; it exists for convenience and speed and need not equal the final result of an actual compilation. C++ modules keep the same extension; the two are told apart by the file header.

#### Directions for the implementation

- **The data model of the graph is the most important design**: it is to be clearly defined, stable, and fit to be published. The module cache, passes written in Camel, compiling compute code into GPU kernels, and `std::cpp` are all built on it.
- **The internal representations of the compiler core may be refactored thoroughly**, for speed and memory efficiency along the whole pipeline.
- **How memory is managed is not part of the semantics of the language**: an object has no observable identity, the moment of collection is not observable, and C++ operators touch objects through handles. Each back end may choose what suits it. The constraints generational collection puts on the authors of C++ operators are acceptable.
- **The parallel scheduler hands subgraphs with nothing to parallelize to the bytecode interpreter.**
- **The output of a program and the text written by `fs` are the same bytes on every platform**; on Windows no `\r\n` is written.
- **The debugger**: the arms of a branch are folded into the frame of the enclosing function by default, and the real subgraph structure can be shown on request; line stepping follows the order of execution, and when a line did not execute because nothing uses its result, an explanation is given.

### 2.12 Supplementary rulings (2026-10-14)

#### Modules and namespaces

- **Top-level `let` and `var` of a module do not depend on the order of writing, only on dependence.** `let area = pi * r * r` may come before `let pi`; a cycle among top-level values is a compile-time error. A local `let` in a function body is still declared before use.
- **Inside a namespace block, the names of the same block and of enclosing blocks can be used without a prefix.**
- **`using namespace`**: may be used on a namespace of the module itself; takes effect only in the block where it is written (in a function body, only in that function); when two `using`s bring in names with the same name and signature that cannot be told apart, it is an error at the use.
- **Name precedence has one rule**: local > this module (the enclosing namespaces and the module top level) > imported by name > brought in by `using namespace` > built in. The first layer that has the name is taken; functions of the same name in one layer form an overload set, and an identical signature is an error; layers are not merged. Operator overloads are the exception and are merged across layers.
- **`import a from m` brings the name `a` in from `m`**, the same as `import { a } from m`, as the language documentation says; a module alias is written `import m as x`.
- **`use` is not implemented** and the reserved word is retired; `import { … } from m::N` is allowed.
- **A namespace block may hold values**: `namespace Config { export let limit = 10 }`, used as `Config::limit`; it differs from a top-level `let` only by the path in its name.
- **A module may qualify its own things with its own name**: in `module shapes`, `shapes::area` is the same as `area`.
- **Diagnostics, the debugger and graph names always show the full name** `module::path::name`, leaving out the module where that is unambiguous.
- **The module cache lives in a `__cmo__/` directory beside the source file**; it is off by default at first; a module served from the cache does not replay compile-time warnings. An importer's cache is invalid when any of these change: the signature of an exported function (including the inferred return type), the module-level values it uses transitively, its effects, the body of a generic function.

#### Types and conversion

- **Converting a value to its own type is legal and does nothing**: `xs as int[]` (with `xs: int[]`) passes, as `p as A` through an alias does.
- **The signature text of the standard library uses type variables, not `any`** (as `print<T>(x: T)`). Camel functions with a variable number of arguments of differing types are not made for now.
- **`any` is to be tightened step by step.** An opening is made only temporarily, for a specific need that the type system cannot yet express; wherever a clear recommended form exists (generics, overloads, concrete types), `any` is rejected and the diagnostic gives the recommended form. `json.decode` becomes a self-describing `Json` type, with `decode_as<T>`. Compile-time errors from now on: `any` as the condition of `if`, `any` as the subject of `match`, the unchecked `as any as T`.
- **A function with a `var` context parameter can be partially applied**: `add<t>` is a function value that writes a captured variable; its type carries `sync`, and each call counts as one write of `t`.

#### Syntax and narrowing

- **`!` and `~` behave like the unary minus**: they bind less tightly than `^` and more tightly than `->`; `!x->f` negates first and then passes the value on.
- **The scope of narrowing**: both `x != null` and `null != x` narrow; in the then arm of `if x != null && ready then ...`, `x` is narrowed (any operand of `&&` that is a direct null test of `x` counts); the else arm of `||` is not narrowed for now. Writing `let x = ...` again inside a narrowed arm is a redeclaration.
- **A nullable function value may be compared with `null`**; "function values cannot be compared" means two function values.
- **`let r = ...` binding a `T!` whose `r` is never used is warned about too** (as the statement form in 2.11).
- **Hovers and documentation take `///` comments only**; an ordinary `//` is not documentation.

#### Operators

- **Users can overload operators in Camel source**, written `func operator &(a: Mask, b: Mask): Mask` (notation provisional). The overloadable operators are those that today become `__x__` functions; `&&`, `||` and constructs such as `??`, `!!`, `->` cannot be overloaded. Names of the form `__x__` stay reserved and are not written by users.

#### Modifying collections

- **`append` returns a new array and does not modify the original**: after `let ys = xs->append<5>`, `xs` is unchanged; to "append to a variable" write `acc = acc->append<5>`, as `x = x + 1`. Changing in place is only an optimization of the implementation (when the old value is no longer used it can be extended where it is), not part of the semantics, and no explicit in-place operator is added. The other standard-library operators likewise. Migration: an unused result of a pure operation (such as `xs->append<5>` written as a statement) is a compile-time error that suggests rebinding.

#### The graph

- **The published node kinds**: `DATA PORT OPER FUNC CALL FILL ACCS CAST COPY BRCH JOIN LOOP GATE SYNC`. `BIND` and `DREF` are removed; higher-order operators such as `map`, and `attempt`, remain operator nodes, with "calls subgraphs" declared in the operator table; besides purity an operator declares whether it may fault, and an operator that declares nothing is treated as possibly faulting.
- **Both forms of branch arms are legal graphs**: the front end produces "one subgraph per arm"; after inlining, the nodes of an arm merged into the enclosing graph as a stretch between the branch and the join are legal too.
- **Whether an operator may fault need not be declared operator by operator**: module authors declare it where useful; an operator that declares nothing is treated as possibly faulting.
- **How side-effect tracking is kept under passes**: effects are a summary on the graph (whether the exit of the graph depends on effects, which captured variables it writes), recomputed from the nodes and checked at every commit; a pass cannot commit a graph whose summary disagrees with its content. Whether a pass is right to delete or move an effect is still that pass's responsibility (section 4).
- **`LOOP`**: the condition subgraph and the body subgraph share one capture table; `for … while` is a third form of the same node; with several state values the body returns a tuple, and the value of `LOOP` is that tuple.
- **The direction for passes written in Camel**: graphs and nodes are immutable values, a pass is `(Program) => Program!`; a rewrite is "a set of changes → `apply` → a new graph or an error value", with no modification in place; a pattern rule is a pair of functions with the same parameters, the graph of the left one being the pattern and that of the right one the replacement. Details later.

#### Back ends and run time

- **The goal of `std::cpp` is programs that can be deployed without the runtime.** Where a faithful translation is impossible, the generated code may use Camel's runtime modules, kept as clean as possible. Generated programs report faults with source positions, keep the recursion limit, and do not grow the stack on tail calls.
- **What has identity is to be listed**: `var` cells, objects modified in place through `var` parameters, objects wrapping a foreign library (Python objects and the like); every other value has no observable identity, and a back end may copy or share it freely. No type word is added to object headers. The module interface generated by `std::cpp` uses standard C++ types and does not expose the runtime's internal types.
- **A back end may collect less garbage, or none**: when and whether collection happens is not observable (2.11).
- **Generational collection is on by default once it is usable.**
- **When the parallel scheduler hands serial subgraphs to the bytecode interpreter, the recursion limit is counted by the same rule**: with a raised `--max-recursion-depth`, a deep non-tail recursion completes on `std::prl` as it does on `std::fvm`.

### 2.13 Supplementary rulings (2026-10-15)

- **After narrowing**, operations meant for a `T?` (`??`, `match`) may still be written on a name known to be non-null, with a warning that it is non-null; a null test on a non-nullable value is likewise allowed with a warning (its result is fixed). In an `if` without `else`, an unused `T!` in the arm is warned about.
- **JSON**: on malformed text `decode` returns an error value of `Json!`, not a fault. The accessors of `Json` accept `Json?` so that they chain, `as_float` accepts integers, and `get<index>` out of range gives `null`. `decode_as` takes the target type as a "shape value" for now; it changes once a notation for explicit type arguments at a call is decided.
- **The cases of a `match`**: the value of every case must be accepted by the type of the matched value.
- **Modules**: a module may import itself, to no effect; a cycle of types across modules gets a cycle diagnostic that lists the cycle; reopening a namespace block does not carry the `using`s of the earlier block; an ambiguous name lists the qualified candidates; the language server shows namespace members by their full names; C++ modules can export values (`math::pi`).
- **Collections**: returning a new value versus changing in place is no longer told apart by two names (`sort` and `sorted`): only `sort` remains, and it returns a new array; `xs = xs->sort` sorts in place when the old value is no longer used, the same optimization of the implementation as `acc = acc->append<v>`. Tensors keep an explicit in-place operator (2.10); arrays have none.
- **Operator overloading**: a user overload more specific than a built-in one wins; overloading `==` gives `!=`; an operator declared in a namespace block belongs to the module.
- **The loop header takes the `do` form** (superseding the provisional notation of 2.10): the state is declared once, several state values are separated by commas and the value of the loop is their tuple; `do` is followed by an expression, with `{` read as after `then` (`{ x: 1 }` is a struct, `{ 5 }` a block).

  ```camel
  let total    = loop acc = 0 for x in xs do acc + x * x
  let (i, acc) = loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)
  let p        = loop p = { x: 0 } for d in ds do { x: p.x + d }
  let found    = loop hit = -1 for x in xs while hit < 0 do if x > 9 then x else hit
  ```

- **Explicit type arguments at a call: both layers.** First: a type parameter that appears only in the result type is inferred from the expected type (a `let` annotation, a parameter's type, a return position), as `let cfg: Config! = json::decode_as(text)`; this extends 2.10's "a generic is instantiated once the context determines it". Second: where nothing can be inferred, `f::<T>(x)` (as in Rust), as `json::decode_as::<Config>(text)`, `empty::<int>()`. `f<w>` still means `with` arguments and partial application only; both together are `f::<T><w>(x)`. With these, the "shape value" form of `decode_as` is retired.
- **Sum types follow Rust's `enum`**: variants have names and may carry a tuple or a struct of data; they may be generic and recursive.

  ```camel
  type Shape = enum {
      Circle(float),
      Rect { w: float, h: float },
      Empty,
  }
  let s = Shape::Rect { w: 2.0, h: 3.0 }
  let area = match s {
      case Shape::Circle(r)     => 3.14159 * r * r
      case Shape::Rect { w, h } => w * h
      case Shape::Empty         => 0.0
  }
  type Tree<T> = enum { Leaf, Node(Tree<T>, T, Tree<T>) }
  ```

  A `match` over an `enum` that lists every variant needs no `case _` (as with `bool`, `T?`, `T!`). Variants are not told apart by the type of their value (that would be run-time type discrimination, excluded by 2.9). The `A | B` that can be written today but that no value can enter is retired; `|` remains the logical or. No node kind is added on the graph: constructing enters a tag, `match` reads the tag, branches and takes the data out in the arm, as for `T?` and `T!`, which can be seen as two built-in `enum`s.
- **Tensor dimensions may have names**: where a dimension first appears in a parameter's type it is bound with `constas n` (the same pattern as `typeas T` binding a type variable), and is written `n` afterwards; a name that appears only in the result type is an error.

  ```camel
  func dense(x: Tensor<f32, const [constas n, 3]>, w: Tensor<f32, const [3, constas k]>): Tensor<f32, const [n, k]>
  ```

  Dimensions allow addition and multiplication by a constant (linear forms, whose equality is decidable); other results are unknown. Where an argument's dimension is unknown (`?`), compilation lets it pass and the call checks it at run time (as `as` does). Named dimensions are only checked; they create no instances: a function still has one graph.

- **The reserved `T ? U` is retired**: a value that may fail is written `T!` only, and error values have one type (told apart by their Kind). Should typed errors be needed later, they come as `T ! E` once `enum` exists. The special reading of `T ?` followed by a type goes with it: `?` is only the nullable mark.
- **`std::cpp`**: the fault reports of generated programs are byte-identical to the interpreter's; recursion is counted exactly (as on every back end); the extra blank line of `println<fmt>` and the extra line feed of `print<fmt>` are defects of the interpreter and are fixed there; `std::cpp::bench` is removed; `map`/`filter` run sequentially in generated programs for now; `main` takes its parameters as `--arg name=value`, as the command line does.

---

## 3. `macro`

- `macro` is a native marker provided by the language core to help with dynamic graph rewriting: a function marked `macro` can be evaluated early by the `std::macro` pass, which computes the static values and forms a new static graph.
- **`macro` itself does not define a stage, and it is not required.** When the pipeline has no `std::macro` pass, a macro function is just an ordinary function.
- A value is either a compile-time constant or the return value of a macro; only these two can be processed by a macro.
- When the body of a macro function has side effects, they are handled as in any other normal function: the effect happens at the moment of evaluation. This is expected behaviour and cannot be avoided in practice.
- **Failure of macro evaluation**: report a compile-time error directly (see 2.7).

Design motivation: the dynamism of the Python language itself determines its flexibility, and in turn brings the trouble of extracting a static graph. Camel wants a static graph and complete type information, in pursuit of the best room for optimization. The need to construct graphs dynamically is met by the macro system: with a pipeline of multi-stage graph rewriting, first construct the static graph dynamically, then optimize the static graph aggressively, and finally execute it or translate it into another format.

---

## 4. Passes and pipelines

- The whole run is modelled as multi-stage graph rewriting. The stages are defined by the sequence of passes the user composes on the command line, for example `camel xxx.cml std::macro std::opt ...`.
- **The language core does not restrict the order of passes.** But a different order may indeed give a different result of execution: passes do not guarantee that they produce the same graph, they modify the graph, and they may have side effects. **"Behaviour is unchanged under any sequence of passes" is not a property of Camel; do not implement or test it as a contract.**
- With no user-specified pass, the default is `std::default` (which points to the node virtual machine; the code is authoritative).
- The starting point of this design is engineering convenience, not academic elegance: with one body of code, changing the combination of passes does many things — prototype testing, debugging, compiling and optimizing, exporting for deployment, translating into other formats and so on — without rewriting similar logic in another language. This is one of the characteristics that follow from being graph-native.
- **Source-level restrictions take effect only in the step "translate source to graph".** The rules of `var`/`let`, `wait`, `sync` and so on constrain how the compiler constructs the graph; once the graph is obtained, these rules are no longer constraints on the later stages of graph rewriting. What constraints a pass observes is that pass's own contract; whether the semantic behaviour of the graph changes after a rewrite is the responsibility of the pass's developer and of its user respectively. Camel is only responsible for compiling to the graph and places no restriction on passes.
- The optimization passes in the standard library should not make a program wrong. This is an ordinary software-quality requirement, ensured by testing.
- **Automatic differentiation is a library built on the pass mechanism, not part of the language core.** It has its own constraints: a graph on which macros have not been executed does not carry enough information for differentiation, so using `grad` and the like requires `std::macro` in the pipeline. This is a precondition of the library, not a constraint of the language core; when it is not met the library should give a clear diagnostic.
- Export (such as ONNX) is a translation pass, likewise built on the pass mechanism.

---

## 5. Common misreadings

All of the following are misreadings that have actually happened; check against them before changing code or documents:

1. Treating "an unused effect outside a sync block does not execute" as a defect to fix, or turning it into an error. — It is expected behaviour.
2. Treating `sync func` as making the function body a sync block automatically, or treating the prefix and suffix `sync` as two spellings of one modifier. — Their meanings differ.
3. Believing that the effects inside the body of a function called from a sync block therefore execute. — A function's behaviour is decided by its own subgraph.
4. Implementing a `var` parameter as pass-by-value with a copy back on return. — Camel is by reference by default.
5. Implementing the immutability of `let` as run-time memory protection. — It is a compile-time check.
6. Believing that `wait` applies to all the code after it. — It applies only to the one expression that follows.
7. Treating side effects in the body of a macro function as an exception or an error. — They are handled as in an ordinary function.
8. Believing that a macro function cannot run without `std::macro`. — It is then an ordinary function.
9. Treating "behaviour is unchanged under any sequence of passes" as a contract of the language, and judging sensitivity to pass order a defect on that basis. — Passes may change the graph and have side effects.
10. Treating the preconditions of automatic differentiation as constraints of the language core. — They are constraints of the library.
11. Picking one of the derived characteristics as Camel's "core selling point" (for example "one body of code, many uses"). — The core is being graph-native; the rest are natural consequences.
12. Changing the semantics to make some test pass. — First confirm the semantics, then judge whether the test or the implementation is wrong.

---

## 6. Questions not yet decided

When you meet one of the following, ask the language author:

- The concrete content of the type-expression language: the meaning and fate of each of the reserved type operators `&`, `\`, `+`, `-`, `^` and of `typeof`, `keyof`, `ident of T`; how conditions and matching are written in type functions.
- The meaning of `typeof` applied to a constant value, and its use in generics.
- How failure or non-termination of the evaluation of a type function is handled.
- The behaviour when the same mutable object is written through references in several places outside a sync block, none of them with `wait`.
- **After a fault in a parallel `map`, whether elements of smaller index that have not started still run** (`parallel-fault-lower-elements`):
  `range(0, 64)->map<bad>` where every element faults and a worker happens to run element 40 first. Implemented: only the elements with an index above 40 are not started, 0 to 39 still run, so the one reported is always element 0, as on the linear back ends and the same on every run.
  Alternative: start no element at all after a fault; "the smallest index" is then taken among the elements that happened to have started, and may differ from run to run.
- **Whether arguments of built-in operators enter `T?` / `T!`** (`entering-operator-args`): `xs->append<5>` (with `xs: (int?)[]`), `len([])`.
  Implemented: they do not; the first needs `5 as int?`, and the `[]` of the second has no element type and is not reported. The parameter types of a built-in operator are computed from the arguments by its C++ type rule, so there is no target type before the call.
  Alternative: let a type rule name the types it accepts, so that built-in operators behave as user functions do.
