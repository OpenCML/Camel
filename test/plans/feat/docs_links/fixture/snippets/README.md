Fixtures for `docs.snippets_dump_is_compared`, `docs.snippets_dump_excerpt_matches_up_to_renumbering`,
`docs.snippets_dump_excerpt_different_operator_fails`,
`docs.snippets_dump_excerpt_number_may_not_be_reused`,
`docs.snippets_entry_argument_reaches_both_steps`, `docs.snippets_fault_shown_by_the_page` and
`docs.snippets_fault_text_differs_from_the_page`, plus `docs.snippets_pending_zero_says_why`.

`pending_zero_sample.md` is the page that case reads with the pending list read as empty: its fence
does not compile and the list holds no entry for it, so one run prints the zero sentence and refuses
on the page itself.

`dump_stale.md` is a page whose graph dump does not draw the program: the checker must run the
fence, compare the dump with the `text` block, and fail naming the first line that differs. The
mismatch is deliberate and the page says so, because a reader-facing page never holds one. The other
half of the comparison -- that `origin`, `span` and `srcfile` are normalised before it, so a dump
taken from another file and another layout is accepted -- is pinned by the pages themselves: the
tutorial's whole dumps (`01_five_minutes`, `09_evaluation_by_dependency`) carry the path of their own
`examples/` file, and the checker compiles the fence out of a temporary directory.

An excerpt is compared up to renaming its numbers one-to-one onto the dump's, so three more pages pin that rule from both sides: `dump_renumbered.md` names every
node and the cluster differently and must hold; `dump_content_differs.md` writes `:io/print` where
the program reaches `:io/println` and must fail, because only numbers are renamed;
`dump_number_reused.md` writes one number on two nodes and must fail, because the renaming is
one-to-one and an excerpt may not quietly lose a node.

A fixture is checked by naming the page as the checker's scope, never by `--default-roots`: a
fixture is not a reader-facing document and must not be compiled by the gate's own scan of `docs/`.

Two pages pin the shape of the two steps a snippet is put through. `entry_argument_both_steps.md`
gives the program an entry argument in its command fence and names a scheduling pass, so the compile
step runs the program and needs the argument too: a compile command built without the page's `--arg`
fails there, and the run prints the block. `fault_shown_by_the_page.md` is a program that faults and
a `text` block that quotes the lines printed before the fault and the located diagnostic: the
compile step has to stop before the program runs, and the run step still has to compare the block,
which is the two streams of one run -- the program's lines and the compiler's diagnostic.

That comparison has a negative reading beside it, because widening what is compared is not a licence
to accept any block. `fault_text_wrong.md` is the same program with a block that declares another
index and another column: the page is red, and the failure names the line that differs. A run that
holds this page would be comparing nothing.