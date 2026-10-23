# The anchored node: the engine's handover, and what the drop refusal actually covers

Author: the H367 agent (the effect check and the anchor handover), 2026-10-24. Status: open —
the handover half needs the coordinator's ruling; the audit half needs no decision.

The two rulings of 2026-10-23 this work reads
(`docs/handover/reviews/questions-2026-10-23.md`):

- `rule-drops-effect`: before a rule is applied, the subgraph bound by the pattern variables the
  right side does **not** use is examined, and an effect node — or a node carrying a sync or order
  edge — there refuses the application.
- `rule-conditions-and-policy` item 3: when a rule replaces the node the graph's exit and returned
  value live on, the **engine** moves the anchor onto the replacement, as the C++ pass's
  `replaceFoldResult` does. Not a rule condition.

## 1. The handover is in the engine already, and the corpus is what says so

`DraftWritings::rewrite` has moved the anchor since before this work: after the replacement is
built and the uses are retargeted, `draft_.setEntryNode`, `setExitNode`, `setOutputNode`,
`setReturnNode` and `setRegionResult` each move when they name the rewritten node
(`src/runtime/writings.cpp`). The measurement, from the whole fast layer:

| run | `anchor_add` | `anchor_mul32` |
| --- | --- | --- |
| `rules::simplify` (the engine) | 1 node, 0 edges, exit PORT `x` | 1 node, 0 edges, exit PORT `x` |
| `std::opt::algebra neutral_only=true` (the C++ pass) | 3 nodes, 2 edges, exit `:op/add_l` | 3 nodes, 2 edges, exit `:op/mul_i` |
| the deleted full law table (the golden) | identical to the reference row | identical to the reference row |

So the engine reaches the replacement of `x + 0 => x`, which is the port, and the anchor is on it.
That is exactly item 3's sentence. The difference in the corpus is therefore **by design**: the C++
pass keeps the operation as the named node because its `isReplaceable`
(`src/passes/opt/generic/rewrite.cpp`) refuses to rewrite the exit, the output and the returned
value, and the engine does not refuse — it hands over.

Two further measurements fix the direction, because a refusal and a handover reach different
graphs:

1. **The refusal reproduces the C++ form.** With a temporary `isReplaceable`-shaped guard in
   `DraftWritings::rewrite` (`exit/output/return == node → continue`), the library side of
   `anchor_add` and `anchor_mul32` becomes 3 nodes and 2 edges exiting at the operation — the C++
   form, node for node. The corpus entries would stop being differences.
2. **That guard costs the `rules` lane 68 of its 115 tests.** Measured three times on the same
   tree: `rules.plan.toml` is 115/115 green with the engine as it is, 47/115 with the guard. The
   first failures are the ones that fold an expression at a graph's return node
   (`rules.rewrite_pass`: `coefficients(x) = x * 3 + x * 5` becomes `x * 8`; `rules.library_simplify`
   the same), and the `check` machinery, which builds its sample graph so that the matched node is
   the anchor, stops firing altogether.

The engine's handover is the reading that keeps both: the ruling's sentence, and the lane that
folds a returned expression. What is left in the corpus (the two anchored entries) is the
reference mode's documented restriction, which H160 already records as the reason `algebra.cpp`
is not deleted (`docs/technical/45_graph_module.md`: the reference mode "keeps the
graph's named node (the C++ pass's `isReplaceable`), while the rule engine retargets it"). **The
two entries do not change sides under either reading**; what changes is whether the library reaches
the port or the operation.

The decision this leaves to the coordinator: keep the engine's handover and the corpus's two
recorded differences (this work's reading, and the one the `rules` lane and item 3 both support),
or apply the reference mode's refusal to the engine and update 68 `rules` entries plus the `check`
machinery. This work does not make the second change on its own — it is a semantic change to the
rule engine, it contradicts item 3's sentence, and 68 tests are the coordinator's budget to spend.

## 2. What the drop refusal actually refuses (the audit, no decision needed)

`dropsOrdered` (rewrite loop and saturation kernel) asks `pureComputation` of every node the right
side stops reading — and `pureComputation` walks the `norm` and `with` input closure of that node,
refusing when it meets:

| node | refused by | which ruled condition it is |
| --- | --- | --- |
| `Func`, `Call`, `Join`, `Brch`, `Sync`, `Loop` | `carriesOrdering` | **effect node** (a call runs a graph; a branch, join, sync or loop runs or orders what it holds) |
| `Oper` that is not pure — `:io/println` and the other effectful operators | `carriesOrdering` | **effect node** |
| `Gate` that waits on something | `carriesOrdering` (`!ctrlInputsOf.empty()`) | **effect node** and **order/sync edge** |
| any node with a control input | `!draft_.ctrlInputsOf(at).empty()` | **order and sync edge** (design principles 3.3: a control input orders the node) |
| the slot of a value that can be written in place | `writtenInPlace` (through `isWrittenInPlace`, gates followed, `var` arguments and writing operands) | **node written in place** |
| a pure `Oper` / a `Cast`, `Copy`, `Accs`, `Fill` | nothing — refused only when it is in the closure of one of the rows above | not a ruled condition |

Every condition the ruling lists is refused, and none of them incidentally: each row above is a
separate test in `pureComputation`, and the closure walk means a condition met deep under a dropped
value is found too (a dropped call whose callee reads a written slot refuses).

Where the check is **wider** than the ruling, deliberately:

- It refuses a dropped pure computation that is ordered after an effect through its own control
  input, even though the program would still run that effect. Design principles 3.3/3.4 (H95) is
  the authority: the engine "must not move the computation across an effect", so the writing waits
  for the control inputs the match read. This is the conservative side of the ruling, not a gap.
- It refuses a dropped pure computation whose closure contains a node written in place or a
  `Gate` that waits, on the same conservative reading.

Where it is **narrower** than a literal reading: a variable the right side **does** read is never
examined, and `remainder` terms are examined only when the right side does not read that variable
at all. That is correct rather than narrow: a read variable's value is still the value the program
computes, so nothing about it leaves the program.

## 3. What is missing, if anything

Nothing from the ruling's list. The gaps that remain are the ones the design already names:
`with` inputs are walked (so a dropped computation reached through a pass parameter is examined too),
but a node reached only through a `Brch`/`Loop` **region body** is not, because `pureComputation`
walks `normInputsOf` and `withInputsOf` and not region results — a dropped value whose only route to
an effect is "the region's result feeds an effect" is not examined on that route. The branch node
itself carries ordering (`carriesOrdering`), so the common shape is covered; a case where the branch
is outside the dropped closure and only the region result is inside is the one shape a test would
have to build.