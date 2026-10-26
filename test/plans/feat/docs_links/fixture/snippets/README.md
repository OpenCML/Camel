Fixtures for `docs.snippets_dump_is_compared`, `docs.snippets_dump_excerpt_matches_up_to_renumbering`,
`docs.snippets_dump_excerpt_different_operator_fails` and
`docs.snippets_dump_excerpt_number_may_not_be_reused`.

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