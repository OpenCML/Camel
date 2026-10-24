# The value of a body that ends in a write, and how such a body is written

Status: the printer half is landed here (commit 8f6aeac88). The value output the ruling names is
decided in principle (`docs/handover/reviews/questions-2026-10-24-evening.md`, var-version-node
:151/:152: a value use of an assignment gives the value written, the version is the other output), but
its *site* is open: the one attempt measured here (the body result) segfaults and turns 43 green tests
red, so it was reverted (commit a0001f185). The second half, how such a body is written, is open for
the language author. Raised by the lane that lands H242a's last red (`print_and_recompile`,
handover/h485-printer-var-read: h477 + h474 + `origin/develop`).

## What was measured

The tree of h477 + h474 + `origin/develop` was red on exactly one test:
`[SUMMARY] total: 6175, pass: 6155, fail: 1, skip: 11, known failures: 8` -- `print_and_recompile`.

`cml1` is not a builder name: `bindTemporary` has written `$tagged<N>` since h464
(`src/compile/gir/builder_expr.cpp`), and the name in the printed document is the printer's own
`freshName()` (`src/passes/trans/cml/cml.cpp`, "cml{}"). The write's product is a cell (`:op/assn*`
returns this version's cell, the ruling), so a read after a write takes that write as its data port,
and the printer had no name for it. The cell a write stores into is the variable it wrote, so the
read is written with the variable's name (`cellReadBy`, commit 8f6aeac88). Before, on
`test/cases/trans/cml/linear.cml`:

```
   9	    var bonus: int = 1
  10	    bonus = bonus + score
  11	    println('bonus =', cml1)
  12	    return string(cml1) + ' points'
  13	}
```

`linear.printed.cml:11:24: [Error]: Unresolved reference: 'cml1'` (and `12:19`). After: lines 11-12
read `bonus`, and `cml` on the printed document exits 0. `closure.cml` (`return cml1` inside the
closure over a cell) is repaired by the same change.

The second half showed once the printer could write the document at all. h464 changed the result type
of every assignment (`src/builtin/modules/globals.cpp`: `:op/assn_l` is
`TupleType::cell(Type::Int64())`, where `origin/develop` has `Type::Int64()`), so the value output
was gone and `func bump(var x: int) { x += 1 }`, whose value is a write, became a cell-returning
function -- which `std::cml` refuses by design:

```
std::cml cannot print bump: the return type is a tagged, nullable or cell tuple.
```

Restoring the value output at the one place where a body's value is taken
(`Builder::setBodyResult`, `src/compile/gir/builder_control.cpp`: a cell-typed body value is read
through the variable it is a version of, and the graph's exit is that read) gives back develop's
reading -- `N03 [:var/get] -> O02 EXIT`, `bump: (var int) => int`, and `main`'s read of `n` taking the
cell with the call as its control, develop's shape (commit 7cb81efd2). The corpus then reads, case by
case:

```
branch: EQUAL      closure: EQUAL     interval: EQUAL    linear: EQUAL
loop: EQUAL        with_param: EQUAL  var_param: DIFFERENT
```

and the printer's refusals (`refuse/named_type`, `refuse/tagged_node`, `refuse/operator`,
`refuse/partial_value`, `refuse/inline_region`, the big-constant one) all still refuse.

**But that site is not a small change (reverted, commit a0001f185).** The same tree is red on 43
tests that are green without it -- `[SUMMARY] total: 6175, pass: 6112, fail: 44` against the
`pass: 6155, fail: 1` above. Two of the reasons are defects of the change itself, and the shape of
the rest says the value output has a home of its own:

- `test/plans/feat/mm/blocks.plan.toml` (4 tests): `camel: internal error: Runtime graph
  '__g_22__' breaks its edge invariants: graph '__g_22__' node 2 (11): lies on a cycle of inputs:
  2(11) <- 3(11) <- 5(11) <- 4(11) <- 2`.
- `test/plans/feat/linear/var_reference.plan.toml` (`var_capture.*`, 30 tests) and
  `test/cases/linear/byref/var_capture_reference.cml` -- a case `origin/develop` has -- segfault
  (`rc=139`):

```
#0  GraphDraft::appendInputEntry  #1  GraphDraft::appendInput  #2  gir::linkNodes
#3  gir::Builder::visitLinkNode  #4  gir::Builder::dispatch  #5  gir::Builder::visitExecNode
```

  The read this change adds at a body's value is erased by the SSA conversion
  (`collapseNonEscapingVariableReads`), which leaves the graph's exit, output and return pointing at
  a node that is gone, and a later GCT `link` still names it. Making those anchors follow the value
  (`if (draft.exitNode() == readId) { draft.setExitNode(version); }` and the same for `outputNode`
  and `returnNode`) removes the `blocks` cycle but not the segfault, so the conversion itself would
  have to know which reads are anchors -- a change in the var-version machinery, not at a use site.

- `declared_resources` (`test/plans/feat/modules/resources.plan.toml`), `poison:modules.resources:core`,
  and `capacity.perm_query_types` (`test/plans/feat/mm/capacity.plan.toml`) fail their recorded
  footprints (`Error: :var/new has no semantics`): the footprints h464's landing pinned are the
  ones this change moves.
- `docs.snippets_compile` fails `docs/tutorial/tutorial.en.md:517` and its `.cn.md` twin
  (`does not compile: no diagnostic`), and `core` / `core_test_pass_is_not_in_a_pipeline` fail -- both
  reader-facing expectations.

## The open question: how a body's value is written

Without the value output, `var_param.cml` stops at the printer's refusal above; with it, it stops at
the shape below. `var_param.cml` then prints as

```
func bump(var x: int): int sync {
    x += 1
    return x
}
```

and recompiling that document is not equivalent to the original:

```
DIFFERENT: the two canonical documents are not equivalent
a structural class has 1 vertex/vertices on the left and 0 on the right
  left:  N:g2:n2: {"id": "n2", "inputs": [{"kind": "normal", "slot": 0, "source": "n0"}, ...], "kind": "OPER", "payload": {"op": ":op/assn_add_l@1.0"}, "type": 3}
```

The original has four nodes with the read at the exit; the recompiled one has five, the fifth a GATE
over the read. The cause is older than this batch: a function's body is printed with an explicit
`return` (`src/passes/trans/cml/cml.cpp`, `printStatements`), and an explicit `return` builds a GATE
when the value depends on a write (`src/compile/gir/builder_control.cpp`, `visitExitNode`), while an
implicit body value does not. `origin/develop`'s own build has the same asymmetry for the same shape
written without a cell:

```
func f(): int sync { var v: int = 1; v = 2; v }   prints as   v = 2 / return v
```

and its canonical documents differ in the same way (`DIFFERENT`). The corpus has no such case, so the
gap stayed invisible until a write's product became a cell and `var_param.cml` fell into it.

Two ways to close it, both a decision about form:

1. The builder gives an implicit body value the same GATE an explicit `return` gets. Cost: the graph
   of every function whose implicit tail value depends on a write changes shape, well beyond the one
   red this lane was sent for.
2. The printer writes a function body's tail value as a bare expression (the form it already writes
   for a lambda's block body, `blockText`) when the graph has no return gate. Cost: the printed text
   of every function body whose value is not an explicit `return` changes from `return v` to `v`,
   which is the language's print form, not this lane's to choose.

## The questions

1. Where is a write's value output restored? The body result is measured here and is not the site
   (segfault, 43 reds, footprints and reader-facing expectations). The alternatives the measurements
   point at: h464's own landing of the assignment's result type (the goldens pin `var int`, so this
   lane would not touch them), the SSA conversion learning which reads are graph anchors and which
   belong to another graph, or every value use of a write reading it -- the general form of the
   ruling, which is the largest of the three.
2. How is a body whose value depends on a write written: `return v`, the bare tail expression the
   printer already writes for a lambda's block body, or a GATE on the implicit body value to match an
   explicit `return`?

## What this lane did not touch

`src/passes/trans/cml/cml.cpp`'s spelling of a body's tail (question 2, untouched on purpose), the
goldens h464 updated (`:op/assn_l` as `var int`), every expectation, and `known_fail`. The branch
carries the printer's `cellReadBy` (8f6aeac88), this file, the body-value read (7cb81efd2) and its
revert (a0001f185), so the next attempt can start from the measurement instead of repeating it.