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
  defect, not a shortcut. A question that several passes or back ends ask (can this node be
  dropped? which overload? what does `as` allow?) has one implementation that all of them call;
  copies drift apart and then disagree.
- **Names say what a thing is.** No `tmp`, `data2`, `handleStuff`, `doIt`; no abbreviations that
  are not already used in the surrounding code. A function's name and signature should make its
  comment short.
- **Small, single-purpose functions.** A function that needs section comments to be followed is
  several functions. Deep nesting is a smell: return early, extract, or use the language's own
  constructs (`loop … while`, `match`, pipelines in Camel).
- **No dead or speculative code.** No commented-out code, unused parameters, "just in case"
  branches, flags nothing reads, or abstractions with one user and no second in sight.
- **Comments state current facts and reasons**, not history ("used to", "after the rewrite") and not a
  narration of the code. Task numbers, test names and measured counts appear in reports and notes,
  never in code or comments.
- **Errors are values or located diagnostics**, never silent fallbacks, magic sentinels or
  swallowed exceptions (see the next section).
- **Tests assert behaviour that matters** (a property, a graph shape, an exact output), not
  counters that move with unrelated changes.
- **Camel code** follows `docs/stdlib-conventions.cn.md` section 6.5 and is formatter output.
- **Documents cite files and symbols, never line numbers.** A line number moves with every edit; cite `src/compile/gir/builder.cpp` and the function, type or test name instead.

- **Documentation moves with the code.** A commit that changes a module's behaviour or invariants updates that module's page under `docs/internals/` (or, until the reorganisation lands, its page under `docs/technical/`) and the affected `docs/reference/` (today `docs/language/`) sections in the same commit. Those pages say what is true now: responsibilities, core data structures, invariants, interfaces, where the code lives — no history, no task numbers, no line numbers.

## Rules That Cross Subsystems
These mistakes recur in every subsystem. Each one yields a program that runs and gives another
answer, so a test that merely passes does not catch it.
- **A refusal is said, never a silent fallback.** A limit, an unsupported case or a failed load
  that quietly takes another path hides the cause and changes the result. Wrong: a cap is reached,
  one log line is written, the work continues serially (or with the next candidate). Right: a
  located diagnostic naming the limit or the case. A fallback exists only where the language
  defines it.
- **"Pure" means pure and cannot fault.** Whether an operator can fault is as much its property as
  its effects; dropping, moving, merging or reordering a node that can fault changes whether the
  program fails. Wrong: delete `let r = 5 / z` because it has no effect. Right: one predicate in
  the core reads the effect and fault properties of the operator, and every pass calls it.
- **What is exported, printed or converted means what it means when it runs.** If the target
  cannot express the semantics, reproduce them with an explicit check, or refuse. Wrong: lower a
  division to a back end's `Div` that drops the zero-divisor fault; print `int?[]` (the grammar
  rejects it) or `(x: int) => int[]` for an array of functions (it parses as another type). Right:
  the same fault, or a refusal naming the operator; a printed type parses back to the same type.
- **No environment-variable switches.** A switch that changes behaviour is invisible in the
  command line and the source, untestable, and usually reads garbage as "off" or "unlimited".
  Wrong: `CAMEL_X_ISA=avx2` picks a kernel; `CAMEL_X_LIMIT_MB=2GB` silently means no limit. Right:
  a documented option or pass parameter that rejects invalid values. The variables the design
  principles name are the only environment input.
- **Catch only what you can handle.** A broad `catch` turns internal errors into user-facing ones
  and hides them. Wrong: `catch (const std::exception &)` reports a logic error or out-of-memory as
  "macro evaluation failed". Right: catch the diagnostic and fault types that step defines; anything
  else propagates as the internal error it is.
- **A cache is keyed by identity.** Not by printed text, an address, or a hash of a part. Types use
  the canonical id; declarations use the declaration plus its arguments. Wrong: key = fact name +
  printed `with` value (two modules' facts collide); key = `Type *` (two spellings of one type
  miss). Right: the canonical id, through the one channel every instance uses. Never cache a
  result computed from something still being defined.
- **Process-wide state is registered per owner and released per owner.** Wrong: the allocator
  keeps one root set that each `Context` overwrites and clears when it is destroyed. Right:
  `register(owner, …)` and `unregister(owner)`, and the holder walks every registration.
- **A generic layer never knows a name.** Behaviour that depends on a name is a declaration or an
  operator trait. Wrong: the compiler special-cases `"stream::tee"`; the module loader tests
  `moduleName == "python"`; an autodiff engine hard-codes `tensor:where`. Right: the module
  declares the property and the layer reads it.
- **Accepted means works.** Wrong: an option that is parsed and never read; syntax the parser
  takes and a later phase calls "unsupported". Right: implement it, or delete it so it is an
  unknown name or a syntax error at the earliest point.
- **Do not hard-code an element type.** Wrong: a gradient kernel reads `dataAs<float>()` for every
  tensor, so an f64 tensor is read as f32; a shape rule answers f32 whatever the input. Right:
  instantiate per supported element type and refuse the rest.

## Architecture Reporting
- During major refactors, identify the primary structural contradiction early.
- If a deeper dependency blocks progress, surface that blocker directly instead of only cleaning peripheral symptoms.
- Record important decisions and rationale in both code comments and docs when the refactor changes semantics.

