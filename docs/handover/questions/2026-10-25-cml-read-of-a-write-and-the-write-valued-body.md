# A read of a write's product, and the value of a body that ends in a write

Status: open, for the language author. Raised by the lane that lands H242a's last red
(`print_and_recompile`, handover/h485-printer-var-read: h477 + h474 + `origin/develop`).

The lane that landed the var-version ruling asked the first half of this already
(`2026-10-25-cml-printer-var-version-read.md`, on handover/h464-escape-cell). This file records what
the fix measured, and the second half the first half exposed.

## What was measured

The tree of h477 + h474 + `origin/develop` is red on exactly one test:
`[SUMMARY] total: 6175, pass: 6155, fail: 1, skip: 11, known failures: 8` -- `print_and_recompile`.

**First half, fixed here.** `cml1` is not the builder's name: `bindTemporary` has written
`$tagged<N>` since h464 (`src/compile/gir/builder_expr.cpp`, `bindTemporary`), and the name in the
printed document is the printer's own `freshName()` (`src/passes/trans/cml/cml.cpp`, "cml{}"). The
write's product is a cell (`:op/assn*` returns this version's cell, the ruling), so a read after a
write takes that write as its data port, and the printer had no name for it. The cell a write stores
into is the variable it wrote, so the read is written with the variable's name
(`cellReadBy`, commit 8f6aeac88). Before, on `test/cases/trans/cml/linear.cml`:

```
   9	    var bonus: int = 1
  10	    bonus = bonus + score
  11	    println('bonus =', cml1)
  12	    return string(cml1) + ' points'
  13	}
```

`linear.printed.cml:11:24: [Error]: Unresolved reference: 'cml1'` (and `12:19`). After: lines 11-12
read `bonus`, and `cml` on the printed document exits 0. `closure.cml` (`return cml1` inside the
closure over a cell) is repaired by the same change. The corpus then passes `linear`, `branch`,
`loop`, `interval`, `closure`, `with_param` and stops on the seventh entry, `var_param.cml`.

**Second half, open.** `var_param.cml` is `func bump(var x: int) { x += 1 }`. The printer refuses it:

```
std::cml cannot print bump: the return type is a tagged, nullable or cell tuple.
```

The graph says why, and it is not the printer's doing. `func bump(var x: int) { x += 1 }` is a body
whose value is a write, and h464 changed the assignment's result type for every assignment
(`src/builtin/modules/globals.cpp`): `:op/assn_l` is `TupleType::cell(Type::Int64())` there, where
`origin/develop` has `Type::Int64()`. The graph exit is that cell, so the function's own type became
a cell:

```
h477+h474+develop:  N02 [:op/assn_add_l] value_type="var int" -> O02 EXIT   (bump: (var int) => var int)
origin/develop:     N02 [:op/assn_add_l] value_type="int"     -> O02 EXIT   (bump: (var int) => int)
```

The same signature reaches the diagnostic: `IgnoredSideEffect` says
`The call of '<__assn_add__>: (var int, int) => var int'` here, `=> int` on `origin/develop`. The
ruling itself separates the two (`questions-2026-10-24-evening.md`, var-version-node, item 3): "an
assignment used as a value still gives the value written, as today; it and the version are two
outputs". So the value output is missing, and a body whose value is a write is where it shows.

**What the repair still needs.** A probe that lets a cell-typed body value read its version at
`Builder::setBodyResult` restores the type (`bump: (var int) => int`, and `main`'s read of `n` goes
back to the cell with the call as its control, develop's shape) -- but the printed document is still
not equivalent, and the reason is older than this batch. A function's body is printed with an
explicit `return` (`src/passes/trans/cml/cml.cpp`, `printStatements`), and an explicit `return`
builds a GATE when the value depends on a write (`visitExitNode`), which an implicit body value does
not:

```
DIFFERENT: the two canonical documents are not equivalent
a structural class has 1 vertex/vertices on the left and 0 on the right
  left:  N:g2:n2: {"kind": "OPER", "payload": {"op": ":op/assn_add_l@1.0"}, ...}
```

The same `DIFFERENT` comes out of `origin/develop`'s own build for the same shape written without a
cell -- `func f(): int sync { var v: int = 1; v = 2; v }` prints as `v = 2` / `return v` and does not
round trip there either. The corpus has no such case, so the gap stayed invisible until a write's
product became a cell and `var_param.cml` fell into it.

## The questions

1. Is a body whose value is a write telling the truth with `bump: (var int) => var int`, or should a
   value use of a write give the value written (the read the ruling's "two outputs" describes), as
   `origin/develop` does?
2. How should the printer write a body whose implicit tail value depends on a write: the explicit
   `return v` it writes today (which recompiles to a GATE the original does not have), the bare tail
   expression it already writes for a lambda's block body -- or should the builder give an implicit
   body value the same GATE an explicit `return` gets?

## What this lane did not touch

`src/compile/gir/**` (the probe above is in `D:\Projects\Camel-wt\notes\h485-builder-probe.patch`,
uncommitted), the goldens h464 updated (`:op/assn_l` as `var int`), and every expectation. The only
commit on the branch is the printer's `cellReadBy`.