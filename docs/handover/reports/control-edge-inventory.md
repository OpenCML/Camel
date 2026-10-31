# The control-edge deletion: what has to move, in what slices

State on this tree: the `SYNC` **node kind** is gone (`788391f5e`, "Delete the SYNC node kind",
2026-10-30) and nothing under `src/`, `include/` or `modules/` names `GCNodeKind::Sync`. What
remains is the `CTRL` **edge kind**, which `docs/design/effects/edges.cn.md` 1 and
`docs/design-principles.cn.md` 3.1 say is not one of a graph's three (data, sync, order). Two
cases hold the debt: `edges.no_ctrl_edge_kind` and `edges.gate_has_no_control_input`, both
`known_fail = ["gir"]` in `test/plans/feat/linear/effects_model_edges.plan.toml`.

The premise the plan header names -- "the sync chain carries the wait instead" -- is available:
`LinkType::Sync` is alive (6 construction sites; 28 checked-in pictures carry `type="SYNC"`), so
no slice below waits on the deleted node. Nothing was found that still leans on `GCNodeKind::Sync`.

## 1. Construction: 17 `linkNodes(LinkType::Ctrl, ...)` sites

| file | sites |
|---|---|
| `src/compile/gir/builder_control.cpp` | 8 -- 1359, 2803, 2861, 2891, 2912, 3538, 4013, 4201 |
| `src/compile/gir/builder_expr.cpp` | 5 -- 589, 793, 1551, 1704, 1733 |
| `src/compile/gir/builder_operators.cpp` | 3 -- 4122, 4396, 4420 |
| `src/compile/gir/builder_helpers.h` | 1 -- 686, the `linkCtrl` helper the others can use |

## 2. Consumers: 63 files, 6 name or build the kind, 57 only read

`LinkType::Ctrl` occurs in 6 source files: four build a CTRL edge (`builder_control.cpp`,
`builder_expr.cpp`, `builder_operators.cpp`, and the `linkCtrl` helper at
`builder_helpers.h:686`), two name it in a switch (`builder_support.h:85`, `types.cpp:21`).
`DraftEdgeKind::Ctrl`, `ctrlInputsOf` or `ctrlOutputsOf` occur in 59, two of which are builders;
the 57 that only read are `src/passes` 22, `src/runtime` 14, `modules` 9, `src/execute` 5,
`src/compile` 4, `include/camel` 3.

A reader can keep working while the kind exists; it cannot once the kind is `Order`/`Sync`.
So every slice must move the readers of the sites it deletes, not the readers of the kind.

## 3. Recordings: 26 pictures, 138 `type="CTRL"` edges

| directory | files | edges |
|---|---|---|
| `test/plans/feat/opt/golden` | 5 | 53 |
| `exemplars/*/expected` (4 dirs) | 4 | 42 |
| `test/plans/smoke/golden` | 3 | 18 |
| `test/plans/feat/consistency/golden` | 7 | 10 |
| `test/plans/feat/trans/golden` | 2 | 6 |
| `test/plans/feat/docs/fixture/images` | 2 | 6 |
| `test/plans/feat/compile/golden` | 2 | 2 |
| `test/plans/feat/golden/tutorial_examples` | 1 | 1 |

20 of the 26 sit under a `golden/` directory and carry 90 edges. Ten more copies of the same
recordings are not pictures: 4 `exemplars/*/run.log` (42 lines), 3
`test/plans/feat/docs_links/fixture/snippets/*.md` (3), and 3 plan files that quote edges
(`linear/effects_model_edges.plan.toml` 4, `linear/var_reference.plan.toml` 2,
`linear/captured_writes.plan.toml` 1). A slice that changes the graph re-records its own
pictures in the same commit -- never the code first and the picture afterwards, because the
commits in between are red.

## Numbers drift from the plan header

The header says fourteen construction sites, 53 reader files and 143 edges in 26 pictures; on
this tree the same greps read 17, 63 and 138. The header is a summary of the tree it was
written on, not a bound: this page is the count to plan a slice against.

## When a control edge stops being built, who keeps the order it kept

One paragraph, per family of the 17 sites, because a deletion without this answer is a guess.

**1. The `sync` chain of a block** -- `builder_control.cpp:2803`, `builder_expr.cpp:1551`,
`builder_expr.cpp:1733`, `builder_control.cpp:2891` (already deleted: see the slice below). The
replacement is the **sync edge**, built at the same place by `linkNodes(LinkType::Sync, ...)` /
`writeSyncStatement` with `lastSyncedNode_` as the predecessor. This is a rename rather than a
move: a sync edge orders like an order edge *and* carries "used" like a data port
(`edges.cn.md` 1.5), which is exactly what the control edge did there, and `syncInputsOf`
already holds the same pair.

**2. A `var` read after the write that must run first** -- `builder_control.cpp:1359`,
`builder_expr.cpp:793`, `builder_expr.cpp:1704`, `builder_control.cpp:4201`. The design's answer
is a **data edge**: the read's port goes to the write's output (`:var/after(cell, call)` when
the writer is a call through a function value, 5.6). Three of the four already have it, so what
stands there is an edge that only orders -- `builder_control.cpp:4201` says so in its own
comment -- and the replacement is an **order edge**. `1359` is the exception and cannot become
an order edge: the write produced no version, so the read has no data port to the write, and an
order edge would leave the write unexecuted (an order edge does not carry "used"). It waits on
5.6 for that writer, not on a swap.

**3. `BRCH` and the arm it owns** -- `builder_control.cpp:2861`, and the duplicate at `2891`
that this slice removes. The replacement is **nothing**: "`BRCH` 与臂之间没有边，只靠区域归属"
(5.2). The arm's result is a `JOIN` port and `JOIN` reads port 0, so a data edge already orders
`BRCH` before the arm and the arm before `JOIN`; the control edge added only the claim that the
arm runs because of the branch, which region membership already says.

**4. A `GATE`'s control input** -- `builder_operators.cpp:4396`, `4420`. The replacement is
**nothing**: 5.3 gives the GATE one data port and a sync predecessor. This is the clause the
second `known_fail` asserts, so it goes with removing that mark, last.

**5. Edges around a region's exit and a loop's iteration** -- `builder_expr.cpp:589` (a pair with
no data path by construction), `builder_control.cpp:3538` (a sync body's output before the new
state), `builder_control.cpp:4013` (the body after the pull it reads). `3538` is the `GATE` shape
again (5.3: the chain tail reaches the block's value, through a `GATE` when the value is shared);
`4013` has a data port for the element already, so the edge is redundant once that port is
checked; `589` has no data path by construction and must become an **order edge**, where the
sealing check (4.6) is what says whether both ends qualify.

**6. `linkCtrl`** (`builder_helpers.h:686`) is the helper the others call and goes with its last
caller.

## Slice 1: the duplicate sync-chain edge at `builder_control.cpp:2891` (deleted)

The BRCH-to-arm link was written twice: a sync edge and, beside it, a control edge, and the
comment said the control edge stayed "for as long as the chain of a block is carried as control
edges too". A sync edge orders like an order edge and carries "used" like a data port, so the
control edge said nothing the sync edge at the line above did not, and it is gone. Nothing
replaces it, which is why this is the smallest self-contained slice: no site has to be taught a
new kind, and the recording changes only where the pair appeared as CTRL.