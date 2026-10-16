# Structured elementwise and reduction lowering (2026-10-16)

Source revision: `a56f5471` (after commit-hook formatting).

The physical statement representation now covers elementwise/layout fusion,
ordered producer/reduction/epilogue groups, standalone serial axis and global
reductions, sum-to-shape, scalar constants/casts, and parallel maximum trees.
Their old CUDA statement-string implementations were removed. Typed loads,
lexical arithmetic bindings, ordered loops, accumulator types, final rounding,
shared stores and uniform barriers are explicit before final source printing.
Row-region kernels and scalar chains with device fault reporting remain to migrate.

The same targeted command was run after standard builds on Windows RTX 4060 Ti
and Linux RTX 3090 (88 server device 4):

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu
```

Manual checks included the native plan verifier, exact-byte ordered reduction
comparisons under nvm/fvm/jit, and maximum special values at widths 127,128,257,513.
Windows also manually checked lexical numeric permissions under nvm/fvm/jit,
inline+fvm and opt+nvm. The first test run found one obsolete source-string
expectation (the other 83 cases passed); inspection of the generated source
confirmed the explicit rounded addition, iteration extent and output store,
and the assertion was updated to the new representation. Windows artifact
collection required stopping the editor language server that held the DLL open;
the subsequent standard build completed successfully.

No full-project suite or throughput benchmark was run for this structural batch.
The byte-level and numerical regressions cover rounding boundaries, FMA grants,
canonical reduction order, empty domains, broadcast/index mappings, gradients,
NaNs, infinities, signed zeros, resident buffers and row-fusion fallback paths.

Final result: **84/84 passed on each GPU**, with no skips or failures.

- [Windows log](2026-10-16-structured-reductions-rtx4060ti-tests.txt)
- [Linux log](2026-10-16-structured-reductions-rtx3090-tests.txt)
