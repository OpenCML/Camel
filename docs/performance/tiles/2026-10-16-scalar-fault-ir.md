# Structured scalar conditions and device faults (2026-10-16)

Tiles source revision: `3e3b1146`.

Scalar chains now lower to typed statement trees. Floating comparisons, fault
reporting and early returns are explicit statements rather than CUDA text.
Division tests the denominator against positive zero, which also catches negative
zero while allowing NaNs, infinities and nonzero subnormals. Fault reporting
preserves the existing source-node ordinal and runtime fault-buffer ABI.

Validation rejects missing or aliased fault storage, reserved fault ordinals,
unknown symbols and comparison type mismatches. Kernels containing both an early
return and a barrier are conservatively rejected until reconvergence is proven.
The row-region emitter remains the final custom statement-string lowering path.

After standard builds, the targeted suite passed **87/87 on both GPUs**:

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu
```

Devices: Windows RTX 4060 Ti and Linux RTX 3090, 88 server device 4. Manual
checks covered the native IR verifier, negative-zero division faults, and scalar
division with NaN, infinity and the smallest positive f64 subnormal. The latter
case also runs under nvm, fvm and jit in the suite. No throughput claim is made
for this structural change, and no full-project suite was run.

The Linux checkout contained committed revision `3e3b1146`. The shared Windows
checkout also received concurrent LSP changes; a subsequent standard build at
`7b6dd4bb` and combined Tiles/LSP regression run validate the integration source.

- [Windows Tiles log](2026-10-16-scalar-fault-ir-rtx4060ti-tests.txt)
- [Linux Tiles log](2026-10-16-scalar-fault-ir-rtx3090-tests.txt)

Integration result: **94/94 passed**, including all seven LSP cases.

- [Integration log](2026-10-16-scalar-fault-ir-integration-tests.txt)
