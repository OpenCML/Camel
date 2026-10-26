# Refactor Rules

> **Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../docs/design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.

## Design Direction
- Prefer large, structural, end-state-oriented redesigns over incremental compatibility-preserving migration when a subsystem boundary is fundamentally wrong.
- Do not optimize for short-term safety or staged coexistence if that leaves behind temporary scaffolding, bridge layers, or cleanup debt.
- Treat transitional adapters, compatibility shims, dual-track APIs, and temporary fallback code as a last resort.
- If a compatibility bridge is unavoidable, keep it minimal, explicitly documented, and scheduled for deletion in the same refactor stream.

## Implementation Quality
- Favor elegance and correctness first.
- Maintain strong commentary and documentation discipline in English.
- Add comments for non-trivial or opaque logic, especially around invariants, ownership, and edge cases.
- Keep headers in `include/` and implementations in `src/`.
- Avoid implementation logic in headers unless strictly necessary.
- If a single file grows beyond 800 lines, evaluate decomposition.
- Every hand-written source file starts with the two-line header (`// Copyright (c) the OpenCML Organization` / `// SPDX-License-Identifier: MIT`), then a blank line and a short responsibility comment; `node scripts/headers.mjs --fix` writes it and `--check` verifies it. No author, dates or funding lines.

## The Bar a Change Must Meet
Code is read far more often than it is written, and this repository will be public. A change is
returned for rework when it misses any of these, however well it tests:
- **One way to do a thing.** Reuse the existing helper, rule or construct; a second near-copy (a
  per-type duplicate, a parallel code path, a local re-implementation of a library function) is a
  defect, not a shortcut.
- **Names say what a thing is.** No `tmp`, `data2`, `handleStuff`, `doIt`; no abbreviations that
  are not already used in the surrounding code. A function's name and signature should make its
  comment short.
- **Small, single-purpose functions.** A function that needs section comments to be followed is
  several functions. Deep nesting is a smell: return early, extract, or use the language's own
  constructs (`loop … while`, `match`, pipelines in Camel).
- **No dead or speculative code.** No commented-out code, unused parameters, "just in case"
  branches, flags nothing reads, or abstractions with one user and no second in sight.
- **Comments state current facts and reasons**, not history ("used to", "after the rewrite") and not a
  narration of the code. Task numbers appear in reports and notes, never in code or comments.
- **Errors are values or located diagnostics**, never silent fallbacks, magic sentinels or
  swallowed exceptions.
- **Tests assert behaviour that matters** (a property, a graph shape, an exact output), not
  counters that move with unrelated changes.
- **Camel code** follows `docs/stdlib-conventions.cn.md` section 6.5 and is formatter output.
- **Documents cite files and symbols, never line numbers.** A line number moves with every edit; cite `src/compile/gir/builder.cpp` and the function, type or test name instead.

- **Documentation moves with the code.** A commit that changes a module's behaviour or invariants updates that module's page under `docs/internals/` (or, until the reorganisation lands, its page under `docs/technical/`) and the affected `docs/reference/` (today `docs/language/`) sections in the same commit. Those pages say what is true now: responsibilities, core data structures, invariants, interfaces, where the code lives — no history, no task numbers, no line numbers.

## Architecture Reporting
- During major refactors, identify the primary structural contradiction early.
- If a deeper dependency blocks progress, surface that blocker directly instead of only cleaning peripheral symptoms.
- Record important decisions and rationale in both code comments and docs when the refactor changes semantics.

