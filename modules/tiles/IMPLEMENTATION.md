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

The committed local 8e7be374 benchmark records are saved under
`docs/performance/tiles/2026-10-15-rtx4060ti-parallel-max.jsonl`. For 128x4097
maximum, paired speedups range from 2.57 to 3.81x, with identical checked outputs.
The same-code control remains noisy, so this is workload-specific evidence.

## Cross-platform validation of the transfer and reduction path

Revision 8e7be374 rebuilt successfully on the remote Linux toolchain. All 38
checks passed on RTX 3090 device 7, including large uploads and stable parallel
maximum. An earlier attempt on occupied device 3 had four CUDA-context allocation
failures; these were environment failures, not numerical mismatches. The rerun
and the shared-machine conditions are recorded in `docs/performance/tiles/`.

Three fresh-process benchmark runs (12 reports) have the same source fingerprint
as the local measurements, with exact optimized/reference matches before and
after timing. Paired maximum speedups were 13.05–13.79x; elementwise 4.36–4.62x;
softmax 1.20–1.21x. Matmul remains an identical-code control (0.99–1.01x).
These validate the present internal schedules on a second architecture, not the
full target design or a performance advantage over Triton. Indexed domains,
structured control, multiple outputs, device-resident values, general schedule
and resource graphs, Tensor Core lowering and external baselines remain open.

## Checked static index maps

Transpose, axis permutation and positive-step slicing now lower to one `reindex`
operation carrying an offset and output-coordinate strides in element units.
The compiler reuses tensor shape inference and normalizes negative axes/bounds.
The plan verifier proves the maximum mapped address is in the source allocation
and rejects overflow or a rank mismatch before GPU execution. Empty domains
require no accessible source element. CUDA emits one coordinate-based gather;
results remain materialized, with the existing lifetime planner managing them.

Manual NodeVM, FastVM and JIT runs match the CPU for rank-three transpose,
negative-axis permutation, stepped slicing, a composed permutation/slice/reshape
pipeline, and empty inputs/outputs. Native verification rejects four additional
malformed maps. Map fusion, arbitrary masks and runtime-dependent gathers remain
open; this stage does not claim a full layout or domain API.

The local host/GPU/tensor plans pass 41/41 checks after adding the three engine
variants. The committed revision 4428c59a was rebuilt through `npm run build`
locally and on the remote Clang 23/CUDA 12.3 environment. Both reruns passed
41/41. Remote device 7 test output is retained in
`docs/performance/tiles/2026-10-15-rtx3090-index-maps-tests.txt`.

## Fusion across index domains

Scalar-stage fusion now includes reindex and reshape and no longer requires
equal result shapes. Code generation recursively propagates logical coordinates
through every edge, materializing an f32 temporary for each arithmetic node.
Each internal value must have exactly one use; the validator enforces this and
rejects disconnected or multiply used internal values. Shared outputs remain
stage boundaries. The existing interval planner consequently keeps boundary
buffers alive through the fused launch without allocating internal map results.

The new broadcast/arithmetic/transpose/slice/reshape case becomes one kernel
from six, with 500 allocated bytes and five materialized values (two tensor
inputs, two scalar constants and the output). Its shared-result companion has
two kernels. CPU comparisons and fused/unfused benchmark correctness gates pass
on all three engines; local host/GPU/tensor plans pass 44/44, including 17 native
rejected-plan mutations. This small case establishes allocation/launch removal,
not representative throughput. Cross-reduction fusion and persistent device
storage are still separate outstanding work.

Committed revision 42863511 was rebuilt locally and on 88zjwei (RTX 3090 device
7); both complete reruns passed 44/44. The remote log is retained at
`docs/performance/tiles/2026-10-15-rtx3090-mapped-fusion-tests.txt`.

## Aggregate inputs and multiple outputs

The private program now has an ordered output list instead of one output node.
Compilation recognizes native tuple/struct FILL and ACCS nodes and recursively
extracts tensor leaves, including nested aggregates passed to direct calls.
Returned intermediates are fusion boundaries even with one internal consumer;
every returned value stays live through download. Repeated leaves retain output
order without repeating computation. Aggregate results are rebuilt with their
original Camel types under a short no-collection scope and typed write barriers.

The production path downloads all outputs after one graph execution. Benchmark
correctness compares the full concatenated result; reports include output shapes
for all leaves. Identity-only results use owned host snapshots, as single-output
identity did previously. Unsupported non-tensor leaves and empty aggregates are
rejected by the module resolver. Branches and loops remain outstanding.

Manual runs on all three engines with generational-GC allocation/safepoint stress
pass nested tuple/struct results, aggregate inputs, direct calls, repeated outputs,
identity outputs and mixed empty/nonempty results. Native validation now rejects
20 malformed plans, including an output whose lifetime would overlap reused
storage. These tests exercise the new resource contract rather than executing
the graph once per output.

Revision 6df012b6 was rebuilt through the standard command on Windows and Linux.
Both local RTX 4060 Ti and remote RTX 3090 device 7 pass 47/47 host/GPU/tensor
checks, including the three GC-stress aggregate cases. The remote log is
`docs/performance/tiles/2026-10-15-rtx3090-outputs-tests.txt`.

## Initial autodiff execution

Functions produced by existing `grad` under `std::macro` now reach the CUDA path
for arithmetic, broadcasting, row sums and rank-two dense layers. No derivative
rules are duplicated in Tiles. Gradient broadcast/expand/seed operators lower to
existing reindex/reshape nodes, and rank-two matmul derivatives lower to existing
transpose maps and matmul. `sum_to` has a checked broadcast domain and retains
ordered f32 accumulation; substituting the ordinary f64 sum-axis kernel would
change its numeric contract. Same-shape sum-to/cast-like results reuse their value.

An analytic multi-axis broadcast test exposed a CPU `sum_to` fast-path bug:
preserved prefix axes were incorrectly reduced when a later axis was broadcast.
This was fixed separately in ab2c1a2e and tested against analytic derivatives.
The GPU tests cover square, row-reduction, dense parameter-struct gradients,
multi-axis broadcasting, f32 cancellation and empty reductions. Scalar losses,
general pullback closures, additional derivative rules and control flow remain
outside the implemented subset; this is not the complete training stack.

The rebuilt 4a2756e7 passes 51/51 host/GPU/tensor checks on both the local RTX
4060 Ti and remote RTX 3090 device 7. The existing autodiff regression plan also
passed 121/121 locally after the CPU gradient fix and before the formatting-only
Tiles commit hook. Logs are retained as `2026-10-15-rtx3090-gradients-tests.txt`
and `2026-10-15-autodiff-regression.txt` in `docs/performance/tiles/`.

## Boolean branch specialization

The compiler now recognizes native BRCH/JOIN for boolean conditions known from
the invocation's captured values. It visits only the selected JOIN arm, preserving
Camel's conditional evaluation rule. This includes aggregate results and inlined
arm bodies; it does not trace user code. The unselected arm may contain unsupported
effects, while a selected effect still fails compilation without being executed.
Tests alternate the same closure graph's true/false captures and fresh input data,
exercise tuple outputs, and verify effect pruning and rejection separately.

This is not dynamic GPU control flow. Tensor-dependent conditions, scalar
arithmetic needed to derive more specialization conditions, general matches and
loop regions remain outstanding. The semantic branch itself is not converted
into an eager elementwise select.

The committed ea217b7d rebuild passes 55/55 host/GPU/tensor checks locally and
on remote RTX 3090 device 7. Remote evidence is retained in
`docs/performance/tiles/2026-10-15-rtx3090-branches-tests.txt`.

## Typed scalar loss buffers

The private IR now carries f32/f64 element types and computes physical byte
requirements from them. Host result transport preserves bytes per output instead
of flattening everything into float. Full tensor sum and mean produce native
Camel f64 scalar values with ordered double accumulation; mean of an empty
tensor is rejected. Scalar constants retain their exact bit representation.
Scalar-to-tensor arithmetic inserts an explicit f32 conversion as required by
the existing tensor scalar-operand contract. Other f64 operand uses are rejected
until their typed lowering is implemented.

This enables the tested `value_and_grad` graph to return loss and tensor gradient
in one execution. Tests use 16,777,217 and 8,388,608.5 to detect accidental f32
loss truncation, combine scalar/tensor/constant outputs, and cover empty sums.
Mixed-result benchmarks expose output dtypes and require exact scalar f64
agreement with the reference. Reductions remain serial to preserve their current
ordered numerical contract; no new parallel arithmetic policy is implied.

Committed fa7d71e5 rebuilds successfully on Windows and Linux. Both local RTX
4060 Ti and remote RTX 3090 device 7 pass 59/59 checks, including GC-stress
value-and-gradient reconstruction and 24 native invalid-plan mutations. Remote
output is retained in `docs/performance/tiles/2026-10-15-rtx3090-scalars-tests.txt`.

## Device-value ownership and transfers

`Device<Tensor<...>>` is an interned module type preserving its host tensor's
static refinements. Upload snapshots host f32 data, transfers via pinned storage
on an ordered stream, and publishes the device value only after completion.
Download produces a fresh host tensor without consuming or modifying the device
value. Camel's existing ForeignResourceObject handles relocation, shared wrapper
ownership, deferred finalization and end-of-run cleanup. Native allocations own
the backend through shared ownership so their context and loaded driver remain
available until the last allocation is freed. There are no Camel references in
native storage and no raw global allocation registry.

This stage adds transfer/residency values, not resident graph execution: `run`
still accepts host values. Borrowed immutable device inputs, ownership of device
outputs and cross-call completion dependencies must be integrated next. Async
submission and multi-stream resource graphs remain later work. The GC-stress
test keeps old device values across forty new round trips and checks independent
uploads, repeated downloads, type annotations and empty tensors.

Committed 6fecb708 rebuilds successfully on Windows and Linux. Both the local
RTX 4060 Ti and remote RTX 3090 device 7 pass 62/62 relevant checks, including
the three resident-transfer cases under moving-GC stress. Remote output is
retained in `docs/performance/tiles/2026-10-15-rtx3090-resident-tests.txt`.

## Borrowed device inputs

Graph APIs accept device tensor leaves wherever the computation signature expects
a compatible host tensor, including nested aggregates and mixed host/device
arguments. The computation function itself stays placement-independent. Compilation
retains native owners without copying device data to the host. Borrowed inputs
have no writable storage slot; the verifier rejects accidental assignment to an
allocation or use as computed-node storage. CUDA binds their existing addresses
and checks backend ownership. Repeated aliases remain immutable, even when the
planner reuses other temporary allocations. Inspection reports borrowed input
count/bytes separately from new allocations. Identity outputs download directly
from resident storage, and empty domains remain valid.

Local manual checks and the 65/65 relevant suite pass, including all three VMs
under moving-GC stress and 28 invalid native-plan mutations. Graph results still
return host values; ownership transfer for resident outputs is the next step.

Committed 5814f1f9 rebuilds and passes 65/65 checks on both local RTX 4060 Ti
and remote RTX 3090 device 7. The post-commit remote log is retained in
`docs/performance/tiles/2026-10-15-rtx3090-borrowed-input-tests.txt`.

## Resident tensor results

`run_device` preserves tensor result structure while replacing leaves with device
values. A completed launch transfers each distinct output allocation from scoped
execution resources to a shared native owner. Returned borrowed inputs retain
their original owner. Duplicate outputs share ownership; intermediate allocations
and pointer tables remain scoped. No output copy or download is needed, and the
existing input path accepts these values in subsequent invocations. Transfers
publish only completed values, so this stage does not yet introduce asynchronous
dependency semantics. Scalar device leaves remain pending and are rejected by
the public resolver rather than downloaded implicitly.

The manual GC-stress case chains forty graph calls, retains old inputs/results,
checks nested and repeated outputs, host/device identity graphs and empty output
domains. CUDA runtime code has now exceeded 800 lines; decomposition was evaluated.
The driver, cache, scoped resources and ownership transfer currently share private
types. Extracting an internal runtime interface alongside asynchronous resource
planning is preferable to leaking those types through the public program API.

Committed ccdc7a76 rebuilds and passes 68/68 relevant checks on both local
RTX 4060 Ti and remote RTX 3090 device 7, including all three VMs under GC stress.
The remote log is retained in
`docs/performance/tiles/2026-10-15-rtx3090-resident-output-tests.txt`.

## Typed scalar residency

Native device values now carry dtype as well as shape and ownership. Transfers
use checked byte counts, and `Device<float>` preserves f64 scalars without passing
through tensor f32 storage. `run_device` supports mixed tensor/scalar aggregates,
including value-and-gradient results. Device scalar inputs remain runtime values
and can cross graph boundaries; tensor operators insert their existing explicit
f32 conversion, while scalar identity and output retain f64 precision. Validation
rejects resident dtype mismatches and nonscalar f64 inputs. This does not yet add
general scalar arithmetic lowering or asynchronous execution.

Manual moving-GC checks cover resident loss/gradient reuse, precise f64 values
16777217.0 and 1.0000000000000002, host/device scalar identity, explicit upload,
and empty sum results. Native validation now exercises 30 rejected mutations.
