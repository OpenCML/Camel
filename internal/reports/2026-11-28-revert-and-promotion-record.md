# 撤回与晋级记录（2026-11-28）

写者：合并位 2 号（seat-merge-2）。落库：协调者。这张页是当晚那一轮"撤薄壳 + 整份重采金标 + 晋级"的原始记录，读数与口径句都在文内，不再改动。
# Revert + promotion record — 2026-11-28 (seat-merge-2)

## What was withdrawn, and why

The thin-shell export batch (`lane/onnx-run`) arrived at 22:11 on 2026-11-27 as
`504d8fbc46`, with a dynamic-axis follow-up `601873fa3d` at 22:25, and it was
arriving with thirty `pass_export_*` reds and three `registry.*_match` reds.
`task-342` (the re-pin that would have fixed them) did not come back by 01:30,
so the batch came out.

The merges came out first, newest to oldest, and the record and the reference
page it wrote came out after them:

| order | commit | what it is |
|---|---|---|
| 1 | `601873fa3d` | dynamic-axis follow-up, built on the thin shell |
| 2 | `504d8fbc46` | the thin shell itself |
| 3 | `e6639c4f6b` | the AST record the batch wrote (eleven rows) |
| 4 | `3abd89ba62` | the reference page the batch regenerated |

Each revert is a new commit; nothing was force-pushed. The arrival on
`integration` is a merge (`9c6f270c87`), because that ref's pre-push check
requires every arrival to be a merge.

## How the conflicts were resolved, and what that cost

Every conflict was inside the onnx subtree — `src/stdlib/onnx*`,
`test/cases/modules/onnx/*`, `test/plans/feat/modules/onnx.plan.toml` — and not
one spilled into another module. Each was resolved by taking the file as it
stood at `8e29d45d1c`, the merge's own first parent, because withdrawing both
merges must leave the onnx subtree exactly as it was before the batch:

```
git diff --stat 8e29d45d1c -- src/stdlib/onnx src/stdlib/onnx.cml \
    test/cases/modules/onnx test/plans/feat/modules/onnx.plan.toml
(empty)
```

The record was the exception. The conflict resolution kept the diff3 base
blocks — `|||||||` lines and the rows under them — so the record commit carried
170 lines that were not snapshot lines and three copies of two paths. `-Check
ast-hashes` refused to report a stale count at that tip (`exit 2`), which is
why the record was re-taken whole (`seat/golden17`, `a6ff436b95`) rather than
patched:

- "the record commit left diff3 base segments: the conflict resolution kept the
  `|||||||` blocks, so the record was re-sampled whole rather than patched"
- "the reverted tip's record was re-sampled whole because the conflict
  resolution had kept the diff3 base blocks -- 170 lines of them -- and the base
  blocks carried hashes from the withdrawn batch"

## The layer that decided it

Run at the reverted tip `9c6f270c87`, fast layer, `-Jobs 2`, host load 32.81 at
the start:

```
total 9859 / pass 9650 / fail 149 / skip 27 / known 33 / timed out 2 / re-run 2
```

Against the develop-side table (`8e29d45d1c`, same command, 231 distinct
names): **new 1, gone 83, both 148**.

- the one new name is `frontend.ast_record`, `known-cause: broken record at this
  tip` — the record the revert's own conflict resolution broke, not the
  compiler; it clears when the record is re-taken whole
- "the layer ran at the reverted tip; the record it could not read was the one
  the revert's own conflict resolution had broken, not the compiler"
- "the 34 names the withdrawn thin-shell batch was arriving with are absent from
  the table, so a revert that removes a batch removes its reds with it"
- "timed out 2 unchanged: transpose_loop_escape's two, present on the develop
  tip as well"
- "gone 83 = the dead-citation family's re-pinnings, which this tip brings in"

## Where develop went

```
8e29d45d1c..a6ff436b95  244 commits, 48 of them touching src/
ast hashes: 4688 file(s) checked, 0 stale   (read-only check at a6ff436b95 itself)
```

- "develop advances to the tip where the record was re-taken, not to the head:
  the two merges above it change source, and this layer's readings do not cover
  that source"
- "the base was read one minute before the revert; the arrival is a merge
  because a fast-forward would have dropped the two commits that landed
  meanwhile"

## Tables kept

- `D:\Temp\archive\postrevert-9c6f270c87-fails.tsv` — 149 names, the denominator
  for the next round (same compiler as `a6ff436b95`)
- `D:\Temp\archive\fast-8e29d45d1c-fails.tsv` — 235 rows / 231 names, the
  develop-side table used above
