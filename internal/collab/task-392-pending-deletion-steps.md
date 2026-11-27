# The steps that delete the two pending entries, after `task-392` lands

`task-392` translates `math:abs_l`. When it lands, `docs/tutorial/examples/17_fact.cml` stops
reporting the law refuted and prints the reading chapter 17 teaches, which is what both pages'
`text` blocks already say. At that moment the two entries in `PENDING_ALL`
(`scripts/check-doc-snippets.mjs#PENDING_ALL`) are stale: an entry whose snippet compiles and
prints what its block says must be deleted, because the exemption outlived the gap. This card is
the step that does it, written while the files are not to be touched.

## What is deleted

Two entries, one per page, one key: `12ee3b07b5c68a43`, in
`docs/tutorial/17_teaching_a_law.cn.md` and `docs/tutorial/17_teaching_a_law.en.md`. Their reason
reads `math:abs_l is not translated, so the law cannot fire`. Both go; the list becomes
`const PENDING_ALL = []`. `PENDING_MAX` is not touched, `--no-pending` stays in the script and in
the plan, and no other entry exists to renumber.

## The steps

1. Confirm the gap is closed, on the build host, before touching the list:
   `out/latest/bin/camel docs/tutorial/examples/17_fact.cml` prints
   `sampled abs_of_nonnegative: proposition equality, evidence samples 100, fired 8 of 8, coverage
   integer scalar int: abs(x) = x` and then `9`. If the numbers differ from the pages' blocks, the
   blocks are updated to the actual text in the same commit -- that is a page edit, not a deletion,
   and it is what the checker compares.
2. A default-roots run now names both entries as stale; keep its text, because it is the reading
   that the gap closed:
   `node scripts/check-doc-snippets.mjs --default-roots --camel out/latest/bin/camel`.
3. Delete the two entries, leaving `const PENDING_ALL = []`.
4. Run the checker twice on the host and keep both tails:
   * without the switch: `no stale snippet in 195 reader-facing document(s): ... 0 pending
     snippet(s) exempted (0 of at most 10 entries in the list)` and exit 0, with the zero sentence
     printed above it;
   * with `--no-pending`: the same zero sentence and exit 0 -- the two runs now agree, which is the
     one state they did not share while the entries existed.
5. `node test/tools/run-tests.mjs test/plans/feat/docs_links.plan.toml` on the host: 27 of 27.
   `docs.snippets_pending_zero_says_why` reads its own fixture page and no entry of the list, so
   its line and its runtime must not move when the list shrinks -- if it does, the decoupling broke.

## The criteria

* `docs_links` 27/27 (`[SUMMARY]` pasted), on a clean tree, with the tip sha named.
* The two checker runs above, both exit 0, the zero sentence and `0 of at most 10 entries` visible.
* `node scripts/check-doc-links.mjs --citations`: 1244 of 1244 (a changed page text can move the
  count; it must be all of them, not most).
* `node scripts/stdlib-reference.mjs --verify-doc`: up to date.
* One commit, pushed to integration, touching `scripts/check-doc-snippets.mjs` and -- only if step
  1 found a different number -- the two tutorial pages. `origin/integration..HEAD` must hold
  nothing else.

## What is not part of this step

The zero-reading case is not flipped: `D-1128-50` made it read its own fixture page, so it no longer
observes how many entries the list holds, and its `expect_exit = 1` stays right after the deletion
for the same reason it is right now -- its page does not compile. If someone proposes flipping it
back to exit 0, the answer is that it is not reading the reader-facing list at all.