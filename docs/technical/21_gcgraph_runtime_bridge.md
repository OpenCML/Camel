# `GCGraph` Runtime Bridge

This document records the current graph split after the compile-side staging
model was removed.

## Current Architecture

Camel now has two graph domains with a strict boundary:

- `compile::gir::Graph`
  - mutable compile-time carrier used only during lowering
  - owns compile-time nodes in `GraphArena`
  - derives slot layout and frame layout through `refreshDerivedLayout()`
  - is not used as a rewrite/session object anymore
- `runtime::GCGraph`
  - runtime heap object consumed by execution passes and runtime rewrite
  - contains only runtime-shared data and plain GC-managed references
  - exposes generic extra slots for VM/plugin-side O(1) caches

The compile boundary is now:

- front-end lowering builds `compile::gir::Graph`
- runtime materialization encodes a pruned reachable closure into `runtime::GCGraph`
- all post-compile passes and VMs operate on `GCGraph`

There is no longer any compile-side `GraphBuilder`, `GraphDraft`, or
`GraphRewriteSession`.

## Compile-Side Rules

`compile::gir::Graph` is now single-state.

- No shadow staging state
- No draft/seal lifecycle
- No alternate read surface during export
- All mutators update the real graph fields directly
- `refreshDerivedLayout()` is the single place that recomputes runtime/static
  slot numbering before runtime encoding

This keeps compile lowering simple:

- build nodes directly into the graph
- register dependencies/subgraphs directly on the graph
- compute derived layout once before encoding

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

These are bridge responsibilities, not excuses to let runtime hot paths depend
on compile-time graph objects.

## Immediate Follow-up

The next cleanup targets are:

1. keep shrinking runtime metadata records so `GCGraph` carries only universal
   runtime state
2. move remaining scheduler/JIT consumers to runtime-native graph/node access
   only
3. continue replacing compile-graph-keyed cold metadata with runtime-graph-keyed
   stores
4. delete compile-time GIR APIs that still expose post-compile assumptions

## Acceptance Criteria

The split is considered healthy only if all of the following hold:

1. compile-time lowering does not depend on a second staging abstraction
2. runtime passes do not mutate or inspect compile-time GIR graphs
3. runtime execution identity is `GCGraph*`, not compile graph identity
4. `GCGraph` remains trivial and VM-agnostic
5. debug/source recovery stays on cold paths outside the runtime heap object
