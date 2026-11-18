# The exemption list accepts a reason that is no longer why, and hides the names it says are clean

**Question for the language author.** This is not about a failing test. It is about the gate reporting
two things that are both false, in opposite directions, from the same truncated list.

## What was measured

```
gpu-plans.exceptions.txt holds 15 entries
   13 of them are red today for a reason that is no longer the recorded one
      (the record says "outlined count 5 vs 10"; the actual reason is an effect-containment refusal)
   ...and the gate accepts each of them anyway, because the record still exists

the `check=` line truncates its red list at 20 names and appends red_more=12
   ...so it reports "7 exemptions are no longer red and can be deleted"
   ...while those 7 names are inside the red list it just truncated
```

So one mechanism suppresses thirteen real failures and simultaneously invites the deletion of seven
entries that are actually failing. A person reading the line gets a clean bill and a to-do list, and both
are wrong.

## Why it matters more than the count

An exemption is a claim: "this fails, and here is why". The gate checks the first half — the name is
still failing — and never checks the second. So an exemption outlives its reason silently, and the day
the reason changes the gate keeps saying the old one. That is the same shape as this week's other
findings: a condition written on one side of a pair reads as complete from that side.

The truncation is the second half of the same problem. `red_more=12` is honest about how many it hid;
the "can be deleted" list is not, because it was computed from what survived the truncation.

## Default, and work started on it

**Default: an exemption must be validated together with the reason it names, and the "no longer red"
list must be computed over the whole set.** Concretely: (1) each entry records the diagnostic text or
code it exempts; if the actual failure is a different one, the entry stops exempting and the gate says
so; (2) the red list is either not truncated or is truncated only for display, with the delete-candidate
computation always over the full set.

Reasons: (1) an exemption without a live reason is a silenced failure and the whole point of this layer
was to stop silencing; (2) a list that is honest about being incomplete one line earlier and dishonest
one line later is worse than either; (3) both repairs make the gate say less, which is what a gate
should do.

This round changes nothing in the file itself. The thirteen stale entries and the seven falsely-clean
names stay exactly as they are, recorded here, because deleting entries to make a report agree is the
mistake this page is about.