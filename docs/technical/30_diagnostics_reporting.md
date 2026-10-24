# Diagnostics: Presentation, Several Errors per Run, Catalog

This note describes how a diagnostic is built, shown and catalogued, and how one compilation
reports several errors without reporting their consequences. The positions a diagnostic points
at come from the origin chain described in
[10_source_mapping_and_debug_locations.md](10_source_mapping_and_debug_locations.md); the
command-line options are listed in [../cli.md](../cli.md) ("Diagnostics"); the diagnostics
themselves in [../diagnostics.md](../diagnostics.md).

Choices that belong to the language author are collected in section 7, marked in the code with
`DESIGN-QUESTION(diagnostics) Qn`.

## 1. What a diagnostic carries

`Diagnostic` (`include/camel/core/error/diagnostics.h`):

| Field | Meaning |
|-------|---------|
| `type`, `specific`, `severity` | The 32-bit code: type (8 bits), severity (4), number (20). |
| `name` | `<Type>::<Name>`, the name of the catalog entry. |
| `message`, `suggestion` | The entry's templates with the report's values filled in. |
| `range` | The primary location: a line/column range, or a span or origin resolved when shown. |
| `related` | Secondary locations (`DiagLabel`): a range and a sentence fragment about it. |
| `notes` | Remarks without a location. |
| `stage` | `parse`, `typecheck`, `pass:<name>`, `run` or `internal`. |
| `promoted`, `suppressed` | Set by the warning policy (section 5). |

A secondary location is attached before the diagnostic is committed
(`DiagnosticBuilder::relatedOrigin / relatedSpan / related`, `note`), or afterwards to every
error recorded since a mark (`Diagnostics::addRelatedToErrorsFrom`), for context that is only
known where the failing work was requested: the call that asked for an instance of a generic
function is attached to the errors found in the instance's body.

Lines and columns are 0-based inside the compiler and in JSON, 1-based in text. A column counts
code points. (Spans store byte offsets into the UTF-8 text, which is what the lexer counts in;
the column of a position is counted from the start of its line when it is shown.)

- A line wider than 120 columns is cut to a window that keeps the start of the range in view,
  with `...` on each cut side.
- Without a position only the header (and the file, when known) is printed.
- Without the file's text in the source context the file is read from disk; if that fails, the
  `-->` line stands alone.

### The JSON `data` object

`--output-format json` prints a diagnostic as an object with the rendered `message` and a `data`
object; tooling reads `data`. The command line produces it in one place (`Diagnostic::toJson`)
and every code gets the same envelope — no code adds a key of its own, so there is no per-code
schema to grow:

| Key | When | Meaning |
|-----|------|---------|
| `stage` | always | Where the diagnostic was raised: `parse`, `typecheck`, `compile`, `pass:<name>`, `run` or `internal`. For a pass it is the specification as written on the command line, so `pass:hash::nosuchpass` names the module and the pass. |
| `name` | always | `<Type>::<Name>`, the catalog entry. |
| `moduleName` | always | The name the diagnostic carries for its module: the compilation unit's — the declared `module` name, or `main` for an entry file that declares none — and, for a diagnostic located only by a position (a run-time fault, a tag warning), the stem of `modulePath`. It is `""` exactly when `modulePath` is. |
| `modulePath` | always | The file the diagnostic is *located* in; `""` exactly when it has no source position. |
| `suggestion` | when non-empty | The help text. |
| `notes` | when non-empty | The remark lines. |
| `relatedElided`, `promotedFromWarning` | when true | See section 5. |

The two module fields describe the diagnostic's own file, never a module its message happens to
name. `ModuleNotFound` at an import is located at the import: `data` names the importing module
and its file, and the module that was not found is `{0}` of the message. A run-time fault of
`fs`, `json` or `re` is located at the call in the program's file, and the path the fault is
about is in the message. A diagnostic the command line itself raises — an unknown pass name, a
pass a module does not provide, a pass that gives up, the passes written after one that consumes
the graph — has no file at all, so both are `""`; the same diagnostic prints `():?:?:` in the
short text form. Its subject is not lost: `stage` carries `pass:<module>::<pass>` (or
`pass:<name>`), and the message carries the rest. A consumer that needs the module or pass of a
command-line diagnostic reads `stage`; it must not invent one from `moduleName`.

The language server builds its own `data` from the semantic model, and it carries only `name`
(plus `suggestion` and `notes` when non-empty): no `stage`, `moduleName` or `modulePath`.

## 3. Several errors in one compilation

The builders already skipped a statement that failed and went on (`BuildAbortException`); the
command line stopped at the first error because it limited the diagnostics container to one.
`--max-errors` (default 20) is now the limit, and what has to be prevented is the report of
errors that only follow from an earlier one.

**Syntax.** Every independent syntax error of a file is reported. The parser
([34_handwritten_parser.md](34_handwritten_parser.md), section 6) reports an error once, says
nothing more until it has resynchronized at the next statement, and goes on from there, so a
second diagnostic is a second mistake. (Under ANTLR only the first was reported: its recovery
guessed at a missing or superfluous token and what it reported afterwards mostly followed from
the guess.) Lexical errors are independent of one another and are all reported; a file with a
lexical error gets no syntax error, because the parser saw the tokens without the offending
text.

**Stages.** A file with syntax errors is not compiled further. A module whose GCT has errors
(unresolved types, constructs that are not supported, literals that cannot be cast) is not
built into a graph, so the errors of the GIR builder (unresolved names, calls that do not
match) appear once the GCT errors are fixed. This is as before; within each stage all
independent errors are reported.

**Statements (GIR builder).** `visitExecNode` builds the statements of a block one by one.
One that fails is skipped; the builder's state (current graph, scopes, type bindings, the
`sync`/`wait` flags) is put back to what it was before the statement, and the next statement
is built. When the block is done it fails as a whole if any statement failed. So:

- one statement reports at most one error (the first the builder meets in it);
- a block used as a value (a branch arm, a function literal) with a failed statement makes
  the enclosing statement fail without a second report;
- a function whose body failed is not analysed further: no "effect not executed" warnings
  are computed for a body with missing statements, and the missing `main` of an entry module
  is not reported on top of other errors.

**Poisoned names.** What a failed declaration would have bound is remembered
(`poisonedNames_`, by scope), and a reference to such a name fails without a report: in

```camel
let r = add(nope, 2)     // error: unresolved reference 'nope'
let s = add(r, r)        // not reported: r is poisoned; s is poisoned in turn
println(s)               // not reported
println(other_missing)   // error: independent
```

two errors are reported instead of four. The same is done for

- a function without a declared return type whose body failed: a call of it is not reported
  as a call of an incomplete function (`failedBodies_`);
- a type declaration that failed, and the names of an import that failed (`poisonedTypes_`
  in the GCT builder); when an import of a whole module fails, which type names it would have
  brought is unknown, and no unresolved type is reported for that module at all;
- a module that imports a module in error: the imported module's errors are shown first, the
  importing module gets one `ImportedModuleHasErrors` at the import, and nothing for the uses.

**Duplicates.** An error with the code, position and text of one already recorded is not
recorded again (`Diagnostics::add`). The body of a generic function is checked once per
instance; what does not depend on the type variables fails identically in each.

**Run time.** A fault ends the run at the first one, as before: the run-time sink keeps a
limit of one whatever `--max-errors` says.

### Known under-reporting and over-suppression

- A second independent syntax error in a file is seen only after the first is fixed.
- A second error inside one statement (`add(nope1, nope2)`) is seen only after the first is
  fixed.
- GIR-stage errors of a module are hidden while it has GCT-stage errors.
- A poisoned name hides a real mistake that involves it: in `let x = nope` followed by
  `x.no_such_field`, the second is reported only after the first is fixed. A name is poisoned
  by scope: a name in an inner scope that shadows a poisoned outer one is declared normally,
  but a *misspelt* use of another name that happens to equal a poisoned name in an enclosing
  scope is not reported.
- After a failed whole-module import every unresolved type of the module is silent, also one
  that is simply misspelt.

None of these lets an erroneous program compile: suppression only applies after an error was
recorded, and a module with an error is never run.

## 4. Run-time faults

A fault is raised with `throwRuntimeFault(code, args...)` and reported at the node that was
executing. The catch-all `RuntimeDiag::RuntimeError` says only "An error has occurred: ...";
the built-in operators and the modules `fs`, `json`, `re`, `time`, `math` (and single sites of
`process`, `random`, `hash`, `array`, `statistics`) now raise codes of their own with the values
involved (`InvalidNumberText`, `FormatFailed`, `OperandTypeUnsupported`, `FieldNotFound`,
`InvalidArgument`, `FileOpenFailed`, `FileSystemError`, `RegexInvalid`, `TimeFormatFailed`).

Only the code and the message of a fault changed. What is a fault and what is an error value is
decided by the design principles (3.9): no fault was turned into an error value, and nothing
that did not fault before faults now.

## 5. Warnings

The warning policy (`WarningPolicy`, set from `-Wno-<name>`, `-Wnone`, `-Werror`,
`-Werror=<name>`, `-Wno-error=<name>`) is applied where a diagnostic is stored
(`Diagnostics::add`) and, for the warnings that are built without a container (the
command-line warning about passes that never run), by `applyWarningPolicy`.

- A warning switched off is not stored; the copy handed back is marked `suppressed`.
- A warning made an error gets severity `Error` and `promoted`; its code keeps the warning
  severity bits, so it is still the same catalog entry. Being an error, it stops the
  compilation like any other; one raised by a pass ends the pipeline after that pass.
- `-N` / `--max-warning` limits how many warnings are printed; the rest are counted.

There is no way to switch a warning off inside a source file. That would be syntax, which is
for the language author to decide (`DESIGN-QUESTION(diagnostics) Q3`).

## 6. Catalog

`scripts/diagnostics-catalog.mjs` reads the enumerators
(`include/camel/core/error/diagnostics/messages/*.h`) and the message tables
(`src/core/error/diagnostics/messages/*.cpp`) as text.

- `--check`: every enumerator has a table entry whose name is the enumerator, a message and a
  suggestion field; no value is used twice within a type; severity bits are valid; warning
  names are unique across types (the command line names them without the type). Two branches
  that each took "the next free number" produce two enumerators with one value, which compiles
  and silently loses one table entry; this check is what finds it at the merge.
- `--write`: generates `docs/diagnostics.md`, with an example per code taken from the
  negative tests (the first test, in plan order, that expects the diagnostic and whose
  program has at most 40 lines). A change of the tests can change an example: run `--write`
  again.
- `--verify-doc`: the document is what `--write` would produce.
- `--coverage`: lists the codes that no plan, golden file or scripted case mentions. It is a
  report and never fails.

`--check`, `--verify-doc` and `--coverage` run in the test plan `compile.diag_catalog`. `camel --explain <name or code>` prints
an entry from the tables compiled into the binary.

New codes: take a number that `--check` accepts, add the table entry, run `--write`. Blocks in
use: syntax `0x01`-`0x0B`; run time `0x30`-`0x3B` for operator and module faults; semantic
`0x80` and up for multi-error reporting.

### Which codes carry a fix, and how the first set was chosen

The registry's seventh column says, for every declaration, `edit` when the report attaches a
machine-applicable change, or `none: <cause>` when it does not. Building that column for all 260
codes is the catalog's work; deciding which codes can honestly carry an `edit` is not, so it was
measured. This is the survey of 2026-10-24 on `769b358a9`, and it is the reason the first set is
the one it is.

What the tree said before anything was added: **260 codes, 256 with help, 4 without** (`UnknownInternalError`,
`MissingToken`, `UnexpectedToken`, `InvalidArgument`), and **not one with a machine-applicable
change** - the mechanism did not exist, and `suggestion` was the only advice a code could give.

How often a code fires was measured twice, because the two answers differ in kind.

- **Committed programs.** All 2662 `.cml` files under version control, each compiled on its own
  with `camel <file> --error-format json --max-errors 50`: 2568 diagnostics, 237 distinct codes,
  39 runs that failed for a reason of their own. The top is module and name resolution, because
  most of those files are library fragments that were never entry points:
  `NativeDeclarationNotLoaded` 357, `UnresolvedTypeReference` 228, `NoMatchingFunction` 201,
  `EntryModuleMissingMain` 177, `UnresolvedReference` 155, `RuntimeError` 143,
  `IgnoredSideEffect` 84, `EntryArgumentMissing` 79, `EffectNotExecuted` 53,
  `UnusedFallibleValue` 51, `ModuleNotFound` 50, `ArgumentsRejected` 43, `DivisionByZero` 42,
  `LiteralNotRepresentable` 34, `TagHasNoReader` 34, `CapturedWriteInAsyncFunction` 33,
  `ExitInUnusedBranch` 31, `MissingToken` 29, `ConstructNotSupported` 27,
  `InvalidEscapeSequence` 27, `UnexpectedToken` 25.
- **Mutated programs**, which is what a person typing actually produces. 3000 mutants of 98
  files (`test/cases/parse/*`, `examples/*`, `test/cases/format/*`), seeded so the run repeats:
  one to three mutations each of "delete a line", "insert a junk character", "delete a span" and
  "truncate the file": 3476 diagnostics, 52 distinct codes. The top is entirely the syntax and
  lexical layer: `UnexpectedToken` 844, `MissingClosingDelimiter` 637,
  `EntryModuleMissingMain` 512, `UnresolvedReference` 242, `UnmatchedClosingDelimiter` 225,
  `UnclosedStringLiteral` 195, `MissingComma` 116, `MissingToken` 113, `UnterminatedComment` 77,
  `InvalidNumberFormat` 64, `KeywordAsIdentifier` 47, `ComparisonNeedsSpace` 47,
  `IllegalCharacter` 36, `ModuleNotFound` 35, `ReturnTypeMismatch` 24, `MissingInitializer` 22,
  `MissingThen` 21, `WhenAfterBody` 18, `AngleCloseAfterSpace` 14, `LineStartsWithOperator` 5.
- **What the tests expect** was counted as well - 788 `expect_diagnostic_name` and 208
  `expect_diagnostic_code` - and it is the wrong ranking to use here: a code a test names often
  is a code the tests were written around, not one a writer meets. (9 codes are named by no test
  at all; that is `--coverage`'s business, not this column's.)

The set to add fixes to first is therefore the head of the mutation ranking, filtered by whether
the repair is determinate: the parser named the single missing token, or the text to remove or
replace is the text the message is about. Fourteen codes qualified - `IllegalCharacter`,
`InvalidNumberFormat`, `UnclosedStringLiteral`, `UnterminatedComment`, `MissingClosingDelimiter`,
`UnmatchedClosingDelimiter`, `MissingThen`, `ExtraSemicolon`, `MissingComma`, `MissingToken`,
`WhenAfterBody`, `ComparisonNeedsSpace`, `AngleCloseAfterSpace`, `LineStartsWithOperator` - and
they are 3237 of the 3476 mutants' diagnostics, 93%.

The codes those rankings put higher were left without a fix on purpose, and the column says why:
`UnresolvedReference`, `UnresolvedTypeReference`, `NoMatchingFunction` and the rest of the
resolution family are `choice` - the compiler does not know which name or type was meant, and a
fix that picks one is a wrong change a tool would apply without asking, which is worse than a
sentence. `EntryModuleMissingMain`, `ModuleNotFound` and `NativeDeclarationNotLoaded` are `context`
- the repair is a second file or a second place, not a range in this one. `RuntimeError` and the
run-time faults are `runtime`. Measured with `notes/sweep.mjs` and `notes/audit.mjs`, both outside
the repository: the first compiles a file list and mutates one, the second merges the counts with
the registry.

## 7. Questions for the language author

- **Q1. Syntax errors after the first.** Settled by the hand-written parser: every
  independent syntax error is reported (section 3).
- **Q2. One error per statement, and silence for poisoned names.** Section 3 lists what is
  not reported until an earlier error is fixed. The alternative for poisoned names, a
  "poisoned type" that flows through expressions so that independent errors in the same
  statement are still found, needs every check of the builder to accept it.
- **Q3. Suppressing a warning in the source.** Not implemented; needs syntax.
- **Q4. `-Werror` and warnings raised by passes.** A pass warning made an error
  (`ConstantExpressionFaults` under `-Werror`) ends the pipeline before the program runs, so
  with `-Werror` a program that would only fault if a branch were taken does not run at all.
- **Q5. Names of the syntax diagnostics and wording of the messages** (`MissingThen`,
  `AssignmentInCondition`, `KeywordAsIdentifier`, ...): they describe mistakes against the
  current grammar and should follow it when it changes (`then` in particular).
- **Q6. A fault raised by `process:exit`.** `<exit> operator invoked` is still the catch-all
  `RuntimeError`; whether leaving the program is a fault at all is not for this work to decide.
