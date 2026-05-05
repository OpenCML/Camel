# Runtime GC Lifetime Model

Camel's runtime values live in four allocator domains with different ownership rules:

- `autoSpace` owns runtime `rtdata::Object` values and is the only domain swept by the runtime GC.
- `graphSpace` owns materialized `GCGraph` carriers and their in-place native payloads.
- `permSpace` owns `Type` metadata and other process-lifetime immutable descriptors.
- `metaSpace` owns manually managed compiler/runtime metadata.

`GCGraph` is an `rtdata::Object` only so the GC can use its `updateRefs` hook to trace static data.
The graph carrier itself is not an `autoSpace` object and is reclaimed by `GCGraphManager`, not by
mark-sweep.

## Tracing Contract

`rtdata::Object::updateRefs` receives both a relocation callback and the static `Type` describing the
object layout. Composite values must pass the precise child `Type` for every GC-traced slot. This is
required because `Tuple`, `Struct`, and `Array` store raw `slot_t` payloads without per-field runtime
metadata.

The important consequences are:

- `Tuple`, `Struct`, `FixedArray`, and `Array` trace only fields whose `TypeCode` is GC-traced.
- Inline small-array storage is traced the same way as fixed backing storage.
- `Function` and compile-time `StaticFunction` relocate the closure tuple itself, then the tuple scan
  traces closure members with their tuple layout.
- Module object types such as `TensorType`, `PyObjectType`, and `ParameterType` must be allocated in
  `permSpace`, because they are `Type` descriptors, not collectable runtime objects.

## Root Sources

The GC reaches live objects through these roots:

- `Context` installs `GCGraphManager::gcRoots()` as the process root set while runtime graphs are
  alive.
- `GCGraph::updateRefs` traces the graph static tuple using the graph's static tuple type.
- `FramePool` and `TaskflowFramePool` register external root tracers while schedulers are alive.
- Macro evaluation registers typed value roots for temporary compile-time/runtime bridge values.

Context teardown must clear the installed root set before destroying `GCGraphManager`, otherwise the
global `autoSpace` singleton can retain a dangling vector pointer.

## Current Collector Mode

The production configuration currently disables young-generation copying:

```cpp
.enableYoungGenCopying = false
```

Runtime schedulers and native graph payloads still pass raw object pointers through frames and static
slots, so stable addresses are the safe default. With copying disabled, small objects allocate into
the elder free-list space and major GC performs mark-sweep over elder and large-object spaces.

The young-copying path is kept structurally valid for future work: relocated objects carry their
layout type during scan, graph/static roots are traced even when the root object itself is outside
autoSpace, and young marks are cleared during major collection. Fully enabling copying still requires
complete typed root coverage and write barriers for old-to-young references.

## Phase 2 Safepoint Contract

Moving-capable collection is serviced only by explicit `autoSpace().safepoint(...)` calls. Allocation
and allocation-failure paths may queue a minor or major request for the next safepoint, but they do
not run young-generation copying directly. If an allocation cannot be satisfied, the allocator may
run an emergency non-moving mark-sweep over elder and large-object spaces before retrying.

Runtime safepoints are installed at pass boundaries, NodeVM node/function-call boundaries, FastVM
bytecode boundaries, macro execution boundaries, and Taskflow's linear scheduler boundaries. At
those points, live runtime values must be recoverable from registered roots: graph roots, active
frames, macro value roots, or explicit `mm::RootHandle` instances for C++ locals.

The current non-moving domains are intentional:

- JIT machine code has no stack maps yet, so JIT execution remains outside the moving-GC contract.
- Taskflow worker subflows do not yet provide a global stop-the-world worker barrier; they are safe
  with the production non-moving auto-space mode and must not be treated as fully moving-safe.
- Native module bridges that hold auto-space objects across a safepoint must use `mm::RootHandle` or
  store the value in a traced frame/static slot before crossing the boundary.

## Phase 1 Observability Controls

GC diagnostics are configured explicitly through environment variables so language-level test cases
do not need source changes:

- `CAMEL_GC_VERIFY=before|after|both|1` enables heap verification around every requested
  collection.
- `CAMEL_GC_STRESS_ALLOC=N` requests a collection after every `N` auto-space allocations. The
  request is serviced at the next GC safepoint, not inside the raw allocation path, so object
  constructors are not interrupted with half-initialized payloads.
- `CAMEL_GC_STRESS_SAFEPOINT=N` requests a collection every `N` explicit runtime safepoints.
- `CAMEL_GC_STRESS_MODE=minor|major|both` selects which collection kind stress requests use.
- `CAMEL_GC_LOG_MOVES=1` emits movement records when the copying path relocates an object.

The verifier checks allocator structure, object header validity, region tags, and every typed
GC-traced reference reachable from the named root sources. External roots are registered with stable
source names such as `FramePool.activeFrames`, `TaskflowFramePool.activeFrames`, and
`MacroEvaluator.valueRoots`; failures include the root path plus owner/slot/type information from
the tracing callback.

Two runtime passes expose the same infrastructure:

- `std::gc::verify` validates the heap and leaves the graph available for later passes.
- `std::gc::snapshot` / `std::gcsnap` prints JSON with region object counts/bytes, collection
  counters, deferred/emergency collection counters, moved/promoted/freed counters, root-source data,
  and remembered-set size.
