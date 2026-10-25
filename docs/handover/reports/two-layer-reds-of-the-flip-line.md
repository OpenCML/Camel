# The flip line's reds, in both layers, from `tip=c5c506e44` to `tip=b4b7f960e`

Readings only; no expectation, golden, tier or `known_fail` was changed to produce this page. The
fast layer is measured on this line; the slow layer was measured by the slow-layer measurement lane
at `tip=c5c506e44` and every entry of that account re-run here by plan. Section 3 records the one fix
this line made, at `tip=92984364f`, and section 5 the account's readings and the endpoint.

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
| 5 | `declaration_evaluator` | **not this family**: the test-only pass `tensor::test_index_eval` over `test/cases/modules/tensor/index_semantics.cml` finds 43 places where the declared facts and the CPU kernels disagree (15 mixed widths, 4 reshapes of unknown extent, 8 reductions of an empty extent). The program holds no `grad` or `vjp`, and every `Tensor` in it states its element type | **red**, waiting on the ruling of `docs/handover/questions/declaration-evaluator-facts-vs-kernels.md` -- not a case to be quietly greened |
| 6 | `print_and_recompile` | the value path of a `var` parameter | **red**, the printer half landed; this half is another open ruling, worked by its own lane and not to be touched here |

The two reds at `tip=b4b7f960e`, with their timings: `declaration_evaluator` `752.14 ms` and
`print_and_recompile` `5.43 s`.

The three entries that were red only as stale records are green at `tip=26bd1c6cc`, measured in the
same gate: `specialize_growing_shape.fold_specialize.bounded` `144.94 ms` (its plan now asserts
`stats.specializations === 1` and `stats.graphs === 5`), `profile.alloc_step` `222.71 ms`, and
`fusion_group.profile.none` `387.14 ms` (its two recordings say they were recorded from this tree
and carry no commit id).

Two of the previous tip's reds left without a new one: `export_fold` (10.03 s timeout with empty
stdout before, `PASS 343.89 ms` here, its onnx model written and parsed by the plan's own verify
script) and `rules.tensor_saturation_keeps_values` (`RewriteInvalid` before, `PASS 392.03 ms` here).

## 2. Slow layer -- 11 at `c5c506e44`, 10 with the C++ fix, 2 of the account left at `b4b7f960e`

Whole-tree slow measurement at `tip=c5c506e44` (measurement lane), plus every entry of that account
re-run here by plan (a plan file runs in full whatever the tier says), twice each for the three
entries that only this line had. The account has since shrunk twice: at `tip=511711e49` the three
`poison:modules.rules` entries and the `gpu/tiles_transfer` entry are green, and at `tip=b4b7f960e`
the two `gc_pacing` entries and the two `nn` entries of the family are green as well.

| # | test | state | evidence |
|---|---|---|---|
| 1 | `debugger.dap_protocol` | **red on this line's tip**, but develop has fixed it | `test/plans/feat/tools/debugger.plan.toml` reads `13/14` with only this one failing at `511711e49`; on develop `c3b17207e` the entry is green (`1.60-1.77 s`) and the same plan reads `14/14`, so it clears when this line merges that develop |
| 2-4 | `poison:modules.rules:rules.library_verdicts_are_exactly_these`, `…reduced_element_type_instances_are_checked`, `…every_exported_rule_enters_the_check` | **green** | `test/plans/feat/modules/rules.plan.toml` reads `140/140` |
| 5 | `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update` | **green** | `test/plans/gpu/tiles_transfer.plan.toml` reads `3/3` |
| 6-7 | `poison:…nn:lrit`, `poison:…nn:mnist` | **green at `b4b7f960e`** | the plans that hold them, `test/plans/feat/modules/nn.plan.toml` and `test/plans/feat/opt/screens.plan.toml`, read `68/68` and `127/127` |
| 8 | `poison:…index_semantics:declaration_evaluator` | **red**, waiting on a ruling | the entry behind it is the one row 5 of section 1 describes |
| 9 | `feat/trans/cpp :: agree_with_fvm` | **green** | `test/plans/feat/trans/cpp.plan.toml` reads `11/11`; **only this line** had it, fixed here, section 3 |
| 10-11 | `feat/mm/gc_pacing :: gc_pacing.training_400`, `.training_800` | **green at `b4b7f960e`** | `test/plans/feat/mm/gc_pacing.plan.toml` reads `15/15`; **only this line** had them |

So the eleven-entry account now reads **2 red**, and one of the two is already green on develop. It
was 11 entries because the flip integration changed the element type of every unstated tensor; each
one of them has been answered, either by this line (the C++ cell marking, the family's typing) or by
develop (the rules family, the debugger, the tiles fixture), and rows 5 and 8 are not that family at
all -- they are two separate open questions, each with its own page.

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
tree (`d01288e3`). The rehearsal worktree (`h465-rehearsal`, its own branch and its own build on the
host) stays in place for the next one.

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
lane `39784865d`), and no reading ever gained a name. `skip 11` and `known failures: 8` never moved,
and the test total grew by one because develop adds a test. Nothing was weakened, no `known_fail` was
added or removed, no `tier` was moved, and `perf/**` and `feat/opt/progen` were not run. The
expectations the other lanes landed -- the exact specialization count, the two re-recorded profiles,
the `profile_alloc.py` prefix, the typed cases and the gradient tangent -- were merged as they are
and not touched here.

## 5. The account's readings, and the endpoint

| layer | before this batch | at `b4b7f960e` (measured) | the endpoint, written down |
|---|---|---|---|
| fast | `fail 9` (`c5c506e44`: 6218/6190) | **`fail 2`** (6219/6198): `declaration_evaluator` `752.14 ms`, `print_and_recompile` `5.43 s` | **`fail 1`** = `declaration_evaluator` once `print_and_recompile`'s own lane lands; that lane's half is not this line's and its branch is not merged here |
| slow | 11 (`c5c506e44`) | **2** of the eleven-entry account: `debugger.dap_protocol` (already green on develop `c3b17207e`, so it clears at the next develop merge) and `poison:…index_semantics:declaration_evaluator` | **`1`** = the `declaration_evaluator` entry, waiting on the ruling of `docs/handover/questions/declaration-evaluator-facts-vs-kernels.md`; the debugger entry goes green with develop |

Both of the fast layer's reds are therefore open questions with a page, not defects of the flip: one
asks what the declaration evaluator's facts should say where they meet the kernels, the other is the
value path of a `var` parameter. Everything the flip integration itself introduced -- the crashes,
the print form, the element type, the `var` cell, the type identity, the C++ cell marking, the stale
specialization and profile records -- is green.

Two caveats on the table. The slow layer is a whole-tree selection and the measurement lane owns its
full run; what is measured here is every entry of the eleven-entry account, plan by plan. And the
`print_and_recompile` lane has not landed here: its branch is not to be merged by this line without
the coordinator's word.