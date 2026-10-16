# Tiles consumes core graph instances (2026-10-16)

Source: `5b7ba74a`.

All extraction entry points now receive the execution context's FactStore.
For an operator whose graph operand types match the concrete call, extraction
obtains `core.instance`. It copies a complete instance into its owned plan only
when every parameter is known. The core store checks graph revisions and the
operator-declaration epoch on reads. Unknown or invocation-specific instances
are instead computed through core instantiation and never written into the
shared graph. Synthetic gradient primitives have no graph anchor and instantiate
directly.

The inspection counters `graph_fact_instances` and `specialized_instances`
describe successful extraction queries, not device kernels or source-cache hits.
The new test observes two graph instances and zero specialized instances for a
static add/transpose function, including repeated extraction. A dynamic function
alternates between [2,3] and [4,1] without populating shared graph facts. Captured
axis 0/1 reductions exercise cached unknown facts followed by call-specific
instantiation. Five CPU/GPU comparisons are exact under nvm, fvm and jit.

Manual execution preceded the automated assertions. Standard builds precede:

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu test/plans/feat/modules/graph_facts.plan.toml
```

This includes the core's seven graph-fact cases: declaration/rewrite invalidation,
dependency cones, persistence and registry/store rules. Those mutation checks
exercise the core store itself; they are not a claim that Tiles executes arbitrary
graph rewrites. No full-project suite or performance benchmark was run.

Remaining work includes consuming general core access maps throughout physical
lowering, graph-region partitioning, symbolic-shape compilation and expert kernels.

Final result: **97/97 passed on each GPU**, with no failures or skips.

The new case also passed manually on Windows under nvm/fvm/jit with generational GC, verification=both, stress allocation interval 3, stress safepoint interval 1 and major collection mode. Each run returned five exact-zero differences.

- [Windows RTX 4060 Ti](2026-10-16-graph-facts-rtx4060ti-tests.txt)
- [Linux RTX 3090, device 4](2026-10-16-graph-facts-rtx3090-tests.txt)
