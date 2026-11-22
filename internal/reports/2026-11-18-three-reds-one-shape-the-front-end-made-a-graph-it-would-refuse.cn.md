# Three reds, one shape: the front end made a graph its own criteria would refuse

The last twenty-four reds in the optimizer family are three findings that arrived separately and
have one shape. Each was found by asking a refusal where it comes from, and each answer was above
the pass that was being blamed.

## One: a transform that only passes a function through declares nothing

` utodiff:with_grad ` takes a function value and returns a function value. Its kernel returns
the result of building a gradient function, so it runs no body and writes nothing. It was declared
not pure with no resource set, which the analysis reads as touching every resource, which met a read
of the cell it passes. Six reds. The compiler's own rejection of the honest alternative names the
rule: an operator that is not pure touches, allocates or affects something, declare it pure instead.

## Two: a zero for a width the type did not state

An array whose length the type does not state cannot be given a zero that satisfies the adjoint
identity -- one empty array is wrong for length two and for length three. The forward half now
carries that input's shape as a residual and the transpose builds the zero from it, so the answer
has the width the value had. The empty-array branch is deleted rather than narrowed, because a
silent empty array is the one answer nothing can tell apart from a correct one.

## Three: an effect in a body never had an edge to its owner

` orderEffectsOnOneResourceIn ` keeps the last effect of each block and only for nodes that are
themselves effects. An effect inside an arm or a loop body belongs to a different block, and the
owner that stands for the region is not an effect node, so it is skipped. The edge from that effect
to the branch's owner is therefore never written -- while the check that refuses pairs exactly those
two, because the owner stands in its region for what is under it.

## The shape

In all three the graph was built by the front end, and the pass that was blamed only copied it. The
readings are all the same kind: run the same check over the graph **before any pass ran** and it
refuses the same pair. A rewrite could have added the missing edge in each case and would have hidden
a producer defect at every call site instead of one place.

**A refusal that a pass can reach is evidence about the graph that pass received, not about the pass.**

## What was refuted along the way

A hypothesis that the guard switches ` protect_order ` and ` protect_faults ` were being turned off
by callers was checked and is false: they have no readers in the passes at all. What refuses is the
rule's own condition, and it is right to. The lesson is the same in the other direction: a thing that
is named like a switch is not evidence that anyone can switch it.
