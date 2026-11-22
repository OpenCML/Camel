# `fill` declares its fields as ordinary parameters and reads them from `with`, so neither side can call the other

**Question for the language author.** The declaration and the kernel each make sense alone, and
together they cannot meet. Nineteen construction sites are held behind this, and the fix is a
decision about the language, not about the compiler.

## What was measured

The frontend lane is moving construction from `Fill` nodes to `OPER` nodes, and `fill` is the one
operator it could not move. The two readings, both from the tree at `1bf533760`:

```
declaration  fill::<T, ...F>(template: T, ...fields: ...F)     the fields are ordinary parameters
kernel       src/stdlib/std/builtin/operators/struct.cpp       __fill_value__ reads them from the `with` view
```

So a call built the way the declaration says cannot be filled: the `with` view is empty
(`with.size()==0`), and the answer that comes back is the template itself. And a call built the way
the kernel reads cannot resolve the overload, because the fields are not where the declaration
puts them.

## Why this is a question and not a defect

Either side could be made to follow the other, and both are defensible:

1. **Align the kernel to the declaration** — `__fill_value__` reads the fields from its ordinary
   parameters. The declaration is what the library publishes and what a reader of `stdlib/std.cml`
   sees; the kernel is an implementation of it. Cost: one function in `struct.cpp`, and the
   `with` form stops working for any call that already uses it.
2. **Align the declaration to the kernel** — `fill::<T, ...F>(template: T, with ...fields: ...F)`.
   Filling by name is naturally a `with` operation, and the other struct operators already take
   their fields that way. Cost: it changes a published signature, which the interface rules forbid
   after the interface has been merged.
3. **Author rules the signature itself** — neither side moves until the author names the shape.

## The recommendation I will act on if no ruling arrives

**Take route 1.** The published declaration is the contract every other operator is read against,
and an implementation that disagrees with its own declaration is the side that is wrong. It also
keeps the interface stable, which is the rule that cannot be undone later.

**Blocked meanwhile:** the nineteen `fill` construction sites stay as they are, and the frontend
lane stays `blocked` rather than `doing` — nobody is working on it, and the table must not say
someone is.

## What this is not

It is not a reason to drop `Fill` from `GCNodeKind`, and not a reason to change any pass's
semantics. The `Cast` and `Accs` halves of the same move are already measured: `type="CAST"` and
`type="ACCS"` are both absent from the printed graph, and `type="FILL"` remains only because these
nineteen sites are still waiting here.