# task-313 handover — the facts store keyed on the graph's identity

Written when the re-key half is delivered: the store keys on identity, stamps real revisions, keeps a
fact across a version of its graph, and has the check that fails without each of those. The next
reader continues on `lane/task-229` at `48412ffeff`; nothing is pushed, no branch was opened.

## 1. Where the work stands

Delivered in `6c09a8562b` ("facts: key a stored fact on the graph's identity, not on the graph
object"), on top of the upper half `b4b39467e7`:

```
GraphAccess::identity   new hook, the graph's identity on its version line; wired in
                        frozenGraphs()/draftGraphs() to runtime::graphIdentity
                        (GCGraph::id / GraphDraft::id)
GraphAccess::version    now runtime::graphRevision (GCGraph::revision / GraphDraft::revision);
                        the constant 0 a frozen graph answered is gone, and
                        runtime::graphContentVersion is deleted (one question, one place)
GraphAccess::nodeVersion  wired for frozen graphs to runtime::graphNodeVersion
                        (GCGraph::nodeContentHash) -- the first owner answer for the reported-read rule
Store::Key              {kind, identity, node} instead of {kind, const graph::Graph *, node}
StoredFact::versions    vector<VersionMark>{identity, revision}, Entry::versions the same
versionsOf              de-duplicates by identity (two anchors on two versions are one graph read)
dump / forEach          the stamp prints the identity, not a graph address
no-identity rule        an access with no owner has no identity: find answers Absent, putValue
                        refuses by name, obtain answers without storing
```

Delivered in `65923248a4` ("facts: two versions of one graph name the same anchor": `sameAnchors`\ncompares identities, and the revalidation check that was red before it) and `48412ffeff` ("facts: a\nversion ending leaves the fact to another version of its line": the version line, the identity\npassed in by the holder, and the two line-lifetime checks).\n\nReadings, seven plans (the eighth, `facts_readers_sweep`, was retired upstream as
`D-1127-238`; the new denominator is therefore `326/229/4/93`, not the card's `327/229/5/93`):

```
[provenance] tip 6c09a8562b193466bb3b182477e4e122f7f82620 | binary built 2026-11-28T03:13:41+08:00
[SUMMARY] total: 326, pass: 229, fail: 4, skip: 93      (= the base at faf02396a6, word for word)
```

The four failures are the base's own (`types_and_node_kind`, `node_kinds_match_the_graph`,
`constant_folding_makes_a_value`, `constant_folding_survives_a_safepoint`).

## 2. The open question, and how it was settled

**Settled (the Lead ruled 甲): the acceptance reading is the framework store's own.** The measured
question was which store answered `FACTS node_other`; it is the runtime store
(`Context::facts()` returns `camel::runtime::fact::FactStore &`), and this card re-keys the
framework store (`camel::facts::Store`). The reading that verifies this card is therefore produced
inside `camel-facts --self-check`, not by `test::facts_node`.

Three readings now exist in `test/tools/facts/main.cpp` (`runSelfCheck`), each shown to fail before
its fix:

```
one identity, two revisions, a node nobody touched
    -> probe == Status::Revalidated    (failed with "a fact about a node nobody touched is
                                        revalidated across a version of its graph" until
                                        `sameAnchors` compared identities, not pointers)
two versions alive, one ends
    -> the fact survives, and is found on the other version
                                       (failed with "a fact survives one version of its graph
                                        ending, while another version of the line is alive" when
                                        `graphGone` was reverted to clearing on the first version)
the last version ends
    -> the fact is gone                (failed with "a fact is cleared when the last version of
                                        its line ends, and not before" when `graphGone` was a no-op)
```

`test::facts_node`'s `node_other` is the runtime store's answer; it is carried by the other card
(乙), not this one. The comparison that card needs is `internal/reports/task-313-two-stores.md`.

### What the store now does when a version ends

`Store::noteGraph` records the versions of each identity the store was told are alive;
`Store::graphGone` re-anchors a fact of that identity onto another live version of the line, and
clears it only when no version can be reached any more. Clearing (`clearSlotsOfLocked`) skips an
anchor whose graph has ended instead of reaching through it; the four clearing sites (`~Store`,
`clear`, `invalidate`, `invalidateGraph`) all use it.

**The identity is passed in, never asked for.** `holdGraph`/`forgetGraph` take it from the holder
(`toValue(graph->id())` in `program.cpp`, `~GCGraph`, `~GraphDraft`): a graph is announced while it
is still being constructed, and a `dynamic_cast` of a half-built object is undefined behaviour --
it segfaulted `camel inspect --fact` until the manager started handing its own id over. The store
keeps the pointer only to know when it ends; it never dereferences it.

`invalidateGraph` keeps its meaning: the facts of this identity are void.

### The second hazard, and why it was left alone

`Store::find` checks the stamp before the reported marks. That is right, not a defect: equal stamps
*are* `Valid`; the marks only decide a fact whose stamp moved. It was not the red cause, so it was
not touched.
## 3. What is left, in order

Nothing on this card's own four points: 甲 (the framework store's own acceptance reading), (c) (the
version line) and (d) (the census and the store-to-store table) are all delivered and measured.

What a next reader would pick up, in the order the Lead set:

1. **乙** -- the runtime store's `Revalidated` production. Its card is the Lead's to open; this card
   is its prerequisite, and `internal/reports/task-313-two-stores.md` is the table it is opened
   from. The one row that is 乙's whole subject: `Dependency::Cone` revalidation, which only the
   runtime store does.
2. The two rows of that table that are *not* work but a decision, if the Lead wants the two stores
   to agree in words: `Dependency::Node` says `valid` where the framework says `revalidated` for
   the same judgement (change a word, not a rule), and the two stores disagree on what one version
   object ending means (framework keeps the fact, runtime erases the identity's entries).
3. `core.arrangeable` reads a region: `GraphAccess::regionVersion` has no owner yet, so a fact that
   reported reading one keeps the conservative rule. That is the design, not a defect, and it is the
   one reading to state separately rather than fix here.

## 4. Boundaries

- `src/kernel/facts` + `src/kernel/include/camel/runtime` + `test/tools/facts`: this lane's only
  write surface. `graph_encode` invariants (`task-312`) and the onnx retirement account are not
  touched. `invalidateGraph`'s meaning was not changed.
- No push, no new branch. Build and plan runs only through
  `internal/tools/camel-remote.ps1 -Jobs 1 -SlotWaitSeconds 1800 -QueueOnTreeLock`.
- The denominator is `326/229/4/93` (seven plans); a number that differs from it is a change to
  explain line by line, not a baseline to re-take. The eighth plan, `facts_readers_sweep`, was
  retired upstream (`D-1127-238`): `327/229/5/93` -> `326/229/4/93`, the difference being exactly
  the one failure in the retired plan.
- Anyone reading facts readings from another lane: the store's behaviour changed in these commits
  (identity keys, real revisions, a node-version answer for frozen graphs, and facts that outlive a
  version). If a lane's reading moves, tell the Lead so it can tell that lane.## 4. Boundaries

- `src/kernel/facts` + `src/kernel/include/camel/runtime` + `test/tools/facts`: this lane's only
  write surface. `graph_encode` invariants (`task-312`) and the onnx retirement account are not
  touched.
- No push, no new branch. Build and plan runs only through
  `internal/tools/camel-remote.ps1 -Jobs 1 -SlotWaitSeconds 1800 -QueueOnTreeLock`.
- The denominator is `326/229/4/93` (seven plans); a number that differs from it is a change to
  explain line by line, not a baseline to re-take.
- Anyone reading facts readings from another lane: the store's behaviour changed in this commit
  (identity keys, real revisions, a node-version answer for frozen graphs). If a lane's reading
  moves, tell the Lead so it can tell that lane.