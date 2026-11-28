# The sixty cells of the alias-table removal

`186dff396f` ("A pass has one name: the core alias table is gone and every spelling it answered now
names its pass") deleted `passAliases` from `src/kernel/execute/pass.cpp`. Its parent,
`186dff396f^`, still carried the table at lines **111-174** -- **31 map entries**: the 30 rows of
this report plus `{kRunPassName, kRunPassDefaultTarget}`, i.e. `std::run` -> `std::fvm`. That 31st
row is not here: `std::run` is a name of its own (the fallback pass, `docs/cli.md`), not a spelling
of another pass, and it still answers today.

Each of the 30 rows is read **twice**, on the tree at `0b1f54dc29` (one commit past the deletion):
the left-hand spelling and the right-hand one, one `camel` call each, 60 cells.

## Reading

- tree: a detached checkout of `0b1f54dc29` ("The AST record follows the declarations that moved:
  std.cml's row, and no other"), `[provenance] binary built 2026-11-28T15:38:50+08:00 | tier fast |
  jobs 2`
- program: `test/cases/probe/t425/tiny.cml`, `func main() sync { }` -- the pass-name check happens
  before anything runs, so a program that parses and has a `main` is enough for both halves
- one plan, 60 entries, one run:
  `[SUMMARY] total: 60, pass: 54, fail: 6, killed by a signal: 1`, `checks: 1 run, 0 failed`
- the plan and the case were a probe and are **not** in the tree: they were deleted after the
  reading, and the tree went back to its branch with `git status` empty

## The judgement the plan applied

| cell | expected | verdict |
|---|---|---|
| left-hand spelling | `exit 1` and the output contains `Pass <name> not found` | refused as an unknown pass, and the refusal **quotes the name it did not find** -- that is what tells "this name is gone" from "this build is broken" |
| left-hand spelling | `exit 0` | accepted: the name is live -- then ask whether it is the pass's own name or a surviving alias |
| right-hand spelling | `exit 0` | the name the left-hand spelling resolved to is a name the command line knows, and the pipeline runs |
| right-hand spelling | `exit 1` | only correct if it is the retired name or a parameter refusal; `Pass <name> not found` hides a broken row |
| every cell | `did you mean`, `Did you mean` absent | migration hints are forbidden: a name that is gone is refused, not advertised |

"Did you mean" was checked twice: every one of the 60 entries carried
`expect_output_not_contains = ["did you mean", "Did you mean"]`, and the refusal text of
`std::cxx::opt` was fetched whole from the run (8595 bytes, 172 lines) and read: **0** occurrences
of `mean`, `suggest` or `instead`.

## The four rows that are not what the table looked like

`std::opt` and `std::jit` are the **passes' own names**, and the spellings that retired are the
table's *right*-hand sides. `186dff396f` changed the registrations:

| file | before | after (`0b1f54dc29`) |
|---|---|---|
| `src/stdlib/std/cxx/opt/opt/opt.cpp:45` | `"std::cxx::opt"` | `"std::opt"` |
| `src/stdlib/std/fvm/fastvm.cpp:1648` | `"std::fastvm::jit"` | `"std::jit"` |

So `std::opt` and `std::jit` answer `exit 0` **because they are the surviving names**, and
`std::cxx::opt` / `std::fastvm::jit` are the retired ones (`Pass <...> not found`). The CSV marks
the two surviving spellings `own`, and their two partners `new` with the refusal quoted. Nothing
here was silently accepted.

## The two findings outside the alias question

- `std::gc::views_escape` is a **known name** whose pass **aborts** on the smallest program:
  `Camel Invariant Violation: Location ../src/kernel/core/rtdata/view.cpp(162), Guarantee
  !isViewSlot(slot, type), Reading "A compact element view escaped into the reduce accumulator
  ((int, int))"`, `rc=134`. Re-read on the integration tip `094341adb5` with the same program and
  the same command: **it aborts there too** (`std::gc::views` answers as the control in the same
  run), so this is a defect that predates the alias work, not a row of this table.
- `std::gc::poison` is a **known name** that refuses to run as a pass on its own: `Pass
  'std::gc::poison' failed with an internal error: GC poison self-test requires --gc-check poison.`
  The name half of the row is correct; the run needs the CLI switch.

## The two capture levels of the `first_stderr_line` column

- verbatim: the four cells above (`std::cxx::opt`, `std::fastvm::jit`, `std::gc::views_escape`,
  `std::gc::poison`) were captured from the run's own output files
  (`-FetchFile 'fail/probe.t425_aliases/<test>/stderr.txt' -FromThisRun`) and are quoted as text.
- enforced: every other cell is the line the entry's `expect_output_contains` required and the run
  satisfied -- `Pass <name> not found, available passes are:` for a refusal, `(no refusal line;
  exit 0)` for an accepted name. Re-running the same 60 entries re-checks the whole table.

## Defect lead: `std::gc::views_escape` aborts on the smallest program

A defect card will be opened from this; whoever takes it should start here.

**The stderr, whole** (536 bytes on disk at the capture; ANSI colour escapes stripped here, the
text is otherwise unchanged):

```
Camel Invariant Violation:
    Location   : ../src/kernel/core/rtdata/view.cpp(162)
    Function   : void camel::core::rtdata::assertStoredValueIsNotView(slot_t, const type::Type *, std::string_view)
    Guarantee  : !isViewSlot(slot, type)
    Reading    : A compact element view escaped into the reduce accumulator ((int, int)). A view is tied to the thread-local view ring and must be materialised before it is stored.

Stack Trace: (not available on this platform)
```

**The smallest reproduction**: a file `m.cml` holding

```camel
func main() sync {
}
```

and the command

```
camel m.cml std::gc::views_escape std::nvm
```

which exits `134` (SIGABRT) with the text above.

**The control that says the probe is not what broke**: in the same run, the same program, the
neighbouring pass answers --

```
camel m.cml std::gc::views std::nvm     -> exit 0, 82 ms
```

so the program, the pipeline shape and the runner are all fine; the abort is the pass.

**Seen on two trees, byte-identical**: the alias-removal tree `0b1f54dc29` (where the sixty cells
were read) and the integration tip `094341adb5`. That is what makes it a defect that **predates**
the alias work rather than one those commits introduced.
