# A frozen node names its type by `TypeId`, and `typeIdOf` adopts whatever it is handed

Question raised by the lane `handover/h493-type-identity` (H493) on 2026-10-25, on
`handover/h465-flip-integration` tip `012b42bcb` merged with `origin/develop` `5021c7308`
(merge commit `7609dce2b`, no conflicts; `[SUMMARY] total: 6218, pass: 6191, fail: 8, skip: 11,
known failures: 8`). Everything below is verbatim from that tree. The lane changed nothing on the
integrated line: it measured four probes on branches of its own and left them there.

## 1. The mechanism

`include/camel/runtime/graph.h:264-284`: a frozen node record carries no type pointer, only an id.

    struct GCNode {
        ...
        /// The type of the node's value (camel/core/type/intern.h); kNoTypeId when it has none.
        camel::core::type::TypeId typeId = camel::core::type::kNoTypeId;
        ...
        camel::core::type::Type *dataType() const {
            return typeId == camel::core::type::kNoTypeId
                       ? nullptr
                       : camel::core::type::TypeTable::instance().get(typeId);
        }
    };

`src/runtime/graph.cpp:143-154` decides the id:

    /// The TypeId a node record stores for `type`: types outside the type table (open signatures,
    /// unions, type variables) are given one here (TypeTable::adopt), so every type a frozen graph
    /// names has an id.
    camel::core::type::TypeId typeIdOf(Type *type) {
        if (type == nullptr) { return camel::core::type::kNoTypeId; }
        if (type->typeId() == camel::core::type::kNoTypeId) {
            camel::core::type::TypeTable::instance().adopt(type);
        }
        return type->typeId();
    }

`adopt` gives an id to the object it is handed; it does not make the table hold one object per
structure, and `intern()` keys a composite on the **addresses** of its components
(`src/core/type/composite/array.cpp:139-156`). So two objects of one type that reach a frozen
record get two ids — an object identity written into the graph itself. Three readers of that id
are in the same family as the comparisons the lane was sent to count:

- `src/runtime/graph_edges.cpp:196` — a writing union's two nodes must have the same type, read
  as `a->typeId != b->typeId`;
- `src/runtime/graph.cpp:1913` — `GCGraph::nodeContentHash` mixes `n->typeId` into the hash of
  the node record;
- `src/passes/opt/generic/screen.cpp:175` — a pure operator node's expression class holds
  `(*it)->typeId`.

## 2. The reading that lands on it

Baseline, verbatim from the plan runner:

    [FAIL] rules.tensor_saturation_keeps_values                 395.84 ms
        [INFO] expected exit 0, got 1
        [INFO] stdout does not contain '[8.0, 16.0, 24.0]'
        [stdout] ():?:?: [Error]: Pass 'rules::saturate_all' failed: error RewriteInvalid:
        Runtime graph 'neutral'...  (this is the baseline text; see probe 3 below for the change)

Probe 3 (`handover/h493-p3-writings`, `6ad8b1c8b`: `src/runtime/writings.cpp:776`,
`ha->dataType != hb->dataType` → `typeEquals`) — the red list is byte-for-byte unchanged
(`fail: 8`), but the failure text moved:

    [stdout] ():?:?: [Error]: Pass 'rules::saturate_all' failed: error RewriteInvalid: Runtime
    graph 'neutral' breaks its edge invariants (3 violation(s)):
    [stdout]   graph 'neutral': equivalence 0 is between nodes of different types
    [stdout]   graph 'neutral': equivalence 1 is between nodes of different types
    [stdout]   graph 'neutral': equivalence 2 is between nodes of different types.

The merge that `assertEqualDeferred` refused now goes through, and the check one step later
refuses it on `typeId`. Probe 4 (`handover/h493-p4-invariant`, `5c0708e8b`: probe 3 plus
`graph_edges.cpp:196` → `typeEquals(a->dataType(), b->dataType())`) turns the case green:

    [PASS] rules.tensor_saturation_keeps_values                 282.77 ms
    [SUMMARY] total: 6218, pass: 6192, fail: 7, skip: 11, known failures: 8

So the case needs **two** sites, and the second one is not a comparison of two `Type *` — it is a
comparison of two integers the type table handed out per object. No expectation and no
`known_fail` was touched on any probe.

## 3. The related reading on the frozen graph's content

`src/passes/opt/generic/rewrite.cpp:213-272` — `contentHash`, the key of `RewriteMemo::leftAlone`:
it hashes the raw `GCNode` chunk bytes (so `typeId` is in it, `:243`) and then mixes four raw
pointers (`:263-266`, `graph.funcType()`, `runtimeDataType()`, `staticDataType()`,
`closureType()`). Two graphs that differ only in which object a type is get two hashes, so the
memo cannot recognize a graph it has already rewritten.

`src/runtime/draft_session.cpp:655-673` — `sameContent` compares `funcType()`, `closureType()`
and `staticDataType()` by pointer and `memcmp`s the raw node records, so the same difference is
read as "the content changed": a new revision, and every `ProgramSpecializationKey` names a
`baseRevision` (`include/camel/runtime/specializations.h:76-82`).

Both were measured only through their effect: probe 1 (the specialization key by structure) and
probe 2 (`tensorOf` returning the core's primitive singleton) each turn `export_fold` from a
10.04 s timeout to a pass in 0.36–0.38 s.

## 4. Questions for the author

1. Is a frozen node's type meant to be a `TypeId` at all, or should a record a rewrite re-encodes
   name an interned type so that one type is one id? Today `typeIdOf` adopts, so the id is an
   object identity and `GCNode::dataType()` returns whichever object that id was made from.
2. If a record's type is meant to stay an id, is the writing-union invariant
   (`graph_edges.cpp:196`) meant to compare ids or types? Probe 4 needed the types.
3. Is `nodeContentHash` / `contentHash` / `sameContent` meant to treat two objects of one type as
   the same content? If yes, the type has to enter those hashes by structure, which needs a
   structural hash the type module does not have (`camel/core/type/text.h` offers text and
   `typeEquals`, not a hash).