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

## Verified schedule foundation

The semantic node list remains independent from kernel groups and physical
allocation slots. A validation boundary checks shapes/topology before scheduling
and group/lifetime legality before execution. The first scheduler fuses only
single-use equal-domain elementwise chains (64-operation limit); no arithmetic
reassociation or reduction-order change is authorized by this optimization.
An ordered-stream interval plan reuses allocations after all prior readers.
This is the initial resource implementation, not the final asynchronous model.

`inspect` exposes schema version 1 and stage membership plus byte/allocation
counts. In the `7x259` pipeline test, five elementwise operations become one
kernel and allocation capacity falls from 44,556 logical bytes to 15,548 bytes.
The shared-result example uses two kernels and 15,544 allocated bytes versus
37,300 logical bytes. These measurements establish launch/storage reduction,
not yet a wall-clock speedup. Native malformed-plan tests exercise the verifier.
The updated host/GPU Tiles and tensor suite passes 34/34 tests, including ten
native rejected-plan mutations and fusion tests across all three execution engines.

## Measurement infrastructure

`benchmark` now uses the production allocation/upload/launch/download path and
compares it against the unfused schedule. It validates outputs before and after
timing, restores inputs on every repetition, alternates variant order, and emits
21 raw CUDA-event samples after five warmups. Compile/load, allocation, upload
and download host costs are reported separately; graph extraction is excluded.
The module cache now refreshes recency to keep consecutive measured variants live.

The suite passes 35/35 checks, including the measurement protocol. Raw records and
the reproducible runner are under `docs/performance/tiles/` and
`test/tools/benchmark-tiles.py`. The committed/rebuilt desktop run shows elementwise
speedups of 3.20–3.29x over unfused code; exploratory runs varied from 2.15–3.42x. Wider
performance claims require the remote measurements and external baselines.

Remote environment inspection found CUDA 12.3 and Node 20, but the default CMake
3.22 and Clang 14 are older than the current project's build requirements.
Remote compilation remains outstanding; a saved Conan Clang 23 profile points
to a Conda toolchain that should be located before installing another toolchain.

## Stable parallel maximum and transfer ordering

The first parallel reduction selects last-axis maxima for widths >= 128 using
256-thread blocks. Selection carries original indices, preserving the first
equal maximum, first-position NaN behavior and signed zeros. The maximum's
numerical contract is unchanged; sums/means retain ordered f64 accumulation.
Schedules expose the implementation name and validate its domain/block contract.
The reference benchmark explicitly disables this optimization as well as fusion.

New tests cover widths 127, 128, 257 and 513 with NaNs/infinities/signed-zero ties,
and 65,537 rows that exercise multiple grid-stride iterations. The large case
revealed the pageable-upload/nonblocking-stream race fixed in `a87af97d`; see
[the transfer contract](notes/transfers.md). Current local suite: 38/38 passing.

Remote baseline `75776da8` built successfully in an isolated archive directory
using the existing Conda Clang 23 toolchain, CMake 4.4.3, Conan 2.33 and CUDA 12.3.
The Conda sysroot needed a private include path for the system Python multiarch
configuration header. All 35 baseline host/GPU/tensor checks passed on device 3
(RTX 3090). Revalidation of the newer transfer/reduction work is in progress.
