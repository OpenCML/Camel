# The flip line's reds, in both layers, at `tip=c5c506e44`, with the C++ fix at `tip=92984364f`

Readings only; no expectation, golden, tier or `known_fail` was changed to produce this page. The
fast layer is measured on this line; the slow layer was measured by the slow-layer measurement lane
at `tip=c5c506e44` and the three entries marked below were re-run here twice. Section 3 records the
one fix this line made, at `tip=92984364f`.

## 1. Fast layer -- `fail 9`

`-Plan smoke,test/plans/feat -Tier fast -Jobs 8` reads `total 6218, pass 6190, fail 9, skip 11,
known failures: 8` at `tip=c5c506e44`, and the same summary with the same nine names at
`tip=92984364f`.

| # | test | kind | owner |
|---|---|---|---|
| 1 | `lrit` | bare `Tensor` / unstated element type into `=` | open question, with the author |
| 2 | `nn.lrit` | same case through the `nn` screens plan | same |
| 3 | `mnist` | same family | same |
| 4 | `nn.mnist` | same family | same |
| 5 | `declaration_evaluator` | rank-0 array beside a float literal: kernel reads f64, the declaration evaluator f32 | open question (`rank-zero-array-and-a-float-literal.md`) |
| 6 | `print_and_recompile` | the value path of a `var` parameter | the printer half landed; this half is open |
| 7 | `specialize_growing_shape.fold_specialize.bounded` | `stats.graphs` reads 5 against a stale `> 5` floor | answered: the reading is the fix, the expectation is being re-recorded by its own lane |
| 8 | `profile.alloc_step` | allocator-call counts move when a type is interned | same |
| 9 | `fusion_group.profile.none` | its recorded report carries those counts | same |

Two of the previous tip's reds left without a new one: `export_fold` (10.03 s timeout with empty
stdout before, `PASS 343.89 ms` here, its onnx model written and parsed by the plan's own verify
script) and `rules.tensor_saturation_keeps_values` (`RewriteInvalid` before, `PASS 392.03 ms` here).

## 2. Slow layer -- 11 at this tip, 10 once the C++ fix below is in

Whole-tree slow measurement at `tip=c5c506e44` (measurement lane), plus my own re-runs of the two
plans named below, twice each:

| # | test | where it comes from |
|---|---|---|
| 1 | `debugger.dap_protocol` | red on develop too |
| 2-4 | `poison:modules.rules:rules.library_verdicts_are_exactly_these`, `…reduced_element_type_instances_are_checked`, `…every_exported_rule_enters_the_check` | red on develop too |
| 5 | `gpu/tiles_transfer :: tree_expansion_autodiff_and_device_update` | red on develop too |
| 6-8 | `poison:…nn:lrit`, `poison:…nn:mnist`, `poison:…index_semantics:declaration_evaluator` | the fast layer's own reds, seen again in the slow selection |
| 9 | `feat/trans/cpp :: agree_with_fvm` | **only this line** -- fixed here, section 3 |
| 10-11 | `feat/mm/gc_pacing :: gc_pacing.training_400`, `.training_800` | **only this line**, waiting on the ruling of section 3 |

Two slow entries went green at this tip without a new one: `feat/modules/onnx :: pass_export_equals_export_model`
(two 120 s liveness kills and a cpu reading of 0 before; `PASS 25.50 s wall / 46.94 s cpu` here) and
`poison:…rules.rules.tensor_saturation_keeps_values` (`PASS 313.05 ms`).

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

### `feat/mm/gc_pacing :: gc_pacing.training_400` / `.training_800` -- waiting on a ruling, not to be fixed

A compile-time refusal, not a moving number: `gc_training_peak.cml:29` calls `sgd(wf, g, 0.5)` with
`(f32[?, 4], Tensor, float)`, and `nn:sgd` (its signature is printed from `modules/nn/optim.cpp:567`)
binds both `P` and `grad of P` to the first argument's type, so the bare `Tensor` of the gradient
does not unify with `f32[?, 4]`. Two runs each (`188.00 ms`, `129.56 ms` and `180.22 ms`,
`209.13 ms`); the same program with `steps=2 batch=64 width=64` prints its result line on develop
(`tip=0a1a509f5`: `gc_training_peak base=18056 peak=28808 low=22664 released=0
loss=0.6931471805599647`) and is refused here. Recorded in
`docs/handover/questions/var-cell-widening-and-the-bare-tensor-return.md`, section 5: it is the
`lrit`/`mnist` family, so it is answered by whatever answers those four, and it is **not** to be
fixed on this line.

Neither of the two remaining is host jitter (both red in two consecutive runs, with the failure text
of a compile, not of a measurement) and neither is a stale expectation (the expectations are `PASS`
strings and numeric bounds that no run reached).

## 4. Acceptance, and what this page did not do

The landing account stays "only fewer, never renamed, same targets". The fast layer is `fail 9` at
`tip=c5c506e44` and is untouched by the C++ fix (same nine names); two of the nine -- the stale
graph floor and the two profile records, entries 7-9 -- are being re-recorded by the lane that owns
them, so `fail 7` is the expectation there, and this page leaves all nine exactly as they are. In
the slow layer the C++ fix takes entry 9 green, leaving 10: three of them wait on the bare-`Tensor`
ruling, one (`debugger.dap_protocol`) is red on develop too, and the rest are red on develop as
well. Nothing was weakened, no `known_fail` was added or removed, no `tier` was moved, and `perf/**`
and `feat/opt/progen` were not run.