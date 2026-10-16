# Tiles multi-target compilation

Status: architecture direction accepted by the user on 2026-10-16; concrete public
signatures and implementation remain in progress. The existing `@gpu` decorator
now uses capability-based CPU fallback; target descriptors and compiled CPU
kernels remain pending. This extends the approved
[Tiles route](../tiles-route/README.cn.md), preserving its graph semantics and
numerical contract. CUDA is currently the only compiled device backend.

## Public boundary

Tiles compiles indexed computations, not exclusively NVIDIA kernels. CPU SIMD,
CPU threading and GPU thread mapping are target-specific schedules over the same
semantic computation. Interpretation of the original Camel graph is a correctness
fallback, not evidence of a compiled CPU kernel backend.

Separate three decisions in the API: requested target family/device, required or
preferred selection, and compiled artifact execution. Do not encode all three in
a CUDA device ordinal or in the generic name `run`. Exact Camel signatures should
be designed together with target descriptors and executable resource types.

`@gpu` expresses a preference. An unavailable GPU or an unsupported computation
may select the original CPU graph before execution. Invalid explicit device
configuration, compiler failures, allocation failures and execution faults remain
errors. Never replay a partially executed region on CPU. Capability checks must
be distinguishable from failures; catching every runtime exception is not a
fallback implementation. Numerical permissions remain lexical and cannot be
expanded by target selection.

Keep `@kernel`'s approved expert-kernel role separate from dispatch policy. Shared
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

## Implementation sequence

1. Finish backend-independent native subgraph invocation and its GC/fault tests.
2. Define target/capability selection and revise the decorator and inspection API
   together; implement the GPU-preferred fallback behavior.
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

This component does not yet insert transfers or propagate resident buffers, and
therefore is not the complete `std::tiles` pass. The production planner must add
those boundaries and target decisions explicitly rather than hide transfers
inside every extracted call.
