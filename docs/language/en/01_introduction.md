# 1. Introduction

English | [中文](../01_introduction.md)

## 1.1 Purpose of this reference

This reference describes the syntax and core semantics of Camel. It is divided into chapters by topic, and every example can be run as written. If you are new to Camel, read the [tutorial](../../tutorial/tutorial.en.md) first; for the command line see [cli.md](../../cli.md); for the meaning of each diagnostic see [diagnostics.md](../../diagnostics.md) or `camel --explain <name>`.

The fundamental positioning of the language and its execution model are laid down by [the design principles](../../design-principles.en.md), which are normative: where this reference conflicts with them, they prevail.

## 1.2 What Camel is

Camel is a graph-native general-purpose programming language; AI is its primary application domain.

- **A program expresses only computational intent**: what is to be computed, and what depends on what. What the source code writes down is a computation graph; it is not captured or recovered from some other language.
- **Every language element corresponds directly to a piece of structure on the graph**: an expression is a node with data edges, a function is a subgraph, a call is a node, a `sync` block is a control edge, a branch is a branch node. All other properties follow from this correspondence: evaluation by dependency, computations that do not depend on each other can run in parallel, the same source code can be processed by different passes.
- **Compilation, optimisation, differentiation, export and execution are all transformations of the graph**, called passes. Passes are combined on the command line:

  ```bash
  camel prog.cml                      # default: run on the node VM
  camel prog.cml std::opt std::fvm    # optimise first, then run on the bytecode VM
  camel prog.cml std::rgir            # print the graph
  ```

The smallest program:

```cml
module hello

func main(): int sync {
    println('Hello, Camel!')
    return 0
}
```

```text
Hello, Camel!
```

## 1.3 Notation

- **Syntax** is written in a notation close to BNF: `*` zero or more times, `?` optional, `|` alternative, literal symbols inside quotes. The exact grammar is in [8.5](08_top_level_and_grammar.md#85-the-complete-grammar).
- **Examples** are given as `cml` code blocks; a `text` code block is its output.
- **"Behaviour of the current implementation, not yet decided"**: the implementation already has a definite behaviour, but there is no ruling on the language rule yet, and it may change later. Do not rely on the details of these behaviours when writing programs.

## 1.4 Chapters

| Chapter | Contents |
|---|---|
| [2 Lexical structure](02_lexical.md) | Comments, identifiers, keywords, literals, operators |
| [3 Data model](03_data_model.md) | Types, values, `let`/`var`, nullable and fallible types, maps and sets |
| [4 Execution model](04_execution_model.md) | Evaluation by dependency, `sync`, references, faults and exit, passes |
| [5 Modules](05_modules.md) | `module`, `import`, `export`, `namespace` |
| [6 Expressions](06_expressions.md) | Literals, access, calls, pipes, operators, `if`, `match`, lambdas |
| [7 Statements and declarations](07_statements.md) | `let`/`var`, `return`/`throw`, blocks, functions, generics, type declarations |
| [8 Program and grammar](08_top_level_and_grammar.md) | Top-level structure, entry, grammar summary, constructs reserved but not implemented |

**Loops.** `loop` is an expression with state: the state is written on the loop head, the body's value is the next round's state, and the value of the whole loop is the final state. `loop acc = 0 for x in xs do acc + x` takes one element of the sequence per round, in order; `loop i = 1, acc = 0 while i <= n do (i + 1, acc + i)` goes on while the condition holds; with both written, each round looks at the condition first and then takes an element. There is no `break` and no `continue`: ending early is written as a `while` condition, and skipping the rest of a round as an `if`. A loop costs no call depth, so it may run any number of rounds. The exact rules are in [6.8.6](06_expressions.md#686-loop).
