# Camel Design Principles and Semantic Rulings

> **This is a translation. The authoritative text is [`design-principles.cn.md`](design-principles.cn.md).**
> Where the two differ, the Chinese file is right and this file is to be corrected.

This document records the language author's explicit statements about what Camel fundamentally is and about its semantics, and it states only the **current** rules. It is normative: when the implementation, another document or a test result conflicts with this document, this document prevails, and the conflict is to be recorded as a defect or a to-do, not used as a reason to change the semantics.

**Before changing Camel's semantics, passes, schedulers, exporter or automatic differentiation, read this document first.** Questions that it does not cover or still marks as "undecided" go to the language author; do not choose on your own.

The rules are organized by topic. When and how each ruling was made or changed is in the [log of design-principles rulings](design-principles-log.cn.md) (in Chinese, in chronological order, including the statements that were superseded or withdrawn; the table that maps its old section numbers to the new numbers of this document is at its beginning). Statements marked as a "tendency" or "in principle" are not hard rules; do not implement or check them as prohibitions.

---

## 1. What Camel is: graph-native

Camel's most central characteristic is that it is graph-native. Its other properties are natural consequences of this position, not features added on top. The eight lines of `ZEN` outline this position: the first two say the graph is fundamental, the other six are corollaries.

- **It models computational intent only.** A Camel program describes "what is to be computed and what depends on what". It does not itself carry CPU-intensive work, nor configuration of all kinds; operators, the runtime and passes handle those. That is exactly why it is free and flexible.
- **The graph is native, not a product.** The graph is not captured or recovered from code in another language; what the source says is the graph.
- **Passes are functions, functions are graphs, and execution is rewriting to the empty graph.** A pass is a function transformation and a function is a graph; compile-time evaluation, optimisation, export and execution are all stages of rewriting the graph, and execution ends when the empty graph is produced.
- **Language elements correspond directly to the graph.** To understand or design any language construct, first answer what it is on the graph. The criterion: a syntactic element must have a stable mapping to the graph; that is what being graph-native means. If what a construct looks like on the graph depends on what is written inside it, the construct should not enter the language.
- **Different from traditional control flow.** Syntactic sugar is not used to imitate traditional control flow; the mapping between syntactic elements and graph nodes is kept simple. (This does not exclude concise forms with no ambiguity, such as list comprehensions.)
- **Not limited to AI.** What is modelled on the graph is the intent and dependencies of a computation; HPC and simulation workloads of all kinds fit equally. The outward statement of position: Camel is a graph-native general-purpose programming language, and AI is its foremost application area; it no longer calls itself a domain-specific language for AI. From the viewpoint of the paper, Camel does not itself carry CPU-intensive work; from the viewpoint of a general-purpose language, it must still have a certain generality. The two viewpoints coexist.
- **Only one language.** Computing types from types, and generating code from the structure of a type, are written in the main language: a type is a compile-time value, and a type function is an ordinary function that runs at compile time, using the same constructs as the value level (see [Generics by structure](design/generic-structure/README.cn.md)). There is no separate type-level language (such as TypeScript's conditional types, mapped types and `infer`). Graph transformations, rules and schedules are likewise written in Camel itself.
- **Shorthands may exist, but there is only one semantics.** A shorthand (such as a decorator) is a mechanism for library authors; it may exist, but it can only be another way of writing the same semantics and must not carry rules of its own.
- **Red lines are few, and each has an enforcement mechanism;** everything else is a tendency or a quality requirement and is recorded as such. The current list of red lines, each with the mechanism that enforces it:

  1. A Camel program cannot build a structurally broken graph — whole-graph validation on commit.
  2. A back end sees only a graph in the executable state (one writing per value, acyclic, correctly typed) — checks at the entry of the back ends and of the export passes.
  3. A node that can fail or has an effect is not moved out of its control region — validation on commit.
  4. A rule passes random testing before it is admitted; a fact satisfies the join and monotonicity laws — test tools enumerate the `rule` and `fact` declarations.
  5. A graph and its source round-trip — round-trip tests over every test case and random program.
  6. The result is determined (same input, same configuration, same result) — a fixed commit order for the parallel combinators; saturation and extraction decide by id; repeated runs are compared.
  7. Numerical results are reproducible on one device (unless `nondeterministic` is granted) — bit-for-bit comparison over repeated runs.
  8. A promise (`pure`, `uses`) is not violated — compiler checks.
- **No compatibility cost is considered during the internal-testing stage.** An old form is deleted outright: no alias, no transition layer, and no diagnostic or note pointing to the new form; once deleted it is simply an unknown name or a syntax error.
- **Our own libraries are written in Camel as far as possible**, to expose the language's shortcomings and absorb them into the core (see 10.4).

---

## 2. Programs and graphs

### 2.1 Language elements and the graph

| Language element | What it is on the graph | Resulting behaviour |
|---|---|---|
| Expression, operator call | Nodes and data edges | Evaluated by dependency; a node nothing depends on does not execute; nodes that do not depend on each other may run out of order or in parallel |
| `sync` block | Control edges between adjacent expressions | Executes in written order inside the block |
| Effects on the same resource | Control edges between the effects that use the same resource inside one function body | Execute in written order (3.3) |
| Function | Subgraph | Its behaviour is decided by the function itself; a call only places the subgraph node in a larger graph and does not change its structure |
| `var x = v`, reading, assigning | Three kinds of operator node: make a cell, read it, write it; the name `x` is bound to the cell | What is mutable is the cell, not the value |
| Lambda, function as a value | A function constant; with captures, one more node that puts the captured values in | A function value is "graph + captured values" |
| `f<w>` as a value (partial application) | A function value that captured `w`; its graph hands the remaining arguments and `w` to `f` | The same structure as a lambda |
| `macro` function (tag `#macro`) | A subgraph that can be evaluated early, its result materialized as a constant of the graph | Builds a static graph dynamically |
| `if`, `match` | A branch, one subgraph per arm | Only the chosen arm is evaluated |
| `a ?? b`, `a !! b`, `x ?-> f` | A branch: the condition is a one-bit tag on the value (null / error), and the two arms are the original value and the substitute | The same structure as `if` |
| `a && b`, `a \|\| b` | A branch: `a && b` is `if a then b else false`, `a \|\| b` is `if a then true else b` | The right side is an arm, evaluated only when needed; cannot be overloaded |
| `a & b`, `a \| b` | An ordinary operator node | Both sides are evaluated; can be overloaded |
| `if x != null then A else B` | The same graph as a `match` on `x`: the non-null arm is entered with the opened value | Narrowing is not a separate mechanism |
| A value entering `T?`, `T!` | One entering node per level | Entering happens only where it is needed |
| Constructing an `enum`, and `match` | Constructing puts in a tag; `match` takes the tag, branches, and takes the data out in the arm | No new kind of node on the graph; `T?` and `T!` can be seen as two built-in `enum`s |
| `throw e` | Returning an error value | The same structure as `return` |
| Fault isolation (`attempt`) | One tagged call of a subgraph | The boundary of the fault is the boundary of the subgraph; the structure does not vary with the content of the subgraph |
| `map`, `filter`, `reduce`, `foreach` | One operator node; the element function is passed in as a function value | Higher-order operators are not a new kind of node |
| Generic function | A subgraph template; each call with a set of concrete types instantiates a subgraph whose types are all concrete | Only concrete types are on the graph |
| Type function | Evaluated while the graph is built; leaves no node on the graph | A value of kind "type" does not exist at run time |
| Tag `#` | A compile-time fact on a declaration or a node; it produces no node on the graph | Exists at compile time; cannot be "called" |
| `loop` expression | One `LOOP` node with a condition region, a body region and state edges | The state is decided by the loop header, not by what the body contains |
| A `let` at the top level of a module | A node of the common root graph | Evaluated when depended on; importing executes no code |
| A function using a top-level value of a module | A capture port of the function graph, bound to "module + name"; the value is passed down along the calls | No node reference across graphs |
| `import { f } from m`, `import { v } from m` | No node is built: calling `f` is an ordinary call; `v` is an ordinary edge on the root and a capture port inside a function | Importing executes no code |
| Importing a generic function | No node is built; one instance per set of types, shared by the whole program, belonging to the module that declares it | Only concrete types are on the graph |
| `namespace`, `using namespace`, `m::f` | No trace on the graph; they only decide which "module + qualified name" a name resolves to | A namespace is part of the name |
| Pass | A program that turns one graph into another (or consumes a graph) | An open pipeline |

### 2.2 The structure of the graph

- **There should be no node reference across graphs.** A node in one graph does not point to a node in another graph; between graphs there is only one relation, "a graph refers to a graph" (a call). Every design must pass this first.
- **The node kinds visible outside**: `DATA PORT OPER FUNC CALL FILL ACCS CAST COPY BRCH JOIN LOOP GATE SYNC`. Higher-order operators such as `map`, and `attempt`, remain operator nodes; the operator table declares that they "call a subgraph".
- **Both forms of a branch arm are legal graphs**: the front end produces "one subgraph per arm"; after inlining, the nodes of the arm are merged into the outer graph and form a stretch between the branch and the join, which is also legal.
- The details of graph storage and the graph API are in [Graph store and graph API (second version)](design/graph-store/README.cn.md).

### 2.3 The data model of the graph

- **The data model of the graph is the most important design**: it must be clearly defined, stable, and open to the outside. The module cache, writing passes in Camel, compiling computation code into GPU kernels and `std::cpp` are all built on it.
- **Node attributes belong to the externally visible graph data model**; passes written in Camel can read and write them too.
- **The internal representation of the compiler core may be completely restructured**, for the speed and memory efficiency of the whole pipeline.

### 2.4 Capacity and defaults

- **A single graph has no capacity limit.** Ids are always 32-bit (the language author confirmed the [graph store and graph API, second version](design/graph-store/README.cn.md) on 2026-10-19, which removes the former limit of 65,535).
- The total limit of each of the three spaces for graph, types and metadata is 256 MB; expansion that does not stop is stopped by the exporter's own round budget.

### 2.5 Round trip between graph and source

- **A graph and the source have the same expressive power**: any legal graph can be written as Camel source, and writing it back and compiling it again gives the same graph (names, source locations and the like excepted). This is enforced by a round-trip test (all test cases and randomly generated programs); a graph structure that cannot be written back is regarded as a gap in the language and must be filled.
- The standard pass `std::cml` exports the current graph as Camel source: the product is as readable as possible, but human readability is not a constraint; generic instances are written as functions of concrete types, and the inlined parts need not be restored; large constants are put in a data file beside it and referred to from the source.

---

## 3. Evaluation and effects

### 3.1 Evaluation by dependency

By default evaluation is driven by dependency, and there is no fixed order of execution.

- **A call with effects outside a synchronous block, whose result is not used, does not execute.** This is a natural result of evaluation by dependency, a legal and expected behaviour, not an error, and no error needs to be reported. When the compiler is smart enough it may give a warning.
- A resource decides only the order among the parties that execute, not whether anything executes (3.3).
- **Effect functions also return a value.** `println(x)` returns `x`, so that debugging code such as `x->println->f` shows the data at each stage; it is not the mechanism that makes an effect run (when the value is used, it runs anyway). If its return value is used, it is certain to be executed and ordered before the use.
- **A node runs if and only if this run uses it** (the author's ruling, formal F1). A node used only by one arm runs only when that arm is taken; a node used only inside a loop body does not run when there are zero rounds. An implementation may run a fixed plan, but the plan must agree: a node read by only one arm sinks into that arm, a node read by several arms is copied into each (one copy runs each time), and a pure node that cannot fault may stay outside.
- **Order does not decide whether something runs** (formal F2): the order on one resource or within a sync block constrains only effects that run anyway; it never makes an effect run because it is ordered before another.
- **The graph has three kinds of edge** (settled 2026-10-23, see [Edges of the graph](design/effects/edges.cn.md)):
  - **data edges**: where a value comes from. A node's inputs are an ordered list of ports, each with a **role**, ordinary or context (`with`), taken from the callee's signature; the edge itself is of one kind. Passes that do not care about roles (dead code, common subexpressions, scheduling, structural rule matching) see one kind of edge; passes that do (partial application, differentiation, specialisation, export) read the port's role.
  - **sync edges**: between adjacent statements of a sync block; the later comes after the earlier, and the earlier is used because of it.
  - **order edges**: order only — when both ends run, the first runs before the second; they do not pass on "used". They come from effects on one resource, from writes to one `var`, and, when a call in a sync block is inlined, between the adjacent statements of the chain and the effects of the body.
  - **Pure computation has no order edges**; pure computation that cannot fault is not on the sync chain either (the chain links only statements with effects or that may fault, 3.4); it has data edges only, the one exception being the block's value, which takes the sync edge from the chain's tail; there is no "control edge" kind.
- **The execution rule**: the nodes reachable from the program's result along data or sync edges run; order edges take no part; among the nodes that run, the order respects all three kinds. "Runs if used" and "order does not decide whether something runs" are consequences of this rule.
### 3.2 A function's behaviour is decided by the function itself

A function, once compiled, yields its own subgraph. An outside call merely arranges this subgraph node in a larger graph and does not change its own structure.

```camel
func log(x: int): int {
    println(x)          // the result is not used and the body is not a synchronous block: does not execute
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    return b            // returns 3, prints nothing
}
```

The body of `main` is a synchronous block, so the two calls to `log` execute in order. But `println` belongs to the body of `log` itself, which is not a synchronous block there, so it does not execute. To print, `log` should be declared as `func log(x: int): int sync { ... }`.
- **Pure computation is deterministic** (the author, 2026-10-24): the same inputs always give the same result. An operation whose result varies with time, scheduling or the outside world is either an effect (such as reading the clock, `now()`) or says it is nondeterministic in its declaration (such as `first_of`, or `merge` in arrival order). Optimisations that reuse results (incremental computation, common-subexpression merging) rely on this.

### 3.3 Resources, `uses` and `pure`

- **Vocabulary** (the author, 2026-10-24): the mechanism as a whole is the **resource effect system**. A **resource** is what an effect acts on (the console, a file, a `var`…); an **effect** is one access to a resource (a read or a write); `uses R` declares a function's set of effects, and `pure` is the empty set. The word "side effect" is not used: in Camel an effect, like a value, is a result the program writes down, not something incidental. Effects indexed by resources come from Lucassen and Gifford's polymorphic effect systems (FX).

- **Resource**: something whose accesses need to be ordered. Resource classes are declared by a library with the keyword `resource` (`export resource fs`) and can be grouped (`resource io = [stdout, fs]`); they share the namespace with other names, are imported by the same rules, and can be written qualified. The most basic resources of the standard library (`stdout`, `stderr`, `stdin`, `fs`, `clock`, `env`) are visible by default.
- **Every `var` is automatically a resource**; a `var` formal parameter of a function is a resource parameter, which at the call corresponds to the variable the caller passes in; when a closure captures a `var`, its function type carries that resource.
- **Resources are part of the function type**: `func save(p: Params): unit uses fs { ... }`. `uses` is followed by a comma-separated list of resources, without parentheses. A native operator (a declaration without a body in a module) writes `uses` in its signature; a native operator without it is regarded as using the "global" resource, kept in order with every effect.
- **The resources of a Camel function are inferred by the compiler** (the sum of the resources of everything called in the body, a fixed point when recursive); they need not be written by hand; the editor's hover and the documentation show the inferred resources with qualified names. Writing `uses` explicitly is a promise, and a body that exceeds it is a compile error; when the inferred resources of an exported function change, a warning is given.
- **Effects follow the resource declaration**; a `var` in a signature is checked against the resource declaration, and an inconsistency is a declaration error.
- **`pure`** promises that the function uses no resource, and is checked by the compiler; it can be written on a function declaration and on a function type (for example, to require a callback to be pure: `f: (x: T) => U pure`).
- **The parameters of function types are by default generic over resources**: `f: (x: int) => int` without resources means `f` may use any resources, and the function calling it uses these resources accordingly; this is determined per instantiation. A function with fewer resources may be used where more resources are allowed, not the reverse.
- **The same resource is automatically ordered**: within one function body, the effects that actually execute and use the same resource execute in written order (the compiler generates control edges). Calling another function is, at the call site, "one effect using its resources"; the compiler does not reorder across functions.
- **The purity of an operator must be declared.** An operator without a declaration is treated as having effects (at worst less optimization, never a wrong result); the standard library modules in the repository must declare operator by operator, and for third-party modules a hint is given when a declaration is missing.
- **Whether an operator may fault is not required to be declared one by one**: module authors declare as needed, and an operator without a declaration is treated as possibly faulting.
- Precedents and trade-offs: inference rather than mandatory annotation (the lesson of Java's checked exceptions); the resources of higher-order functions are determined by instantiation, without Koka-style row polymorphism; no effect handlers are provided; resources are distinguished by concrete object through the handle types of the next item (decided, not yet implemented).
- **Unordered resources**: effects on a resource declared `resource net unordered` keep no written order among themselves and may run at the same time when independent; "order does not matter" is the declaring library's promise. When order is needed, write the calls in a `sync` block, or use a handle.
- **Handle types (per-object resources)**: for a type tagged `#resource`, each value is a resource of its own; operations write `uses parameter` in their signatures (such as `func next(s: Stream<T>): T? uses s`), as `var` parameters do. Operations on the same handle follow the written order, different handles constrain nothing; the compiler infers at the call which value is used. Used for streams, sessions, file handles and the like.
- **Concurrency needs no `async`/`await`**: independent computations and effects on unordered or different resources may run at the same time; while an effect waits (network, disk, subprocess, a model call), the runtime runs other ready nodes of the graph. This is implementation; the meaning equals executing one by one in some valid order, and `std::nvm` may block one at a time.
- **Cancellation**: only effects on resources or operators declared `cancellable` can be cancelled, leaving no inconsistent state (the library's responsibility); others complete and their results are discarded. The standard `first_of(a, b, …)` takes the result of the first to succeed and cancels the rest; its result depends on completion order, a nondeterminism stated in its declaration.
- **Different resources do not affect each other** (the author's ruling, formal F9): "Effects on the same kind of resource are ordered naturally through the `uses` resource system; different kinds do not affect each other unless a sync block is written." Conflict is judged by resource only; effects on different resources have no order, and an order needs a sync block.
### 3.4 `sync` blocks

A `sync` written before a block (including the postfix form on a function body, `func f(): T sync { ... }`, and a standalone `sync { ... }`) means this is a synchronous block: **the effects of the statements it writes directly certainly happen, in written order**; pure computation has no observable order, and the compiler arranges it freely (the author, 2026-10-24). This is the as-if rule of C and Java: whatever the program can observe happens in written order. **A fault is observable**: a computation that may fault (integer division, indexing, a conversion that can fail…, by the operator table's conservative classification) stays in its written place just as an effect does: `let r = 5 / z; println("after")` does not print `after` when `z` is zero (as with Java's precise exceptions); only computation that is **pure and cannot fault** is arranged freely by the compiler (the same class as "a part moved out of a loop body must be pure and unable to fault"). This class ("freely arrangeable computation") is **one predicate**, used everywhere: leaving the sync chain, moving out of a loop body or an arm, being dropped by a rewrite (9.7), getting no order edges after inlining, and being computed ahead during extraction. A call is judged by the facts inferred for its callee: a function whose body contains computation that may fault, or a recursive function (recursion may exceed the depth limit), counts as possibly faulting, the same method as resource inference. Arms, loop bodies and nested blocks written inside it are not governed by it (see "a sync block does not reach inward" in this section).

- It serves "I want it to happen" and "order across resources". The sync chain links only the statements with effects or that may fault (and structural statements whose arms or loops contain them), and its tail reaches the block's value (through a `GATE` when needed, 3.1); pure statements are not on it. It is not extended into synchronization by category.
- **Timing uses `time::timed(f, x)`**: it runs `f(x)` once and gives back the result and the elapsed time. Reading the clock is an effect, and "it measures this computation" must be a data dependency: `timed` keeps the call from being computed ahead, moved or merged with another, and waits for a device to finish. Subtracting two `now()` readings around a pure computation does not measure it (constant folding, merging and asynchronous execution all break it). Repeated measurement with statistics is `bench::timeit`, built on `timed`.
- **A sync block does not reach inward** (the author's ruling, formal F17): a sync block governs only the statements it writes directly; arms, loop bodies and nested blocks written inside it run as they are written, and to synchronise them write `sync` there. A block's meaning does not change with where it is written. Example: in `func f(c: bool): int sync { if c then { println(1); println(2) }; return 0 }` the `if` is on the sync chain and its value is `println(2)`, so `println(2)` runs and `println(1)` does not; to print both, write `if c then sync { … }`.
### 3.5 References, `var` and `let`

- Everything in Camel is a reference by default. A closure's capture of a variable is also a reference.
- `var` marks a mutable object, `let` an immutable one.
- `let` is strictly unmodifiable: an operation that modifies a variable must explicitly declare which parameter is `var`; passing a `let` variable to a `var` parameter is not allowed, so in theory modification cannot be done. This is a **compile-time checking constraint**, not a lock on memory. Passing a `let` object to a `var` parameter is a compile-time error.
- **The declaration of mutability is a closed loop.** For a function to modify a parameter internally, it must use a function that itself has effects: assignment in Camel is a function with effects; or other built-in C++ operators whose declarations also explicitly mark the effect and require the corresponding input parameter to be mutable. So, to modify a parameter, that parameter must be declared `var` (`modify(var i: ...)`); otherwise there is no effective way to modify it. Mutability is passed up from the lowest operator declaration, and every level must declare it explicitly; a function that does not declare a parameter `var` cannot modify it. Assigning to a non-`var` parameter inside a function body, or passing it to an operator that requires mutable input, is a compile-time error.
- **The tracking of modification should be complete**: a function writing a `var` through a capture (including a variable at the top level of a module) is, like writing through a `var` parameter, to be recorded at the call site as one write to that variable.
- **A function with a `var` context parameter can be partially applied**: `add<t>` is a function value that writes a captured variable, its type carries that resource, and each call is recorded as one write to `t`.
- **Camel favours immutable variables.** `var` and the assignment operators are only an opening left for convenience and should be used as little as possible. Model computation should be written as a pure data flow with `let`.
- **Differentiation and variables**: in a function being differentiated, when the gradient would have to pass through the reading and writing of a `var`, it is rejected outright with a clear diagnostic, and not supported. There is no need to write `var` just for differentiation.
- **A top-level `var` of a module read and written directly by a function** (such as the top-level `cnt` in `fib1.cml`): not fully implemented; leave it alone unless it becomes necessary.
- **Known shortcomings.** Camel still has deficiencies in the management and tracking of effects, which need further study. Problems that appear when captured references, the tracking of effects and the passes interact should be recorded faithfully as findings to be studied, with a minimal reproduction; only clear implementation errors should be fixed, and ad hoc rules should not be used to cover up.
- **A path rooted in a `var` can be assigned**: `v.f = e`, `v[i] = e`, `v[i].f = e`, `v[i] += e` and so on mean "replace `v` as a whole by the updated value" (equivalent to `v = set(v, i, set(v[i], f, e))`); value semantics is unchanged: after `let a = v`, changing `v` leaves `a` alone. When nothing else shares the value, the update is in place (the guarantee of 10.5). An index out of bounds on the path is a fault (3.9).
- **Reading a `var` uses the nearest write before it** (formal F3), which therefore runs; writes are only ordered among themselves: in `x = 1; x = 2; return x` the first write is not used and does not run.
### 3.6 Higher-order operators and parallelism

- **There is no execution order among the elements of `map` and `filter`**: a synchronous block does not affect the execution order inside `map`; a parallel scheduler may execute the elements concurrently, and the results are arranged by element position; this is exactly where the speed-up from parallelism comes from (see `test/cases/para/core/monte_carlo_pi.cml`). `foreach` and `reduce` execute in element order; their unordered parallel versions are `unordered_foreach` and `unordered_reduce` of the `para` module. There is no `ordered_map` or `unordered_map` operator in the repository. The order of effects inside the element function is not guaranteed under a parallel `map` and is the user's responsibility.
- **The parallelism of a higher-order function comes from a declared property** (the elements do not depend on each other), not from the name; a function written by a user that declares the same property can also run in parallel.
- **The granularity of parallel scheduling**: not down to the atomic level. Creating a task for every very small function call is pointless and only adds burden.
- **When one element of a parallel `map` faults**: the elements already started run to the end, and those not yet started are not started. When several elements fault at the same time, the one with the smallest index is reported.

### 3.7 Branches and `match`

- **An `if` without `else` has no value itself**; its arm may be of any type, and the value of the arm is discarded: `if debug then println('checking')` is legal, `let x = if debug then 1` is a compile-time error. Nothing changes on the graph: one branch plus two arms, the unwritten `else` is an empty arm, and no value is taken at the join. An arm need not write `return null` just to make the types match.
- **When one arm of a branch has no value (`void`) and the other is `T`, the branch is `T?`**, and the arm without a value gives `null`.
- **A `match` must have a default branch.** Lacking `case _` is a compile-time error; except when both literals of `bool` are listed, when all variants of an `enum` are listed, and for the fixed patterns of `T?` and `T!`. The compiler does not append branch arms the user cannot see. The value of every branch must be acceptable to the type of the matched value.

### 3.8 Null

- **"May be null" and "may fail" are two dedicated built-in forms**, written `T?` and `T!` (3.9). Each carries a one-bit tag and can be taken apart only with `??`, `!!`, `?->` and the fixed patterns of `match`. There is no run-time discrimination of general union types.
- **`a ?? b` handles null.** The right side may be nullable: when the right side is `T?` the result is `T?`, and when it is `T` the result is `T`; it associates right to left, so `a ?? b ?? 9` means "try a, then b, finally give the default". On the graph it is a branch.
- **A value entering the position of `T?` or `T!` is entered automatically**, as long as the target type is known (an argument, a typed `let`, an array element, the two arms of a branch); `null` likewise. The reverse direction is never automatic: `T?` and `T!` can be used as `T` only after being taken apart with `??`, `!!`, `?->` or `match`. The element type of an empty array literal `[]` is likewise taken from the target position.
- **`x == null` and `x != null` can be written**, and the result is `bool`. Two function values cannot be compared, but a nullable function value can be compared with `null`.
- **In the non-null arm of `if x != null then ... else ...`, `x` is `T`.** This is equivalent to a `match` on the nullable value: the port entering that arm carries the opened value. It applies only to `let` bindings and parameters (a `var` may be rewritten elsewhere); only the direct forms are recognized (`x != null`, `x == null`, `null != x`, and several of them joined with `&&`, as long as one of the terms is a direct null test on `x`); it applies only inside the arm; the else arm of `||` is not narrowed for now. Writing `let x = ...` again inside a narrowed arm is a duplicate declaration.
- **After narrowing**, operations aimed at `T?` such as `??` and `match` may still be written on a name already known to be non-null, with the warning "already known to be non-null"; testing a non-nullable value for null is likewise allowed, with a warning (the result is constant).

### 3.9 Errors and faults

- **The error value is one fixed built-in structure**: a kind name, a message, and the source location where it arose. All functions that may fail share this one error.
- **A value that may fail is written only `T!`**, and there is only one type of error value (distinguished by Kind); when typed errors are needed later, they will be added in the form `T ! E` once `enum` exists. `?` is only the nullable mark. `int?!` is allowed; `int??` and `int!!` are not. The marks are written from the innermost outwards, so `T?!` is fallible(nullable(T)) and `T!?` is nullable(fallible(T)).
- **`a !! b` handles errors**; the right side may be nullable, with the same rules as `??`. `x ?-> f` passes the error on unchanged when `x` is an error, and calls `f` otherwise. On the graph all of them are branches.
- **`throw` is kept, and it means returning an error value**, the same structure as `return`, and it accepts only an error value: `throw error('Kind', 'message')`, with the location filled in by the compiler. `try`/`catch` does not enter the language: it has no stable mapping on the graph. `try`, `catch`, `finally` and `raise` are not reserved words; writing them is an ordinary name or a syntax error.
- **Errors and faults are two different things.** Only operations declared as able to fail (reading a file, parsing, the network and so on) return an error value. Division by zero, out-of-bounds access and recursion that is too deep are faults; they do not become error values, otherwise every division and every subscript would have to handle an error.
- **A fault terminates with a location by default, but can be isolated at a subgraph boundary.** An isolation operator `attempt` is provided: a fault that occurs in the function it wraps becomes an error value at the boundary, to be handled with `!!`. Clean-up is guaranteed before termination: output is flushed and external resources are released.

  <!-- camel-snippet: fragment: a fragment of the fault example; risky is the function of the paragraph above -->

  ```camel
  let a = risky(0)                 // fault: the whole program terminates with a location
  let b = attempt(risky, 0) !! -1  // the fault is isolated, b = -1 (sketch)
  ```

- **An error value that is not handled and reaches the exit of `main`**: printed to standard error, with a non-zero exit code.
- **A `T!` value that is not used in a synchronous block is a compile-time warning**; in an `if` without `else`, a `T!` in the arm that is not used is likewise warned; a `let r = ...` that binds a `T!` while `r` is never used is likewise warned. To discard deliberately, write `let _ = ...`. A `main` without a declared return type that ends with an error value is a special case.
- **A constant expression that is certain to fault** (such as `1 / 0`): the folding pass does not fold it, gives a warning with a location, and leaves it to run time. This is the folding pass's own choice, not a constraint on all passes (see 9.6).
- **Out of bounds**: when the length is known at compile time (especially for types of fixed length), an out-of-bounds `slice` or subscript should be a compile-time error, not silent truncation or something left to run time. A run-time out-of-bounds `slice` is a fault, consistent with an out-of-bounds subscript; `take` and `drop` truncate by definition and are not affected; a `slice` whose start is after its end (`xs->slice<2, 1>`) gives an empty array and is not a fault.
- **Recursion depth**: tail recursion is to be optimized, and programs should as far as possible be written as tail recursion or as loops; for non-tail recursion there is a configurable limit, whose default is 1000 uniformly on all schedulers, counted by the parallel scheduler too.
- For numeric faults see 5.1, for the faults of reductions see 7.7.
- **A timeout is a parameter of the call and returns an error value** (`T!`), not a fault, as in `http::get(url, timeout: 5.0)`.

### 3.10 Loops

- **The `LOOP` node is introduced.** Most modern computation-graph standards have such a node, and interoperating with them needs it. The condition and the body are two control regions in the same graph, sharing one frame with the surrounding computation, with no separate capture table; `for … while` is a third form of the same node; with several states the body returns a tuple, and the value of `LOOP` is also this tuple.
- **A loop is an expression with state.** The state is written in the loop header, the value of the body is the next round's state, and the value of the whole loop is the final state. No statement form is added for "a `var` outside the loop as the state"; to iterate only for effect, use existing means such as `foreach`.
- **The loop header is written in the `do` form**: the state is declared once, several states are separated by commas, and the value of the loop is the tuple they form; what follows `do` is one expression, and `{` is read as after `then` (`{ x: 1 }` is a struct, `{ 5 }` is a block).

  <!-- camel-snippet: fragment: the loop forms; xs, n and ds are the surrounding example's values -->

  ```camel
  let total    = loop acc = 0 for x in xs do acc + x * x
  let (i, acc) = loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)
  let p        = loop p = { x: 0 } for d in ds do { x: p.x + d }
  let found    = loop hit = -1 for x in xs while hit < 0 do if x > 9 then x else hit
  ```

- **No `break` and `continue`.** Finishing early is written as a continue condition in the loop header (like `found` above).
- A loop header must have `for` or `while`; `for … while` tests the condition first in each round and then takes the next element; `for` walks an array, a string (by code point, each element the one-character string `s[i]` gives), a map (in ascending key order, each element a key/value pair) or a tensor (along its first dimension, each element a tensor of one rank less; a rank-0 tensor cannot be walked); the type of the state is the type of the initial value, tensor shapes are not widened automatically, and a type annotation is written when needed.
- **A loop can collect per-round outputs**: besides its final state, a `LOOP` node can have outputs that produce one value per round, collected by round into an array (the scan outputs of ONNX `Loop`). This is a graph-level capability with no source syntax; its first user is automatic differentiation (9.9).
- **Collecting a value per round in source needs no new syntax**: keep an array in the loop state and append each round (`losses->append(l)`; `+` on numeric arrays is elementwise addition, not concatenation); the guaranteed in-place update at count 1 (10.5) avoids copying; the compiler recognises "append one value per round to a state array" as the graph-level collected output, which autodiff and export use as scan outputs.

---

## 4. Syntax and operators

### 4.1 Lexing and statements

- **Angle brackets hug.** The angle brackets of `with` arguments and type arguments must hug the token before them: `f<a>` is `f` with an argument, `f < a` is a comparison. The comparison operator `<` must be preceded by whitespace; `i<n` is an error, and the diagnostic should point out why.
- **A newline ends a statement**, on the idea of JS's automatic semicolons, going by what matches intuition. A line continues when a bracket is unclosed, when the line ends with a token after which something must follow such as a binary operator or a comma, or when the next line starts with an operator that can only be binary (`->`, `.`, `??` and so on); a next line that starts with a token that can begin an expression such as `-`, `+`, `(`, `[` is a new statement. The semicolon can still be used.
- **When a symbol with two identities appears at the beginning of a line, it takes the identity that starts something new**: `@` at the beginning of a line is an annotation, `+` and `-` at the beginning of a line are unary. To write a matrix product or an addition or subtraction on two lines, leave the operator at the end of the previous line.
- **The parser** uses a hand-written lexer and parser, no longer depending on ANTLR, for efficiency and determinism.
- **Comments** are written only with `//` and `/* */`; `#` does not mean a comment; a `#!` on the first line of a file is still ignored as an interpreter line.
- **Documentation comments** are a convention of the language: a comment starting with `///` directly above a declaration is its documentation. Hover and documentation take only `///` comments; an ordinary `//` is not documentation.
- `===` and `!==` are elementwise equality and inequality (4.2).

### 4.2 Operators

- **`^` binds one level tighter than multiplication and division, and associates right to left.** `2 * 3 ^ 2` is 18.
- **Unary minus is always an operator and is never merged into a literal**; its precedence is lower than `^` and higher than `->`. So `-2 ^ 2` and `-x ^ 2` are both -4, and `-2->f` and `-x->f` both negate first and then pass in: a literal and a variable read the same way. `a-1` is still a subtraction. `!` and `~` are like unary minus, with precedence lower than `^` and higher than `->`: `!x->f` negates first and then passes in.
- **`&&` and `||` are reserved operators that cannot be overloaded, translated into branches.** `a && b` is `if a then b else false`, `a || b` is `if a then true else b`: the right side is an arm, evaluated only when needed. Both sides are `bool`.
- **`&` and `|` are ordinary logical operations that evaluate both sides, and can be overloaded.** Use them for a little simple logic when no branch is needed; the element-wise logical operations of tensors, and the ones a user overloads for their own types, are also this group. `a &= b` is `a = a & b`; `&=` and `|=` are logical and and or on booleans; bit operations use functions (`bit_and` and so on).
- **`==` and `!=` compare arrays, tuples and structs by content, element by element.** The two sides must be of the same type, and the element types themselves must be comparable (those containing function values cannot be compared), otherwise it is a compile-time error; float elements follow the rules of floating point (an array containing `nan` is not equal to itself). On the graph it is an ordinary operator node.
- **`===` and `!==` are elementwise equality and inequality** (the author, 2026-10-25): `a === b` applies `==` to the two arrays element by element and gives a `bool` array of the same shape; the shape rules are those of every other elementwise operation. Whole comparison is elementwise comparison reduced to one `bool`: `a == b` means the shapes are equal and `a === b` holds everywhere. So `==` gives a `bool` for every type and every array, numeric arrays included, and its result does not change with whether the element type is written. On scalars `===` is `==`. Overloading `===` gives `!==`.
- **`<`, `>`, `<=`, `>=` compare numeric arrays element by element** and give a `bool` array: an array as a whole has no order, so these operators have no other meaning on arrays to conflict with.
- **A compound assignment `a op= b` is `a = a op b`** (the author, 2026-10-24): whenever `a = a op b` is a legal assignment (`a op b` is defined and its result can be assigned to `a`), `a op= b` is legal, whoever defines the types; otherwise it is an error. So `t += u` on tensors computes a new tensor and rebinds, consistent with numbers; whether it happens in place is the memory plan's decision (the one rule for updating in place), not observable in the semantics.
- **List comprehension** `[e for x in xs if c]` means `filter` plus `map`, that is `xs->filter<c>->map<e>`; there is no order among the elements.
- **Function composition `f..g`** (the author, 2026-10-24): first `f`, then `g`; `(f..g)(x)` is `g(f(x))`, in the direction of the pipeline `x->f->g`; between integers `a..b` is a range: half-open `[a, b)` (like the slice `t[a:b]`), giving `int[]`; it is the former two-argument `range(a, b)`, which is removed; a step is written `range(a, b, step)`; there are no float ranges, ranges do not appear in type positions, and `for i in 0..n` is fused by the compiler without materialising an array. `f..g` is `x => g(f(x))`: its typing, generic instantiation and overloading follow the rules of that lambda; `..` binds tighter than `->`, so `x->f..g` reads `x->(f..g)`. Usable with higher-order functions (`xs->map<normalize..encode>`), rules (`map(map(xs, f), g) => map(xs, f..g)`) and composing passes. The reserved token `?..` is removed.

### 4.3 Operator overloading

- **Users can overload operators in Camel source**, written `func operator &(a: Mask, b: Mask): Mask` (the form is provisional). What can be overloaded are the operators that today fall onto `__x__` functions; `&&` and `||` and constructs such as `??`, `!!` and `->` cannot be overloaded. Names of the form `__x__` are still reserved and users do not write them directly.
- A user's overload takes precedence when it is more specific than the built-in; overloading `==` gives `!=` (`!=` cannot be overloaded by itself); an operator declared in a namespace block belongs to the module.

### 4.4 Functions and parameters

- **Camel distinguishes two kinds of parameters at the language level.** One is the attribute the function is to be configured with (the `with` parameter, i.e. the context parameter), the other is the input the function is to process, that is, what is to flow through the function (the ordinary parameter). In a sense this makes up for Camel's lack of object orientation: the configuration and state that other languages hold in the members of an object are in Camel the context parameters of a function.
- **`with` parameters suit the parameters and weights of a model**: ordinary parameters are data inputs, `with` parameters are weights, together with shapes, switches, hyperparameters and the functions a function calls: whatever "decides which function this is". `logits<m>(x)` matches the mathematical f_θ(x): `logits<m>` is the network with weights `m`, applied to the data `x`. Besides `f<w>(x)`, a call can also be written `x->f<w>`, which suits chained calls and chaining the inputs and outputs of the layers of a model.
- **Parameters may have defaults and arguments may be given by name**, by one rule for ordinary and `with` parameters: `func scale(x: f32[n], by: f32 = 1.0)`, `with <rounds: int = 8, strict: bool = false>`; at the call `scale(x, by: 2.0)`, `f->simplify<rounds: 16>`. A default is a compile-time constant expression, filled into the graph at the call; named arguments come after positional ones; parameters after one with a default must also have defaults. `name: value` is spelled as in a struct literal.
- **`f<w>` is partial application**: `let g = f<w>` gives a function whose context parameters are already set.
- **A `with` parameter is not a generic parameter.** It is a context parameter; do not use it to declare type parameters or compile-time parameters.
- **The applicability condition `when`**: a function signature may be followed by `when condition`, an ordinary compile-time predicate (`is_numeric::<T>()`, `is_function::<F>()`, written in the library with `match type`, and users may write their own). It is evaluated at instantiation; when it does not hold, **this overload does not apply** and overload selection tries the other candidates of the name; when none applies, the error is reported at the call, listing the unmet condition of each candidate. `when` means the same thing on rules ("this rule applies here when..."), on functions ("this overload applies when...") and on `match` cases (`case p when condition =>`); the position decides when it is evaluated. There is no separate `where`.
- **A generic function can appear in any position that expects a function**, including being passed to another generic function (see 5.5).
- **Angle brackets are only for "configuration"**: configuration is whatever "decides which function this is" (the mapping function of `map`, the predicate of `filter`, a model's weights, hyperparameters) and is a `with` parameter, keeping the pipeline style `xs->map<f>`; the data flowing through a function, and the function a transformation transforms (such as the function `grad` differentiates), are ordinary parameters: `grad(loss)`, `with_grad(loss)`. When a function passed as an ordinary argument is known at compile time, the compiler still specialises for it (an optimisation, not semantics). This distinction gives transformations their defaults: `grad` differentiates the weights (`with` parameters), batch mapping maps over the data (ordinary parameters) with weights shared, and sharding splits the data and replicates the weights.
- **A pipeline call may take extra arguments**: `x->f(a, b)` means `f(x, a, b)`; `x->f` and `x->f<w>` are unchanged; in a chain of calls the receiver enters the first call of the name after `->`, so `x->g(1)(2)` means `g(x, 1)(2)`.

---

## 5. The type system
- **A function value has no identity** (the author, 2026-10-24): the value of a function or a region is decided only by what it computes; there is no "which object it is". Two function values cannot be compared for equality and have no address. So function bodies and regions that differ only in parameter names or inputs may be shared by the compiler, and the program cannot observe it.
- **`when` comes before the body it guards** (the author, 2026-10-24): `when` is a condition of applicability and belongs to the signature, not to the defining clauses after a body (`fact`'s `join`/`meet`/`widen`/`narrow`). Functions and rules write it after the signature and before the body; when the signature does not fit on one line, `when` takes a line of its own (the formatter lays it out so):
  <!-- camel-snippet: fragment: shows where when goes, the body is elided -->
  ```camel
  func softmax::<T>(x: T[n, m], axis: int = -1): T[n, m]
      when is_float::<T>()
  {
      ...
  }
  rule add_zero::<T>(x: T) when is_integer::<T>() { x + 0 => x }
  ```
  A `match` guard likewise comes before `=>` (`case p when c => …`).

### 5.1 Basic types and numeric types

- **Numeric types are not converted implicitly.** An integer does not automatically become a float, and no other two numeric types convert either (for parameters, return values and variables alike); write `float(x)` explicitly when needed. The one thing that moves is a **literal**, which takes its type from the context (see the end of this section).
- **A literal with an exponent or a decimal point has `float` as its default type** (it is an `f32` when the context asks for one): with no context, `1e5` is `100000.0`.
- **Faults of conversion**: `int('12abc')` is a fault, the whole string must be one number, and to parse safely use `parse_int`, which returns `int!`; a conversion whose value does not fit the target type is a fault: `int(1e30)`, `int(nan)`, `i32(5000000000)`.
- **Overflow of integer addition, subtraction and multiplication, and the minimum integer divided by -1, keep wrapping around.**
- **Converting a value to its own type is legal and does nothing**: `xs as int[]` (`xs: int[]`) passes, as does `p as A` through an alias.
- There should be no type that is still undetermined at run time: after compilation every value has a determined type (for `any` see 5.10).
- **Diagnostics and the graph use the type names in the source** (`first<int>`, not `first<i64>`).
- **Literals take their type from context** (as Go's untyped constants): in order, the expected type (declaration, parameter, return position, one side of a binary operation or comparison), the type of the other operand of a binary operation, then the default (`int` for integers, `float` for decimals and exponent forms, `c128` for imaginary); operations between literals are computed exactly at compile time, with no intermediate overflow and no rounding before the target type is known; a value that does not fit the target type is a compile-time error, `SemanticDiag::LiteralNotRepresentable` (the message quotes what was written and gives the reason: out of range, would be truncated), located at the position that gave the type. Variables are still never converted implicitly. Example: in `x * 2 + 1` with `x` of type `f32`, `2` and `1` are `f32`; `0.1 + 0.2` is the double nearest to 0.3 and `9223372036854775807 + 1 - 1` is the largest integer; `1 / 2` is 0 by integer division and `1.0 / 2` is 0.5.
- **Complex numbers**: core numeric types `c64` and `c128`, stored compactly in arrays. An imaginary literal is a number immediately followed by the suffix `i`: `4i`, `4.0i`, `1e-3i` (not on hexadecimal or binary literals); `3 + 4i` is `c128`, `let w: c64 = 1 - 2i`. Printed as `3+4i`.
- **"Positions with an expected type" include**: declarations, parameters, return positions (the last expression of a function body, and a `return` inside a branch arm, which takes the expected type of the arm's position, unrelated to the function's return type), the other side of a binary operation, elements of an array literal, fields of a struct literal, `match` patterns (taking the scrutinee's type), and the values of the arms of `if`/`match` (taking the expected type of the whole expression, as in `let x: float = if c then 1 else 2`). Without an expected type, the literals in one array take the widest of "integer < decimal < imaginary" (`[1, 2.0]` is `float[]`). Arithmetic between literals (the unary `~` included) is computed exactly at compile time, and a folded expression that is out of range reports at the whole expression; `&&` and `||` are not folded that way (they are short-circuit branches). A value that does not fit reports `LiteralNotRepresentable`. A value bound by `let` has a fixed type and, like a variable, is never converted implicitly afterwards.
- **Overload choice is exact first, then entering**: candidates whose types match exactly are tried first; only when none matches may literals enter the parameter types. More than one candidate at either step is an ambiguity error; declaration order never breaks the tie. Ordinary calls and partial applications (`f<w>` as a value) follow the same rule.

### 5.2 Named types and structural types

- **A struct with a name and an `enum` are named types**: `type Point = { x: float, y: float }` creates a new type whose identity is the name plus the module that declares it, and which belongs to that module. Tuples, arrays, function types, and `type Name = another existing type` are aliases and are compared by structure (such as `type Pair<T> = (T, T)`).
- **A struct value without a name can be used directly as a named type of the same structure**: `let p: Point = { x: 1.0, y: 2.0 }`, the fields corresponding by name. **Between two named types an explicit conversion is required** (`m as Seconds`), even if the structure is the same; the fields correspond one to one by name. When a "wrapped number" (such as `Meters`) is needed, write a single-field struct; no new syntax is added.
- **Field order**: the canonical order of a named type is its declaration order; for a struct type without a name, the written order is part of the type (a different order is a different type). The leaf order of parameter trees, checkpoints and `tree_paths` all follows the canonical order.
- **Structural types are finite**: a struct or tuple cannot contain itself directly or indirectly; a recursive type can only go through an `enum`. The computation of type functions therefore terminates by the structure of the type, and there is in addition an adjustable expansion limit.
- **A struct may recurse through an indirection**: recursion through an array, a map, a set or an `enum` is allowed (`type Node = { children: Node[] }`); containing itself directly (`type A = { a: A }`) is an error. The empty array literal `[]` takes its type from context (the rule of 5.1).

### 5.3 `enum`

- **Sum types are done as Rust's `enum`**: variants have names, can carry the data of a tuple or a struct, can be generic and can be recursive.

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

- When a `match` on an `enum` lists all variants, `case _` need not be written (the same as for `bool`, `T?`, `T!`). Variants are not distinguished by the type of the value (that would be a run-time type test, already excluded). `|` is only logical or. No new node kind is added on the graph (see 2.1).

### 5.4 Multi-dimensional arrays and dimensions

- **Native multi-dimensional arrays are a core type**: `T[dims...]`, an element type plus a shape, with value semantics. `int[]` (one-dimensional, length known only at run time, appendable) is `int[?]`; `f32[3]`, `f32[n, 784]`, `u8[h, w, 3]`; `int[][]` remains an array of arrays (rows may differ in length). A "tensor" is a multi-dimensional array with a numeric element type. **There is no type name `Tensor`** (the author, 2026-10-24: deleted when the migration completes, with no compatibility kept); the library of tensor operations is called `tensor`, and signatures write `f32[n, 784]` or `func f::<T>(x: T[..]) when is_numeric::<T>()`. Write `f32[m, n]`, not `f32[m][n]`: a multi-dimensional array is one object. Appending applies only to `T[]`.
- **A dimension name bound at its first occurrence in a parameter type, when it is not an existing name** (replacing `constas`); existing names are references; a name appearing only in the return type is an error; a dimension name used only once draws a warning (suggesting `?`). `?` is an unnamed size of any value (unknown at compile time, checked at run time at the call, as with `as`); `..s` is any number of dimensions. Dimensions allow addition and multiplication by constants (linear forms whose equality is decidable); other results are unknown. Dimension names live only in types and may not share a name with a run-time variable; named dimensions create no new instances.
- **An array of unknown shape entering a position that declares a shape**: not let through implicitly; convert explicitly with `as`, checked at the conversion.
- **Shape checking belongs to the core**; elementwise operations, matmul, reductions and the like are library functions whose facts are standard tags on their declarations (6.6). Sparse, quantized, distributed and tabular data are named types defined by libraries, built from core arrays, with common interfaces expressed through generics.
- Details and reasons: [Native multi-dimensional arrays, memory management and interop](design/arrays-memory/README.cn.md).
- **Indexing and slicing**: `t[i]` takes the `i`-th along the first dimension (a row when multi-dimensional); `t[a:b]` slices the first dimension half-open, `a` and `b` optional, with an optional step `t[a:b:s]`; multi-dimensional forms are `t[:, 0]`, `t[a:b, c]`; `t[idx]` with an integer array `idx` takes rows by index, i.e. `take`. A slice is a view sharing the buffer. **No negative indices** (a negative index is a fault); the last element is `last(t)`. Before the H21 indexing syntax lands, the function form of taking rows is `take(t, idx)`: `idx` is an `int[]` or a rank-1 integer tensor, the result's shape is `[len(idx)] + shape(t)[1:]`, repeated indices are kept, and an out-of-range or negative index is a fault; an arbitrary row order is not a view, so the result is a copy. `one_hot(labels, classes)` turns integer class labels into `[len(labels), classes]` f32 rows (before H15 there is no per-element type annotation, so the element type is fixed to f32).
- **Arrays have one semantics, like every other type**: value semantics, indexing, slicing, `map`/`filter`/`sort`, appending and path assignment are the same for every element type; compact storage of plain-data elements (numbers, complex numbers, structs made only of them and so on) in data blocks is implementation (10.5). Differences come only from the element type itself: which operators exist (numeric arrays have elementwise arithmetic because numbers have arithmetic) and whether arrays are differentiable (the tangent of an array is the array of its elements' tangents; when the element has no tangent, neither does the array).
- **Streams `T..`** (the author, 2026-10-24): one `T` followed by an unspecified number of further elements arriving over time, a possibly infinite sequence produced on demand. It reads like `T[..]` (followed by an unspecified number of further dimensions): "`..` is an unspecified number more". A stream is a **value**: only its source (reading a file, receiving from the network, a model handing out its output piece by piece) and its sink (writing out) are effects; the transformations in between (`map`, `filter`, `scan`, `batch`, `zip`) are pure functions. `batch(s, k)` gives `T[k]..`, the batch size in the type.
- **A stream is consumed once** (the author, 2026-10-24): on every execution path a stream value has at most one consumer (the arms of a branch each consuming it count once); this is checked on the graph, and a second use is a compile error. To give it to several consumers, write the primitive `tee(s, n)`, which gives two streams, each seeing the whole same sequence of elements; `n` is only a buffering hint for the executor: in the semantics the buffer is unbounded, and an executor may hold back the consumer that is ahead or enlarge the buffer, without a fault; a build-up of memory is reported by diagnostics. Sharing has to be written, so the place where elements may be kept is visible in the code (precedents: a Java Stream and a Rust iterator are consumed once). A read from the source happens once.
- **Streams are not merged and cannot be captured**: stream-typed nodes take no part in common-subexpression merging or in merging during equality saturation (two equal stream expressions are two streams); passes must preserve linearity, and the seal check verifies it. A lambda cannot capture a stream (it may be called any number of times); pass the stream as an argument.
- **An element is computed if and only if a consumer pulls it** ("runs if used" at the granularity of elements, 3.1); backpressure follows from this.
- **Generators**: a function whose body writes `yield` produces a stream, and its return type must be written as a stream type in the function head (`func pairs(n: int): (int, int).. { … yield (i, j) … }`); a simple generator can use the library function `unfold(initial state, step)`.
- **Two ways of scheduling, one meaning**: an executor may run a graph that involves streams (consuming or producing) element by element, the whole graph each time, or keep the graph resident with elements flowing through it as a pipeline; the results are the same, provided the order of elements on each edge is kept and a node computes only from the inputs that have arrived (the determinacy condition of Kahn process networks). Operators that break determinacy, such as merging several streams in arrival order or windows by time, say so in their declarations.

### 5.5 Generics

- **A generic function is instantiated by the types of its arguments.** A generic function is a subgraph template; each call with a set of concrete types instantiates a subgraph whose types are all concrete, and the body is checked at that time. Only concrete types are on the graph, and the runtime needs no type information.
- **Type parameters are declared at the head of the function**: `func first::<T>(xs: T[]): T`. A function with `::<…>` after its name is generic, one without is not, so whether a function is generic is visible at its head. The declaration is spelled as an explicit type argument at a call (5.9, `first::<int>(xs)`); at a call it is usually inferred from the arguments. A name declared at the head takes precedence over a type of the same name inside the function. Rules (`rule`) and facts (`fact`) declare type parameters after their name in the same way. There is no `typeas`.

  ```camel
  func first::<T>(xs: T[]): T { return xs[0] }

  with <key: (x: T) => K>
  func keys::<T, K>(xs: T[]): K[] { return xs->map<key> }
  ```
- **Dimension names still bind in place in shapes** (5.4): a dimension name is part of the shape notation (as n×k in mathematics) and is visible where it is written; it may also be declared at the head (`func dense::<n, k>(…)`), without being required.

- **A generic function can appear in any position that expects a function**, including being passed to another generic function. The compiler defers its instantiation until the type variables can be determined from the context; if they cannot be determined it is a compile-time error that points out which variable cannot be fixed. The deferral happens only at compile time, and only concrete instances are on the graph.
- **A generic function can omit its return type**, which is inferred from the body after each instantiation; an ordinary function must write it; the editor's hover shows the type inferred at each call site.
- **The text of standard-library signatures uses type variables, not `any`** (such as `print<T>(x: T)`). Camel functions with a variable number of parameters of different types are not done for now.
- **Operations that expand by type are written with generics, not built into the compiler.** Operations like `tree_map`, "whose shape is decided by the type and which expand at compile time", will finally be written in the library as Camel generic functions; for this, generics must be able to recurse over the fields of structs and tuples at compile time (the design is to be discussed separately). Until then no more such built-ins are added, and no transitional mechanism is made. The kernel of a native operator does not read the static type of the call at run time; whatever must vary with the type is expanded at compile time.
- `macro` has nothing to do with type computation; do not build type functions on top of `macro`.
- **Writing expansion by structure** (design A, see [Generics over structure](design/generic-structure/README.cn.md)): `match type T { case struct => …; case tuple => …; case #leaf => …; case _ => … }` picks a branch by the shape of the type at instantiation; a case may also write a type name (`case int`, `case MyStruct`, `case T`), which picks a branch by the type's **identity**, exactly; `static for n, F in fields T => …` unrolls over the fields at instantiation (the position says what each binding is, so no `constas`; write `n, F, tags` when the field's tags are needed); compile-time field access `t.[n]`; comprehensions `{ … => [n]: e }` and `( … => e )`; `static loop acc = init for n, F in fields T do …` is a fold unrolled at compile time; variadic `..ts` with spread calls `f(t, ..ts)`, the arity fixed at compile time and possibly 0.
- Instances containing `match type` or `static for` are **inlined automatically** into their callers; an expansion failure reports an **instantiation chain** with the static bindings (the field names along the chain form the path); libraries may write `static error "…"`; the instantiation depth limit is adjustable; values produced by the expansion such as paths are run-time values (folded to constants), not compile-time parameters.

### 5.6 Type functions and primitives

- **Type functions are written with dedicated type expressions**, called with angle brackets in type positions. A type is always written after `:`, so these angle brackets are not ambiguous with the angle brackets of `with` parameters. A type declaration with parameters is a type function.

  <!-- camel-snippet: fragment: a signature sketch; swap has no body and is not a loaded operator -->

  ```camel
  type Pair<T> = (T, T)                    // sketch
  func swap::<T>(p: Pair<T>): Pair<T>
  ```

- **New primitives are used only for what the main language cannot express**, and appear as marks, fixed in number: `::<T>` (type parameters declared at the head), `constas n` (a compile-time value parameter), `typeof x` (the static type of an expression), `fields T` (the field table of a type). Whatever can be computed from the primitives (such as `keyof`, which takes the field names) is written in a library, as a type function or an ordinary function, not as a keyword.
- **Ordinary functions and type functions are symmetric in their form.** `func` defines an ordinary function, called with `f(...)`; `type` defines a type function, called with `F<...>`, appearing only in type positions, such as `type Device<T> = match type T { ... }`. A type function has no context parameters (`with`); inside the angle brackets there are only types and compile-time values. What distinguishes them is the result: one whose result is a type is defined with `type`; one whose result is a value (including judgements evaluated at compile time, such as `is_numeric::<T>()`) is defined with `func`.
- **The body of a type function is a type expression**, not a statement block: the braces in `type P = { w: float }` are a struct type and cannot double as a statement block. A more complex computation is split into several type functions, or uses `match type`, which starts with a keyword.
- **Types appear only in type positions.** The type positions are marked out by the syntax: after `:`, after `type Name =`, after `as`, inside `::<...>`, `match type`, after `fields`, and inside the angle brackets of a type function. In the type positions after `match type` and `fields`, a following `(` is always read as the start of a type: `fields ()` is the empty tuple type, and a function type takes one more pair of parentheses (`fields ((x: int) => int)`). There are only two ways for a type to get into value code: `::<T>` (type parameters declared at the head and explicit type arguments at a call) and obtaining by `typeof`. So `f<...>` in a value position is always a context parameter, and `F<...>` in a type position is always a type-function call; the parser need not know what the name refers to.
- **A type function can be an argument of a type function** (higher-order types): writing the name of a type function without angle brackets in a type position refers to that type function itself, such as `MapLeaves<Device, Params>`. Whether a formal parameter is a type or a type function is inferred from how it is used in the body.
- **A field name computed from a literal form gets square brackets**: in a comprehension, the `n` in `[n]: e` is a compile-time value and the `n` in `n: e` is a literal field name; a compile-time field access is written `t.[n]`.

### 5.7 Name lookup in generics

- Names are always looked up lexically at the definition; when an operation is done on a value that depends on a type variable (such as `==` at a leaf, or the overload selection for a callback argument), overloads are in addition looked up in the module where the type of that value is declared. What the caller imported is not looked at.
- To use a different operation, pass the function in explicitly as an argument (such as `tree_eq<my_eq>(a, b)`).
- **Operators of a named type travel with the type**: for an operation on a value of any named type (not only in generics), besides the lexically visible overloads, the **exported** overloads of the module that declares the type are searched; a type reached through an alias or a re-export likewise. Overloads that are not exported are not visible.

### 5.8 Variance

- **Generic types are always invariant**: `Array<int>` cannot be used as `Array<int?>`; implicit conversion (such as entering `T?`) happens only on leaf values and does not penetrate the parameters of a generic type.

### 5.9 Explicit type arguments

Type arguments can be written explicitly at a call, in two layers.

- **The first layer**: a type parameter that appears only in the return type is inferred from the expected type (the annotation of a `let`, the type of a formal parameter, a return position), such as `let cfg: Config! = json::decode_as(text)`; this is an extension of the "generic instantiation deferred until the context can determine it" of 5.5.
- **The second layer**: when it cannot be inferred, write `f::<T>(x)` (the same form as Rust), such as `json::decode_as::<Config>(text)`, `empty::<int>()`. By position, the order is the order in which the type variables first appear in the signature; they can also be written by name, `f::<T = int>`, independent of position.
- `f<w>` still means only `with` parameters and partial application; when both are written it is `f::<T><w>(x)`.
- **Lambda parameter types may come from context**: when a lambda appears where a function type is expected (an argument, a `with` argument, an annotated binding), its unannotated parameter and return types are inferred from that function type, as in `tree_reduce<(count, leaf) => count + numel(leaf)>(model, 0)`; annotations are required when inference fails.

### 5.10 No `any`

- **There is no `any` type**: after compilation every value has a definite type. Several types are accepted through type parameters, values of different types sit side by side in tuples or structs, and JSON of a known shape is read with `decode_as`.
- Camel has no way to test types at run time.

---

## 6. Tags and decorators

There are two mechanisms, very different underneath, so their notations are clearly distinguished.

### 6.1 Decorators `@`: transformation

`@d<a> func f` is `let f = d<a>(f₀)`, where `d` is an ordinary function and may change behaviour; a type decorator is a type function, and `@D type T = E` is `type T = D<T₀>`; a code block can also be decorated (equivalent to an immediately called function without parameters, its behaviour exactly the same as the undecorated block). One per line, applied from the bottom up in turn (the order is meaningful, it is function composition); a repeated composition is defined as a new decorator function. Function decorators are lower case, type decorators upper case (consistent with type functions).

### 6.2 Tags `#`: compile-time facts

- A tag is attached to a declaration or a node, exists only at compile time, cannot be "called", and produces no node on the graph. **The language itself gives tags no meaning**: the meaning is defined by the library that declares it, implemented by the pass that reads it, and the consequences are the library's responsibility (consistent with section 9); the meaning of standard tags is written in this specification (6.6).
- There are two kinds of fact: properties (without parameters, such as `#leaf`) and relations (with compile-time values as parameters, such as `#vjp<cube_rule>`). Several tags are written `#[a, b<v>]`, and the brackets can be omitted when there is only one. Tag names are lower case.
- **Tags are declared with the keyword `tag`**: `export tag leaf on type`, `export tag fixed on field`, `export tag vjp<rule: F> on func when is_function::<F>()` (a function parameter is a type parameter with a condition, the same spelling as a pass's signature, or a concrete function type `(x: T) => R` when the signature is fixed; a keyword is never a type name, and there is no type marker for "any function"), `export tag reassociate on nodes`. `on` lists the positions where it can be attached: `func`, `type`, `field`, function parameters, and `nodes` (the nodes written lexically in the tagged function or code block, including branch arms, loop bodies and lambdas, but not the functions it calls); variants and modules may be added later. An alias group `tag finite_math = [no_nans, no_infs, no_signed_zeros, flush_subnormals]` expands into the facts in it. Attaching to a position that is not listed is a compile error.
- **Facts accumulate over regions**: a node has all the facts of all the tagged regions it is in; for facts of the same name with parameters the inner one prevails. No merge rule is needed.
- **A tag belongs to the declaration** (the name), regardless of how a decorator wraps its implementation. A tag can be attached only at the declaration, and cannot be added afterwards to a declaration elsewhere (whether scheduling is an exception is to be discussed separately, see section 15).

### 6.3 Reading tags

- A C++ module reads them through the registration and query interface; in compile-time code a tag is a compile-time value, `match type T { case #leaf => … }`, and `fields T` gives the tags of each field at the same time.
- Tags share the namespace with other names, are imported in the same way, and can be written qualified, `#[tiles::kernel]`.
- Whether a requirement-like tag (such as `#kernel`) is satisfied is checked by the library's pass.

### 6.4 Keywords

Keywords are kept only for modifiers that change semantics (such as `sync`, `export`, `resource`, `tag`). `macro` is the standard tag `#macro` (see section 8), usable without an import. There are no `inner`, `outer`, `atomic`, `shared`: a native implementation is expressed by a declaration without a body in a module, an external implementation is taken on by the module system, "as a whole, not expanded" is expressed by tags such as `#kernel`, and data sharing is replaced by immutable data.

### 6.5 The two kinds of early computation are kept apart

`match type`, `static for` and type functions in generics are always completed at instantiation and are language semantics; the early evaluation of `#macro` is done by the stage `std::macro` of the pipeline (section 8).

### 6.6 Standard tags

- **Numeric permissions are tags**: `#[reassociate, contract]`, with no separate `numeric` wrapper and `enum`; `finite_math` is an alias group (section 7).
- **`macro` is the standard tag `#macro`** (section 8).
- **All facts about operators are standard tags**: purity, faults, resources, numeric classification, index mapping (which elements of the input each element of the output is computed from) and so on are all standard tags on the operator declaration, whose meaning is written in this specification; C++ operators declare them through the same tag registration interface, and for functions written in Camel they are inferred automatically from the body. Concepts such as "index semantics" and "operator semantics" are not set up separately; the existing `operator_semantics` and `index_semantics` are kept as the internal storage of these standard tags.
- The sharding tag `#shard` and the block-kernel tag `#kernel` are in section 13.

---

## 7. The numeric contract

### 7.1 The canonical algorithm

- **The baseline is the canonical algorithm, not the implementation of some machine**: addition, subtraction, multiplication, division and square root are each correctly rounded per IEEE at every step, strictly in the order the program is written, not contracted into fused multiply-add. When computing by the canonical algorithm (`#strict`, safe mode, see 7.4), the results of all back ends and all devices are bit-for-bit identical.
- **The canonical algorithm of matrix multiplication is a chain of fused multiply-adds in ascending k** (FMA is itself an IEEE-specified single correctly rounded operation, and so is likewise bit-reproducible); this is the canonical algorithm of matrix multiplication itself, not a relaxation of "not contracted into fused multiply-add".
- **The definition graph is the specification**: a fused or hand-written implementation must agree with the definition graph of the operator, faults included. A broadcast whose dimension may be 1 gives no index mapping before the shape is determined.
- **Transcendental functions** (`exp`, `log`, `sin` and so on) are not required to be bit-identical across devices, and only an explicitly written error bound (in ulp) is promised, declared one by one; the goal of the standard library is that `exp` and `log` do not exceed 2 ulp; special values such as NaN, infinity and zero must be exact.

### 7.2 Permissions

- The names of the permissions: `reassociate` (changing the order or association of a reduction), `contract` (contracting into fused multiply-add), `approx_math` (approximate math functions), `change_precision` (lowering the precision of inputs or accumulation; raising the accumulation precision also belongs here), `finite_math` (extra assumptions about NaN, infinity, negative zero and subnormals; the combined spelling of `no_nans`, `no_infs`, `no_signed_zeros` and `flush_subnormals`), `nondeterministic` (allowing practices whose result varies with scheduling, such as floating-point atomic add or a split-K merged with atomic operations).
- **No permission implies another.** A module can only declare what an operator itself does, and cannot widen the permissions on the caller's behalf.
- **`unordered_reduce`** grants only "the merge order of this reduction may change", and nothing else.
- **Exact operators may declare that they are affected by the four assumptions of `finite_math`**; rounding permissions still do not apply to them.
- **`refine_accuracy`** (rule language v2, 8.10): allows the compiler to replace one writing by another whose error is no larger (`log(1 + x)` → `log1p(x)`, softmax with the maximum subtracted). "No larger" means: on every input, the right side's error does not exceed the left side's error plus the bound the rule declares (in ulps, 2 by default), and it is strictly better somewhere; not allowed by default, granted by a tag.

### 7.3 Defaults: relaxed by default, tightened by declaration

| Permission | Default | Note |
|---|---|---|
| `reassociate` | **allowed** | Changing the order and grouping of additions: the premise of parallel reductions and blocked matrix multiplication |
| `contract` | **allowed** | Merging a multiply and an add into one FMA |
| `approx_math` | not allowed; granted by a tag | Approximate transcendental functions |
| `change_precision` | not allowed; granted by a tag | Computing at lower precision (TF32, BF16 and so on) |
| `finite_math` | not allowed; granted by a tag | Assuming there is no NaN, infinity, negative zero or subnormal |
| `nondeterministic` | not allowed; granted by a tag | Allowing practices whose result varies with scheduling |

- **Permissions govern what the compiler changes, not what the user wrote.** A type or conversion the user writes (`bf16(x)`, `f32(y)`, a tensor `cast`) is the precision asked for and needs no permission; `change_precision` governs the compiler computing below the written precision (a f32 matrix product done in BF16, a narrower accumulator). `bf16`/`f16` are floating types on a par with `f32`: they have literals, scalar arithmetic, comparisons and conversions, and arithmetic runs in f32 and rounds once. A reduction over a floating array accumulates in f64 and rounds once, returning the element type (as tensor reductions do).

- The default relaxation applies to element-wise tensor operations, the same as to scalars.
- Algebraic optimization does not automatically assume there is no NaN, infinity, negative zero or subnormal; a rule must still preserve the behaviour of these special values, or check the relevant explicit permission.
- An f32 matrix multiplication implemented by a library on the CPU is available by default just as on the GPU, on the condition that the result is reproducible (7.6; in safe mode the canonical algorithm is used); rounding the multiply and the add separately counts as a change of the accumulation manner and falls under what is allowed by default; BF16 computation needs only `change_precision`.
- Lowering the precision (such as computing an f32 matrix multiplication in bf16 or TF32), approximate math functions and assumptions about special values need an explicit permission.

### 7.4 Tightening

- The tag `#strict` makes the marked function or code block compute bit by bit by the canonical algorithm, using no permission (the default permissions included).
- For the whole program, use the safe mode of the pipeline parameter: `std::opt safe_fp=true` and the parameter of the same name of each back end (a back end turns off the default relaxation and computes bit by bit by the canonical algorithm).
- **`std::opt::algebra` is an independent general algebraic optimization pass, integrated by default in `std::opt`.** `safe_fp=true` restricts the algebra stage to strictly equivalent floating-point transformations, including provably equivalent coefficient doubling; even if a node has wider permissions, this stage does not use them to change the rounding path. Constant folding still follows the canonical algorithm, and integers still wrap by their bit width. This is a policy choice of algebraic optimization, not the algorithm of the run-time operators or a global floating-point switch of the whole pipeline; rewrites already made by earlier passes are not rolled back.

### 7.5 Scope

- **A permission written on a piece of code takes effect only on that piece of code**: a called function does not inherit it automatically; when inlining or expanding a definition graph, the permission is kept with the node and saved with the cache.
- A permission is written on a function or a code block (as a tag, 6.2); branch arms, loop bodies and lambdas lexically nested in it count as the same piece of code. There is no module-level default permission; the entry point of a back end (such as `tiles.run`) does not accept permissions.
- An algebraic optimization pass can choose the numeric policy through its own parameter (7.4); this does not constitute a node permission propagated to later passes or called functions.
- For details see `docs/design/op-semantics`.

### 7.6 Reproducibility

- Two runs on the same device with the same configuration give bit-for-bit identical results; practices whose result varies with scheduling (such as floating-point atomic add) must not be used, unless `nondeterministic` is granted. The same holds for two runs on the same device, with the same number of cards and the same version, including parallel reductions on the GPU, matrix multiplication implemented by a library, and the summation of gradients across cards in multi-card data parallelism.
- The canonical algorithm on the CPU remains the reference answer: tests compare bit by bit in strict mode, and by error bound in default mode.
- Bit-identical results across different devices are not the default; use strict mode.
- The order of summation across cards is as above.

### 7.7 Special values and reductions

- **`max` and `min` use IEEE 754-2019 `maximum` and `minimum`**: NaN propagates, and -0 is below +0. They are associative and commutative, so a reduction for the maximum or minimum can be reordered without a permission. **`relu` is defined as `maximum(x, +0)`**. The maximum in max-pooling and `log_softmax` follows this rule.
- **The value of NaN**: the NaN produced by `max`, `min` and `relu` is always the standard quiet NaN (printed as `nan`). NaNs produced by ordinary arithmetic are not constrained by this.
- **`argmax` and `argmin`** compare by IEEE 754-2019 `maximum` and `minimum`: with a NaN they return the position of the first NaN; on ties they return the position of the first extreme value, with +0 greater than -0.
- **Reductions**: a reduction over an empty range is a fault, even if the result itself has no elements. The sum of an int64 tensor is accumulated exactly in int64 and wraps on overflow, consistent with integer arithmetic.
- There is only one rule for converting a float to text: the shortest one that reads back to the same number. `string`, `println` and `format` agree.
- **The text of a NaN has no sign**: every NaN is written `nan` (`NAN` when a format specification asks for upper case), whatever its sign bit, with or without a format specification, on every back end.

---

## 8. `macro` and staging

- `macro` is a native mark provided by the language core (the standard tag `#macro`, usable without an import) that helps with dynamic graph rewriting: a function marked `#macro` can be evaluated early by the `std::macro` pass, computing static values and forming a new static graph.
- **`macro` itself defines no stage and is not required.** When there is no `std::macro` pass in the pipeline, a macro function is just an ordinary function. `std::macro` is a stage of the pipeline, not compile time.
- A value is either a compile-time constant or the return value of a macro; only these two can be processed by a macro.
- When a macro function body has effects, it is treated like any other normal function: the effects happen at the time of evaluation. This is expected behaviour and cannot be avoided from a practical viewpoint.
- **Failure of macro evaluation**: treated as a run-time fault, like a fault during the run of any pass.
- Loops in `std::macro` run at most 1 million rounds (adjustable, like the recursion limit).
- The two kinds of early computation are kept apart (6.5).

Design motivation: the dynamism of the Python language itself determines its flexibility, and in turn brings the trouble of extracting a static graph. Camel wants a static graph and complete type information, to pursue the best room for optimization. The need to construct graphs dynamically is met by the macro system: with a pipeline of multi-stage graph rewriting, first construct a static graph dynamically, then optimize the static graph aggressively, and finally execute it or translate it into another format.

---

## 9. Passes, pipelines and graph rewriting

### 9.1 Passes and stages

- The whole run is modelled as multi-stage graph rewriting. The stages are defined by the sequence of passes the user composes on the command line, for example `camel xxx.cml std::macro std::opt ...`.
- **A pass takes a graph and gives a graph.** The only difference is effects: a pass that prints, exports or executes gives the empty graph after its I/O and the pipeline ends there; it is the same kind of thing as any other pass, with no separate "consuming pass". **A pass is a function transformation**: graphs and nodes are immutable values, and a pass is `(f: F) => F!`, taking a function and returning a function of the same type, of the same kind as `with_grad`. The whole program is its root function, and a pass in the pipeline applies to the root function; applying a pass to one function is an ordinary call (`loss->simplify`), with no wrapping. The program snapshot (`Program`) is an internal representation and does not appear in the interface for writing passes; a rewrite is "a set of changes → `apply` → a new graph or an error value", with no in-place modification; a pattern rule is written as a pair of functions with the same parameters, the graph on the left being the pattern and the one on the right the replacement. The details are to be settled separately.
- The starting point of this design is engineering convenience, not academic elegance: with one body of code, changing the combination of passes does many things — prototype testing, debugging, compiler optimization, export for deployment, translation into other formats and so on — without switching to another language to write similar logic again. This is one of the characteristics that the graph-native position brings.

### 9.2 The explicit pipeline

- **The pipeline is composed explicitly by the user**, and the language core does not restrict the order of passes; a module cannot add passes on import, nor change the pipeline automatically.
- When the user specifies no pass, the default is `std::default` (which points to the node virtual machine; the code prevails).
- **The `std::` prefix is given only to the core's own passes**; a module's passes use the module's own name, and aliases are registered by the module.
- When a pass of a module (such as `onnx::export`) is written on the command line, the compiler loads that module automatically; a pure program need not `import` a module it does not use just to be exported.
- **A library checks its own preconditions**: when a pass it needs is not in the pipeline, a diagnostic is given saying which pass to add.
- Whether the pipeline can be written in the source is not yet decided (section 15).

### 9.3 Pass parameters

- **A pass's settings are its `with` parameters**: named, typed, possibly with defaults (4.4); passes written in C++ and in Camel share one model. In source: `f->std::opt<safe_fp: true>`.
- **On the command line, parameters are `key=value` words after the pass name**: `camel m.cml std::opt safe_fp=true onnx::export out=model.onnx std::fvm`. A word containing `=` is a parameter of the nearest pass before it; the value is parsed by the parameter's declared type (a string takes the text as written), and no quoting is needed.
- **A fixed pipeline is written as a Camel function**: a pass is a function transformation, so a pipeline is function composition (`f->std::macro->std::opt<safe_fp: true>->tiles::compile`), used by name on the command line and shareable in a library; there is no separate pipeline configuration file.
- **A setting that belongs to one pass is that pass's parameter**, not a global command-line option; global options are kept for process-wide matters (collector, memory limits, devices, module search path, diagnostic format). Settings are not taken from environment variables.

### 9.4 The life cycle of tags

- **A tag is handled up by the pass that reads it**: after a pass has used a tag, the tag may no longer exist, and later passes (such as inlining) are not bound by it. The order of passes is arranged by the user.
- **A library's tags may be used up by the pass that reads them**; standard tags that affect results (such as numeric permissions, 7.5) stay on the node and do not disappear with the pass.
- **A tag can declare the pass that reads it**; when this pass is not in the pipeline, the compiler warns "the tag has no effect", so that whether a tag takes effect is observable.
- **Inlining keeps the origin**: the inliner records the identity of the inlined function and its declared tags on the inline origin of the nodes; if a pass that reads a tag finds that the tag appears only on nodes that have already been inlined, it gives the warning "the tag did not take effect because the function was inlined before this pass" and says what order should be adjusted.

### 9.5 Source-level rules constrain only graph construction

- **The language core does not restrict the order of passes.** But with a different order the result of execution may indeed differ: passes do not guarantee to produce the same graph, they modify the graph, and they may have effects. **"Behaviour unchanged under any pass sequence" is not a property of Camel; do not implement or check it as a convention.**
- **Source-level restrictions take effect only in the step "source translated into a graph".** Rules such as `var`/`let`, `sync` and resources constrain how the compiler constructs the graph; once the graph is obtained, these rules are no longer constraints on later graph-rewriting stages. What constraints a pass must observe is that pass's own convention; whether the semantic behaviour of the graph changes after a rewrite is the responsibility of the pass's developers and users respectively. Camel is responsible only for compiling to a graph, and puts no restriction on passes.
- The optimization passes in the standard library must not turn a program wrong. This is an ordinary software quality requirement, guaranteed by tests.

### 9.6 Optimization and the behaviour of passes

- **"The program behaves the same whether or not optimization is on" is not a design principle of Camel**, and the language core cannot enforce such consistency. What a pass keeps and what it deletes is decided by the pass itself. For example, a pure computation in a synchronous block whose result is not used but which faults (`let q = 1 / z`): without passes it faults, and a simplification pass may delete it so the program goes on. This is not a defect.
- **Optimization does not change the identity of operators on the graph.** Fusion and switching to a library implementation (BLAS and so on) are implementation choices recorded in node attributes, and the original operator stays on the graph; a new operator may appear only when it has its own semantic declaration and definition graph.

### 9.7 Graph rewriting and effects

- **How effect tracking is preserved beneath the passes**: effects are a summary on the graph (whether the exit of the graph depends on effects, which captured variables it writes), recomputed from the nodes and verified at every commit; a pass cannot commit a graph whose summary does not match its content. Whether a pass is right to delete or move an effect remains the pass's own responsibility (9.5).
- **A rewrite does not change which effects run** (the author, 2026-10-23): the program's meaning is defined on the source program, and optimisation must preserve it. If a rule's right-hand side drops part of what its left-hand side matched, the rule applies only when the dropped part is **pure and cannot fault** (the same predicate as 3.4: `x * 0 => 0` does not apply to `(a / b) * 0` either, or the fault would vanish); in equality saturation, extraction likewise may not choose a writing that drops a part that may fault; the rule engine checks this once for every rule, so rule authors need not. Example: `x * 0 => 0` does not apply to `println(5) * 0`: in the source the multiplication uses `println(5)`, so it runs, and it must still run after the rewrite. Simplification of pure computation is unaffected. Likewise, when a rule moves part of a function body, loop body or arm outside it, the moved part must be pure and unable to fault (otherwise an empty array or zero rounds would gain a fault); the rule engine checks this too.

### 9.8 Entry and export

- **All passes share `main` as the entry.** Export has two cases, depending on what the program expresses:
  - **The program expresses only pure computational intent** (this is the original face of a Camel program): the program itself should not care how or what to export internally. Then a combination of passes suffices to export, for example `camel x.cml std::macro std::opt onnx::export`, and what is exported is the computation that `main` expresses.
  - **The program also serves intents beyond computational intent** (for example timing, running a benchmark): then exporting with a combination of passes is no longer suitable. It is better to go all the way: the program calls an operator internally (such as `onnx.export_model`) to export one of its own subgraphs.
- **`main` can take parameters.** `main` is a subgraph like any other function, and its parameters are the inputs of this graph: when exporting, the inputs of the model (the shape taken from the tensor type with a shape); at run time, supplied from outside (the command line; when missing, a clear error with a location is given, and zero or random numbers are not substituted); when taking gradients, the quantities that can be differentiated with respect to. A `main` without parameters exports a constant model with no inputs, which is allowed. Returning a single value is one output; returning a tuple or a struct is expanded into several outputs.
- **Only the return value of a `main` with an explicitly declared return type counts as the result of the program.** The return value of a pure `main` is output through the existing `-o` / `--output <file>` on the command line, standard output by default; no dedicated option is set up.
- Export (such as ONNX) is a translation pass, likewise built on the pass mechanism. **An export pass need not do many checks**: translate what can be translated, and report an error when it cannot; no academic fastidiousness is pursued. An export pass may do the necessary adaptive simplification, but should not become the main force; the main evaluation and simplification should be done before it by `std::macro` and the optimization passes.

### 9.9 Automatic differentiation

- **Automatic differentiation is a library built on the pass mechanism, not the language core.** It works on the graph it is given and does not require `std::macro` earlier in the pipeline; a structure it cannot handle (an unexpanded compile-time construct, say) is refused at its location with a diagnostic. These are the library's preconditions, not constraints of the language core.
- For a function being differentiated and `var`, see 3.5.
- **Differentiating through loops**: the reverse pass needs the intermediate values (residuals) of every forward round. The forward loop keeps each round's residuals as collected outputs (values, not closures), with a manifest saying which slot holds which residual; the reverse pass is a loop over the rounds in reverse order. O(1) per round, O(n) in total. The forward loop's round count may depend on its condition.
- **Residuals are not bounded**: as with any growing array, running out of memory is reported as resource exhaustion with a location; no separate round limit. A recomputation tag (checkpointing) may later bring memory down to O(√n); the residual design does not preclude it.
- **The tangent of an array is the array of its elements' tangents**: for `f32[n]` it is `f32[n]`, for `{x: f32, y: f32}[]` an array of the same struct; when the element has no tangent (such as `string`), neither does the array (5.4). A loop that iterates over or captures an array that depends on a differentiated parameter **and has no tangent** is refused, with a located diagnostic naming the `loop` and the array; a captured numeric array accumulates its gradient as usual, and the reverse of reading by index is a scatter-add. No **continuous** value that depends on a differentiated parameter but has no gradient channel may drop its gradient silently; it is refused with a location. **Discrete values** (integers, booleans) have a zero derivative by definition: an integer or boolean computed from a parameter and converted back to a float has a zero gradient, not a refusal (as in JAX).
- **Effects in a loop's body or condition follow the existing rule**: effects run in the forward pass only and the reverse pass is pure; the check "reading a `var` of a differentiable type is refused" extends to the loop's condition and body, the diagnostic naming the `loop`.
- **ONNX export of gradients with loops is a separate item**: residuals are values and map to `Loop` scan outputs and a reverse `Loop` (with ONNX sequence types for ragged nested loops when needed).
- **Differentiation is with respect to `with` parameters**: `grad(f)` (the gradient only) and `with_grad(f)` (the value together with the gradient, `(value, gradient)`) differentiate with respect to the **first `with` parameter** of `f` (the model's weights), holding the other `with` parameters (such as hyperparameters) constant; e.g. `with_grad(loss)<m>(x, y)` gives `(loss, a gradient of the same type as m)`. The recommended style writes the transformation and the call in two steps: `let loss_with_grad = with_grad(loss)`, then inside the loop `let (l, g) = loss_with_grad<m>(x, y)`. To differentiate with respect to data (adversarial examples, say), make it a `with` parameter through a lambda. `grad` refuses at compile time, saying which condition failed, when the first `with` parameter is a `var` or has no tangent, or when the function has no `with` parameter.

### 9.10 Schedulers, back ends and releases

- **The parallel scheduler hands a subgraph with nothing to parallelize to the bytecode interpreter**, and counts the recursion limit by the same rule: when `--max-recursion-depth` is raised, deep non-tail recursion runs to the end on `std::prl` just as on `std::fvm`.
- The instruction-set baseline of the release package is `x86-64-v3` (AVX2 and FMA; raised from `x86-64-v2` on 2026-10-21, see the ruling log).

---

## 10. Modules and libraries

### 10.1 Modules and the graph

- **Code at the top level of a module does not execute by default**: Camel is not an imperative scripting language. Top-level statements are evaluated by dependency like anywhere else.
- **A module can export values.** A module itself is a `__root__` graph; the whole program has just one `__root__`, on which the top-level nodes of all modules sit, and functions take values through capture ports; a capture is bound by "module + name", and is not looked up by name at the call site. A top-level `let` of a module is a node on the root and is evaluated when depended on. This way there is no node reference across graphs. The specific way of merging comes first as a design note. A C++ module can also export values (`math::pi`).
- **Top-level values are evaluated before `main` starts, on the basis of static dependencies**: the top-level values used by the functions statically reachable from `main` along the call chain are all evaluated, and whether the arm using one is reached at run time does not matter.
- **The top-level `let` and `var` of a module are independent of writing order and look only at dependencies.** `let area = pi * r * r` can be written before `let pi`; a cycle among top-level values is a compile-time error. A local `let` in a function body is still declared before use.
- **Modules can import each other.** For this, loading has two phases: first collect the declarations of all modules, then build the function bodies. A module importing itself is allowed and has no effect; types forming a cycle across modules give a diagnostic of the cycle that lists it.
- **A `var` cannot be exported**; export the functions that read and write it. The `main` in an imported module is an ordinary function.

### 10.2 Namespaces and names

- **Inside a namespace block, names of the same block and of enclosing blocks can be used without a prefix.** A namespace block can contain values: `namespace Config { export let limit = 10 }`, used as `Config::limit`; it differs from a top-level `let` of a module only in the path within the name. Reopening the same namespace block does not carry the `using` of the earlier block to the later one.
- **`using namespace`**: it can be used on a namespace of this module; it takes effect only in the block where it is written (written in a function body it affects only that function); when two `using`s bring in names that are the same in name and signature and cannot be told apart, an error is reported at the use.
- **There is only one rule for name priority**: local > this module (the enclosing namespace, outer namespaces, module top level) > imported by name > brought in by `using namespace` > built-in. Take the first layer that has the name; functions of the same name in the same layer form an overload set, and the same signature is an error; layers are not merged across. Operator overloads are an exception, merged across layers.
- **`import a from m` introduces the name `a` from `m`**, equivalent to `import { a } from m`, consistent with the language documentation; to give a module an alias, write `import m as x`. **Imports can be renamed**: `import { find as str_find } from str`. A conflict between two imported names is a compile-time error; one must not be silently taken. `import { … } from m::N` is allowed. `use` is not implemented, and this reserved word is retired.
- **A module's `import` looks in the module's own directory first**, then in the module roots (the directories given by `-I`, in the order given), then in the standard library; the entry program and library modules follow the same rule, so a library's imports resolve to the same place whoever imports it.
- **A module can qualify its own things with its own name**: in `module shapes`, writing `shapes::area` is the same as writing `area`.
- **Diagnostics, the debugger, the language server and graph names always show the full name** `module::path::name`, omitting the module name when it causes no ambiguity. When a name is ambiguous, the qualified candidates are all listed, whatever aliases the imports gave the modules.

### 10.3 The module cache

- **A compiled module can be cached** (`.cmc`, cached module object), so that a module need not be recompiled. The cache of each module is one graph rooted at that module; it is for convenience and speed and need not be identical to the final result of an actual compilation. **The cache is not implemented**: this section fixes its form and invalidation conditions; whether to implement it is still open.
- The cache is put in the `__cmc__/` directory beside the source file; off by default at first; a module that hits the cache does not replay compile-time warnings. The importer's cache is invalidated when any of the following changes: the signature of an exported function (including the inferred return type), the module-level values it uses indirectly, its effects, the body of a generic function. `--verify-ops` still reads the source, not the cache.

### 10.4 The place of native C++ modules

- **Semantic atoms and implementation atoms are separate.** A function's **semantics** comes from one of two sources, a primitive operator or a definition written in Camel; its **execution** may in addition have a native kernel. With the two separated, the library need not choose between expressiveness and performance.
- **Primitive operators (body-less C++ declarations) only in three cases**: dealing with the outside world (I/O, the operating system, foreign libraries, devices); kernels that need memory and layout control the language does not expose (matrix multiplication, convolution, `take` and scatter, sorting); scalar mathematical functions. The primitive set is **minimal and orthogonal**: what other primitives compose cheaply is not a primitive. Every primitive declares its semantic facts (purity, resources, index maps, laws, derivative).
- **Everything else is defined in Camel** (the default): softmax, normalisation, losses, optimisers, `tree_*`, statistics, the array and string utilities. The compiler sees through them, so fusion, differentiation, export and rules apply without a derivative or export rule written for each.
- **Common hot definitions carry a native kernel**: the Camel body remains the semantics, and `#impl<f>` names the native kernel `f` that implements it (a private body-less declaration of the same module). **It is not expanded by default**: ordinary passes and executors treat it as one operator, so its compile and run cost is that of a primitive; only passes that need to look inside (differentiation, fusion, export, rules) expand it on demand. A differential test keeps the two in agreement. The common numeric operations (`sum`, `min`, `max`, `all`, `map`/`reduce` over numeric arrays and the like) have a kernel from the start; the others get one when measurement shows the compiler's result clearly slower, and lose it once the compiler catches up.
- **A library is compiled once**: library functions are compiled and optimised once per instance, into the module cache (10.3) and the specialisation cache; a call site pays a call or an inlining, not a re-optimisation of the library body.
- Expressiveness problems exposed in the implementation are discussed one by one, and common needs are absorbed into the language core rather than worked around in the library.

### 10.5 Library boundary

- **The boundary of the standard library**: primitives that must touch the system or are performance-sensitive use C++ operators; composed logic built on the primitives is written in Camel. A library written in Camel is a subgraph, visible to the passes; a C++ operator is an opaque node on the graph.
- **Module interface**: organize a layer of public C++ interface out of the internal headers, promising compatibility by version; the other internal headers carry no promise.
- **The way memory is managed does not enter the semantics of the language**: objects have no observable identity, the timing of reclamation is not observable, and C++ operators reach objects through handles. Each back end may choose the way that suits it; a back end may reclaim little or even no garbage, and neither the timing of reclamation nor whether it happens is observable.
- **Garbage collection**: small native Camel objects can go through generational management (they can move); where external interfaces are involved, native Camel objects are used to manage external pointers, and external memory does not enter the movable heap. A module must not keep a raw pointer to a Camel object across calls. The constraint generational reclamation places on C++ operator authors is acceptable. Order of landing: first audit the modules and set down the handle rules, then enable generations on the interpreting back end and stress-test it, and turn it on for the JIT and the like after stack maps are done. Generational reclamation is on by default once it is available.
- **Things with identity must be listed**: `var` cells, objects modified in place through a `var` parameter, objects wrapped from external libraries (Python objects and so on); other values have no observable identity, and a back end may copy or share them freely. No type word is added in the object header.
- **Memory management has three layers, all implementation**: compile-time planning (data whose lifetimes the graph shows), **data blocks** (counted), and the generationally traced GC heap (all other objects, graphs and snapshots included). A **data block** is a general mechanism of the core memory layer, not specific to arrays: memory that **holds no references**, **has a fixed address** (the GC never moves it) and **is counted**; it may have a foreign owner with a release callback and may live on another device. Its users are arrays of plain-data elements, `Bytes`, strings above a threshold, table columns, memory-mapped files, foreign memory (DLPack, the Python buffer protocol, OpenCV and so on) and device memory. **"Holds no references" is guaranteed by the type system**: plain-data types are numbers, booleans, complex numbers, and structs and tuples made only of them; so blocks cannot form cycles, the GC never scans them, and they can be handed to foreign code without copying (a red line). Block bytes count towards GC triggering and limits. Data under a threshold (256 bytes) is inline in the GC object and becomes a block only when a fixed address is needed (export, views). **When the count is 1, operations that "return a new value" work in place; this is a guaranteed behaviour.** The run-time representation of arrays is compatible with DLPack (data pointer, device, element type, shape, strides, offset), exchanged with PyTorch, NumPy and CUDA libraries without copying. Cross-language cycles formed when Python embeds Camel are not collected in the first phase. Small logic computations must not get slower; checked against the performance baseline. See [Native multi-dimensional arrays, memory management and interop](design/arrays-memory/README.cn.md).

### 10.6 `std::cpp`

- **The goal of `std::cpp` is to generate programs that can be deployed independently of the runtime.** Where a semantically equivalent translation is impossible, it may refer to a Camel runtime module, but should keep that as clean as possible. The generated program must give faults with source locations, observe the recursion limit, and tail calls must not grow the stack. The fault reports it generates are word for word the same as the interpreter's; recursion counting is exact (consistent with all back ends).
- The module interface generated by `std::cpp` uses standard C++ types and does not expose the runtime's internal types.
- The extra empty line of `println<fmt>` and the extra line end of `print<fmt>` are defects of the interpreter, and the interpreter is fixed; `std::cpp::bench` is removed; `map`/`filter` in the generated program are serial at first; the parameters of `main` are given as `--arg name=value`, consistent with the command line.

---

## 11. Standard library conventions

### 11.1 Strings and `Bytes`

- **`string` is guaranteed to be valid UTF-8**, with subscripts and length counted in code points; byte sequences use the built-in type `Bytes`, and the two are converted by explicit encoding and decoding. `string` represents only text; the file interface has two sets, text and binary. Operators that read and write by code point are provided in addition.
- Strings also have `contains`, written the same as for arrays: `'hello'->contains<'ell'>`.

### 11.2 `Json`

- `json.decode` returns the self-describing `Json` type, and `decode_as<T>` is provided (for explicit type arguments see 5.9).
- For bad JSON text, `decode` returns the error value of `Json!`, not a fault.
- The accessors of `Json` accept `Json?` and `Json!`, for chained access (access can be chained even when parsing failed); `as_float` accepts integers; `get<index>` out of range gives `null`.
- **When `Json` accessors take a `Json!`, the error passes through unchanged**: `decode(text)->get<'a'>` has type `Json!?`, handled with `!!` or `??`. A document that is too deep reports `JsonTooDeep` (distinct from malformed text); the default depth limit is 10000, to become an optional parameter of `decode`.

### 11.3 Collections and sorting

- **Maps and sets are made built-in types.** No new literal is added; the existing array literal is used with an `as` conversion (the same form as `t as Tensor<...>`). Every operation returns a new value; a lookup that finds nothing gives null.

  ```camel
  let m = [('a', 1), ('b', 2)] as Map<string, int>   // sketch
  let v = m['a'] ?? 0
  ```

- Iterating and printing a map is by sorted key; the types that can be keys are integers, booleans, strings, and tuples made of them.
- **`append` returns a new array and does not modify the original**: after `let ys = xs->append<5>`, `xs` is unchanged; to "append to a variable" write `acc = acc->append<5>`, like `x = x + 1`. In-place rewriting is only an implementation-level optimization (when the old value is no longer used it can be appended in place); it does not enter the semantics, and no explicit in-place operator is set up. The other standard-library operators are the same. The result of a pure operation that is not used (such as `xs->append<5>` written as a statement) is a compile-time error, with a hint to rewrite it as a rebinding.
- Returning a new value and modifying in place are not told apart by two names: there is only `sort`, which returns a new array; `xs = xs->sort` sorts in place when the old value is no longer used, the same kind of implementation-level optimization as `acc = acc->append<v>`. Tensors keep explicit in-place operators, arrays have none.
- **An `enum` can be a map key or a set element** when all its field types can be keys (stricter than comparable: floats compare but are not keys); ordered first by the variants' declaration order, then field by field.

### 11.4 Call style

- The value being processed is an ordinary parameter, and the rest are `with` parameters (`'a,b'->split<','>`, `xs->take<3>`); when two parameters have equal standing, both are ordinary parameters (`merge(m1, m2)`). The operations on maps and sets keep only one form accordingly.

### 11.5 Output and formatting

- **The output of a program and the text written by `fs` are the same bytes on all platforms**; `\r\n` is not written on Windows. For the rule of converting floats to text see 7.7.
- **String interpolation uses backticks**: `` `step ${i}: loss ${l:.4f}` ``; `${expression}` interpolates, with an optional `:format` as in `format`; a plain `{` needs no escape; it may span lines; it is shorthand for `'...'->format<...>`, and a format that does not match the type is a compile-time error.

---

## 12. Tools

### 12.1 The debugger

- It provides two views, source lines and graph nodes, and single-stepping likewise comes in two kinds, by line and by node.
- The arms of a branch are by default merged into the frame of the enclosing function, and can be switched to show the real subgraph structure; code that has been inlined is shown in the caller's frame, annotated with which function and which call it came from (see the regions in 3.3 of [Graph store and graph API (second version)](design/graph-store/README.cn.md)).
- Line stepping follows the order of execution, and when a line was not executed because nothing uses its result, an explanation is given. When returning from an arm of a branch to the join, it does not stop a second time on the same line, and the breakpoint on that line is hit only on first entry.

### 12.2 The language server

- Hover and documentation take only `///` comments (4.1); hover shows the inferred resources (3.3) and the type inferred at each call site of a generic function (5.5); namespace members are shown by their full names (10.2).

---

## 13. The AI stack

### 13.1 Scheduling

- **Tendency (not a prohibition)**: Camel source expresses the intent of the computation losslessly; content that depends on the environment, such as where and how to compute (blocking, layout, placement, memory policy), is in principle not written into the source but injected by the context at execution time (pass parameters, schedule files), the format and granularity being decided by the library that reads it. Placement is the exception and is written in the source: `#gpu` on a function, a block or a module declares that the computation runs on the device, and the `tiles::compile` pass reads it (13.4). An operator the device cannot run inside a `#gpu` scope is a compile-time error, not a silent fallback to the host; when no pass reads the label, the compiler gives a warning (9.4). The `@gpu` decorator and the explicit run operators `run` and `run_device` were removed (language-author decision of 2026-10-21, GPU design section 6): the device is selected at run time by `--devices`.
- **For the performance ceiling, writing the schedule directly in the source is allowed**: the previous item is a tendency and does not exclude the expert path. Block-level kernels (`Tile<T,[shape]>` and `#kernel` in section 6 of the Tiles route) write the block shape in the source, under the author's direct control, coexisting with the high-level path decided automatically by Tiles.
- **The granularity of scheduling stops at the function**: for finer granularity, extract that piece into a sub-function and use per-function scheduling. After inlining, the inline origin recorded on the nodes can still map them back to the original function. Auto-tuning records its results by the structural identity of the computation (operators, shapes, types, before-and-after relations), not depending on names. The language provides no mechanism for "naming a step of the computation".
- **Scheduling can also be written in Camel**: the intent is written as a function, and in another module a transformation function (or a decorator) schedules it; it need not rely only on combining passes on the command line.

### 13.2 Low-level computation

- **Low-level computation is also expressed with Camel graphs**, with no separate intermediate representation: physical operations (loading a block, in-block matrix multiplication, synchronization and so on) are operators registered by the back-end library, with semantic declarations and, when available, a reference implementation on the CPU; every step of lowering is a graph-to-graph rewrite, and only the last step prints target code. The graph after each level of lowering can, where possible, still run on the CPU and be checked against the reference answer.

### 13.3 Devices and memory

- `Device<T>` is kept, and for structs and tuples it allocates by field: `Device<{ a: A }>` and `{ a: Device<A> }` are the same type. Moving data is an ordinary operation with effects; it can be written explicitly in code, inserted by a transformation function written in Camel acting on a function value ("a pass is a function"), or done automatically by an optimization pass.
- Asynchrony does not enter the semantics: order is decided by data dependencies and `sync`; streams, events and overlapped execution are implementation matters of the back end.
- The upload, region execution and download of `tiles::compile` are pure computations, and device memory is an unobservable allocation; identical region calls can be merged.

### 13.4 Distribution

- Sharding is written as the tag `#shard<[axis name, ...]>`, which can be attached to function parameters; axis names are abstract, the size of an axis is injected at execution time; the same information can also be written in a schedule file. The `shard` pass of the standard library derives the sharding of each result, inserts communication (ordinary operators with effects) and reports inconsistencies; an operator for which derivation cannot continue (a native operator without a declared index mapping) falls back to gather-then-compute, which is correct but slower. The type system is unchanged. The order of summation across cards follows 7.6.
- **Placement (a tendency, not a red line)**: the user declares where computation runs; GPU is not the default. Usually the source states only the intent (`#gpu`, `#cpu` on a function, block or module) and the compiler derives residency, transfers and synchronisation from the graph; which card and how many is usually run-time configuration (such as `--devices`), so the same code moves between machines unchanged. When needed, the source may name a specific device (pipeline stages, pinning to a card for performance, heterogeneous devices), and run-time configuration may override or complete it. Engineering convenience and the optimisation ceiling come before the tidiness of "no configuration in source".

### 13.5 Training infrastructure

See [Training infrastructure design](design/training/README.cn.md) for details.

- **Parameter trees**: the standard library provides operations such as `tree_map` (which can act on several isomorphic trees at once, the corresponding leaves being passed to the callback as several arguments), `tree_unzip` and `tree_reduce`, acting on values nested from structs and tuples; the leaf order is depth first, in field declaration order; the leaf boundary is always decided by the type of the tree arguments (`tree_map` goes by the first tree, `tree_unzip` by the reference tree given), not by the formal parameter types of the callback; the shape of the tree is decided by the type, and the operation expands at compile time into ordinary nodes leaf by leaf.
- **The paths of a parameter tree** are written `blocks.0.w`, the root being the empty path; names in a checkpoint agree word for word with the result of `tree_paths`; `save_tree` and `load_tree` belong to the `ckpt` module, and `tree_paths` belongs to the Camel-only `tree` library.
- **Stateless random numbers**: besides the existing random numbers with global state, key-determined random numbers are provided (`key`, `fold`, `split`, `normal`, `uniform`, `randint`). They are pure functions, not external effects; the canonical algorithm is Philox4x32-10, results for integers and the uniform distribution are bit-identical across devices, and the normal distribution follows the ulp bound of transcendental functions.

### 13.6 Deployment and data pipelines

- Deployment sets up no separate format: the formats of the existing back ends are enough, and a frozen program can also be exported directly as `.cml` and run.
- Data pipelines need nothing new at the language level; the standard library provides tools for reading and batching.
- **Networking and streams**: modules `net` and `http` (C++ primitives underneath); `Stream<T>` is a standard library type (a handle type): pulled on demand, possibly unbounded, iterable with `for`, with lazy `map`, `filter` and the like, backpressure following from pulling; windows, keyed aggregation, merging and batching come from the library. Tables follow the Arrow C data interface for zero-copy exchange with Pandas, Polars and DuckDB. The long-term direction is a multi-machine runtime with one model for batch and stream processing. See [Networking, concurrency of long effects, and streams](design/io-concurrency-streams/README.cn.md).
- **Tables** `Frame<Row>`: the row type is an ordinary struct, stored by column and compatible with Arrow; nullable columns are `T?`. The column structure may be written by hand, inferred at compile time with `schema_of<'file'>` (frozen into source with `camel schema`), or obtained at run time as a `Table` and converted with `as`. No `collect()` is needed: laziness and query optimisation come from on-demand evaluation and the library's `rule`s, executed at run time as fused, batched streams; result row types are computed by type functions. See [Standard library plan](design/stdlib/README.cn.md) 5.1.

---

## 14. Common misreadings

These are all misreadings that have actually happened; check against them before changing code or documents:

1. Treating "effects not used outside a synchronous block do not execute" as a defect to fix, or changing it into an error. — This is expected behaviour.
2. Still understanding the ordering of effects by the retired `wait` and prefix `sync func`. — They have been replaced by "the same resource is automatically ordered" and the inferred `uses`; the postfix `sync` on a function body (the body is a synchronous block) is kept.
3. Thinking that a function called from a synchronous block has the effects in its own body executed because of that. — A function's behaviour is decided by its own subgraph.
4. Implementing a `var` parameter as pass-by-value copied back on return. — Camel is by reference by default.
5. Implementing `let`'s immutability as run-time memory protection. — It is a compile-time check.
6. Treating effects in the body of a macro function as an exception or an error. — They are handled like those of an ordinary function.
7. Thinking a macro function cannot run without `std::macro`. — Then it is an ordinary function.
8. Treating "behaviour unchanged under any pass sequence" as a convention of the language, and on that basis judging sensitivity to pass order a defect. — Passes may change the graph and have effects.
9. Treating the preconditions of automatic differentiation as constraints of the language core. — They are constraints of the library.
10. Picking one derived characteristic as Camel's "core selling point" (for example "one body of code, many uses"). — The core is graph-native; the rest are natural consequences.
11. Changing the semantics to make a test pass. — First confirm the semantics, then judge whether the test or the implementation is wrong.
12. Reading the order of two marks backwards (taking `T?!` for nullable-over-fallible). — Marks are written from the innermost outwards: `T?!` is fallible(nullable(T)) and `T!?` is nullable(fallible(T)); the parser and the printer agree (found by H115, documents corrected by H123).

---

## 15. Questions not yet decided

When you meet any of the following, ask the language author:

- The concrete content of the type-expression language: the meaning and fate of each of the reserved type operators `&`, `\`, `+`, `-`, `^` and `ident of T`.
- The meaning of `typeof` applied to a constant value, and its use in generics.
- How to handle a type function whose evaluation fails or does not terminate.
- The behaviour when one mutable object is written through references in several places outside a synchronous block.
- Whether a tag can be attached to a declaration elsewhere, apart from the declaration itself (whether scheduling is an exception).
- **After a parallel `map` faults, whether elements with a smaller index that have not yet started still execute** (`parallel-fault-lower-elements`):
  every element of `range(0, 64)->map<bad>` faults, and the worker threads happen to have run the 40th first. The current implementation: only those with an index greater than 40 are no longer started, 0 to 39 are still executed, so what is reported is always the 0th, the same as the linear back end and the same on every run.
  Candidate: after a fault, start no element at all; then "the smallest index" is taken only among the elements that happened to have started, and may differ from run to run.
- **Whether the arguments of built-in operators are entered automatically** (`entering-operator-args`): `xs->append<5>` (`xs: (int?)[]`), `len([])`.
  Current implementation: not entered; the former needs `5 as int?`, and the `[]` of the latter has no element type and gives no error; because the parameter types of built-in operators are computed from the arguments by C++ type rules, and before the call there is no target type.
  Candidate: let the type rules report "the type I accept", making built-in operators consistent with user functions.
- **Declaring the pipeline in the source**: whether to allow the entry file to write out the pipeline explicitly (such as `pipeline std::macro, tiles::compile, std::nvm`), as explicit and visible as on the command line, and committed with the code.
