# The sync chain's "used", the `sync`-block splice and the SYNC node: what the edge model leaves open

Lane H418 (`handover/h418-effects-model`), 2026-10-24. The source of truth is
`docs/design/effects/edges.cn.md`. Its §5 settles the model: there is no control edge (§1: a graph's
edges are data, sync and order), a sync edge carries "used" *and* orders and joins two statements
written directly in one `sync` block or the block's chain tail to the block's value (§4.6, §5.3), an
order edge only orders (§4), the `GATE` is the node that lets a value through once the chain has
passed -- one data port, one sync predecessor, no control input (§5.3) -- `std::opt::ctrl` becomes
`std::opt::order` (§5.4), and inlining a callable into a `sync` block needs no `BLOCK`: the body is
spliced flat, one **order** edge runs from the previous statement to each effect or may-fault node of
the body and one onward to the next statement, and the body's result takes the call's place in the
chain (§5.7, which also records that the `SYNC` node's old inlining role is superseded).

Two clauses of that model cannot be implemented on `origin/develop` without deciding something §5
does not say. Both were measured, and this file records the measurements and what this lane chose.
The `GATE` half of §5.3 was implementable and is landed (commit `a593759ac`).

## What was measured (host, this branch, 2026-10-24)

**1. `GCNodeKind::Sync` is produced in one branch of one function.** Every construction site is
`GraphDraft::addSyncNode` (`src/runtime/draft_construct.cpp:319`), and its callers are: the test
passes (`src/passes/test/{edges,chunks,facts,fault,graph_release,keepfn}.cpp`), and one production
site, `inlineCallableInDraft` (`src/runtime/draft_inline.cpp:1424-1440`), which builds a SYNC only
when a spliced body has more than one executable entry root *and* the call had external control
predecessors. Thirty other places consume the kind (back ends, the C++/CML/GIR/DOT exporters, the
generic passes, the loop model). Probes of three hand-written sync-body shapes through
`std::opt::inline std::gir` produced no SYNC node, and no golden file holds one. So the kind is
reachable from the language only in that one branch, and it is what the design says to delete.

**2. Moving the `sync` block's chain from control edges to sync edges leaves the layer red, widely.**
The builder chains a `sync` block's statements with `LinkType::Ctrl` at five sites
(`builder_expr.cpp:1139`, `builder_operators.cpp:3097`, `builder_control.cpp:2737`,
`builder.cpp:1244`, the `lastSyncedNode_` input of the return GATE at `builder_control.cpp:2860`).
Replacing those five with the sync edge (§5.3's chain, plus `kGCNodeFlagSyncStatement` on both
ends, which the builder already records for the branch and the loop) built and ran; the run was cut
after printing 60 failures and had not reached the end of the layer, so 60 is a lower bound. Two
error classes, verbatim:

    camel: internal error: Runtime graph 'main' has GATE node 12 without control input.
    camel: internal error: A sync edge joins one statement to the next one.

The first came from `validateRuntimeGraphPayloadImpl` (that rule is the one `a593759ac` fixed; it is
what the second measurement below is about). The second is `GraphDraft::setSyncInputs` refusing a
second sync predecessor, reached by the inliner and the macro rewrite, which find the statement
written before the call by reading its **control** inputs and then write the chain themselves. The
families that failed are `consecutive_sync_calls.*`, `arm_entry_after_inline.*`,
`arm_call_then_effect.*`, `value_ctrl_gate_anchor`, `entry_*_target_*`,
`specialized_arg_keeps_order.*`, `sync.ctrl_dedup_snapshot`, `gir.*_snapshot`, `cse.result`,
`fuse.result`, `library_gemm.*`, `grad_library.*`, `impl.pass.optimizer.run.*`, `boundaries.a*gir`,
and others. In two of them the program ran and printed the wrong result rather than failing
loudly (`library_gemm.tolerance*`, `grad_library*`), which is the part that says this is a model
change and not a bookkeeping one.

**3. The two rules `a593759ac` fixed.** With both old rules, a committed GATE whose wait is a sync
edge was dissolved into its input and the sync edge went with it (`SYNCGATE frozen_sync_edges=0
gates=0 gates_waiting_on_the_chain=0`); with only `dissolveUnorderedGates` fixed, the commit refused
it (`EDGES refused: Runtime graph '__root__' has GATE node 4 without control input.`). Both changes
are load-bearing, and `test::edges_sync_gate` (`test/plans/feat/opt/edges.plan.toml`) is the shape's
only producer.

## 1. After the splice, what makes the body's statements run?

§5.7 says the spliced body's effect nodes are reached by **order** edges from the previous statement
and to the next one, and §4 says an order edge only orders: it does not make anything execute. §7 of
the same section nonetheless says inlining copies the callable's body into the block, and the body
of a `sync` function carries its own chain, whose edges are sync edges and *do* make their statements
execute. The two readings differ exactly on the body's own intermediate statements:

- *Splice verbatim*: the body's internal sync chain is copied with it, the statements a spliced body
  writes directly are executable because the chain reaches them, and §5.7's order edges to the
  effect nodes are the extra constraint §5.7 asks for (they also cover the pure computations, which
  no chain reaches). Predicted shape: the call disappears, the chain runs
  `before -> body statement 1 -> ... -> body statement n -> body value -> after`, and the body's
  effect nodes additionally carry order edges. This is what measurement 2 did not produce, and its
  failures are the ones this reading would have to explain.
- *Order only*: the body's statements are ordered, not chained; a statement the body writes
  directly is executable only because it is an effect anchored by whatever reaches the block's
  result. The block then has **two** chains (the caller's) and the body's own statements are not
  statements of it -- which contradicts §4.6's "a sync edge joins two statements written directly in
  one `sync` block" if the body's statements are written directly in the block after the splice, and
  makes "used" (the fact a statement is executed for its effect and not for its value) unexpressible
  for anything inside the body. This is the alternative the failing families point at.

**Chosen until it is decided:** nothing -- no builder site was changed. The measurement above is the
record, and the sites stay as they are. This is the point to decide before the splice is written.

## 2. Is the caller's own chain edge to the spliced body's result a sync edge or an order edge?

§5.3 gives the `GATE` a **sync** predecessor as its wait, and §5.7 says the body's result takes the
call's place in the chain: if the value node is not the block's own, the result is reached through a
GATE, so the chain's tail must reach that GATE through a sync edge. §5.7's own sentence gives the
edges around a spliced body as order edges, "one from the previous statement to each effect or
may-fault node of the body, one onward to the next statement". Read literally, the edge onward to
the next statement is an order edge, and the chain would end at the body.

**Chosen until it is decided:** the tail-to-GATE edge is a sync edge, because §5.3's GATE needs one
and §5.7's order edges are what *additionally* order the body's effect nodes. No builder site was
changed for this, so nothing depends on the choice yet.

## 3. The branch statement's own link (`lastSyncedNode_ -> brchNode`)

`BUILDER` today writes a `Ctrl` edge from the chain's tail to the `BRCH` node of a branch inside a
`sync` block, with a comment saying why: the branch's selector is read once, and the arms must not
run before it. Under §5 the control edge is gone, and this one cannot become an order edge:
`isPureNode(Brch)` is true (`src/runtime/graph_edges.cpp:150`), and `checkOrderEdges` refuses an
order edge whose user is a pure node. §5.7's splice is the only ruling that touches branches, and it
does not mention the branch statement itself.

**Chosen until it is decided:** the edge stays a control edge. It is not in this lane's way, since
the seal check has no rule for the control kind, but it is the one builder link the two other kinds
cannot express, so a `std::opt::order` that has to see every ordering will not see this one.

## 4. The return GATE's second input (`modifierOf(resNode)`)

`builder_control.cpp:2860` gives the return GATE a control input from the `var` cell the result was
read from, beside the chain's tail, and the pass comment says it must be retained "even when its
value already depends on it". §5.6 expresses that ordering as the read's **data** edge to the writing
nearest before it, which is not implemented (`docs/handover/reviews/questions-2026-10-23.md`,
`h242a-capture-order-edge`). The chain's tail input of that GATE can become a sync edge without it
(measurement 2 did not fail on this line); the modifier input cannot be dropped until §5.6 lands
without losing the ordering.

**Chosen until it is decided:** the modifier input stays a control edge, and the chain input stays a
control edge with it, because dropping one of a GATE's two inputs while the other is decided by a
clause that is not implemented would make the graph say something about the model that is not true
yet.

## 5. What replaces the SYNC node as a statement anchor?

§5.3 deletes the `SYNC` node. The kind is consumed in thirty places, but the harder half is that the
repo uses it as *the* node that may carry an order edge -- and, after `a593759ac`, as the node a sync
edge may end on: `isPureNode` returns false for `Sync` (an anchor, "a call, a function, a gate, a
loop, a sync node ... is an anchor that may be an effect"), so every order-edge test pass builds its
anchors with `addSyncNode`. The nearest replacement, a `GATE` with no inputs, is refused by the rule
`a593759ac` wrote (`without a wait: no sync predecessor and no control input`) and is off-model
anyway (§5.3 gives a GATE one data port). An inert `OPER` with no payload is not pure either, but
nothing says what a graph may hold when an anchor carries no operator, and the back ends encode
operators, so this lane did not assume it.

**Chosen until it is decided:** the kind was not deleted. Deleting it needs the anchor question
answered first, and the answer is a language-level one: what node stands for "this piece of work has
an effect, so an ordering may name it" when the statement is not a call, not a gate and not a
branch. The measurement above (the kind is reachable from the language in one branch only) is the
evidence that nothing else depends on the deletion.

## What this lane landed

- `8213df8a6` -- the measurement of the model the design settled, pinned red as four entries of
  `test/plans/feat/linear/effects_model_edges.plan.toml` over one frozen input
  (`test/cases/linear/runtime/effects_model_edges.cml`). Two of them are still `known_fail`: the
  ordering between a `var`'s write and the read of it is still a control edge (§5.6, item 4 above),
  and an order edge does not exist yet where a control edge carries an ordering today.
- `a593759ac` -- §5.3's GATE: the runtime validation accepts a GATE whose wait is a sync
  predecessor and no longer requires a control input, and `dissolveUnorderedGates` keeps a GATE that
  waits on the chain, with `test::edges_sync_gate` as the shape's producer.

The remaining work is items 1-5 above, in the order the model needs it: the splice's "used" (item 1),
then the chain's two kinds (item 2), then the `ctrl`-to-`order` reclassification, which is where
items 3 and 4 have to land in the same commit as §5.6's read-to-write data edge, and then the SYNC
node's deletion (item 5).