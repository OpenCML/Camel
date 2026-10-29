# The place of a spliced body's chain is resolved before its code is in

## What the graph showed

`test/cases/opt/generic/static_branches.cml` under `std::opt`, the recursion unrolled three times;
each round prints `tick N` and only `tick 3` ran.

- `N15 -> N1B` (sync+ctrl) enters the arm's chain from the branch; `N1B -> N1D -> N1F` are sync, so
  the three rounds' statements are one chain in order.
- `N1B` has **two** sync users, `N1D` and `N17` (the statement after the call): a chain that forks,
  which is no chain (`checkSyncEdges`, 5.3). The layout took the `N1B -> N17` route and the rest of
  the chain was reached by nothing.
- That fork edge is `println(origin 4567) -> println(origin 4604)`, an **earlier** round. Traced by
  origin, not id, because one graph's ids do not name another's nodes; every prune's hand-off was
  checked the same way and none pointed at a println -- they hand the place to a GATE, with a join
  place available.

## Why

`inlineCallableInDraft` reads the body's chain end **when it splices** (`bodyTail`) and writes the
statement after the call as following it. The body's own calls are spliced in later rounds of the
same pass and each extends the chain past `bodyTail`, so the snapshot describes a chain that no
longer ends where it was read. An ordering defect, not a question of semantics: which place a
statement has is not being changed, only when it is read.

## What was tried

- (a) Hand the place to the chain predecessor of the body's **value** rather than to "the last
  statement not followed in the body".
- (b) **Chosen.** Keep the splice, resolve the place again once the code is in: since a statement of
  a block follows one other at most, a statement that finds itself following one that a second
  statement follows moves to the end of the chain growing from it (`resolvePlaceForks`, called after
  each splice, idempotent). Refused when the target waits on the statement moved, which keeps the
  graph off a cycle of inputs.
- (c) Re-join the whole block after the merge. **Rejected**: it chains a captured `var`'s writes in
  layout order, moving a write past reads the capture rules keep it apart from -- the 18
  `var_capture` / `captured_write_order` failures came from that one call.


Walking sync users alone, without the region and cycle guards, segfaults on `static_branches`,
`recursion_unroll` and `shape_unroll`: it reads through a region the erase takes away.
