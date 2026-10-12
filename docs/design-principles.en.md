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
| `macro` function | A subgraph that can be evaluated early, its result materialized as a constant of the graph | Builds a static graph dynamically |
| `a ?? b`, `a !! b`, `x ?-> f` | A branch: the condition is a one-bit tag on the value (null / error), and the two arms are the original value and the substitute | The same structure as `if` |
| `throw e` | Returning an error value | The same structure as `return` |
| Fault isolation (`attempt`, name provisional) | One tagged call of a subgraph | The boundary of the fault is the boundary of the subgraph; the structure does not vary with the content of the subgraph |
| Generic function | A subgraph template; each call with a set of concrete types instantiates a subgraph whose types are all concrete | Only concrete types are on the graph |
| Type function | Evaluated while the graph is built; leaves no node on the graph | A value of kind "type" does not exist at run time |
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
- **A value whose static type is `any` printing as `<any>` is normal**: Camel has no ability to determine a type at run time, and to prevent serious errors this is the safest course.
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

- Whether the configurable limit for non-tail recursion (see 2.9) takes the same default on every scheduler.
- The design of the loop node (see 2.9): its structure on the graph, and its relation to tail-recursive subgraphs, automatic differentiation and export.
- Whether `raise` stays: `throw` has been decided to mean returning an error value; whether `raise` is kept as a synonym for it or removed.
- The type notation for "may fail": 2.9 decides on `T!`; the grammar also has an earlier reserved `T ? U` (called ErrorThen in the syntax tree), and the relation between the two is to be decided.
- The concrete content of the type-expression language: the meaning and fate of each of the reserved type operators `&`, `\`, `+`, `-`, `^` and of `typeof`, `keyof`, `ident of T`; how conditions and matching are written in type functions.
- How numeric parameters in types (such as the dimensions of a tensor) are bound to names, and how arithmetic is done on them.
- The meaning of `typeof` applied to a constant value, and its use in generics.
- Whether the type parameters of a generic function may be given explicitly at the call.
- The position of `any`: kept as an escape hatch, or replaced by generics; whether the currently unchecked `any as T` is tightened.
- How failure or non-termination of the evaluation of a type function is handled.
- The behaviour when the same mutable object is written through references in several places outside a sync block, none of them with `wait`.
- How side-effect tracking is preserved under the various passes (see 2.7).
