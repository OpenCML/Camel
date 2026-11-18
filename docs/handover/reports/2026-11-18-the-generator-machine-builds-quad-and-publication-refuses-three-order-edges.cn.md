# The generator machine builds `quad`; publication refuses it on three order edges

Date 2026-11-18. Lane `lane-genbody`, worktree `D:\Projects\Camel-wt\genbody`, base `87ad8f5f9`.
Machine: `src/compile/gir/builder_generator.cpp` (`Builder::buildGeneratorBody`).

## What landed

The pc dispatch, the arms, the pack and the loop that names them — the segment the previous
report stopped in front of — are written, and one category of body reaches them:

* **elaboration**: one arm per `yield` plus one for "the body ran out of points"; the `else` of the
  branch for `k` is the branch for `k+1`; the last branch answers "stop" on **both** sides (the
  target's own last branch does: `g9n47`'s two arms both `stopHere`);
* **the loop**: state pre-split into `(answer, point)`, one port per part in each region, the round
  state rebuilt where the loop's value is read, counter port as the third `with` input;
* **the answer**: `(element, closure)` FILL, cast to `answerType`, packed with the next point, and
  the step's exit behind a GATE that waits for the LOOP.

Everything else is refused **by name** at the seam (`refuseShape`): a statement that is not a
`yield`, or a `yield` whose element is not a literal. That covers `count` (a local across a yield),
`sign` (a yield in an arm) and `reading` (a closure before a yield), in the order they are next.

## The wall: three order edges into an arm

With the construction tree's claim lifted for a probe, `quad` builds to the end and the published
graph is refused by its own invariant:

```
graph '__gen_step__': order edge 15 -> 17 enters region 3; an arm or a part of a loop runs as a
whole, so the order it has with the code around it is written on the node that owns the region
```

and the same for `15 -> 33` (region 4) and `31 -> 33` (region 5). Three edges, one cause.

`Builder::recordWrite` (`builder_expr.cpp:636-666`) orders a write after every pending **read** of
the cell. The read is the `__eq__(pc, k)` condition, built in the **loop body's** region; the write
is the arm's `:op/assn_l`, built in the **arm's** region. `regionExit` is false (the exit is the
write, not a JOIN or a LOOP), so the order edge is written across the region boundary — the one
thing 4.6 forbids. The comment at `builder_expr.cpp:629-631` says exactly the opposite ("a read in
the region around the write … stays pending"), and the target agrees: it holds
`g9n11(:var/get) -control-> g9n54(JOIN)`, the read resolved *at the JOIN*, and no order edge into
`g9r3`. **The code and its comment disagree here, and the target is on the comment's side.**

The fix is one condition — keep the use pending when the read's region is not the write's — and it
is in `builder_expr.cpp`, not in this lane's file. It was **not** made: it changes a rule every
switch arm in the language shares, and it belongs to a reading of its own (and its own plan run),
not to the tail of this one. This is what the next round should open with.

## Readings taken

* six plans, each on its own: `compile` 2627/2610/0/17 (the plain baseline, not 2630 — no other
  lane's cases here), `linear` 869/864/0/3/2, `opt` 1594/1594/0, `parse` 121/121/0, `para`
  104/104/0, `tools` (see the lane's report line). Nothing regressed: the machine is still
  unreachable from a program, because the construction tree claims every `yield` (`gct/builder.cpp`
  :5778, unchanged and restored after the probe).
* the target's `quad` step graph `g9` is 70 nodes / 95 edges / 8 regions; the closure graphs are
  `g3` (one DATA) and `g17` (the arm's, capturing `__gen_pc__`). The ruler for the spec's second
  number is still open: `nodes` matches exactly, `edges` does not (see the previous report).
* measured difference already known and **not** hidden: this machine builds the state `(pc,)` in the
  **arm** and captures it whole, where the target builds it inside the closure from a captured
  `__gen_pc__`. Same value, one node fewer inside the closure.

## Instrument

The probe (a `_genbody` case, a temporary plan, and a one-line lift of the construction tree's
claim) was removed before the commit; the tree carries no trace of it.