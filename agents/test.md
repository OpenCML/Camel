# Test Rules

Read [`../docs/design-principles.cn.md`](../docs/design-principles.cn.md) first. Do not change semantics to make a test pass.

## Running tests

Do not build Camel on this workstation. Use the remote Linux build host through the repository's only gate entry:

```powershell
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan <plan-file-or-directory> -Jobs 8
```

The wrapper transports the current branch and uncommitted changes, builds, runs the selected plans and returns their exit code. Do not invoke another copy of the wrapper or a hand-written build command.

During development, run the plans touched by the change plus `smoke`. Before finishing, run `smoke` and `feat/**` on the host. A plan file named explicitly runs in full, regardless of the selected tier; a directory selects its `fast` layer by default.

Useful local, non-build checks:

```powershell
node scripts/check-doc-links.mjs --all
node scripts/check-std-includes.mjs
node scripts/conflict-markers.mjs
node scripts/diagnostics-catalog.mjs --check
```

## Test layers

- `fast`: `smoke` and `feat/**`, the ordinary acceptance layer.
- `slow`: explicit slow plans and slow entries, used when the change warrants them or before release.
- `milestone`: full regression, GC poison/stress, random programs, PyTorch comparisons and performance work. Run it on a quiet host at 09:00 Asia/Shanghai each day and before every release.
- `debug`: assertions that require a Debug build. Select it only with `--tier debug`; it is excluded from Release layers.
- `all`: every plan except the deliberate `debug` tier, not a synonym for fast.

Do not run full stress or performance suites to "be safe". Use the relevant named plan. See `docs/test-plans.md` for the authoritative selection rules.

## Evidence and provenance

- Record the test target, tier, total, pass, fail, skip and known-failure counts, and the tested commit. `skip` is not failure; an empty selection is not a green run.
- **`total` is not coverage: report `total - skip`.** A skipped case and a passing case are the same shape in the totals. On 2026-11-15 the GPU layer printed `total=146 pass=19 fail=3 skip=124`: 124 were skipped because the probe `test/tools/have-cuda-device.mjs` found no visible device, so that layer's real coverage on that host was **22 cases**, and a ratchet over the other 124 would have measured nothing. State the coverage next to the total, and say plainly that a fix verified on 22 cases does not speak for the rest.
- **Two numbers printed side by side must carry their units, or someone will add them.** `checked 19, declined 3` reads as "19 examined plus 3 skipped, of 22 sites"; the two numbers in fact counted different things -- call sites and readings -- and a site can produce several readings. The same confusion had already happened once with `185` (a count of red *cases*) against `3` (a count of declined *readings*). If the two numbers answer different questions, do not force them into one unit; make the line say so, and say that they must not be summed.
- Every reading belongs to a tree and a build. Do not quote a SHA for a dirty tree; the runner's `[provenance]` line is the source of this information.
- Before treating a failure as new, check whether it exists on the base and whether another branch already fixed it. Do not diagnose from a truncated tail alone; read the full output named by the runner.
- **Quote only flags the tool actually has.** An unknown flag is not an error in most of these scripts: it is ignored and the default path runs, so the command succeeds, prints a plausible reading, and the premise you attached to it is false. On 2026-11-15 two instructions carried flags that do not exist -- `test.js --changed-base <sha>` (the override is `$env:CAMEL_TEST_BASE`) and `check-doc-links.mjs --check` (the modes are `--citations`, `--default-roots`, `--questions-index`, `--technical-index`) -- and both produced readings that looked like answers. `--changed-base 0e94f3b` was worse than ignored: the runner read `0e94f3b` as a **target path**, so the base silently stayed `origin/develop` while the command still ran. Before writing a flag into a dispatch, run the tool's usage or read its argument parser; **a reading is only as good as the flag it was asked for.**
- **A stale binary gives a credible false red.** After merging, rebuild before measuring: on 2026-11-15 a lane measured on a local binary older than the merge and saw five reds (`ref.one_box_two_names.*`) that did not exist on the host build, and another lane saw two reds from a `SyntaxDiag` change that predated its checkout. The reading is the same shape as a real regression, so nothing in the output tells you it is stale -- only the build stamp and the order of operations do.
- **A check that normalizes a path must cross the drive-letter colon.** `check-doc-snippets.mjs` erased the directory part of a diagnostic path with `(?:[^\s:()]*[\\/])?`, whose character class has no `:`, so on Windows it could not see past `D:` and compared `D:<file>:4:15` against a page that says `<file>:4:15`. The same document therefore passed on Linux and failed on Windows, and the page was correct all along. Where a check feeds a tool an absolute path and then compares text, make the normalization platform-independent and say so in the comment.
- **A rule that changes what the compiler accepts must be measured on every family that accepts it, not on the plan directory it lives in.** Three times on 2026-11-15 a lane ran only its own plans, reported green, and a control tree showed the new rule had refused programs that were working: a fold that made reads observe later writes (`new_values`); an effect check that refused every `tiles::compile` call site; and a closure rule that refused the standard library's own `stdlib/stream.cml:198` together with `byref` and `stream.tee_*` -- 208 reds against 10 on the control tree, invisible from `feat/compile` alone. The reading that catches this is `--changed` over the whole tree **plus the families the rule can reach**; do not copy the lane's directory list into the gate, because that list is exactly the thing that was too small. Measure the same batch on the base tree so that a family already red is not counted as new.
- **`skip` describes the host, not the layer.** On 2026-11-15 the GPU layer reported `skip=124, pass=22` in the afternoon and `skip=0, pass=131, fail=15` that evening with no change to the layer: the probe `test/tools/have-cuda-device.mjs` found a device the second time. Coverage is meaningful only with the host and the time attached; a `total - skip` quoted without them describes a machine, and comparing two of them across runs compares the machines.
- **A check over an empty set passes.** When a checker iterates a collection it parsed out of a data file, pin a lower bound on the collection's size, or a parse that silently yields nothing reads exactly like a clean result. On 2026-11-15 a merge resolved a conflict by rewriting `diagnostics/registry.tsv` with CRLF line endings; the consumer splits on `\n` and kept a `\r`, so the `field[6] === 'edit'` set came out **empty** and `fixes_apply` -- a property quantified over that set -- reported success. Nothing in the output distinguished "every entry is fine" from "there were no entries". The separator a script assumes is part of the file's contract, and CRLF is the failure mode that survives every editor and every diff.
- **An invisible door and a missing door look the same from a failure.** Before concluding that a capability does not exist, read the tool's own parameter table, not the reports of the people who could not find it. On 2026-11-15 three lanes failed to record AST hashes and each reported it as something a lane "cannot do"; a fourth lane and the coordinator agreed and proposed building an escape hatch. `camel-remote.ps1` had carried `-UpdateAstHashes` all along, and its own description read "which a lane cannot do, and a lane that skipped it left a red window on the base branch" -- the door existed, its sign was invisible. The correction was to make the existing door readable (print the binary's build time; accept `-AstOnly` so an update cannot rewrite every stale line) rather than to open a parallel second door, because two doors to the same room guarantee that one is eventually used for the wrong job.
- **A condition that never fires and a condition that fires with no effect look identical from outside.** Before changing how a pass decides something, put a counter on the branch's exits and read it, rather than reasoning about whether the branch is reached. On 2026-11-15 one rework wrote four separate guards into the same fold and three of them were wrong in exactly this way: a `joinsNoValue` guard that was entered and had no effect, a narrow condition that could never match a straight-line program, a "straight-line only" definition that was never enforced inside the fold, and a bail-out that fell back to the same value as the case it was meant to separate. Each was found by printing counts, none by re-reading the code. The related trap is a probe whose condition cannot hold at all: a probe that required "the cell is this node's first input" was pointed at a set of *read* nodes, whose first input is their version, so it printed nothing on every run -- which read exactly like "this path is never taken".
- **A passing case's stdout is not in the runner's output.** The runner prints a case's stdout head when it fails, so a probe that prints and does not change the result is invisible: the run is green and the reading does not exist. Either put the probe's number into an assertion the plan already checks, or let an assertion fail once to surface the output and then remove the probe -- and say that you did, because a temporary red in the history is otherwise indistinguishable from a defect you left behind.
- `null` is absence of an exit code, not a timeout. A signal, timeout and failure to start are different outcomes.
- Keep logs and probe artifacts outside the worktree. Never kill a shared-host process by a broad program-name pattern.
- Build with Release for routine verification. Use Debug only for debug-only behavior; `GUARD` must remain active in Release, while `ASSERT` may be compiled out.

## Writing and changing tests

- Manually run a new feature or case first and inspect stdout/stderr. Only then encode the observed contract in a plan.
- Test behavior and invariants, not counters that move with unrelated edits. Use exact output where formatting matters, and properties where implementation may vary.
- Cover every affected backend, or require a located refusal on unsupported ones. A rule shared by several backends should have one shared case, not separate implementations.
- Put generated files and fixtures under `test/`; keep temporary probes outside it. Do not create test or report scaffolding when the project has no need for it.
- **Any edit to a `.cml` file under `test/cases/` changes its recorded AST hash -- comments and `desc` strings included.** The snapshot records a hash of the file's bytes, so a citation fix that touches only a comment breaks `frontend.ast_snapshot` exactly like a syntax change. On 2026-11-15 a documentation commit (`4da099048`) rewrote `questions/` to `reviews/` inside two `desc` strings of `test/cases/compile/ref/ref_boxes_made_per_call_*.cml` and left the snapshot stale; the resulting red was then attributed to three unrelated lanes, because attribution had reasoned "that commit could not have touched hashes". Do not narrow the scope of this rule to syntax or `test/cases/parse`: the case that broke it was neither. Re-record with `check.py snapshot --update --only '<pattern>'` per file, never a bulk `--update`. Changes to diagnostics require `diagnostics/registry.tsv` followed by `node scripts/diagnostics-catalog.mjs --write`.
- Do not weaken an assertion, delete a case or mark a failure merely to make the suite green. Explain every changed expectation in the change report.

## `known_fail`

A known failure is a precisely described open defect, not a budget or an exemption:

- Use `known_fail = ["backend"]` with a `backends` list, and `known_fail = true` without one.
- Include `known_fail_owner`, a concrete `known_fail_reason`, and `known_fail_msg` containing the expected diagnostic fragment.
- The runner rejects a marked case that starts passing, a missing expected message, or a failure whose reason differs from the pinned fragment. Remove a stale mark in the same commit that fixes the case.
- Real compiler internal errors on runnable programs are not marked known. They are ordinary failures requiring rework.

## Selection and generated checks

- `--changed` selects plans from the changed paths and their declared cases. An unreadable base or selection is an error, not a reason to run the whole fast layer. An empty selection means no tests ran; name the intended plans explicitly.
- `CAMEL_TEST_BASE` overrides the comparison base when necessary.
- A new plan needs a selection rule or a deliberate exclusion, so it cannot silently disappear from `--changed` and layer accounting.
- Regenerate rather than hand-edit generated indexes, diagnostics, standard-library references and snapshots. Checks that examine zero items fail rather than pass.
