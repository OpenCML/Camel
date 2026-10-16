# Unified reduction traversal and core maps (2026-10-16)

Source: `195767a9`.

Axis, full-tensor and sum-to reductions now share a physical traversal descriptor
(count plus operand address). Full sum/mean and sum-to validate the core domain,
output projection, affine operand map and numerical law before scheduling.
Serial lowering no longer implements a separate sum-to indexing algorithm.
Reduction coordinates advance lexicographically, preserving the declared
row-major contribution order. Full reductions use f64 accumulation and results;
sum-to uses f32 accumulation. Canonical full and axis maps keep compact formulas.

Native mutation tests enumerate all contributions for a `[2,3,2]` source,
including sum-to `[3,1]` with nonadjacent reduction dimensions. They compare
ordinary and permuted operand addresses, reject incompatible dimension roles,
and check generated source/cache changes. These are physical-representation
checks; synthetic permuted operators are not executed on GPU in this batch.

After manual native checks, standard builds and the targeted suite passed on both
GPUs. The GPU suite covers registered operators, scalar reductions, gradients,
row regions, exact reduction bits and core graph facts across VM backends.

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu test/plans/feat/modules/graph_facts.plan.toml
```

- Windows RTX 4060 Ti: **97/97**, no failures or skips.
- Linux RTX 3090, device 4: **97/97**, no failures or skips.

No full-project suite or performance benchmark was run. Only direct output
projections and unconditional nonnegative affine reads are supported. General
symbolic/guarded domains, automatic GPU regions and expert kernels remain work.

- [RTX 4060 Ti results](2026-10-16-general-reductions-rtx4060ti-tests.txt)
- [RTX 3090 results](2026-10-16-general-reductions-rtx3090-tests.txt)
