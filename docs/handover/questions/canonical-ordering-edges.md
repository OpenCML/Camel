# The canonical document cannot export the ordering edges yet

## What the review asks for

Item 13 of the author's list: both JSON versions export `sync` and `order` edges, the canonical
version counts them as part of equivalence, the control edges come out of both once their
replacement lands, and the document is updated. Version 1 already writes them
(`src/passes/trans/json/gir.cpp`, both without `sourceIndex`/`targetIndex`: they are not ports).
Version 2 (`std::jgir canonical=true`, `src/passes/trans/json/gir_canonical.cpp`) still writes
only `normal`, `context` and `control`.

## What happens when version 2 writes them

Adding `sync` and `order` as `{kind, slot: null, source}` inputs and nothing else is a dozen
lines, and every canonical pair in `test/cases/trans/cml_canon_contract.py` still passes. The
round trip in `test/cases/trans/cml_contract.py` does not: `var_param` becomes `DIFFERENT`.

    program            std::cml print, recompiled
    ----------------------------------------------------
    bump(var x: int) { x += 1 }                 original
    bump(var x: int) { let c = x + 1            printed
                       x = c
                       return c }

Both bodies mean the same thing, and both build five nodes of the same kinds with the same data
edges. The printed body's graph carries two edges the original's does not:

- `order` from the read (`:var/peek` of `x`) to the write (`:op/assn_l`), which
  `Builder::recordWrite` adds because the read is a pending use at the write and the write has no
  *direct* data edge from it (`builder_expr.cpp`, `hasDirectDataDependency`). The assignment form
  has the same data path peek -> add -> write, so the edge is redundant there; the `x += 1` form
  registers no pending read use at all, so it never asks.
- `sync` from the write to the function's exit `GATE`, beside the `control` edge both graphs
  have: the printed body's write is a statement of a sync block and the exit waits on the block's
  tail (`builder.cpp`, the block-value gate; `builder_control.cpp` for the branch and loop
  chains), while `x += 1` is the body's value.

Neither edge changes which effects run or what they print: both order the same nodes and both
pairs run alike (`cml_contract.py` already asserts equal stdout). They make the two graphs
different documents.

## Why that is not a printer bug

Compiling the three spellings of the same body gives three graphs:

| body | gates | order edge read -> write | sync edge write -> gate |
| --- | --- | --- | --- |
| `x += 1` | 1 | no | no |
| `x = x + 1` (body value) | 2 | yes | no |
| `let c = x + 1; x = c; return c` | 1 | yes | yes |

So no source text the printer can write reproduces the `x += 1` graph, and the printer's own
output is not what differs from the original -- the builder does, between `x op= e` and
`x = x op e`. Principles 3.5 and F3 do not say a read the write already depends on needs an order
edge, and 5.3 says a sync block's statements are one chain, so both graphs are defensible; what
is not defensible is that the two spellings of one assignment build different ones (the
principle that `x op= e` *is* `x = x op e`, design principles line 190).

## Why it is not being finished here

The two ways out both live in the edge refactor, which the review assigns to the same lane as
this item and which holds `builder_control.cpp`:

- make `x op= e` build what `x = x op e` builds (order edge and the sync chain included), so the
  printer's split statement form and the single expression agree; or
- stop writing the ordering edge that is already implied by the data path, in `recordWrite` and
  in the block-value gate, so redundant ordering never reaches a document.

Either one changes graphs across the corpus, which is exactly what the edge refactor is doing.
Exporting `sync` and `order` in version 2 before that lands turns `cml_contract` red -- and the
invariant it protects, that the printed source compiles to the original graph, is the one thing
the printer is measured by. So version 2 keeps three kinds until the refactor lands, and
`docs/technical/gir-interchange.md` should only be updated together with it (version 1's
`sync`/`order` edges are missing from it today).

Acceptance when it is done: `cml_contract` and `cml_canon_contract` both pass with the ordering
edges in the document, and `var_param` still round trips.