# `GCGraph` Native Layout Plan

This document records the target runtime-native `GCGraph` / `GCNode` layout
agreed for the next major refactor stage.

It intentionally describes the end-state runtime object model rather than the
current bridge implementation.

## 1. Design Goals

The native runtime graph layout must satisfy the following constraints:

- `GCGraph` is a runtime-only, read-only graph object.
- Runtime execution passes consume `GCGraph` directly and do not rely on
  compile-time GIR node objects on the hot path.
- Runtime graph rewriting does not mutate an existing graph in place. A rewrite
  pass constructs a new runtime graph object.
- Runtime graph objects stay plain and movable. No RAII-dependent structures may
  live inside the hot runtime graph payload.
- Adjacency queries remain O(1).
- The layout remains compact enough to avoid paying large per-node overhead for
  control-flow or operator-heavy graphs.

## 2. High-Level Model

The final runtime graph object is split into three conceptual layers:

1. `GCGraph`
   - the runtime graph identity object
   - traced by the runtime and used by all runtime passes
2. Native payload
   - read-only packed graph storage owned by the graph
   - contains node blob storage, edge storage, ports, closure nodes, branch-arm
     metadata, and return metadata
3. Cold metadata / debug side stores
   - compile-only or diagnostic-only information
   - may use STL containers and hash tables because they are not part of the
     execution hot path

The critical separation is:

- hot runtime graph payload is compact and plain
- cold debug and naming data is centralized outside that payload

## 3. Read-Only Runtime Graph Rule

The native runtime graph is immutable after construction.

This is a deliberate design rule:

- execution passes never patch a graph in place
- runtime rewrites produce a new `GCGraph`
- old runtime graphs remain valid until GC collects them

Therefore, rewrite support does not require mutable node objects. It requires a
runtime-side graph builder or draft API that constructs a fresh graph object
from existing runtime graph data.

## 4. Node Identity Model

The design does not keep a dense "node ordinal index" as the primary node
identity.

Instead, the runtime graph uses:

- node reference = node block offset in the node blob

That means all internal graph references point directly to the node's position
inside the packed node arena. This removes the need for an additional
`nodeOffsets[]` table.

The trade-off is intentional:

- random access by "the i-th node" is no longer the primary API
- sequential traversal is the natural access pattern
- the few consumers that truly need dense ordinals can build temporary local
  maps in pass-specific code

This keeps the graph core smaller and makes all internal references naturally
blob-relative.

## 5. Index Width

The native runtime layout uses 16-bit indices by default.

The expected scale of a single runtime graph makes 16-bit widths a good default
for:

- node references
- edge offsets
- edge counts
- node-block counts

Using 32-bit fields everywhere would make the fixed node header too large and
would waste space across all graphs.

The baseline type family is:

```cpp
using gc_node_ref_t = uint16_t;  // node block offset inside the node blob
using gc_slot_idx_t = int16_t;   // frame/static slot index
using gc_off_t = uint16_t;       // offset into a packed graph-local arena
using gc_cnt_t = uint16_t;       // count inside a packed graph-local arena
using gc_block_t = uint64_t;     // fixed-size node blob block
```

If extremely large graphs ever become a real requirement, the correct solution
is a separate wide-layout variant rather than making all graphs pay the wide
cost by default.

## 6. `Slice`

Persistent graph storage uses a small slice descriptor:

```cpp
struct Slice {
    gc_off_t offset = 0;
    gc_cnt_t count = 0;
};
```

`Slice` is a storage-level concept.

It is not a substitute for `std::span`. The intended split is:

- graph payload stores `Slice`
- accessors return `std::span`

This keeps the stored graph format plain while still offering a convenient
runtime view API.

## 7. `GCNode`

`GCNode` is the runtime node header and the first block of each variable-length
node object in the node blob.

It is not just an external record. It is the fixed-size part of the runtime
node object itself.

```cpp
struct GCNode {
    gc_slot_idx_t dataIndex = 0;
    gc_cnt_t blockCount = 0;

    Slice normInputs;
    Slice withInputs;
    Slice ctrlInputs;
    Slice normOutputs;
    Slice withOutputs;
    Slice ctrlOutputs;

    camel::core::type::Type *dataType = nullptr;

    uint8_t kind = 0;
    uint8_t flags = 0;
};
```

Semantics:

- `dataIndex`
  - runtime slot index for the node result
- `blockCount`
  - total number of fixed-size blocks occupied by this node in the node blob,
    including the header block
- six `Slice` fields
  - identify this node's regions in the unified graph edge arena
- `dataType`
  - runtime result type for the node
- `kind`
  - runtime node kind discriminator
- `flags`
  - small per-node flags such as constant or macro-related bits

No separate per-node stream header is needed. `GCNode` is already the node
header.

## 8. Unified Node Blob

The runtime graph stores nodes in a single variable-length node blob:

```cpp
gc_block_t *nodeBlocks = nullptr;
gc_cnt_t nodeBlockCount = 0;
```

Each node is laid out as:

```text
[ GCNode ][ kind-specific body blocks... ]
```

To move to the next node during sequential iteration:

```text
next = current + current->blockCount
```

This layout is preferable to a split `GCNode[] + body stream` design because:

- the fixed header and variable body stay physically adjacent
- there is no duplicated node header
- the graph truly becomes a single packed node blob
- no additional node-offset directory is required when node references are
  already blob-relative

## 9. Unified Edge Arena

The runtime graph keeps a single packed edge arena:

```cpp
gc_node_ref_t *edges = nullptr;
gc_cnt_t edgeCount = 0;
```

The graph does not keep six separate edge arrays for different edge kinds.

Instead:

- each `GCNode` stores six `Slice`s
- each slice points into the same `edges[]` array

This is simpler and more compact than maintaining separate arrays such as
`normInputs[]`, `withInputs[]`, `ctrlInputs[]`, and so on.

The graph accessors remain straightforward:

```cpp
std::span<const gc_node_ref_t> edgeSlice(Slice slice) const;
std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> normOutputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> withOutputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> ctrlOutputsOf(gc_node_ref_t ref) const;
```

Each accessor:

1. resolves the node by reference
2. reads the corresponding `Slice`
3. returns a `std::span` over the unified edge arena

## 10. Node Traversal API

Since node references are block offsets, the primary traversal mode is
sequential iteration rather than random access by dense ordinal index.

The graph should provide a node iterator / range abstraction:

```cpp
for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
    const GCNode *node = *it;
    gc_node_ref_t ref = it.ref();
}
```

This makes the common access pattern explicit:

- sequential graph traversal is native
- direct reference-based lookup remains O(1)

If a specific VM needs a dense local numbering, that VM may build a temporary
mapping during its own lowering stage.

## 11. Kind-Specific Node Bodies

The node body begins immediately after `GCNode`.

The node kind determines how the body is interpreted. All node body structs must
remain plain and trivially movable.

### 11.1 Nodes Without Extra Body

These kinds require no body beyond `GCNode`:

- `Data`
- `Port`
- `Copy`
- `Cast`
- `Sync`
- `Gate`
- `Bind`
- `Dref`

For these nodes, `blockCount` is just the number of blocks required by the
header itself.

### 11.2 `OPER`

`OPER` stores both the executable handle and its URI inline in the node body.

The agreed direction is:

- the operator handle belongs to the hot runtime node body
- the URI string is short enough that inline storage is acceptable
- no separate cold lookup table is required for URIs

A representative layout is:

```cpp
struct GCOperBody {
    operator_t op = nullptr;
    uint16_t uriBytes = 0;
    uint16_t reserved = 0;
    // followed by URI bytes, padded to the next block boundary
};
```

### 11.3 `FUNC`

`FUNC` stores the direct runtime callee graph:

```cpp
struct GCFuncBody {
    GCGraph *calleeGraph = nullptr;
};
```

### 11.4 `CALL`

`CALL` stores only the minimal call-mode metadata required by the runtime.
Indirect-callee lookup still comes from the graph inputs rather than from a
heavy call descriptor.

### 11.5 `ACCS`

`ACCS` stores the accessor mode plus its compact parameter:

- tuple index, or
- interned struct-key id

### 11.6 `FILL`

`FILL` stores the target fill category explicitly rather than forcing each
runtime consumer to rediscover it from type structure on the hot path.

### 11.7 `BRCH` and `JOIN`

Control-structure metadata is stored explicitly in the node bodies.

`BRCH` stores:

- matched `JOIN` reference
- range inside the graph-level branch-arm arena
- optional default-arm marker

`JOIN` stores:

- matched `BRCH` reference
- arm count

This makes runtime branch/join semantics explicit and avoids repeatedly
reconstructing them from generic graph edges alone.

## 12. Branch Arm Arena

Branch-arm metadata is intentionally kept in a separate packed arena:

```cpp
struct GCBranchArm {
    gc_node_ref_t head = 0;
    gc_node_ref_t tail = 0;
};
```

Graph payload:

```cpp
GCBranchArm *branchArms = nullptr;
gc_cnt_t branchArmCount = 0;
```

The reason for keeping this arena is that branch-arm head/tail information is
control-structure metadata, not just ordinary adjacency.

The runtime uses it to answer questions such as:

- where does arm `k` begin?
- which tail node feeds the corresponding join arm?

without requiring repeated graph searches during execution or scheduling.

## 13. Ports, Closure, and Return Metadata

The native payload also stores:

- `normPorts[]`
- `withPorts[]`
- `closure[]`
- `exitNode`
- `outputNode`
- `returnNode`
- `returnKind`

All node references in these fields use the same node-reference model as the
rest of the graph: they are block offsets inside the node blob.

The public payload view itself should stay pointer-based and plain:

- `GCGraphNativePayload` exposes raw arena pointers plus counts
- ownership of those arenas belongs to cold metadata or a cold storage object
- runtime consumers never own or resize these arenas directly

This preserves the invariant that the hot graph representation is a compact,
plain snapshot even if the implementation temporarily uses RAII on the cold
side to manage the backing storage.

## 13.1 Runtime Graph Heap

Runtime graphs do not belong in the moving `autoSpace` heap.

The agreed direction is:

- materialized `GCGraph` objects live in a dedicated non-moving graph heap
- graph payload arenas also live in that graph heap
- graph memory is reclaimed manually when the runtime graph manager clears the
  materialized graph closure
- graph static-slot storage also lives in the graph heap
- objects referenced by those static slots may still point at normal runtime
  `GCObject` instances allocated in `autoSpace`

This split keeps graph identity and payload addresses stable while still
allowing graph-owned slots to reference ordinary runtime objects.

## 13.2 Two-Phase Materialization

Runtime graph materialization should not rely on ad-hoc incremental growth.

The preferred pipeline is:

1. plan
   - enumerate reachable runtime nodes
   - compute node blob size
   - compute edge arena size
   - compute branch-arm arena size
   - compute ports / closure sizes
2. allocate
   - request exact-size graph-owned arenas from the non-moving graph heap
3. emit
   - write node headers and bodies
   - write adjacency slices
   - write ports / closure arrays
   - write branch-arm metadata

This keeps allocation simple and preserves the packed immutable runtime layout.

## 14. Cold Debug and Naming Data

Debug information is a cold-path concern and does not need plain-data
restrictions.

The agreed design is:

- keep runtime hot graph payload clean
- use a centralized debug-info store
- allow STL containers and hash tables there

Representative examples include:

- graph display names
- stable graph identifiers
- mangled names
- node debug entity identifiers
- origin/source mapping information

This centralized store may freely use:

- `std::string`
- `std::vector`
- `std::unordered_map`

The only exception intentionally kept inside the node blob is `OPER` URI
storage, because it is small, strongly tied to operator semantics, and useful
for dumps and traces.

## 15. Compile Metadata Boundary

Compile-time GIR remains available only as cold metadata.

It is still useful for:

- debugging
- diagnostics
- transitional migration support
- compile/runtime reverse mapping while the refactor is incomplete

However, the native runtime execution model defined here does not allow hot
runtime consumers to depend on compile-time GIR nodes for execution semantics.

The graph core should also expose the minimal structural helpers needed by all
runtime VMs:

- `node(ref)` to resolve a runtime node reference
- `nodeRef(node)` to recover the blob-relative reference for a resolved node
- `nextNodeRef(ref)` for sequential traversal across the node blob
- edge-slice accessors that convert stored `Slice`s into `std::span` views

## 16. Builder / Rewrite Implication

This layout implies a specific runtime rewrite architecture:

- existing runtime graphs are immutable
- rewrites build a fresh runtime graph
- the builder is responsible for:
  - writing node blob blocks
  - writing the unified edge arena
  - writing ports / closure arrays
  - writing branch-arm metadata
  - sealing the finished graph payload

In other words, runtime rewrite support should be a graph-construction API, not
an in-place mutation API.

## 17. Why This Layout Is Preferred

This design is preferred because it combines:

- compact fixed-size node headers
- variable-length node bodies
- O(1) adjacency access through slices
- no duplicated per-node stream header
- no separate node-offset directory
- immutable graph semantics
- clean hot/cold separation for debug and naming data

It is the layout that best matches Camel's intended end-state:

- runtime graphs are first-class runtime objects
- runtime passes work on runtime-native graph data
- compile GIR becomes a compile-only product plus cold metadata source
