# Specialization publishes its ports one place over from its own signature

**Question for the language author.** Found while placing a port-type check; not the thing that lane
was sent to do, and it is the reason that check cannot be asked of every graph.

## What was measured

Putting the port-type check at seal -- where every commit asks every graph -- turns **67 cases red
across two families, 137 places**. Every one of them is a graph the compiler built for itself:
`*$spec*`, `main$lift0`, `split_of_jvp_of_*_backward`.

The one that names the shape:

```
f1$spec0 declares parameter 0 as 'bool'
   and its port node 1 carries the type of parameter 1
```

So the normalized port list of a specialized graph is **shifted one place** against the signature
that same graph declares. It is not that a type is wrong; it is that the list and the signature
disagree about which port is which.

That is a builder/specialize defect. It was not visible before because nothing compared the two.

## Why it blocked the check, and what the check does instead

Asking unconditionally needs this fixed first: otherwise the check refuses the compiler's own
outputs, and refusing a correct graph is worse than not asking. So the check is put only to graphs
**this commit gave a new signature** -- ones whose previous signature exists and differs. A graph
built fresh has no old signature to disagree with.

## Default, and work started on it

**Default: treat this as a defect in specialization and fix the shift, then ask the port check
unconditionally.** The narrow placement is a holding position, not the intended end state -- it
leaves fresh graphs unasked, and a graph that is wrong from the moment it is made is exactly the
kind this week's other four findings were.

This round's work does not wait on the answer: the narrow placement is landed and green, so the
ruling only decides whether the next lane fixes the shift and widens the check. If the answer is
that the shift is intended -- the port list and the signature legitimately index differently -- then
the check should compare against the port list's own order rather than against the signature, and
this page should be read as recording that the two orders differ by design. That reading is not
adopted here because the two are described as the same thing everywhere else in the code.