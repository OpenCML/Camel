# Refactor Rules

Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first. It is normative and prevails over code, tests and other documents. Do not make an undecided semantic choice ad hoc.

## Design and structure

- Prefer the correct end state over a compatibility-preserving series of partial designs. Do not leave parallel implementations, adapters, bridge layers or migration scaffolding without a deletion path.
- One fact has one source and one implementation. Read operator, resource, effect, type and declaration properties rather than comparing module or operator names.
- Reuse the existing generic mechanism before adding a special case. A generic layer never knows a concrete module, operator or type name.
- Name responsibilities precisely. Keep functions small, interfaces coherent and storage ownership explicit. Decompose files that exceed about 800 lines when that improves the boundary.
- Delete dead and speculative code, unused flags and unreachable compatibility paths. Accepted syntax or options must work; otherwise reject them at the earliest meaningful point.
- Headers declare interfaces in `include/`; implementations live in `src/`. Every hand-written source file starts with the two-line SPDX header and a short responsibility comment. `node scripts/headers.mjs --check` verifies this.

## Correctness invariants

- Errors are values or located diagnostics, never silent fallbacks, magic sentinels or swallowed exceptions. Catch only what the layer can handle.
- Purity includes the inability to fault. Dropping, moving or merging a faulting computation changes observable behavior and is not a legal optimization.
- Exported, printed and converted values have the same meaning as execution. If the target cannot express the semantics, emit the necessary check or refuse with a location.
- Switches belong to documented options or pass parameters, not implicit environment variables.
- Caches are keyed by identity, not text, address or a partial hash. Process-wide state is registered and released per owner.
- GC and graph lifetimes are explicit. Objects stored as live objects must actually be alive, and C++-only holders of programs or graph versions must pin and unpin them correctly. Do not hide a lifetime defect by rooting everything forever.
- Do not hard-code an element type. Instantiate supported types exhaustively and reject unsupported combinations.

## Rewrite and transform rules

- Preserve order and ownership through every edit. A replacement, inline or extraction carries the relevant data, synchronization and order edges; low-level APIs cannot silently bypass them.
- A refusal is relaxed only after the rejected class is proven correct. Include the reading that proves it; fix the graph producer rather than deleting the guard that exposed the defect.
- Do not duplicate reachability, control-region, refusal or type queries. Use one structured diagnostic with a stable name and location rather than integer-coded or test-only side channels.
- A transposition law's head uses type variables for every position. Forward and transposed rules have different type-argument counts; do not concretely pin a position that the walk cannot supply.
- Match types bind their case-local names. A concrete subject with no selectable case is `TypeFunctionNotApplicable`, not an uninstantiated generic type.
- Graph and source round trips preserve semantics, region structure and all tags needed by later passes. Readability is secondary to a correct recompilation.

## Comments and documentation

- Comments state current facts and invariants, not history, task numbers, dates, measured counts or links to old handover pages.
- Cite symbols as `path/file.cpp#Symbol`, not line numbers. Do not repeat the same rule in code, review pages and generated documents; keep one authoritative source.
- Update the module's technical page and affected language reference in the same change as its behavior. Describe what the code does now, not what used to happen.
- Keep implementation commentary in English. Do not add comments that merely narrate the code.

## Tests and acceptance

- Each change includes relevant behavioral tests. Test a property, graph shape, exact diagnostic or output, not a count that can change for unrelated reasons.
- Give each structural step a checkable outcome and identify what is done, in flight or not started. A red reading locates the next issue; do not replace it with "basically working".
- Cover every affected backend and the C++ translator, ONNX, autodiff and macro expansion when they consume the changed construct. Unsupported paths must refuse explicitly.
- Run the relevant plans and the repository's static checks. Use actual execution to confirm behavior, not only compilation or a textual search.
- Before finishing, review the whole diff for unrelated reverts, duplicated logic and stale comments. The common purpose of the staged set must be clear.
