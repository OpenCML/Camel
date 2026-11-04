# The reduced programs of result `adb67e081` of the CC2027 evaluation

`CC2027/eval/results/adb67e081/e9_reduced/` in `Camel-Archive` holds six reduced programs of the
defects that survived on the v0.2.43 pin. Four of them still failed:

| program | pipeline | what it did |
|---|---|---|
| `rc00_seed27.cml` | `std::opt std::nvm` | SIGSEGV |
| `rc03_seed30.cml` | `std::opt` | branch-arm invariant: `DATA 30 runs between BRCH 7 and its JOIN 6 and belongs to no arm` |
| `rc04_seed72.cml` | `std::opt::inline` | edge invariants: `node 24 lies on a cycle of inputs: 24 <- 27 <- 38 <- 24` |
| `rc05_seed104.cml` | `std::opt::fold` | branch-arm invariant: `GATE 28 reads OPER 21 (:mark/filter_arr), which lies in an arm of BRCH 24` |

They are copies, unedited, kept under the name the archive gives them. The expected behaviour of
every one is the behaviour the program has without the optimization passes: exit 0 and no output
(`rc05` reads `PROGEN_I0` from the environment, so the harness sets it).

`rc05_seed104` passes now that a pruned branch hands its place to a node of the arm's own region
(`pruneBranch`, `GraphDraft::armRegionOf`); its runs are in `feat/compile/fold_arms`. `rc04_seed72`
passes too: the cycle of inputs its run reported was the place-fork guard of the inliner
(`resolvePlaceForks`, `src/runtime/draft_inline.cpp`) walking four of the five kinds of input edge,
so a chain end that reached the statement being moved through an order edge only looked free; the
move closed the cycle and the seal named it. It is in `feat/opt/adb67e081`. `rc03_seed30` still
fails as recorded, with the same pipeline and the same message, and is in that plan as **work
owed**: red on purpose, with no `known_fail` mark, so the plan's `fail` count is the debt and
moves down only when something fixes it.

`rc00_seed27` is in **no plan**, and that is a gap rather than a decision: it crashes in the
scheduler (`std::opt std::nvm` SIGSEGV 10/10, while `std::opt` alone returns; `std::opt std::gir`
and `std::opt std::stats` are fine), which is the execution plan's business and not a rewrite's.
It is listed here so the next round of lane assignments sees it.

`rc03` and `rc05` are the same family -- a node placed by the folding where it does not run whenever
the statements around it run -- but they are not one defect, and not one rule: `rc05` handed the
branch's place into a nested region, and reads the value only that region computes (4.1 rule 3);
`rc03` leaves a node between the `BRCH` and its `JOIN` that belongs to no arm at all (4.1 rule 2).