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
- **Not limited to AI.** What is modelled on the graph is the intent and dependencies of a computation; HPC and simulation workloads fit equally.

The eight lines of `ZEN` outline this position: the first two say the graph is fundamental, the other six are corollaries.

### Language elements and the graph

| Language element | What it is on the graph | Resulting behaviour |
|---|---|---|
| Expression, operator call | Nodes and data edges | Evaluated by dependency; a node nothing depends on does not execute; independent nodes may run out of order or in parallel |
| `sync` block | Control edges between adjacent expressions | Executes in written order inside the block |
| `wait` expression | Control edges between that expression and the related operations before and after it | Locally ordered |
| Function | Subgraph | Its behaviour is decided by the function itself; a call only places the subgraph node in a larger graph and does not change its structure |
| `macro` function | A subgraph that can be evaluated early, its result materialized as a constant of the graph | Builds a static graph dynamically |
| Pass | A program that turns one graph into another (or consumes a graph) | An open pipeline |

---

## 2. Execution model

### 2.1 Evaluation by dependency

By default evaluation is driven by dependencies; there is no fixed execution order.

**A call with side effects that is outside a sync block and whose result is not used does not execute.** This is the natural result of evaluation by dependency. It is legal, expected behaviour, not an error, and needs no error report. A sufficiently smart compiler may give a warning.

### 2.2 `sync` modifying a block

A `sync` written before a block (including the suffix form of a function body, `func f(): T sync { ... }`, and a standalone `sync { ... }`) says the block is a sync block: all expressions in it execute in written order, which can be understood as the compiler inserting control edges between each consecutive pair.

Inside a sync block it does not matter what is called, nor whether it has side effects, because there is already an order. Side effects only need to be considered under the default unordered execution, because they limit what may be reordered.

### 2.3 `sync func` modifying a function

The prefix `sync func f(...)` modifies the function: it says the function has side effects and needs to be synchronized, that is, callers may need to `wait` it.

**Its meaning differs from the suffix `sync`. This is by design; do not equate the two.** Declaring `sync func` does not mean the function body is a sync block. To have the body execute in order, write `sync func f(): int sync { ... }`.

### 2.4 A function's behaviour is decided by the function itself

A function compiles to its own subgraph. A call from outside only arranges this subgraph node inside a larger graph; it does not change the subgraph's own structure.

```camel
func log(x: int): int {
    println(x)          // result unused, body is not a sync block: not executed
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    return b            // returns 3, prints nothing
}
```

The body of `main` is a sync block, and the two `log` calls execute in order. But `println` belongs to the body of `log`, which is not a sync block, so it does not execute. To print, `log` should be declared `func log(x: int): int sync { ... }`.

### 2.5 References, `var` and `let`

- Everything in Camel is a reference by default. A closure's capture of a variable is a reference too.
- `var` marks a mutable object, `let` an immutable one.
- `let` is strictly unmodifiable: an operation that modifies a variable is itself `sync` and must declare explicitly which parameter is `var`; a `let` variable may not be passed to a `var` parameter, so modification is impossible in principle. This is a **compile-level check**, not a lock on memory.
- Passing a `let` object to a `var` parameter is a compile-time error.

The reason for this design is `wait` (below): for a function whose input parameters are mutable, the compiler automatically tracks its outward side effects and automatically inserts control edges.

### 2.6 `wait`

The scope of `wait` is the **one expression** that follows it.

Meaning: operations before and after the `wait` that use the value of that expression, or a value modified by that expression (equivalently, that use the side effect it produces), are naturally ordered locally.

```camel
A(i)
wait modify(i)
B(i)
```

The compiler automatically inserts the control dependencies A → modify → B.

**The precondition is that the side effect is declared explicitly.** Camel only checks functions that declare their side effects explicitly: in the example above `modify` must declare the parameter it modifies as `var` (`modify(var i: ...)`). If `modify` does not declare `var i`, the compiler does not treat it as a function that modifies `i`, and does not insert these control dependencies. When testing the behaviour of `wait`, the test program must satisfy this precondition.

Inside a sync block it makes no difference whether `wait` is written.

### 2.7 Known shortcomings

Camel's management and tracking of side effects still has defects and needs further research. Problems that arise when reference capture, the tracking of `wait` and the individual passes interact are to be recorded faithfully as findings for research, with minimal reproducers; fix only clear implementation errors, and do not cover problems with ad-hoc rules.

---

## 3. `macro`

- `macro` is a native marker provided by the language core to help with dynamic graph rewriting: a function marked `macro` can be evaluated early by the `std::macro` pass, which computes the static values and forms a new static graph.
- **`macro` itself does not define a stage, and it is not required.** When the pipeline has no `std::macro` pass, a macro function is just an ordinary function.
- A value is either a compile-time constant or the return value of a macro; only these two can be processed by a macro.
- When a macro function body has side effects, it is handled like any other normal function: the effect happens when it is evaluated. This is expected behaviour and cannot be avoided in practice.
- **A failing macro evaluation**: not yet decided. For now it is handled by reporting an error directly (undecided, see section 6).

Design motivation: the dynamism of the Python language itself determines its flexibility, and in turn brings the trouble of extracting a static graph. Camel wants a static graph and complete type information, in pursuit of the best room for optimization. The need to construct graphs dynamically is met by the macro system: with a pipeline of multi-stage graph rewriting, first construct the static graph dynamically, then optimize the static graph aggressively, and finally execute it, or translate it to another format.

---

## 4. Passes and pipelines

- The whole run is modelled as multi-stage graph rewriting. The stages are defined by the pass sequence the user composes on the command line, for example `camel xxx.cml std::macro std::opt ...`.
- **The language core does not restrict the order of passes.** But with a different order the result of execution may indeed differ: a pass does not guarantee to produce the same graph, it modifies the graph, and it may have side effects. **"Behaviour is unchanged under any pass sequence" is not a property of Camel; do not implement or test it as a contract.**
- With no user-specified pass, the default is `std::default` (which points to the node VM; the code is authoritative).
- The starting point of this design is engineering convenience, not academic elegance: with one piece of code, changing the combination of passes does many things — prototyping, debugging, compile-time optimization, export for deployment, translation to other formats, and so on — without rewriting similar logic in another language. This is one of the characteristics that the graph-native position brings.
- The optimization passes of the standard library should not make a program wrong. This is an ordinary software-quality requirement, ensured by tests.
- **Automatic differentiation is a library built on the pass mechanism, not part of the language core.** It has its own constraint: a graph on which macros have not been executed does not carry enough information for differentiation, so using `grad` and the like requires `std::macro` in the pipeline. This is a precondition of the library, not a constraint of the language core; when it is not met, the library should give a clear diagnostic.
- Export (such as ONNX) is a translation pass, likewise built on the pass mechanism.

---

## 5. Common misreadings

All of the following misreadings have actually happened; check against them before changing code or documentation:

1. Treating "an unused effect outside a sync block does not execute" as a defect to fix, or turning it into an error. — It is expected behaviour.
2. Treating `sync func` as making the function body a sync block automatically, or treating the prefix and suffix `sync` as two spellings of one modifier. — Their meanings differ.
3. Believing that the effects in the body of a function called from a sync block will therefore execute. — A function's behaviour is decided by its own subgraph.
4. Implementing a `var` parameter as pass-by-value with a copy back at return. — Camel is by reference by default.
5. Implementing the immutability of `let` as run-time memory protection. — It is a compile-time check.
6. Believing that `wait` applies to all the code after it. — It applies only to the one expression that follows.
7. Treating side effects in a macro function body as an exception or an error. — They are handled like those of an ordinary function.
8. Believing a macro function cannot run without `std::macro`. — It is then an ordinary function.
9. Treating "behaviour is unchanged under any pass sequence" as a contract of the language, and judging order-sensitivity a defect on that basis. — Passes may change the graph and have side effects.
10. Treating the precondition of automatic differentiation as a constraint of the language core. — It is a constraint of the library.
11. Picking one of the derived characteristics as Camel's "core selling point" (for example "one piece of code, many uses"). — The core is being graph-native; the rest are natural consequences.
12. Changing the semantics to make a test pass. — First confirm the semantics, then judge whether the test or the implementation is wrong.

---

## 6. Questions not yet decided

Ask the language author when you meet any of the following:

- The final handling of a failing macro evaluation (for now, an error is reported directly).
- Whether the recursion-depth limits of the schedulers need to be the same.
- The behaviour when the same mutable object is written through references in several places outside a sync block, none of them with `wait`.
- How side-effect tracking is preserved under each pass (see 2.7).
