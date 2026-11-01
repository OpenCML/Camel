# Where a bad pass parameter is reported, and why there is only one place

## The two readings, on the same command

`camel test/cases/modules/onnx/export_loop.cml "std::opt settle_runs=0" onnx::export`

**Before the merge that brought both mechanisms into one tree** -- the construction-time path:

```
error[RuntimeDiag::InvalidArgument]: Invalid argument of 'std::opt': settle_runs must be at least 1, got 0.
help: The operator was given an argument it cannot take; ...
EXIT=1
```

**After it** -- the declaration-time path, which runs first:

```
error[RuntimeDiag::InvalidPassParameter]: Invalid pass specification 'std::opt settle_runs=0': settle_runs must be at least 1, got 0.
  --> <pipeline>:1:1
help: A pass is written on the pipeline as its name followed by its parameters as key=value words, ...
EXIT=1
```

**One diagnostic, not two**: `valueProblem` refuses while the pipeline is read, the pass is never
built, and the construction receiver never has anything to receive. The `InvalidArgument` version
cannot be reached any more, so its path was deleted -- `settleRunsOf` reads the value, and the range
is the declaration-time check's business -- and its test went with it. Who took it over is
`valueProblem` + `RuntimeDiag::InvalidPassParameter`, with the position of the word in the pipeline
that the construction site could not give.

## The division, in one table

| what is wrong | who catches it | when | what the user sees |
|---|---|---|---|
| parameter undeclared, duplicated, wrong type, missing, out of the declared range | `valueProblem` / `bindArgs` (`src/execute/pass.cpp:1370-1400`) | while the pipeline is **read** | `ParamArgumentUnknown` / `Duplicate` / `WrongType` / `Missing`, or `InvalidPassParameter` for a range, with `<pipeline>:line:col` |
| the specification is not a call at all | `parameterDiagnostic`, `Kind::Syntax` | while the pipeline is read | `InvalidPassParameter` |
| the pass **refuses to be built** for a reason its declaration does not describe | `constructPass` -> `reportPassFailure` (`src/execute/pass.cpp:2551`, `:2520`) | while the pass is **constructed** | `PassInternalError` naming the pass and what it said |
| the pass breaks while running | the same `reportPassFailure`, from the `apply` handler (`:3050`) | while the pass **runs** | `PassInternalError` |

**The rule**: anything the declaration can describe is refused where the declaration is read, and
gets a position; anything only the maker knows is refused where the maker is called, and gets the
pass name. The two never answer the same input, because the first runs before the second exists.

## What this cost, and what stays

`std::opt settle_runs=0` used to end in `terminate called after throwing ... DiagnosticsLimitExceededException`,
`EXIT=134`: the diagnostic was composed correctly and nobody received it. Two independent fixes
arrived for that one hole -- a declaration-time check, and a construction-time receiver -- and only
one of them can be the answer for a given input. Keeping both was the state this round ended:

- the **declaration-time check** is the answer for `settle_runs`;
- the **construction receiver** stays, because a maker can still throw for a reason no declaration
  describes, and because a throw from a maker must never reach `terminate` again. It is held by
  `pass_guard.maker_failure_reported`, which runs the test pass `test::broken_maker` -- a maker that
  throws on purpose -- and asserts the diagnostic, exit code 1, and the absence of
  `terminate called` / `DiagnosticsLimitExceededException`.

The intermediate type (`PassParameterViolation`) is gone with the path: the construction receiver
now has three cases, not four -- a diagnostics-limit throw (already recorded, say nothing), a
`std::exception` (report it as `PassInternalError`), and nothing else.