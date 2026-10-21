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
   - contains the node records, the input table, the payload area, ports,
     closure nodes, branch-arm metadata, and return metadata
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

Since graph model step 3 (2026-10-15; docs/design/graph-model, section 4.1) a
node's identity is its **dense index**: nodes are fixed 28-byte records in one
array, and `gc_node_ref_t` is the index of the record, `0 .. nodeCount() - 1`.

Random access by index and sequential traversal are the same operation, and
any consumer can size a per-node array by `nodeCount()`.

## 5. Index Width

Node indices, edge offsets, counts, payload offsets and frame slots are 32-bit:

```cpp
using gc_node_ref_t = uint32_t;  // index of a node record
using gc_slot_idx_t = int32_t;   // frame/static slot index
using gc_off_t = uint32_t;       // offset into an array of the graph (inputs, payload words)
using gc_cnt_t = uint32_t;       // count of a graph-local array
```

A graph has no size limit other than the index `0xFFFFFFFF`, which is `kInvalidNodeRef`
and `kNoPayload`.

## 5a. Node Chunks

The per-node columns are cut into chunks of 256 consecutive nodes (`GCNodeChunk`,
one block of the graph space each): the node records, the source positions, the
permissions and attributes (when some node has any), the input table of those
nodes and their payload words. NodeId `ref` is node `ref & 255` of chunk
`ref >> 8`; `GCGraph` holds the directory of chunks. Offsets inside a record
(`inBegin`, `withBegin`, `ctrlBegin`, `payload`) are offsets in the arrays of the
node's own chunk; node references everywhere are NodeIds of the graph. A chunk
counts the graph versions that hold it (`shares`), so a version can share the
chunks it did not change; the last one that lets go frees it
(`GCGraphManager::freeGraph`).

## 5b. Regions

A region is a set of the nodes of one graph that is named (docs/design/graph-store 3.3).
`GCGraph::region(id)` is a `GCRegion` of the graph's region table (kind, parent, owning node,
index, result node, form ports, origin, tags, and for an inlined body the function and the
call); `GCGraph::nodeRegion(ref)` is the innermost region of a node, from a column of the node
chunks that is absent while every node is in the body. Region 0 is the body of the function;
a graph with no other region has an empty table. Region ids are not reused, as node ids are
not. Today only annotation regions are made: `std::opt::inline` puts the body of each function it inlines in an `Inline` region inside the region of the call (the regions show in `std::jgir`), and no back end reads them;
the control kinds (`Arm`, `LoopCond`, `LoopBody`) are for the branch and loop rewrite.
`validateRuntimeGraphPayload` checks the table (a tree rooted at the body, nodes named exist,
every node in a region that is there).

## 6. Edges: the Input Table

Each edge is stored **once**, as an entry of the input table:

```cpp
gc_node_ref_t *inputs = nullptr;   // edgeCount entries
```

A node's inputs are one run of the table: its Norm inputs, then its With
inputs, then its Ctrl inputs. The record keeps the start of each group; the
Ctrl group ends where the next record's run begins, and a sentinel record after
the last node closes the last run. Before step 3 every edge was stored twice
(once as an input of the user and once as an output of the source).

## 7. `GCNode`

```cpp
struct GCNode {               // 28 bytes
    GCNodeKind kind;
    uint8_t flags;            // constant / macro
    uint16_t reserved;
    gc_slot_idx_t dataIndex;  // frame slot (> 0) or static slot (< 0) of the value
    TypeId typeId;            // type of the value (camel/core/type/intern.h); 0: none
    gc_off_t inBegin;         // first Norm input in the input table
    gc_off_t withBegin;       // first With input
    gc_off_t ctrlBegin;       // first Ctrl input (ends at the next record's inBegin)
    gc_off_t payload;         // word offset of the payload, kNoPayload when none

    Type *dataType() const;   // the type, through the type table
};
```

The type is a `TypeId`: every type a frozen graph names has one (types outside
the table, such as the open signature of a function being built, are given one
when the graph is encoded, `TypeTable::adopt`). `dataType()` resolves it
through the type table without a lock.

## 8. Payload Area

What a node of a kind carries beyond its record is in the payload area, an
array of 8-byte words:

```cpp
gc_payload_word_t *payload = nullptr;   // payloadWordCount words
```

The record's `payload` field is the word offset of its body; payloads are laid
out in node order. The unit is 8 bytes because two bodies still hold pointers
(the callee graph of a `FUNC`, the implementation an `OPER` caches); the later
steps of the graph model replace them by indices (the graph-reference table, a
shared execution plan). Read a body with `GCGraph::nodeBodyAs<Body>(ref)`, its
raw bytes with `nodePayloadBytes(ref)`.

## 9. Users: an Index Built on Demand

The frozen graph does not store the reverse direction. `GCGraph::uses()` builds
the index of every node's users (per group, in node order, each user once) the
first time it is asked for, keeps it for the graph's lifetime and frees it with
the graph; it is thread-safe. `normOutputsOf`, `withOutputsOf` and
`ctrlOutputsOf` read it, so their results are those the encoder stored before.

```cpp
std::span<const gc_node_ref_t> normInputsOf(gc_node_ref_t ref) const;   // inline, O(1)
std::span<const gc_node_ref_t> withInputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> ctrlInputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> normOutputsOf(gc_node_ref_t ref) const;  // through uses()
std::span<const gc_node_ref_t> withOutputsOf(gc_node_ref_t ref) const;
std::span<const gc_node_ref_t> ctrlOutputsOf(gc_node_ref_t ref) const;
```

## 10. Node Traversal API

```cpp
for (gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
    const GCNode *node = graph->node(ref);
}
// or
for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
    const GCNode *node = *it;
    gc_node_ref_t ref = it.ref();
}
```

## 11. Kind-Specific Node Bodies

The node body is the node's run of the payload area (section 8).

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

`Bind` and `Dref` are reserved kind values that never appear in a frozen graph
(nothing creates `Bind`; `Dref` is a build-time placeholder that encoding
rejects), so no consumer of `GCGraph` handles them.

These nodes have no payload (`GCNode::payload` is `kNoPayload`).

### 11.2 `OPER`

`OPER` stores the executable handle and the operator's index in the operator
table (`camel/core/operator_table.h`). The body is fixed-size:

```cpp
struct GCOperBody {
    operator_t op = nullptr;          // implementation, cached by a scheduler
    camel::core::OperId oper = 0;     // operator table index
    uint32_t reserved = 0;

    std::string_view uri() const;     // resolved through the table, lock-free
};
```

- the operator handle belongs to the hot runtime node body
- the URI, the declared traits (`pure`, `stateOnly`, `elementwise`, `mayFault`,
  `callsGraphs`), the type resolver and the type folder are stored once per
  operator in the table, not per node; passes ask the table by `OperId`
- an `OperId` is process-local and never renumbered while the process lives
- passes and schedulers recognise an operator by its `OperId`, never by comparing
  the URI text: the operators the core knows have compile-time ids
  (`camel::core::oper::VarGet`, `oper::MapArr`, `oper::Isolate`, ...; the table
  interns their URIs first, in a fixed order), a module's pass interns the URIs
  of its own operators once and compares ids, and the families "built-in"
  (`:` prefix), "marked" (`:mark/`) and "assignment" (`:op/assn...`) are flags
  of the entry (`isBuiltin`, `isMarked`, `isAssignment`). The URI is still how
  a scheduler finds the implementation (the executor manager) and what messages
  print

(Until 2026-10-13 the URI characters were stored inline after the body, once per
node.)

### 11.3 `FUNC`

`FUNC` stores the index of its callee in the graph's reference table (section
11.8); `GCGraph::calleeIdOf(ref)` gives its `GraphId` and `Program::calleeOf(graph, ref)`
the graph the program holds under it. A draft has the same payload and a table of the
same kind (`GraphDraft::payloadGraphs()`, `GraphId`s, which starts as the `refs()` of
the graph it was decoded from); `GraphDraft::calleeIdOf`, `calleeGraphOf` (resolved in
the draft's program) and `setCalleeGraph` read and write it.

```cpp
struct GCFuncRef {
    uint32_t ref = kNoLoopGraph;   // index into refs(); kNoLoopGraph: no callee yet
    uint32_t reserved = 0;
};
```

### 11.4 `CALL`

`CALL` stores only the minimal call-mode metadata required by the runtime.
Indirect-callee lookup still comes from the graph inputs rather than from a
heavy call descriptor.

### 11.5 `ACCS`

`ACCS` stores the accessor mode plus its compact parameter:

- tuple index, or
- the `SymId` of the struct field name (`camel/core/symbol.h`)

The body is fixed-size: field names are interned in the process-wide symbol
table and resolved through `GCAccsBody::key()`, which takes no lock.

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

### 11.8 The Reference Table

A frozen graph refers to other graphs only through its reference table
(`GCGraph::refs()`, design section 3.7; since 2026-10-15). Each graph appears
once and carries the roles it has (`GCGraphRefRole`): called by a `FUNC` node
(`kGraphRefCallee`), written inside this graph (`kGraphRefNested`), the graph of
a function constant of the static area (`kGraphRefStatic`).
`dependencies()`, `subGraphs()` and `staticGraphRefs()` are views of the table by
role; `Program::traceGraphs` walks the table. The encoder writes a `FUNC` callee as its
index and refuses a callee that is not in the table; validation checks that
every `FUNC` index names a callee entry and that every graph a function constant
points to is in the table.

The entries are `GraphId`s, not pointers (since 2026-10-20): a frozen graph says
which graphs it refers to and no more. The graph an id names is what the `Program`
the graph is read in holds under it (`Program::graph(id)`; the accessors
`calleeOf`, `loopCondOf`, `loopBodyOf`, `outerOf`, `dependencies`, `subGraphs`,
`staticGraphRefs`). A caller that executes, compiles or rewrites a graph
therefore has a `Program` in hand: a pass has the one it is given, an activation
the program of the function value it was called through (`Function::program()`),
a draft the one it was decoded against (`GraphDraft::program()`). A graph does not
keep other graphs alive either: a program does (a `Program` is rooted as a whole,
`GCGraphManager`), and a graph a transformation makes outside every commit is
kept by `Context::retainRuntimeGraphs`.

The payload of a draft `FUNC` and `LOOP` node is the same index (`GCFuncRef`,
`GCLoopRef`) into the draft's own table `GraphDraft::payloadGraphs()`, whose entries are
`GraphId`s. The encoder builds the frozen table from the graphs the nodes and the static
values name and rewrites each index through the graph id.

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
rest of the graph: they are indices of node records.

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
- the frame layout (`GCGraph::runtimeDataType()`, the tuple type of the frame
  slots that frames trace by) is built at the end of the graph's own block
  (`TupleType::constructFrameLayout`, since 2026-10-14; graph model step 3,
  the slot type table of section 4.1). It is not in the type table and is
  released with the graph, so a rewrite that changes a graph no longer leaves
  a layout behind in the permanent space. Only the graph's frames refer to it;
  a draft keeps the slot types as a plain vector
  (`GraphDraft::runtimeSlotTypes`) and the encoder builds the layout

This split keeps graph identity and payload addresses stable while still
allowing graph-owned slots to reference ordinary runtime objects.

## 13.2 Two-Phase Materialization

Runtime graph materialization should not rely on ad-hoc incremental growth.

The preferred pipeline is:

1. plan
   - enumerate reachable runtime nodes
   - count the node records and the payload words
   - compute input table size
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
- the names a function captures

The source position of each node is not in this store: since graph model step 3
it is a column of the frozen block parallel to the node records
(`GCGraphNativePayload::origins`, one 32-bit origin id per node, 0 for none;
`GCGraph::nodeOrigin(ref)`), written with the records by the encoder. It was a
hash map per graph (node ref to origin), filled by a second payload plan at
every encoding.

Beside it, and only in a graph where some node has any, is the column of
numeric permissions (`GCGraphNativePayload::permissions`, one byte per node;
`GCGraph::nodePermissions(ref)`, 0 for a graph without the column): the
permissions the program granted with `@numeric(...)` (design principles 7.5,
`docs/design/op-semantics` 6.3). Like the origin, it comes from the draft
header (`DraftNodeHeader::permissions`) and survives rewrites, inlining and
cloning node by node.

This centralized store may freely use:

- `std::string`
- `std::vector`
- `std::unordered_map`

(`OPER` nodes used to keep their URI text inside the node; since graph model
step 2 they keep the `OperId`, and the URI is in the operator table.)

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
- `nodeRef(node)` to recover the index of a resolved record
- input accessors that return `std::span` views of the input table, and the
  output accessors that read the use index (section 9)

## 16. Builder / Rewrite Implication

This layout implies a specific runtime rewrite architecture:

- existing runtime graphs are immutable
- rewrites build a fresh runtime graph
- the builder is responsible for:
  - writing the node records and the payload area
  - writing the input table
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
