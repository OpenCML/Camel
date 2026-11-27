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