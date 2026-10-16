# Physical integer expressions and branch arms (2026-10-16)

Source revision: `9191cad0`, including branch-arm support from `0fba69cb`.

The physical scalar representation now distinguishes i32/u32 registers and
buffers from public tensor element types. Wrapping arithmetic, signed/unsigned
extrema and comparisons, bit operations and equal-width bitcasts have matching
host evaluation and CUDA emission. Logical right shifts explicitly mask the
count to five bits. Unsupported division and numeric integer/float conversions
are rejected rather than inheriting C++ undefined behavior or implicit casts.

The structured conditional has two independently scoped arms. Validation visits
both arms for memory/type errors, divergent barriers and early returns. Tests
include sibling-scope leakage, escaped locals and a return in the else arm of a
kernel with a barrier.

Manual native checks exercise signed wrapping, unsigned underflow, multiplication,
large shift counts, exponent extraction and payload-preserving bitcasts across
zeros, subnormals, infinities and NaNs. A manual CUDA differential probe checks
9,728 host/device results on each GPU. Integer bit patterns are compared exactly,
including results that encode floating NaNs; they do not use the floating
arithmetic test's NaN-classification exception.

Validation uses standard builds and the targeted suite:

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu
```

This batch prepares the row-region lowering, which still uses its previous
emitter. It does not introduce public integer tensor operators or claim a
throughput improvement. No full-project suite was run.

Final result: **87/87 passed on each GPU**, with no failures or skips.

- [Windows RTX 4060 Ti](2026-10-16-integer-ir-rtx4060ti-tests.txt)
- [Linux RTX 3090, device 4](2026-10-16-integer-ir-rtx3090-tests.txt)
