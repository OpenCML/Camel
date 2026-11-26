# Close-of-day Actions run on develop: two reds, one of them a build break

Run [`45850100433`](https://github.com/OpenCML/Camel/actions/runs/45850100433), dispatched on `develop`
(`1a7cf9ff32f9`), 2026-11-26 23:32 +08. Macos-arm64 was cancelled by concurrency; the ASan and no-JIT legs
were not requested.

## Readings

| job | conclusion | detail |
| --- | --- | --- |
| Source headers | success | — |
| Build and test (linux-x64) | **failure** | step `Test (smoke + feat)`: `[SUMMARY] total: 9718, pass: 9038, fail: 629, skip: 20, known failures: 31` |
| Build and test (win-x64) | **failure** | step `Build (Release)`: clang-cl stops on `span:75:9: arithmetic on a pointer to an incomplete type 'const camel::runtime::EditNote'` |
| Build and test (macos-arm64) | cancelled | concurrency |
| ASan + UBSan, linux-x64-nojit | skipped | inputs not requested |

## Red 1: the Windows build does not compile this tree

`a40026d36f` (graph editing: three table rows, `add_param`, a refusal that names its edit) forward-declares
`EditNote` in `src/kernel/include/camel/runtime/resources.h:416` and then gives one of the functions a
default argument of type `std::span<const EditNote>` (`resources.h:507`, `= {}`).

The forward declaration is deliberate — `draft.h` includes this header, so defining `EditNote` here would
close an include cycle. The default argument is what breaks: on MSVC's `<span>` a `span` over an incomplete
type cannot be built from `{}`, because constructing it computes the end pointer. Our Linux builds use
libstdc++'s `<span>`, which is tolerant of it, which is why this only shows up on the win-x64 leg.

Every affected object fails at once (`tiles`, `graph`, `test` modules), so this is not a sporadic error:
**the published line does not build on Windows at this commit**, and it has not built since `a40026d36f`
landed. This is a defect of the change, not of the runner.

## Red 2: one test is red and not declared known

`floatgate.f32_loop_state_cotangent_port.nvm` —

```
(floatgate_f32_loop)…/test/cases/transforms/transpose/floatgate_f32_loop.cml:28:16: [Error]: …transpose has no transpose for this function: a loop carries its state out of itself…
```

The plan (`test/plans/feat/transforms/floatgate.plan.toml:46`) expects this case to pass. It is a law
coverage gap, not a numeric disagreement: the transposer refuses because the loop carries state out.

Everything else in the 629 is declared `known_fail` (31 exemplars, all with an owner). The pass/fail pair of
this run cannot be compared name-by-name with the last promotion (9739 / 9048 / 634 / 26 / 31) because this
run took the fast layer on `develop`, not on `integration`, and the two trees differ by 618 commits.

## What this tells the promotion

Both reds are already in `develop` and in `integration`, so the promotion about to run will meet them and
they are **not** evidence against the integration tip. They do mean the promotion's "new red = 0" bar cannot
be met on absolute counts: the comparison has to be against the last promotion's per-name table, and these
two names have to be on the resulting list with their cause attached.

## Follow-ups recorded in the backlog

- the Windows build break — a fix lane, urgent, one slot;
- `floatgate.f32_loop_state_cotangent_port` — a law coverage gap: which transform refuses to carry loop
  state out of itself, and whether the refusal is right (then the case is a record that must change) or the
  law is missing (then the law lane owns it).