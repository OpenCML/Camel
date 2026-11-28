# progen findings, 2026-11-28: two runtime graphs a legal program builds are refused by the store's own invariants

`23b0204cf9` (`lane/progen`) · build host `zjwei@camel-work` ·
`[load]` 39.50 → 32.43 (1m) during these runs · 2026-11-28.

**These two are reported compiler defects, and the generator does not avoid either shape** —
dropping a construct from `test/tools/progen` because the compiler breaks on it would be making the
test cases dodge the defect, and the next program with that construct would then never be written.

| # | Record | Seed | Program | Fails in | Rule | Check |
|---|--------|------|---------|----------|------|-------|
| 1 | [`seed39-cycle.md`](seed39-cycle.md) | `39` | 21 lines | `main`'s runtime graph | no cycle of inputs (`docs/design/graph-store` 5.4) | `src/kernel/runtime/graph_edges.cpp:800` (`EdgeChecker::findCycle`), reported at `:811`, message at `:1004` |
| 2 | [`seed71-branch-arm.md`](seed71-branch-arm.md) | `71` | 11 lines | `main`'s runtime graph | a node between a BRCH and its JOIN belongs to exactly one arm (`docs/technical/17_graph_rewrite_api_contracts.md` 4.1) | `src/kernel/runtime/branch_arms.cpp:99` (`validateRuntimeBranchArms`), fails at `:358-370`, message at `:88-95` |

Both are `InternalDiag::UnknownInternalError at 1:1` — the invariant is a property of the committed
graph, not of a source location — and both are `RUN_ERROR` on all seven stage prefixes measured
(`none`, `macro`, `inline`, `fold`, `cse`, `algebra`, `dce`, each ending `std::nvm`), so no pass is
implicated: the graph is wrong when it is first committed.

## How they were found

`task-437` brought `test/tools/progen` back to the language the tip has (commit `23b0204cf9`, same
lane):

- `main` declares no return value — a declared one is printed as a line of the program's output, and
  the reference computes the printed lines and not that one, so the 41 programs that printed one
  line too many were fixed by the generator no longer returning a value, not by the reference
  learning to expect it;
- a closure holds the value a name had when it was made, not the name (`model.capture_write_violations`
  discards the three shapes the diagnostic itself names as refused);
- `lo..hi` is a `Range`, not an array.

After that generator, 100 programs run once each on `std::nvm` pass **98 of 100**, and the prefix
matrix (100 programs × 7 stage prefixes) passes **686 of 700 cells**. The two programs left are the
two records above — 7 red cells each, in every prefix.

## Coverage the generator now does not have, stated rather than hidden

The language allows a value that survives across calls to a closure in two ways only: a `Ref`
(design principles: a closure may capture a handle, because a handle is a value) or a loop's carried
state / `fold`. The shape `test/tools/progen` used to write — a function that owns a `var`, makes a
closure over it, and lets each call see the previous call's write — is refused by the front end now
(`SemanticDiag::ClosureAssignsCapturedName`), and the generator therefore no longer produces it.
That is an honest loss of coverage in the generator, not a change to the language: no `Ref`-based
replacement was written here, and the finding records above do not depend on it.

## Not touched

`known_fail`, `baseline.json`, the golden files and `test/plans/**` are unchanged by this lane; no
assertion was edited to make a red green. The `progen_check` entry of
`test/plans/feat/opt/progen.plan.toml` (25 programs × 41 pipelines) was **not** re-run after this
generator change, and its `desc` still names `generate.VERSION 8` — that plan belongs to the
decision-maker's side and its reading must be re-recorded before it is quoted as green.