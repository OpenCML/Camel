# The flip line's reds, in both layers, from `tip=c5c506e44` to `tip=26bd1c6cc`

Readings only; no expectation, golden, tier or `known_fail` was changed to produce this page. The
fast layer is measured on this line; the slow layer was measured by the slow-layer measurement lane
at `tip=c5c506e44` and the three entries that only this line had were re-run here twice. Section 3
records the one fix this line made, at `tip=92984364f`, and section 5 the account's six readings.

## 1. Fast layer -- `fail 9` at `c5c506e44`, `fail 6` at `26bd1c6cc`

`-Plan smoke,test/plans/feat -Tier fast -Jobs 8` reads `total 6218, pass 6190, fail 9, skip 11,
known failures: 8` at `tip=c5c506e44`, the same summary with the same nine names at `tip=92984364f`
(after the C++ fix), and `total 6219, pass 6194, fail 6, skip 11, known failures: 8` at
`tip=26bd1c6cc`, after `origin/develop` (`43a5c8066`) and `handover/h499-type-identity`
(`26bd1c6cc`) were merged. The total grew by one because develop adds one test; the six remaining
names are exactly the six that were red before:

| # | test | kind | owner |
|---|---|---|---|
| 1 | `lrit` | bare `Tensor` / unstated element type into `=` | the bare-`Tensor` family, being fixed on this line |
| 2 | `nn.lrit` | same case through the `nn` screens plan | same |
| 3 | `mnist` | same family | same |
| 4 | `nn.mnist` | same family | same |
| 5 | `declaration_evaluator` | rank-0 array beside a float literal: kernel reads f64, the declaration evaluator f32 | same |
| 6 | `print_and_recompile` | the value path of a `var` parameter | the printer half landed; this half is another open ruling |

The three entries that were red only as stale records are green at `tip=26bd1c6cc`, measured in the
same gate: `specialize_growing_shape.fold_specialize.bounded` `144.94 ms` (its plan now asserts
`stats.specializations === 1` and `stats.graphs === 5`), `profile.alloc_step` `222.71 ms`, and
`fusion_group.profile.none` `387.14 ms` (its two recordings say they were recorded from this tree
and carry no commit id).

Two of the previous tip's reds left without a new one: `export_fold` (10.03 s timeout with empty
stdout before, `PASS 343.89 ms` here, its onnx model written and parsed by the plan's own verify
script) and `rules.tensor_saturation_keeps_values` (`RewriteInvalid` before, `PASS 392.03 ms` here).

## 2. Slow layer -- 11 at `c5c506e44`, 10 with the C++ fix, 6 measured at `511711e49`

Whole-tree slow measurement at `tip=c5c506e44` (measurement lane), plus my own re-runs of the plans
named below by name (a plan file runs in full whatever the tier says), twice each for the three
entries that only this line had. At `tip=511711e49` the entries were re-checked one by one, and the
slow layer there is **6**, not 10: the three `poison:modules.rules` entries and the
`gpu/tiles_transfer` entry went green with the fixes develop has since landed.

| # | test | state at `511711e49` | where it comes from |
|---|---|---|---|
| 1 | `debugger.dap_protocol` | **red**, `test/plans/feat/tools/debugger.plan.toml` reads `13/14` with only this one failing | red on develop too |
| 2-4 | `poison:modules.rules:rules.library_verdicts_are_exactly_these`, `…reduced_element_type_instances_are_checked`, `…every_exported_rule_enters_the_check` | **green**, `test/plans/feat/modules/rules.plan.toml` reads `140/140` | red on develop at `0a1a509f5`, answered since |
| 5 | `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update` | **green**, `test/plans/gpu/tiles_transfer.plan.toml` reads `3/3` | red on develop at `0a1a509f5`, the fixture that made it red is fixed |
| 6-8 | `poison:…nn:lrit`, `poison:…nn:mnist`, `poison:…index_semantics:declaration_evaluator` | **red** | the fast layer's own reds, seen again in the slow selection -- the bare-`Tensor` family |
| 9 | `feat/trans/cpp :: agree_with_fvm` | **green**, `test/plans/feat/trans/cpp.plan.toml` reads `11/11` | **only this line** -- fixed here, section 3 |
| 10-11 | `feat/mm/gc_pacing :: gc_pacing.training_400`, `.training_800` | **red** | **only this line**, the bare-`Tensor` family, being fixed on this line |

Entries 6-8 and 10-11 are one family: a bare `Tensor` -- the element type of a value not stated at
its source -- does not bind to a parameter of known shape. With that family done, what is left is
entry 1 alone, which is red on develop as well; the endpoint section 5 states that.

Two slow entries went green before this at `c5c506e44` without a new one: `feat/modules/onnx ::
pass_export_equals_export_model` (two 120 s liveness kills and a cpu reading of 0 before;
`PASS 25.50 s wall / 46.94 s cpu` there) and `poison:…rules.rules.tensor_saturation_keeps_values`
(`PASS 313.05 ms`).

### The one merge this line refused

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
merge of its own. The merge commit was `579d5ad57` and this line is back at `511711e49`.

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

### `feat/mm/gc_pacing :: gc_pacing.training_400` / `.training_800` -- the bare-`Tensor` family, being fixed on this line

A compile-time refusal, not a moving number: `gc_training_peak.cml:29` calls `sgd(wf, g, 0.5)` with
`(f32[?, 4], Tensor, float)`, and `nn:sgd` (its signature is printed from `modules/nn/optim.cpp:567`)
binds both `P` and `grad of P` to the first argument's type, so the bare `Tensor` of the gradient
does not unify with `f32[?, 4]`. Two runs each (`188.00 ms`, `129.56 ms` and `180.22 ms`,
`209.13 ms`); the same program with `steps=2 batch=64 width=64` prints its result line on develop
(`tip=0a1a509f5`: `gc_training_peak base=18056 peak=28808 low=22664 released=0
loss=0.6931471805599647`) and is refused here. Recorded in
`docs/handover/questions/var-cell-widening-and-the-bare-tensor-return.md`, section 5, and it is the
same family as entries 6-8 of the slow layer, so it is fixed by whatever fixes those. `origin/develop`
now carries the answer for that family; the fix is one work item on this line, and nobody else should
touch these five entries while it runs.

Neither of the two remaining is host jitter (both red in two consecutive runs, with the failure text
of a compile, not of a measurement) and neither is a stale expectation (the expectations are `PASS`
strings and numeric bounds that no run reached).

## 4. Acceptance, and what this page did not do

The landing account stays "only fewer, never renamed, same targets": the fast layer went `fail 9`
(`c5c506e44`) -> `fail 9` with the same names (the C++ fix) -> `fail 6` (`26bd1c6cc`, the two merges:
`origin/develop` at `43a5c8066` and `handover/h499-type-identity` at `26bd1c6cc`), and the six
remaining names are six of the nine. `skip 11` and `known failures: 8` never moved, and the test
total grew by one because develop adds a test. Nothing was weakened, no `known_fail` was added or
removed, no `tier` was moved, and `perf/**` and `feat/opt/progen` were not run. The expectations the
other lane landed -- the exact specialization count, the two re-recorded profiles, the
`profile_alloc.py` prefix -- were merged as they are and not touched here.

## 5. The account's six readings, and the endpoint

| layer | before this batch | at `511711e49` (measured) | once the bare-`Tensor` family is done (projected) |
|---|---|---|---|
| fast | `fail 9` (`c5c506e44`: 6218/6190) | **`fail 6`** (6219/6194 at `26bd1c6cc`; `511711e49` differs from it by this page only) = the six baseline names | **`fail 1`** -- `print_and_recompile` only |
| slow | 11 (`c5c506e44`) | **6** (re-checked entry by entry at `511711e49`: `debugger.dap_protocol` red, the three `poison:modules.rules` entries and the `gpu/tiles_transfer` entry green, the three fast-family entries and the two `gc_pacing` entries red) | **1** -- `debugger.dap_protocol`, red on develop too |

What that means for the day this line lands: its reds are the ones develop is red on anyway, plus
`print_and_recompile`, which is another open ruling and not this line's. Everything the flip
integration itself introduced -- the crashes, the print form, the element type, the `var` cell, the
type identity, the C++ cell marking, the stale specialization and profile records -- is green.

Two caveats on the table. The slow layer is a whole-tree selection and the measurement lane owns its
full run; what is measured here is every entry of the eleven-entry account, plan by plan. And the
bare-`Tensor` lane has not landed: its branch has to start from a tip of this line (section 2), so
until it does, entries 6-8 and 10-11 of the slow layer and four of the fast layer's six stay red.