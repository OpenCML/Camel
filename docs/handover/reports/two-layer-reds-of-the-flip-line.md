# The flip line's reds, in both layers, from `tip=c5c506e44` to `tip=924bc6070`

Readings only; no expectation, golden, tier or `known_fail` was changed to produce this page. The
fast layer is measured on this line; the slow layer was measured by the slow-layer measurement lane
at `tip=c5c506e44` and every entry of that account re-run here by plan. Section 3 records the one fix
this line made, at `tip=92984364f`, and section 5 the account's readings and the endpoint.

Three lanes landed on this line after its rank-2 constant rule was revived (`ed6b7fd08`,
`1763def51`): the write-value lane, the declaration-evaluator lane and the type-identity lane. Both
of the fast layer's reds are green after the first two, and the endpoint is `fail 0`. Section 6
records those three landings, each rehearsed in `h465-rehearsal` before it was merged here, and the
readings this page closes on.

## 1. Fast layer -- `fail 9` at `c5c506e44`, `fail 6` at `26bd1c6cc`, `fail 2` at `b4b7f960e`

`-Plan smoke,test/plans/feat -Tier fast -Jobs 8` reads `total 6218, pass 6190, fail 9, skip 11,
known failures: 8` at `tip=c5c506e44`, the same summary with the same nine names at `tip=92984364f`
(after the C++ fix), `total 6219, pass 6194, fail 6, skip 11, known failures: 8` at
`tip=26bd1c6cc` (after `origin/develop` `43a5c8066` and `handover/h499-type-identity` `26bd1c6cc`),
and `total 6219, pass 6198, fail 2, skip 11, known failures: 8` at `tip=b4b7f960e` (after the
bare-`Tensor` lane `handover/h506b-bare-tensor` `39784865d`). The total grew by one because develop
adds one test; `skip 11` and `known failures: 8` never moved. The six names of the middle reading and
their fates:

| # | test | kind | state |
|---|---|---|---|
| 1-4 | `lrit`, `nn.lrit`, `mnist`, `nn.mnist` | bare `Tensor` / unstated element type into `=` | **green at `b4b7f960e`**: the lane typed the gradient node by its value's tangent, and the cases state their element type. Its own whole-plan re-runs total `278/278` over the four plans that hold them (`feat/modules/nn`, `feat/opt/screens`, `feat/modules/tensor`, `feat/mm/gc_pacing`) |
| 5 | `declaration_evaluator` | **not this family**: the test-only pass `tensor::test_index_eval` over `test/cases/modules/tensor/index_semantics.cml` finds 43 places where the declared facts and the CPU kernels disagree (15 mixed widths, 4 reshapes of unknown extent, 8 reductions of an empty extent). The program holds no `grad` or `vjp`, and every `Tensor` in it states its element type | **green at `tip=924bc6070`**: `handover/h523-decl-eval` (`8146a407e`) answered the three families, section 6 |
| 6 | `print_and_recompile` | the value path of a `var` parameter | **green at `tip=c81fd895a`**: `handover/h515-write-value` (`531c0782f`) landed its other half, section 6 |

The two reds at `tip=b4b7f960e`, with their timings: `declaration_evaluator` `752.14 ms` and
`print_and_recompile` `5.43 s`. The same two names are the two the three landings of section 6 took
green, and they were this line's only reds at every reading in between: `print_and_recompile`
`3.84 s` and `declaration_evaluator` `408.94 ms` at `tip=ea230f2ed`, the tip the three landings
started from.

The three entries that were red only as stale records are green at `tip=26bd1c6cc`, measured in the
same gate: `specialize_growing_shape.fold_specialize.bounded` `144.94 ms` (its plan now asserts
`stats.specializations === 1` and `stats.graphs === 5`), `profile.alloc_step` `222.71 ms`, and
`fusion_group.profile.none` `387.14 ms` (its two recordings say they were recorded from this tree
and carry no commit id).

Two of the previous tip's reds left without a new one: `export_fold` (10.03 s timeout with empty
stdout before, `PASS 343.89 ms` here, its onnx model written and parsed by the plan's own verify
script) and `rules.tensor_saturation_keeps_values` (`RewriteInvalid` before, `PASS 392.03 ms` here).

## 2. Slow layer -- 11 at `c5c506e44`, 10 with the C++ fix, 1 of the account left at `0909710c9`, 0 at `924bc6070`

Whole-tree slow measurement at `tip=c5c506e44` (measurement lane), plus every entry of that account
re-run here by plan (a plan file runs in full whatever the tier says), twice each for the three
entries that only this line had. The account has shrunk three times: at `tip=511711e49` the three
`poison:modules.rules` entries and the `gpu/tiles_transfer` entry are green; at `tip=b4b7f960e` the
two `gc_pacing` entries and the two `nn` entries are green; and at `tip=0909710c9`, after merging
develop to `72d148822`, the debugger entry is green too.

| # | test | state at `924bc6070` | evidence |
|---|---|---|---|
| 1 | `debugger.dap_protocol` | **green** | `test/plans/feat/tools/debugger.plan.toml` reads `14/14` (`2.30 s`); develop `72d148822` fixed it (`1.60-1.77 s`) and this line took that merge |
| 2-4 | `poison:modules.rules:rules.library_verdicts_are_exactly_these`, `…reduced_element_type_instances_are_checked`, `…every_exported_rule_enters_the_check` | **green** | `test/plans/feat/modules/rules.plan.toml` reads `140/140` (`845.26 ms`, `514.26 ms`, `1.55 s`) |
| 5 | `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update` | **green** | `test/plans/gpu/tiles_transfer.plan.toml` reads `3/3` (`2.96 s`) |
| 6-7 | `poison:…nn:lrit`, `poison:…nn:mnist` | **green** | the plans that hold them, `test/plans/feat/modules/nn.plan.toml` and `test/plans/feat/opt/screens.plan.toml`, read `68/68` (`lrit` `280.95 ms`, `mnist` `324.69 ms`) and `127/127` (`nn.lrit` `2.12 s`, `nn.mnist` `3.77 s`) |
| 8 | `poison:…index_semantics:declaration_evaluator` | **green** | `handover/h523-decl-eval` (`8146a407e`) fixed the 43 disagreements (row 5 of section 1); `test/plans/feat/modules/index_semantics.plan.toml` reads `5/5` with `declaration_evaluator` `502.26 ms`, section 6 |
| 9 | `feat/trans/cpp :: agree_with_fvm` | **green** | `test/plans/feat/trans/cpp.plan.toml` reads `11/11` (`126.79 s` on this run, `82.82 s` and `73.72 s` at `tip=92984364f`); **only this line** had it, fixed here, section 3 |
| 10-11 | `feat/mm/gc_pacing :: gc_pacing.training_400`, `.training_800` | **green** | `test/plans/feat/mm/gc_pacing.plan.toml` reads `15/15` (`2.30 s`, `4.18 s`); **only this line** had them |

The whole account re-measured at `tip=924bc6070` after the three landings, plan by plan and in one
run (`-Jobs 8`): **`[SUMMARY] total: 383, pass: 383, fail: 0`** over the eight plan files that hold
the eleven entries (`debugger` `14`, `rules` `140`, `tiles_transfer` `3`, `nn` `68`, `screens` `127`,
`index_semantics` `5`, `cpp` `11`, `gc_pacing` `15`). The whole-tree slow selection itself was not
run -- that is the measurement lane's, and the poison layer is sampled -- so what is measured here is
the eleven-entry account and nothing else.

So the eleven-entry account read **1 red** at `0909710c9` and reads **0 red** at `924bc6070`; the one
that left is entry 8, the `declaration evaluator` question, answered by its own lane (section 6). The
account was 11 entries because the flip integration changed the element type of every unstated
tensor; each one has been answered, either by this line (the C++ cell marking, the family's gradient
typing) or by develop (the rules family, the debugger's protocol, the tiles fixture), and entries 5
and 8 were never that family at all -- they are two separate open questions, each with its own page.

Two slow entries went green before this at `c5c506e44` without a new one: `feat/modules/onnx ::
pass_export_equals_export_model` (two 120 s liveness kills and a cpu reading of 0 before;
`PASS 25.50 s wall / 46.94 s cpu` there) and `poison:…rules.rules.tensor_saturation_keeps_values`
(`PASS 313.05 ms`).

### The one merge this line refused, and the rehearsal that is now the rule

`handover/h506-bare-tensor` (`887856c6b`, the tip at the time) was merged and **not landed**: the
whole fast gate read `6219 / 5951 / fail 249 / skip 11 / known 8` against `fail 6` before it, with
the gradient programs running but their expected values no longer printed (for example
`grad_library`: "the program prints the gradient difference and the licence bound" fails). The cause
is in the merge itself, not in the merge's targets: both this line and develop changed
`modules/tensor/ops/vjp.cpp` and `…/vjp.h` (develop's and this line's versions differ by 128 and 49
inserted lines), the lane's develop merge took develop's side (`a41b28e8d`'s second parent is
`bbac1ff46`, which does not contain `5b629ad4d`), and merging that branch into this line blended the
two implementations into a third one -- develop `09360763`, this line `f67fea29`, the lane
`53619ff9`, the blend `47cbecf9`. The lane's work needs to sit on top of this line's `vjp.cpp`
rather than beside it, so its branch has to start from a tip of this line and not from a develop
merge of its own. The merge commit was `579d5ad57`, kept as the record of an untested blend, and this
line is back at `511711e49`.

A later `git merge-tree --write-tree HEAD handover/h506-bare-tensor` on the lane's newer tip
(`41be74f4e`) writes a tree whose `vjp.cpp` is the lane's `53619ff9` taken whole rather than a blend
-- the merge base moved, so the same two branches resolve differently from one day to the next. That
is the reason for the rule now in force here: **no lane is merged into this line before a rehearsal**
-- `git merge-tree --write-tree` first, then the branch merged into a throwaway worktree and the
whole fast gate run there. Only if the rehearsal's `fail` and its red names are no more than this
line's own does the merge touch this line, and the rehearsal's reading goes in the report. The one
exception is a tip already known to be bad, which is not merged at all.

The rule was exercised for real on the lane's second delivery: `handover/h506b-bare-tensor`
(`39784865d`) starts from this line at `3892afcf4` and carries no develop merge of its own, its
`git merge-tree --write-tree` against this line is conflict-free, and the same merge rehearsed in a
throwaway worktree ran the whole fast gate at `6219 / 6198 / fail 2 / skip 11 / known failures: 8`
with `declaration_evaluator` and `print_and_recompile` as the only reds -- four names fewer than this
line's six and none added, so it was merged, and this line's tree after the merge is the rehearsal's
tree (`d01288e3`).

It was exercised again on develop, which is the harder case because it conflicts: `origin/develop`
`72d148822` (804 files, +2617/-2310) rehearsed to the same `6219 / 6198 / fail 2 / skip 11 /
known failures: 8` with the same two reds, four conflicts resolved by content, and this line's tree
after that merge is again the rehearsal's tree (`13123955`). The four conflicts were: a question page
both sides added, taken from develop as the superset; three cases where this line had the substance
(the `load_npy` element type, the Loop-carried array rationale) and develop's side was a citation
cleanup -- this line's text kept, the cleanup applied on top, so no task-number citation is left in
those files. The merge lands develop's answers retiring four pages this line was carrying, and brings
`tags-struct-argument-record.md` in their place.

The rehearsal worktree (`h465-rehearsal`, its own branch and its own build on the host) stays in
place for the next one.

Two habits follow from the same episode: after any amend, reset or re-merge on this line, the
helper's staging branch (`handover/h465-flip-integration` in `camel-work/Camel.git` on the host) has
to be deleted before the next gate run, or it refuses the push as non-fast-forward while it still
points at the old tip; and a refused merge is reported with the readings that refused it, not
reworked into this line.

## 3. The three that only this line has

### `feat/trans/cpp :: agree_with_fvm` -- a real defect in the C++ backend, fixed here

Not the tensor alias flip's: the affected programs hold no tensor at all, they are the `int[]`
rebinding cases. `test/plans/feat/trans/cpp.plan.toml:31` translates every program in
`test/tools/cppexec/expect.txt` with `std::cpp`, builds it and compares its output with `std::fvm`'s.
Two consecutive runs before the fix, `82.82 s` and `73.72 s`:

```
115   97.5%  same
  3    2.5%  does not build
  test/cases/consistency/new_values/peek.cml: does not build: …:87:45: error: no member named 'get'
      in 'camel::standalone::Array<long>'
  … peek_loop.cml:86:50, … rebinding_loop.cml:297:54: the same message
```

The generated program, verbatim, from `camel test/cases/consistency/new_values/peek.cml std::cpp`:

```cpp
    rt::Array<int64_t> var1 = lit::array;
    const int64_t item2 = 1;
    var1 = rt::append(std::move(var1), item2);
    const rt::Array<int64_t> v3_read = var1.get();
```

`var1` is declared as a plain local (no `rt::Cell<…>` in the file) and the read of it is emitted as
`.get()`, which `camel::standalone::Array<long>` does not have. The three interpreters
(`std::nvm`, `std::fvm`, `std::jit`) print the expected output for the same case, so only the C++
backend was wrong. The same command on develop (`tip=0a1a509f5`) emits `const rt::Array<int64_t>
v3_read = var1;` -- the whole program differs by that suffix -- and a probe of every built tree on
the host shows the same split: everything derived from develop emits **0** `.get()`, everything
derived from this integration line emits **13**.

What produced it: `src/passes/trans/cpp/function_emitter.cpp`. A `var`'s cell marking is `Local` for
a non-escaping variable (a plain `T name = init;`) and `Shared` for an escaping one (an
`rt::Cell<T>`), and a read appends `.get()` for anything that is not `Local` (`:1654-1656`, and
`:1618-1624` for `:var/take`). The in-place form's nodes dropped that marking: `:op/assn_owned`
stored the append node's **text** without its cell (`…valueOf(frame, norm[1]).text, false`), and the
append node itself stored the dereferenced text for a shared variable. So an escaping variable's
read dereferenced twice (`out.get().get()`), and a non-escaping variable's read, whose declaration
was a plain local, dereferenced once. The fix makes the marking flow through the form: `:var/take`,
`:struct/append_arr_in_place` and the `:op/assn_owned` that stores it each keep the variable's cell
and its marking, exactly as a write leaves the variable.

Readings after the fix, at `tip=92984364f` (build and plan on the host, same targets as above):

- `test/plans/feat/trans/cpp.plan.toml`: **`[SUMMARY] total: 11, pass: 11, fail: 0`** (was 10/1).
- The three programs' generated C++ **compiles** with the tool's own command
  (`clang++ -std=c++20 -O2 -ffp-contract=off -w -I include <file>`) and each one's output is
  **byte-identical to all three interpreters** (`cmp` clean against `std::nvm`, `std::fvm` and
  `std::jit`).
- The probe above: `peek.cml` on this line is back to **0** `.get()`, the same count develop has.
- Full fast gate `smoke,test/plans/feat -Tier fast -Jobs 8`: `6218 / 6190 / fail 9 / skip 11 /
  known 8`, and the nine names are **exactly** the nine from before the fix, so the fix moved
  nothing else.

The change arrived with the var-version machinery of the in-place planning merge on this line
(`42d5ef8c0`, commit `0b1be856d8`, which touched `builder_expr.cpp`, `builder_operators.cpp`,
`ops.cpp` and 13 lines of the emitter), and that commit is not in `origin/develop`: this is this
line's account, not the flip's.

### `feat/mm/gc_pacing :: gc_pacing.training_400` / `.training_800` -- the bare-`Tensor` family, fixed by the lane

A compile-time refusal, not a moving number: `gc_training_peak.cml:29` calls `sgd(wf, g, 0.5)` with
`(f32[?, 4], Tensor, float)`, and `nn:sgd` (its signature is printed from `modules/nn/optim.cpp:567`)
binds both `P` and `grad of P` to the first argument's type, so the bare `Tensor` of the gradient
does not unify with `f32[?, 4]`. Two runs each (`188.00 ms`, `129.56 ms` and `180.22 ms`,
`209.13 ms`); the same program with `steps=2 batch=64 width=64` prints its result line on develop
(`tip=0a1a509f5`: `gc_training_peak base=18056 peak=28808 low=22664 released=0
loss=0.6931471805599647`) and was refused here. Recorded in
`docs/handover/questions/var-cell-widening-and-the-bare-tensor-return.md`, section 5.

Both entries are green at `tip=b4b7f960e`, with the rest of that family: the lane typed the gradient
node by its value's tangent, the case states its element type, and
`test/plans/feat/mm/gc_pacing.plan.toml` reads `15/15`. Together with the two plans holding the
teaching examples and the `tensor` plan, the lane's four re-runs total `278/278`.

Neither of the two remaining is host jitter (both red in two consecutive runs, with the failure text
of a compile, not of a measurement) and neither is a stale expectation (the expectations are `PASS`
strings and numeric bounds that no run reached).

## 4. Acceptance, and what this page did not do

The landing account stays "only fewer, never renamed, same targets": the fast layer went `fail 9`
(`c5c506e44`) -> `fail 9` with the same names (the C++ fix) -> `fail 6` (`26bd1c6cc`: `origin/develop`
`43a5c8066` and `handover/h499-type-identity` `26bd1c6cc`) -> `fail 2` (`b4b7f960e`: the bare-`Tensor`
lane `39784865d`) -> `fail 2` with the same two names (`0909710c9`: `origin/develop` `72d148822`) ->
`fail 2` at `ea230f2ed` (the rank-2 revival; the names did not move) -> `fail 1` (`c81fd895a`: the
write-value lane) -> `fail 0` (`29264b5e4`: the declaration-evaluator lane), where the type-identity
lane's landing (`924bc6070`) left it, and no reading ever gained a name. `skip 11` and
`known failures: 8` never moved, and the test total grew by one because develop adds a test and by
eight because the rank-2 revival adds eight. Nothing was weakened, no `known_fail` was added or
removed, no `tier` was moved, and `perf/**` and `feat/opt/progen` were not run. The expectations the
other lanes landed -- the exact specialization count, the two re-recorded profiles, the
`profile_alloc.py` prefix, the typed cases, the gradient tangent, the write-valued body's golden and
the four expectations the write-value lane re-recorded for the new graph shape -- were merged as they
are and not touched here.

## 5. The account's readings, and the endpoint

| layer | before this batch | at `0909710c9` (measured) | the endpoint, reached |
|---|---|---|---|
| fast | `fail 9` (`c5c506e44`: 6218/6190) | `fail 2` (6219/6198): `declaration_evaluator` `365.94 ms`, `print_and_recompile` `5.48 s` | **`fail 0`** (6227/6208/`skip 11`/`known 8`) at `tip=924bc6070`: `print_and_recompile` green with the write-value lane, `declaration_evaluator` green with the declaration-evaluator lane, section 6 |
| slow | 11 (`c5c506e44`) | 1 of the eleven-entry account: `poison:…index_semantics:declaration_evaluator`, waiting on the ruling of `docs/handover/questions/declaration-evaluator-facts-vs-kernels.md` | **`0`** -- the eleven entries re-measured plan by plan at `tip=924bc6070` read `383/383`, entry 8 green with the declaration-evaluator lane; nothing else of the account is red |

Both of the fast layer's reds were open questions with a page, not defects of the flip: one asked
what the declaration evaluator's facts should say where they meet the kernels, the other was the
value path of a `var` parameter. Both are answered and green, and the third lane landed without
adding one. Everything the flip integration itself introduced -- the crashes, the print form, the
element type, the `var` cell, the type identity, the C++ cell marking, the stale specialization and
profile records -- is green.

Two caveats on the table. The slow layer is a whole-tree selection and the measurement lane owns its
full run; what is measured here is every entry of the eleven-entry account, plan by plan. And the
`print_and_recompile` lane did land here, at the coordinator's word after it had been refused once
without one (section 3 records that refusal); its branch was merged and its reading taken in the
same two-tree rehearsal every other landing of section 6 had.

## 6. The three landings, each rehearsed, and the endpoint `fail 0`

Every landing was rehearsed first on a line whose tree is this one's, `h465-rehearsal`
(`D:\Projects\Camel-wt\h465-rehearsal`), at the tip this line stood on, and only a reading with
fewer reds and no new name was allowed to touch this line. In both trees the whole fast layer is
`smoke,test/plans/feat -Tier fast -Jobs 8`; a plan file named as a target runs in full whatever its
tier says, which is how the slow entries below were re-measured.

| landing | its own commit | rehearsal | this line after the merge |
|---|---|---|---|
| `handover/h515-write-value` | `531c0782f`, one commit on `1ee7acab6` | `total 6227, pass 6207, fail 1, skip 11, known failures: 8`; the one red is `declaration_evaluator` `585.68 ms` | `6227 / 6207 / fail 1 / 11 / 8`, `declaration_evaluator` `329.43 ms`; `print_and_recompile` green |
| `handover/h523-decl-eval` | `8146a407e`, one commit on `776240961` | `6227 / 6208 / fail 0 / 11 / 8` | `6227 / 6208 / fail 0 / 11 / 8` at `29264b5e4` (`RUN_EXIT=0`) |
| `handover/h522-type-identity-2` | `22769a406`, one commit on `3d30c2c60` | `6227 / 6208 / fail 0 / 11 / 8` | `6227 / 6208 / fail 0 / 11 / 8` at `924bc6070` (`RUN_EXIT=0`) |

The write-value lane lands the two author rulings of 2026-10-24: a compound assignment `a op= b` is
`a = a op b`, so the value the assignment hands on is the plain operator's node and the place is read
through `:var/peek`; and a tail `return e` and a body whose last statement is `e` build one graph,
because the exit asks one question (`valueNeedsSyncEdge`) instead of demanding a control edge and a
GATE from every value. Its own commit names the four re-recorded expectations the new shape needs
(the `var` corpus's golden, the effect trace's `:op/assn_l`, the closure footprint's `result fresh`,
`jit_framed`'s `ticks` round) and adds `test/cases/trans/cml/write_value.cml` to the round-trip
corpus; none of them loosens a pin.

The declaration-evaluator lane answers the 43 disagreements between the declared facts and the CPU
kernels one family at a time -- a rank-0 array's float literal reported as one type in `Arg::type()`
and `Arg::slot()`; `reshapeIndexing` asking the rank the facts state, with an open first extent taking
the instance the counts give when the target is written out; and a rank-0 result reaching the
declaration like any other, so an empty axis is compared as the fault both sides raise -- and settles
the width of `divide` and `pow` from `operandType` rather than from the `scalar` flag a rank-0 array
shares with a value. The lane's own reading is `EVAL total operators=33 cases=2780 agree=2780
faults=211 points=53904 unsound=0 mismatches=0 undeclared=0`, and `declaration_evaluator` `502.26 ms`
green in the plan that holds it. `docs/handover/questions/declaration-evaluator-facts-vs-kernels.md`
stays where it is: it records what was asked, and the answer is the lane's commit.

The type-identity lane reads a type's identity off the table's canonical id wherever a graph decides
it -- the node key of the writings, the specialization binding's instance parameter in its
comparison, hash and order, and the node content hash -- and adds
`docs/handover/questions/canonical-node-key-keeps-one-spelling.md` for the one thing it did not
decide: which spelling a merged writing keeps. It leaves `OperQueryKey` in
`src/runtime/draft_writes.cpp` on the type address, because that key memoises a recomputation rather
than deciding an identity; this line merged that exactly as it stands and decided nothing for it.

What the merges did to the files both sides had touched, checked after each merge by comparing every
path of the merged tree with both sides: the write-value merge combined four files
(`src/compile/gir/builder.cpp`, `builder_control.cpp`, `builder_helpers.h`, `builder_operators.cpp`)
in different regions, and the merged content of each is this line's plus that lane's hunks -- no file
was left as a third version, and nothing under `modules/tensor/` was changed by it at all. The
declaration-evaluator lane's three tensor files (`kernels/elementwise.cpp`, `ops/indexing.cpp`,
`passes/index_eval.cpp`) were untouched here since its merge base, so the merge took each whole; the
type-identity lane's eight files were untouched here too. Nothing was rebased, `origin/develop` was
not merged into this line, and the two lanes whose expectations moved are the ones whose commits say
why.

Readings this page closes on, at `tip=924bc6070`:

- fast: `total 6227, pass 6208, fail 0, skip 11, known failures: 8`.
- slow: the eleven-entry account, re-measured plan by plan, `383/383`.
- the five checkers, in the same fast layer at this tip: `catalog.check` (`node
  scripts/diagnostics-catalog.mjs --check`) `353.45 ms`, `docs.ai_reference.current` (`node
  scripts/camel-for-ai.mjs --check`) `262.78 ms`, `docs.links.citations` (`node
  scripts/check-doc-links.mjs --citations`) `2.79 s`, `docs.headers.canonical` (`node
  scripts/headers.mjs --check`) `652.37 ms`, and `registry.signatures_match` (`node
  scripts/stdlib-reference.mjs --verify-ops --camel …`) `941.20 ms` -- all five `PASS`.

This page's own commit changes documentation only, so it moves no reading above; the whole fast layer
was measured again at that commit to say so, with the same `fail 0` and the same five names passing.