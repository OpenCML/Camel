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
