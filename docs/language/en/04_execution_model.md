# 4. Execution model

English | [中文](../04_execution_model.md)

> The rules of this chapter come from sections 2–4 of [the design principles](../../design-principles.en.md), which prevail in case of conflict.

A Camel program is a graph. This chapter explains how this graph is evaluated: evaluation by dependency, `sync`, `var`, `macro`, faults and exit, passes.

| Language element | What it is on the graph |
|---|---|
| Expression, operator call | A node with data edges; only nodes reachable along edges from the function's exit are executed |
| Function | A subgraph of its own; a call site is a node in the enclosing graph that points to that subgraph |
| `sync { ... }` block | Control edges between the adjacent statements it writes directly |
| `var` parameter | The callee subgraph and the caller point to the same object |
| `if`, `match`, `??`, `!!`, `?->` | A branch node; each arm is a subgraph |
| `#macro` function | An ordinary subgraph; the `std::macro` pass can evaluate it ahead of time and materialise the result as a constant in the graph |

---

## 4.1 Program and entry

- Every source file is a module, made up of declarations ([8.1](08_top_level_and_grammar.md#81-program)).
- The entry is the function named `main`. Like any other function it is a subgraph, and it can take parameters and return a value:

  - The parameters are the inputs of the program, given on the command line with `--arg name=value`; a missing one is the located error `RuntimeDiag::EntryArgumentMissing`, and is not replaced by zero or a default value. A value is read by the parameter's declared type, and an integer or a float at the declared **width**: an `i32` is an int32 and an `f32` a float, a value the type cannot hold is refused with that type's name and range (`i32`, `f32`), and a value that fits is converted once at that width. A complex number is written as the language writes one (`1+2i`, `0.5-0.25i`, or `-2i` for the imaginary part only), and a real number is its real part with an imaginary part of zero (`1.5` is `1.5+0i`, as `let z: c128 = 1.5` does; a literal takes its type from context, design principles 5.1).
  - **Only a `main` that declares its return type has a result** (the exit code, and what `-o` writes).
  - When it returns `int` (or `bool`), the return value is the exit code of the process.
  - When it returns a value of another type (`float`, `string`, a tensor, a tuple, ...), this value is the result of the program: it is printed to standard output, or written to a file by `-o <file>`.
  - When it returns `void`, the exit code is 0. A `main` that does not write a return type has no result: its body still has a value ([7.4.1](07_statements.md#741-declaration); `'a'` when it ends in `println('a')`, since `println(x)` returns `x`), but that value is neither printed nor written, and it is not the exit code, which is 0.
  - When the return type is `T!`: a value that has not failed is treated as its `T`; an **error value nobody handled** is printed on **standard error** as the diagnostic `RuntimeDiag::UnhandledError`, at the place recorded in the error value, and the exit code is 1 (see [4.6](#46-faults-exit-and-resource-limits)).

```cml
// camel greet.cml --arg name=Camel --arg times=2
func main(name: string, times: int): int sync {
    println(`Hello, ${name}! x${times}`)
    return 0
}
```

```text
Hello, Camel! x2
```

For the detailed rules see "Programs whose `main` takes parameters or returns a value" in [cli.md](../../cli.md).

## 4.2 Evaluation by dependency

Evaluation is driven by dependencies, and there is no fixed order of execution: an expression is evaluated if and only if its result is needed, that is, it reaches the function's return through data edges or control edges. Expressions that do not depend on each other have no order between them and may run out of order or in parallel.

**A call with side effects that is outside a sync block and whose result is not used does not execute.** This is a natural consequence of evaluation by dependency; it is legal, not an error. The compiler gives the warning `SemanticDiag::EffectNotExecuted`, so that a forgotten `sync` does not go unnoticed:

```cml
func greet(): int {
    println('hello from greet')     // result unused and not in a sync block: not executed
    return 1
}

func main(): int sync {
    println(greet())
    return 0
}
```

```text
warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.
  --> greet.cml:2:5
   |
 2 |     println('hello from greet')     // result unused and not in a sync block: not executed
   |     ^^^^^^^
help: Evaluation is driven by dependencies: outside a sync block an expression whose result (or the
      variable it writes) nothing needs does not run. This is legal. To run it, put it in a sync
      block ('sync { ... }', or a function body 'func f(): T sync { ... }'), or use its result.

1
```

The warning goes to standard error and the program runs as usual. Turn it off with `-Wno-effect-not-executed`; treat warnings as errors with `-Werror` (see "Diagnostics" in [cli.md](../../cli.md)).

## 4.3 `sync`

`sync` appears in two positions, with different meanings.

### 4.3.1 Sync blocks (block modifier)

A `sync` written before a block marks a sync block: **the statements it writes directly** execute in the order written, as if the compiler inserted control edges between adjacent statements. In a sync block it does not matter what is called or whether it has side effects, because there is already an order.

- As a function body: `func f(...): T sync { ... }` (`sync` comes after the return type).
- As a branch arm or lambda body: `if c then sync { ... }`, `(x: int) => sync { ... }`.
- As a standalone block statement `sync { ... }`. Note that it is itself an expression too: when it is written in a function body that is not a sync block and its value is not used, the whole block does not execute.
- **A sync block does not inherit inward** ([design principles 3.4](../../design-principles.en.md#34-sync-blocks)): a sync block orders only the statements it writes directly; the branch arms, loop bodies and nested blocks written inside it follow their own spelling, and to synchronise one of them, write `sync` there — what a block means does not change with where it is written. In the `if` below the `if` itself is on the sync chain but its arm is not: the arm is evaluated by dependency, the `println(1)` whose value nothing uses does not execute, and `println(2)` is the arm's value, which is the `if`'s value, so only it executes. To print both, write `if c then sync { ... }`:

```cml
func f(c: bool): int sync {
    if c then { println(1); println(2) }   // the arm is not a sync block: only println(2) executes
    return 0
}
```

```cml
func greet(): int sync {
    println('hello from greet')     // sync block: executed, and before the return
    return 1
}
```

### 4.3.2 A function's behaviour is decided by the function itself

A function is compiled into its own subgraph. A call only places this subgraph node into a larger graph and does not change its structure. So when a function whose body is not a sync block is called from a sync block, evaluation inside the function body is still by dependency:

```cml
func log(x: int): int {
    println(x)          // result unused, and the function body is not a sync block: not executed
    return x + 1
}

func main(): int sync {
    let a = log(1)
    let b = log(a)
    println(b)
    return 0
}
```

```text
3
```

(There is also a `SemanticDiag::EffectNotExecuted` warning pointing at `println(x)`.) The body of `main` is a sync block, and the two calls of `log` execute in order; but `println(x)` belongs to `log`'s own function body. To print, write `log` as `func log(x: int): int sync { ... }`.

### 4.3.3 `sync` on a function body (function modifier)

`sync` is the only modifier of a function ([7.4.4](07_statements.md#744-modifiers)), written in the **function body position**, and it makes that body a sync block (4.3.1): the statements written directly in it execute in the order written, and they have all finished when the function hands out its result. It does not change the function's value semantics, it only adds ordering constraints; the caller needs no cooperation (a `var` parameter is a resource parameter, design principles 3.3, and needs no declaration).

```cml
func f(): int {
    println('f')        // not executed: the function body is not a sync block
    return 1
}

func g(): int sync {
    println('g')        // executed: the function body is a sync block
    return 2
}

func main(): int sync {
    println(f() + g())
    return 0
}
```

```text
g
3
```

To make a function body execute in order, write the `sync` in the function body position; the body of a lambda is a block, and the `sync` is written before it: `(x: int) => sync { ... }`. A lambda head has no modifier (`sync (x: int) => ...` is a syntax error), and neither has a function type: `sync` is a property of a block, not of a type.

## 4.4 `var` and the order of writes

- All values are passed by reference. A `var` parameter is a reference, and a write to it by the function is a write to the caller's variable; when the same variable is passed to two `var` parameters, the two parameters are aliases of the same object.
- Assignment is an operation with a side effect. **The declaration of side effects is a closed loop**: to modify a parameter, it must be declared `var`; mutability is declared from the lowest-level operator all the way up, explicitly at every level. A function that has not declared a parameter as `var` cannot possibly modify it.
- Passing a `let` to a `var` parameter, and assigning to a non-`var` parameter inside a function body, are both compile-time errors (`SemanticDiag::ImmutablePassedToVarParam`).

**The order comes from the sync block that writes.** Several writes to one variable in the same sync block are ordered by the order they are written in. When the function body is not a sync block, a write is ordered against the operations that read it only by dependency:

```cml
func bump(var counter: int): int sync {
    counter += 1
    return counter
}

func twice(): int sync {
    var c = 0
    bump(c)
    bump(c)
    return c
}

func main(): int sync {
    println(twice())
    return 0
}
```

```text
2
```

- When a write is not in a sync block and nothing reads the variable afterwards, the modification does not execute (evaluation by dependency).
- A write that is not in a sync block has no order relative to earlier reads; the compiler gives the warning `SemanticDiag::IgnoredSideEffect`.
- Tracking of modifications covers captures: when a function writes a `var` through a capture, the call site is recorded as a write to that variable. For a call through a function value (an argument, a return value or a stored lambda), the call site can recognise side effects only by its type (`var` parameters).

The management and tracking of side effects is still under research (the design principles, 2.8). The behaviour when the same mutable object is written in several places outside a sync block is not yet decided.

## 4.5 `#macro` functions

`#macro` is a standard tag ([6.2](../../design-principles.en.md), [8](../../design-principles.en.md)), written before a function declaration, usable without an import:

```cml
#macro func cube(x: int): int { return x * x * x }

func main(): int sync {
    println(cube(3))
    return 0
}
```

`camel m.cml` and `camel m.cml std::macro std::nvm` both print `27`; in the graph of the latter, `cube(3)` is already the constant `27` (which can be seen with `camel m.cml std::macro std::rgir`).

`#macro` is a mark on the declaration, not part of the function type: the function type is that of an ordinary function. It may be written only on a function declaration; on a type, field, parameter or block it is a tag-position error, and `#macro<...>` is a tag-argument-count error. A lambda has no declaration to carry it, so `#macro` cannot be written there.

- The tag itself does not define a stage, and it is not mandatory: when the pipeline has no `std::macro`, a macro function is just an ordinary function.
- Only calls whose arguments are all constants (or results of another macro call) are evaluated ahead of time; otherwise the call is left to run time.
- Side effects in the body of a macro function are handled as in an ordinary function: they happen at the time of evaluation.
- Evaluation proceeds in rounds: a round's results become the next round's constants, until no call can be evaluated ahead of time any more. A macro function value obtained in one round can therefore be expanded in the next.
- A failure of ahead-of-time evaluation (an operator reports an error, or the evaluation depth limit is exceeded; the default is 64 levels, adjustable with `--max-macro-depth`) is a run-time fault, like a fault during the run of any pass: the stage is `pass:std::macro`, the position is the call, the program does not run, and the effects the evaluation performed before failing stay. A loop evaluated by `std::macro` runs at most 1,000,000 rounds (`--max-macro-rounds`), and passing that is the same fault.
- `grad` and the other automatic-differentiation operators are macro operators — a fact of the operator declaration, not the `#macro` tag; using them requires `std::macro` in the pipeline (section 16 of the tutorial). This is a precondition of that library, not a constraint of the language.

## 4.6 Faults, exit and resource limits

A **fault** is a situation in which the program cannot continue: a zero divisor, an out-of-range subscript, a `match` with no matching case, too deep a recursion, number text that cannot be converted, and so on. A fault terminates the program with a source location and exit code 1; output produced before it is written out as usual. A fault is not an error value, and `!!` cannot catch it.

```cml
func main(): int sync {
    let xs = [1, 2, 3]
    println('before')
    println(xs[5])
    println('after')
    return 0
}
```

```text
before
error[RuntimeDiag::IndexOutOfRange]: Index 5 is out of range for an array of length 3.
  --> fault.cml:4:15
   |
 4 |     println(xs[5])
   |               ^
help: Ensure the index is within the valid range of the collection.
```

The operator that isolates a fault at a subgraph boundary is `attempt` ([6.10.1](06_expressions.md)).

**`exit(n)`** ends the program immediately with exit code `n`; earlier output is written out as usual.

**Recursion depth.** A call in tail position does not occupy a stack frame, and tail recursion can be arbitrarily deep. The number of calls open at the same time in non-tail recursion has a limit, 1000 by default; exceeding it is the fault `RuntimeDiag::MaxRecursionDepthExceeded`; adjust it with `--max-recursion-depth <n>` or the environment variable `CAMEL_MAX_RECURSION_DEPTH`. The count includes calls through function values and the calls a higher-order operator makes for each element. The default is the same on every back end, and the parallel scheduler counts like the others.

```cml
func depth(n: int): int {
    return if n == 0 then 0 else 1 + depth(n - 1)     // non-tail recursion
}

func count(n: int, acc: int): int {
    return if n == 0 then acc else count(n - 1, acc + 1)   // tail recursion
}

func main(): int sync {
    println(count(1000000, 0))
    println(depth(200))
    println(depth(1200))
    return 0
}
```

```text
1000000
200
error[RuntimeDiag::MaxRecursionDepthExceeded]: Maximum recursion depth (1000) exceeded when evaluating graph '__g_7__'.
```

(Followed by the location and a suggestion.)

**Other limits**: the nodes, blocks and edge-table entries of a single graph are each at most 65,535; the three memory regions graph space, permanent space and metaspace are 256 MB each, adjusted with `CAMEL_GRAPH_SPACE_LIMIT_MB`, `CAMEL_PERM_SPACE_LIMIT_MB` and `CAMEL_META_SPACE_LIMIT_MB` respectively, and exhausting one is `RuntimeDiag::MemoryExhausted`. For the full table see "Resource limits" in [cli.md](../../cli.md).

## 4.7 Passes and pipelines

A program is processed by the sequence of passes on the command line: `camel file.cml pass1 pass2 ...`. Each pass transforms the graph, executes the graph (a scheduling pass) or translates the graph into something else (a translation pass).

| Pass | What it does |
|---|---|
| (none written) | `std::default`, that is, `std::nvm` |
| `std::nvm` | Node VM: interprets directly on the graph |
| `std::fvm` | Bytecode VM |
| `std::jit` | Bytecode VM with JIT compilation; graphs that cannot be compiled are executed by the interpreter |
| `std::prl` | Parallel scheduling: nodes that do not depend on each other can execute concurrently |
| `std::macro` | Evaluates macro calls ahead of time |
| `std::opt`, `std::opt::inline`, `std::opt::fold`, `std::opt::cse`, `std::opt::dce`, `std::opt` | Optimisation |
| `std::rgir`, `std::gir` | Print the graph (DOT format) without running it |
| `std::stats` | Prints statistics of the graph and passes the graph on unchanged |
| `onnx::export`, `std::cpp` | Translate to an ONNX model, to C++ source |

- **The language does not restrict the order of passes, and the order can affect the result.** Passes modify the graph and may have side effects; "the same behaviour under any sequence of passes" is not a property of the language.
- Scheduling passes and translation passes consume the graph, and passes written after them do not run (there is a warning `RuntimeDiag::PassesNeverRun`). When none of the specified passes consumes the graph, the default pass runs next.
- The source-level rules (`var`/`let`, `sync`) constrain how the compiler builds the graph; once the graph has been obtained, these rules no longer constrain later passes. The optimisation passes of the standard library aim not to change the result of the program; this is a quality requirement on those passes, ensured by tests.
- When a pass of some module (such as `onnx::export`) is written on the command line, the compiler loads that module automatically.

## 4.8 Order in higher-order operators

- For `map` and `filter` there is no agreed order of execution between the elements: parallel scheduling (`std::prl`) may execute them concurrently, and the results are arranged by element position. A sync block does not change this. The order of side effects in the element function is not guaranteed under a parallel `map`.
- `foreach` and `reduce` execute in element order. Their unordered parallel versions are `unordered_foreach` and `unordered_reduce` of the `para` module.
