# The value a pass ends with: who carries it, and where it was dropped

## The inventory (measured with `git grep` over `src`, `include`, `modules`)

| function | shape | call sites | carried by | verdict |
|---|---|---|---|---|
| `GraphIRPass::failWithErrorValue(slot_t)` | `void`, a setter | 4 producers (`modules/tiles/src/region_pass.cpp:60`, `src/execute/camel_pass.cpp:220`, `:284`, `:293`) + 1 relay (`src/execute/pass.cpp:2606`) | the pass object, then the caller pass | **not** a return value: `[[nodiscard]]` would say nothing |
| `GraphIRPass::hasErrorValue()` | `bool` | 2 readers (`src/execute/pass.cpp:2605`, `:3072`) | -- | `[[nodiscard]]` now |
| `GraphIRPass::errorValue()` | `slot_t` | 2 reads, both inside a `hasErrorValue()` branch (`:2606`, `:3073`) | -- | `[[nodiscard]]` now |
| `makeError(kind, message, location)` | `slot_t` | 62 call sites: 60 `return makeError(...)` from an operator, 2 through `failWithErrorValue` | the language's `F!` result | a value constructor; `[[nodiscard]]` would fire on every operator that builds one, which is all of them |
| `reportPassErrorValue(ctx, error, json)` | `int` (the exit code) | **1 definition, 0 call sites** | -- | see below |

## What the reading found

**`PassApplyStatus::Errored` is produced and never consumed.** `applyPassesDetailed` returns
`{nullptr, PassApplyStatus::Errored, error}` (`src/execute/pass.cpp:3091`), and `PassApplyResult`
carries the value in `.error`. Nothing reads it:

- `errored()` is called nowhere;
- `reportPassErrorValue` -- the function written to print that value and set the exit code, documented
  as "the driver that ran the pass list calls this as soon as it sees `PassApplyStatus::Errored`" --
  is called nowhere;
- `applyPasses` (`:3189`) returns `applyPassesDetailed(...).program`, which is `nullptr` for this
  status, and the value goes with it.

So the header comment at `include/camel/execute/pass/base.h:102` -- "The pass ended with an error
value, **which the driver has already reported**" -- describes a report that no code makes. A pass
that ends with an error value therefore *stops* the pipeline and says nothing: the run is silent,
which is the one thing the ruling on `failWithErrorValue` calls worse than an abort.

This is the same family as the two other findings this lane recorded: a message no case prints, and a
comment promising a shape the code cannot reach. **Nothing in the test suite can see it**, because a
silent stop produces no output to assert on -- the tests that would catch it assert a message.

## What changed, and what did not

Changed, in the commit that wrote this page:

- `hasErrorValue()` and `errorValue()` are `[[nodiscard]]`, so a read that nobody uses is a warning
  rather than a habit;
- `applyRegisteredPass` no longer drops the value when it has no caller to hand it to: it stops the run
  and commits `PassInternalError` saying so, instead of continuing as if the pass had succeeded. No
  call site reaches that branch today -- both pass `this` -- and it exists so that a future one
  cannot be silent. **It has no case, and cannot have one without a pass that calls another pass with
  no caller**; that is the reason the branch is a refusal rather than a report.

Not changed, and queued: the `Errored` value still reaches no reader. Fixing it means the driver that
runs the pass list -- the one that decides the exit code and the diagnostic format -- has to see
`applyPassesDetailed`'s result, and the entry points that discard it (`applyPasses`, and the CLI's own
list loop) have to stop discarding it. That is a driver change, not a pass change, and it needs a case
that fails a pass with an error value and asserts the printed text and the exit code.