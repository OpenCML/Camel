# A condition written on one side only

## What was found

A read that stands in one region was handed a version node that belongs to another. The rule that
would have refused that pairing exists and is correct -- and it is written only where a value is
**written**, not where it is **read**.

` Builder::recordWrite ` asks whether the read's region is the exit region and, when it is not,
leaves the read pending so the region's own exit orders it: ` eadRegion != exitRegion `. The
twin of that condition on the reading side does not exist. ` Builder::readVariableThrough ` takes
the join node a branch recorded for a variable and hands it to every later read of that variable
without asking where the read stands.

So a branch inside a loop body -- which is an arm of a branch in the encoded graph -- records a join,
and a read after the loop receives it. The read is standing outside the arm and is handed a node
inside it.

## Why this is worse than a rule that is missing entirely

A rule that is absent is visible as absent. This one is present, is right, and covers half the cases
it names, so it reads as done. The sweep over the corpus found exactly one case that reaches the
other half, and that case had been marked a known failure with its error codes written into the mark
-- ` JOIN 12 `, ` BRCH 24 `, ` OPER 26 ` -- so the defect had been observed, recorded,
accepted, and never asked again.

The fix is six lines in the reading side of the same function pair, and it turns that case from an
internal error into the value its own record already said it should be.

## The general shape, with the other one from the same night

The same night found the opposite mistake: a condition written only on the ordering side (a read in
an outer region that must stay pending) with no twin on the data side, so an effect inside a body
never got an edge to the owner standing for its region.

One wrote the condition on one side of a pair and not the other; the other wrote it on one side of a
concept and not the other. **Both read as complete from the side that has it.**

## What this changes about how to look

When a condition holds "in one direction", the question to ask is not whether it is true but whether
its mirror exists -- and the cheap way to find out is to build the smallest program that would need
the mirror and see whether the refusal happens or the wrong answer does.
