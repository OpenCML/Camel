# The initializer of a `var` is not subject to F3

Author: the H242a agent (edge representation), 2026-10-23. Status: open, needs the language author.

## Problem

Design principles 3.5 (formal F3) says:

> 读一个 `var` 用到它前面最近的一次写（形式化 F3），那次写因此执行；写与写之间只排序：
> `x = 1; x = 2; return x` 里第一次写没有被用到，不执行。

The rule holds for a write made by an assignment. It does **not** hold for the initializer of a
`var`, because the initializer's value is not a write of its own: it is the data input of the cell
the front end creates (`:var/new`). Nothing in the graph can tell "the value the cell was made
with" from "the value the cell holds", so the read of a `var` reaches the initializer's effect
through the cell and the effect runs.

Two repros, both `effects_outside_sync.cml`-style, both run as noted on every back end:

```camel
// F3 says: only `x = 2` is read, so nothing is printed and the result is 2.
// Today: prints 1 (the initializer's effect runs), then 2.
func initializer_read(): int {
    var x = println(1)
    x = 2
    return x
}

// F3 says: the read uses the write nearest before it (`x = println(2)`), so only 2 is printed.
// Today: prints 1, then 2.
func initializer_overwritten(): int {
    var x = println(1)
    x = println(2)
    return x
}
```

The graph of `initializer_overwritten` is:

```
N02 :io/println(1) --NORM--> N03 :var/new  (the cell; type "var int")
N04 2 --NORM--> N05 :io/println(2)
N03 --NORM(0)--> N06 :op/assn_l        N05 --NORM(1)--> N06
N03 --NORM(1)--> N07 :var/get          N06 --CTRL--> N07
N07 --> EXIT
```

`N07` (the read) is on the result path; it reads the cell `N03` and is ordered after `N06` by a
control input. `N03` is live because `N06` (the write) needs the cell as its place, and `N03`'s own
data input is the initializer's effect `N02`. So the initializer runs although no read uses the
value it wrote.

## Why this blocks the H242a acceptance item

H242a asks, among other things, that no `var` write be executed unless a read uses it. Half of that
is done (an assignment that no read uses is dropped, and the write-after-write ordering is an order
edge now, not a control input). The initializer half cannot be reached from the edge model: it is
about what a cell *is*, not about which kind of edge joins two nodes.

## Options

1. **Make a `var` "create a cell + one ordinary write" (what F3 states).** `:var/new` takes no
   value (`() -> var T`) and the initializer becomes an `:op/assn_l` write to the fresh cell, so
   the read uses the nearest write and the initializer's effect is dropped like any other unread
   write. Cost: the element type of a bare `:var/new` must come from somewhere (the place's type,
   or the first write); every pass that reads `VarNew`'s input must be revisited; the stdlib
   declaration of `:var/new` changes. This is the design's own text, so it needs no decision in
   principle — only a plan and a work item. Recommended.
2. **Keep the cell carrying the initializer, and drop the initializer's effect when a later write
   of the same variable exists.** Cost: a special rule for the initializer, exactly the kind of
   "temporary rule" design principles 3.5 (known gaps) forbids; the graph would lie about what the
   cell holds.
3. **Leave it, and say so in 3.5.** Cost: F3 stays a rule with an exception the text does not
   state, and the H242a acceptance item can only be met in part. Not recommended.

## Recommendation

Option 1, as a work item of its own: it is a change to the `var` representation and cannot ride on
the edge refactor. Until then, H242a asserts the half that holds and names this file; it does not
weaken the assertion into something that passes.