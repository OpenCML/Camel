# The tiles pass publishes a body before the type that describes it

**Question for the language author.** Thirty of the thirty-two `gpu-plans` failures are one timing
question, and it is not the check being wrong. The reading is true. It is asked too early.

## What was measured

```
check=gpu-plans  total=146  pass=114  fail=32  skip=0   (twice, byte-identical)
32 failures = 13 rejection points;  "the assertion is stale" = 0 of them
```

Every rejection names a node and a resource — `'choose'` n11 `tiles:download` / **write
`tiles::driver`**; `constant_axis` n4 **`tiles:upload`**; n5 **`tiles:run_compiled`** / **read
`tiles::device`**. So the messages are specific, which is the positive control for "the check is
working". The problem is when.

```
region_outline.cpp:598   session.commit()      publishes the body -- with upload/download/run_compiled
region_pass.cpp:82       settleDeclarations()  then adds the type to that body
```

Between those two lines the graph on the table is a body whose effects are wider than the type
attached to it. The commit check is asked exactly there.

## Why this is known to be the pass's job

`7a70b5fad` (2026-11-15) fixed this same batch by **widening the type over the body**. Its three named
cases are `nofallback.branch_segment…`, `nofallback.branch_scope…`, `resident_loop_declares_its_blocks`,
and the graph in them is `'choose'`. Its commit message says the check, its width, the GPU plan
assertions and the exemptions are untouched. So the reading that just reopened this is the same reading
it closed — the widening that satisfied it stopped being enough.

Also: the same question is already asked at the pass boundary (`PassEffectContainment`), and this batch
passes there. So what the pass returns is self-consistent. The failure is inside it.

## Why the pass cannot simply build the type first

Attempted and withdrawn: `bodySignature` takes a `const GCGraph&`, but a draft is a `GraphDraft` and
only becomes a `GCGraph` after `encode()`, and `collectReachableRuntimeGraphs()` hands back the graph as
it was before the edit. The patch compiled, and the thirty-two readings did not move by one. So
"body first, type after" is the only order the current API allows — and it is the order this check
refuses.

## Default, and work started on it

**Default: the pass computes the footprint of the operators it inserted and widens the type before it
publishes -- no new API, no change to the check.** Reasons: (1) `7a70b5fad` is the precedent and it says
in its own message that the pass is what needed fixing; (2) moving the check back to the pass boundary
would stop asking it of a graph the pass hands over, which is the widest hole this week has been
closing; (3) a commit that knows it is inside a pass would be the check deciding its own deadline, and a
check that can be told to wait is not a check.

What this costs: the pass has to know its own insertion footprint rather than rely on the type being
carried over afterwards. That is more work in `modules/tiles` and less in the runtime, which is where
the defect has been both times.

The one case that is **not** this question: `grad_expand.scope_sees_the_derived_graph` has two
`tiles:download` calls in one region with no order between them, while `stdlib/tiles.cml:16` says every
call there keeps its written order. That is the pass losing an order, the check is right, and it is
recorded as a separate defect rather than folded in here.