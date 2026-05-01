# `GCGraph` Runtime Bridge

This document records the current compile/runtime graph split after the legacy
compile-side `Graph/Node` implementation was deleted from the active build.

## Current Architecture

Camel now has two graph domains with a strict boundary:

- `compile::gir::DraftGraphBuilder + runtime::GraphDraft`
  - the only active compile-time editable graph stack
  - `DraftGraphBuilder` owns compile-only cold metadata and transient allocation
  - `GraphDraft` owns mutable graph semantics: nodes, edges, ports, closure nodes,
    static slots, and graph-to-graph references
- `runtime::GCGraph`
  - runtime heap object consumed by execution passes and runtime rewrite
  - contains only runtime-shared data and plain GC-managed references
  - exposes generic extra slots for VM/plugin-side O(1) caches

The compile boundary is now:

- front-end lowering builds `DraftGraphBuilder`
- runtime materialization encodes a pruned reachable closure into `runtime::GCGraph`
- all post-compile passes and VMs operate on `GCGraph`

There is no longer any active compile-side `Graph` / `Node` implementation.
Compile-side graph identity is strongly typed as `graph_ptr_t`
(`std::shared_ptr<DraftGraphBuilder>`); the old `shared_ptr<void>` graph erasure
path has been removed from active code paths.

## Compile-Side Rules

Compile-time graph construction now centers on `DraftGraphBuilder`, not a
separate compile-only graph object.

- `GraphDraft` is the single mutable graph surface
- `DraftGraphBuilder` carries compile-only metadata keyed by draft node id
- compile-side graph references are strongly typed `graph_ptr_t`
- no active compile path is allowed to reintroduce `Graph` / `Node`

This keeps compile lowering simple:

- build nodes directly into the draft
- register dependencies/subgraphs directly on the builder
- export directly from builder/draft to `GCGraph`

## Runtime-Side Rules

`GCGraph` remains a plain runtime object.

It must not own:

- STL containers
- smart pointers
- VM-specific APIs
- RAII-dependent state

Cold metadata and debug/source mappings stay outside the GC object in
context-managed runtime-side stores.

Runtime rewrite is now centered on:

- `runtime::GraphDraft`
- `runtime::RuntimeGraphDraftSession`
- `decode(GCGraph) -> GraphDraft`
- `encode(GraphDraft) -> GCGraph`

Compile-time GIR is not part of runtime rewrite semantics.

## Bridge Responsibilities That Still Exist

The bridge layer still has real work to do:

1. encode compile-time graphs into runtime graphs
2. prune unreachable graph closures before runtime installation
3. canonicalize static function references so runtime closures point at
   `GCGraph`
4. register cold debug/source metadata for runtime diagnostics
5. keep compile-side graph handles strongly typed so bridge code cannot silently
   reintroduce erased compile graph payloads

These are bridge responsibilities, not excuses to let runtime hot paths depend
on compile-time graph objects.

## Immediate Follow-up

The next cleanup targets are:

1. keep shrinking runtime metadata records so `GCGraph` carries only universal
   runtime state
2. make compile-time lowering depend only on `DraftGraphBuilder`/`GraphDraft`
   semantics instead of legacy node-handle conventions
3. move remaining scheduler/JIT consumers to runtime-native graph/node access
   only
4. continue replacing compile-graph-keyed cold metadata with runtime-graph-keyed
   stores
5. delete compile-time GIR APIs that still expose post-compile assumptions

## Acceptance Criteria

The split is considered healthy only if all of the following hold:

1. compile-time lowering does not depend on a second staging abstraction
2. runtime passes do not mutate or inspect compile-time GIR graphs
3. runtime execution identity is `GCGraph*`, not compile graph identity
4. `GCGraph` remains trivial and VM-agnostic
5. debug/source recovery stays on cold paths outside the runtime heap object
