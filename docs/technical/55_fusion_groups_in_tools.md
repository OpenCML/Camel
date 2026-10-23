# Fusion groups in the tools: `std::profile` and the debugger

A back end may run several marked nodes as one pass at the last of them
(`include/camel/execute/fusion_group.h`; design principles 9.6 keeps the operators on the graph,
so the program is the same program). Two tools used to lie about that:

- the profile's operator table had no row for the group's own time, because a group is not an
  operator call, and the time disappeared between the rows;
- neither the profile nor the debugger mentioned the members that were skipped, so the tail looked
  like one operator that took as long as four and the members looked like nodes that never ran.

This note records what each tool now says, why the members are not given rows or stops, and the
limits that remain.

## 1. Profile

One row per group, printed after the operator table, and a line in the aggregate:

```
  fusion groups        3 runs in 1 group (9 operator executions), 41.2 µs (0.4% of the run)
...
fusion groups (one pass each; its members' operators did not run as calls)
  tensor.fusion="vjp_div_neg"  3 members (tensor:divide, tensor:multiply, tensor:neg)
    3 runs (9 operator executions), 41.2 µs (0.4% of the run, first 13.1 µs)
```

A **member gets no operator row**. A row means "this operator was called this many times"; the
members' operators were not called, so a row with zero would be a row for an operator that never
ran and a row with their share of the group's time would be inventing a boundary the kernel does
not have. Absent is the true statement, and the group's row names the members so a reader can find
them. The same reasoning gives the step block its `operator executions per step` line: it adds
`runs × members` to the operator calls of the step, so the group is counted once per round rather
than not at all or twice.

The invariant, printed nowhere but true and checked by the guards:

```
operator executions = operator calls (the table's rows) + group runs × members
```

`operators.calls` (JSON) and the table are the first part; the top-level `operator_executions`
adds `fusion_operators`, and `step_fusion` (next to `step`) does the same per round. Every one of
those keys is emitted only when a group ran. A group's time is its kernel
call at the tail only; the validation at the earlier members is a shape check and is not counted
(it is per member and is not the group's work).

## 2. Debugger

A stop is *before a node runs*. The members do not run, so they are never stopped at; a node step
walks over them and stops at the tail, where the group runs. The stop carries `fusion` (the group's
name, its members' operators in execution order, their refs) and the debug console prints one
sentence:

```
fusion group `vjp_div_neg` runs here: it computes tensor:divide, tensor:multiply, tensor:neg in one pass; those nodes do not execute separately.
```

The alternative -- stop at a skipped member and say it is a member -- was rejected: a stop asserts
that the node is about to execute, and the profile says it did not. Two tools disagreeing about
whether a node ran is the defect this change removes, not a second way to report it. A member with
no source origin (the VJP's nodes have none today) cannot be named by a source breakpoint at all;
a node step is the tool that reaches the group, and it reaches it at the tail.

## 3. Cost and limits

Off (no `std::profile`, or `report=false hints=false`): nothing new runs; the report and the JSON
are byte-identical to the base commit's, which
`test/cases/modules/tensor/fusion_group_none.profile.golden.txt` pins.

On: one relaxed flag load per attributed node, one `steady_clock` pair per **group run** (not per
member) plus a handful of atomic adds and, on the first sight of a group, a copy of its key, name
and member list. A group of *n* nodes costs less than *n* operator timing pairs, because the
kernel's interior is one call.

What it still cannot see:

- the group's interior: the row is one number for the whole kernel, so "which member is slow" is
  not answerable, and no boundary exists to attribute an allocation to a member;
- the head's validation time (see §1);
- a kernel that does work off the calling thread: the timing pair is the caller's wall time, and
  the allocation column's per-thread limit applies unchanged;
- the debugger cannot break inside a group kernel: the group is one node to the observer, so the
  unit of debugging there is the group, not the operator.

## 4. Guards

- `test/plans/feat/modules/fusion_group.plan.toml`: `fusion_group.profile.group` (one group, exact
  counts and members, once-only invariant, cross-backend reference), `fusion_group.profile.none`
  (the base report and JSON, `report=false` byte-identical, no fusion key), and the row assertions
  of `.fused` / `.safe` (the table has the forward divide and no multiply or neg row).
- `test/plans/feat/tools/debugger.plan.toml`: `debugger.dap_fusion_group` (one fusion stop, at the
  tail, members in order, no member stop, the exact sentence once).