# A fault raised inside the standard library names its file, never a line

`test/plans/feat/modules/stdlib_faults.plan.toml` pinned the fault of the library at line numbers
inside `src/stdlib/**`: `clamp` at 506, `gcd` at 593, `lcm` at 608, the statistics family at
48/49/54, the array reductions at 62/150. A line inside the library moves whenever anybody edits
the library, so each such pin is a test that goes red for a change that broke nothing. The plan
itself records the cost: `5870d0b8f` chased `clamp` from 495 to 506, and the `zip_with` entry's own
`desc` records its line moving 254 → 300 → 366 → 371 as the array module was rewritten underneath
it. This card replaced those pins with the file the diagnostic came from, its kind and its message,
and left the line pins that point into `test/cases/**` alone: those lines are the program's own
text, and only the case can move them.

Branch `lane/dead-citation`, base `origin/integration @ 8c09b72c65`, worktree
`D:\Projects\Camel-wt\misc`. One commit for this card:

| commit | what | size |
|---|---|---|
| `6bd1566074` | the 23 entries that stopped pinning a stdlib line, plus the header comment stating the rule | 1 file, +58/−23 |

No `src/**` file is touched, `test/tools/run-tests.mjs` is not touched (0 lines: the runner already
has `expect_diagnostic_file`), and the working tree is clean.

## 1. What each pin became

Twenty-three entries changed. Twenty-one of them now name a file and keep the kind and the message;
two (`min_empty`, `max_empty`) turned out to be attributed to the program's own call and keep their
line 6.

| entry (case) | instances | was | now |
|---|---|---|---|
| `clamp_bounds`, `.opt` (`fault_clamp_bounds.cml`) | 5 | line 506 | `out/latest/stdlib/math.cml` + `clamp needs lo <= hi` |
| `clamp_nan`, `.opt` (`fault_clamp_nan.cml`) | 5 | line 506 | `out/latest/stdlib/math.cml` + `clamp needs lo <= hi` |
| `gcd_overflow`, `.opt` (`fault_gcd_overflow.cml`) | 5 | line 593 | `out/latest/stdlib/math.cml` + `gcd does not accept the smallest integer` |
| `lcm_overflow`, `.opt` (`fault_lcm_overflow.cml`) | 5 | line 608 | `out/latest/stdlib/math.cml` + `lcm does not fit the type` |
| `quantile_range`, `.opt` (`fault_quantile_range.cml`) | 5 | line 48 | `out/latest/stdlib/statistics.cml` + `a quantile is between 0 and 1` |
| `variance_short`, `.opt` (`fault_variance_short.cml`) | 5 | line 54 | `out/latest/stdlib/statistics.cml` + `a variance needs at least two elements` |
| `stdev_short`, `.opt` (`fault_stdev_short.cml`) | 5 | line 54 | `out/latest/stdlib/statistics.cml` + `a variance needs at least two elements` |
| `median_empty`, `.opt` (`fault_median_empty.cml`) | 5 | line 49 | `out/latest/stdlib/statistics.cml` + `the quantile of no elements` |
| `min_empty_string` (`fault_array_min_empty_string.cml`) | 4 | line 150 | `out/latest/stdlib/array.cml` + `a reduction over no elements` |
| `mean_empty` (`fault_array_mean_empty.cml`) | 4 | line 150 | `out/latest/stdlib/array.cml` + `a reduction over no elements` |
| `argmax_empty` (`fault_array_argmax_empty.cml`) | 4 | line 150 | `out/latest/stdlib/array.cml` + `a reduction over no elements` |
| `argmin_empty` (`fault_array_argmin_empty.cml`) | 4 | line 150 | `out/latest/stdlib/array.cml` + `a reduction over no elements` |
| `chunks_zero` (`fault_array_chunks_zero.cml`) | 4 | line 62 | `out/latest/stdlib/array.cml` + `a chunk holds at least one element` |
| `min_empty`, `max_empty` (`fault_array_{min,max}_empty.cml`) | 8 | line 6 | line 6 kept; the message added (`a reduction over no elements`) |

60 instances were re-pinned to a file. The plan now holds 21 `expect_diagnostic_file`, 19
`expect_diagnostic_line` and 23 `expect_output_contains`.

`min_empty` and `max_empty` are the two that do not belong in the class: `min` and `max` carry
`#impl(min_kernel)`, the kernel raises the fault, and the diagnostic is reported at the call site in
`fault_array_min_empty.cml:6:13` — the program's own text. The first attempt gave them a file pin
too and the run said so (`§5`, reading `b705e8d014`).

`stdlib.fault.zip_with` is the same class and was **left alone**: its line 371 is a stdlib line, and
it is one of the five `stdlib.fault.zip_with.{nvm,fvm,jit,prl,opt}` baseline reds this lane was told
not to touch. It is reported here rather than repaired, and it is the only red in every reading
below.

## 2. Readings

All on the plan file alone (`-Plan test/plans/feat/modules/stdlib_faults.plan.toml -NoGpuWatch`),
`[SUMMARY]` quoted verbatim.

Before the change, the branch tip as handed over (`cec4050820`, the plan with the 23 line pins):

```
[provenance] tip cec4050820aebece0fa89fdc08f14f62f32eda87 | binary built 2026-11-27T23:25:28+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 16
[SUMMARY] total: 112, pass: 107, fail: 5
```

After the change (`6bd1566074`):

```
[provenance] tip 6bd15660745622e269334d7991e9497653e74610 | binary built 2026-11-27T23:59:28+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 13
[SUMMARY] total: 112, pass: 107, fail: 5
```

The five reds in both are `stdlib.fault.zip_with.{nvm,fvm,jit,prl,opt}`. Nothing else moved.

The card asked for "20 red before, 20 green after". On this tree the twenty were **already green
before**: `5870d0b8f` had chased the clamp pin to the line the library now has, and the same was true
of gcd, lcm, statistics and array. That is why the change is not visible as a red-to-green pair — it
is visible only when a line moves, which is exactly what §3 does. The premise of the card was true on
the tree it was written against (`clamp` at 506 vs a pin at 495); it had been paid off by re-pinning,
which is the treatment, not the cure.

## 3. Root proof: one comment in the library, no case touched

A single comment line inserted above `export func clamp` in `src/stdlib/math.cml` moves 506 → 507,
593 → 594, 608 → 609 — the three functions below it. Nothing else is touched: no case, no diagnostic,
no other library file. Two runs on that tree, differing **only** in the plan file:

The baseline plan (the 23 line pins), scratch commit `6082ece98e`:

```
[provenance] tip 6082ece98e02ec3e12b0c411e268d0f2bac57794 | binary built 2026-11-27T23:40:00+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 16
[SUMMARY] total: 112, pass: 87, fail: 25
```

25 = the 20 `clamp_bounds`, `clamp_nan`, `gcd_overflow`, `lcm_overflow` instances + the 5
`zip_with` baseline reds. This is the card's "before 20 red", earned here rather than assumed: the
one comment, alone, takes twenty green entries down.

The changed plan (21 file pins), scratch commit `97e5b11671`, the comment still in `math.cml`:

```
[provenance] tip 97e5b11671757b5d0b7dbc59270fd275b7c730f6 | binary built 2026-11-27T23:44:20+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 13
[SUMMARY] total: 112, pass: 107, fail: 5
```

All 60 file-pinned instances green with the library's line numbers moved under them.

The comment removed, the branch back at `6bd1566074`, run again:

```
[provenance] tip 6bd15660745622e269334d7991e9497653e74610 | binary built 2026-11-27T23:59:28+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 13
[SUMMARY] total: 112, pass: 107, fail: 5
```

The counterfactual commits were scratch: `git reset --hard 6bd1566074` removed them, and neither the
comment nor the old plan is in the branch. `git diff 8c09b72c65 HEAD -- src/stdlib` is empty and
`git diff 8c09b72c65 HEAD -- test/tools/run-tests.mjs` is empty.

## 4. The assertions that stayed have teeth

Three deliberate wrong-value runs, each one mutation on top of `6bd1566074`, scratch commit
`1ee5db06a1`:

```
[provenance] tip 1ee5db06a1d5869b48f882c21ab4b97bc87fec4b | binary built 2026-11-27T23:57:13+08:00 | tree /home/zjwei/camel-work/misc | tier fast | jobs 14
[SUMMARY] total: 112, pass: 93, fail: 19
```

19 = 5 `zip_with` + 5 + 5 + 4, one group per mutation.

**(A) the message, misspelled** — `clamp_bounds` and `.opt`: `clamp needs lo >= hi` for
`clamp needs lo <= hi`:

```
[FAIL] stdlib.fault.clamp_bounds.nvm
[INFO] output does not contain 'clamp needs lo >= hi'
[stderr] (math)/home/zjwei/camel-work/misc/out/latest/stdlib/math.cml:506:36: [Error]: clamp needs lo <= hi ...
```

The first attempt at this mutation truncated the message to `clamp needs lo <= h`, which is still a
substring of the real one and stayed green — the same lesson as `contains ["ran"]` in the task-318
page: a `contains` assertion only bites when the wrong text is not a prefix of the right text. The
reading above is the operator inverted, not truncated.

**(B) the diagnostic's name, corrupted** — `gcd_overflow` and `.opt`, `InvalidArguments` for
`InvalidArgument`:

```
[FAIL] stdlib.fault.gcd_overflow.nvm
[INFO] expected diagnostic RuntimeDiag::InvalidArguments in out/latest/stdlib/math.cml
[stderr] (math)/home/zjwei/camel-work/misc/out/latest/stdlib/math.cml:593:49: [Error]: gcd does not accept the smallest integer ...
```

**(C) a line pin in the program's own text, moved off by one** — `stdlib.fault.index`, line 7 for
line 6:

```
[FAIL] stdlib.fault.index.nvm
[INFO] expected diagnostic RuntimeDiag::IndexOutOfRange, line 7
[stderr] (fault_index)/home/zjwei/camel-work/misc/test/cases/modules/stdlib/fault_index.cml:6:14: [Error]: Index 5 is out of range ...
```

This is the sample the card asked for: the user-program pins still compare a line, and the reported
position is the case's own `:6:14`. The other fourteen user-program pins (`index_negative`,
`index_empty` 6; `index_char` 3; `repeat_huge`, `shl_width`, `shr_negative`, `shl_i32` 5) stayed
green in every run above — including the one where a comment in `math.cml` moved three library
lines — which is the point of keeping them: nothing outside their own case can move them.

## 5. What the diagnostics actually carry (two intermediate readings)

Two readings of the work in progress, on commits that were amended away and are not in the history.
They are recorded because they are how the file spelling was decided.

Pinning `src/stdlib/math.cml` (`56378016eb`): `[SUMMARY] total: 112, pass: 39, fail: 73` — and the
failure text said why:

```
[INFO] expected diagnostic RuntimeDiag::InvalidArgument in src/stdlib/math.cml: that diagnostic is reported in out/latest/stdlib/math.cml, not in the file the entry requires
[stderr] (math)/home/zjwei/camel-work/misc/out/latest/stdlib/math.cml:506:36: [Error]: clamp needs lo <= hi ...
```

The compiler reads the library from the installed copy (`out/latest/stdlib`), so the file a
diagnostic names is that copy. The plan now writes `out/latest/stdlib/<name>.cml`, which is the same
file the build made from `src/stdlib` and whose **name** does not move when the library gains a line.

With that spelling and `min_empty`/`max_empty` still given file pins (`b705e8d014`): `[SUMMARY]
total: 112, pass: 99, fail: 13` — the eight `min`/`max` instances, reported from the call site:

```
[INFO] expected diagnostic RuntimeDiag::InvalidArgument in out/latest/stdlib/array.cml: that diagnostic is reported in test/cases/modules/stdlib/fault_array_min_empty.cml, not in the file the entry requires
[stderr] (fault_array_min_empty)/home/zjwei/camel-work/misc/test/cases/modules/stdlib/fault_array_min_empty.cml:6:13: [Error]: Invalid argument of 'array.min': a reduction over no elements. ...
```

## 6. Boundary

- `src/**` semantic change: none. The only `src/stdlib` write was the one comment of §3, reverted;
  the branch's diff touches one plan file (and, in the previous card's commit, one report page).
- Diagnostics and their text: unchanged. `expect_diagnostic_name` and the new
  `expect_output_contains` are the old name pin plus the message the diagnostic always printed.
- Runner: unchanged. `expect_diagnostic_file` already existed, resolves a relative value from the
  case's own directory (which is why the value climbs out of `test/cases/modules/stdlib`), and is
  checked against the reported path resolved from the repository root — the two `[INFO]` lines of
  §5 are that comparison speaking.
- Branches: not pushed by hand (the remote tool stages by commit), no new branch, no `src/**` edit.