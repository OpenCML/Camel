# Camel Tiles

Experimental bundled C++ module for compiling pure Camel tensor function graphs
to NVIDIA CUDA kernels. It builds with Camel and is imported with `from tiles`.
This is the first executable foundation of the
[indexed computation proposal](../../docs/design/indexed-computation/README.md),
not an implementation of its complete language, scheduling, or optimization model.

```cml
import { Tensor, reshape } from tensor
import { run } from tiles

func add(a: Tensor<f32>, b: Tensor<f32>): Tensor<f32> { return a + b }

func main(): int sync {
    let x = reshape([1.0, 2.0, 3.0], [3])
    println(run<add>((x, x)))
    return 0
}
```

## API

NVRTC always receives `--fmad=false`, `--prec-div=true`, `--prec-sqrt=true`
and `--ftz=false`. The in-process module cache compares the source and all compiler
options, including the target architecture; device/context and NVRTC library are
fixed for each cache owner's lifetime. Benchmark reports expose `compile_options`.
These defaults do not grant numerical relaxation permissions. Explicit FMA
operations are independent of the flag that disables implicit contraction.

Tiles retains the core graph's lexical `@numeric` permission byte per lowered
node, exposes it as `numeric_permissions` in `inspect`, and includes it in source
cache identity. It emits explicit f32 FMA for an elementwise multiply/add pair
only when both operations grant `contract` and the product is private to that
launch. Broadcasting is preserved. Returned/shared products retain their rounding
point, and a caller's grant never widens an unannotated callee's permissions.
The other permission bits are retained but do not yet select relaxed algorithms;
f64 scalar contraction is also not implemented. NVRTC implicit contraction stays
disabled. Matmul's normative increasing-k FMA chain needs no relaxation grant.

The benchmark's existing numerical gate compares against unfused execution with
a tolerance. Contraction can legitimately exceed that tolerance for cancellation
cases; such a benchmark still refuses to report a speedup. A reference for each
selected relaxed algorithm is needed before relaxing this gate. `run` and
`run_device` execute the granted contraction independently of that benchmark gate.

`inspect` also lists `operator_contracts`: stable operator keys and their numeric
classification, or an explicit undeclared/opaque dependency for a known lowering
whose core declaration is not available yet. Compilation supports semantic major
version 1; incompatible versions are refused. Plans retain the core's immutable,
process-lifetime declaration record as a revision token (not a GC pointer).
Validation before generation/execution rebinds keys and rejects replaced records,
including same-version replacements and declarations newly added to opaque
operators. The source cache includes both stable keys and these process-local
revision tokens. Tokens are never persisted or presented as portable cache keys.
FMA selection additionally requires the core declaration of correctly rounded
f32 arithmetic sensitive to contraction; an opaque declaration grants no such
optimization capability.

A separate process-local source-template LRU looks up the exact lowered plan
before printing CUDA. Runtime scalar/tensor contents and device owners are excluded;
graph literals, shapes, indexing and launch choices are included. Storage planning
and validation run on every call. It retains at most 32 entries and 16 MiB of
accounted entry payload (excluding allocator overhead), with synchronized access.
This is not a persistent semantic fingerprint or a cache of graph compilation.

- `run_device<fn>(inputs)` executes the same graph and returns `Device<Tensor<...>>` or `Device<float>`
  leaves in the result's original tuple/struct structure. Outputs retain their
  allocations instead of downloading; feed them directly into subsequent graph
  calls and use `download` only when host values are needed. Returned inputs and
  repeated leaves share native ownership. Completion is synchronous. Scalar leaves
  retain f64 storage, including mixed loss/gradient results. Device scalar inputs
  and ordinary host `float` inputs are runtime values with exact f64 payloads.
- `run<fn>(inputs)` specializes a function graph for actual input shapes, compiles
  CUDA with NVRTC, executes on the GPU, and returns host `Tensor<f32>` or `float` leaves
  in the function's original tuple/struct structure. Nested nonempty tuples and
  structs of these values are supported as parameters and results. Host `float`
  arguments and captured floats are uploaded as runtime inputs, so changing their
  values does not change generated code. Graph DATA literals remain constants;
  integer attributes and static branch conditions still specialize the graph. Pass the single
  parameter directly for a unary function, or a tuple for multiple parameters.
- `source<fn>(inputs)` returns generated CUDA without initializing CUDA or
  executing the function body. It performs the same graph validation as `run`.
- `inspect<fn>(inputs)` returns a versioned JSON plan summary without CUDA:
  semantic values, kernel groups, materialized values, physical allocations,
  logical bytes, allocated bytes and ordered output node IDs. Repeated output
  IDs preserve repeated result leaves. These are plan metrics, not timings.
- `benchmark<fn>(inputs)` compares the optimized and unfused schedules with
  the same input snapshots. It returns JSON containing raw CUDA-event samples,
  host compile/load/allocation/transfer times, hardware/compiler metadata, and
  correctness comparisons over every output leaf. It requires at least one
  nonempty computed output. `output_shapes` lists all leaves in aggregate order;
  the existing `output_shape` field is also present for a single leaf.
  `output_dtypes` distinguishes f32 tensor storage from f64 scalar storage.
  Scalar f64 outputs must match the reference exactly; f32 outputs retain the
  reported tolerance policy.
- `available()` reports whether driver/NVRTC initialization succeeds. A successful
  result does not guarantee a subsequent compilation or memory allocation succeeds.
- `device()` reports the selected GPU, or raises a diagnostic explaining failure.
- `upload(tensor)` returns an immutable `Device<Tensor<...>>` owning f32 GPU
  storage. Static dtype/shape refinements are retained. `download(deviceValue)`
  returns a host tensor. Both transfers complete before returning; repeated
  downloads preserve the device value. Empty tensors are supported.
  `upload(float)` and `download(Device<float>)` preserve all f64 bits.
  Device storage is owned through Camel's foreign-resource control blocks and
  released by finalization or end-of-run cleanup. The native allocation retains
  its CUDA context and driver lifetime, and contains no moving-heap pointers.
  Graph APIs accept these values as inputs, including mixed host/device tensor
  leaves in tuples and structs. The function keeps its ordinary tensor signature:
  `run<square>(upload(x))` borrows the existing device allocation without copying
  the input back to the host. Borrowed inputs are never writable temporary storage.
  `inspect` reports `borrowed_inputs` and `borrowed_bytes`; `allocated_bytes`
  excludes them. Results from `run` still return to the host.

The function context is ordinary Camel context, not a compile-time modifier.
The module inspects the supplied function graph at invocation. It never traces
the body by executing it and never silently falls back to CPU execution.

## Supported subset

Contiguous f32 tensor inputs and outputs, including nested tuple/struct
construction and field access; direct nonrecursive function calls;
captured tensors and supported numeric/static attributes; broadcasting arithmetic
(`+`, `-`, `*`, `/`, minimum, maximum); negation, abs, exp, log, sqrt, rsqrt, relu;
axis sum/max/mean with static attributes; reshape with static shape; rank-2 matmul.
Softmax is expressed as max/subtract/exp/sum/divide, not recognized by function name.
Unsupported graph nodes, effects, mutable parameters, dynamic attributes, other
element types, and unsupported tensor operations produce diagnostics.

Boolean `if` conditions already known at specialization (for example, a captured
configuration flag) select exactly one arm. Only that arm is translated; an
unselected effect is neither executed nor rejected. A selected effect is rejected
without execution. Aggregate branch results are supported. This is specialization,
not device-side dynamic branching: GPU-dependent conditions, `match` and loops
still need additional lowering. Changing a captured flag specializes again; the
generated-source cache cannot substitute the other arm's implementation.

## Execution and numerical behavior

The compiler snapshots inputs into an owned IR, retaining no Camel GC pointers
across CUDA work. Shape inference reuses the tensor operation registry.
Intermediates remain on the device. Single-use indexed scalar chains
are fused into groups of at most 64 operations. Shared results retain materialization
boundaries; axis sum/mean may absorb their single-use producers. Each fused node keeps an f32 rounding point;
the scheduler does not reassociate expressions or reorder reductions. Allocation
slots are reused only after their last reader on the ordered stream has finished;
a kernel cannot overwrite storage it reads. Inputs start live at upload time.
Every returned tensor stays live until download, including inputs or intermediates
that are also returned. Multiple outputs execute as one graph, preserving shared
work. Result reconstruction briefly excludes GC while assembling aggregates and
uses the normal typed write barriers; no GC pointers survive in the CUDA cache.
Plan validation checks topology, shapes, stage boundaries, launch dimensions,
storage capacities and nonoverlapping lifetimes. Matmul uses masked 16x16 shared-memory tiles and explicit
f32 FMA. Last-axis maximum uses a block-parallel implementation for widths of at
least 128. Extrema propagate any NaN to canonical f32 `0x7fc00000` and order
negative zero below positive zero, matching the CPU numeric contract. Other axis
reductions are serial per output, with f64 accumulation for sum/mean.
There is no fast-math mode; GPU transcendental results need not be bitwise equal
to CPU results. Floating-point agreement is tested with tolerances.

Driver contexts, streams, buffers, and modules have scoped ownership. A bounded
32-entry cache stores compiled modules by generated source within the selected
device context; it does not cache tensor values. Each invocation uploads fresh
inputs. Calls are synchronous and serialized through the backend. This release
does not claim competitive performance with Triton.

## Build and runtime

Run `npm run build`. CMake discovers CUDA Toolkit headers if installed. Without
them, the module still builds and supports `source`; execution reports that CUDA
support was not built. CUDA Driver and NVRTC are loaded lazily, so importing the
module does not require a GPU. On Windows, the matching NVRTC builtins DLL must
be installed alongside NVRTC.

- `CAMEL_TILES_DEVICE`: nonnegative CUDA device ordinal (default 0), selected at
  first backend initialization.
- `CAMEL_TILES_NVRTC`: preferred NVRTC library path.
- Windows also searches `CUDA_PATH/bin` and the Toolkit directory found at build
  time. Linux uses the loader search path and the configured Toolkit library path.

## Verification

Capture performance records with
`python test/tools/benchmark-tiles.py --runs 3 --output results.jsonl`.
The benchmark performs a correctness execution, five warmups, then 21 timed
executions per variant with alternating order. Inputs are restored before every
execution because storage reuse may overwrite them. Uploads are outside the
CUDA-event interval, which includes kernel launch gaps and measures warmed
resident data. Download time is reported separately. This is not a cold-memory
bandwidth benchmark or end-to-end model throughput. Graph specialization and
snapshot creation are outside the reported phase timings. No timing threshold
is enforced by correctness tests.

The unfused baseline uses the same compiler, arithmetic contract and storage
planner with fusion and parallel maximum disabled. The report records this
reference policy. It is not a Triton or vendor-library comparison.
An `identical_device_code` result is a noise control, not an optimization result.

Run host-only compiler checks with
`node scripts/test.js test/plans/feat/modules/tiles.plan.toml`.
Run NVIDIA execution checks explicitly with
`node scripts/test.js test/plans/gpu/tiles.plan.toml` (also included by the
all-plans runner). These cover vector addition, softmax, odd-size tiled matmul,
broadcasting, fresh values with cached code, reshape, captures, direct calls,
negative-axis reduction, and empty dimensions across NodeVM, FastVM, and JIT.
The host plan also runs native mutation tests built by the standard build. Its
test executable stays in the build tree and is not a distributed runtime tool.

## Remaining architecture

Autodiff-generated `grad` functions can be passed to `run` after `std::macro`.
Supported derivative primitives include broadcast/expand, shape-matched casts,
zero/one seeds, `sum_to` and rank-two matmul gradients. Broadcast/expand use index
maps; matmul gradients use transpose maps and matmul. `sum_to` preserves the
tensor implementation's row-major f32 accumulation order, distinct from the f64
accumulation of `sum_axis`. This supports tested polynomial, row-reduction,
broadcast and dense-layer gradients, not all derivative rules. F64 scalar
addition, subtraction, multiplication, division and negation are supported,
including composed scalar losses. Single-use scalar chains fuse with exact
double intermediates and disabled contraction. Scalar division by either sign
of zero faults before a result is returned, using a device fault word checked
at completion. Other scalar math functions, arbitrary pullback closures, control flow and
unsupported derivative operators still require further lowering work.

Static `transpose`, `permute` (including negative axes) and positive-step `slice`
lower to a checked index map: source address equals an element offset plus the
dot product of output coordinates and element strides. Bounds are checked before
CUDA execution, including arithmetic overflow. Empty domains do not dereference
addresses. Transpose, permutation, slice and reshape can fuse with each other
and with elementwise operations across different domains. Generated loads
compose each edge's coordinates, including broadcasting, while preserving f32
rounding points. Shared results remain materialization boundaries; supported
axis reductions may consume mapped producers directly. There is no storage aliasing. This representation
separates logical indexing from thread placement and is a foundation for later
map composition and view elimination, not the complete indexed-domain design.

Physical matrix tile loads use structured buffer accesses: an index expression
and a Boolean validity mask travel together through coordinate substitution.
Host evaluation short-circuits masks and skips invalid addresses; CUDA lowering
prints conditional loads with exact fallback bits. The host evaluator is useful
for checking mappings but is not a symbolic bounds proof. Kernel loops, stores,
barriers still need migration to a complete structured kernel IR.

Elementwise f32 and scalar f64 arithmetic now use immutable, typed `ScalarExpr`
trees. Conversions and FMA are explicit operations; basic CUDA arithmetic uses
round-to-nearest intrinsics so each retained node preserves its rounding point.
The host evaluator preserves intermediate IEEE bit patterns and checks the host
rounding mode. It covers arithmetic bodies, not complete kernels or graph faults:
scalar division's zero check remains in the surrounding kernel control flow.
Host transcendental results are not a proof of CUDA's declared ulp bounds.
The selected FMA still requires the original nodes' permissions and operator
contracts; constructing a physical FMA expression does not grant permission.

Single-use pointwise and index-map producers can fuse into axis sum/mean. Each
output thread computes its producer values in f32 and folds them in increasing
axis order with the existing f64 accumulator, then rounds the result to f32.
This removes producer buffers without granting reassociation or contraction.
Same-shape pointwise consumers can also join the reduction stage: its f32 result
rounding remains explicit before the epilogue runs. An epilogue group contains
one reduction; shape-changing consumers and multiple reductions remain separate.
The ordered reduction uses 32-thread blocks; expensive math is kept in a separate
pointwise stage when fewer than 2048 output rows exist, to avoid serializing it into
too few lanes. Shared results and observable producer outputs remain boundaries.
`reduction_benchmark.cml` measures three shapes and two affine epilogues against the unfused CUDA schedule;
this heuristic is not an autotuner and does not guarantee every shape improves.

First-class index domains, public masks, layouts, schedule/resource graphs, loop and
branch lowering, parallel reductions, cross-reduction fusion, asynchronous memory planning,
asynchronous transfers, Tensor Core lowering, autotuning, broader autodiff
integration, and additional backends remain future work. The current private IR
is an implementation detail, not a new normative Camel graph semantics.
