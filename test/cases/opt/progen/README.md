# progen findings

Defects found by the random program generator and differential runner in
[`test/tools/progen`](../../../tools/progen/README.md), each with a reduced program.

- `findings/<name>.cml`: one program per finding. Its header gives the pipeline, the expected
  output (computed by the generator's reference evaluator, not by a backend) and what happens.
- `findings.json`: the same list for tools (pipeline, environment, expected lines).
- `judgement/`: programs for the questions in "For the owner to decide".

```
python test/tools/progen/progen.py findings          # re-runs every finding, 3 runs each
```

prints, per finding, whether it still fails as recorded. All 33 failed on every run when this
was written (2026-10-11, branch `lane/gen`, Linux container, release build). The test plan
`feat/opt/progen` runs a batch of seeds that pass and, since the fixes below, the findings
recorded as fixed (`progen.py fixed`: key `fixed` of `findings.json`, each on the four
schedulers under the rewrite sequence it was found with, `std::inline`, `std::opt`,
`std::inline std::opt::dce`, `std::opt::simplify` and `std::specialize`). The other findings are
in no test.

**Fixed since**: F25 to F31 and F33, the failures without any pass except F24 (branch
`lane/frontend-fixes`), and the findings of cause C1 (branch `lane/simplify-fixes`). Their rows
name the commit; each has a test in the default set, and `progen.py findings` reports them as
"does not fail". Findings traced to a cause but not fixed say so in their row; see "Root causes"
below. For the others, **nothing was fixed, and the compiler code was not read to confirm a
cause** (except where a source file is named). The "class" column is a judgement from the symptom and the
reduced program, using the five classes of the roadmap (effect order lost; a written variable
taken for one value; branch arms; macro operands inlined early; unbounded growth) and
"(other)" where none fits. Two findings of one class may have one cause, and one reduced
program may hide a second defect.

## Fixed (branch `lane/inline-fixes`)

Twenty findings of progen had three causes (A to C; D is a fourth defect found by hand). A to C are about what a branch arm is to the schedulers:
when an arm is taken they run the nodes that run after the arm's head and not after the JOIN,
as one range of the linear order, and everything else of the graph unconditionally before the
BRCH. The findings were read against the graphs (`std::rgir`, `std::tns`) before and after the
pass; the status column of the tables names the cause.

- **A** (`e82fa21e`, `src/runtime/draft_inline.cpp`). The splice hands the control predecessors
  of a call (the BRCH whose arm the call is, the statement before it) to the entry roots of the
  body. SYNC and GATE nodes were not roots. A callee that had itself been inlined into starts
  with the entry SYNC of that splice, so it had no root: the predecessors went to the callee's
  recorded entry node (often its last GATE) and the body ran outside the arm, before the BRCH.
  Code of an arm that is not taken ran (F01, F02, F03, F18), a BRCH was laid out before the
  branch its JOIN was in, so that skipping its arm skipped statements of the enclosing sync
  block or the taken arm (F04, F09, F10, F12, F19) or left a value unset (F14, F15, F16, F32), or the
  layout could not be executed at all (F20, F21, F22).
- **B** (`ac9a55a1`, `src/runtime/draft_inline.cpp`). A call can be inside an arm through an
  argument only (it has no control predecessor). Nodes of its body that read no parameter then
  had nothing that kept them in the arm. F05: a branch on a constant of a pure callee left the
  arm while its JOIN stayed. F06: the body waited on the BRCH but not on the arm's head (the
  head was the operator computing an argument the callee ignores), and was not run.
- **C** (`ff52c85b`, `src/passes/opt/generic/rewrite.cpp`). `replaceNode` gives the users of a
  removed node the ordered nodes its value came from. It walked through an arm's head that is
  a pure node to the BRCH, so the users waited on the BRCH and no longer on the head (F08, F13:
  `std::opt::dce` after `std::inline` removed the unused conversion after the head).

- **D** (`ca28e0b8`, `src/runtime/draft_inline.cpp`). `tryResolveStaticValue`, which finds the
  constant an argument is so that specialization can bind it, looked through CAST nodes: the
  constant under `i as float` was bound without being converted (F35, found by hand).

Not fixed: **F17** and **F36** (below) are in the JIT back end.

F12 and F19 (found with `std::opt::simplify`, which runs the same inliner) have cause A: the
simplify line reached it independently, with `GATE(constant)` in front of the body where A
above names the entry SYNC. F07, F11 and F23 print their expected output on the four schedulers
since these commits as well; the simplify line traced them (and F06, F08, F13) to the
schedulers reading an arm from its head only, and fixes that in `resolveRuntimeBranchArmEntry`.
B and C keep the code of an arm behind its head on the rewriting side; the two do not conflict.
F07, F11 and F23 are left to that line and are not recorded as fixed here.

Seeds 1-1000 of the `cross` matrix (41 cells per program, all features), before and after:

| | OK | located refusal | silent mismatch | crash | hang | run-stage error | not run | programs with a bad cell |
|---|---|---|---|---|---|---|---|---|
| before | 40126 | 24 | 545 | 39 | 7 | 4 | 255 | 52 |
| after A, B and C | 40975 | 24 | 1 | 0 | 0 | 0 | 0 | 1 |

No cell is bad after that was not bad before. The cell left is seed 944 under
`std::inline std::jit` (`L8 -nan` where `L8 -2` is expected; the other schedulers print `-2`);
it was not reduced.

## How they were found

| batch | seeds | features | matrix (cells per program) | programs | cells | agree | located refusal (legal) | silent mismatch | crash | run-stage error | other bad | not run | programs with a bad cell |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A | 1-2000 | all | cross (41) | 2000 | 82000 | 80380 | 48 | 1113 | 217 | 42 | 0 | 200 | 108 |
| B | 2001-2400 | without env, macro | cross (41) | 400 | 16400 | 15974 | 0 | 157 | 28 | 41 | 0 | 200 | 22 |
| C | 2401-2800 | without effects, var, varparam, capture, wait | cross (41) | 400 | 16400 | 16276 | 8 | 79 | 5 | 0 | 0 | 32 | 9 |
| D | 2801-3200 | without string, float, struct, tuple | cross (41) | 400 | 16400 | 15791 | 16 | 136 | 46 | 145 | 44 | 222 | 34 |
| F | 3201-3400 | all | frozen P1-P12, R01-R60 without module passes (37) | 200 | 7400 | 7277 | 15 | 6 | 8 | 0 | 40 | 54 | 7 |
| E | 3401-3600 | all | full (101) | 200 | 20200 | 19906 | 0 | 195 | 7 | 0 | 0 | 92 | 9 |
| total | | | | 3600 | 158800 | 155604 | 87 | 1686 | 311 | 228 | 84 | 800 | 189 |

Reading the table:

- "cross" is every one of 25 rewrite sequences on the node VM, plus no rewrite, `std::inline`,
  `std::opt::simplify`, `std::opt` and `std::macro std::opt` on `std::fvm`, `std::jit` and
  `std::prl`, plus the run without passes.
- "located refusal": all are `RuntimeDiag::MacroEvaluationFailed` of `std::macro` (see J1).
- "not run": after four crashed cells the rest of a program is skipped (a crash takes many
  seconds to be collected on this system).
- "other bad" in D and F: one program each that the compiler rejects (41 and 37 cells, F33),
  and five cells that timed out at 30 s. Three of the five were run again with a limit of
  150 s: two are segmentation faults that were slow to be collected, one passed in 0.3 s. No
  hang was confirmed. Batch F also has one unlocated refusal (see "Seen, not reduced").
- Bad cells were run two more times. In batch A, 100 of the 1372 bad cells were not the same on
  all three runs (27 programs): 68 on `std::nvm`, 17 on `std::prl`, 8 on `std::jit`, 7 on
  `std::fvm`.

**Where the bad cells are (batch A).** The run without passes, the four schedulers without a
rewrite, and the sequences `std::opt::fold`, `std::opt::cse`, `std::opt::dce`, `std::macro`,
`std::devirtualize`, `std::specialize`, `std::opt::fold std::opt::cse`,
`std::opt::cse std::opt::dce` and `std::macro std::opt::fold std::opt::cse std::opt::dce` each
have exactly one bad program, the same one: it crashes without any pass (F25). Every other bad
cell has `std::inline`, `std::opt::simplify` or `std::opt` in its pipeline: 52 programs are bad
under `std::inline std::nvm`, 52 under `std::opt::simplify std::nvm`, 54 under
`std::opt std::nvm`.

**`std::macro`.** No finding is of the class "macro operands inlined early". Over batches A to
E, 9 cells of 4 programs (A 204, C 2508, D 2920 and 3027) are bad with `std::macro` in front of
a sequence that passes without it on the same scheduler. The one that was reduced (D 2920,
`std::macro std::inline`) is a crash that depends on the path of the source file and whose
`macro` function is not even called, so it says nothing about `std::macro` itself; the other
three were not reduced.

**Self-check of the generator.** The run without passes agreed with the reference for 1999 of
2000 programs in batch A, 398 of 400 in B, 400 of 400 in C, 395 of 400 in D, 199 of 200 in F
and 200 of 200 in E: 3591 of 3600. All nine disagreements are the compiler failing to build
or run the program: F25 (three programs; two were reduced, the third was matched by its shape
only), F31 (four) and F33 (two). No disagreement was a wrong reference. Earlier self-check
runs over seeds 1-1500 (before the batches) agreed on 1499; the one failure was F25.

## Findings

Ordered by severity: silent wrong results from standard passes; faults and crashes from
standard passes; failures without any pass. "all" under schedulers means `std::nvm`,
`std::fvm`, `std::jit` and `std::prl` were each run on the reduced program and failed.

### Silent wrong results after a standard pass (exit 0, no diagnostic)

| id | program | pipeline | schedulers | expected | actual | class | status |
|---|---|---|---|---|---|---|---|
| F01 | `inline_untaken_arm_effect.cml` | `std::inline` | all | nothing | `a` (a call in the arm of `if false` runs) | branch arm | fixed (A, e82fa21e) |
| F02 | `inline_wrong_arm_runs.cml` | `std::inline` | nvm, prl | nothing | an empty line (the `else` arm runs although the test is true) | branch arm | fixed (A, e82fa21e) |
| F03 | `opt_untaken_arm_effect.cml` | `std::opt` | all | nothing | `L2 0 0 0 0` (a lambda in the arm of `if false` runs) | branch arm | fixed (A, e82fa21e) |
| F04 | `inline_statement_effect_lost.cml` | `std::inline` | all | `pq` | nothing (a `println` statement of `main` is dropped) | effect order lost | fixed (A, e82fa21e) |
| F05 | `opt_statement_effect_lost.cml` | `std::opt` | all | an empty line | nothing (the first `println` of `main` is dropped) | effect order lost | fixed (B, ac9a55a1) |
| F06 | `opt_lambda_effect_lost.cml` | `std::opt` | all | `false` | nothing (effect of a call with unused result in a sync lambda) | effect order lost | fixed (B, ac9a55a1); the schedulers also misread the arm (cause C1, 741b8705) |
| F07 | `simplify_nested_function_effect_lost.cml` | `std::opt::simplify` | all | an empty line | nothing (`println` of a nested sync function) | effect order lost | passes since A-C; see the note below; the schedulers also misread the arm (cause C1, 741b8705) |
| F08 | `inline_dce_arm_effect_lost.cml` | `std::inline std::opt::dce` | all | an empty line | nothing (`println` of the taken arm) | branch arm | fixed (C, ff52c85b); the schedulers also misread the arm (cause C1, 741b8705) |
| F09 | `inline_fvm_arm_effect_lost.cml` | `std::inline` | fvm, jit | an empty line | nothing (`println` of the taken arm; nvm and prl print it) | branch arm | fixed (A, e82fa21e) |
| F10 | `inline_unused_branch_effect_lost.cml` | `std::inline` | all | an empty line | nothing (`println` of the taken arm of an unused branch) | branch arm | fixed (A, e82fa21e) |
| F11 | `simplify_arm_write_lost.cml` | `std::opt::simplify` | all | `a` | an empty line (a write made in the taken arm is lost) | written variable taken for one value | passes since A-C; see the note below; the schedulers also misread the arm (cause C1, 741b8705) |
| F12 | `simplify_effect_order.cml` | `std::opt::simplify` | all | empty line, then `L1 0 0` | `L1 0 0`, then empty line | effect order lost | fixed (A, e82fa21e; found by the simplify line as well) |
| F13 | `inline_dce_arm_value.cml` | `std::inline std::opt::dce` | all | `1` | `0` | branch arm | fixed (C, ff52c85b); the schedulers also misread the arm (cause C1, 741b8705) |
| F14 | `inline_branch_value_null.cml` | `std::inline` | nvm, fvm, jit | `L4 (0, [0]) 0` | `L4 null 0` | branch arm | fixed (A, e82fa21e) |
| F15 | `inline_match_value_null.cml` | `std::inline` | all | `L6 { n: 5, tag:  }` | `L6 null` | branch arm | fixed (A, e82fa21e) |
| F16 | `fold_inline_env_value.cml` | `std::opt::fold std::inline` | all | `L11 false 4` | `L11 false 0` (a value read from the environment) | (other) wrong value after inlining | fixed (A, e82fa21e) |
| F17 | `inline_jit_argument_value.cml` | `std::inline` | jit | `L8 0 0 24` | `L8 0 0 0` (nvm, fvm and prl print 24) | (other) wrong value after inlining | open: in the JIT back end, see F36 |
| F32 | `fold_inline_match_arm_value.cml` | `std::opt::fold std::inline` | all | `L4 0 3 a` | `L4 0 0 a` (a pure function; no effect, no variable) | branch arm | fixed (A, e82fa21e) |
| F36 | `inline_jit_second_call.cml` | `std::inline` | jit | `1`, `1`, `11` | `1`, `2`, `17` (found by hand: a self-tail-recursive function, right on its first call, wrong on later ones) | (other) wrong value after inlining | open: in the JIT back end |
| F35 | `opt_cast_argument_value.cml` | `std::opt`, `std::opt::simplify`, `std::specialize` | all | `7.5`, `17.25`, `10.75` | `0.5`, `-1.75`, `7.75` (found by hand: an argument written `i as float`) | (other) wrong value after a rewrite | fixed (D, ca28e0b8) |

### Faults and crashes after a standard pass

| id | program | pipeline | schedulers | expected | actual | class | status |
|---|---|---|---|---|---|---|---|
| F18 | `inline_untaken_arm_fault.cml` | `std::inline` | all | nothing, exit 0 | `Array index out of bounds`, exit 1: the body of a call in the arm of `if false` runs | branch arm | fixed (A, e82fa21e) |
| F19 | `simplify_recursion_never_ends.cml` | `std::opt::simplify` | nvm, prl | nothing, exit 0 | `MaxRecursionDepthExceeded` in graph `__g_10__` (fvm and jit finish) | unbounded growth | fixed (A, e82fa21e; found by the simplify line as well) |
| F20 | `inline_crash_function_argument.cml` | `std::inline` | all | nothing | segmentation fault | (other) crash after a rewrite | fixed (A, e82fa21e) |
| F21 | `inline_crash_capturing_lambda.cml` | `std::inline` | all | nothing | segmentation fault | (other) crash after a rewrite | fixed (A, e82fa21e) |
| F22 | `specialize_inline_crash.cml` | `std::specialize std::inline` | all | an empty line | segmentation fault | (other) crash after a rewrite | fixed (A, e82fa21e) |
| F23 | `simplify_crash.cml` | `std::opt::simplify` | all | nothing | segmentation fault | (other) crash after a rewrite | passes since A-C; see the note below; the schedulers also misread the arm (cause C1, 741b8705) |
| F34 | `opt_permanent_space.cml` | `std::specialize std::opt` | all (the pass fails before any scheduler runs) | nothing, exit 0 | `PassInternalError`: "Permanent space exhausted (32 MB of types and layouts)", no position, exit 1 | unbounded growth (a query that allocates; cause C3) | open: cause C3 |

### Without any pass

| id | program | schedulers | expected | actual | class | status |
|---|---|---|---|---|---|---|
| F24 | `const_graph_capacity.cml` | all, and every pipeline | `a` | `std::bad_alloc`, no position, exit 1, before the first pass | unbounded growth (capacity) | |
| F25 | `nested_function_value_crash.cml` | nvm, fvm, jit crash; prl prints something else | `[1]` | segmentation fault | (other) crash | fixed (a42941f8) |
| F26 | `string_of_bool_crash.cml` | all | `true` | segmentation fault | (other) crash | fixed (edc8f2ea) |
| F27 | `string_ordering.cml` | all | four lines `true` | four lines `false` | (other) operator not implemented | fixed (db5e452c) |
| F28 | `string_plus_assign.cml` | all | `ab` | `a` | (other) operator not implemented | fixed (5f3c58f2) |
| F29 | `mod_assign.cml` | all | `2` | located run-time error "assignment operator not fully implemented" | (other) operator not implemented | fixed (1224039c) |
| F30 | `tuple_comparison_parse.cml` | all | `(true, x1)` | syntax error "missing ')' at ','" | (other) parser | fixed (1d881d59) |
| F31 | `return_named_function.cml` | all | `true` | "Compile graph 'f4' still contains a live DREF node", no position, exit 1 | (other) internal error | fixed (a42941f8) |
| F33 | `let_named_function_captured.cml` | all | `[2]` | compile-time error "Function of type 'void' cannot be called" at the call in the lambda | (other) front end | fixed (a42941f8) |

Notes on single findings:

- **F24.** The limit is 62 graphs that own a constant object, where a graph is a function, a
  lambda or one arm of an `if` / `match`, and it owns one when a string literal or an array,
  tuple or struct literal (even with run-time elements) is written in it. 62 functions each
  returning `'a'` fail; 31 `if` expressions with two string arms fail; 3000 strings in one
  graph, or 600 `if` expressions with integer or float arms, compile. The only user of the
  16 MB meta space is the graph arena (`include/camel/compile/gir/arena.h`,
  `src/core/mm/mm.cpp`), which is where the `std::bad_alloc` comes from; that the arena takes
  about 256 KB per such graph is an inference from 16 MB / 62, not something read in the code.
  `docs/cli.md` lists a permanent space of 32 MB with a named diagnostic; this limit is not
  listed and has no diagnostic. The generator stays below it (at most 34 such graphs by its
  own count), which is why its programs are about 115 lines.
- **F27, F28, F29 (fixed).** `src/builtin/executors/builtin.cpp` mapped `op/lt_s`, `op/le_s`, `op/gt_s`,
  `op/ge_s`, `op/assn_add_s` (and the untyped `op/lt`, `op/le`, `op/gt`, `op/ge`, `op/assn_add`,
  `op/assn_sub`, `op/assn_mul`, `op/assn_div`, `struct/contains_str`) to `__not_implemented__`
  (`src/builtin/operators/other.h`), whose body is an `ASSERT(false)` followed by returning the
  null slot. In a release build the assertion is compiled out, so the operator silently yields
  null: a comparison is false, an assignment does nothing. The type checker accepts all of them.
- **F25.** The ingredients are a nested function that captures a local, a second nested
  function that calls the first, and the first passed as a value to `map`. Two more programs
  of the batches crash without passes in the same family (a nested function that reads a
  captured `var`, passed to `map` in one arm and called in the other).
- **F18** against principles 2.9 ("a constant expression that must fault is left for run time;
  the program behaves the same with and without optimization"): without passes the function is
  never called, so nothing faults. The index is not a literal one; the reducer rejects
  candidates with a literal index outside a literal array (2.7 makes those compile-time errors).
- **F17, F36.** The graph and the bytecode `std::inline` leaves are right: `std::inline std::fvm`
  runs the same bytecode and prints the expected lines, and so do the node VM and the parallel
  scheduler. Inlining the arms into the function makes it call only itself, which is the
  condition for the JIT to compile it (`std::jit` alone reports "targets non-self graph" for it
  and interprets). The MIR of one activation (`std::mir`) is right as well; the self tail call
  is not native on Linux but goes through `trampolineTail`
  (`src/passes/sched/fastvm/jit/runtime/trampoline.cpp`, the JIT-to-JIT path: release the
  caller's frame, acquire one, `invokeOwnedJitFrame`), reached from `case OpCode::TAIL` of
  `src/passes/sched/fastvm/jit/backend/x64/x64_backend.cpp`. In F36 the first call of
  the function is right and every later one is wrong (each `i * j` is added twice), which fits a
  function that is interpreted first and compiled later; that, and what goes wrong on the
  compiled path, was not established.
- **F01, F03, F18** are one symptom (code of an arm that is not taken runs) and **F04-F10**
  another (an effect that must run is dropped). Hand-simplifying them usually loses the failure:
  each needs its particular shape (F01 passes when either the `var` declaration or the tuple
  result of the callee is removed).

### Root causes

Read from the graphs (`std::rgir` after each step of the smallest failing sequence) and the code.
`std::opt::simplify` is rounds of `std::opt` (specialize, devirtualize, inline), fold and dce; it
does not run cse.

- **C1: an arm is looked for from its head only (F06, F07, F08, F11, F13, F23). Fixed in
  `741b8705`** (regression tests: plan `feat/opt/progen_simplify`). The
  schedulers run a taken arm as the range from its entry to its tail, and
  `resolveRuntimeBranchArmEntry` (`src/execute/graph_runtime_support.cpp`) found the entry among
  what follows the arm's recorded head. After inlining, an arm's nodes wait on the BRCH directly,
  and the head can become a node nothing follows: dce removes the unused pure node after it (F11,
  F23: `std::inline std::opt::dce`), or fold prunes the inner `if false` that held its only user
  (F07: `std::inline std::opt::fold`). The arm was then taken to be empty: its effects did not run
  and the JOIN read a slot never written (null; F23 crashes in `len(null)`). The rewritten graph
  was right; all four schedulers misread it. The entry lookup now also takes what the tail depends
  on inside the branch, which is how the execution order already lays an arm out.
- **C2: an inlined body is not ordered after what its call was ordered after (F12, F19). Not
  fixed: the code is the inliner's** (`collectExecutableEntryRoots` and the entry targets in
  `src/runtime/draft_inline.cpp`). The entry roots of a callee are its executable nodes whose
  inputs are all DATA or PORT. A node that reads a constant through a GATE is not one: fold's
  pruning and specialization leave `GATE(constant)` in front of the first node of a body (a gate
  that waited on a parameter which is now a constant), and inside one `std::opt` session such
  gates are not dissolved yet. With no root, the call's control predecessors go to the gates of
  the parameters, and a body that reads no parameter gets none of them.
  - F12: the `println` of the inlined `w14` is not ordered after the `reduce` before the call
    (second round: specialize, then inline, in one session).
  - F19: the body inlined at the tail of an arm (a nested branch) does not wait on that arm, so
    it lies outside the branch; the arm's tail is then laid out before the arm's entry, NodeVM and
    Taskflow never reach the end of the arm and run into the other arm, which is the recursive one
    (`std::opt std::opt::fold std::opt` is the smallest failing sequence).
  Tried, not committed: in both overloads of `collectExecutableEntryRoots`, an input that is
  not DATA or PORT disqualifies a node only if an executable node is reached from it through
  GATE / SYNC nodes alone:

  ```cpp
  if (inputNode->kind != GCNodeKind::Data && inputNode->kind != GCNodeKind::Port) {
      std::vector<gc_node_ref_t> work{in};          // was: hasNonDataPortInput = true; break;
      std::unordered_set<gc_node_ref_t> seen;
      while (!work.empty() && !hasNonDataPortInput) {
          const gc_node_ref_t c = work.back();
          work.pop_back();
          const auto *node = seen.insert(c).second ? sourceGraph->node(c) : nullptr;
          if (!node) continue;
          if (isExecutableEntryKind(node->kind)) hasNonDataPortInput = true;
          else for (auto inputs : {sourceGraph->normInputsOf(c), sourceGraph->withInputsOf(c),
                                   sourceGraph->ctrlInputsOf(c)})
              work.insert(work.end(), inputs.begin(), inputs.end());
      }
      if (hasNonDataPortInput) break;
  }
  ```

  With it F12 and F19 pass on all four schedulers, and over seeds 1-1000 (20 cells per program:
  no pass, fold, dce, and 17 with `std::inline`, `std::opt` or `std::opt::simplify`) 19,988 of
  20,000 cells agree, 6 are located refusals, and one program is left with a bad cell: seed 299,
  a silent mismatch under `std::specialize std::inline` (not reduced). After `741b8705` alone
  34 programs have a bad cell (44 before it), so C2 is also behind most of what was filed under
  `std::inline`. The default test set was not run with the change.
- **C3: asking whether a value is written allocates in the space that is never freed (F34). Not
  fixed** (the capacity of that space and its diagnostic are handled elsewhere). `isVarArgument`
  (`src/runtime/draft_writes.cpp`) resolves the overload of an effectful operator through
  `operFuncType` on every call, and the resolver builds a function type in the permanent space
  each time. `isWrittenInPlace` asks it for every user of a value; the specialization probe of
  the rewrite engine (`tryResolveStaticValue`) asks that for every argument of every call, again
  after each inlined call. On seed 3293 the resolver ran 171,810 times before the limit (1,658
  times in the run of `std::opt::simplify` alone, which passes); the graphs do not grow (79
  graphs, 121 inlined calls, within the growth budget). So neither unbounded growth of the
  program nor a program too large: the cost is per query. Fold and cse ask the same question for
  every node.

### Seen, not reduced

- **Results that change from run to run** after `std::inline`, `std::opt::simplify` or
  `std::opt`: 100 bad cells of batch A, on every scheduler (see above). Under `std::prl` the
  lines of a sync body come out in another order on repeated runs (batch A seeds 244, 514, 752,
  960, 1049). Reducing seed 244 led to F01 instead; the cells on the node VM were not looked at.
- **A printed value that looks like an address** (batch A seed 716, `std::opt` on `std::fvm` and
  `std::jit`: `L10 106794191113924` and `L10 98920901664452` where `L1 5 5 5 pq` is expected).
  Reducing it led to another instance of F05.
- **Failures that depend on the path of the source file.** Two reduced programs (a crash after
  `std::inline std::opt::dce`, and `MaxRecursionDepthExceeded` after `std::inline` in a recursion
  bounded by an environment value) failed on every run in the directory of the batch and on none
  of 10 runs after being copied elsewhere. They are not archived because they do not fail here.
- **`Permanent space exhausted (32 MB of types and layouts)`** from `std::opt::simplify` in the
  frozen pipeline R22 on a 113-line program (batch F seed 3293), reported as
  `RuntimeDiag::PassInternalError` without a position. Since reduced to 17 lines and archived as
  F34 (cause C3): it needs neither `std::opt::simplify` nor a large program.
- **An effect lost under `std::inline`** in a program without `var`, captures or effectful
  functions (batch C seed 2508: a `println` of `main` before a `match` with a call in an arm
  that is not taken), and **`null` in place of a string** built in a function inlined into an
  `else` arm (batch C seed 2504). Same symptoms as F04 and F14.
- The remaining failing programs of the batches were grouped by which pipelines fail
  (`triage.py --patterns`: 56 groups in batch A) but only one program of the larger groups was
  reduced. The 189 failing programs are therefore not known to be covered by the 33 findings.

To look at a seed: `python test/tools/progen/progen.py show --seed 716` prints the program and
its reference output (batches B, C and D need their `--features`, as in the table above).

## Checked against the design principles

Each finding was compared with `docs/design-principles.cn.md` so that expected behaviour is not
reported as a defect:

- No finding relies on an effect outside a sync block (2.1, 2.4, misreadings 1 and 3): every
  `println`, write and effectful call of the programs is a statement of a sync block, and every
  function that prints has a sync body. The lost effects of F04-F10 are such statements.
- No finding compares two pipelines (section 4, misreading 9). Each run is compared with the
  one output the principles allow for the program; the claim is "a standard pass must not make
  the program print something else" (section 4, last-but-two bullet).
- No finding has an effect in a `map` or `filter` element function (2.7); `foreach` and `reduce`
  are ordered, which is what F12 depends on (the `reduce` is the first statement, the call the
  third).
- No finding uses `wait` outside a sync block, a module-level variable, or two writers of one
  variable without order (section 6, undecided).
- Macro: no finding is a refusal by `std::macro`; those are counted as legal (but see J1).
- The reads of variables in the programs are after-the-fact reads in the same sync body (F11),
  which 2.5 and 2.6 order by position.

## For the owner to decide

**J1. Should `std::macro` refuse a `macro` function that uses `map`, `filter` or `reduce`?**
`judgement/macro_builtin_refused.cml`: prints 6 without passes; with `std::macro` it is rejected
at the call (`MacroEvaluationFailed`: "Operator ':mark/reduce_arr' is unavailable in macro
execution"). 87 cells of the batches are this refusal; the generator now avoids declaring such
functions `macro`.
- Reading A: it is the ruling of 2.7 ("a failed macro evaluation is a compile-time error"), and
  the diagnostic is located, so this is a legal refusal.
- Reading B: the evaluation did not fail because of the program (as a division by zero would);
  the evaluator lacks an operator. Section 3 says a macro function *may* be evaluated ahead of
  time and is an ordinary function otherwise, and `std::macro` already leaves a call for run
  time when an argument is not constant. By that reading it should leave this call too, and
  rejecting a correct program is a defect.

**J2. Is a bare arm of an `if` that is a statement of a sync block inside that sync block?**
`judgement/bare_arm_var_call_warning.cml`: `let r = if a > 2 then wr(c, 5) else 0` in `main`'s
sync body, where `wr` writes `c` and nothing reads `r` or `c` afterwards. The compiler warns
that the call "is neither waited nor in a sync block ... the write runs only if the variable is
read afterwards", and then the call runs (prints `wr`) under every pipeline tried.
- Reading A (the warning): an arm is a sub-graph of its own, as a function is (2.4), so the
  enclosing sync block does not order what is inside; the call should not run, and the
  behaviour is the defect.
- Reading B (the behaviour): the `if` is one of the expressions of the sync block (2.2), the
  taken arm is its value, so it runs in order; the warning is wrong.
The generator writes every effectful arm as `sync { ... }`, so no finding depends on this.

**J3. Should `string(1.5)` be `1.5` or `1.500000`?** `judgement/string_of_float_format.cml`:
`println(1.5)` and `'{}'->format<1.5>` write `1.5`, `string(1.5)` gives `1.500000`.
- Reading A: `string` is the C conversion with six decimals by design.
- Reading B: a value has one default text, and `string` should give what `println` prints.
The generator does not use `string` on floats.
