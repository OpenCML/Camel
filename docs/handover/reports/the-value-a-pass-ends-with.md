# The value a pass ends with: who carries it -- and a reading that was wrong

## The correction first

This page first said `PassApplyStatus::Errored` "is produced and never consumed", with
`reportPassErrorValue` at "1 definition, 0 call sites". **That was wrong**, and the way it was wrong
is the point:

```
git grep -n "errored()" "reportPassErrorValue(" -- src include modules
```

The driver lives in **`tools/`**. Counting readers inside three directories and concluding that a
status has none is the same error as reading a plan up to the first `]`: the scope of the search is
part of the reading, and a search that cannot reach the answer is not evidence of its absence.

The same grep over the whole tree:

| reader | call sites |
|---|---|
| `result.errored()` | `tools/camel-cli/main.cpp:682`, `:703`; `tools/camel-cli/timeit.cpp:556`, `:584`, `:598`, `:611`; `tools/debugger/src/run.cpp:310`, `:324` |
| `reportPassErrorValue(...)` | `tools/camel-cli/main.cpp:686`, `:704`; `tools/camel-cli/timeit.cpp:561`, `:586`, `:600`, `:613`; `tools/debugger/src/run.cpp:314`, `:325` |

So a pass that ends with an error value **is** reported: the CLI, `--timeit` and the debugger each
read the status and call `reportPassErrorValue`, which sets the exit code and prints the value as an
error nobody handled. The comment at `include/camel/execute/pass/base.h#PassApplyResult` -- "which the driver has
already reported" -- is true. The claim that no test could see it was this page's own blind spot.

## What the whole-tree grep did find

`applyPasses` (`src/execute/pass.cpp#applyPassesDetailed`, declared at `base.h:187`) is
`applyPassesDetailed(...).program`: for an `Errored` result that is `nullptr`, and the value goes with
it. **It has no callers anywhere in the tree** -- the entry points that run a pass list are the CLI's
(`main.cpp:680`, `:701`) and both read the detailed result. So it was a helper that silently drops a
status, kept for a use nobody had; it is deleted, and `applyPassesDetailed(...).program` is what a
new caller should write, with the `.errored()` check the three existing drivers make.

## The inventory, corrected (measured over `src`, `include`, `modules`, `tools`)

| function | shape | call sites | verdict |
|---|---|---|---|
| `GraphIRPass::failWithErrorValue(slot_t)` | `void` setter | 4 producers (tiles `region_pass.cpp:60`, `camel_pass.cpp:220`, `:284`, `:293`) + 1 relay (`pass.cpp:2606`) | cannot be `[[nodiscard]]`; its comment now says the caller must check `hasErrorValue()` or hand the value on |
| `GraphIRPass::hasErrorValue()` | `bool` | 2 readers in `pass.cpp` (`:2605`, `:3072`) | `[[nodiscard]]` |
| `GraphIRPass::errorValue()` | `slot_t` | 2 reads, both inside a `hasErrorValue()` branch | `[[nodiscard]]` |
| `makeError(kind, message, location)` | `slot_t` | 62 call sites, 60 of them `return makeError(...)` from an operator | a value constructor; not `[[nodiscard]]` |
| `reportPassErrorValue(ctx, error, json)` | `int` | 8 call sites, all in `tools/` | reported |

## The rule this round produced

**A count is only as good as the set it was counted over.** `git grep -- src include modules` cannot
see `tools/`. Before an absence is reported, the search's roots belong in the sentence: "no reader in
`src`, `include`, `modules`" is a different claim from "no reader", and only the first was checked.