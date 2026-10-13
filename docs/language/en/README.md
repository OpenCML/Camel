# Camel Language Reference

English | [中文](../README.md)

> The fundamental positioning of the language and its execution model are laid down by [the design principles](../../design-principles.en.md), which are normative; where this reference conflicts with them, they prevail.

This directory is the language reference of Camel, divided into chapters by topic. Every example in it has been run on the current implementation, and the `text` code blocks are real output. If you are new to Camel, read the [tutorial](../../tutorial/tutorial.en.md) first.

| Chapter | Document | Contents |
|---|---|---|
| 1 | [Introduction](01_introduction.md) | What Camel is, notation |
| 2 | [Lexical structure](02_lexical.md) | Comments, identifiers, keywords, literals (ranges and rounding), operators |
| 3 | [Data model](03_data_model.md) | Types, numbers and conversions, arrays, tuples, structs, strings, `T?` and `T!`, `Map` and `Set`, `let`/`var` |
| 4 | [Execution model](04_execution_model.md) | Entry, evaluation by dependency, `sync`, `var` and `wait`, `macro`, faults and `exit`, recursion limit, passes |
| 5 | [Modules](05_modules.md) | `module`, `import`, `export`, `namespace`, the bundled modules |
| 6 | [Expressions](06_expressions.md) | Access, calls, pipes, operators, `??`/`!!`/`?->`, `if`, `match`, lambdas, higher-order operators, precedence |
| 7 | [Statements and declarations](07_statements.md) | `let`/`var`, `return`/`throw`, functions, `with` parameters, overloading, generics, annotations, type declarations |
| 8 | [Program and grammar](08_top_level_and_grammar.md) | Top-level structure, grammar summary, list of constructs that are reserved but not implemented |

## Related documents

- [Tutorial](../../tutorial/tutorial.en.md) and [examples](../../../examples/README.md)
- [Command line](../../cli.md), [diagnostics catalogue](../../diagnostics.md)
- [Technical documents](../../technical/README.md): compilation pipeline, the intermediate representation of the graph, runtime
- Grammar: [8.5 The complete grammar](08_top_level_and_grammar.md#85-the-complete-grammar), generated from the production above each function of the parser (`src/parse/syntax/`); the token table is `include/camel/parse/syntax/token_kinds.def`
