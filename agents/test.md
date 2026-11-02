# Test Rules

> **Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../docs/design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.

## General Policy
- Prefer TDD: write tests before implementation whenever feasible.
- Place test cases under `test/`.
- For substantial refactors, update `docs/` in the same change set.
- **Before writing a file, print the target's absolute path.** `[System.IO.File]::WriteAllText` and shell redirection (`>` , `Out-File`) resolve a *relative* path against the process's working directory, not against the directory a previous `cd` put the shell in -- and that working directory is not always the worktree the reader assumes. On 2026-11-02 six writes landed in the main checkout `D:\Projects\Camel` that way, two of them as **0-byte** files: a 0-byte file is the worst shape, because it looks like "I created it" while nothing was written. So: derive the path from an absolute root (or `$PSScriptRoot`), print it with `Resolve-Path -LiteralPath` / `[IO.Path]::GetFullPath()`, and end a round with `git -C D:\Projects\Camel status --porcelain`, which must print nothing.
- **A commit that no remote branch contains does not exist for anyone else.** The last command of a round is `git branch -r --contains HEAD`: an empty list means the round's work is invisible to every other lane, however green its readings were. On 2026-11-02 four commits sat on a local branch while another lane reported, in good faith, that the rule it needed was not in the tree -- "I committed it" is not "it can be merged". Push the branch (or hand the merge lane a branch that is pushed) before saying a round is done.
- **Only a tip that `origin/<branch>` resolves to is a reading that can be merged.** `git cat-file -t <sha>` answering `commit` proves the object is in *this* repository and nothing more; `git branch -r --contains <sha>` printing nothing proves no remote branch carries it. Both together mean the reading cannot be checked by anyone else, which is the same class of problem as a change written into somebody else's checkout: the numbers may be right and nobody can reproduce them. On 2026-11-02 the fix for the Debug link error `typeinfo for camel::rewritemod::ProofObject` was reported at `41f2303d7` -- a commit that existed locally and that no remote branch contained. A merge lane's answer to "is this ready" is therefore `git ls-remote --heads origin | grep <branch>` and a `rev-parse` of that ref, never "the lane says its branch is at <sha>".
- **A merge runs three instruments first, and any one of them red stops the merge.** `git grep -n '^<<<<<<< '` for conflict markers -- including the four `|||||||` lines a diff3 conflict leaves, which that grep does not match; `node scripts/diagnostics-catalog.mjs --check` for a code registry that still names what the tree emits; and `node scripts/check-doc-links.mjs --questions-index`, because `docs/handover/questions/` is a table whose row count is maintained by hand and whose pages move to `reviews/` when the author rules on them. The index rule is one line: **the page is generated, not written.** ode scripts/check-doc-links.mjs --questions-index --write builds it from the directory -- one row per page, the page title as the sentence, the header counting the directory -- and --questions-index without --write compares the file with exactly that text, so a hand edit is a red that names the line and prints both versions. The fix is always to regenerate. The author made this the rule at 11:10, after a night in which the same table broke three times in three merges (3 pending-index problems, a row for a page that had moved to reviews/, a header one behind the directory) and after he retired every hand-kept state table at 08:10: an index kept by hand beside a directory is two sources for one fact.

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

## Guess the name and the count is wrong, not small

Three counts on 2026-11-02 were wrong because the name was invented rather than read out of the
header, and each one was wrong in a way that looked like a finding:

- `setTagUses`, `setFieldTagUses`, `setOperRef`, `setNativeValueType` all counted **0 call sites**,
  which read as "four of the fifteen per-node side tables of the construction tree are never
  filled, so a serializer can drop them". The tables are all live: the setters are named
  `addTagUses`, `addFieldTagUses`, `markNativeValue`, and the operator reference is written
  `operRefs_[node]` directly. Every one of the fifteen has 1 to 8 write sites.
- A `shape` count of tree lines was 9,381 lines too high -- exactly `3 x 3,127`, the three record
  header lines of every dump -- because the header loop advanced an index and the body loop then
  walked the list from the start. A throwaway script written first had it right; the tool did not.
- A pass named `upper` was searched for and found nowhere, because no pass is called that.

**Before counting a setter, an accessor or a pass, read the family out of the header**: setters here
are `set*`, `add*`, `mark*` or a direct map write, and a pass name is in `stdlib/std.cml`. A zero
from a guessed name says the name is wrong, not that the code is missing -- and the difference
matters, because the first version of the four-table reading above was about to be reported as a
finding about the model.

## A wall clock on this host is not a cost, and a phase is not a sum with its children

Two readings taken on 2026-11-02, from one command, wrong in the two ways that make a performance
number unusable:

- **`--time-phases` scopes nest.** `compile` is the scope around `mainModule->compile(Done)`
  (`tools/camel-cli/main.cpp`), so `parse prelude`, `gct prelude` and `declare prelude` are reported
  at depth 1, inside it. A report that added 11.22 ms of prelude phases to a `compile` that had
  grown by 10.96 ms counted the same work twice and read 22.2 ms where the truth was 11.2. **A
  child's time is inside its parent's: read `depth` before adding anything up, and sum one depth.**
- **A wall clock on this host measures the other lanes.** The same command read `imports_nothing`
  6.00 ms of wall time on one run and 30.12 ms on the next, while the top-level CPU phases of the
  two runs read 5.60 and 17.09 ms: eight lanes run suites on 32 threads, so a wall clock is the
  queue and the CPU time is the work. `total_ns` in the phase report is wall; `cpu_ns` per phase is
  the half that does not move with load. **Compare CPU, quote CPU, and when a wall number has to
  appear, say what other load the two runs met.**

The pair matters because it fails in opposite directions: the nesting makes a cost look twice as
large as it is, and the shared host makes it look arbitrary. A performance reading in this tree is
`cpu_ns` summed over one depth, from two binaries whose `camel --version` lines are quoted beside
it.

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
