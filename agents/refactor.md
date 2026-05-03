# Refactor Rules

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
- New `.h/.cpp` files must include the standard file header and a short responsibility comment block.

## Architecture Reporting
- During major refactors, identify the primary structural contradiction early.
- If a deeper dependency blocks progress, surface that blocker directly instead of only cleaning peripheral symptoms.
- Record important decisions and rationale in both code comments and docs when the refactor changes semantics.

