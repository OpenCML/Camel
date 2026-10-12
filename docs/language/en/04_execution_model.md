# 4. Execution model

English | [中文](../04_execution_model.md)

> The rules of this chapter come from sections 2–4 of [the design principles](../../design-principles.en.md), which prevail in case of conflict.

A Camel program is a graph. This chapter explains how this graph is evaluated: evaluation by dependency, `sync`, `var` and `wait`, `macro`, faults and exit, passes.

| Language element | What it is on the graph |
|---|---|
| Expression, operator call | A node with data edges; only nodes reachable along edges from the function's exit are executed |
| Function | A subgraph of its own; a call site is a node in the enclosing graph that points to that subgraph |
| `sync { ... }` block | Control edges between adjacent expressions in the block |
| `sync func` (function modifier) | A mark on the function type; it produces no edge inside the function body |
| `var` parameter | The callee subgraph and the caller point to the same object |
| `wait e` | Control edges between `e` and the operations before and after it that use its value or the variables it modifies |
| `if`, `match`, `??`, `!!`, `?->` | A branch node; each arm is a subgraph |
| `macro` function | An ordinary subgraph; the `std::macro` pass can evaluate it ahead of time and materialise the result as a constant in the graph |

---

## 4.1 Program and entry

- Every source file is a module, made up of declarations ([8.1](08_top_level_and_grammar.md#81-program)).
- The entry is the function named `main`. Like any other function it is a subgraph, and it can take parameters and return a value:

  - The parameters are the inputs of the program, given on the command line with `--arg name=value`; a missing one is the located error `RuntimeDiag::EntryArgumentMissing`, and is not replaced by zero or a default value.
  - When it returns `int` (or `bool`), the return value is the exit code of the process.
  - When it returns a value of another type (`float`, `string`, a tensor, a tuple, ...), this value is the result of the program: it is printed to standard output, or written to a file by `-o <file>`.
  - When the return type is not written or is `void`, the exit code is 0.

```cml
// camel greet.cml --arg name=Camel --arg times=2
func main(name: string, times: int): int sync {
    println('Hello, {}! x{}'->format<name, times>)
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

A `sync` written before a block marks a sync block: the expressions in the block **execute in the order written**, as if the compiler inserted control edges between adjacent expressions. In a sync block it does not matter what is called or whether it has side effects, because there is already an order.

- As a function body: `func f(...): T sync { ... }` (`sync` comes after the return type).
- As a branch arm or lambda body: `if c then sync { ... }`, `(x: int) => sync { ... }`.
- As a standalone block statement `sync { ... }`. Note that it is itself an expression too: when it is written in a function body that is not a sync block and its value is not used, the whole block does not execute.

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

(There is also an `EffectNotExecuted` warning pointing at `println(x)`.) The body of `main` is a sync block, and the two calls of `log` execute in order; but `println(x)` belongs to `log`'s own function body. To print, write `log` as `func log(x: int): int sync { ... }`.

### 4.3.3 `sync func` (function modifier)

A `sync` written before `func` modifies the function: it declares that the function has side effects and needs to be synchronised, and that callers may need to `wait` it.

- Functions with `var` parameters and functions that write captured variables (including module top-level variables) should be declared this way; when they are not, the compiler gives the warnings `VarParamInAsyncFunction` and `CapturedWriteInAsyncFunction`. For a lambda the form is `sync (x: int) => ...`.
- **It does not make the function body a sync block.** To make the function body execute in order, use both:

```cml
sync func f(): int {
    println('f')        // still not executed
    return 1
}

sync func g(): int sync {
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

At present this modifier is only a declaration: the compiler checks "whether a function with side effects has declared it", and does not produce any order at call sites on the basis of it. How call sites should treat it is not yet decided.

## 4.4 `var`, references and `wait`

- All values are passed by reference. A `var` parameter is a reference, and a write to it by the function is a write to the caller's variable; when the same variable is passed to two `var` parameters, the two parameters are aliases of the same object.
- Assignment is an operation with a side effect. **The declaration of side effects is a closed loop**: to modify a parameter, it must be declared `var`; mutability is declared from the lowest-level operator all the way up, explicitly at every level. A function that has not declared a parameter as `var` cannot possibly modify it.
- Passing a `let` to a `var` parameter, and assigning to a non-`var` parameter inside a function body, are both compile-time errors (`SemanticDiag::ImmutablePassedToVarParam`).

**`wait` applies to the one expression that follows it.** Operations before and after the `wait` that use the value of that expression, or use a variable modified by it, are locally ordered with it:

```cml
sync func bump(var counter: int) {
    wait counter += 1
}

// the function body is not a sync block: the order comes from wait and data dependencies
func twice(): int {
    var c = 0
    wait bump(c)
    wait bump(c)
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

- `wait` is not mechanical ordering; it orders on the basis of how `var`s are used. What is ordered before the modification are those earlier operations that **directly used** the value of the variable: in `A(i + 1); wait modify(i)` it is `+` that directly uses `i`, so `modify` is ordered after `+`, with no guarantee that it is ordered after `A`. A waited write is ordered before later reads of the variable.
- If the waited expression involves no `var` at all (for example, only input and output), `wait` has nothing to go by and therefore has no meaning; write such calls in a sync block.
- In a sync block it makes no difference whether `wait` is written or not.
- If nothing reads `i` any more after `wait modify(i)`, the modification does not execute (evaluation by dependency).
- A write that has no `wait` and is not in a sync block has no order relative to earlier reads; the compiler gives the warning `SemanticDiag::IgnoredSideEffect`.
- Tracking of modifications covers captures: when a function writes a `var` through a capture, the call site is recorded as a write to that variable. For a call through a function value (an argument, a return value or a stored lambda), the call site can recognise side effects only by its type (the `sync` modifier, `var` parameters).
- `wait` can be written before calls, assignments and operator expressions. Writing it before a block, a name, a literal or a lambda is reserved, not implemented (`SemanticDiag::ConstructNotSupported`); to make a block execute in order, use `sync { ... }`.

The management and tracking of side effects is still under research (the design principles, 2.8). The behaviour when the same mutable object is written in several places outside a sync block, none of them with `wait`, is not yet decided.

## 4.5 `macro` functions

A function marked `macro` can be evaluated ahead of time by the `std::macro` pass, and the result is materialised as a constant in the graph:

```cml
macro func cube(x: int): int { return x * x * x }

func main(): int sync {
    println(cube(3))
    return 0
}
```

`camel m.cml` and `camel m.cml std::macro std::nvm` both print `27`; in the graph of the latter, `cube(3)` is already the constant `27` (which can be seen with `camel m.cml std::macro std::rgir`).

- `macro` itself does not define a stage, and it is not mandatory: when the pipeline has no `std::macro`, a macro function is just an ordinary function.
- Only calls whose arguments are all constants (or results of another macro) are evaluated ahead of time; otherwise the call is left to run time.
- Side effects in the body of a macro function are handled as in an ordinary function: they happen at the time of evaluation.
- A failure of ahead-of-time evaluation (an operator reports an error, or the evaluation depth limit is exceeded; the default is 64 levels, adjustable with `--max-macro-depth`) is the compile-time error `RuntimeDiag::MacroEvaluationFailed`, and the program does not run.
- `grad` and the other automatic-differentiation operators are macro operators, and using them requires `std::macro` in the pipeline (section 16 of the tutorial). This is a precondition of that library, not a constraint of the language.

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

The operator that isolates a fault at a subgraph boundary (provisionally named `attempt` in the design principles) is not provided yet.

**`exit(n)`** ends the program immediately with exit code `n`; earlier output is written out as usual.

**Recursion depth.** A call in tail position does not occupy a stack frame, and tail recursion can be arbitrarily deep. The number of calls open at the same time in non-tail recursion has a limit, 256 by default; exceeding it is the fault `RuntimeDiag::MaxRecursionDepthExceeded`; adjust it with `--max-recursion-depth <n>` or the environment variable `CAMEL_MAX_RECURSION_DEPTH`. The count includes calls through function values and the calls a higher-order operator makes for each element. Whether all back ends take the same default is not yet decided (`std::prl` does not count when no limit is given and is limited only by the thread stack).

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
    println(depth(300))
    return 0
}
```

```text
1000000
200
error[RuntimeDiag::MaxRecursionDepthExceeded]: Maximum recursion depth (256) exceeded when evaluating graph '__g_7__'.
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
| `std::opt`, `std::inline`, `std::opt::fold`, `std::opt::cse`, `std::opt::dce`, `std::opt::simplify` | Optimisation |
| `std::rgir`, `std::gir` | Print the graph (DOT format) without running it |
| `std::stats` | Prints statistics of the graph and passes the graph on unchanged |
| `onnx::export`, `std::cpp` | Translate to an ONNX model, to C++ source |

- **The language does not restrict the order of passes, and the order can affect the result.** Passes modify the graph and may have side effects; "the same behaviour under any sequence of passes" is not a property of the language.
- Scheduling passes and translation passes consume the graph, and passes written after them do not run (there is a warning `RuntimeDiag::PassesNeverRun`). When none of the specified passes consumes the graph, the default pass runs next.
- The source-level rules (`var`/`let`, `wait`, `sync`) constrain how the compiler builds the graph; once the graph has been obtained, these rules no longer constrain later passes. The optimisation passes of the standard library aim not to change the result of the program; this is a quality requirement on those passes, ensured by tests.
- When a pass of some module (such as `onnx::export`) is written on the command line, the compiler loads that module automatically.

## 4.8 Order in higher-order operators

- For `map` and `filter` there is no agreed order of execution between the elements: parallel scheduling (`std::prl`) may execute them concurrently, and the results are arranged by element position. A sync block does not change this. The order of side effects in the element function is not guaranteed under a parallel `map`.
- `foreach` and `reduce` execute in element order. Their unordered parallel versions are `unordered_foreach` and `unordered_reduce` of the `para` module.
