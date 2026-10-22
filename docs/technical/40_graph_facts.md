# Graph Facts: Kinds, the Store, Stamps, Rewrites and Persistence

This note describes the graph facts of `docs/design/op-semantics` section 10 as implemented:
what a pass or a back end derives about the program is kept beside it, so that whoever asks
again gets the answer without deriving it again, and never an answer that no longer holds. The
headers are `include/camel/runtime/facts.h` (registry, store, stamps), `core_facts.h` (the core's
kinds) and `fact_persist.h` (the hooks a cache will call).

## 1. Three classes of information

| Class | What | Where | Stale? |
|---|---|---|---|
| (i) `GraphContent` | what the graph cannot give back if lost: the numeric permissions the program granted | the graph (the permission column) | never: a kind of this class reads the graph each time; `put` refuses it |
| (ii) `Derived` | what can be derived: shapes, index instances, reorder freedom, effect summaries, loop domains | the store, with a stamp | yes: a read checks the stamp |
| (iii) `Target` | what depends on the target too: layouts, schedules | the store for the process, with the target's hash; the back end's own compile cache across processes | yes, and when the target hash differs |

## 2. Kinds

A kind is registered once, `registerKind(KindSpec<T>{...})`, with:

- `key` `owner.name` and `owner`: `core` for the core's kinds; a module uses its own name, so
  `tiles.` is reserved to the module tiles (a kind `tiles.x` of another owner is refused);
- `version`, the C++ type of its values, its class and its scope (`Node`, `Graph`, `CrossNode`,
  `CrossGraph`);
- `dependency`: `Anchors` (the conservative rule) or `Cone` (with a `cone` hash, section 4);
- `readsDeclarations`: whether the operator declaration epoch enters its stamp;
- hooks: `compute` (the value at the anchors; also the validator), `print`, `encode`/`decode`
  (classes i and ii only), `anchorsIn` (where a CrossNode or CrossGraph kind has facts).

Registering the same key again with the same version, type, class and scope returns the same
id (a module loaded twice); anything else is refused with `std::invalid_argument` naming the key
and the rule.

The core's kinds (`core_facts.h`):

| Kind | Scope | Class | Value |
|---|---|---|---|
| `core.shape` | Node | ii, Cone | element type and extents of the node's value, from its type (where the compiler's shape inference puts them); read through a `ShapeReader` the tensor module registers |
| `core.instance` | Node | ii | the index instance of an OPER node whose operator declares one (`instanceOf`), or "none" |
| `core.reorder` | Node | ii | what its reduction may do (`reorderFreedomOf`) |
| `core.effects` | Graph | ii | the effect summary through calls and loops (`effectSummaryOf`) |
| `core.loopDomain` | CrossGraph | ii | a LOOP node and the graph that holds its body region: the form, `t < len(xs)` with the extent when the array is a constant, the element port (design 11.3) |
| `core.permissions` | Node | i | the permissions granted to the node |
| `core.resources` | Node | ii | what the node does to resources (`resourcesOf`, `docs/technical/41_resources_and_definitions.md`), at the nodes that touch any |

## 3. The store and stamps

`Context::facts()` is the program's store. `put`, `get`, `obtain` (get or compute and store),
`probe` (what a read finds), `verify` (recompute and compare), `invalidate`, `invalidateGraph`,
`clear`, `forEach`, `counts`.

A fact is indexed by its first anchor's `(GraphId, node)` and the kind; any number of anchors is
allowed. Its stamp holds `(GraphId, closure revision)` of each anchored graph, the declaration epoch when
the kind reads declarations, the target hash, and the cone hash for `Cone` kinds. Every read
compares them, in release builds too; a stale fact reads as absent and is dropped.

**GraphId and revision** name a graph and its content:

- `GCGraph::id()`: the graph's `GraphId` (`camel/runtime/graph_id.h`), allocated by the program
  (`Context::graphIds()`) and kept by every commit for the copy it encodes of a graph; a graph a
  rewrite creates gets a new one.
- `GCGraph::revision()`: a process-wide number. A commit keeps it for a graph it did not
  change and gives a new one to a graph that changed: one a pass asked to edit
  (`RuntimeGraphDraftSession::edit`/`rootDraft`) or whose unordered gates the commit dissolved,
  and that the commit did not encode back to the same content (`sameContent`: records, edges,
  payloads, ports, permissions, frame and static layout, static values, the graphs referred to,
  each by its copy; source positions do not count). `std::opt` asks to edit every graph; only
  those it really changes get a new revision. The revision is that of one graph.

A fact reads its anchors' graphs and graphs they refer to, so its stamp holds, per anchored graph,
`closureRevision` (`reachable.h`): a hash of the `(GraphId, revision)` of everything reachable
from it. This is the conservative rule of design 10.3, coarser than per-node revisions: an edit
of a callee makes the facts on its callers stale too. The footprint cache of `core.resources`
is keyed the same way.

## 4. Rewrites

Every rewrite of the program goes through a `RuntimeGraphDraftSession` commit (`std::opt::inline`,
`std::opt` and its parts, `std::macro`, specialization, entry arguments, `tensor::fuse`, the ONNX
export's session), so the revisions are maintained in one place. A fact computed
before a pass reads as stale afterwards when the pass changed what it read, and is computed again
on the next `obtain`; it is never reused silently. A `Cone` kind (`core.shape`) rehashes what it
read when a revision changed: when that is unchanged (the node's type) the fact stays valid with a
new stamp (`revalidated`).

A pass can also change declarations: loading a module declares its operators, and every
declaration increments `OperatorTable::declarationEpoch()` (`operator_table.h`). A fact of a kind
that reads declarations is therefore stale after any module load, even when the graph it is about
did not change. `std::opt` does this: its algebra phase resolves `rules::simplify`, which imports
the `rules` library, and that imports `math` (H100, `resolvePassWithModule` in
`src/execute/pass.cpp`); the epoch moves on its first run. The stamp is conservative by design
(10.3 lists the declaration epoch beside the graph revision, and a kind opts in with
`readsDeclarations`), so this is invalidation, not a defect: the recompute path is reached and the
facts are rebuilt. A case must not turn the resulting totals into the property — `FACTS epoch` and
`FACTS revision` say *why* a fact went stale, and the per-kind lines say which facts did (H121).

A detached copy (the working copy of autodiff) keeps the GraphId of its graphs; the graph that held a loop gets a new revision, so facts computed on the program are valid on the copy where nothing changed.

Facts do not follow a pass on their own (design 10.5: "derived facts are not stale notes that
happened to survive a clone"); `tx.keep` (a pass re-establishing a fact it maintains) and the
commit-time verification of kept facts are not implemented yet.

## 5. Persistence hooks

There is no module cache yet. `fact_persist.h` gives what it will call:

- `portableGraphHash(graph)`: the content of a graph and of everything below it, the same in every
  process (no GraphId, revision, OperId, TypeId or pointer; operators by URI, semantics key and
  traits; constants by type and value);
- `exportFacts(store, root)`: every valid stored fact of a Derived kind that encodes, and every
  GraphContent value of a kind that encodes, as `PortableFact` (kind, version, anchors by the
  graph's name in the program and node, the anchored graphs' hashes, the encoded value), `toText`/`parsePortableFact`;
- `importFact(store, root, fact)`: checks kind, version, graphs and hashes, decodes and stores a
  Derived fact; compares a GraphContent value with the graph.

Class (iii) is never persisted by the core (registration refuses encode/decode for it): a back end
such as Tiles keeps its own compile cache, keyed as design 10.6 says.

## 6. Tools and tests

- `camel inspect --facts <program>` computes every kind for the front end's program and prints one
  line `FACT {json}` per fact with its stamp, for example:

  ```
  FACT {"kind":"core.shape","version":1,"class":"derived","anchors":[{"graph":"layer","node":2}],"value":"f32 [2, 4]","stamp":{"revisions":[{"graph":"layer","revision":2}],"epoch":0,"target":0,"cone":"5ea9e00000000050"}}
  FACT {"kind":"core.reorder","version":1,"class":"derived","anchors":[{"graph":"total","node":1}],"value":"regroup, permute","stamp":{"revisions":[{"graph":"total","revision":3}],"epoch":68,"target":0}}
  FACT {"kind":"core.loopDomain","version":1,"class":"derived","anchors":[{"graph":"twice","node":5},{"graph":"twice","node":2}],"value":"for: t sequential, t < len(node 1) = 2; element port 2 reads xs(t)","stamp":{"revisions":[{"graph":"twice","revision":4}],"epoch":0,"target":0}}
  FACT {"kind":"core.permissions","version":1,"class":"content","anchors":[{"graph":"total","node":1}],"value":"reassociate","stamp":null}
  ```

  (`test/cases/facts/tensors_and_loops.cml`.)
- `test/plans/feat/modules/graph_facts.plan.toml`: the registry and store rules
  (`test::facts`), facts across `std::opt::inline` and `std::opt` (`test::facts_seed`, `test::facts_check`), the persistence round trip (`test::facts_persist`),
  and `inspect --facts`. The check pass prints the declaration epoch and the graph revisions it
  finds (`FACTS epoch check=`, `FACTS check revision`), a per-kind tally per graph (`FACTS graph
  kind <graph> <kind>`), and `FACTS after recompute valid=.. stale=.. gone=..`: after the stale
  facts are computed again no fact a consumer can read is stale. The test passes are named in the
  pipeline; no environment variable gates them.

## 7. Operators that read graphs

Independent of the store, but part of the same work for Tiles: an operator that reads the graph
of a function value it receives declares `OperatorTraits::readsGraphs` (Tiles' `run`,
`run_device`, `source`, `inspect`, `benchmark`; `onnx:export_model`; `autodiff:grad`,
`with_grad`). Every back end runs the program's own graph, so such an operator sees the LOOP of a
function and its regions wherever it runs (`docs/technical/39_loops.md` section 4).
