# Board prune, 2026-11-27 (batch 1)

Conditions, all three required before a task was deleted:

1. the task's status was `completed`;
2. this line was written **before** the deletion, from the task's own record
   (subject, status, the owner it was filed with, the readings its description names, and any
   commit sha it names);
3. the task was not created today (the high-numbered ids at the end of the board were not touched).

`open` / `doing` / `blocked` were not touched. Readings are summarised from the task's own
description; where the description states criteria but records no measured value, that is said
in the line rather than invented here. A sha the description does not name is written `no commit
recorded`. The owner is recorded because deleting the row removes it from the board.

Deletion note: a task can only be mutated by its owner or by the Team Lead, and nine of these ten
belong to other lanes, so this lane deleted only the one it owns (`task-22`). The other nine are
recorded above with their revisions and left for the Team Lead to delete. Deleting the board row
is bookkeeping; the record above is what keeps the evidence.

## Recorded in this batch (10)

```
task-2  | Merge block-regions and witness into integration | completed | owner: seat-merge-1 | r3 | readings: two lanes in one batch with one build; lane/block-regions tip d3c832d2b (block identity kept once, ArgsView.oper set by one helper) and lane/witness tip 8ed0263b6 (LAW_APPLIED reading, vjp-parity --reaches); the merge commit is the merge seat's | commit: d3c832d2b, 8ed0263b6 (the two tips merged)
task-4  | Paper D: the section 15 query and edit surface | completed | owner: cap-probe | r3 | readings: criteria stated (greps for the removed names return 0, §15.2 and §15.3 examples compile, two-argument replace matches three-argument output); measured values not recorded in the task record | commit: 1b8e5938 (lane tip), afbd88c52 (recreated base)
task-5  | Slimming: kinds-to-oper to zero, then judge the :op/ rename | completed | owner: lane-kinds-oper | r3 | readings: criteria stated (before/after counts from a re-runnable command, plans with real exit codes and SUMMARY); measured values not recorded in the task record | commit: no commit recorded
task-7  | Backend speed: rebase the timing lane and land its readings | completed | owner: lane-backend-speed | r3 | readings: criteria state workstation timings (named workload, before/after, re-runnable command); measured values not recorded in the task record | commit: bba8d2238 (lane tip)
task-8  | Fix the vacuous guard in runtime.oper_shape | completed | owner: lane-oper-shape-guard | r3 | readings: criteria stated (case compiles and runs, SUMMARY before/after with real exit codes, assertion shown red once then restored) | commit: no commit recorded
task-9  | Slimming 1: arrangeable judged against the fact layer | completed | owner: lane-arrangeable | r3 | readings: criteria stated (grep for the old names returns 0, plans with real exit codes and SUMMARY) | commit: 009a84267 (lane tip)
task-22 | Process traces left in src/test, four places | completed | owner: lane-misc | r3, deleted here | readings: criteria stated (before/after counts of the two greps, a process-trace check run, plans with real exit codes and SUMMARY, name-by-name against the 630-row red table with 0 new); measured values not recorded in the task record | commit: no commit recorded
task-24 | replace(exit(d), v): rewire the reader, keep the node, still refuse deleting an anchor | completed | owner: cap-probe | r3 | readings: criteria stated (the three examples' real exit codes and SUMMARY, graph_edits/graph_values/graph_function green, 0 new reds by name) | commit: no commit recorded
task-26 | Two gate reds: attempt/tags plans and the internal/perf file headers | completed | owner: lane-misc | r3 | readings: criteria stated (the three reds quoted before and after, the two gate commands at exit 0, 0 new reds by name) | commit: no commit recorded
task-27 | Rebase lane/transfer and lane/rescue-1 | completed | owner: lane-parked-rebase | r4 | readings: lane/transfer new tip c550850a6 (base 6d1816ce2), self-checks exit 0, one plan run total 10 / pass 10; lane/rescue-1 had no content of its own (patch already upstream), three plans: transforms 217/208/8 (those eight reds are on the tip already), gc_collections 73/73, showcase_s2 10/10 | commit: c550850a6, 6d1816ce2
```
## Recorded in batch 2 (10)

Format from here on: `id | subject | status | owner | revision`. The result and the commit live in
git and the backlog; this line only has to say which task it was.

```
task-54 | splitting a central header by role, first candidate by churn x rebuild cost | completed | owner: lane-block-hash | r3
task-58 | diagnostics streams: the three tensor assertions judged one by one | completed | owner: lane-enter-shape | r7
task-59 @hold | ref_boxes finalized off by 1-2: a collector statistic, awaiting a ruling | completed | owner: lane-parked-rebase | r4
task-62 | the C++ back end gives T.. a C++ type and writes CALL-shaped iteration as a loop | completed | owner: lane-ssa-residue | r4
task-63 | slow layer: ten feat.mm.gc_pacing reds judged one by one | completed | owner: lane-arrangeable | r3
task-64 | verifying -FetchFile "this run did not write it" on the build host | completed | owner: seat-merge-1 | r3
task-65 | test::loop_collect_summary: the state arity and the lost source position | completed | owner: lane-witness | r5
task-66 | slow layer: four feat.mm.capacity reds judged one by one | completed | owner: lane-oper-shape-guard | r4
task-69 | three generated files stale: stdlib reference, AI reference, questions index | completed | owner: lane-misc | r3
task-71 | remaining `let x = 0 .. n` sites used as arrays, judged one by one | completed | owner: lane-arrangeable | r3
```

## Recorded in batch 3 (10)

```
task-95  | rescuing the 46 uncommitted changes in the console-res worktree | completed | owner: lane-parked-rebase | r3
task-96  | closing out "one console resource": bounds.cpp, two doc pages, readings | completed | owner: lane-parked-rebase | r4
task-98  | wiring up the three block-space readings and finding who holds a 9.0 MB block | completed | owner: lane-backend-speed | r3
task-101 | an index page for internal/reports (278 pages, only ls found them) | completed | owner: lane-misc | r3
task-102 | worktree pool close-out: 86 registered trees cleaned toward 16 | completed | owner: lane-enter-shape | r3
task-103 | one new red: max_pool2d_window_transpose expectation vs implementation | completed | owner: lane-kinds-oper | r3
task-104 | regenerating the stdlib-signatures region of the AI reference | completed | owner: lane-misc | r3
task-105 @void | voided duplicate dispatch of the console-rescue rebase | completed | owner: lane-editview-commit | r4
task-106 | the Range-as-argument family: 11 cases that lost what they measure | completed | owner: lane-enter-shape | r3
task-109 | lane/cli-rework rebased and read, to judge it for the merge queue | completed | owner: lane-kinds-oper | r4
```

## Recorded in batch 4 (10)

Marker on the subject: `@void` for a voided dispatch, `@hold` for a finding that awaits a ruling.
Both are still deleted; the marker is what makes them findable later.

```
task-127 | native.para_unordered_reduce_callback_arity: defect or stale record | completed | owner: lane-witness | r4
task-128 | task-128: defunctionalize for streams, parts A and B only | completed | owner: lane-oper-shape-guard | r11
task-129 | two narrowing pins carry no file name, so they cannot be checked | completed | owner: lane-misc | r3
task-130 | the 84 reds whose own verify script exits 1, classified | completed | owner: lane-parked-rebase | r4
task-131 | the 103 reds that no longer compile, grouped by diagnostic and judged | completed | owner: lane-editview-commit | r3
task-135 | 15 rows of stricter-or-refused: loop state, empty array, branch and match types | completed | owner: lane-parked-rebase | r4
task-136 | the four after_narrowing verifies: five warnings, stale or changed | completed | owner: lane-editview-commit | r3
task-137 | the 16 output/value/count mismatches judged one by one | completed | owner: lane-oper-shape-guard | r3
task-140 | two records repaired: the unordered_reduce scaffolding and four narrow pins | completed | owner: lane-editview-commit | r3
task-142 | algebra rules: array_combine no longer merges like terms (evidence first) | completed | owner: lane-oper-shape-guard | r3
```
