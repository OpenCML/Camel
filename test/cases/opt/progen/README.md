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
was written (2026-10-11, branch `lane/gen`, Linux container, release build). None is in the
default test set; the test plan `feat/opt/progen` runs only seeds that pass.

**Nothing here was fixed, and the compiler code was not read to confirm a cause** (except
where a source file is named). The "class" column is a judgement from the symptom and the
reduced program, using the five classes of the roadmap (effect order lost; a written variable
taken for one value; branch arms; macro operands inlined early; unbounded growth) and
"(other)" where none fits. Two findings of one class may have one cause, and one reduced
program may hide a second defect.

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

| id | program | pipeline | schedulers | expected | actual | class |
|---|---|---|---|---|---|---|
| F01 | `inline_untaken_arm_effect.cml` | `std::inline` | all | nothing | `a` (a call in the arm of `if false` runs) | branch arm |
| F02 | `inline_wrong_arm_runs.cml` | `std::inline` | nvm, prl | nothing | an empty line (the `else` arm runs although the test is true) | branch arm |
| F03 | `opt_untaken_arm_effect.cml` | `std::opt` | all | nothing | `L2 0 0 0 0` (a lambda in the arm of `if false` runs) | branch arm |
| F04 | `inline_statement_effect_lost.cml` | `std::inline` | all | `pq` | nothing (a `println` statement of `main` is dropped) | effect order lost |
| F05 | `opt_statement_effect_lost.cml` | `std::opt` | all | an empty line | nothing (the first `println` of `main` is dropped) | effect order lost |
| F06 | `opt_lambda_effect_lost.cml` | `std::opt` | all | `false` | nothing (effect of a call with unused result in a sync lambda) | effect order lost |
| F07 | `simplify_nested_function_effect_lost.cml` | `std::opt::simplify` | all | an empty line | nothing (`println` of a nested sync function) | effect order lost |
| F08 | `inline_dce_arm_effect_lost.cml` | `std::inline std::opt::dce` | all | an empty line | nothing (`println` of the taken arm) | branch arm |
| F09 | `inline_fvm_arm_effect_lost.cml` | `std::inline` | fvm, jit | an empty line | nothing (`println` of the taken arm; nvm and prl print it) | branch arm |
| F10 | `inline_unused_branch_effect_lost.cml` | `std::inline` | all | an empty line | nothing (`println` of the taken arm of an unused branch) | branch arm |
| F11 | `simplify_arm_write_lost.cml` | `std::opt::simplify` | all | `a` | an empty line (a write made in the taken arm is lost) | written variable taken for one value |
| F12 | `simplify_effect_order.cml` | `std::opt::simplify` | all | empty line, then `L1 0 0` | `L1 0 0`, then empty line | effect order lost |
| F13 | `inline_dce_arm_value.cml` | `std::inline std::opt::dce` | all | `1` | `0` | branch arm |
| F14 | `inline_branch_value_null.cml` | `std::inline` | nvm, fvm, jit | `L4 (0, [0]) 0` | `L4 null 0` | branch arm |
| F15 | `inline_match_value_null.cml` | `std::inline` | all | `L6 { n: 5, tag:  }` | `L6 null` | branch arm |
| F16 | `fold_inline_env_value.cml` | `std::opt::fold std::inline` | all | `L11 false 4` | `L11 false 0` (a value read from the environment) | (other) wrong value after inlining |
| F17 | `inline_jit_argument_value.cml` | `std::inline` | jit | `L8 0 0 24` | `L8 0 0 0` (nvm, fvm and prl print 24) | (other) wrong value after inlining |
| F32 | `fold_inline_match_arm_value.cml` | `std::opt::fold std::inline` | all | `L4 0 3 a` | `L4 0 0 a` (a pure function; no effect, no variable) | branch arm |

### Faults and crashes after a standard pass

| id | program | pipeline | schedulers | expected | actual | class |
|---|---|---|---|---|---|---|
| F18 | `inline_untaken_arm_fault.cml` | `std::inline` | all | nothing, exit 0 | `Array index out of bounds`, exit 1: the body of a call in the arm of `if false` runs | branch arm |
| F19 | `simplify_recursion_never_ends.cml` | `std::opt::simplify` | nvm, prl | nothing, exit 0 | `MaxRecursionDepthExceeded` in graph `__g_10__` (fvm and jit finish) | unbounded growth |
| F20 | `inline_crash_function_argument.cml` | `std::inline` | all | nothing | segmentation fault | (other) crash after a rewrite |
| F21 | `inline_crash_capturing_lambda.cml` | `std::inline` | all | nothing | segmentation fault | (other) crash after a rewrite |
| F22 | `specialize_inline_crash.cml` | `std::specialize std::inline` | all | an empty line | segmentation fault | (other) crash after a rewrite |
| F23 | `simplify_crash.cml` | `std::opt::simplify` | all | nothing | segmentation fault | (other) crash after a rewrite |

### Without any pass

| id | program | schedulers | expected | actual | class |
|---|---|---|---|---|---|
| F24 | `const_graph_capacity.cml` | all, and every pipeline | `a` | `std::bad_alloc`, no position, exit 1, before the first pass | unbounded growth (capacity) |
| F25 | `nested_function_value_crash.cml` | nvm, fvm, jit crash; prl prints something else | `[1]` | segmentation fault | (other) crash |
| F26 | `string_of_bool_crash.cml` | all | `true` | segmentation fault | (other) crash |
| F27 | `string_ordering.cml` | all | four lines `true` | four lines `false` | (other) operator not implemented |
| F28 | `string_plus_assign.cml` | all | `ab` | `a` | (other) operator not implemented |
| F29 | `mod_assign.cml` | all | `2` | located run-time error "assignment operator not fully implemented" | (other) operator not implemented |
| F30 | `tuple_comparison_parse.cml` | all | `(true, x1)` | syntax error "missing ')' at ','" | (other) parser |
| F31 | `return_named_function.cml` | all | `true` | "Compile graph 'f4' still contains a live DREF node", no position, exit 1 | (other) internal error |
| F33 | `let_named_function_captured.cml` | all | `[2]` | compile-time error "Function of type 'void' cannot be called" at the call in the lambda | (other) front end |

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
- **F27, F28, F29.** `src/builtin/executors/builtin.cpp` maps `op/lt_s`, `op/le_s`, `op/gt_s`,
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
- **F01, F03, F18** are one symptom (code of an arm that is not taken runs) and **F04-F10**
  another (an effect that must run is dropped). Hand-simplifying them usually loses the failure:
  each needs its particular shape (F01 passes when either the `var` declaration or the tuple
  result of the callee is removed).

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
  `RuntimeDiag::PassInternalError` without a position. `docs/cli.md` documents the limit; the
  size of the program that reaches it is the observation.
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
