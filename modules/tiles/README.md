# Camel Tiles

Experimental bundled C++ module for compiling pure Camel tensor function graphs
to NVIDIA CUDA kernels. It builds with Camel and is imported with `from tiles`.
This is the first executable foundation of the
[indexed computation proposal](../../docs/design/indexed-computation/README.md),
not an implementation of its complete language, scheduling, or optimization model.

```cml
import { Tensor, reshape } from tensor

func add(a: Tensor<f32>, b: Tensor<f32>): Tensor<f32> { return a + b }

func main(): int sync {
    let x = reshape([1.0, 2.0, 3.0], [3])
    println(add(x, x))    // camel program.cml tiles::compile std::fvm
    return 0
}
```

## API

For programs with static f32 tensor shapes, the experimental region pass avoids
wrapping individual functions:

```text
camel program.cml tiles::compile std::fvm
```

`tiles::compile` loads the bundled module automatically (the core no longer has a
`std::tiles` alias; Tiles registers its own names). It extracts supported, pure, dependency-convex regions and inserts
explicit resident inputs, calls and host reads. Shared device results remain
resident across regions. Other nodes execute normally on the selected scheduler.
The rewrite itself does not initialize CUDA; runtime prefers CUDA and falls back
to the original CPU graph for missing capability. Faults after target selection
are not retried. No special import or test environment variable is required.

Current limits: dynamically shaped tensors, symbolic dimensions and unsupported
operators remain outside selected regions; CPU fallback interprets the region
through the current scheduler. Compiled CPU kernels, device cost-based placement
and shape-polymorphic compilation are not yet implemented. Use `tiles::compile std::gir`
or `tiles::compile std::jgir` to inspect the rewritten graph.

A `#gpu` label asks for the device explicitly (design principles 13.4):

```cml
import { Tensor } from tensor

#gpu
func add(x: Tensor<f32>, y: Tensor<f32>): Tensor<f32> { return x + y }
```

`#gpu` marks a function or a block with a compile-time fact; it names no card. The card (or the
host) is chosen at run time with `--devices`. `tiles::compile` reads the label as a promise:
every operator written in the scope — including the ones written in the functions it calls —
must have a device program, and a scope the device cannot serve is a compile error, never a host
run. The capability miss is `GpuScopeUnsupported`; an operator the device back end has a program
for but not for this shape, or one whose module provides none, is `GpuScopeNoDeviceProgram`. Both
are located at the operator, name the scope and say why. A `#cpu` block inside the scope is the
explicit host escape and stays the host's. Without `tiles::compile` in the pipeline nothing reads
the label, and the compiler warns that the tag has no reader (design principles 9.4). Outside a
`#gpu` scope, a node the device cannot take is left on the host, as before; that host side of the
preference path is scheduled for deletion.

For statically shaped eligible tensor regions, `tiles::compile` uses region extraction and resident
transfers. Unsupported nodes stay on CPU while supported regions prefer GPU; captures and CPU
effects are preserved. Partitioning happens once, when the pass rewrites the program.
Static regions now generate their launch plans and CUDA source from parameter types
when outlined, without allocating placeholder input tensors or initializing CUDA.
The core context's process-local `tiles.compiled_region` target fact associates the
owned template with the extracted callee graph. Re-encoding an unchanged callee
preserves its GraphId and revision, so the actual runtime call reads the same fact.
Each call copies the template and binds fresh inputs through an explicit parameter
map; it does not extract the graph, infer shapes, instantiate operators or plan
fusion again. Constants are retained separately from invocation bindings. Input
snapshots and resident allocation owners exist only in that call's copy. Binding
still checks shapes/dtypes and replans storage for host versus resident inputs;
owned-plan validation still runs. NVRTC compilation remains lazy.

Graph revision (including lexical numerical permissions), declaration epoch and
the CUDA lowering-policy target hash govern validity. A stale fact is dropped;
later compilation rebuilds it rather than executing an old declaration. Native
declaration revision tokens are process-local and never serialized. Facts contain
no Camel GC pointers. General call-site specialization with captures keeps its
existing fresh-extraction path; captured region boundaries are fresh parameters.
Symbolic and unknown shapes do not use this regional path.
The outlined execution entry explicitly requests a static template, so a cache
clear or another fact reader dropping a stale entry cannot permanently disable
reuse. The per-call specialization (`source`, `inspect`, `benchmark`,
`run_preferred_resident`) does not infer eligibility from a cache miss.

Templates live until replacement, explicit fact invalidation/clear, or destruction
of their execution context. Replacing a revision reuses its graph's fact entry.
The source LRU remains bounded separately; the graph-fact store has no LRU, so
repeated construction of distinct outlined graphs in one long-lived context can
retain additional templates until that context or its facts are released.
A region that cannot be extracted is left on the host and runs through the
ordinary scheduler; this is graph execution, not a fused CPU JIT kernel.
Configuration errors, malformed graphs, compilation failures and device execution
faults are reported; an executed GPU region is never replayed on CPU.
`run_preferred_resident` chooses the host the same way when no device program can
be made for its function, unless the function's body is `#gpu`: a device unit is
reported as `GpuScopeNoDeviceProgram` instead of being replayed on the host. Functions with
mutable parameters, unbound context parameters or no ordinary inputs are still refused by the
compiled-region path.

CPU fallback may change the last bits of transcendental results such as `exp`
and `log`: each backend must satisfy the operator's declared ulp bound, rather
than match the other backend bit for bit. Correctly rounded primitive operations
and specified reduction algorithms retain their strict numerical contract.

The benchmark correctness gate compares optimized and unfused execution on the
same device. Without relevant explicit numeric permissions it compares the raw
output bits, including signed zeros; a tolerance is used only for outputs that
depend on operations granting `contract`, `reassociate`, `approx_math` or
`change_precision`. Reports distinguish `bitwise_elements` and `relaxed_elements`.
This is an optimization equivalence check, not a CPU reference or a proof that
transcendental functions satisfy their declared ulp bounds.

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
selected relaxed algorithm is needed before relaxing this gate. A compiled-region
call, and `run_preferred_resident`, execute the granted contraction independently
of that benchmark gate.

`inspect` also lists `operator_contracts`: stable operator keys and their numeric
classification, or an explicit undeclared/opaque dependency for a known lowering
whose core declaration is not available yet. Compilation supports semantic major
version 1; incompatible versions are refused. Plans retain the core's immutable,
process-lifetime declaration record as a revision token (not a GC pointer).
Validation before generation/execution rebinds keys and rejects replaced records,
including same-version replacements and declarations newly added to opaque
operators. The source cache includes both stable keys and these process-local
revision tokens. Tokens are never persisted or presented as portable cache keys.

Tensor lowering now retains the core's owned index instances specialized from the
same operand types and constants used for shape inference. `inspect` lists their
result nodes in `indexed_nodes`. Instances are structurally validated against the
captured declaration before scheduling. The parallel maximum implementation also
requires a compatible f32 maximum law and the core's regroup, permute and block
identity freedoms. Missing declarations choose the ordered implementation.
Transpose, permutation, slice, broadcast and axis expansion retain the core
access map as their authority. The output domain writes coordinates directly;
source coordinates may include the core's quotient/remainder reshape expressions.
Physical address lowering checks nonnegative intermediate ranges and operand
bounds. `rowMajorView` supplies compact strides when it proves an equivalent
strided map. Empty domains evaluate no source address. Synthetic scalar fills
and gradient matrix primitives still use their explicit physical layouts.

FMA selection additionally requires the core declaration of correctly rounded
f32 arithmetic sensitive to contraction; an opaque declaration grants no such
optimization capability.

A separate process-local plan/source LRU looks up the exact lowered computation
before fusion scheduling or CUDA printing. Runtime scalar/tensor contents and device
owners are excluded; graph literals, shapes, indexing, numeric permissions, operator
declaration revisions and scheduling options are included. A hit restores the launch
plan and source; storage planning and validation run on every call so host and
resident inputs can share kernels without sharing allocation ownership.
Loop keys additionally include the exact int64 domain, state/capture bindings,
next-state outputs and the complete owned body, including its embedded constant
bits, index instances, permissions and declaration revisions. Deferred body ports
carry no invocation payload; changing an outer runtime input still reuses code.
It retains at most 32 entries and 16 MiB of
accounted entry payload (excluding allocator overhead), with synchronized access.
This is not a persistent semantic fingerprint or a cache of graph compilation.

- `run_preferred_resident<fn>(inputs)` specializes a function graph for actual input
  shapes, compiles CUDA with NVRTC and executes on the device when a program can be
  made for it; the host prepares a resident program otherwise. A reusable graph-bound
  template is used when the signature fixes the shapes; a shape-generic signature is
  specialized from the call's arguments on the same device, not moved to the host
  merely because the template cannot be made. It returns
  `Device<Tensor<...>>` or `Device<float>` leaves in the result's original tuple/struct
  structure. Outputs retain their allocations instead of downloading; feed them
  directly into subsequent graph calls and use `download` only when host values are
  needed. Returned inputs and repeated leaves share native ownership. Completion is
  synchronous. Scalar leaves retain f64 storage, including mixed loss/gradient
  results. Device scalar inputs and ordinary host `float` inputs are runtime values
  with exact f64 payloads. Nested nonempty tuples and structs of these values are
  supported as parameters and results. Host `float` arguments and captured floats are
  uploaded as runtime inputs, so changing their values does not change generated
  code. Graph DATA literals remain constants; integer attributes and static branch
  conditions still specialize the graph. Pass the single parameter directly for a
  unary function, or a tuple for multiple parameters. This is the operator
  `tiles::compile` inserts for an outlined region.
- `source<fn>(inputs)` returns generated CUDA without initializing CUDA or
  executing the function body. It performs the same graph validation as
  `run_preferred_resident`.
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
  Device storage is owned through Camel's foreign-resource control blocks; that
  ownership ends with finalization or end-of-run cleanup, while the physical
  native block may remain cached in its backend's pool until a trim or the
  backend's destruction. The native allocation retains its CUDA context and
  driver lifetime, and contains no moving-heap pointers.
  Graph APIs accept these values as inputs, including mixed host/device tensor
  leaves in tuples and structs. The function keeps its ordinary tensor signature:
  `run_preferred_resident<square>(upload(x))` borrows the existing device allocation
  without copying the input back to the host. Borrowed inputs are never writable
  temporary storage. `inspect` reports `borrowed_inputs` and `borrowed_bytes`;
  `allocated_bytes` excludes them. Its result stays resident; `download` brings it
  back to the host.

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
not device-side dynamic branching: GPU-dependent conditions and `match`
still need additional lowering. Changing a captured flag specializes again; the
generated-source cache cannot substitute the other arm's implementation.

Pure `loop ... for ... do ...` expressions with one or more states support a static `int[]`
domain and either f64 scalar states or statically shaped f32 tensor states with
identical shapes and dtypes. Mixed scalar/tensor recurrent states remain a CPU
boundary; read-only scalar captures are supported.
The compiler owns one body program and emits one device loop with lane-local
state; the body is compiled once, independently of the trip count. Captured f64
scalars and f32 tensors bind as body inputs. Tensor body values must be scalar
or match the state shape, and their access maps must be canonical pointwise maps.
Supported body operations are pointwise arithmetic, f64 scalar add/subtract/
multiply/divide/negation and explicit scalar conversions. Empty domains return
every initial state without executing the body. All next-state expressions are
computed before any state assignment, so swaps and coupled recurrences preserve
simultaneous updates. One operation owns the body and exposes ordered typed
result projections; it produces all states in one launch even when the caller
uses only one result. Tuple destructuring does not duplicate the recurrence. Fixed tuple
projections stay in the same selected region as their eligible producer, so a
selected result downloads only its own visible leaf. Whole tuple results retain
their structure while transferring each tensor/scalar leaf explicitly.

The static iteration table preserves every signed int64 value, including negative
values, values above 2^53 and the int64 extrema. The element may enter floating
arithmetic through explicit `float(k)`; CUDA uses `__ll2double_rn` directly from
the integer value. No floating conversion precedes that operation. Loop-body
numeric permissions and operator dependencies remain attached to the owned body
and participate in validation and source-cache identity; they grant no additional
reassociation or contraction.

`while`, `for ... while`, dynamic domains, mixed recurrent lane domains, nested loops, integer
element arithmetic, shape-changing state, reductions and non-pointwise accesses
inside the body remain unsupported. These are computation boundaries for the
compiled-region path; `source` reports an unsupported computation, and a `#gpu`
scope refuses one at compile time. Scalar
division faults during device execution are reported without CPU replay. Region
analysis reads the body to establish the supported subset. A selected loop region
owns that body, and `tiles::compile` does not outline it independently. Unsupported
loops remain CPU boundaries; the graph pass may still select eligible regions
in their unclaimed body graphs, as it does in other reachable graphs.

## Execution and numerical behavior

The compiler snapshots inputs into an owned IR, retaining no Camel GC pointers
across CUDA work. Fully resolved static graph outputs use `core.shape`; unknown
graph shapes specialize through the tensor operation registry for that call.
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
to CPU results. Strict arithmetic is checked bitwise; transcendental operations
use their declared ulp bounds. Relaxed comparison applies only to algorithms
with the required numeric permissions.

Driver contexts, streams, buffers, and modules have scoped ownership. A bounded
32-entry cache stores compiled modules by generated source within the selected
device context; it does not cache tensor values. Each invocation uploads fresh
host inputs, while resident inputs reuse their native allocations. Public calls
wait for completion before returning Camel values. The internal native submission
interface can chain work on distinct streams through completion events; it does
not introduce a pending Camel value or change source-level `sync`/`wait`.
Compilation-cache access uses a short mutex rather than serializing device
execution. This release does not claim competitive performance with Triton.

Native device and pinned-host blocks are served by two backend-private idle
pools. Caching is ready-only and bounded: 64 MiB / 32 entries for device memory
and 16 MiB / 32 entries for pinned host memory, allocated in 256-byte size
classes. Checkout takes the smallest cached block that is large enough, so a
small request prefers a small cached block but consumes a larger one whenever no
smaller block is available. A block returns to its pool only when its last shared
lease drops, and every live lease keeps its backend alive; an idle slot holds a
raw handle only, so a pool can never extend its backend's lifetime. A released
block that exceeds the idle byte budget or slot count is freed immediately; all
other idle memory is freed only by an explicit trim, by the same-kind trim that an
out-of-memory retry performs, or when the owning backend is destroyed.
`inspect`'s `allocated_bytes` remains a planning metric for device storage; it
does not describe cached or otherwise idle native memory. If draining a stream or
activating the owning context fails during cleanup, both pools permanently
disable reuse before any lease share is dropped: a block that may still be the
target of pending work is abandoned rather than cached or freed, and further
checkouts on that backend are rejected. That failure path does not retry or
recover the memory, so a backend that reports a failed cleanup should be
replaced rather than reused.

## Build and runtime

Run `npm run build`. CMake discovers CUDA Toolkit headers if installed. Without
them, the module still builds and supports `source`; execution reports that CUDA
support was not built. CUDA Driver and NVRTC are loaded lazily, so importing the
module does not require a GPU. On Windows, the matching NVRTC builtins DLL must
be installed alongside NVRTC.

The device a run uses is given on the command line with `--devices`
(`docs/cli.md`): without it the back end takes CUDA device 0, and the host runs
the program when there is none; `--devices 2` requires device 2, and a missing
one is `DeviceUnavailable` rather than a fallback; `--devices cpu` runs wholly on
the host. The named ordinals are checked once at startup, before any graph runs:
a missing one is reported even when the program never reaches a device
operation. `CAMEL_TILES_DEVICE`, which selected the ordinal before, is no longer
read.

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
When the CUDA Toolkit is present the same build also produces native CUDA contract
executables; `tiles-async-check` covers the memory-pool contract described above
(reuse and size classes, idle budgets, injected allocation failure, disabled reuse
after a failed cleanup, lease lifetime and shared-backend reuse) and needs a GPU
to run.
The test-only `tiles::test_outline_compiled` pass verifies actual callee-fact
reuse with per-thread extraction counts before comparing fresh input snapshots
against an independent extraction. It covers changing inputs/captures, tuple
exits, embedded axes and concurrent GC-stressed calls. Its pre-execution fixture
also checks f64 bit patterns, embedded literals, resident owner release, explicit
invalidation and stale facts consumed by another reader after declaration or
numerical-permission changes. Test diagnostics produce no ordinary runtime output.

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
lower to checked core index maps. Affine maps can simplify to an element offset
plus a dot product with physical strides; reshape-based axis expansion can retain
quotient/remainder expressions. Bounds are checked before
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
for checking mappings but is not a symbolic bounds proof. Kernel loops, stores
and barriers now use the structured statement representation described below.

Elementwise f32 and scalar f64 arithmetic now use immutable, typed `ScalarExpr`
trees. Conversions and FMA are explicit operations; basic CUDA arithmetic uses
round-to-nearest intrinsics so each retained node preserves its rounding point.
The host evaluator preserves intermediate IEEE bit patterns and checks the host
rounding mode. It covers arithmetic bodies, not complete kernels or graph faults:
scalar division's zero check remains in the surrounding kernel control flow.
A native GPU differential check compares 5,120 f32/f64 arithmetic results
against this evaluator, including random bit patterns, signed zeros, subnormals,
infinities, NaNs and explicit casts. Extrema compare canonical NaN bits; other
arithmetic compares NaN classification because its payload is unspecified.
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

Rank-two row-local DAGs can now share intermediate values across multiple reductions
inside one CUDA block. This covers softmax and small layernorm expressions, including
row/column broadcasts and multiply-used intermediates. A structured row plan owns
typed arithmetic stages, core reduction laws and inclusive shared-memory lifetimes;
the launch validator reconstructs and checks that plan before execution.
Maximum uses its declared exact reduction freedom. Sum/mean retain the f64
accumulator: a runtime exponent-range proof allows a parallel tree only when every
addition is exact in f64. Wide ranges and nonfinite inputs execute the canonical
ascending chain instead. This grants no extra numerical permission.

The initial row scheduler supports one full-graph output, 2–2048 columns, at most
64 stages and 48 KiB of static shared memory. It selects row fusion only when the
sum of additive reduction widths is at most 1024: larger strict reductions can
serialize heavily when cancellation defeats the exactness proof. Other graphs use
the existing stages. This is a measured heuristic, not an autotuner. Row programs
are tested with signed zeros, subnormals, nonfinite values, cancellation and random
finite bit patterns on NodeVM, FastVM and JIT (`gpu/tiles_rows.plan.toml`).

First-class public index domains, masks, layouts, schedule/resource graphs, general
loop and dynamic branch lowering, general region fusion, asynchronous memory planning,
public asynchronous transfers, Tensor Core lowering, autotuning, broader autodiff
integration, and additional backends remain future work. The current private IR
is an implementation detail, not a new normative Camel graph semantics.

### Structured kernel bodies

Declared pointwise operands use their core affine access maps in fused serial
lowering, including both levels of a permitted multiply/add contraction. Operand
positions are distinct even when two arguments refer to the same graph value.
The shared affine address composer checks each operand axis over the rectangular
domain and is also used by tiled matrix loads. Pointwise lowering currently
requires a direct parallel output domain and unconditional nonnegative affine
accesses; unsupported maps are rejected rather than replaced with broadcasts.
Row regions and ordered-reduction epilogues require canonical broadcast maps,
so cross-row maps stay in the general physical schedule. Explicit internal
primitives without index declarations retain their built-in physical indexing.

Index instantiation during graph extraction calls the core's `sem::instantiate`
by `OperId`, using the actual call's types and static slots. Layouts and synthetic
gradient matrix primitives use that same protocol. Tensor's private index
callbacks are not invoked directly, so a replaced core declaration supplies both
the selected maps and the laws against which they are validated. Static slots
are borrowed only inside extraction's no-collection scope and never retained in
the owned execution plan. Extraction uses the execution context's FactStore to
obtain `core.instance` when graph operand types match the concrete call and every
instance parameter is known. Each read checks the graph revision and declaration
epoch through the core store. Unknown graph facts remain unknown: call-specific
instantiation stays in the owned plan and is never published into the shared
function graph. `inspect` reports `graph_fact_instances` and
`specialized_instances` as extraction query-result counts, not kernel counts or
source-cache hit counts. Synthetic gradient primitives still instantiate directly
because they have no originating graph node.

Matrix multiplication, elementwise/layout fusion, canonical serial reductions
and parallel maximum lower to a typed physical statement tree before CUDA
serialization: lexical index/scalar bindings, masked loads and stores, loops,
conditions, assignments and block barriers. Global and shared buffers have
explicit element types and write permissions. The validator checks references,
lexical scope and types, and rejects barriers under thread-dependent conditions
or loops. There is no raw CUDA statement node. Address bounds, races and shared
value initialization remain the lowering's proof obligations, checked by GPU
edge-shape and canonical FMA-chain regressions rather than claimed by this validator.
The accumulator survives across panels and padded K contributions are excluded.
Row-region kernels use the same statement representation, including shared
stage values, lane trees, integer exponent bounds and mutually exclusive exact
tree/canonical-chain paths. All kernel families now use structured statements;
the final CUDA printer owns statement syntax. Fault-reporting scalar chains use
typed IEEE comparison conditions, an explicit status-word resource, fault reports
and early-return statements. The validator rejects status-word aliasing, invalid
fault ordinals, mistyped conditions and kernels mixing early exits with barriers
without a reconvergence proof. Both signed zeros trigger scalar division faults;
NaNs do not. Tensor division continues to use IEEE arithmetic without this check.
Conditions carry two independently scoped arms. Both arms may assign existing
accumulators; arm-local bindings never escape or become visible in the sibling
arm. Barrier and early-exit checks traverse both arms, including empty-then
branches. This supports mutually exclusive optimized and canonical reduction
paths without re-evaluating a mutable condition after the first arm executes.
Physical scalar registers and buffers additionally support i32/u32, independently
of the public tensor element types. Integer add/subtract/multiply wrap modulo
2^32; min/max and comparisons use the declared signedness. Bitwise AND/OR/XOR
and logical right shift operate on the bit representation; shift counts use
their low five bits. Explicit equal-width 32-bit bitcasts preserve all payload
bits, including floating NaNs and signed zero. Integer division and numeric
float/integer conversions other than the explicit i64-to-f64 conversion below
are currently rejected. These are internal physical
instructions, not new Camel source-language integer semantics. The host oracle
and CUDA emitter implement the same rules without signed C++ overflow.
Physical i64 literals, symbols and immutable constant-table loads preserve exact
signed payloads. Same-type i64 comparisons remain integer comparisons, including
above 2^53. The explicit i64-to-f64 cast rounds to nearest-even; other i64
arithmetic, mixed-type comparisons and i64 bitcasts are rejected.
Serial reduction bodies explicitly retain their accumulator type, initial value,
ordered loop, mean finalization and output rounding. Fused producer and epilogue
nodes use lexical scalar bindings, with contraction permitted only by both
original operator declarations and lexical grants. Parallel maximum has typed
shared accesses and uniform barriers outside each lane predicate.

Matrix kernels now require the core's rank-two matrix domain and ascending f32
FMA-chain law. Operand addresses are composed from the declared affine maps;
shared A/B panels require independence from the other output dimension. The
consumer rejects unsupported guards, indirect maps, out-of-domain coordinates
and incompatible laws before execution. Synthetic gradient matrix primitives
receive their own matmul contract while retaining the original gradient contract
as a compilation dependency. The source-template key now serializes the complete
owned indexing instance, including expressions, parameters, predicates and law
selection; changing a map cannot reuse previously generated addresses.

Axis sum/mean/maximum lowering validates the core iteration roles, direct output
projection and full numerical law (accumulator, initial value, order, empty result
and finalization). Its serial and parallel-maximum loads use the core operand
map, with the same bounds checks as pointwise and matrix accesses. Canonical
maps retain the compact row/axis address. Noncanonical affine reads are supported
by those kernels but excluded from row regions, whose shared-memory accesses
assume canonical rows. Guarded, indirect and negative-stride reads are rejected.
Full-tensor sum/mean and sum-to use the same traversal and mapping checks.
Reduction dimensions are traversed lexicographically in domain order, preserving
the canonical row-major contribution sequence even for nonadjacent sum-to axes.
Full sums retain f64 results and accumulators; sum-to retains its f32 accumulator.
Only direct output projections and unconditional nonnegative affine reads are
supported; symbolic or guarded domains still require further lowering.

## Graph region analysis

`regions<fn>()` returns JSON describing static GPU candidate regions of `fn`:
member node IDs in dependency order, distinct boundary inputs and outputs, and
exclusion reasons. It inspects the function graph without calling it or loading
CUDA. Candidate selection uses core declarations, type/instance facts and the
same physical index-contract checks as lowering. The current coverage is static
f32 pointwise operations, rank-2 matrix multiplication, supported reductions and
the static loop subset described above. Dynamic shapes, standalone scalar
arithmetic, layouts and other node kinds remain boundaries.
This is analysis for the graph pass, not automatic GPU execution.

Each candidate is also extracted into a function draft. Runtime boundary values
become distinct parameters; constant boundary values (including reduction axes)
stay constants. Multiple exits are packed into a tuple. Operator types, source
origins and numerical permissions are preserved. Inspection reports `parameters`,
`result_arity` and `extracted_nodes`; it does not install or execute these drafts.

Regions are connected and dependency-convex: replacing each region by one call
must leave the complete CPU/GPU dependency graph acyclic. In particular, a direct
GPU edge cannot justify fusion across a CPU detour. Shared values used outside a
region remain explicit outputs; unreachable nodes are omitted. Control-edge
endpoints are excluded. Partitioning is local to each graph; loop eligibility
also checks its body, and the graph pass visits reachable graphs while skipping
bodies already owned by selected loop regions. The internal planner is deterministic
and produces maximal legal contractions for its greedy order, not a globally
optimal cost partition.
