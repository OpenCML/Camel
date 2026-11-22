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
## 决策者答（2026-11-23 01:35）

**按路线 1：改内核，让 `__fill_value__` 从普通参数读字段。** 这不需要语言层面的新裁定，`with` 参数的判断依据已经定了：`with` 是配置函数的东西（相当于函子的 `this`，决定"这是哪一个函数"），每次调用要处理的数据是普通参数。`fill` 的模板与各字段值是这一次构造的输入，属于普通参数，声明是对的，内核读错了位置。

顺带：页里说"其余构造结构体的算子已经从 `with` 取字段"——按同一条判据，那几个也读错了位置。请列出它们，以 `C-` 编号进 backlog，与 `fill` 同批改到普通参数（声明若也写成了 `with`，声明一并改；这是按既定判据纠错，不算改已合入的接口）。前端车道解除阻塞。

（2026-11-23 复核：已答，归档。）

## 决策者更正（2026-11-23 05:55）：上面第二段推广错了，撤回

上面答"`fill` 改内核、读普通参数"成立：`fill` 的声明本来就把字段写成普通参数，是内核读错了位置。

但第二段"其余构造算子同样读错了位置，声明若也写成 `with` 就一并改"是我推广错了，撤回。判断一个参数该不该放 `with`，负责人的判据是（2026-10-25）：设想函子，它是构造时绑定一次、反复作用于数据的 `this`，还是每次调用的参数；**而且 `->` 只传一个值，其余参数放尖括号便于串成流水线**，所以 `'{}'->format<x>`、`xs->slice<0, a, b, s>`、`xs->append<5>`、`xs->contains<x>`、`reduce<f, init>` 这类声明本来就是对的。只有"声明与内核不一致"的地方才是缺陷，按声明修内核；声明与内核一致的，一律不动。
