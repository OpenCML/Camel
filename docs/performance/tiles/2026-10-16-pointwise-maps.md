# Core pointwise access maps (2026-10-16)

Source: `4e14744e`.

Declared pointwise operands now obtain physical addresses from their core index
instances in serial and fused lowering. The same mapping is composed through
both nodes of an authorized multiply/add contraction. Matrix and pointwise
lowering share the affine address composer and its per-axis bounds checks.
Proven identity storage maps retain the linear coordinate directly.

The supported subset is a direct parallel output domain with unconditional,
nonnegative affine operand maps. Unsupported guards, indirect maps and signed
strides fail explicitly. Internal physical primitives without declarations retain
their built-in indexing. General quasi-affine and masked mappings remain work.

Row regions require canonical broadcast maps. Ordered-reduction epilogues do
too: a cross-row map cannot reuse the current row's reduction register. Both
scheduling and execution-plan validation enforce that condition.

Native mutation checks verify that two operands referencing the same value keep
distinct mappings; transposing one changes its evaluated address, CUDA source
and cache entry. They reject out-of-bounds accesses, nonidentity output domains
and guarded accesses. A reduction epilogue initially fuses, then becomes two
kernels when its input map transposes rows; its stale fused plan is rejected.
These mutation checks inspect/evaluate the physical representation rather than
claiming execution of a user-defined transposed-add operator on the GPU.

Manual native and row-region checks preceded the automated assertions. Standard
builds precede the targeted suite:

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu test/plans/feat/modules/graph_facts.plan.toml
```

The GPU suite covers actual broadcast/layout composition, lexical FMA permissions,
gradients, exact reductions, row regions and matrix edge shapes. No full-project
suite or performance benchmark was run for this batch.

Final result: **97/97 passed on each GPU**, with no failures or skips.

- [Windows RTX 4060 Ti](2026-10-16-pointwise-maps-rtx4060ti-tests.txt)
- [Linux RTX 3090, device 4](2026-10-16-pointwise-maps-rtx3090-tests.txt)
