# A page the zero reading reads

This page is a fixture, not a reader-facing page: `--default-roots` does not walk it, and the
pending list holds nothing for it. Its fence does not compile, so a run over this page has one
problem to name.

`test/plans/feat/docs_links.plan.toml`'s `docs.snippets_pending_zero_says_why` reads this page with
the pending list read as empty: the list holds no entry for this fence, the fence does not compile,
and the run has to say that nothing waits *and* refuse on the same page -- the two halves of one
reading, neither of them borrowed from the reader-facing scope.

```cml
func main() => int sync {
    return an_operator_the_language_does_not_declare(0, 0)
}
```