# progen findings, 2026-11-28: two runtime graphs a legal program builds are refused by the store's own invariants

`23b0204cf9` (`lane/progen`) · build host `zjwei@camel-work` ·
`[load]` 39.50 → 32.43 (1m) during these runs · 2026-11-28.

**These two are reported compiler defects, and the generator does not avoid either shape** —
dropping a construct from `test/tools/progen` because the compiler breaks on it would be making the
test cases dodge the defect, and the next program with that construct would then never be written.

| # | Record | Seed | Program | Fails in | Rule | Check |
|---|--------|------|---------|----------|------|-------|
| 1 | [`seed39-cycle.md`](seed39-cycle.md) | `39` | 21 lines | `main`'s runtime graph | no cycle of inputs (`docs/design/graph-store` 5.4) | `src/kernel/runtime/graph_edges.cpp:800` (`EdgeChecker::findCycle`), reported at `:811`, message at `:1004` |
| 2 | [`seed71-branch-arm.md`](seed71-branch-arm.md) | `71` | 11 lines | `main`'s runtime graph | a node between a BRCH and its JOIN belongs to exactly one arm (`docs/technical/17_graph_rewrite_api_contracts.md` 4.1) | `src/kernel/runtime/branch_arms.cpp:99` (`validateRuntimeBranchArms`), fails at `:358-370`, message at `:88-95` |

Both are `InternalDiag::UnknownInternalError at 1:1` — the invariant is a property of the committed
graph, not of a source location — and both are `RUN_ERROR` on all seven stage prefixes measured
(`none`, `macro`, `inline`, `fold`, `cse`, `algebra`, `dce`, each ending `std::nvm`), so no pass is
implicated: the graph is wrong when it is first committed.

## How they were found

`task-437` brought `test/tools/progen` back to the language the tip has (commit `23b0204cf9`, same
lane):

- `main` declares no return value — a declared one is printed as a line of the program's output, and
  the reference computes the printed lines and not that one, so the 41 programs that printed one
  line too many were fixed by the generator no longer returning a value, not by the reference
  learning to expect it;
- a closure holds the value a name had when it was made, not the name (`model.capture_write_violations`
  discards the three shapes the diagnostic itself names as refused);
- `lo..hi` is a `Range`, not an array.

After that generator, 100 programs run once each on `std::nvm` pass **98 of 100**, and the prefix
matrix (100 programs × 7 stage prefixes) passes **686 of 700 cells**. The two programs left are the
two records above — 7 red cells each, in every prefix.

## Coverage the generator now does not have, stated rather than hidden

The language allows a value that survives across calls to a closure in two ways only: a `Ref`
(design principles: a closure may capture a handle, because a handle is a value) or a loop's carried
state / `fold`. The shape `test/tools/progen` used to write — a function that owns a `var`, makes a
closure over it, and lets each call see the previous call's write — is refused by the front end now
(`SemanticDiag::ClosureAssignsCapturedName`), so `23b0204cf9` dropped it, which was an honest loss.

`f6b1994223` (same lane) puts it back the way the language allows — the handle:

```
import { Ref, ref, get, set } from state
    let v14: int = 0;
    let r7 = ref(v14);
    let c9 = func (p8: int) => int sync {
        set(r7, get(r7) + p8);
        return get(r7);
    };
    let n10 = c9(1); let n11 = c9(1); let n13 = c9(1);
    let g12 = get(r7);
    println('L2 {} {} {} {}'->format<n10, n11, n13, g12>);
```

The handle is not a way around the capture rule: the closure's write and read go through `set` and
`get`, and the printed readings move with them — the reference (`model.py` evaluates a handle as a
shared cell) and the compiler agree, 100 programs on `std::nvm` pass **98 of 100**, the two left being
the two records above, and no program of the shape is refused. These programs are also the samples for
**reading a key that is written**: one place writes the cell through `set` and another reads it with
`get` after the calls.

One rule the generator had to respect: `ref(<a loop or match expression>)` is refused with
`SemanticDiag::RefContentHoldsRef` ("a value it holds is projected from a value this program does not
spell out", `src/compile/src/gir/builder_refs.cpp:506`), so the initial value is a `let` with its type
spelled out and the box is made of that name.

## Both defects are in the translation, not in a pass

`std::gir` — the pass that only prints the graph the translation built — fails on both programs with
the same diagnostic `std::nvm` gives:

```
--- seed 71 std::gir: exit=1 ---
{"severity":"Error","code":"0x00000000",...,"message":"An error has occurred: Runtime graph 'main'
 breaks the branch-arm invariant: OPER 4 (:op/copy) runs between BRCH 2 and its JOIN 1 and belongs to
 two arms, 0 and 1: ...","data":{"stage":"internal","name":"InternalDiag::UnknownInternalError",...}}
--- seed 39 std::gir: exit=1 ---
... "Runtime graph 'main' breaks its edge invariants (1 violation(s)):\n  graph 'main' node 7 (11):
 lies on a cycle of inputs: 7(11) <- 13(8) <- 7." ...
```

So the graph is already wrong when it is handed to the first pass: the introducing code is in the
**GIR builder**, not in a rewrite rule. (The cycle in seed 39's reduction is the two-node
`7(11) <- 13(8) <- 7`, shorter than the six-node one the whole generated program reports.)

## Seed 71, reduced once more: the statement after the `if` is part of it

Whole programs, each through `std::gir` only (`SHORT` = the graph commits):

| Program | Result |
|---------|--------|
| `if true then sync { null; } else sync { println(''); null; };` | SHORT |
| `if true then sync { var v50 = false; null; } else sync { println(''); null; };` | SHORT |
| `if true then sync { var v50 = false; v50 = false; null; } else sync { println(''); null; };` | SHORT |
| `if true then sync { var v50 = false; v50 = false && v50; null; } else sync { println(''); null; };` | SHORT |
| the same, **plus `let n64 = 0;` after the `if`** | **`OPER 4 (:op/copy)` in two arms** |
| `var v50 = false;` before the `if`, written in an arm | SHORT |
| the same `var` written in **both** arms | SHORT |

The defect needs all three: a `var` declared and written inside one arm, and **a statement after the
`if`** — i.e. the `if` has to be a step of the enclosing block, so the arm's value is handed out of the
branch instead of ending it. That is the shape `builder_control.cpp:268` (`copyNodeOf`) makes the copy
for; the copy keeps the region of the arm it was made in, and the block that continues after the `if`
reads it from the other side of the JOIN. **Not fixed here** — see "State of C" below.

## Two more, found by the re-pointed `progen_check` batch

`generate.VERSION` 9 re-points every seed at another program (as 7 → 8 did), so the 25-seed batch of
`test/plans/feat/opt/progen.plan.toml` names other programs now and reads **35 wrong of 1025 cells**:
990 pass, and the 35 are two seeds that build a graph a pass then refuses. Neither seed uses the `Ref`
shape, so neither is a consequence of `f6b1994223`.

| Seed | Cells | Fails in | Message |
|------|-------|----------|---------|
| `11` | 18 | `std::opt` (17), and `std::cxx::fold` in `inline+fold@nvm` | `Runtime graph '__root__' breaks its edge invariants: sync edge 9 -> 105 joins two blocks: 9 is in the block of region 0 and 105 in region 20.` (the `fold` cell: `graph 'main'`, `sync edge 7 -> 106`) |
| `25` | 17 | `std::opt` | `graph 'w34$spec19' node 3 (10): passes a string as ordinary argument 0 of 'r1$spec40', which takes a int` (in two cells `'w34$spec3'` … `'r1$spec16'`) |

The names are listed in that entry's `desc`; no seed was dropped, `CHECK_EXCLUDED` was not touched and
no assertion was changed.

## seeds 48 and 94 (`lane-witness`: `fold`, four back ends, two cells each)

### The crossing edge of seed 71, from the draft graph itself

A temporary environment-gated print in the commit check (added for one round and **removed again**,
not committed: the source tree carries no trace of it) dumped the committed draft of the two
statement program, node by node. `main`, 17 nodes:

```
DATA 0 | region the body (region 0) | norm[] with[] wait[] out[2]
JOIN 1 | region the body (region 0) | norm[2] with[9 16] wait[11] out[]
BRCH 2 | region the body (region 0) | norm[0] with[] wait[] out[1] | BRCH join 1 arms[1 4]
DATA 3 | region the body (region 0) | norm[] with[] wait[] out[4]
OPER 4 (:op/copy) | region arm 0 of BRCH 2 (region 1) | norm[3] with[] wait[2] out[5 16]
OPER 5 (:var/new) | region arm 0 of BRCH 2 (region 1) | norm[4] with[] wait[] out[7 11]
DATA 6 | region the body (region 0) | norm[] with[] wait[] out[10]
OPER 7 (:var/get) | region arm 0 of BRCH 10 (region 2) | norm[5] with[] wait[10] out[]
DATA 8 | region the body (region 0) | norm[] with[] wait[] out[]
JOIN 9 | region arm 0 of BRCH 2 (region 1) | norm[10] with[7 8] wait[] out[11]
BRCH 10 | region arm 0 of BRCH 2 (region 1) | norm[6] with[] wait[2] out[9] | BRCH join 9 arms[2 3]
OPER 11 (:op/assn_b) | region arm 0 of BRCH 2 (region 1) | norm[5 9] with[] wait[9] out[]
DATA 12 | region the body (region 0) | norm[] with[] wait[] out[13]
OPER 13 (:io/println) | region arm 1 of BRCH 2 (region 4) | norm[12] with[] wait[2] out[16]
DATA 14 | region the body (region 0) | norm[] with[] wait[] out[15]
GATE 15 | region the body (region 0) | norm[14] with[] wait[1] out[]
GATE 16 | region arm 1 of BRCH 2 (region 4) | norm[4 13] with[] wait[] out[]
```

**`GATE 16` is arm 1's value gate (region 4) and its first operand is node `4`, the `:op/copy` that
lives in arm 0 (region 1).** That is exactly the reported violation, and it is one edge: the else arm's
gate was handed the *other* arm's copy as its value. `JOIN 1` reads that gate (`with[9 16]`), which is
what the check sees as "belongs to two arms".

So the introducing step is not where the copy is made — `builder_control.cpp:268 copyNodeOf`, and the
copy is where it belongs, in arm 0, waiting on the BRCH — it is where **the arm's value gate is built
with the arm-0 copy as its value operand**. `addArm` → `readArmCellValue`
(`builder_control.cpp:2212`, `:862`) is the code that hands an arm's value out and is the next thing to
read: the copy reaches the gate instead of a `:var/get` of that arm's own cell, and the arm the gate
belongs to holds no cell at all.

## seeds 48 and 94, and the reading that says nothing

Same batch, same re-pointing. A probe run of the two seeds against the tip build (`17:06`, `c014edb0ca`,
`BUILD_EXIT=0`) reads:

```
seed 48 [nvm]:  exit=1 -> Cannot convert the string '' to int.
seed 48 [fold]: exit=1 -> Pass 'std::cxx::fold' failed with an internal error:
                          Runtime graph 'r17' breaks its edge invariants (1 violation(s)):
seed 48 [opt]:  exit=1 -> Pass 'std::opt' failed with an internal error:
                          Runtime graph 'r17' breaks its edge invariants (1 violation(s)):
seed 94 [nvm]:  exit=1 -> Cannot convert the string '' to int.
seed 94 [fold]: exit=1 -> Cannot convert the string '' to int.
seed 94 [opt]:  exit=1 -> Cannot convert the string '' to int.
```

The `nvm` lines are the probe's own doing — it set `PROGEN_I0` to the empty string, so the program
stopped before it ran; **seed 94's three lines therefore say nothing** and its `fold` reading is not
reproduced here. Seed 48's `fold`/`opt` lines are clean: the pass fails before the program runs, and
the graph that breaks is **`r17`** — a rest/closure graph, not `main`, which is the first time one of
these defects lands outside the entry graph. Whether 48 and 94 share a root cause with 11 (`std::opt`
wiring a `sync` edge between blocks of two regions) or with 25 (specialization handing an argument of
the wrong type) is **not established**; the next step is the same draft-graph dump the other two need.

## State of C: located, not fixed

The four defects are located as above and no fix is in place. What the criteria of C still lack:

- seed 71: the reduction is 2 statements plus the `if` (table above); the introducing code is in the
  builder's branch/copy region handling (`builder_control.cpp:268`, `copyNodeOf`, whose node keeps the
  arm's region) — the exact line that hands the copy to the JOIN is not yet named;
- seed 39: fires at `std::gir`; the reduction still needs `get_env`, a `with <...>` function used as a
  value and called, and a `var` read through a coalesce into a call — the cycle is between a sync node
  and an operand of the `let` that follows;
- seeds 11 and 25: `std::opt` (and `std::cxx::fold` once) build a graph their own commit check refuses;
  neither is a wrong value or a crash, and both fire only under passes, so their introducing code is
  in the pass, not the builder.

`progen` therefore still reads **98 of 100** on `std::nvm` (686 of 700 cells with the prefix matrix,
before the `Ref` shape; the shape adds no red), and `progen_check` reads 990 of 1025. The two minimal
programs are **not** in `test/cases` yet: a case that lands red without a fix would be a `known_fail`
in disguise, and `known_fail` is not this lane's to write.

## Not touched

`known_fail`, `baseline.json` and the golden files are unchanged by this lane; no assertion was edited
to make a red green. The one plan file changed is `test/plans/feat/opt/progen.plan.toml`
(`c014edb0ca`): its `desc` now says `generate.VERSION 9`, records the two new defects and says the
re-pointed batch is red — its assertions, `expect_stdout_contains`, `expect_output_not_contains`,
`expect_exit` and `cpu_budget_ms` are untouched. `cpu_budget_ms` still carries the 83670 ms reading
whose re-derivation belongs to the plan's owner (the re-pointed batch measured 162.49 s of CPU, over
the 150000 ms budget, on a host at load 27 → 33).