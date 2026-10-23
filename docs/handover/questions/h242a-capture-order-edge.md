# A capture-ordering edge the design calls an order edge is a use edge today

Author: the H242a agent (edge representation), 2026-10-23. Status: open, needs the language author.

## Problem

`Builder::orderAfterLastWriter` (src/compile/gir/builder_expr.cpp) is called once per captured use
of a callee, with no distinction between a use that writes the variable and one that reads it:

```cpp
for (const CapturedUse &use : captured) {
    orderAfterLastWriter(use.variable, targetNode);
    ...
}
```

It links the variable's last writer to the call with a **control** input. Two of the design's
sentences meet on that one link:

- writes of one `var` only order between themselves (design principles 3.5, F3), so it ought to be
  an **order** edge (docs/design/effects/edges.cn.md 1.5: order edges come from effects on one
  resource and from writes of one `var`);
- a read uses the write nearest before it and that write therefore executes (F3), which is what the
  control input does: it is the only edge that keeps the writer call on the result path when the
  later call merely *reads* the captured variable.

H242a changed the link to an order edge and measured what that costs. On
`test/cases/linear/byref/captured_write_order.cml`:

```
expected 10 | 10 | 100 | 10 | 111 | 1011 | 12 | 611 | 40 | 2508 | 21
got       0 | 20 | 320 | 40 | 111 | 1011 | 12 | 611 | 40 | 2508 | 21
```

`call_then_call` returns 0 instead of 10: `bump()` (which writes the captured `cnt`) is ordered
after `peek()` (which only reads it) and nothing else keeps it alive, so `peek()` sees the value
from before the write. `test/plans/feat/linear/captured_writes.plan.toml` asserts that structure
directly (`write_a -> read_a` as a CTRL edge, `use_b -> write_b` as a CTRL edge) and 20 cases of the
whole fast layer go red. The link was left as a control input.

## What telling the two apart needs

- For a **written** capture, the edge may be an order edge: a later read of the variable creates its
  own use edge from the call (the new modifier), so the write still executes.
- For a **read** capture, the design wants a *data* edge: the call uses the value the write made.
  There is no port for it today. A call to a function that reads a captured `var` carries the
  variable as a captured resource, not as a port, so the value it sees is whatever the shared object
  holds at that moment, and only an execution-carrying edge can make that the written value.

## Options

1. **Give the read capture a real data input** (the variable's current value at the call site, a
   `:var/peek`-style port on the call), then the written-capture link can become an order edge and
   the read is a data edge like any other. Cost: a port on every call that reads a captured
   variable, in the call's signature and in the inliner; it is the direction the design points.
   Recommended.
2. **Split the helper by `use.written`** and leave the read case as a control input for now. Cost:
   small, but the graph then says "control input" where the design says "data", which is the same
   debt as today, only smaller — and it closes nothing, because the read case is the one that
   matters.
3. **Leave the link as it is** and let the stage that deletes `Ctrl` decide. Cost: the design's
   "writes of one `var` are ordered by an order edge" is not observable in a compiled program.

## Recommendation

Option 1, in the stage that deletes `Ctrl`; H242a's representation and schedulers are ready for it
(an order edge is laid out by every back end and never makes a node execute, which is guarded by
`order_edge_orders_the_nodes_that_run` in test/plans/feat/opt/edges.plan.toml).