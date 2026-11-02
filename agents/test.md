# Test Rules

> **Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first** (English translation: [`design-principles.en.md`](../docs/design-principles.en.md)). It is the language author's normative statement of what Camel is and of its execution model; it prevails over code and other documents, and questions it leaves undecided go to the language author.

## General Policy
- Prefer TDD: write tests before implementation whenever feasible.
- Place test cases under `test/`.
- For substantial refactors, update `docs/` in the same change set.

## Verification Style
- Always define explicit acceptance criteria for an implementation plan and validate against them.
- For every new feature or new test case, manually run the relevant command-line case first and inspect the raw execution behavior and output before trusting the automated test harness. Use that manual observation to decide what the later `verify_*` or plan assertions should actually lock down.
- A plan entry may use only the fields `docs/test-plans.md` defines. The runner rejects an unknown key (and a wrong type on the fields it types) with an error naming the plan file, the test and the key, and fails that plan; the other plans still run. Do not rename an assertion to something the runner ignores: it would pass while asserting nothing. `expect_stdout_exact` (or its legacy name `expect_stdout`) compares the whole normalized stdout; `expect_stdout_contains` does not.
- When you add a plan field, add it to the runner and to `docs/test-plans.md` in the same change; when you remove one, remove it from both and fix the plans that use it.
- **A guard must be able to fail.** Say in its comment which change to the tree makes it red, and record that red run (a plan case, a test, or the report with the failing output). A number a verdict rests on comes from the tree — the files, the parser, the registered operators — never from a literal list inside the guard; where a literal is a pin, a plan pins it so a change is visible. A guard that examined zero things fails; it never reports a pass.
- **Pin the words that separate the cases, not a word both of them can produce.** `bf16` contains `f16`, and the reduced-precision diagnostic prints one help paragraph for both types ("bf16 and f16 are floating types of the language"), so `expect_output_contains = ["bf16"]` stays green even when the message names `f16` — a guard that cannot fail, measured: the bf16 case swapped to `f16` still passed 44/0. Pin the phrase only one of them can print (`element type 'f16'`, `element type 'bf16'`) and record the swap that reds it (43/1). The same trap is any short word that is a substring of its sibling: a pin of `f32` is satisfied by `f32[]`, and a pin of `NaN` is satisfied by `NaN` in a legend.
- **A probe of a run-time overflow on a narrow type cannot be written as a literal expression.** `let x: f16 = 65504.0 + 65504.0` is refused at compile time ("the literal '65504.0 + 65504.0' cannot be represented as type 'f16': the value 131008 is out of range"), because the constant folders check the folded value against the declared type — the infinity rule of the element type never runs. Put one operand in a variable (`let max: f16 = 65504.0; let over: f16 = max + max`) so the sum is a run-time operation; that is what answers infinity rather than saturating.
- **A migration changes what a fixture means, not only what it checks.** When a name moves from one mechanism to another (a builtin module becomes a declaration module, an operator stops being computed in C++), every test that used that name as an *example of the old mechanism* is now testing something else, and it will keep passing or start failing for a reason unrelated to its title. Before merging a migration, grep the tree for the migrated names and read each use: the pass-resolution diagnostic, the LSP's "computed member" examples, and a pinned count of declarations are the three that actually broke. A pinned count that moves because of a migration is updated to the measured value in the same change, with the reason; a guard whose subject changed is retargeted to a name that still exercises the mechanism, never deleted.
- **A rule shared by several back ends or paths has one test that runs on all of them.** Otherwise each platform exercises a different implementation, or the check covers one element type and the bug lives in another. Wrong: two interpreter sources of which each platform compiles one, tested through whichever was built; a gradient check on f32 only. Right: one case parameterized over the back ends and element types, with the outputs compared.
- **A `known_fail` mark has one spelling per shape of the entry, and the runner refuses the other.** With `backends`, a list of those back ends; without one, `known_fail = true`; mixing the two shapes is refused at load, by name (`docs/test-plans.md`, `known_fail`). It is written this way because a mark read by nobody is worse than no mark: it looks like a decision, and the case it mutes goes on failing in the open. Read `known failures: N` together with `skip N` before calling a rise a regression -- a mark only applies to a run that happens, so a case that starts running can turn its mark into an `[XFAIL]` for the first time (2026-11-02: `skip 43 -> 10` and `known failures 3 -> 6` in one run, nothing regressed).
- **A plan that starts no test proves nothing, and the runner says so.** A plan file whose own `tier` is the layer being run, and from which that layer selects none of its entries, fails the run by name; so does a run that selects no test at all. A plan whose own tier is another layer is counted on one line (`plans of another layer, started nothing here: N`) so its zero is a reading, not an absence. A plan file named on the command line still runs in full, whatever `--tier` says, so a named plan that starts nothing is a plan with no entries. Written because two of `jit_framed`'s milestone entries had never once run, and their red and their green were both worth nothing until someone proved they started.
- **Do not draw a conclusion from the twelve-line tail of a failure.** The log's `[stdout]`/`[stderr]` block is at most the last twelve lines, and only with `CI` or `CAMEL_TEST_SHOW_OUTPUT` set; the line that says what happened is regularly above the cut. Every failed test's log entry carries `[INFO] full output: test/results/log/<run>/fail/<plan>/<test>/ (stdout.txt, stderr.txt, output.txt, meta.json)`, relative to the checkout, and that file is the whole of it. Twice on 2026-11-02 a conclusion was drawn from a truncated fragment and was wrong both times. A run directory that recorded a failure is never pruned by the retention bound, so the file is still there tomorrow.
- **On a shared host, kill by identity and never by program name.** `kill <pid>` for a process you started and noted, or `pkill -f '~/camel-work/<your worktree>/'` with a path prefix that carries your own tree's name. `pkill -f '<script name>'` (`run-tests`, `node`, `camel`) is forbidden: on 2026-11-02 one such command was issued while five other lanes had runs in flight, and they survived only because `pkill` killed the issuing shell first. Every kill is followed by a `pgrep` pass over the same selector, with the survivors' pids recorded: the check is part of the command, not an optional courtesy.
- Start with small inputs when debugging high-volume workloads such as `fib 30`.
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

## The instrument's range is narrower than the fact

Three zero-hit readings on 2026-11-02 were all the same mistake, and none of them was a broken branch:

- `git branch -r --contains <sha>` reads **the local checkout's `refs/remotes`**. When those are stale, a branch that was pushed correctly looks like it does not exist; four tool commits were reported as "on no remote branch" while `origin/lane-a3` had held them all along, and `git push` answered `Everything up-to-date` because there was nothing to push. **Either `git fetch` first, or ask the server: `git ls-remote --heads origin <branch>`.**
- A build artefact was searched for in `build.ninja` while the build wrote `build-Release.ninja`: the grep was right and the file was the wrong one.
- A `git grep` for a tool's own message hit nothing because the main checkout was three commits behind the tree that contained it, so "the tool isn't there" was really "this tree predates the tool".

**A zero is not a reading until the search space has been shown to contain the thing.** State the command, the file or the ref it read, and which tree that ref or file belongs to.
