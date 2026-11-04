# Four references, one function, two moments -- and no case that can reach it

## What changed

`EdgeChecker::checkBranchBackReferences` and the encoder now run the **same** function,
`camel::runtime::checkGraphCrossReferences(const GCGraph &)` (`include/camel/runtime/graph_ref_check.h`,
implemented in `src/runtime/graph_edges.cpp`):

| caller | when | what it does with a violation |
|---|---|---|
| `EdgeChecker::run` (`src/runtime/graph_edges.cpp`) | a frozen graph is checked | appends it to the violations the checker reports |
| `encodeDraftGraph` (`src/runtime/draft_session.cpp#encodeDraftGraph`) | a draft is about to be published | refuses the encode: `std::runtime_error` naming the graph and the reference, which the pass boundary reports as `PassInternalError` naming the pass |

The four checks, unchanged in wording: `BRCH.join` in range and a JOIN; `JOIN.brch` in range and a
BRCH; a branch and its join naming each other; a region's `owner` and `result` in range. The encoder
ran none of them before: a draft could hold a number its own graph could not resolve, be published,
and trip a reader somewhere else.

## The hit count, measured

**Zero.** With the check wired into `encodeDraftGraph`, the whole plan tree is green:
`[SUMMARY] total: 8359, pass: 8347, fail: 0, skip: 10, known failures: 2`. No program in the suite
builds a draft with a reference that does not resolve. So this is a **refusal**, not a report: it
costs one pass over the graph's nodes and regions per encode, and it fires on nothing today. That is
what a boundary check is for, and it is also why there is no case for it.

## Why there is no case (two instruments tried, both measured)

1. **Corrupt `BRCH.join` from a pass.** `GraphDraft` has setters for a region's result and owner and
   **none** for `BRCH.join` / `JOIN.brch`, so the only door is the raw payload:
   `DraftNodeView::payloadAs<DraftBrchPayload>(node)->join = 9999`. It compiles, and it is not sound:
   the same write produced

   ```
   graph 'main' branch 4: its join is node 0, which is not a join        (run A)
   graph 'main' branch 4: its join is 24469 and the graph has 8 nodes    (run B)
   ```

   Two runs, two different numbers: the encoder rebuilds branch numbering from the draft's arms, not
   from the payload field that was written. A case built on that instrument would be flaky, which is
   worse than absent, so the pass and the case were removed rather than committed.

2. **Corrupt a region's result.** `draft.setRegionResult(1, 9999)` is accepted, the graph encodes,
   the program runs, exit code 0 -- the number was remapped to a node of the graph on the way in. The
   region half of the four checks is a range test the encoder cannot be made to fail.

So the honest statement is: **the check is in place and shared; nothing in the tree trips it; and no
draft-level instrument can be made to trip it.** A case has to wait for a typed `setBrchJoin` on
`GraphDraft` (the right fix -- not another raw payload write) or for a debug hook that freezes a
graph with a chosen number.

## The rule this round produced

**A check with no case is a claim, and a claim needs a stated reason to be believed.** The reason
here is not "it is hard to test"; it is that both instruments were tried and both are measurably
broken (one nondeterministic, one remapped). That distinction is what makes the remaining absence a
decision instead of an oversight.