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
