# Changelog

## 0.1.0

- Language `camel` for `.cml` files: TextMate grammar (`source.camel`) and language configuration
  (comments, brackets, auto-closing and surrounding pairs, indentation, word pattern).
- Client of the `camel-ls` language server (`camel-ls --stdio`), found through the setting
  `camel.server.path`, `$CAMEL_HOME/bin` or `PATH`; command **Camel: Restart Language Server**.
- Command **Camel: Show Graph of Current Function** (custom request `camel/graph`), with the
  setting `camel.graph.passes`.
- Command **Camel: Run Current File with Passes…**, with the settings `camel.path` and
  `camel.run.passes`.
- Semantic token modifiers `mutable` and `sync`, with a default style (underline, italic).
- Debugger type `camel`: starts `camel-db --dap` (setting `camel.debugger.path`, then
  `$CAMEL_HOME/bin`, then `PATH`); launch attributes `program`, `passes`, `args`, `cwd`, `env`,
  `stopOnEntry`, `stepBy`, `frames`; breakpoints in `.cml` files and on functions (by full
  name); F5 debugs the current file. Command **Camel: Toggle Subgraph Frames** (also in the title
  of the Call Stack view) switches between the arms of branches folded into their function's
  frame and every subgraph as a frame. Not yet tried in the VS Code user interface.
