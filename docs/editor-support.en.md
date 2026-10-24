# Editor Support

[中文简体](editor-support.cn.md) | English

Camel's editor support has two parts:

- **`camel-ls`**, a language server speaking the [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) over standard input and output. It is built with the rest of the repository and works with any editor that has an LSP client.
- **The VS Code extension** in [`editors/vscode`](../editors/vscode): a TextMate grammar and a language configuration for `.cml` files, a thin client that starts `camel-ls`, and two commands that have no counterpart in the protocol (show the graph of a function, run the file through a pass sequence).

Debugging is a third part with a guide of its own: [Debugging Camel Programs](debugging.en.md). The extension contributes the debugger type `camel`, which starts `camel-db --dap`.

---

## 1. Building and installing

### 1.1 `camel-ls`

`camel-ls` is one of the tools under `tools/` (`tools/lang-server`); the ordinary build produces it next to `camel`:

```bash
npm run build
```

The executable is `out/latest/bin/camel-ls` (`camel-ls.exe` on Windows). Editors find it most easily when `CAMEL_HOME` points at the installation root and its `bin` directory is on `PATH`:

```powershell
$env:CAMEL_HOME = "project-root\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
```

```bash
export CAMEL_HOME=/path/to/project-root/out/latest
export PATH="$CAMEL_HOME/bin:$PATH"
```

The server is started as `camel-ls --stdio` and needs no other argument. Like `camel`, it finds the standard library through `CAMEL_HOME` (or `CAMEL_STD_LIB`).

### 1.2 The VS Code extension

Requirements: Node.js 20 or later and VS Code 1.82 or later.

```bash
cd editors/vscode
npm install
npm run package
code --install-extension camel-lang-0.1.0.vsix
```

`npm run package` checks the grammar against the lexer, compiles the TypeScript source and writes `camel-lang-<version>.vsix`. To try the extension without installing it, open `editors/vscode` in VS Code after `npm install` and `npm run compile`, and press F5.

The extension looks for `camel-ls` in this order: the setting `camel.server.path`, then `$CAMEL_HOME/bin`, then `PATH`. `camel` (for the run command) is found the same way, with the setting `camel.path`. When `camel-ls` is not found the extension reports where it looked and points at the setting; syntax highlighting does not depend on the server.

| Command | What it does |
|---------|--------------|
| `Camel: Restart Language Server` | Stops `camel-ls` and starts it again. |
| `Camel: Show Graph of Current Function` | Shows the graph of the function under the cursor as Graphviz DOT in a read-only document beside the editor; refreshed when the file is saved. A Graphviz preview extension can render it. |
| `Camel: Run Current File with Passes…` | Asks for a pass sequence (for example `std::opt::inline std::fvm`) and runs `camel <file> <passes>` in the integrated terminal. |

| Setting | Default | Meaning |
|---------|---------|---------|
| `camel.server.path` | `""` | Path of `camel-ls`. |
| `camel.path` | `""` | Path of `camel`. |
| `camel.debugger.path` | `""` | Path of `camel-db`, started as `camel-db --dap` for a debug session. |
| `camel.graph.passes` | `[]` | Passes applied before the graph is printed, for example `["std::opt::inline"]`. |
| `camel.run.passes` | `["std::opt::inline", "std::fvm"]` | Pass sequence proposed the first time a file is run. |
| `camel.trace.server` | `off` | Logs the protocol messages (`messages` or `verbose`). |

See [`editors/vscode/README.md`](../editors/vscode/README.md) for details.

---

## 2. Capabilities


| Capability | LSP method | Status |
|------------|------------|--------|
| Diagnostics (push) | `textDocument/publishDiagnostics` | Implemented. Published when a document is opened or changed, for clients that do not announce pull diagnostics (pushing to a client that pulls would show every problem twice); `initializationOptions.diagnostics` = `"push"`, `"pull"` or `"both"` overrides. |
| Diagnostics (pull) | `textDocument/diagnostic` | Implemented. Code, catalog name, suggestion, notes and the compiler's own fixes (`data.fixes`: a title, and edits as `{range, newText}`) in `data`, secondary locations as `relatedInformation`. |
| Incremental sync | `textDocument/didChange` (incremental) | Implemented (range edits, UTF-16 positions). |
| Hover | `textDocument/hover` | Implemented: declaration as written, types in source spelling, comment lines above a declaration, instances of a generic function and the instance or overload a call resolved to, fields, literals, library signatures, the `--explain` entry of a diagnostic under the cursor. A member of a namespace is shown by its full name, `func Tools::inc(x: int): int` (design principles 10.2); so are the outline and completion. |
| Go to definition | `textDocument/definition` | Implemented: locals, parameters, functions (the overload a call chose), type aliases, type variables, imported names across files. Not for library functions implemented in C++. |
| Find references | `textDocument/references` | Implemented within one program: the open file and the modules it imports. |
| Document highlight | `textDocument/documentHighlight` | Implemented. |
| Document symbols | `textDocument/documentSymbol` | Implemented (hierarchical; works on files with syntax errors). |
| Workspace symbols | `workspace/symbol` | Implemented: open documents and the `.cml` files under the workspace folders, parsed, not compiled. |
| Completion | `textDocument/completion` | Implemented: names in scope, fields after `.`, module members after `::`, keywords, built-in and library functions with signatures; also while the text does not parse. A namespace member is labelled with its full name (`Tools::inc`) and inserts the part that is written there (`inc`). |
| Signature help | `textDocument/signatureHelp` | Implemented for functions called by name; not for `f<...>(` and not for function values without a declared type. |
| Rename | `textDocument/rename` | Implemented with `prepareRename`; refused, with the reason, whenever a reference could be missed (see limitations). |
| Semantic tokens | `textDocument/semanticTokens/full` | Implemented (`full`). |
| Formatting | `textDocument/formatting` | Implemented (whole document; a document with a syntax error is not formatted). |
| Range formatting | `textDocument/rangeFormatting` | Not implemented: the formatter (`tools/format`) formats whole files only. |
| Code actions | `textDocument/codeAction` | Implemented. Every fix the compiler attached to a diagnostic that the requested range touches is offered as a quick fix whose workspace edit applies it, and the action quotes the diagnostic it repairs; a name that resolves to nothing is repaired by the candidate search among the names in scope (design principles 4.1), marked preferred. Diagnostics that carry no fix offer nothing: their `suggestion` is prose, and a guess an editor applies is worse than a sentence. |
| Graph of a function | `camel/graph` (custom) | Implemented: the `std::rgir` text of the function at the cursor, optionally after a pass sequence, for the unsaved text. |

### 2.1 Semantic tokens

The server's legend uses standard token types (`namespace`, `type`, `typeParameter`, `parameter`, `variable`, `property`, `function`, `macro`, `keyword`, `operator`, `number`, `string`) and these modifiers:

| Modifier | Standard | Meaning |
|----------|----------|---------|
| `declaration` | yes | The token declares the name. |
| `readonly` | yes | An immutable binding (`let`). |
| `defaultLibrary` | yes | A name of the standard library or a built-in. |
| `mutable` | no | A variable or parameter declared with `var`. |
| `sync` | no | A sync block: a function whose body is written `: T sync { ... }`, and a lambda such as `sync (x: int) => ...`. |

`mutable` is the distinction that matters when reading Camel: whether a name can be assigned again. The VS Code extension declares both `mutable` and `sync` and styles them by default (`mutable` underlined, `sync` italic); `sync` marks a sync block ([4.3.3](language/en/04_execution_model.md#433-sync-on-a-function-body-function-modifier)), which only the `: T sync` function body and the `sync` lambda spell today. In another editor, map them to a highlight of your choice; an editor that does not know a modifier ignores it.

### 2.2 The `camel/graph` request

A custom request, client to server, that returns the graph of the function at a position as text.

Parameters:

```json
{
  "textDocument": { "uri": "file:///path/to/file.cml" },
  "position": { "line": 7, "character": 10 },
  "passes": ["std::opt::inline"]
}
```

`passes` is optional: the passes applied, in order, before the graph is printed.

Result:

```json
{
  "function": "fib",
  "passes": ["std::opt::inline"],
  "text": "digraph GraphIR { ... }",
  "diagnostics": ""
}
```

`function` is the name of the function the position is in, or `null` when there is none; `text` is Graphviz DOT in the format of the `std::rgir` pass (see [cli.md](cli.md)); `diagnostics`, when present, is what the compiler reported while building the graph.

---

## 3. Known limitations

Everything below was observed with the LSP test driver in a container. **Nothing has been
verified inside an editor**: the VS Code extension compiles and packages, but activation,
colours, the graph view and the run command were never exercised in VS Code.

- **Whole-file recompilation.** There is no incremental compilation. A model is rebuilt when a
  request needs it or 150 ms after the last message (`initializationOptions.debounceMs`). A
  file of 2000 lines takes about 60 ms, one of 20000 lines about a second
  ([technical/32_semantic_query_layer.md](technical/32_semantic_query_layer.md), section 5).
- **Imported modules are read from disk.** Unsaved changes of an imported file are not seen
  by the files that import it.
- **One program at a time.** References, rename and highlights cover the open file and what it
  imports. Who imports a file is unknown: references asked from a library file miss its
  users, and an exported name cannot be renamed.
- **Rename refuses when it is not certain**: a file with errors, an exported name, `main`, a
  name declared in another file, a field, a library function, a name with a use inside a
  generic function that is never called, a new name already declared or used where the old
  one is visible.
- **Text with errors.** With a syntax error the outline, local definitions and completion
  still work, from what the parser recovers; types come from the last text that compiled, so
  the type of a declaration added since then is unknown until the text compiles again. Only
  the first syntax error of a file is reported (a choice of the compiler).
- **Fields** are matched by the text of the receiver's type; go to definition on a field does
  nothing.
- **Signature help** is found from the tokens before the cursor: not inside `f<...>(...)`
  with-arguments, and a block `{` between the call and the cursor ends the search.
- **Semantic tokens** skip tokens that span several lines (multi-line strings, block
  comments); the TextMate grammar colours those.
- **`camel/graph`** runs the `camel` executable beside `camel-ls` on a scratch copy of the
  text written next to the document (so that relative imports resolve) and removes it
  afterwards; a read-only folder makes the request fail. The graph of a function includes
  the functions nested in its cluster.
- **Text that is not UTF-8** cannot be sent at all (the protocol is JSON); such a frame is
  dropped with a parse error.
- **Documentation comments** are a convention of the server, not of the language: the `//`
  lines (or one block comment) directly above a declaration.

---

## 4. Other editors

`camel-ls` is an ordinary stdio language server, so any LSP client can start it. The configurations below are **unverified**: they were written from memory of each editor's configuration interface, checked against neither its documentation nor a running editor, and have not been run against `camel-ls`. They assume `camel-ls` is on `PATH`; otherwise give its full path.

None of these editors has a syntax definition for Camel. Highlighting then comes from the server's semantic tokens alone, where the editor supports them.

### 4.1 Neovim (built-in LSP client)

Neovim 0.11 or later, in `init.lua`:

```lua
vim.filetype.add({ extension = { cml = 'camel' } })

vim.lsp.config('camel_ls', {
  cmd = { 'camel-ls', '--stdio' },
  filetypes = { 'camel' },
  root_markers = { '.git' },
})
vim.lsp.enable('camel_ls')

vim.api.nvim_create_autocmd('FileType', {
  pattern = 'camel',
  callback = function()
    vim.bo.commentstring = '// %s'
  end,
})
```

Neovim 0.8 to 0.10, which have no `vim.lsp.config`, start the server from a filetype autocommand:

```lua
vim.filetype.add({ extension = { cml = 'camel' } })

vim.api.nvim_create_autocmd('FileType', {
  pattern = 'camel',
  callback = function(args)
    vim.bo[args.buf].commentstring = '// %s'
    vim.lsp.start({
      name = 'camel-ls',
      cmd = { 'camel-ls', '--stdio' },
      root_dir = vim.fs.dirname(vim.fs.find({ '.git' }, { upward = true, path = vim.api.nvim_buf_get_name(args.buf) })[1]),
    })
  end,
})
```

The graph request can be sent by hand:

```lua
local params = vim.lsp.util.make_position_params(0, 'utf-16')
vim.lsp.buf_request(0, 'camel/graph', params, function(err, result)
  if result then print(result.text) end
end)
```

### 4.2 Emacs

There is no major mode for Camel yet; a minimal one is enough for an LSP client to attach to:

```elisp
(define-derived-mode camel-mode prog-mode "Camel"
  "Major mode for Camel source files."
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (modify-syntax-entry ?/ ". 124b" camel-mode-syntax-table)
  (modify-syntax-entry ?* ". 23" camel-mode-syntax-table)
  (modify-syntax-entry ?\n "> b" camel-mode-syntax-table)
  (modify-syntax-entry ?' "\"" camel-mode-syntax-table))
(add-to-list 'auto-mode-alist '("\\.cml\\'" . camel-mode))
```

With **eglot** (built into Emacs 29 and later):

```elisp
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs
               '(camel-mode . ("camel-ls" "--stdio"))))
(add-hook 'camel-mode-hook #'eglot-ensure)
```

With **lsp-mode**:

```elisp
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(camel-mode . "camel"))
  (lsp-register-client
   (make-lsp-client
    :new-connection (lsp-stdio-connection '("camel-ls" "--stdio"))
    :activation-fn (lsp-activate-on "camel")
    :server-id 'camel-ls)))
(add-hook 'camel-mode-hook #'lsp-deferred)
```

### 4.3 Helix

In `~/.config/helix/languages.toml` (or `.helix/languages.toml` in a project):

```toml
[language-server.camel-ls]
command = "camel-ls"
args = ["--stdio"]

[[language]]
name = "camel"
scope = "source.camel"
file-types = ["cml"]
roots = [".git"]
comment-token = "//"
block-comment-tokens = { start = "/*", end = "*/" }
indent = { tab-width = 4, unit = "    " }
language-servers = ["camel-ls"]
```

Helix highlights with tree-sitter grammars, and there is none for Camel: a file opens without syntax colours, while the language server features work.

---

## 5. Working on the grammar

The TextMate grammar (`editors/vscode/syntaxes/camel.tmLanguage.json`) is kept in step with the lexer's own token table (`include/camel/parse/syntax/token_kinds.def`). Two kinds of tests keep it that way; both run with the default test set (`npm run test`):

```bash
node scripts/test.js test/plans/feat/tools/grammar.plan.toml
```

- `grammar.tables` and `grammar.files` need nothing installed. The first fails when the lexer gains or loses a keyword, the parser a literal token, or the compiler a built-in type name that the grammar does not follow.
- `grammar.scopes.*` tokenise a set of programs with the libraries VS Code itself uses and compare the scopes of every token with golden files under `test/plans/feat/tools/golden/grammar/`. They are **skipped** until `npm install` has been run in `editors/vscode`.

After a deliberate change to the grammar, look at the new scopes, then record them:

```bash
node editors/vscode/scripts/tokenize.mjs test/cases/tools/grammar/functions.cml
node test/tools/run-tests.mjs --update-golden test/plans/feat/tools/grammar.plan.toml
```
