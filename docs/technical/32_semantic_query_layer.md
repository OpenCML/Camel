# The Semantic Query Layer

This note describes how a tool asks the compiler about a source text by position: what a name
is, its type, where it is declared, where it is used, what is visible at a point. The language
server (`tools/lang-server`, [../editor-support.en.md](../editor-support.en.md)) is its first
user. Positions and origins are those of
[10_source_mapping_and_debug_locations.md](10_source_mapping_and_debug_locations.md); the
semantic side tables of
[11_semantic_models_and_reverse_mapping_foundation.md](11_semantic_models_and_reverse_mapping_foundation.md)
serve the debugger and are not used here (section 6 says why).

## 1. Where the information is

| Question | Who knows | Kept after the compilation? |
|----------|-----------|-----------------------------|
| Which declaration a type name means | GCT builder (`typeScope_`, the module's imported types) | No: the `Type *` replaces the name |
| Which declaration a value name means | GIR builder (`nodeScope_`, `graphScope_`, `templateScope_`, the module's imported entities) | No: an edge replaces the name |
| The type of an expression | GIR builder (the type of the node it builds) | On the node, without a way back from a source range |
| Which overload a call chose, which instance of a generic function | GIR builder (`visitLinkNode`, `resolveGenericCall`) | Only as the callee of the node |
| The body of a generic function | Built once per instance; a function that is never called is never built | Nothing for an uninstantiated function |
| Names and ranges of declarations | Parse tree | AST anchors exist for some (`BindingName`, `FuncName`), not for parameters |
| Anything in a file with a syntax error | Only the parse tree ANTLR recovers | The compiler stops before the AST |
| Anything in a module with a GCT-stage error | Parse tree, AST | The GIR is not built |
| A statement that failed in the GIR builder | The statements around it are built ([30](30_diagnostics_reporting.md), section 3) | Nothing for the failed statement |

So a single compilation resolves everything a tool wants and then forgets it, and it says
nothing about text it could not build, which is the usual state of a text being edited.

## 2. Design

Two sources, joined by source range:

1. **The syntactic index** (`camel/service/semantic/syntax_index.h`). One walk over the parse
   tree of a file, also the tree recovered from a file with syntax errors
   (`CamelParser::syntaxTree()`): every declaration with the exact range of its name, the
   lexical scopes, every reference (`a::b::c` with its segments), member names after a dot,
   keys of struct literals and types, the tokens, the comment lines above a declaration.
   References are resolved by the scoping rules as far as text allows.
2. **Facts from the builders** (`camel/compile/semantic_recorder.h`). The GCT and GIR builders
   report to a `SemanticRecorder` when the context has one: the type a type expression stands
   for, each bound name with its type, the type of each expression, what each name reference
   resolved to (a declaration, a set of functions, a generic function, an operator group),
   what each call resolved to (the overload, the instance, the operator), each function body
   built (once per instance). A fact is keyed by the origin of the GCT/AST node, so the
   recorder needs no knowledge of the trees.

`analyze()` (`camel/service/semantic/model.h`) compiles a text exactly as `camel check` does,
with a recorder attached, builds the syntactic index of the entry file and of every module
compiled from source, and attaches each fact to the declaration or reference with the same
range. A fact that matches nothing is dropped: the model then says less, never something
wrong. The result (`SemanticModel`) holds only texts, ranges and strings; the compilation is
gone when `analyze()` returns.

Where the compiler said something, that is the answer. Where it did not, the scoping rules
give one, marked as such (`RefFacts::resolved` is false). Rename, which must not guess, uses
the difference (section 4).

The layer is in `libcamel` (`src/service/semantic`), knows nothing of the language server
protocol and is called as a library: `analyze({path, text})`, then the queries of
`SemanticModel`. Positions are byte offsets into UTF-8; `LineIndex` converts to the protocol's
UTF-16 columns and from the compiler's code point columns.

### Changes to the compiler

- `Context::setSemanticRecorder` (default null).
- GIR builder: `visit()` reports the type of what it built; reports in `visitNRefNode`,
  `buildFuncBody` (parameters, the function), `visitDRefNode`, `visitOperand`,
  `visitLinkNode`; a table of declaration origins by scope, filled only while recording.
- GCT builder: one report at the end of `visitType`.
- `CamelParser::syntaxTree()`: the tree of the last parse whatever its outcome.
- `Module::importedRefs()`, `importedNamespaces()`: read access for listing what is visible.
- `Namespace::forEach` (const) did not compile when instantiated; fixed.

Nothing that is built changes. Without a recorder a report site is a test of a null pointer.

## 3. What it answers

- **Hover**: the declaration as written (the header of a function, `let x: T`, a type alias),
  types in source spelling (`int`, `float`: the compiler's `i64`, `f64` are mapped back), the
  comment lines directly above a declaration, the instances of a generic function, and at a
  call the instance or overload that call resolved to; the type of a field, a literal, an
  operator call; the signatures of library functions.
- **Definition, references, highlights**: locals, parameters, functions (the overload a call
  chose), type aliases, type variables, imported names (to the declaration in the other
  file), fields (uses on receivers of the same type).
- **Outline**, **completion** (names in scope, fields after a dot, module members after
  `::`, keywords read from the lexer's vocabulary), **signature help**, **semantic tokens**,
  **rename**.

### Generic functions

The body of a generic function is built once per instance, so one range gets several facts.
They are kept: a local of `func id(x: typeas T)` has the types `int | string` when the function
is called with both, the declaration lists its instances, and the callee of each call carries
the type of the instance that call uses.

### Overloads

A name reference reports every function of that name; the call then reports the one chosen.
Definition and references use the chosen one where there is a call.

## 4. Source with errors

| State of the text | What is available |
|-------------------|-------------------|
| Compiles (warnings allowed) | Everything |
| A statement fails in the GIR builder | Everything for the other statements; in the failed one, names resolve by scope, without types |
| GCT-stage error (unresolved type, unsupported construct) | Syntactic index; types of declarations from the last text that compiled |
| Syntax error | Syntactic index of what ANTLR recovered; types of declarations from the last text that compiled; module members after `::` for the imports that can still be read |

"From the last text that compiled": `AnalysisOptions::previous` names an earlier model of the
same file. A declaration without facts takes the types of the declaration with the same name
at the same place (enclosing functions, rank among equal names) in it. That is what makes
`p.` complete to the fields of `p` while the line is being typed.

**Rename** is allowed only when every reference is certain: the file compiles; the name is
declared in this file, is not exported (its importers are unknown) and is not `main`; every
value reference spelt like the name was visited by the compiler, and the compiler and the
scoping rules agree on it. A reference in code the compiler did not build (a generic function
that is never called) makes it refuse, with the reason. The new name must not be declared or
used where the old one is visible.

## 5. Cost

Measured with `camel-ls --time <file>` (model: compile, record, join; compile-only: the same
without a recorder; syntax-only: parse and index), release build, a shared machine with other
builds running, so the figures vary by a factor of two between runs:

| File | Lines | Model | Compile only | Syntax only |
|------|------:|------:|-------------:|------------:|
| generated, 100 functions | 505 | 7–11 ms | 10–12 ms | 3–5 ms |
| generated, 400 functions | 2005 | 50–70 ms | 47–62 ms | 19–31 ms |
| generated, 4000 functions | 20005 | 0.55–1.1 s | 0.43–0.62 s | 0.24–0.30 s |
| `benchmarks/resnetbn/model.cml` | 275 | 65–240 ms | 68–180 ms | 55–160 ms |
| `benchmarks/lora/lora_mlp.cml` | 201 | 39–68 ms | 52–68 ms | 43–60 ms |

Recording and the join are within the noise up to a few thousand lines and add roughly a third
at 20000. The cost of the report sites without a recorder was not measured against a build
without them; each is one branch on a member that is null.

There is no incremental compilation. The server recompiles the whole text when a request needs
the model or when no message has arrived for 150 ms, so a burst of edits is compiled once, and
a request cancelled while it waited is not computed.

## 6. Limits and open points

- **Imported modules are read from disk**, not from the editor's unsaved text.
- **References are those of one compilation**: the entry file and what it imports. Who
  imports a file is not known, which is why exported names cannot be renamed and why
  references asked from a library file miss its users.
- **Fields** are found by receiver type text, not by a declaration: a struct type has no
  identity beyond its fields. Rename of fields is refused.
- **Type references** are resolved by scope only; the GCT builder reports the type an
  expression stands for, not the declaration.
- **Pattern bindings** of `match` are indexed from the syntax; the builder does not report
  them.
- **The AST/GCT/GIR semantic side tables** of note 11 are keyed for the debugger (node
  stable ids, semantic parts of a node). They do not record name resolution, which is what a
  query by position needs; the recorder is a separate, optional channel so that ordinary
  compilations pay nothing for it.
- **Several facts for one range** from nested constructs that share a span are resolved by
  taking the last report (the outermost node). No wrong type has been observed; a finer rule
  would need the builders to say which node is the expression's value.
