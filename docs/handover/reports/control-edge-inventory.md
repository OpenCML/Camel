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