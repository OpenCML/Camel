Fixtures for `docs.snippets_dump_is_compared`.

`dump_stale.md` is a page whose graph dump does not draw the program: the checker must run the
fence, compare the dump with the `text` block, and fail naming the first line that differs. The
mismatch is deliberate and the page says so, because a reader-facing page never holds one. The other
half of the comparison -- that `origin`, `span` and `srcfile` are normalised before it, so a dump
taken from another file and another layout is accepted -- is pinned by the pages themselves: the
tutorial's whole dumps (`01_five_minutes`, `09_evaluation_by_dependency`) carry the path of their own
`examples/` file, and the checker compiles the fence out of a temporary directory.

The fixture is checked by naming the page as the checker's scope, never by `--default-roots`: a
fixture is not a reader-facing document and must not be compiled by the gate's own scan of `docs/`.