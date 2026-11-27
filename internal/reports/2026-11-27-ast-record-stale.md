# The sixteen stale AST rows: expectation out of date, re-recorded

`parse.frontend | frontend.ast_record` was the promotion's one real new red (run `45983763798` at
`8c8ae279af`, linux leg): `4647 file(s) read, 16 stale (16 mismatch, 0 missing, 0 gone)`.

**Verdict: the expectation was out of date, and it has been re-recorded. The cause: a block root
went from one node to a layer of nodes (`BlockRootLoad`).** Not "the AST check was fixed", and not a
regression rolled back.

## What was measured, and in which order

```
c2296925b2   Merge lane/abort-fix: a compiler-made operator reference is not a name the static scan checks
             BUILD_EXIT=0 | ast hashes: 4647 file(s) read, 16 stale (16 mismatch, 0 missing, 0 gone)
             [SUMMARY] total 10, pass 8, fail 2 | RUN_EXIT=1
a53a1308be   its first parent (Merge lane/arm-fill)
             BUILD_EXIT=0 | [SUMMARY] total 10, pass 10, fail 0 | RUN_EXIT=0   (0 stale)
```

So the sixteen rows come in with that merge. The merge's parse side is where the AST changed, not
the kernel's printing:

```
src/parse/include/camel/parse/ast/data.h   +25  DataType::BlockRoot and class BlockRootLoad
src/parse/src/ast/builder_expr.cpp         +7/-1  visitBlockHeaded:
      before:  *initial << overNode;
      after:   rootLoad = createNodeAs<BlockRootLoad>(type); *rootLoad << overNode; *initial << rootLoad;
src/parse/src/ast/data.cpp                 +12   toString() prints EditRoot / ViewRoot
```

The meaning is the one the merge states: in `edit d = f { … }` the name `d` stands for the graph the
compiler opens on `f`, not for `f` itself. So the binding's initial value is a root that holds the
function reference, rather than the reference alone.

## Why this counts as an expectation following the tree, not the tool

Three questions were answered from the source, before any row was touched:

```
· Is a reference lost?      No — `*rootLoad << overNode` keeps it as the root's child.
· Is one bound wrongly?     No — BlockRootLoad's blockKind is the block's own kind (EditExpr/ViewExpr),
                            fixed at parse time, not read from the program.
· Did anything else move?   No — the sixteen rows are all probes that write `edit d = f { … }` or
                            `view d = f { … }`; no case outside them is stale.
```

An extra layer that the change meant to add is a different thing from an extra layer that appeared by
accident; that difference is what the three questions separate.

## The re-record

Done through the tool's own path, never by hand: `camel-remote -UpdateAstHashes -AstOnly …`, which
hands the work to `check-ast-hashes.mjs --update` and so to `check.py snapshot --update --only`.

```
ast hashes: 4647 file(s) read, 0 stale (0 mismatch, 0 missing, 0 gone)
[PASS] frontend.ast_record   [PASS] frontend.ast_snapshot   [PASS] frontend.ast_snapshot_drop_guard
[SUMMARY] total 10, pass 10, fail 0 | RUN_EXIT=0
test/plans/feat/parse/golden/ast_hashes.txt | 32 +++++-----
1 file changed, 16 insertions(+), 16 deletions(-)      numstat: 16  16
```

Three of the sixteen, old hash to new: `edit_value.cml` `f798a0c45f6fd7eb → d044c5ff8f14b0f8`,
`view_value.cml` `45c52dd25a1dd471 → c7425d39c3e1e8fb`, `design_view_min.cml`
`d243b9bf2aafcaf4 → d394efc4696a60d5`. In each, the recorded text moved by one node: the binding's
initial value is now `BlockRootLoad(EditRoot | ViewRoot)` over the function reference.

The branch is `lane/ast-golden`, tip `11617a060b`, tree `D:\Projects\Camel-wt\misc`.

## One measurement worth keeping

The dispatch described the sixteen as all under `probe/editview*`. Fifteen were. The sixteenth is
`test/cases/probe/blockscope/imported_type_in_body.cml`: same merge, same shape, different directory.
It was found because the first re-record left `1 stale` — the count said so, the description had not.
