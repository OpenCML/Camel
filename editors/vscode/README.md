# Camel for Visual Studio Code

Editor support for [Camel](https://github.com/OpenCML/Camel) source files (`.cml`):

- **Syntax highlighting** from a TextMate grammar checked against the token table of the language's own
  lexer (`include/camel/parse/syntax/token_kinds.def`).
- **Language features** from the `camel-ls` language server (diagnostics, hover, navigation,
  completion, semantic highlighting, ...). What the server supports is listed in
  [`docs/editor-support.en.md`](../../docs/editor-support.en.md).
- **Camel: Show Graph of Current Function**: the graph the compiler builds for the function under
  the cursor, as Graphviz DOT, beside the editor.
- **Camel: Run Current File with Passes…**: runs `camel <file> <pass>...` in the integrated
  terminal.

- **Debugging** (type `camel`): breakpoints, stepping, call stack and variables through
  `camel-db --dap`, a debug adapter that is built with the rest of the repository. Press F5 in a
  `.cml` file, or add a configuration from the snippets. What stepping, the stack and variables
  mean in Camel is explained in [`docs/debugging.en.md`](../../docs/debugging.en.md). The debugger
  has been tested by speaking the protocol to the adapter; the VS Code user interface on top of it
  has not been tried by hand.

The extension is a thin client: everything that needs to understand a program is done by
`camel-ls`, and everything that controls a running one by `camel-db`.

## Requirements

`camel-ls` and `camel` are built with the rest of the repository (`npm run build`, see
[`docs/setup.en.md`](../../docs/setup.en.md)); they land in `out/latest/bin`. The extension looks
for each of them in this order:

1. the setting (`camel.server.path` for `camel-ls`, `camel.path` for `camel`);
2. `$CAMEL_HOME/bin`;
3. the directories of `PATH`.

`CAMEL_HOME` is read from the environment VS Code was started in. When `camel-ls` is not found the
extension says where it looked and offers to open the setting; syntax highlighting works without
it.

## Building and installing

```bash
cd editors/vscode
npm install
npm run package            # checks, compiles, writes camel-lang-<version>.vsix
code --install-extension camel-lang-0.1.0.vsix
```

To try it without installing, open `editors/vscode` in VS Code, run `npm install` and
`npm run compile`, and press F5 ("Run Extension").

## Commands

| Command | What it does |
|---------|--------------|
| `Camel: Restart Language Server` | Stops `camel-ls` and starts it again, looking it up afresh. |
| `Camel: Show Graph of Current Function` | Sends the request `camel/graph` for the cursor position and shows the answer in a read-only document beside the editor. The document is refreshed when the source file is saved. |
| `Camel: Run Current File with Passes…` | Asks for a pass sequence (remembered per workspace), saves the file and runs `camel <file> <passes>` as a task in the integrated terminal. |

The graph is shown as DOT text, the output of the `std::rgir` pass. The extension does not bundle
a renderer: install a Graphviz preview extension and use it on the graph document (its name ends
in `.dot`) to see the picture.

Passes are applied in the order given. `camel fib.cml std::inline std::fvm` inlines, then runs on
the bytecode VM; with no pass, or when no pass consumes the graph, `std::nvm` runs the program.

## Settings

| Setting | Default | Meaning |
|---------|---------|---------|
| `camel.server.path` | `""` | Path of `camel-ls`. Changing it restarts the server. |
| `camel.path` | `""` | Path of `camel`, for **Run Current File with Passes…**. |
| `camel.debugger.path` | `""` | Path of `camel-db`, started as `camel-db --dap` for a debug session of type `camel`. |
| `camel.graph.passes` | `[]` | Passes applied before the graph is printed, for example `["std::inline"]`. |
| `camel.run.passes` | `["std::inline", "std::fvm"]` | Pass sequence proposed the first time a file is run. |
| `camel.trace.server` | `off` | Logs the protocol messages in the output channel "Camel Language Server". |

## Semantic highlighting

`camel-ls` publishes semantic tokens with two modifiers that are specific to Camel, and the
extension gives them a default style so that the two distinctions are visible in every theme:

| Modifier | Meaning | Default style |
|----------|---------|---------------|
| `mutable` | a variable or parameter declared with `var` (a `let` binding is `readonly`) | underlined |
| `sync` | a function declared `sync func`: it has side effects | italic |

Change them under `editor.semanticTokenColorCustomizations`, with the selectors `*.mutable:camel`
and `function.sync:camel`.

Without the server the grammar still tells the two kinds of declaration apart: the names declared
by `let` get the scope `variable.other.constant.camel`, those declared by `var`
`variable.other.readwrite.camel`.

## Notes on the grammar

A TextMate grammar sees one line at a time and has no symbol table; where the language needs more
than that, the grammar follows the parser's own rule or leaves the decision to the server:

- `name<...>` directly after a name is a list of with-arguments or type arguments, `a < b` with a
  space is a comparison. The parser decides the same way (by adjacency).
- A sign directly before digits is part of the number, as it is for the lexer: `a -1` is the name
  `a` followed by the literal `-1`, not a subtraction. Write `a - 1`.
- `t.0.1` is the name `t`, a dot and the real `0.1` for the lexer; write `(t.0).1`.
- `@name` is highlighted as a decorator at the start of a line only; elsewhere `@` is the operator.
- User-defined type names cannot be told from other names without the server.

## Development

```bash
node scripts/check-grammar.mjs     # keyword, operator and type tables against the lexer and parser
node scripts/check-manifest.mjs    # package.json, grammar and language configuration agree
node scripts/tokenize.mjs ../../examples/tty/snake.cml   # scopes of every token of a file
```

The same checks, and snapshots of the scopes of a set of programs, run with the repository's
tests (from the repository root; the snapshot tests are skipped until `npm install` has been run
here):

```bash
node scripts/test.js test/plans/feat/tools/grammar.plan.toml
node test/tools/run-tests.mjs --update-golden test/plans/feat/tools/grammar.plan.toml
```
