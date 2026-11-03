# Two readings of one judgement, and the measurement that says they cannot be one function

## The CTRLONLY reading: what its print condition actually was

The instrument that produced the `CTRLONLY graph=... node=...` lines was, verbatim:

```cpp
    const bool syncUser = !graph.syncUsersOf(ref).empty();
    const bool ctrlUser = !graph.ctrlUsersOf(ref).empty();
    if (!syncUser && ctrlUser) {
        std::fprintf(stderr, "CTRLONLY graph=%s node=%u\n", graph.name().c_str(), static_cast<unsigned>(ref));
    }
    return syncUser || ctrlUser;
```

So the condition was **"no sync successor** and has a control successor" -- not "has a control successor".
Everything above that line in the function has already returned for a data user and for the graph's
result, so each line meant: no data user, not the graph's result, no sync successor, one control successor.

`guard` measured on the `std::opt std::gir` graph that `__root__:60` **has** a sync successor
(`N3C -> N3E [SYNC]`) and a sync predecessor. Both readings cannot be of the same graph, so the
`CTRLONLY` lines are **retracted as evidence about the printed graph**: they are a reading of the
program at the `std::opt` pass boundary (`CHECKPOINT pass=std::opt`, three nodes:
`__root__` 60, `arm` 10, `main` 63), not of the graph `std::gir` prints. The boundary's node numbers
are the `GCGraph`'s; the printer's names are its own. What the three lines do establish is the
checkpoint they were taken at, and nothing about the printed graph.

**Still useful, and not thrown away:** the three lines are a *checkpoint reading* -- at the `std::opt`
boundary, three nodes (`__root__` 60, `arm` 10, `main` 63) had only a control successor at that
moment. **They say nothing about the final graph** and are not evidence of a defect.

## The open question is closed: `broken = 0`

This page left one question open -- whether `syncUsersOf` and `syncPredsOf` disagree on a freshly
encoded graph (which would be a defect in the credential function, worse than slice 3b). It was
measured systematically: at every pass boundary, over every graph, for every edge of the three kinds,
the two directions were compared.

**The first instrument was wrong**, and its wrongness looked exactly like a finding: it returned a
`std::span` built over a local `std::vector` inside a callback, so the span dangled and the reverse
lookup read freed memory. It reported `edges=901343 broken=68520`, with examples like
`EXAMPLE sync graph='main' 3->4 users=1 preds=0`. That is an instrument bug, not a code bug -- and it
is the third time this lane's instrument, not the code, was the thing that was broken.

Rewritten without the temporary span (three explicit loops, one per edge kind), the same gate run
gives:

```
edges = 901343    broken = 0    checkpoints = 4998    examples = none
```

So **the accessors are mirrors in both directions** for normal, sync and control edges, and the
question is void: the `CTRLONLY` lines and the printed graph are two different graphs. **No defect
follows from this, and slice 3b's priority is not raised by it.**

## P0-4: converge the liveness model onto the boundary's rule -- tried, measured, reverted

The plan was: `EffectLivenessFacts::isSyncStatement` becomes `hasSyncSuccessor`, filled from the
successor side everywhere, so that the two models answer with one direction. It was implemented --
renamed, all six callers repointed, `effectStandingOf` and the docs updated -- and the fast gate
returned:

```
[FAIL] specialized_arg_keeps_order.opt.nvm
[FAIL] specialized_arg_keeps_order.simplify.fvm
[FAIL] specialized_arg_keeps_order.macro_simplify_cse_dce.nvm
  Runtime graph '__root__' has GATE node 6 without a wait: no sync predecessor and no control input.
```

A **GATE** in `__root__` carries its wait as an **incoming** edge. Told to look only forwards, the
liveness model answered "this gate carries no wait", and the seal check refused the graph. That is
the "delete it and there is no answer" case, measured, so the change was reverted whole. The tree is
back at `386634e57` and green: `[SUMMARY] total: 8354, pass: 8342, fail: 0, skip: 10, known failures: 2`.

## Why the two models legitimately differ, caller by caller

| caller | fact filled from | what it needs the edge for | successor-only |
|---|---|---|---|
| `fusion_group.cpp:94` | `syncPredsOf` (+`hasSyncEdges`) | may this node be taken into a fusion group as private? | loses a chain's tail |
| `gates.cpp:33` | `syncInputsOf` | does this gate carry a wait? | **loses every gate's answer** |
| `keepshape.cpp:100` | `syncInputs` or `syncUsers` | diagnostic text | no loss (already both) |
| `draft_inline.cpp:1759` | `syncInputsOf` | is the call inside a block? | loses the block's first call |
| `graph.cpp:576` | `syncPredsOf` | the gate/wait question again | **the measured failure above** |
| `writings.cpp:1148` | `syncInputsOf` | is the member a statement of a block? | loses the head |

**The boundary asks "does something make this run?"** -- a successor is the evidence, and only a
successor: an edge into a node says the node before it was placed.

**The liveness model asks "is this node part of a chain, and does it carry a wait?"** -- there an
incoming edge is the evidence, because a gate waits on what comes into it and a chain's head has
nothing before it.

## What was learned, in one line each

- The two models diverged for a historical reason (the boundary was narrowed first, and its
  successor-only reading was measured: **0 predecessor-only effects over the whole fast gate**), not
  because one of them is wrong about its own question.
- Forcing one direction on both is not convergence: it deletes a caller's answer and the deletion is
  visible as a refusal (`GATE node 6 without a wait`).
- The honest form of "one judgement" here is **one documented rule per direction, each naming which
  side of the edge it reads and why** -- not one boolean that means two things.

## Queued, not done

- Measuring whether `syncUsersOf` and `syncPredsOf` disagree on a freshly encoded graph (the open
  question above) belongs to the lane that can print the graph.
- The boundary's control-edge credential is still live; the guard lane's slice 3b decides its fate
  (see `what-an-effect-stands-on.md`), and the retracted `CTRLONLY` lines are not evidence for it.