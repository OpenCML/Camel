# `GCGraph` Runtime Bridge

This document records the first runtime graph split milestone.

## Intent

Camel now starts separating compile-time GIR from runtime graph objects.
The immediate goal is to stop expanding the old model where one `Graph`
instance simultaneously serves as:

- compile-time IR
- rewrite workspace state
- finalized executable graph
- runtime function target
- lifetime anchor

The new direction is:

- `compile::gir::Graph` stays a compile-time product
- `runtime::GCGraph` becomes the runtime-side graph object
- runtime entry points materialize a pruned runtime graph closure before execution

## Current Bridge Design

The current implementation introduces `runtime::GCGraph` and
`runtime::GCGraphManager`.

`GCGraph` is now constrained to stay a trivial runtime GC object.
It currently owns only:

- a pointer to a bridge record managed outside the GC object itself
- a runtime static-area tuple pointer
- runtime graph references represented as plain traced pointers
- a small fixed extra-slot array for VM/plugin side O(1) caches

The critical rule is that `GCGraph` itself must not contain:

- STL containers
- smart pointers
- any VM-specific API surface
- any state that relies on destructors or RAII for correctness

Bridge-owned STL state stays outside the GC object in `GCGraphManager`
side records so the split can proceed incrementally without polluting the
runtime heap object model.

The remaining compile/runtime fallback is now intentionally centralized in a
small compatibility layer:

- `camel::execute::compatGraph(...)`
- `Frame::sourceGraph()` as an explicit compile-time accessor
- `Function::sourceGraph()` as an explicit compile-time accessor

Runtime-facing APIs now follow a stricter split:

- `Function::graph()` returns runtime `GCGraph*`
- `Frame::graph()` returns runtime `GCGraph*`
- compile-time fallback must be spelled explicitly via `sourceGraph()`

No scheduler or frame-pool hot path should add new compile-time fallback
outside that boundary.

`GCGraphManager` currently provides:

- root materialization from a compile-time graph
- compile-graph to runtime-graph mapping
- reachability traversal over the runtime graph closure
- pruning by starting from the actual runtime entry graph instead of materializing every compiled graph
- recursive canonicalization of static `Function` values so runtime closures point at `GCGraph`

This is intentionally a bridge layer, not the final runtime graph model.
Existing schedulers still execute through `GCGraph::sourceGraph()`.

## Entry Integration

`Context` now owns a runtime graph manager.

Runtime-facing execution passes currently trigger runtime graph materialization before they run:

- `std::nvm`
- `std::fvm`
- `std::taskflow`
- `std::macro`

This locks in a single compile/runtime boundary for real execution paths without
forcing an all-at-once migration of every scheduler and cache.

Runtime root materialization is now routed through `Context` so the runtime side
has a single place to perform post-materialization registration work.

`std::nvm` and `std::fvm` now go one step further:

- root frames are allocated from `GCGraph`
- higher-order runtime calls prefer `Function::graph()`
- runtime static areas no longer have to alias compile-time `Graph::staticArea()`
- VM-specific caches are attached through generic `GCGraph` extra slots rather than dedicated graph APIs
- NodeVM topo/direct-call caches and FastVM runtime root lookup tables are pass-owned side caches
- FastVM entry pc / JIT flags / JIT entry pointers use generic extra slots, not graph-type-specific methods
- Taskflow higher-order calls now prefer runtime graph identities instead of defaulting to compile graph lookup

The bridge also now starts separating runtime diagnostics from compile graph
identity:

- runtime graph materialization registers graph-level debug origin bindings in a centralized runtime-side map
- bytecode fault reporting can resolve graph origin directly from runtime `GCGraph`
- compile graph fallback remains only where runtime node-level metadata has not been introduced yet

## Non-goals of This Milestone

This milestone does **not** yet do the following:

- execute directly on `GCGraph`
- remove the bridge record's dependency on compile-time GIR and STL containers
- integrate `GCGraph` into the existing moving object GC
- replace compile-time `DATA(Function)` with a dedicated compile-time function constant representation
- remove compile-time `GraphDraft` / `GraphRewriteSession`

## Follow-up Work

The intended next steps are:

1. make runtime execution engines consume `GCGraph`
2. define a dedicated graph heap instead of bridging through compile-time graph lifetime
3. split compile-time function constants from runtime `Function`
4. add runtime graph rewrite APIs that produce new `GCGraph` versions instead of mutating compile-time GIR
5. integrate graph roots and graph/object cross references into tracing GC
6. eliminate the bridge-side compile-time `GIR::Graph` dependency so runtime graph metadata becomes self-hosted

## Transitional Bridges To Delete

The current implementation still contains migration scaffolding that is not part
of the intended end state.

These pieces should be removed once runtime execution metadata becomes a
first-class runtime artifact:

- bridge-side storage of compile-time graph metadata needed only because execution still queries `sourceGraph()`
- the explicit compat layer once runtime execution no longer needs GIR graph fallback at all
- compile-graph keyed JIT fallback still reachable when no runtime graph exists
- any hot-path fallback from runtime graph back to `sourceGraph()` for target identity
- temporary log/debug strings that still expose old `draft`-era naming

The target direction is:

- runtime call targets become direct runtime execution records
- entry pc / topo / jit state hang off runtime execution artifacts, not compile graphs
- debug registries are keyed by runtime graph / runtime node identities, not compile graph pointers
- compile graphs remain compile-only products
