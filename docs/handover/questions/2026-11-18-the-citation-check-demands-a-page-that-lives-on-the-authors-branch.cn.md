# The citation check demands a design page that by design lives only on the author's branch

**Question for the language author.** Sixteen citations in `develop` point at
`docs/design/graph-editing/README.cn.md`. That file exists on `origin/fsh-dev` and nowhere else, because
design pages are written and delivered by the author, and the flow between the branches is one-way.

## What was measured

```
docs.links.citations: 16 citations name docs/design/graph-editing/README.cn.md
   and the working copy has no such directory
```

The file is not missing by accident. `git show origin/fsh-dev:docs/design/graph-editing/README.cn.md`
returns it; `develop` has never had it. Lanes are told to read it that way precisely because they must
not check out or merge that branch.

So the check is asking `develop` to contain a document that the branch layout says it must not contain.

## Why it was not simply fixed

Both obvious repairs are wrong. Adding the page to `develop` puts the author's design page on the
integration branch, which inverts the delivery direction and means the next author edit has to be
mirrored by hand. Deleting the sixteen citations removes the evidence that the design page is where
these rules come from, which is the only thing that makes the citations worth having.

## Default, and work started on it

**Default: the citation check should resolve a citation against the branch the page lives on, not
against the working copy alone -- that is, a citation naming a `docs/design/` page is satisfied by that
page existing on `origin/fsh-dev`.** Reasons: (1) it keeps the one-way flow, which is a decision already
made and not in question; (2) it keeps the citations, which is what makes a rule traceable to its
source; (3) it makes the check's question match reality -- the document does exist, just not here.

This round does not wait: nothing is changed, and `docs.links.citations` stays red as a known, named
condition rather than being silenced. If the answer is instead "design pages must land on `develop`
too", that is a larger change of layout and a different question, and the sixteen citations give the
exact list of what would have to move.