# Tiles multi-target compilation

Status: architecture direction accepted by the user on 2026-10-16; concrete public
signatures and implementation remain in progress. Placement is the `#gpu` label, which `tiles::compile` reads;
target descriptors and compiled CPU kernels remain pending. This extends the approved
[Tiles route](../tiles-route/README.cn.md), preserving its graph semantics and
numerical contract. CUDA is currently the only compiled device backend.

## Public boundary

Tiles compiles indexed computations, not exclusively NVIDIA kernels. CPU SIMD,
CPU threading and GPU thread mapping are target-specific schedules over the same
semantic computation. Interpretation of the original Camel graph is a correctness
fallback, not evidence of a compiled CPU kernel backend.

Separate three decisions in the API: requested target family/device, required or
preferred selection, and compiled artifact execution. Do not encode all three in
a CUDA device ordinal or in the generic name `run_preferred_resident`. Exact Camel signatures should
be designed together with target descriptors and executable resource types.

`#gpu` expresses a placement request. Inside the scope an operator the device cannot
run is a compile-time error; outside it an unavailable GPU or an unsupported computation
may select the original CPU graph before execution. Invalid explicit device
configuration, compiler failures, allocation failures and execution faults remain
errors. Never replay a partially executed region on CPU. Capability checks must
be distinguishable from failures; catching every runtime exception is not a
fallback implementation. Numerical permissions remain lexical and cannot be
expanded by target selection.

Keep `#kernel`'s approved expert-kernel role separate from dispatch policy. Shared
kernel operations need CPU reference semantics; explicitly target-specific
operations require capability declarations and may reject another target.

## Backend contract

A target descriptor identifies backend, device, architecture features and resource
limits. A backend reports supported operations, types, layouts and numerical
contracts, lowers a computation to its physical schedule, compiles an artifact,
and executes it through explicit resources. A negative support result includes a
reason; compilation failure is a separate outcome.

Common inspection exposes regions, semantic identities, permissions, transfers,
chosen target and fallback reason. CUDA source/PTX and future CPU machine code
are optional backend artifacts, not the universal meaning of `source`.

Compiled cache identity includes the semantic region fingerprint, specialization,
backend/compiler version, target features and numerical/code-generation options.
Live pointers, streams and allocation identities never enter persistent caches.
Buffers carry device ownership; transfers and completion dependencies remain
explicit. Do not generalize CUDA streams merely by renaming their handles.

Resident values now own a typed `DeviceStorage` resource rather than an erased
`void` allocation. CUDA checks the concrete allocation type and backend before
borrowing its address, and checks the allocation/address pairing on download.
Reads dispatch through the owning storage and validate the requested extent.
`retainHostValue` creates an immutable native host snapshot, including empty
tensors; it does not initialize CUDA. Native storage must not hold moving Camel
heap pointers or hidden `slot_t` values. This establishes storage for CPU fallback,
but automatic host/device routing and compiled CPU kernels remain pending.

## Implementation sequence

1. Backend-independent native subgraph invocation and its GC/fault tests are done
   (`docs/technical/06_runtime_and_execution.md`, the native operator callback service);
   NodeVM, FastVM, JIT and Taskflow run the same service.
2. Define target/capability selection and revise the `#gpu` placement label and the
   inspection API together; implement the GPU-preferred fallback behavior.
3. Separate common computation from CUDA physical planning without retaining two
   independent semantic representations.
4. Add compiled CPU elementwise fusion, then ordered reductions and tiled matrix
   multiplication with CPU-specific vectorization and threading.
5. Extend backend implementations as devices and validation infrastructure become
   available. This design does not claim AMD, Metal or other GPU support today.

Acceptance requires explicit-target and preferred-target tests, no-device and
unsupported-operation fallback, faults that do not trigger replay, identical
basic arithmetic and bounded transcendental error, cache separation by target,
and independent timing of compilation, transfers and execution. CPU performance
must be measured against unfused CPU execution; GPU performance remains validated
on the RTX 4060 Ti and RTX 3090.

## Region rewriting boundary

`outlineRegions` in `modules/tiles/src/region_outline.cpp` owns extraction,
function materialization, multi-output tuple projection and inter-region edge
rewiring. Its executor and stable operator URI are supplied by its caller; a null
executor creates ordinary CPU calls. It has no CUDA initialization, test output
or test-enablement checks. Test passes use the same rewriting implementation.

With explicit device operators this component inserts typed uploads, resident
calls and host-visible downloads. `tiles::test_outline_resident` exercises this
GPU-required path. The public `tiles::compile` pass loads the module and
uses preferred resident execution without test output or CUDA initialization
during rewriting. This is the static-shape region implementation; symbolic
shape compilation and cost-based target selection remain pending.

Static region launch/source templates are now `tiles.compiled_scope` target
facts anchored on the actual extracted callee, with the core graph lineage,
transitive revision and declaration epoch supplying invalidation. Draft commits
preserve those identities for unchanged callees. A CUDA lowering-policy hash
separates the current process-local artifact configuration; backend machine-code
caches continue to include device architecture and NVRTC options. No native
artifact is serialized by the core. Invocation binding uses an explicit
parameter-to-input-node map and creates fresh input snapshots/owners, without
re-extraction, index instantiation, shape inference or fusion planning. Storage
planning and owned-IR validation still run per invocation. Templates have context
lifetime (or explicit fact invalidation), with no extra LRU bound; fresh graph
lineages can retain additional templates in long-lived contexts.

`tiles::test_outline_host_resident` uses the same transfer graph with host-owned
snapshots. It restores arguments into a rooted typed tuple, invokes the original
region through the active scheduler without suppressing collection across the
call, and retains its result as native host storage. This validates the portable
execution boundary on NVM, FVM, JIT and Taskflow; it is interpretation, not a CPU
kernel compiler.

The internal `tiles::test_outline_preferred` path now selects GPU execution or
host interpretation at runtime. Missing GPU capability selects host storage;
unsupported computation falls back before launch. Once selected, transfer,
compilation and execution faults propagate without replay. `placeOnDevice`
reuses storage already owned by the selected CUDA backend and otherwise copies
through the source storage's read interface. The GPU suite exercises a device
input through an effectful CPU fallback and back into a supported GPU region,
checking both the retained host result and the original device input. A separate
division-fault case checks that CPU effects occur once and the original fault
location is preserved across all four schedulers. The resident invocation is
exported as a test-only pass, named in the pipeline (`tiles::test_outline_resident`), for these
checks. Complete dispatch
inspection remains pending; graph exports expose the inserted transfer/call nodes.

The single-target transfer planner computes deduplicated uploads, host-visible
downloads and resident producer/consumer edges from the live graph. Embedded
constants require no transfer. A value used by both a CPU node and another region
has a host copy and a retained device copy; a CPU detour does not force its direct
region-to-region edge through a redundant upload. `regions` exposes this candidate
plan. The rewriting component realizes these edges without downloading and
re-uploading shared region results. Routing currently supports host storage and
the selected CUDA device; other device backends are not implemented.
