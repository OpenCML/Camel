# Test Rules

> **Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../docs/design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.

## General Policy
- **Before writing a file, print the target's absolute path.** `[System.IO.File]::WriteAllText` and shell redirection (`>` , `Out-File`) resolve a *relative* path against the process's working directory, not against the directory a previous `cd` put the shell in -- and that working directory is not always the worktree the reader assumes. On 2026-11-02 six writes landed in the main checkout `D:\Projects\Camel` that way, two of them as **0-byte** files: a 0-byte file is the worst shape, because it looks like "I created it" while nothing was written. So: derive the path from an absolute root (or `$PSScriptRoot`), print it with `Resolve-Path -LiteralPath` / `[IO.Path]::GetFullPath()`, and end a round with `git -C D:\Projects\Camel status --porcelain`, which must print nothing.
- **And verify such a write with the same absolute path, never with `Select-String` or `Get-Content` alone.** Those two follow *PowerShell's* location, while `[IO.File]` follows the process's, so checking a write with them can read a **different file** and "prove" a write that did not happen -- or report that a file still holds text it no longer has. Measured 2026-11-04: `[IO.File]::ReadAllText('stdlib/rulesets/transpose.cml')` read the main checkout while `Select-String -Path stdlib\rulesets\transpose.cml` read the lane's tree, and for one round the two answers contradicted each other (`pow_d_t` "still there" versus "gone") until both were given absolute paths. A safe read-back is `[IO.File]::ReadAllText((Resolve-Path -LiteralPath <absolute path>))`, and `git -C <tree> diff --numstat -- <path>` is the second opinion: a write that changed nothing shows a diff of `0 0` or no hunk at all.
- Prefer TDD: write tests before implementation whenever feasible.
- Place test cases under `test/`.
- For substantial refactors, update `docs/` in the same change set.
- **A commit that no remote branch contains does not exist for anyone else.** The last command of a round is `git branch -r --contains HEAD`: an empty list means the round's work is invisible to every other lane, however green its readings were. On 2026-11-02 four commits sat on a local branch while another lane reported, in good faith, that the rule it needed was not in the tree -- "I committed it" is not "it can be merged". Push the branch (or hand the merge lane a branch that is pushed) before saying a round is done.
- **Only a tip that `origin/<branch>` resolves to is a reading that can be merged.** `git cat-file -t <sha>` answering `commit` proves the object is in *this* repository and nothing more; `git branch -r --contains <sha>` printing nothing proves no remote branch carries it. Both together mean the reading cannot be checked by anyone else, which is the same class of problem as a change written into somebody else's checkout: the numbers may be right and nobody can reproduce them. On 2026-11-02 the fix for the Debug link error `typeinfo for camel::rewritemod::ProofObject` was reported at `41f2303d7` -- a commit that existed locally and that no remote branch contained. A merge lane's answer to "is this ready" is therefore `git ls-remote --heads origin | grep <branch>` and a `rev-parse` of that ref, never "the lane says its branch is at <sha>".
- **A merge runs three instruments first, and any one of them red stops the merge.** `node scripts/conflict-markers.mjs` for conflict markers -- all four forms: the head, the diff3 base block, the separator and the tail. The one-line `git grep -n '^<<<<<<< '` we typed by hand sees only the first, and on 2026-11-02 that missed a merge that had been aborted and committed as `407a7ee9f`, which left a base block in test/plans/feat/trans/cml.plan.toml -- a plan file that still parses far enough to be reported as a case. A bare separator counts only where a hunk head precedes it, because in Markdown it is a setext heading underline; `node scripts/diagnostics-catalog.mjs --check` for a code registry that still names what the tree emits; and `node scripts/check-doc-links.mjs --questions-index`, because `docs/handover/questions/` is a table whose row count is maintained by hand and whose pages move to `reviews/` when the author rules on them. The index rule is one line: **the page is generated, not written.** ode scripts/check-doc-links.mjs --questions-index --write builds it from the directory -- one row per page, the page title as the sentence, the header counting the directory -- and --questions-index without --write compares the file with exactly that text, so a hand edit is a red that names the line and prints both versions. The fix is always to regenerate. The author made this the rule at 11:10, after a night in which the same table broke three times in three merges (3 pending-index problems, a row for a page that had moved to reviews/, a header one behind the directory) and after he retired every hand-kept state table at 08:10: an index kept by hand beside a directory is two sources for one fact.

## Verification Style
- Always define explicit acceptance criteria for an implementation plan and validate against them.
- For every new feature or new test case, manually run the relevant command-line case first and inspect the raw execution behavior and output before trusting the automated test harness. Use that manual observation to decide what the later `verify_*` or plan assertions should actually lock down.
- A plan entry may use only the fields `docs/test-plans.md` defines. The runner rejects an unknown key (and a wrong type on the fields it types) with an error naming the plan file, the test and the key, and fails that plan; the other plans still run. Do not rename an assertion to something the runner ignores: it would pass while asserting nothing. `expect_stdout_exact` (or its legacy name `expect_stdout`) compares the whole normalized stdout; `expect_stdout_contains` does not.
  **The three stdout assertions, and what each one answers.** `expect_stdout_exact` -- is the whole output this text, character for character. `expect_stdout_contains` -- does the output contain this substring, anywhere. `expect_stdout_matches` -- does **some line** of the output match this regular expression (a search, line by line, like `contains`; `^`/`$` anchor a line, and an unanchored pattern is a substring of a line -- `24\.0` passes on `width 24.0 height 7`, `^24\.0$` does not). A pattern that does not compile, an empty pattern, and `expect_stdout_matches` written beside `expect_stdout_contains`/`expect_stdout_exact` are **plan errors** (`exit 2`, `plan:line`, nothing started): a plan states one thing about its stdout, not two whose order nobody wrote down. A legal pattern that matches nothing is a normal red, and prints the pattern, that it is legal, and the head of the output.
- When you add a plan field, add it to the runner and to `docs/test-plans.md` in the same change; when you remove one, remove it from both and fix the plans that use it.
- **Every reading carries its artifact.** A run prints, right before `[SUMMARY]`,
  `[provenance] tip <sha> | binary built <ISO> | tree <abs> | tier <fast> | jobs <n>`. Quote `tip` from
  the **tested tree's** `git rev-parse HEAD`, never from a branch ref, and never quote a sha for a
  dirty tree -- that line prints `(dirty: N files)` for exactly that reason. `binary built` is the
  executable's mtime, not the build log's: four rewrites in one night came from a correct number
  attributed to the wrong artifact (a stale `refs/remotes`, `build.ninja` for `build-Release.ninja`, a
  lane's readings filed as `develop`'s, a `-NoBuild` exit code read as a failure).
- **Probe artefacts live in `D:\Temp\`, never in the worktree.** An untracked probe directory
  makes `[provenance]` print `(dirty: N files)` on every run, and that flag carries a duty: it is
  how a reader tells whether a reading came off a clean tree. A flag that is always on has stopped
  being a flag -- a permanently dirty tree reports the same line as a tree someone has just edited,
  so the one run that matters reads like all the others. Measured 2026-11-04: a lane kept two probe
  directories under `benchmarks/` and `test/plans/`, and every reading it took that night said
  `(dirty: 2 files)` while the tracked files were untouched.
- **Before treating a red as a new defect, ask whether somebody already fixed it on another branch.**
  Twice in one night the answer was yes: `grad_array_sum` was about to get a probe written for it
  while the fix sat on `origin/lane-lit2` (`19f0b05e8`, `0aed45eb6`), and a `GUARD` question was
  answered the same way. The tool carries the rule so it stops depending on who remembers it:
  ```
  node scripts/find-fix.mjs test/plans/feat/compile/transforms.plan.toml:grad_array_sum,cotangent
  ```
  It fetches first, prints the branches that changed **that case's entry** since `origin/develop`, the
  commits on any remote branch whose message carries the case name or a keyword (each with the branches
  that hold it and `merged`/`not merged`), and the merged count. Read the exit code as the verdict:
  `0` found a candidate, `1` found nothing (so far a new defect -- an empty reading is not a pass), `2`
  could not read (`git fetch` failed, so `refs/remotes` may be stale and would report a false "no
  branch has it"). It writes nothing; `--offline` skips the fetch and says the reading is stale.
- **`null` is not an exit code; it is the absence of one.** A run that ends without an exit code ended in one of three ways, and the runner names which: `got signal SIGSEGV (rc=139, 0.03 s)` (the shell's `128 + n`), `got timeout after 30000 ms` (only a real timeout says this), `could not start: <message>` (the process never existed). `[SUMMARY]` separates the last two for `fail` counts: `, timed out: <n>` and `, killed by a signal: <n>`, printed only when one happened. **Read the exit line before reading a timing**: `got null` was read as a hang for two rounds while the process was dying of SIGSEGV in 0.03 s, and both directions of that diagnosis were wrong.
- **A layer's size is two numbers, and every run prints both**: `[tier] milestone: 27 plan file(s), 3330 declared test entries, 3404 started`. Read the second one as "what this run covers" and the first as "what the files declare"; quoting either alone as "the layer" is how `3330` and `3404` were both read as the same thing. `declared > started` is a defect (an entry that neither ran nor was skipped, so it is in no summary) and fails the run; `declared < started` is normal (`backends` expands an entry, a `compare_to` companion joins). Both are counted, never computed -- `backends.length` multiplication reads `7339` for a layer that starts `3404`.
- **"Full" is not a layer; say which one.** The same tree answers two very different numbers: the whole-tree **fast** layer (`--plan all --tier fast`) is the acceptance reading -- 8447 total, 8435 pass, 0 fail, 10 skip, 2 known failures on 2026-11-02, which is what `develop` itself reads -- while `--tier all` over the whole tree is 11874 and adds the milestone, perf and stress plans that "Run the plans your change touches" says a lane must not start. A report that says "the full suite" without naming the tier cannot be compared with another report, and the two numbers have been read as one thing. Quote the tier, the total, and the pass/fail beside it.
- **A guard must be able to fail.** Say in its comment which change to the tree makes it red, and record that red run (a plan case, a test, or the report with the failing output). A number a verdict rests on comes from the tree — the files, the parser, the registered operators — never from a literal list inside the guard; where a literal is a pin, a plan pins it so a change is visible. A guard that examined zero things fails; it never reports a pass.
- **Pin the words that separate the cases, not a word both of them can produce.** `bf16` contains `f16`, and the reduced-precision diagnostic prints one help paragraph for both types ("bf16 and f16 are floating types of the language"), so `expect_output_contains = ["bf16"]` stays green even when the message names `f16` — a guard that cannot fail, measured: the bf16 case swapped to `f16` still passed 44/0. Pin the phrase only one of them can print (`element type 'f16'`, `element type 'bf16'`) and record the swap that reds it (43/1). The same trap is any short word that is a substring of its sibling: a pin of `f32` is satisfied by `f32[]`, and a pin of `NaN` is satisfied by `NaN` in a legend.
- **A probe of a run-time overflow on a narrow type cannot be written as a literal expression.** `let x: f16 = 65504.0 + 65504.0` is refused at compile time ("the literal '65504.0 + 65504.0' cannot be represented as type 'f16': the value 131008 is out of range"), because the constant folders check the folded value against the declared type — the infinity rule of the element type never runs. Put one operand in a variable (`let max: f16 = 65504.0; let over: f16 = max + max`) so the sum is a run-time operation; that is what answers infinity rather than saturating.
- **A migration changes what a fixture means, not only what it checks.** When a name moves from one mechanism to another (a builtin module becomes a declaration module, an operator stops being computed in C++), every test that used that name as an *example of the old mechanism* is now testing something else, and it will keep passing or start failing for a reason unrelated to its title. Before merging a migration, grep the tree for the migrated names and read each use: the pass-resolution diagnostic, the LSP's "computed member" examples, and a pinned count of declarations are the three that actually broke. A pinned count that moves because of a migration is updated to the measured value in the same change, with the reason; a guard whose subject changed is retargeted to a name that still exercises the mechanism, never deleted.
- **A rule shared by several back ends or paths has one test that runs on all of them.** Otherwise each platform exercises a different implementation, or the check covers one element type and the bug lives in another. Wrong: two interpreter sources of which each platform compiles one, tested through whichever was built; a gradient check on f32 only. Right: one case parameterized over the back ends and element types, with the outputs compared.
- **"Zero hits" is a reading only when the commands and the exception are printed.** An audit that
  reports "no such site" proves nothing; one that prints each `git grep` it ran, the count it
  returned and an explanation of every non-zero line does. Measured 2026-11-02 on the
  `nn` bytecode poison: five searches over `modules/nn` (internal pointers, tensor data pointers,
  higher-order element calls, operator/function entry points, raw pointer locals) returned
  0/0/0/0/1, and the single `auto *tuple = ::Tuple::create(3, mm::autoSpace())` was explained as a
  freshly allocated object written flat inside the same call, which rule 1 allows. The audit was
  then a reading rather than an absence. The same shape applies to a positive result: a cached
  address that crosses a safepoint is proven by the three lines *taking* it, the call that reaches
  the safepoint and the line *using* it, not by naming the file.
- **Before auditing an execution path, paste the `args` line of the case that failed.** Three times
  in one night a lane audited the wrong path because it inferred the back end from the test's name:
  a poisoned build (the instrument was not the one believed), `verify=1` (two trees, two readings,
  both true), and a `--tf bytecode` that the plan text did not say. The plan entry's `args` array is
  the one answer, and it costs one command.
- **A `known_fail` mark has one spelling per shape of the entry, and the runner refuses the other.** With `backends`, a list of those back ends; without one, `known_fail = true`; mixing the two shapes is refused at load, by name (`docs/test-plans.md`, `known_fail`). It is written this way because a mark read by nobody is worse than no mark: it looks like a decision, and the case it mutes goes on failing in the open. Read `known failures: N` together with `skip N` before calling a rise a regression -- a mark only applies to a run that happens, so a case that starts running can turn its mark into an `[XFAIL]` for the first time (2026-11-02: `skip 43 -> 10` and `known failures 3 -> 6` in one run, nothing regressed).
- **A plan that starts no test proves nothing, and the runner says so.** A plan file whose own `tier` is the layer being run, and from which that layer selects none of its entries, fails the run by name; so does a run that selects no test at all. A plan whose own tier is another layer is counted on one line (`plans of another layer, started nothing here: N`) so its zero is a reading, not an absence. A plan file named on the command line still runs in full, whatever `--tier` says, so a named plan that starts nothing is a plan with no entries. Written because two of `jit_framed`'s milestone entries had never once run, and their red and their green were both worth nothing until someone proved they started.
- **Do not draw a conclusion from the twelve-line tail of a failure.** The log's `[stdout]`/`[stderr]` block is at most the last twelve lines, and only with `CI` or `CAMEL_TEST_SHOW_OUTPUT` set; the line that says what happened is regularly above the cut. Every failed test's log entry carries `[INFO] full output: test/results/log/<run>/fail/<plan>/<test>/ (stdout.txt, stderr.txt, output.txt, meta.json)`, relative to the checkout, and that file is the whole of it. Twice on 2026-11-02 a conclusion was drawn from a truncated fragment and was wrong both times. A run directory that recorded a failure is never pruned by the retention bound, so the file is still there tomorrow.
- **On a shared host, kill by identity and never by program name.** `kill <pid>` for a process you started and noted, or `pkill -f '~/camel-work/<your worktree>/'` with a path prefix that carries your own tree's name. `pkill -f '<script name>'` (`run-tests`, `node`, `camel`) is forbidden: on 2026-11-02 one such command was issued while five other lanes had runs in flight, and they survived only because `pkill` killed the issuing shell first. Every kill is followed by a `pgrep` pass over the same selector, with the survivors' pids recorded: the check is part of the command, not an optional courtesy.
- Start with small inputs when debugging high-volume workloads such as `fib 30`.
- **Before symbolizing an offset, check that the symbol you looked up is the function you think it is.** A backtrace line reads `NodeVMSchedPass::call(...)+0xd57`, and `nm | grep call` happily returns a different `call`; the offset then lands inside an unrelated function and the disassembly "proves" something about code that never ran (2026-11-02: `0xa80180+0xd57` fell inside `bindDirectCallFrameSlots`, and the right base was `0xa813d0`). Print the resolved name next to the address (`nm -C --defined-only <lib> | grep '<full signature>'`) and read it back before computing anything.
- **A probe is proven twice before it reads: it is in the binary, and its branch is reached.** `strings <lib> | grep -c <marker>` answers the first; a run that prints nothing answers nothing until the second is answered separately. Twice in one night a probe "found nothing" because the branch never executed -- eight numbered writes in the nvm interpreter printed zero lines while the same run emitted the collection's own diagnostic -- not because the defect was elsewhere. Number the candidate sites and print the number, so "which one" is the program's answer rather than the reader's inference.- **`BUILD_EXIT=0` is not "the binary was rebuilt".** After a probe build, prove the probe is in the artefact before taking a reading from it: `strings <lib> | grep <marker>` (measured 2026-11-02: two probe builds reported `BUILD_EXIT=0` and had not relinked — the library timestamp had not moved and the marker was absent, so the run measured the previous sources). The three instrument failures of that night are one family: a probe that prints once and never again is not "clean here", a probe that does not compile is not a print, and a build that succeeded without linking is neither.
- **The remote wrapper will not build a detached HEAD.** `git bisect` detaches, so `camel-remote.ps1` answers `[camel-remote] detached HEAD in h-guard; check out a branch first` and stops before the build. A bisect drives the wrapper through a temporary branch per step (`git checkout -B <probe> <sha>`), never through a detached checkout.
- **A commit in the middle of `develop` may not compile, so a bisect must skip it.** Measured 2026-11-02: `1eb83d71d` declares `addFuncNode` with five parameters (`include/camel/runtime/draft.h`: `GraphId, Type *, uint8_t runtimeFlags = 0, gc_slot_idx_t dataIndex = 0, uint32_t writtenCaptures = 0`) and defines it with four (`src/runtime/draft_construct.cpp#GraphDraft::addFuncNode`), so the out-of-line definition matches no declaration and the build stops. `git bisect skip` is the tool for that bin, not an abandoned bisect; and the commit is history, not something to fix.
- **`camel --version` names the commit the binary was built from, so a step can prove its own artefact.** The line reads `Camel v0.2.41 (Build 261102_111500_1de967a2a)`, and its last field is the short sha: compare it with `git rev-parse --short HEAD` before taking a reading, and write both down beside the numbers. Measured 2026-11-02: four probes whose artefact matched all read a flat ratio and one step, while the round before them (no such check) produced a chain that a four-row table deletion could not explain.
- **A bisect step is a measurement, so it carries the artefact rule: prove the binary is the one you checked out.** Steps that skip that proof read the previous step's artefact and the chain comes out monotone for the wrong reason (2026-11-02: two adjacent probes read `alloc 4.49 / soak 7.29` and `4.31 / 7.19`, and a four-row table deletion cannot move either number, so at least one of them measured a binary that had not been relinked).
- **Two runs at once share one `out/latest`, and the failures the second one reports are phantoms.** The remote wrapper **recompiles `out/latest` on every invocation**, so what two overlapping runs share is not CPU but the binary: a second invocation started while a first is still testing swaps the artefact under it: measured 2026-11-02, a full `--tier fast` run reported 17 failures in `parse.rulings` and `boundary32`, all "expected exit 0, got exit 1", none of which reproduced when the same plans were run alone (45/45; 8506/8492/0 on the clean re-run). The same rule covers any measurement that pairs "build" with "run" -- one wrapper invocation at a time, and a second measurement waits for the first to say `[SUMMARY]`.- Start with small inputs when debugging high-volume workloads such as `fib 30`.
p
- Use `std::gir` for graph inspection, `std::nvm` for baseline runtime behavior, and `std::fvm` or `std::jit` for scheduler/runtime verification when relevant.
- Prefer routine verification in `npm run build`; use `npm run debug` only when diagnosing logging or debug-only code paths.
- **Drive the compiler with absolute paths and `cwd` at the repository root.** A case given as a relative path resolves its `import`s against the wrong directory: `camel test/cases/compile/enum/cross/use.cml` searches `.../compile/enum/cross/test/cases/compile/enum/cross` first and reports `ModuleNotFound: 'shapes'`, where the absolute path finds the module and the printer's real verdict is `cannot print shapes::area: the graph name ... is not a source identifier`. The search root is built from the case's directory joined onto the working directory, so a relative case path is joined twice. Measured on 2026-11-02 over the corpus of 3377: the same survey reads `printed=559 refused=1814 frontend=1004` with absolute paths and `printed=551 refused=1701 frontend=1125` with relative ones -- **192 of 3377 cases answer differently**, 113 of them falling from a refusal to a front-end error. `test/tools/cml-sweep.mjs` does this correctly (absolute `ROOT`, `cwd: ROOT`) and now refuses to run if it is handed a non-absolute root, so the wrong invocation cannot produce a number at all. **A reading taken with the wrong invocation is not a harder-to-read reading, it is a different reading**: the wrong path form changes the verdict on 192 cases, so the two columns are not comparable and a difference between them is not a regression.
- **Run the plans your change touches, not the whole tree.** The full suite (`test/plans`), the GC poison sweep, the GC stress plans (`gc.collection_trees_*`, `gc_elder_*`), the progen random-program checks, the PyTorch comparisons and the perf/timeit plans are heavy: they saturate every core for minutes and are worth running only at a milestone, by the coordinator, on a quiet machine. During development a targeted plan list plus `smoke` is the expected verification. Never start a full-suite or stress run to "be safe", and do not leave one running when you stop.
  **What the milestone layer actually is, and what it looked like when it was first
  measured (2026-11-02).** `--tier milestone` selects 27 plan files: `tier` is a
  property of the *file*, so naming a file runs it whole and the layer is that whole batch. The
  layer is now read off the tree rather than off a table: `plan-inventory.mjs` counts every test
  whose own tier says `milestone` -- the batch `--tier milestone` selects -- so a plan that grows
  one is counted the day it lands. (Those files declare 3330 entries; a run of the layer reports
  `tests: 3404`, because back ends expand and `compare_to` companions join the run. The file count
  and the run count are two subjects; the gate prints the first and says "test entries".) That
  day's run gave `total: 3390, pass: 3369, fail: 20, skip: 1` -- 16 of the 20 are real reds and 4
  are budgets, below. Nobody had run this layer before, so a red here is not evidence that today's
  change broke it: two of them (`gc.remembered_set_minor_copying`, `gc.foreign_resource_lifetime`)
  are self-tests that shipped with the features they test and had never executed once.

  **Its budgets are calibrated for 8 concurrent jobs.** `progen_check` (132.9 s of 150),
  `collfuzz.many_small` (154.7 s of 151), `rules.every_exported_rule_enters_the_check` (132.4 s)
  and `small` (6.19 s of 6.00) are not slow machines: they sit against their ceilings, so a run
  that is wider or narrower than 8 jobs has to re-calibrate them rather than read them as red.


### `known_fail`, as the runner reads it

| the entry | write |
| --- | --- |
| has a `backends` list | `known_fail = ["<be>"]`; a name that is not a back end is refused: `known_fail names 'fvm', which is not in backends of …` |
| has no `backends` list | `known_fail = true` |
| mixing the two | refused: `known_fail on <test> names back ends (["fvm"]), but the test has no backends list, so no back end of that name runs here: write known_fail = true` |
| `known_fail = true` with a `backends` list | refused: `name the back ends that fail` |
| a mark that starts passing | `marked known_fail but passes, so the mark is out of date and must be removed` |

(source: `test/tools/run-tests.mjs`, `expandBackends` / `__knownFail`, commit `a01fcdfdc`)

**The milestone numbers carry a tip.** `--tier milestone` is 27 plan files and on the order of 3390
tests, and `-Plan <a subdirectory>` still covers the whole layer, because `tier` is a property of the
file. The first run of this layer, on 2026-11-02, was `total 3302 / pass 3286 / fail 16` on
`2c1a7b6c4`; on `37dcd074a` it is 15, and the two that left are the GC self-tests whose thresholds
and collection counts the field could not reach. A count without a tip is not a count.
## Killing a stale run on the shared host

Two ways to name a process, and only two: `kill <PID>`, or `pkill -f '~/camel-work/<your own tree>/'`.

`pkill -f 'run-tests.mjs'` and `pkill node` are forbidden. On 2026-11-02 one lane used the first while five lanes were running: every one of them was inside that pattern, and only the accident that `pkill` matched the killing shell's own `bash -c` first kept them alive; a `pgrep` afterwards found all five still running. **Alive was luck, not judgement.**

The same day made the other half of the point with a check instead of an accident: with the "zero tests started" gate in place, 31 of the tree's 309 plans turned out to be silent zeros - their tier puts them out of an ordinary run, and until the gate existed they said nothing at all - and the first real defect came with them, `test/plans/feat/trans/cml_sweep.plan.toml` missing a plan-level `tier`, calling itself fast while its only test is slow, reddening the whole fast pass. **Green because nothing ran is not green.**

## A host checkout name is a lane's identity, and two runs under one name overwrite each other's reading

`camel-remote.ps1` keeps one checkout per worktree name on the host, so the name is the only thing
that says which lane a build directory belongs to. Two lanes that pass the same `-Path`, or one
session's two runs on two branches, take turns on one directory: the checkout and patch phase of
whichever starts second rewrites the tree the first one's plan phase is still reading, and the build
directory stays incremental across both.

2026-11-04 measured how quietly that goes wrong. A lane's confirmation run on `m-train` started at
19:47 with `-Plan all -Tier fast -Jobs 8`; a second run under the same `mtrain` name started at
19:54 from another branch of the same repository. The first run finished with an entirely ordinary
line -- `[SUMMARY] total: 8804, pass: 8759, fail: 2, skip: 13, known failures: 30`, killed 0, timed
out 0 -- and only the line above it disagreed: `[provenance] tip 5d3017ca2…`, a commit the measured
branch never contained. A summary reading 2 where the same tree read 11 an hour earlier is not
evidence of progress; it is evidence that somebody else's tree was measured.

So **give every lane its own checkout name** (`mtrain`, `b3full`, `devbase`), and when a
`[provenance]` line names a tip this tree does not contain, throw the reading away: do not reconcile
it, do not ask whether the difference is explainable. Re-run under the lane's own name. The number
then is honest and the price is one run; a reconciled number becomes a fact in the next report and
nobody can tell afterwards that it was never measured.

## 闸门的入口只有一个文件

> 闸门入口只认 `internal/tools/camel-remote.ps1`；`<worktree root>/camel-remote.ps1` 是历史遗留副本，不要用

The second copy is not at a worktree root any more, and that matters to the reader: the only one left
on this machine is `D:\Projects\Camel-wt\camel-remote.ps1`, in the directory that *holds* the
worktrees. No checkout contains one -- a recursive find under `D:\Projects\Camel-wt` returns nothing
inside a tree, and a freshly added worktree has none -- so a sentence that names only "the copy at the
worktree root" sends a reader looking for a file that is not there. It is also not a file of this
repository: `git ls-files` names only `internal/tools/camel-remote.ps1`, and `git log -1 -- <that path>`
answers `is outside repository at ...`, so no commit can delete it and no checkout can put it back.

What it is: the wrapper from before the machine-specific values moved into
`internal/tools/camel-gate.local.json` and before the `-NoBuild` guard could name the build
configuration. It is 201 lines shorter than the entry (1275 against 1476) and is **wider** than the
entry, silently, in three measured ways: its guard has no `--config` to pass, so `-Tier debug -NoBuild`
verifies and accepts an install built Release (the fingerprint it compares is a Release one), and the
run then answers a question about a binary the tree never built; it does not bring the plan's stderr
back, so a plan that wrote only its reason to stderr arrives as a bare non-zero exit; and it carries
`$sshHost = '88zjwei'`, `$bare = 'camel-work/Camel.git'` and the libstdc++ directory as literals, so a
run from another machine reaches the host it names -- where the entry stops with exit 2 and names the
setting it could not find. The copy's own header says none of this, so a lane that runs it believes it
ran the gate and reads an older, laxer number as this tree's.

Inside this tree the one caller was `scripts/bisect-merge.mjs`: `#parseArgs` defaulted `--wrapper` to
`path.resolve(<tree>, '..', 'camel-remote.ps1')`, which for any tree under `D:\Projects\Camel-wt\`
resolved to exactly that file, and `#measure` runs the result with `pwsh -File` -- so a bisect took every
reading through the old copy while printing ordinary `[camel-remote]` lines. The default now resolves
`internal/tools/camel-remote.ps1` from the script's own location, so it no longer depends on where the
worktree sits; `--wrapper` still overrides it.

The copy itself is documented here instead of deleted because it still has callers outside this
repository: the machine-local helpers beside it (`bisect2.ps1`, `h598-reds.ps1`, `h585-gir-walk.ps1`,
`.h552-verify.ps1`, ...) hardcode the same path. Deleting it would break those, and the break would read
as "the gate is missing" rather than "you named the old one".

Every command example in this repository names the entry, and names it the way the entry's own header
does: `internal/tools/camel-remote.ps1`, relative to the repository root. None of them points at
`Camel-wt\camel-remote.ps1` any more -- a page that did sent the next reader to the old readings while
saying, in the same paragraph, that the copy was old. A page that records a past measurement may still
say which copy *was* used there; that is history, and rewriting it would be the falsification this
section exists to prevent, not a fix.

Point `-File` at the tree's `internal/tools/camel-remote.ps1`, and read the readings of a run whose
command line did not: `[camel-remote]` lines are printed by both copies and do not say which one printed
them.

## A revert that covers part of a change reads as "the rest of the change is still in the tree"

A control experiment is only as wide as the diff it actually reverted, and a partial revert answers a
different question than the one it looks like it answers: it measures **what the part left in place
still does**, never **whether the change was the cause**.

2026-11-04 paid for this twice. A change made of two commits (`bb432e8ae` + `821f30c83`, five files)
was suspected as the source of ten `cotangent-needs-a-value` failures. The probe reverted one of the
two commits: two of the five files. It read `fail = 10` and was reported as "the change explains one
of the ten, the other nine are pre-existing debts". A second lane reverted the change whole -- five
files -- and read `fail = 2`: the eight names the half revert left red were that change's own
regressions, never debts at all. The half-revert number looked exactly like a complete verdict, so it
was cited as one, twice, by two different readers.

So a control experiment states **which diff it reverted, file by file and commit by commit, and
whether that is the whole change**; and a number from a partial revert is never compared with a number
from a full one. If the change is made of two commits, revert both or say in the first line that the
reading is about the remainder. **A number that answers half the question is not a small answer, it
is an answer to another question.**

## A guard that reports and then reads on can turn a red into a green

`Tuple::updateRefs` compared the value's member count with its type's and, on a disagreement, printed
a line and traced on. Under that behaviour `poison:transforms:vjp_api.carries_its_own_ruleset.nvm`
**passed** the full poison sweep. When the guard learned to stop instead of reporting
(`members <= tupleType->size()`, author's ruling 06:30), the same case failed with that guarantee and
`has 3 member(s) but its type declares 0`.

So the green was a read past the end of the type's array whose garbage code happened not to name a
traced kind: the case was never right, it was lucky. Two things follow. A check with two behaviours
(report-and-continue, report-and-stop) has two different green sets, and **only the stopping one can
say "this never happens"** -- a report-and-continue guard's silence means "nothing crashed", not
"nothing disagreed". And when a guard is strengthened, the **gains and the losses both have to be
re-measured**: a case that turns red is a case that was green for the wrong reason, not a regression.

## A core pass needs a declaration in `stdlib/std.cml` before a plan can name it

Registering a kernel in `src/execute/pass.cpp` (`registerCoreKernel("gc::tuple_members", …)`) is half the
job: the pipeline resolves `std::gc::tuple_members` through **`stdlib/std.cml`**, so a kernel with no
`export func tuple_members::<F>(f: F): F! when is_function::<F>()` line in that file is reachable from
C++ and from no test, and the run says

```
error[RuntimeDiag::InvalidPassParameter]: Invalid pass specification 'std::gc::tuple_members_agree':
module 'std' declares no pass 'gc::tuple_members_agree' in <install>/stdlib/std.cml
```

`gc::poison` is declared there for the same reason (`stdlib/std.cml`, "Runs the allocator's poison
check"). Two things follow: a plan entry and a manual run both need the declaration, and a self-test
whose point is that it is *never* reachable from a plan has to be reachable to be tested at all.

## The instrument's range is narrower than the fact

Three zero-hit readings on 2026-11-02 were all the same mistake, and none of them was a broken branch:

- `git branch -r --contains <sha>` reads **the local checkout's `refs/remotes`**. When those are stale, a branch that was pushed correctly looks like it does not exist; four tool commits were reported as "on no remote branch" while `origin/lane-a3` had held them all along, and `git push` answered `Everything up-to-date` because there was nothing to push. **Either `git fetch` first, or ask the server: `git ls-remote --heads origin <branch>`.**
- A build artefact was searched for in `build.ninja` while the build wrote `build-Release.ninja`: the grep was right and the file was the wrong one.
- A `git grep` for a tool's own message hit nothing because the main checkout was three commits behind the tree that contained it, so "the tool isn't there" was really "this tree predates the tool".

**A zero is not a reading until the search space has been shown to contain the thing.** State the command, the file or the ref it read, and which tree that ref or file belongs to.

The same is true of a non-zero that arrives too easily. `(Get-Process -Id $pid).Threads | % StartAddress`
answers `RtlUserThreadStart` -- an address inside ntdll -- for **every** thread of a process,
including the main one and every thread the program started itself, because that is the stub every
Windows thread begins in rather than the routine the caller passed to `CreateThread`. A count built
on it reads "this process is nothing but operating-system pool workers", which is not a fact about
the process at all. `test_support`'s `thread_count` asks instead with
`NtQueryInformationThread(ThreadQuerySetWin32StartAddress)`, which reports the start routine the
thread was created with: on `-t 1` the same camel process is 4 threads, of which 1 is the run's own
(address outside ntdll) and 3 are the operating system's (address inside it), so the answer is 1,
which is what `count.timeit_cli_one_serial` expects. **When a Windows API's reading looks
surprising, check whether it is the API that answers a different question than the one you asked.**

## A fixture that writes a path into Camel source writes forward slashes

The author's ruling of 2026-11-02 13:10, in response to the `interpreter_first_use` red: **a fixture
that writes a platform path into Camel source text writes it with forward slashes, or with a writing
the language does not unescape.** A Windows path in a double-quoted Camel string is not text -- it is
escapes: `\t` and `\r` are legal escapes, so `D:\temp\run-1/a.svg` becomes `D:` + a tab + `emp` + a
carriage return + `un-1/a.svg` **silently**, and `\C`, `\m`, `\i` are unknown escapes that only warn
(`LexicalDiag::InvalidEscapeSequence`, `docs/language/02_lexical.md`, an error or a warning still open
with the author). The warning does not save the path: the two escapes that broke the image were the
legal ones.

The idiom is one line, at the place the path is made:

```js
const CML = SCRATCH.replaceAll('\\', '/')   // what goes into the program text
```

and `path.join(...).replaceAll('\\', '/')` when the path is built inline, as `pass_param_effect.mjs`
already did. Node-side reads and writes keep the platform path; only the text spliced into a program
uses the forward-slash one.

**This cannot be checked by reading the fixture, which is why the rule is here and not in a guard.**
On 2026-11-02 `test/cases` held 26 fixtures that build Camel text, 15 of them writing a `.cml` file,
and exactly two spliced a path into the text: one wrong (`${SCRATCH}`) and one right (`${CML}`). The
two have **the same static shape** -- a template splice naming a variable -- and differ only in a
value that exists at run time and only on Windows. A checker over the text would report both or
neither. The shape that *can* be seen at run time is the child's own warning line: a fixture writing
a Windows path into a program makes that program print `Invalid escape sequence in string` on stderr,
which is where a guard would read it (on the Windows leg, since a Linux path has no backslash).

## A line count is a `\n` count, and the number carries its command

Two instruments answer "how many lines" differently, and on 2026-11-02 both were used on the same
files within an hour:

- **PowerShell's `Measure-Object -Line` skips empty lines.** On one working copy it read
  `split_operator.cpp` as **190** where the file has **209** `\n`s, and `jvp.cpp` as **1221** where it
  has **1245**. Nineteen and twenty-four lines of difference are just the blank lines.
- **`wc -l` counts `\n`s**, so a file **without a trailing newline** reads one short of its visible
  line count: `modules/transforms/split_operator.cpp` is 209 by `wc -l` and 210 lines as `cat` shows
  it.

**So: count with `\n` (`git show <sha>:<path> | wc -l`), never with `Measure-Object -Line`, and paste
the command beside any number quoted in a report or a page.** Two instruments, two numbers, one file:
that is enough to make every later reading untraceable, and a line count is exactly the measurement
that looks too simple to get wrong.

**The same error one layer down: a line break inside a path is two citations.** `docs.links.citations`
went red on a page name wrapped across two lines: the checker read the fragment that began the second
line as a file name and answered `no such file`. The physical shape of the text was read as its
meaning, the same mistake as a line count taken from the wrong instrument. Keep a path on one line
whatever its length; a checker that answers `no unverified citation` is the reading that it is whole.
This paragraph does **not** reproduce the broken shape -- writing it here would trip the same check,
which is exactly the point.

## A new plan needs a selection rule, or `--changed` can never run it

A plan file added to `test/plans` is only reachable by an ordinary change if `test/plan-selection.json`
has a rule that names it from the paths its cases live in. Without one the plan exists, runs when
named, and is **invisible to `--changed` and to the layer accounting**, which is what the runner's own
book says in one line:

```
plan-selection: 1 plan file(s) no case path selects, so --changed can never run them:
test/plans/feat/tools/plan_normalizers.plan.toml (its cases are under test/tools/).
Add a rule that names the plan for the directory its cases live in.
```

The instance on record: a guard plan whose case is `test/tools/check-plan-normalizers.mjs` landed with
no rule. Its two entries were green when named and the result was `runner.backend_matrix` red on the
next full run — the book's `plan-selection` check has no other place to report it. The cure is one
entry in `rules` (the tool's path to the plan) plus the plan's name in the rule that owns
`test/tools/selftest/`, because the guard reads fixture trees there.

The same applies to a plan that must stay out of an ordinary change: `SLOW_PLANS` and the selection
rules are two halves of one fact — a plan is either reachable on purpose or named in the slow list on
purpose, never neither.

**`changed: 0 target(s)` is a reading, and it means nothing was tested.** `--changed` starts no plan
when no file differs from the base, and prints that count instead of running the layer: 0 changed
files used to widen to 336 plan files and a `[SUMMARY]` with no failure — the reading a correct
selection of one plan gives — so the widening had no symptom. A lane that sees the line is in one of
two situations, and neither is a green: its work is not committed or not on the branch yet, or it
named a base the branch equals. Name the plans (`-Plan smoke,test/plans/feat/...`) or `--all` when the
layer is the question; the layer is never run to fill a selection.

## A timeout budget is a change like any other

A `timeout_ms` in a plan file is not housekeeping: it decides whether a slow machine reports a red or
a slower red. So it goes through a lane branch like code does, and its commit says **which red goes
away because of it and what the two-core CI measured in CPU**, because those are the two numbers that
tell a budget that was too small from a test that got slow.

A plan edited in the main checkout, or in someone else's worktree, is a change with no author and no
reading: on 2026-11-02 five plan files with raised timeouts appeared in the main checkout that way
(`diag_catalog`, `new_values`, `case_ledger`, `plan_inventory`, `trans/cpp`), quoting two-core CI
figures in their comments. The content may well be right; it still cannot land, because nobody can say
which red it removed. The patch is kept outside the tree, and whoever wrote it re-does it on a lane.

**Where the number comes from**: a `liveness` bound is measured with `--jobs 1` **on the gate host**,
and the bound is **four times the slowest reading there**; a reading taken on any other machine is a
reference, never the figure the multiple is applied to. A machine that is not the gate host can be
several times slower, and the budget then carries a margin nobody can see: `98a500e4f` raised this
self-test's bound to 240 s as four times 45.8 s read elsewhere, and the same self-test reads 68.57 s
of CPU on the gate host -- 3.5 times, not four. Quote the host and the layer beside the number, and
when two readings disagree, the gate host's is the one the bound is built on.

## `-NoBuild` after a merge: `exit 4` is the runner refusing, not a failing test

After a merge the tree no longer matches the installed binary and `out/latest/stdlib`, and
`camel-remote.ps1` says so and stops:

```
The plan would run the old binary and the old out/latest/stdlib, not this tree.
Re-run without -NoBuild (it builds this tree and stamps the install), or pass -AllowStale to run anyway.
[camel-remote] h-m4b exit 4 (transfer 2.8 s, build 0.2 s; total 3 s)
```

**`exit 4` means nothing ran.** A log with no `[SUMMARY]` is not a run that passed or failed; it is
a run that did not happen, and reading it as a failure sends you off to diagnose a test that never
started. This is the instrument doing its job - it refused a run of the old binary that would have
produced a green about a tree nobody built - so it belongs here as correct behaviour, not as a trap.

A red also changes hands when the bookkeeping changes: the milestone layer s last red,
`rules.every_exported_rule_enters_the_check`, disappeared in a merge that touched neither the test
nor its plan file (`git log 4fa772787..d526043da -- test/plans/feat/modules/rules.plan.toml` is
empty, and the plan runs 173/173 on `2227c21c1`), because `a01fcdfdc` changed how `known_fail` is
read. **A count belongs to a tip and to the rules that counted it.**
## `[code=127] ccache: not found` is a host fault, not a failing test

**看到 `[code=127] ccache: not found` 或 `cannot open shared object file` ⇒ 这一轮的 `BUILD_EXIT=1` 不是读数，丢掉重跑。** 先答 `~/camel-work/bin/ccache --version`：答版本就与本条无关，答 `exec: ...: not found` 或 `cannot open shared object file` 就是宿主那条（记录见 `docs/handover/reports/2026-11-04-the-host-ccache-lost-its-exec-target.cn.md`）。只走 stdlib/文档/用例的增量跑一次编译都不发生，所以"同机有的车道绿、有的 127"是正常的，别据此改自己的代码。

## `got null` is not a timeout

The runner writes `expected exit 0, got null` when the process died on a signal and no exit code
came back. Two marked cases of `transforms.plan.toml` were read as hangs for a day; run bare with a
timer they take **zero seconds** and return **139**, which is SIGSEGV. **Run it bare once before
explaining it:** a `got null` has at least three causes - killed at the timeout, killed by a signal,
and whatever nobody has seen yet.

## Before asking whether a failure is new, ask whether someone already fixed it elsewhere

`git log --oneline origin/develop..origin/<candidate lane> --grep=<keyword>`. One grep is cheaper
than an hour of probes: two cases that crashed in `jvp_add` and `jvp_mul` were already repaired on
`origin/lane-lit2` (`19f0b05e8`, `0aed45eb6`) and the fix had not been merged, so the failure read
like a new defect in the tree under our hands.
A green run proves the `GUARD`s it walked past, not the `ASSERT`s it compiled out: `GUARD` is defined before `#ifdef NDEBUG` and its comment says it is alive in the build that ships, while `ASSERT` becomes `((void)0)` under it.

When the instrument gets finer, past agreement has to be rejudged: four `known_fail` notes were audited as `same` while the runner printed `got null` for every signal death, and the moment it could say `SIGSEGV (rc=139, 1.24 s)` the same notes read `different`. A `same` is only as good as the reading it was taken on.

Ask whether a thing is there and the answer is a boolean; ask how many lines carry it and the answer is a reading. A boolean is wrong once and the table is wrong with it: an index said three pages while `Contains` said the new row was already in place, and counting the rows found two.

A probe log that records only `[FAIL] <name> <ms>` can count failures and cannot group them: the first diagnostic line is what turns forty reds into one root cause or ten. Print that line, and print it even when jobs run in parallel.

## A check only a Debug build's standard library performs is not an instrument

`std::vector::operator[]` past the end is undefined behaviour, and what reports it is the standard library of the build you happen to run: a Debug build's (`_GLIBCXX_ASSERTIONS`) or an ASan build's. On 2026-11-02 `feat/compile/stream.plan.toml` was **green in Release and red in 24 of its 143 tests in Debug**, all from one line: `settleStreamLinearity` asked `draft.region(draft.nodeRegion(node))` for region 0 of a draft that has no regions at all. Release built the reference, found the loop's carrier list empty, never read through it, and passed -- **that run was not a weaker reading of the fact; it was no reading at all.** The answer is not "add an `ASSERT` there", which is compiled out and leaves the next Release run green by the same mechanism: state the precondition where the answer is unknown (`GUARD` in `GraphDraft::region`, alive in the build that ships) and let the reader that starts from `nodeRegion` ask `regionCount()` first. The leg that has to see it already exists: `ci.yml`'s sanitizer job runs `smoke`, `feat/compile/stream` and `feat/compile/transforms` on a Debug ASan/UBSan build.

## A pointer that answers "which graph" answers with an address

`nodeGraphOf()` cannot return null -- its `GUARD (builder != nullptr)` fails through `fail_invariant` first -- so none of its **61** call sites needs a null branch, and that is the point: the guard turns "no answer" into a stop. But 14 of them only **compare** two answers for identity (`builder_helpers.h`, `builder_expr.cpp`, `builder_control.cpp`, `builder_operators.cpp`, `captures.cpp`, `builder_support.h`), where a null would be worse than a crash: `nullptr == nullptr` says "the same graph". When one of those wants to be sure two handles name one graph, ask the draft whether it is the one the handle names (`isNamedBy`, address **and** epoch, which `3609d015a` made the identity) instead of comparing the address a handle happens to carry.

- 主检出 `D:\Projects\Camel` 只用来 `pull --ff-only`；所有编辑都用工作树内的相对路径，或 `D:\Projects\Camel-wt\<lane>\...` 的绝对路径。写完用 `git -C <树> status --porcelain` 自检，不要用 `git status`（它按进程 CWD 解析，会看到另一棵树）。
## Check the input before you believe the reading

A measurement is a reading of two things: the program and the input. When they disagree with
expectation, the input is as likely to be wrong as the code, and an input that is silently the wrong
file looks exactly like a defect in the compiler. Before taking a reading on a file that was copied,
extracted from another branch or written by a script, check three things: **the size**, **the first
bytes** (`head -c 32 | od -c`), and **the top-level statement count** (`camel-parsecheck dump <file>`
prints `Stmts*` with its count).

Two readings of 2026-11-02 were wrong for this reason and neither was a code defect:

- `--write` "needs two passes": it was run once before the plan entry that names the new diagnostic
  existed, so nothing could be pinned. The writer is idempotent (two runs in a row write nothing).
- "The reported case still ends with `EntryModuleMissingMain`": the file measured was 458 bytes of a
  709 byte test case left behind by a copy, and a truncated file **parses as `ACCEPT` with an empty
  module** (`Module @0..0`, `Stmts* @1..2`), so the module had no declarations and the entry had no
  `main`. Written byte for byte from its branch (472 bytes) the same input gives the diagnostic the
  fix had already added.

**An `ACCEPT` from `parsecheck` proves the text it was given parses, not that the text is the file
you meant to give it.** A parser that accepts a truncated file is not evidence of completeness; the
size and the statement count are what cross-check it.

## Deleting A Named Example Moves Its Class Count
`scripts/stdlib-reference.mjs` keeps `EXAMPLE_STOPS`: for each class of doc example that cannot run,
how many are in it. Deleting an example -- with the declaration it named, or because the shape it
showed is gone -- makes that class one smaller, and the count is the only thing that notices. Leave it
and `registry.docs_complete` goes red **on an otherwise clean `develop`**, which reads as a defect in
the compiler rather than in the bookkeeping: on 2026-11-02 this was the second root hiding inside one
night's twenty-nine reds. Change the number in the same commit as the deletion, and write the reason
beside it -- the file says a number that moved without a reason is the same silence as a relaxed gate.

## A Log File Belongs Outside The Tree
When a run's output is redirected, `*> D:\Temp\<name>.log` and never `*> run.log` beside the sources.
A file written inside the tree is an untracked file the plan-selection audit then has to explain: it
matches no rule in `test/plan-selection.json`, and the audit answers "tracked file matched by no rule",
which turns a reading into a red about the reading. This happened twice in one night -- once as an
extra failure in a full run, once as a "thirtieth red" that was the log itself. The log is the record
of the measurement, so it belongs where records live and not where the sources do.

## A file is written by an absolute path, never a relative one

**In PowerShell, write every file by its absolute path.** A `cd`, a `Push-Location` or a worktree you
just entered does not make a relative path land where you think it does.

`Set-Location` changes the shell's `$PWD` and **not** `Environment.CurrentDirectory`, and the .NET file
APIs (`[System.IO.File]::WriteAllText`, `ReadAllText`, `[System.IO.Directory]`) resolve a relative path
against the process's working directory, which is the one the shell started in -- for a lane, the main
checkout. The symptom is not an error: the command succeeds, and either the file is unchanged, or, the
worse half, **it is changed in another tree**. Both look exactly like a working edit, because a read and
a write that take the same wrong path are self-consistent: a script that read a file, replaced a
paragraph and wrote it back can report that everything went fine while the tree it was asked about was
never touched.

The rule comes from a mis-write (2026-11-04): a lane replaced a lambda in
`modules/transforms/transpose.cpp#buildTransposeMember` and the diff appeared in the main checkout's
`git status` while its own worktree showed nothing. The main checkout is the one surface every lane
reads (`agents/branching.md#The Main Checkout Is A Read-Only Coordination Surface`), so a stray write
there is read by someone else as their own working state.

**Prove where it landed, every time**: immediately after a write, read both surfaces --
`git -C <your worktree> status --porcelain` (the file you meant, and non-empty) and
`git -C D:\Projects\Camel status --porcelain` (empty). Non-empty and empty, in that order, is the
reading that says the write went where it was asked to go; anything else is a rescue, not an edit.

## Notes and comments hold the criterion, never the measurement

A `desc`, a `known_fail_note` or a `#` comment next to a test says **what the case pins** — the
criterion, the hand-computed number, the wall it stands on, and the page that records the decision.
It does not say what the output *is today*.

A measurement (an output excerpt, a count of cases, a `git grep` result, a load average, a date) is a
fact about one tree on one host at one time. Written into a note it becomes an assertion that expires
silently, and it will be read as current by whoever comes next. Put it in a page under
`docs/handover/reports/` with the host, the time, the load and the tree it was taken on, and let the
note point there.

The cost of getting this wrong has been paid: a note carried "a ruling flips about 16 cases" for four
hours after the tree changed so that flipping flipped nothing, and that sentence kept everyone from
touching a line of code that had no consumer left.

## An assertion on a spelling pins the whole sentence, never the punctuation

`...` is the string most likely to appear in diagnostic output by accident: the runner truncates a
long message and the cut lands in `(name=SyntaxDiag::MissingTo...`, an ellipsis in prose is three
dots, and a path or a version can carry one. So `expect_output_contains = ["..."]` does not assert
what it looks like it asserts -- it asserts that three dots appear somewhere, which a wrong
diagnostic satisfies as readily as a right one.

Pin the whole sentence instead, quotes included (`expected '...'`), and add a negative assertion for
the sentence the change replaced, so that the old behaviour cannot come back while the case stays
green. Both halves are needed: the positive one says the new wording is there, the negative one says
the failure it replaced is gone.

The cost of getting this wrong has been paid, twice in one evening. When a parser began reporting
`expected '...'` for a `..` pack, four cases were written with `expect_output_contains = ["..."]`
and four of them passed -- against a binary that reported the *old* semantic sentence instead,
whose truncated `(name=SyntaxDiag::...)` supplied the three dots. The same evening, a migration
script rewrote the very cases that were supposed to hold the old spelling, and the next run
reported a regression that was really the counter-examples deleting themselves. Assertions are
evidence only while nothing else rewrites them; a case that must keep the old spelling belongs on
the script's exclusion list.
