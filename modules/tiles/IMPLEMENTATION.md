# Tiles implementation and acceptance ledger

The active goal is the complete indexed-computation architecture, not just the
initial CUDA tensor translator. Work stays on `wzj-dev`, in focused commits.
The normative Chinese design principles take precedence over this ledger and
the [architecture proposal](../../docs/design/indexed-computation/README.md).
Unresolved language semantics require an author decision. Backend policy and
module operator contracts must be explicit; they do not redefine Camel syntax.

## Delivery sequence

1. **Executable baseline.** Bundled module, direct graph inspection, CUDA/NVRTC
   runtime, source inspection, diagnostics, CPU comparisons and GPU tests.
2. **Verified optimization foundation.** Separate semantic values, schedules and
   storage plans; validate shapes, indexing, dependencies and resource lifetimes.
   Add inspection and reproducible timing. Fuse elementwise work and plan storage
   without changing reduction order or introducing implicit precision changes.
3. **Core expressiveness.** Index domains, masks, views/layouts, construction,
   gather/scatter reductions, scans, aggregates and multiple results. Support
   structured control and callable bodies; reference evaluation and negative
   tests must accompany each capability.
4. **GPU optimization.** Explicit numerical contracts, parallel and fused
   reductions, tiled contractions, Tensor Core variants, pipeline scheduling,
   checked expert schedules, guarded specialization and isolated autotuning.
5. **Runtime and training closure.** Persistent device values, transfer and event
   dependencies, asynchronous execution, ownership and interchange, graph-level
   differentiation, gradients and updates without host round trips.
6. **Advanced coverage.** Ragged/segmented work, online attention, cooperative
   resource programs and multi-device placement. Report unsupported capabilities
   precisely; do not silently substitute CPU execution.
7. **Final acceptance.** Reproducible correctness and performance on the local
   RTX 4060 Ti and remote RTX 3090 environment; documentation, benchmark artifacts,
   reviewed limitations and clean task-scoped commits. No parity claim without
   measurements against an actual baseline on the same hardware.

Steps may overlap where dependencies require it. A passing early milestone is
not completion of the full goal. The preferred downstream infrastructure choice
(MLIR and possible Triton dialect integration) requires a build/integration spike;
the first NVRTC path does not settle that choice for the whole architecture.

## Measurement requirements

- Record hardware, driver/compiler versions, shapes, dtypes and numerical policy.
- Separate compilation, warm device execution, transfers and end-to-end latency.
- Warm up, repeat measurements, report distributions, and retain raw output.
- Compare optimized and reference paths; report launches, intermediate bytes and
  storage peak alongside time. Regressions need an explanation or correction.
- Exercise empty and odd extents, broadcasting, special floating values,
  captures, changing inputs under cached code, invalid contracts and GC stress.
- Tuning candidates must pass correctness checks before measured selection.

## Baseline evidence (2026-10-15)

- `npm run build` succeeds with CUDA Toolkit 12.6.
- Local RTX 4060 Ti (`sm_89`): vector addition, softmax and 16x16 tiled matmul
  execute on CUDA. Odd-size `17x35 @ 35x23` matches CPU in the checked sample;
  `5x257` softmax total absolute difference is `1.2787985020912274e-7`.
- Host Tiles, GPU Tiles and tensor plans: 29/29 passing. NodeVM, FastVM and JIT
  cover closure capture, direct calls, negative reduction axes and empty sizes.
- Manual GC-stress boundary cases pass on all three execution engines.
- Smoke: 9/10 passing at baseline; the `rgir` snapshot differs after the separate
  graph-metadata work in this workspace. Do not absorb its golden update here.
- `ssh 88zjwei` works; remote Linux has eight RTX 3090 GPUs, driver 580.95.05.
  Remote compilation/execution has not yet been validated.

Current implementation limitations are listed in README.md. Baseline timings
from the test runner include startup/compilation and are not kernel benchmarks.
