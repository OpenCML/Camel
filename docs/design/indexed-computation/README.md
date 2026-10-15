# Indexed Computation, Schedules, and Resource Graphs

Status: proposed target architecture, not a semantic ruling. The initial
[`tiles` module](../../../modules/tiles/README.md) implements a small executable
subset; the schedule/resource model described here remains proposed.
Date: 2026-10-15. Working branch: `wzj-dev`.

The user requested a design with broad expressiveness and a high optimization
ceiling. This document replaces the earlier two-operator proposal as the target
architecture. It does not change code, the normative design principles, or the
meaning of existing programs. API spellings and source examples below are
proposals, not programs accepted by the current compiler.

## 1. Decision and scope

Use three cooperating representations:

1. **Computation graphs** describe values, index domains, access functions,
   reductions, calls, branches, and structured loops.
2. **Schedule graphs** describe transformations and implementation choices for
   computation graphs, with applicability conditions and explicit alternatives.
3. **Resource graphs** describe placement, physical storage, accesses, ownership,
   synchronization, asynchronous completion, and target instructions.

These are representations within one compilation system, not three unrelated
languages. Retain logical computation information until a consumer deliberately
lowers it. Expert authors may enter at any level, but lower-level code does not
automatically regain the optimization freedom of a pure computation.

There is no promise to discover an optimal implementation for every program.
Arbitrary indexing, recursion, aliasing, and synchronization make some analyses
undecidable. The goal is to avoid representation-imposed ceilings: expose facts,
permit guarded specialization, preserve hierarchy, and allow explicit expert
implementations where automatic scheduling is insufficient.

The initial hardware target can be one GPU family. The abstractions must also
describe CPU vectors, distributed shards, and other accelerators. Portability
means a portable semantic layer and explicit capability requirements; it does
not mean that every physical implementation runs on every device.

## 2. Existing foundations and non-negotiable boundaries

Read-code basis:

| Foundation | Current evidence | Design use |
|---|---|---|
| Operator identity and traits | `include/camel/core/operator_table.h` | Extend the query interfaces beyond purity and elementwise behavior |
| Tensor operator definitions | `modules/tensor/ops/op_def.h` | Reuse inference, reference execution, and VJP registration |
| Static tensor facts | `modules/tensor/type.h` | Extend shape reasoning without requiring physical layout in the logical type |
| Graph differentiation | `modules/autodiff/pullback.h` | Differentiate semantic graphs and structured operations |
| Pass registration | `include/camel/execute/pass/base.h` | Host compilation pipelines in modules |
| Graph node skeleton | `include/camel/runtime/graph.h` | Preserve calls, captures, branches, and graph-local node references |
| Graph-model proposal | `docs/design/graph-model/README.cn.md` | Use the intended immutable program and transactional rewrite direction |

The following remain as specified in `docs/design-principles.cn.md`:

- A function is a subgraph. Cross-graph values travel through ports and captures;
  a node must never directly reference a node in another graph.
- `with` parameters are context parameters, not type or static parameters.
- Type computation is not macro execution. Named dimensions check relationships;
  they do not mandate one language-level function instance per shape.
- `let` is immutable; writable source operations require the existing explicit
  mutability/effect discipline. References are not silently changed to copies.
- Demand determines execution. An unobserved effect is not automatically rooted.
- `sync` and `wait` keep their existing meanings. Neither becomes a GPU barrier.
- `LOOP` follows the already-decided state-expression design. The current runtime
  node enumeration still lacks it; this is an implementation dependency.
- Autodiff remains a library. It does not differentiate through `var` reads and
  writes on an active gradient path.
- Pass order is open and may change behavior. The contracts in this proposal
  apply to the proposed standard optimization/scheduling pipeline, not all passes.
- The per-graph capacity limit remains. A million-element domain is represented
  by a domain and one body graph, not a million copied nodes.

Older design documents may contain superseded choices. The Chinese principles
remain authoritative. This proposal does not decide their unrelated open issues.

## 3. Semantic objects

### 3.1 Logical values and domains

`Tensor<T, shape>` denotes an immutable logical value. Its type does not promise
a heap allocation, contiguity, a device, or one thread per element. A tensor may
be unmaterialized, recomputed, fused, or distributed as an implementation choice.

Introduce module-defined domain values:

- `Domain`: a finite ordered set of logical index tuples.
- `IndexMap`: a pure graph mapping index tuples and parameters to coordinates.
- `Predicate`: a pure graph selecting valid coordinates or contributions.
- `SegmentDomain`: a finite collection of possibly different-length domains.

Rectangular domains, products, permutations, slices, and bounded ragged domains
have recognized descriptors. General data-dependent maps remain expressible as
ordinary subgraphs. Their lack of an affine summary limits transformations, not
their legality. Dynamic-length results use an explicit shape/count production
stage; they are not pretending to have a compile-time fixed extent.

Domain coordinates have checked, nonnegative bounded index semantics. Shape
products and address calculations must not inherit wrapping integer arithmetic
accidentally. Ordinary Camel `int` arithmetic retains its existing semantics;
conversion into a domain or address calculation validates its range. Solvers may
use mathematical integers internally, then prove that machine lowering fits.

Distinguish three maps:

1. **Logical access map**: output/iteration coordinates to input coordinates.
2. **Storage map**: coordinates to offsets in a physical buffer.
3. **Execution layout**: logical block coordinates to device/instance/lane/local
   slot, including replication where an instruction needs it.

Do not collapse these into a single stride vector or pointer expression early.

### 3.2 Collection operations

All names here are proposed module operators. They are `OPER` nodes with typed
ports and, when needed, callable subgraphs.

| Operation | Contract |
|---|---|
| `tabulate(domain, body)` | Produce one value per index in canonical domain order |
| `reindex(value, domain, map)` | Read a logical value through an index map; invalid active coordinates fault |
| `gather(value, indices, mask, padding)` | Inactive lanes do not read source storage and produce padding |
| `fold(domain, initial, body)` | Execute the stated ordered recurrence |
| `reduce(domain, combine, identity, policy)` | Combine contributions under an explicit ordering/numerical contract |
| `scan(domain, combine, identity, policy)` | Produce prefixes under the same explicit contract |
| `scatter_reduce(shape, contributions, combine, identity, policy)` | Combine contributions by destination index |
| `construct(domain, body, output_specs)` | Assemble structured, potentially differently shaped outputs |
| `contract(inputs, access_maps, reduction_axes, arithmetic)` | Retain a structured tensor contraction, including multiplication/accumulation rules |
| `random_at(seed, logical_indices, algorithm)` | Deterministic counter-based random values independent of scheduling |

`read_tile` is a recognized rectangular form of masked reading.
`tabulate_tiles` is a recognized form of `construct` with rectangular disjoint
outputs. They remain useful APIs, but are not the fundamental expressiveness
boundary. An index grid is `tabulate(domain, identity)`; an optimized `iota`
operator can avoid materializing it.

Masking suppresses the specified memory access, not evaluation of arbitrary
expressions used to compute the mask, index, or padding. Lazy per-element control
flow is a separate lane-predicated subgraph operation: only active lanes evaluate
its body, with explicit inactive results. A scalar `BRCH` remains a scalar branch;
ordinary `where` remains eager selection and cannot hide a faulting input.

`scatter_reduce` is a value operation, not a promise to use atomics. Its result
starts from an explicit identity or initial tensor. Padding and inactive
contributions are excluded before destination validation. Active invalid indices
fault. Duplicate destinations are legal only with a specified combine policy.

### 3.3 Multiple results and assembly

A `construct` body returns a tuple or struct. Each output leaf has a specification:

- element type and logical output domain;
- a map from iteration/local-result coordinates to output coordinates;
- a validity predicate;
- assembly mode: unique placement, reduction, or explicit ordered overwrite;
- initialization/coverage and numerical policy where applicable.

Unique placement requires disjointness. Complete construction requires coverage;
partial construction requires an explicit initial value. Unknown disjointness
does not authorize racy writes: use a bounded runtime validation before writes,
a semantics-preserving reference construction, or refuse that implementation.
General dynamic maps do not require an impractical all-pairs check by default.

Ordered overwrite has a specified logical order and normally fewer parallel
implementations. Unordered last-writer-wins is not a pure construction mode.

The output type is known without invoking the body, including for empty domains.
All shapes/dtypes of returned leaves are checked statically where possible and
otherwise at the operator boundary. Tuple projection must not force duplicate
execution of a shared body. Atomic lowering is only valid for reduction modes
whose arithmetic/order contracts permit the resulting order.

### 3.4 Reducers and algebraic information

A reducer definition contains input/state/result types, identity, combine graph,
optional finalize graph, and an ordering policy. State can be a tuple or struct.
This covers argmax with tie handling, segmented statistics, and online softmax
summaries without hardcoding each as a graph node kind.

Distinguish order preservation from parenthesization:

- ordered fold: preserve the recurrence;
- ordered tree: preserve element order, permit reassociation;
- unordered reduction: permit permutation and reassociation.

Associativity is not commutativity. Floating addition is not exactly associative.
Algebraic laws and identities are conditional facts, not conclusions from a
function's name. Built-in registered laws are reviewed compiler contracts.
User laws need a checkable certificate, a proven restricted form, or an explicitly
trusted declaration whose invalidity is outside the checked optimization contract.
Random testing is evidence, never a proof of a law.

## 4. Numerical and fault contracts

Every operation resolves to an explicit arithmetic contract: storage type,
computation type, accumulator type, output conversion, permitted reassociation,
permutation, contraction/FMA, approximate instructions, denormal treatment,
NaN/infinity/signed-zero rules, and reproducibility scope.

Provide named policy values rather than a global undocumented fast-math switch:

- `source`: preserve the selected operation's declared behavior and do not add
  arithmetic freedoms merely because a new backend is selected;
- `reproducible`: select a stated tree/algorithm and a stated scope (same target
  and configuration initially), rejecting unsupported promises;
- `relaxed`: explicitly list allowed transformations and precision choices.

These names are proposed. They do not retrofit a cross-backend bitwise guarantee
onto existing `sum` or `matmul`, whose contracts must first be audited. Ordinary
integer overflow semantics also remain unchanged.

Numerical permission belongs to an operation/subgraph contract, not merely a
schedule hint. A schedule cannot silently weaken it. Error tolerances in tests
are not permissions to reassociate. Algorithm-changing rewrites such as online
softmax require a documented numerical contract; they are not automatically
bitwise-equivalent identities over floating-point values.

Source faults are not all ignorable. A GPU implementation must validate accesses,
generate checks/error reporting, or reject code whose required fault behavior it
cannot implement. A preserved scalar `attempt` boundary must not disappear.
Retain source origin and logical index information in diagnostics.

The standard pipeline distinguishes removal of an unused pure computation (where
the existing pass contract permits it) from speculating a faulting operation on a
path that previously did not execute. Preserve laziness unless non-faulting
speculation is proven or explicitly allowed by that operation's contract.
Parallel fault winner selection follows the eventual language-author ruling;
this design does not resolve the open parallel-map fault question. Device passes
must reject unsupported observable fault policies rather than silently invent one.

## 5. Schedule graphs

A schedule is a program that transforms a selected computation graph. It may be
written with a compiler module in Camel, consistent with the proposed immutable
`Program -> Program!` pass API. It is not a new required syntax inside every model.

Schedule operations include:

- select a semantic operation or region; tile, split, fuse, reorder, distribute;
- choose reduction trees or Split-K when the arithmetic policy allows it;
- choose placement, execution layouts, vector widths, and library implementations;
- cache/promote a value, recompute it, or change a materialization boundary;
- pipeline a structured loop and choose buffer multiplicity;
- map work onto persistent instances, device groups, or separate kernel launches;
- specialize, generate guarded variants, benchmark candidates, and cache a choice.

Transformation results produce new handles to resulting operations. Handles are
revision-scoped; a consumed/stale handle is an error, not an accidental match.
Selectors use operator identity, structural roles, and stable origin metadata;
they do not depend on incidental node numbers surviving rewrites.

Separate optional preferences from required constraints. A preference may be
ignored with an explanation; a required constraint either applies or fails the
schedule. Unsupported schedules do not silently run an unrelated implementation.
Automatic fallback must be an explicit alternative in the plan.

Each transformation records:

1. structural match and shape/domain conditions;
2. dependence, alias, fault, and numerical conditions;
3. target capability/resource conditions;
4. derived result facts and invalidated analyses;
5. the proof rule, trusted rule, or runtime guard that discharges each condition.

Analysis has bounded tiers: constants and intervals, affine/Presburger relations
where supported, registered operator facts, and conservative unknowns. Time out
to unknown; never assume independence because a solver did not finish. Guards
must be checked before unsafe memory effects and must not themselves introduce
otherwise unobservable effects. Opaque data-dependent contracts may require a
different implementation instead of a guard.

This restricts what the *standard checked scheduler* calls a valid optimization.
It does not restrict the language's open pass mechanism.

## 6. Resource graphs and the expert path

### 6.1 Why a second level is necessary

Pure tensor graphs express results well; they do not express every algorithm
using atomics, shared state, asynchronous communication, or hardware instructions.
Provide a resource-level graph vocabulary, accessible to backend passes and expert
authors through a typed graph-construction module. Building such a graph is ordinary
computation over immutable graph values; executing it is a separate operation.

This avoids requiring linear ownership semantics for every ordinary Camel value.
Resource capabilities are verifier-enforced entities inside the resource graph.
Direct source-level mutable resource APIs, if exposed, still require `var`
parameters and effect declarations. An immutable token or descriptor must never
be used as a loophole to mutate a source-level `let` object.

### 6.2 Entities and access rules

Resource graph entities include:

- buffers/views with allocation identity, bounds, storage maps, address space,
  placement, alignment, and lifetime;
- access capabilities for a region: shared read, exclusive write, or compatible
  atomic access;
- memory versions and dependency edges for conflicting accesses;
- completion events, execution groups, barriers, and collective operations;
- distributed block values with explicit layouts;
- target-specific instructions with declared operand, memory, and convergence
  contracts.

An exclusive capability may split into disjoint subregions and later join when
all child uses finish. Read capabilities may fan out while writes are excluded.
Overlapping atomic capabilities are allowed only with compatible semantics.
Unordered conflicting non-atomic accesses are rejected in checked resource code.

Capabilities cannot be forged through casts, duplicated by tuple construction,
hidden in an unchecked closure, or returned after their region ends. Graph
verification is path-sensitive across branches and loops; branch-exclusive uses
can rejoin, and loop states carry versions explicitly. Interprocedural summaries
include resource escape and alias relationships. Opaque calls require contracts;
absent contracts force conservative treatment or rejection of the checked path.

Do not serialize every operation through one global token. Independent buffers
and provably disjoint regions have independent dependence chains. Versioning is
an analysis/representation of required order, not a new total order.

### 6.3 Async work and synchronization

An async operation returns a completion event and reserves its accessed resource
regions until completion. The event is shareable as a dependency; exclusive write
authority is not. Joining events does not manufacture ownership or memory
visibility that the underlying operation lacks.

A consumer's dependency on completion guarantees both completion and visibility
only in the declared scope. Distinguish lane, subgroup, workgroup, cluster, device,
and system scope where a target supports them. Atomics specify memory order and
scope. Barriers specify the participant group and convergence requirement.

Reject divergent participation in a collective unless a supported contract
proves it legal. Never implement a grid-wide barrier by assuming all workgroups
can be resident simultaneously. Use separate launches or a supported cooperative
execution capability with its actual residency restrictions.

Persistent work queues are expressible through atomic operations, but their
progress assumptions must be explicit. Race freedom alone does not prove
deadlock freedom or device-wide forward progress.

On completion, failure, or cancellation, outstanding borrowers must be resolved
before storage reclamation. A host wrapper can move under GC; device storage and
external buffers remain stable until all events and external leases release them.

### 6.4 Encapsulation and purity

A fresh-resource region can implement a pure tensor operation when verification
establishes that inputs are only read, all local outputs are initialized, no
mutable reference escapes, all required writes complete, and the output behavior
meets the numerical/fault contract. Freeze/publish the final logical values.

All writes necessary to produce a result are dependencies of that result through
the region completion. Externally observable effects require an explicit effect
result/control connection. Do not root every store just because it is a store.
Dropping an unused effectful region follows Camel's ordinary demand rules.

Local allocation alone is not enough to claim purity: clocks, global RNG state,
external writes, observable races, and completion-order-dependent results need
their own contracts. Permitted floating reduction variation must be represented
in the arithmetic policy, not hidden by a `pure = true` bit.

## 7. Graph representation and operator interfaces

Retain the stable graph skeleton: `DATA`, `PORT`, `OPER`, `FUNC`, `CALL`, `FILL`,
`ACCS`, `CAST`, `COPY`, `BRCH`, `JOIN`, `LOOP`, `GATE`, `SYNC`.
Domains, layouts, output specifications, and numerical policies are typed values
or interned descriptors. An operation containing bodies references callable
graphs through the graph reference table. Multiple logical results can use an
aggregate result plus projections. All captures enter through ports.

Add versioned operation interfaces instead of making every pass switch on URI:

| Interface | Facts/actions exposed |
|---|---|
| Shape/domain | Rank, extents, dynamic constraints, iteration axes |
| Indexing | Access maps, masks, injectivity, coverage, strides/alignment facts |
| Effects/resources | Read/write regions, aliasing, escape, completion, convergence |
| Algebra/numerics | Reducer laws, precision and reorder permissions |
| Decomposition | An inspectable semantic subgraph and applicability conditions |
| Differentiation | VJP/JVP rules, residual requirements, recomputability |
| Scheduling | Supported tiling/fusion/distribution operations |
| Target lowering | Capability predicates and implementation choices |
| Cost | Estimated work, traffic, live storage, launches; measured feedback |

An opaque operation is legal. It blocks only transformations requiring missing
facts. High-level operators should have both a named identity and an optional
semantic decomposition: keep the name for library matching and the body for
cross-operator optimization. Decomposition is a chosen rewrite, not eager loss of
all semantic identity. Multiple implementations are checked against one contract.

The current `GCGraph*` pass API is not silently assumed to be a complete transport
for these descriptors. Implement the relevant public Program/graph-schema and
serialization contracts before extending consumers. Backend-specific IR may use
its own storage while preserving verified provenance back to the semantic graphs.

Graph commits revalidate structural types and effect summaries. A transformation
also invalidates or re-establishes domain, alias, layout, numerical, and resource
facts. Derived facts are not stale annotations that happen to survive cloning.

## 8. Worked mappings

Examples below are algorithm/schedule notation, not currently valid Camel code.
Existing source syntax remains `with`, partial application, functions, tuples,
structs, and the decided `loop ... do ...` form. Library APIs carry new concepts.

### 8.1 Vector addition

Semantic graph:

```text
tabulate([N], i => X[i] + Y[i])
```

Schedule: tile the index by B, map tiles to instances, select a contiguous lane
layout, mask the last tile, fuse reads/add/output construction. There is no
intermediate allocation requirement. N=0 issues no device work. Alignment guards
can select a vectorized variant, with a valid unaligned alternative.

The earlier `read_tile`/`tabulate_tiles` example is an explicit algorithmic
decomposition of this graph. A schedule-only tile choice leaves more freedom
than forcing a decomposition into the original semantic program.

### 8.2 Softmax and multiple outputs

Semantic body for a nonempty row:

```text
m = max(row)
e = exp(row - m)
s = sum(e)
return { probabilities: e / s, log_normalizer: m + log(s) }
```

Output specifications place probabilities into [M,N] and log_normalizer into
[M]. A small-row implementation uses one resident block; a large-row variant
uses a permitted multi-stage reduction or an explicitly selected online algorithm.
NaN/infinity/empty-row behavior and intermediate precision are part of its
contract. Padding is excluded from reduction or assigned a valid identity;
ordinary zero padding is not a correct max/softmax rule for all inputs.

### 8.3 Matrix multiplication

Semantic graph retains the contraction:

```text
C[m,n] = reduce_k(multiply(A[m,k], B[k,n]), arithmetic)
```

Schedule tiles M/N/K, chooses lane layouts, promotes reusable A/B blocks, pipelines
K with S storage stages, and selects matrix instructions where legal. Two buffers
can alternate only after the earlier reader of a slot completes; lexical loop
iteration alone does not prove the slot is reusable.

An expert may instead author the tiled state loop directly. This fixes part of
the algorithm and possibly its rounding structure; the optimizer cannot freely
replace it by the high-level contraction without the corresponding contract.
Split-K becomes partial contractions plus `scatter_reduce`/a second reduction,
not unordered ordinary stores. M/N tails mask stores; K tails obey the selected
arithmetic and masking contract, including special-value behavior.

### 8.4 Online attention as an expressiveness test

For one query block, loop over key/value blocks with state `(m, l, O)`:

```text
scores = Qblock @ transpose(Kblock) + bias
mask scores by logical query/key indices
m_next = max(m, row_max(scores))
alpha = exp(m - m_next)
p = exp(scores - m_next)
l_next = alpha * l + row_sum(p)
O_next = alpha * O + p @ Vblock
return O / l
```

This sketch omits required all-masked-row handling: track validity explicitly so
`-infinity - -infinity` does not accidentally produce NaNs. Define zero/invalid
output and normalization policy for such rows. Causal/ragged masks use logical
indices; dropout uses `random_at` on global score coordinates. This is a named
algorithmic variant with an explicit numerical contract, not an unconditional
rewrite of materialized softmax.

The resource schedule may overlap K/V transfers, specialize producer/consumer
groups, and keep O in the selected accumulator layout. Backward can use saved
normalizers or recomputation and may require reductions over shared gradients.

### 8.5 Irregular and stateful tests

- Embedding: masked gather; backward is indexed gradient reduction.
- Histogram: contributions `(bin, 1)` with a specified integer reduction.
- Ragged normalization: segment domains and explicit empty-segment behavior.
- Compaction: predicate, scan, dynamic output count, then unique placement.
- Persistent queue: resource atomics with explicit progress/capability contracts.
- Cross-device reduction: placement plus collectives, ordering policy, and events.

The last two are resource-level programs, not forced into pure `tabulate_tiles`.

## 9. Specialization and dynamic execution

Keep language generics, type functions, macro evaluation, backend specialization,
and autotuning separate. A backend requests facts it needs (tile extents, known
callee, layout parameters); specialization supplies them from literals, inferred
facts, or runtime metadata followed by compilation. Tensor contents are not read
back to the host merely to specialize unless explicitly requested.

Variants use guards for shape, strides, alignment, device, and contract facts.
Cache keys include graph/body hashes and captured code, static argument values,
operator schemas, target features, compiler version, schedule, and numerical
policy. Weight contents are not key material unless deliberately constant-folded.
Put explicit limits on variant counts, compilation time, and unrolling.

An unsupported target operation must be diagnosed, partitioned through an explicit
fallback plan, or executed by an explicitly registered external implementation.
Silent host fallback inside a device loop is prohibited in the checked plan.

Autotuning executes isolated candidates with valid inputs. Stateful kernels need
fresh/restored resources; tuning must not replay externally visible side effects.
Candidate correctness gates include numerical policy, boundary cases, and races;
passing samples is not a formal equivalence certificate. Report compile cost,
warm execution, transfer cost, and end-to-end performance separately.

## 10. Differentiation and full-stack integration

Default order: semantic graph and macro expansion, differentiation, graph-level
optimization, scheduling, bufferization/resource lowering, device code generation.
Other supported orders require explicit operation rules; pass order remains open.

New rules are required for tabulation, multi-output construction, gathers,
scatter-reductions, scans, and loops. Gather gradients accumulate at repeated
indices. Padding contributes no gradient to source storage; if padding is a
differentiable parameter, inactive positions contribute to that parameter.
Construction returns gradients for captures as well as ordinary inputs.

Loop differentiation uses residuals or recomputation without unrolling the
entire graph. Checkpointing requires recomputable pure work; hidden RNG state,
external reads, and mutation cannot be replayed as if pure. Counter-based RNG
makes replay explicit. Resource programs need a semantic implementation or a
registered derivative; arbitrary atomics are not automatically differentiable.

Placement spans the full forward/backward/update graph. Optimizer state can be
immutable logical values with buffer reuse after liveness/alias analysis.
Device-aware external tensor interchange must define ownership, strides, offset,
producer/consumer synchronization, and failure cleanup, beyond today's CPU-copy
DLPack helpers. Distributed collectives need group identity and matching order;
local event tokens alone cannot prove all ranks participate.

## 11. Compiler implementation choice

Use Camel GIR and its public graph model as the semantic authority. Select MLIR
as the preferred downstream compiler infrastructure for structured computation,
bufferization, vectorization, and target integration; keep the adapter versioned.
Evaluate Triton/TritonGPU as a block-GPU lowering path for supported operations.
Do not make generated Python or a particular Triton internal IR the sole ABI of
the architecture. Exact supported versions and dialect conversions need a spike.

The reason is reuse of compiler infrastructure, not assuming these projects solve
Camel's semantics. Camel still owns masks, faults, numerical policies, effects,
shape guards, graph provenance, and runtime interoperability. If a required
resource feature lacks a lowering, implement it or report a capability gap.

Retain structured operations as long as useful. Do not lower every tensor op to
opaque C++ or every domain to scalar loops at the entrance. Conversely, do not
retain high-level structure so rigidly that it prevents target-specific kernels.
Library calls, generated kernels, and expert kernels are peer implementations
with the same declared boundary contracts.

## 12. Delivery and acceptance gates

These are milestones toward one architecture, not parallel permanent semantics.

1. **Contracts and reference evaluator.** Domain/index/assembly/numeric schemas,
   operator interfaces, structured loops, and validators. Validate empty domains,
   tails, dynamic shapes, noncontiguous views, duplicate scatter indices, masking
   of faulting accesses, and invalid contracts. Compare to a reference evaluator.
2. **Pure single-device path.** Vector expressions, reductions, multi-output
   normalization, matmul, gathers, scatter-reduction, scans. Verify resource
   lifetimes, variant guards, diagnostics, and no implicit host round trips.
3. **Optimization control.** Legal schedule transformations, layouts, matrix
   instructions, async pipelines, expert graph APIs, and isolated autotuning.
   Inspect generated code and measure traffic, register/shared storage, launches,
   and execution against handwritten baselines on the same device.
4. **Training closure.** Gradients and parameter updates stay device-resident;
   verify gradients, repeated steps, alias-sensitive buffer reuse, checkpoint
   replay, dynamic variants, and non-multiple tile sizes against references.
5. **Irregular and cooperative workloads.** Ragged/segmented operations, online
   attention, persistent scheduling, and multiple devices. Include unsupported
   scope/convergence/progress diagnostics, not only successful examples.

Negative tests are essential: duplicated write capability; early buffer reuse;
hidden alias; missing completion edge; divergent barrier; stale schedule handle;
illegal reassociation; speculative out-of-bounds access; incorrect reducer law;
unsatisfied specialization; graph expansion beyond capacity.

Follow the repository rule: run a new case manually and inspect raw behavior
before adding automated assertions. No performance claim is made by this document.

## 13. Proposed decisions and explicit limits

Recommended design decisions are made here rather than left as a menu:

- Prefer pure indexed computation, but include a verified resource-level escape
  path for algorithms that require state and hardware coordination.
- Make schedules separate programs with checked obligations; allow expert control
  without forcing every model to carry a hardware schedule.
- Keep logical tensor types independent of placement and layout. Put numerical
  permissions in semantic contracts; put physical choices in schedules/resources.
- Use module operators and callable subgraphs, not a growing list of GPU-specific
  syntax forms or core node kinds.
- Introduce no new universal static-parameter keyword or global ownership system
  in the initial source grammar. Use specialization contracts and resource-graph
  verification, with the existing source mutability rules intact.
- Expose uncertainty in analysis. Use guards, explicit trusted contracts, or
  conservative implementations; never silently assume a missing fact.

These are proposed commitments for review, not edits to normative semantics.
Before implementation, record accepted changes to numerical policies, new
operator fault behavior, and resource execution contracts in the appropriate
specifications. Unrelated unresolved language questions remain with the author.

The resulting model aims at broad Triton-like block programming plus higher-level
cross-operator optimization and an expert resource path. Matching the practical
performance and coverage of a mature compiler remains substantial engineering;
representation choices alone do not establish parity.

## 14. Primary references

These inform the architecture; they do not define Camel behavior.

- [Triton language API](https://triton-lang.org/main/python-api/triton.language.html): block operations, indexing, reductions, atomics, and extension interfaces.
- [Triton fused attention](https://triton-lang.org/main/getting-started/tutorials/06-fused-attention.html): an algorithmic and scheduling stress case.
- [Gluon layouts](https://triton-lang.org/main/getting-started/tutorials/gluon/layouts.html): explicit mapping of values to hardware execution/storage.
- [Gluon async copy](https://triton-lang.org/main/getting-started/tutorials/gluon/async-copy.html): asynchronous movement and shared storage.
- [MLIR Linalg](https://mlir.llvm.org/docs/Dialects/Linalg/): structured iteration/access information supporting multiple transformations.
- [MLIR Transform dialect](https://mlir.llvm.org/docs/Dialects/Transform/): transformation programs and explicit transformation handles.
- [MLIR Bufferization](https://mlir.llvm.org/docs/Bufferization/): delaying the conversion from tensor values to physical buffers.
- [Triton autotune](https://triton-lang.org/main/python-api/generated/triton.autotune.html): selection among implementation configurations.
