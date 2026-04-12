# `GraphDraft` Shared Editable Core

This document records the next-stage architectural boundary for editable graphs.

## 1. Problem Statement

The project currently has two different mutable graph stories:

- compile-time graph construction still centers on the STL-heavy `GIR::Graph`
- runtime rewrite centers on `GraphDraft`

That split is the main structural contradiction. It keeps graph semantics,
editing APIs, and encode/decode rules duplicated across compile-time and
runtime.

## 2. Target Object Model

The long-term graph model should converge to only two graph representations:

1. `GCGraph`
   - immutable runtime graph
   - plain runtime payload
   - consumed by all VMs, JIT, printers, and runtime passes

2. `GraphDraft`
   - the only editable graph representation
   - used by compile-time graph construction
   - used by runtime rewrite
   - encoded into `GCGraph`

Everything else is a controller around those two representations rather than a
third graph model.

## 3. Ownership Split

`GraphDraft` should own only graph semantics:

- nodes
- adjacency
- graph ports
- closure nodes
- static slots and their types
- dependency / subgraph / static-graph references
- signature-level graph attributes required to encode a valid `GCGraph`

`GraphDraft` should not own controller-level identity or workflow metadata such
as:

- source `GCGraph*`
- stable ids
- mangled names
- display names
- session state
- builder-only diagnostics or symbol tables

Those belong to outer controllers:

- compile-time: `GraphBuilder`
- runtime: `GraphRewriteSession`

## 4. Encode / Decode Contract

The encode contract must stay explicit:

- `decode(GCGraph*) -> GraphDraft`
- `encode(GraphDraft, identity/debug metadata) -> GCGraph`

That means draft encoding must not silently depend on draft-owned runtime graph
identity. The caller must provide the cold metadata needed to build the runtime
debug record.

This boundary matters because compile-time construction and runtime rewrite need
the same editable graph semantics, but they do not share the same ownership
story for names, diagnostics, or source handles.

## 5. Current Refactor Step

The current refactor step enforces the first part of that boundary:

- `GraphDraft` no longer stores runtime source graph identity
- `GraphDraft` no longer stores stable id / mangled name / graph name
- runtime rewrite keeps those fields in `RuntimeGraphDraftSession`
- `GraphDraft::encode(...)` now takes explicit identity/debug strings

This is an intentional direction change, not a temporary adapter. Future
compile-time migration should reuse the same contract instead of restoring
identity ownership back into `GraphDraft`.

## 6. Compile-Time Builder Direction

Compile-time should not migrate onto `GCGraph`, because `GCGraph` is immutable
runtime layout. The correct replacement path is:

1. `DraftNode` / `GraphDraft`
   - the only editable graph semantics
   - owns node topology, payload, ports, closure nodes, and graph anchors

2. compile-side host
   - owns transient constant materialization
   - owns node cold metadata such as port names and ACCS string keys
   - owns compile-time operator registry references

That host is intentionally not a third graph IR. It is only the compile-side
owner/controller of one `GraphDraft`.

## 7. Migration Rule

When migrating compile-time lowering off the legacy `Graph / Node` layer, new
code should follow this order:

1. add missing construction/editing primitives to `GraphDraft`
2. store compile-only metadata on the compile-side host keyed by draft node id
3. switch lowering code to create `DraftNode`s directly
4. delete the old `Graph / Node` path once no downstream consumer needs it

The key rule is that no new feature work should deepen the legacy mutable
`Graph / Node` surface area. All new editable-graph semantics belong on
`GraphDraft`.
