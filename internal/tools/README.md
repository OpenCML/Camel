# internal/tools

Tools that move a Camel tree to the build host and start work on it, plus the two local helpers that
serialise a build or a test run when one is made on this workstation. They are here, and not outside
the repository, because every gate reading in this project passes through them: a helper that decides
what "green" means has to be readable, reviewable and diffable like anything else that decides it.

Nothing in this directory compiles Camel on the workstation. The gate pushes the tree to the build
host, builds and runs the plans there, and brings back the readings; the local helpers exist for the
work that is not a full gate run and hold a machine-wide lock so two of them cannot overlap.

## Files

| File | What it is |
|---|---|
| `camel-remote.ps1` | The gate entry. One call moves the tree, builds, runs plans and runs the repository's static checkers on the host. |
| `camel-build.ps1` | Local helper: one build at a time under a machine-wide lock, publishing its progress to the panel's state file. |
| `camel-test.ps1` | Local helper: one plan run at a time under a lock of its own, publishing to the panel's test state file. |
| `watch-fsh-dev.ps1` | Watcher for a branch moving on `origin`; not a gate entry and unrelated to the two locks. |
| `camel-gate-config.ps1` | The loader the four scripts above use for their machine-local settings. |
| `camel-gate-state.ps1` | The one write `camel-build.ps1` and `camel-test.ps1` make to `build-state.json` / `test-state.json`: the JSON goes to a temp file beside the destination and is moved over it in one rename, retried briefly when a reader holds the file, and a publish that still cannot complete is `$false` rather than an exception. The state files are advisory, so that failure is a warning and never the run's exit code. |
| `camel-gate-state.test.ps1` | Its test, with no build and no plan: `pwsh -NoProfile -File internal/tools/camel-gate-state.test.ps1` prints `13 checks, 0 failed` and exits 1 on a failure. It publishes over a missing and an existing file, holds the destination open to require "not published, not thrown" plus a complete old document and no temp file, and runs a reader beside the publisher for two seconds to require that no read ever sees half a document. |
| `run-registry.ps1` | The run registry's one writer (`Add-RunRegistryRow`): one compact JSON line per run snapshot appended to `run-registry.jsonl` under `Global\CamelRunRegistry`, compacted to the newest lines once it passes half a megabyte. Every write is best-effort -- `$false`, never an exception -- and the compaction publishes the way `camel-gate-state.ps1` does: a temp file beside the destination, one rename, retried briefly while a reader holds the file, the temp removed when it cannot land. `Move-Item -Force` reported a held destination as `ERROR_ALREADY_EXISTS` and, under `$ErrorActionPreference = 'Stop'`, threw away a compaction it had already written and left `run-registry.jsonl.<pid>.tmp` in stateDir. |
| `run-registry.test.ps1` | Its test, with no build and no plan: `pwsh -NoProfile -File internal/tools/run-registry.test.ps1` prints `14 checks, 0 failed` and exits 1 on a failure. It requires a compaction with no reader to land with the newest kept lines plus the new row, and holds the destination open to require "not published, not thrown" plus a complete old registry, no temp file and a retry before giving up; the last two are red against the `Move-Item -Force` writer. |
| `camel-remote-args.ps1` | The argument rules `camel-remote.ps1` applies before it pushes anything: comma-split lists (`-Plan`, `-AstOnly`, `-AstDrop`), the `--only`/`--drop` words built from them, and the refusal for a glob that names no `.cml` of this tree. Separated out because those rules are the part that used to fail silently on the host. |
| `camel-remote-args.test.ps1` | Their test, run without a host: `pwsh -NoProfile -File internal/tools/camel-remote-args.test.ps1` prints `N checks, M failed` and exits 1 on a failure. |
| `camel-remote-timesensitive.ps1` | The one reader of a plan's `time_sensitive` declaration: a `*.plan.toml` target that says `time_sensitive = true` at its top level, before any table header, makes the gate take the plan slot and both build slots together. Separated out of `camel-remote.ps1` so it can be called with no host, and because it now has a second, independent reader of the same files to be compared with (`test/tools/time-sensitive-reader.mjs`). The logic is the logic that was inline before the move; the gate calls it and carries no copy. |
| `camel-remote-timesensitive.test.ps1` | The comparison: every `*.plan.toml` in the tree read by that function and by `test/tools/time-sensitive-reader.mjs`, the five target kinds the read is asked about (relative, absolute, a directory, an empty one, one that names nothing), and fifteen boundary shapes (an empty value, a quoted one, mixed case, whitespace, a repeated key, a key inside a table, a key inside a string, a declaration after a nested array, and the plain forms). `pwsh -NoProfile -File internal/tools/camel-remote-timesensitive.test.ps1` prints `27 checks, 0 failed`. The five shapes the two readers disagree on are recorded as divergences with a verdict; each row is red when the disagreement ends, so the table cannot outlive it. |
| `merge-lane.ps1` | The merge position for ONE lane: merge, header check, clang syntax, row readings against the tree the merge lands on, then the push. |
| `merge-batch.ps1` | The merge position for a BATCH: three or four lanes that touch no file in common, merged into one temporary worktree, measured once over the union of their plans, with the baseline taken on a separate checkout of the merge's first parent. `-Target` names the branch it lands on and pushes to, `develop` (the default) or `integration`, with the same parameter and validation as `merge-lane.ps1`. `-ScratchRoot` is where its two trees and every transcript live, from the settings file's `scratchRoot` like `mergeWorktree`, with no built-in path and exit 2 when the seat names none. See "Batching the merge position" below. |
| `merge-batch-sets.ps1` | The two readings a batch is composed by: the pairwise intersection of the lanes' changed-file sets, and the plan-set union with the name `scripts/plan-set.mjs` gives it; plus `Resolve-BatchTarget`, which spells every ref from the target and refuses a target it does not know, and `Resolve-BatchScratchRoot`, which takes the seat's `scratchRoot` and refuses a seat that names none. Separated out because all of it fails quietly, and so it can be checked without a host. |
| `merge-batch-sets.test.ps1` | Their test: `pwsh -NoProfile -File internal/tools/merge-batch-sets.test.ps1` prints `29 checks, 0 failed` and exits 1 on a failure. The plan-set hash check runs `scripts/plan-set.mjs#planSetHash` through node and requires the same string; the target checks include the tool's own refusal of an unknown `-Target`, which needs no host because parameter binding stops before the body runs; the scratch-root checks run the tool against a throwaway git repository to require the exit 2 a seat with no `scratchRoot` gets. |
| `merge-push-shape.ps1` | The shape a commit must have to be pushed to a target: `Test-PushFirstParent` fetches `origin`, resolves the target's tip, and refuses a single-parent commit (a lane commit that would go straight in) or a merge whose first parent is not that tip. One implementation, dot-sourced by `merge-lane.ps1` (before both of its pushes) and `merge-batch.ps1`; the refusal is D-1124-78 and names the commands that redo the merge on the tip. |
| `merge-lane-firstparent.test.ps1` | Its test, and the regression beside it: `pwsh -NoProfile -File internal/tools/merge-lane-firstparent.test.ps1` prints `30 checks, 0 failed`. Four cases -- a merge of the tip arrives, a merge of an older tip is refused with the sentence and the redo command, a single-parent commit is refused, and `Invoke-PushWithRecovery` still pushes when the shape is right -- plus the wiring: each tool calls the reading and neither carries a copy of it. |
| `merge-lane-baseline.test.ps1` | The key a baseline is stored under: `pwsh -NoProfile -File internal/tools/merge-lane-baseline.test.ps1` prints `28 checks, 0 failed`. Same sha twice is a reuse and not a rebuild; a different sha is a different tree; a fixed-name tree holding another commit is called stale (`stale baseline beside the tip: rebuilding`) and moved; two seats that name different trees still resolve one tip to one tree and one branch, which spells the sha. |
| `hook-integration.test.ps1` | The same rule at the push itself: `scripts/hooks/pre-push` refuses a push to `refs/heads/integration` whose commit is not a merge of the remote tip. `pwsh -NoProfile -File internal/tools/hook-integration.test.ps1` runs the real hook (found by its `sh`, taken from git's own install) against a throwaway repository: `13 checks, 0 failed`. |
| `run-tests-diagnostic-file.test.ps1` | The guard for `expect_diagnostic_file`, the plan field that says which file a diagnostic came from (`test/tools/run-tests.mjs`). It needs no build and no plan of this tree: a plan is written under the system temp directory with `tool = "node"` entries whose probe prints one diagnostic in the runner's short format, and the runner's own exit code and message are read back. `pwsh -NoProfile -File internal/tools/run-tests-diagnostic-file.test.ps1` prints `36 checks, 0 failed` and exits 1 on a failure. The right file passes (relative and absolute), another file fails with the required file and the reported file both named, an array/number/empty string/`expect_no_diagnostics` beside it is refused where the plan is read (the probe's marker proves nothing ran), and an entry that does not write the field keeps its old meaning. |
| `lane-audit.mjs` | Which `lane/*` branches are already inside a merged tree, and which are really unmerged: `ahead` / `behind` / `contained` / `equiv` per branch, listed in three buckets -- deletable, suspect-merged, unmerged (by `behind`) -- with `--json` and a `--check` that exits 3 when bucket 3 is over `--limit` (default 20). The base is `seat/merge-rehearsal`, else `develop`, and the report says which one on its first line. A bucket 1 row also says whether `origin/develop` holds it yet: a rehearsal tree is built on one machine, so "contained in the base" is not "published", and a branch deleted while its content is unpublished leaves the base as the only ref holding it. It reads local refs only and fetches nothing; it deletes no ref and creates none, and it takes that reading rather than claiming it: `refs/heads/lane/*` is compared before and after the run, and a sha that moved is exit 4 with the ref named. Bucket 2 means "the same content by different commits" and belongs to the lane's owner -- a row here is a conversation, never a deletion. |
| `branch-check-strings.mjs` | The list a merge was audited by had four cells -- `GCNodeKind::Copy|::Cast` references, `addCopyNode|addCastNode` calls, diagnostic names in `registry.tsv` / `examples.tsv` / `semantic.h` -- and it passed in all four while a merge still broke a reading, because what the reading depended on was a string label: a seat commit deleted two `return "CAST"` from `src/stdlib/std/gir/gir_metadata.cpp`, git saw no conflict on those lines and took the base's version. So this program walks the population the old list had no cell for: every `"..."` / `'...'` span on every line `git diff -M -U0 <base> <lane>` puts on the lane's side, checked against the base's copy of the **same file** (and against the scan set `src/**`, `stdlib/**`, `test/tools/**` for "does the base spell it anywhere"). One row per (lane file, literal) site in three buckets: ① still spelled (in that file, or marked `elsewhere` in another), ② gone from that file while the fork point had it -- with the commit `git log -S` names, and split into `file-lost`, `file-gone` (collapsed to one line per file, and named as a modify/delete the merge stops on anyway) and `tree-gone`; ③ new to both. Exit 1 when ② is not empty, 0 when it is; `--json`, `--limit`, `--match`, `--history`, `--paths`. Prose (`.md`, `.txt`) is counted and not listed. Read-only: no file written, no ref moved, no index or build lock taken, nothing fetched, and the three shas are re-read after the run (a ref that moved is exit 4). Its header lists the seven things it cannot answer -- same spelling with another meaning, cross-file producers, run-time assembly, text-not-syntax (comments, JS regex), everything that is not a literal, undetected renames, and whether the line mattered. |
| `camel-cleanup.sh` | The cleanup door: a run's temporaries live under `~/camel-work/tmp/<lane>-<time>/` and are judged by the manifest the run wrote when it created them. `rm` accepts a path only when it is under that root **and** in a manifest. |
| `camel-cleanup.test.ps1` | Its test: the six readings against a fake home (never the real one). `pwsh -NoProfile -File internal/tools/camel-cleanup.test.ps1 -BashHost <alias>`. |
| `camel-gate.example.json` | The shape of the local settings file, placeholders only. |
| `red-table.mjs` | Regenerates `internal/reports/<date>-develop-reds.cn.md`, the table of what is red on `develop`, from the recorded readings alone — it starts no build and no test, and takes about 6 seconds. `--tsv <file>` writes the same rows with a header stating the `tip`, `jobs`, `load` and `tier` it was taken at; `--diff <a.tsv> <b.tsv>` compares two such tables and **refuses (exit 2, "not directly comparable")** when their `jobs`/`load`/`tier` differ instead of listing every row as `new`. In a red table `fixed` means **"this red no longer appears"**, not "this case was repaired" — a table of reds cannot tell repaired from deleted, and `--cases <list>` is what separates the two (`removed`). `red-table.md` says what each of its seven columns may and may not claim. |
| `red-table-context.mjs` | The header, the whole-name reader and the comparison behind `--tsv`/`--diff`: a verdict line's case name is taken whole (never `\S+`, which truncated the ten names with spaces into `removed`), and unknown `jobs`/`load` refuses a comparison rather than passing silently. |
| `red-table-context.test.mjs` | Its test: `node internal/tools/red-table-context.test.mjs` prints `20/20 checks passed` and exits 1 on a failure. Covers the header round trip, the changed-not-removed case for a name with spaces, fixed-vs-removed with and without a `--cases` inventory, and the two refusals (different `jobs`, quiet-vs-loaded host), including the CLI wiring. |

## What `scripts/hooks/pre-push` refuses

Two rules, and both can be bypassed in one way:

- **`develop` is written from the merge worktree only.** The tree is read from this machine's
  settings file (`mergeWorktree`); a push from any other tree is refused with the key to set.
- **An arrival on `integration` is a merge of its tip** (D-1124-78). The hook reads the refs git is
  about to send, and for `refs/heads/integration` it requires two parents and a first parent equal to
  the remote tip (a new branch has no tip, so only the parent count applies). A lane commit that
  would go straight in is refused by name; a merge made on an older tip is refused with the sentence
  that says what the operator was doing and the command that redoes it on the tip, `rerere`
  included. `merge-lane.ps1` and `merge-batch.ps1` assert the same thing before their own pushes --
  the hook is what covers a push made by hand or by a branch re-landed somewhere else.

`git push --no-verify` runs no hook at all, so neither rule can stop a push that asks to skip them.
That is written here instead of being locked down, because a lock in a hook that git itself offers to
skip is a lock in name only. **A push made with `--no-verify` says so in its conclusion line**, and
the chain it wrote is then the thing to repair (the shape is re-read by the next merge, and
`internal/backlog.md` is where a wrong arrival is recorded).

Beside those two rules the hook runs `scripts/merge-record.mjs verify` over the push's first-parent
range: every merge commit in it needs a pass record in `.git/camel-passed/<sha>.json`, and the record
now carries the measured run's own `exit`. **`exit` absent means `"0"`** -- records written before the
field existed stay valid, no migration -- and any other value is refused in its own words, so a
record that says `exit=unknown` (the gate's exit 7: readings complete, exit code not obtained, see
`camel-remote.ps1`) exists for the reader who wants the reading but opens no door. Only merge commits
are read: a single-parent commit such as a documentation page is not asked for a record at all.

## The machine-local settings file

None of these scripts names a host, a drive or a user's home. Those values come from one JSON file
that is **not** in version control:

```
internal/tools/camel-gate.local.json        # this machine's values; git-ignored
```

To create it:

1. Copy `internal/tools/camel-gate.example.json` to `internal/tools/camel-gate.local.json` beside it.
2. Fill in the placeholders, and delete the example's `_comment` line while you are there: it is not
   a setting, and it is the one key the loader has nothing to compare it to. `sshHost` and `stateDir`
   are required; the rest have defaults.
3. Worktree-local: the file is git-ignored, so each checkout of the repository needs its own copy.

The example file gives the shape only -- every value in it is a placeholder, and no host, drive letter
or user's home appears anywhere in the repository. The real file is the one place those values live.

Rules the loader keeps:

- A key one or two characters from a known name is an error, not a silent no-op: a key that is
  misspelled would otherwise leave a run measuring the machine its owner thought they had changed.
  A key nothing here is close to is a **file newer than the tree's scripts** -- the file is
  git-ignored and shared by path, so a checkout on an older branch reads a file written for a newer
  one. That one warns, naming the key, the file and the tree that does not read it, and the run
  continues; the alternative reports a correct file as a broken one and stops every run of the older
  tree.
- A tool that needs a setting the file does not carry stops with exit 2 and prints the path of the
  file to create. It never guesses a host.
- A parameter with the same name as a key overrides it for one run (`-LocalConfig`, `-StateDir`,
  `-Repo`). `-LocalConfig` also lets a run point at a settings file somewhere else.

The keys, and what each decides:

| Key | Decides |
|---|---|
| `sshHost` | The ssh alias or host name the gate connects to. Also what the panel shows for a host run. |
| `sshExe`, `scpExe` | The ssh and scp programs. Default: `ssh` and `scp` from `PATH`. |
| `stateDir` | The directory holding `build-state.json`, `test-state.json` and `run-registry.jsonl` — the three files the panel reads. |
| `hostWorkRoot` | On the host, the directory below `$HOME` that holds one checkout per worktree name. |
| `hostBareRepo` | On the host, the bare repository a run pushes its staging branch to. |
| `hostClangEnv`, `hostBuildEnv`, `hostBenchEnv` | On the host, the three toolchain prefixes the run puts on `PATH`. |
| `hostLocalBin` | On the host, the extra prefix of the run's `PATH`. |
| `hostLibStdCxxDir` | On the host, the toolchain's libstdc++ directory, exported for installs that predate carrying their own copy. |
| `watchRepo` | The checkout `watch-fsh-dev.ps1` reads. |
| `mergeWorktree` | The one worktree allowed to push `develop` (`scripts/hooks/pre-push`). One absolute path, or several separated by `;`. Leaving it out means no tree may push develop. |
| `baselineRoot` | The directory the per-tip baseline checkouts live under: the tree for a commit is `<baselineRoot>\<sha>`, on a branch `camel-baseline-<sha12>`, so every seat that lands on that commit reuses one checkout and one host build. The reading itself is filed by `scripts/merge-readings.mjs` as `<sha12>-<planSetHash>-v<normal>.jsonl` under the repository's common `camel-readings` directory: the commit and the plan set, never the seat. |
| `baselineWorktree` | The older shape of the same decision: one fixed tree per seat, moved to each tip. Still honoured; `Get-BaselineCheckout` checks its HEAD against the tip first, and a tree holding another commit is printed as `stale baseline beside the tip: rebuilding` and moved instead of read. |
| `scratchRoot` | Where `merge-batch.ps1` writes its merge tree and its transcripts. Its baseline tree follows `baselineRoot` when that is set, and this root otherwise. |

## `camel-remote.ps1`

The gate entry. A run pushes the worktree's branch to the host's bare staging repository, writes the
uncommitted changes as a binary patch and the untracked files as a tar, sends all three in one `scp`,
and on the host puts the checkout on that commit, applies the patch, unpacks the tar, then builds and
runs what was asked. Every line comes back as it happens, the run's clock is split into transfer,
build and plan, the phases are published to the panel's state files, and the exit code is the build's
or the plan's own.

One code is not a verdict: **exit 7 means the readings are complete and the exit code was not
obtained**, which is neither a pass nor a failure. The run reports its code as its own last line
(`__CAMEL_REMOTE_EXIT__=`) so a session that will not close cannot take the code with it, and
`-FinishTimeoutSeconds` bounds the close; a close that misses it keeps every reading and prints
`[camel-remote] <name> exit unknown` (registry: `exit=7 phase=unknown`). Read it as its own state:
not green (nothing says the run's checkers were green), not a red reading (the readings are all
there), and the one state in which a record is written with `exit=unknown` and no push follows.

A clean tree whose host checkout is already on that commit and clean skips the checkout and the patch
altogether, so an unchanged re-run costs one build pass and the plan.

| Parameter | Meaning |
|---|---|
| `-Path <worktree>` | Required. The worktree to send. Its branch name and its directory name decide the staging ref and the host checkout. |
| `-Plan <target>[,...]` | One plan file or plan directory per element; a shorthand target such as `smoke` also works. Empty means "build only". |
| `-Jobs <n>` | The plan runner's parallel width. Default 16. |
| `-Tier <name>` | The runner's tier (`fast`, `slow`, `debug`). `debug` also makes a `-NoBuild` run check the debug install. |
| `-Config Release\|Debug` | Which build to make. Default `Release`. |
| `-NoBuild` | Skip the build and run against the install already on the host. Refuses (exit 4) when that install was built from different sources. |
| `-AllowStale` | Let `-NoBuild` proceed past that refusal. The mismatch is still printed, and the switch is never implied. |
| `-Check <name>[,...]` | Run the repository's own static checkers on the host after the build and after the plan; `all` runs every one. An unknown name is exit 2 before anything runs. |
| `-NoGpuWatch` | Leave out the gpu observation: every run that takes a reading also runs `test/plans/gpu` once and prints one `check=gpu-plans … mode=report` line. |
| `-Changed` | Run the plans the change touches (`--changed`) instead of a written list. The change is listed here, where the base branch is readable, and handed to the host as `--changed-files`; a base that cannot be read is exit 2 before the push, and it never widens to the whole fast layer. |
| `-ChangedBase <rev>` | The base for `-Changed`. Default `origin/develop`. Unreadable here is exit 2, naming the revision. |
| `-UpdateGolden` | Rewrite the named plans' goldens instead of checking them. It rewrites `kind = "snapshot"` goldens only, and brings every golden the run rewrote back into this tree -- all of them or none: a transfer that did not finish writes nothing and is exit 2, because the recording run is green against the files it just wrote while the next run is red against the old ones. |
| `-UpdateAstHashes` | Re-record the front-end AST snapshot: runs `test/tools/parsecheck/check.py snapshot --update` on the host, before the plan, and writes `test/plans/feat/parse/golden/ast_hashes.txt` back into this tree. It is the only way a new `.cml` case reaches that record, because `-UpdateGolden` cannot (`frontend.ast_snapshot` is `tool = "python"` and the runner never passes it `--update`). Name the parse plan beside it to verify the record in the same call. The write-back refuses a record that loses a row whose `.cml` file this tree still has, and refuses a bulk re-record (no `-AstOnly`) that loses any row, so a re-record taken on a tree that is missing another lane's files cannot delete their rows. |
| `-AstOnly <glob>[,...]` | With `-UpdateAstHashes`: name the cases the re-record rewrites. Commas split the list, so `-AstOnly 'a.cml,b.cml'` -- including what `pwsh -File` makes of `'a','b'` -- is two globs; repeating the parameter is a PowerShell binding error, and has to be written as one of those two forms. A glob that names no `.cml` of this tree is exit 2 before the push: a narrowing that selects nothing is a bulk re-record, which is exactly what the flag exists to prevent. |
| `-AstDrop <path>[,...]` | With `-UpdateAstHashes`: a recorded path whose row this re-record may lose (a case that really is deleted). Each name is printed as it is dropped, and a name that no row was dropped for is printed too. Without it the write-back refuses the drop. |
| `-Clean` | Delete this worktree's host checkout, build directory included, and stop. Refuses (exit 3) what lives beside the checkouts: a bare repository, and the cleanup root's own bookkeeping (`tmp/`, `.records/`). |
| `-StackTrace` | Diagnostic build with assertion stack traces. Its readings do not decide green or red. |
| `-FinishTimeoutSeconds <n>` | How long the *close* of a finished run waits, in seconds. Default 600. It counts silence after the plan's own last line (`[SPLIT]`) and nothing before it, so a quiet plan is never cut; a close that misses it keeps every reading and reports exit 7. |
| `-StateDir <dir>` | Override the settings file's `stateDir`. |
| `-LocalConfig <file>` | Use that settings file instead of the one beside the script. |
| `-LibStdCxxDir <dir>` | Override the settings file's `hostLibStdCxxDir`. |

Switches are spelled with one dash (`-NoBuild`), which is PowerShell's form; the two-dash form a shell
habit reaches for (`--NoBuild`) binds to the same switch, so it costs nothing here. What is refused is
a bare token that is not a switch at all: parameter binding is named-only, so `-Path <worktree> smoke`
stops with PowerShell's "no positional parameter accepts 'smoke'" instead of quietly becoming
`-LocalConfig smoke` -- a run that looks right and reads another settings file.

Exit codes: the build's or the plan's own; `5` when the gpu observation read a case red that its
baseline does not record, and also when the host script could not get a shared hold on the old test
lock within its wait (`internal/tools/host/camel-run.sh`, which uses `4` for "no test slot in time"):
both are runs that measured nothing, and a run that measured nothing is not a red; `4` when the
`-NoBuild` guard refused or a host lock was not obtained; `3` when the host is short of disk; `2` when
the worktree, the push, the transfer or a `-Check` name was wrong.

Reading the host's lock files: `fuser -v /tmp/camel-host-test*.lock` lists the processes that have each
file **open**, which is not the same as holding the lock. A run that opened the descriptor and never
took the lock still appears; a descriptor inherited by a child appears under the child's pid; a process
that has released the lock can still have the file open. So a `fuser` listing answers "who could be
holding this" and nothing else -- the authority is `flock`, and the tool that takes it prints what it
holds on its own lines (`HOST_TEST_SLOT=`, `lockFrom=`, `oldLockWaitSeconds=`). Read a slot number
there and not off a `fuser` listing: on 2026-11-24 an open descriptor on the old single lock was read as
"the old lock is held", and those are two different code paths.

The gpu plans are in no other gate: the fast layer is `test/plans/smoke` plus `test/plans/feat`, and
`test/plans/gpu` is under neither, so a red case there was read by nothing. Every run that takes a
reading (a plan, `-Changed`, or `-Check`) therefore ends with one line a lane can paste --
`check=gpu-plans total=<n> pass=<n> fail=<n> skip=<n> red=[<names>] red_more=<n> seconds=<n>
state=<read|noslot|nosummary> mode=report` -- and that is the whole of what the gate says about the
layer. It is not one of the checks counted in the summary above it.

A report-only reading with no ratchet is a list nobody has to act on, so the red set is compared with
`internal/tools/gpu-plans.exceptions.txt` (one case name per line, `#` for provenance). A red the file
records is a known red: printed, and it decides nothing -- a bit-exactness assertion that moves with
the driver or a co-tenant must not block a merge. A red the file does not record, or a reading that
named only part of its reds, makes the run exit `5`, because that is news and news is what a gate is
for. A recorded name that is no longer red is accepted as it is, and the run says the file can be
trimmed, so a fix never waits for the bookkeeping. `-NoGpuWatch` leaves the sweep out, and says so.

None of this stands in for the fix: it makes a red gpu case visible and the next one visible without a
person watching, and it repairs nothing. The layer is guarded when the file is empty because the cases
were fixed, not because the reading is loud.

Examples:

```powershell
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree>
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan smoke,test/plans/feat -Jobs 8
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan test/plans/feat/compile/transforms.plan.toml -Tier fast -Jobs 4 -NoBuild
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Check all
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Clean
```

The header of the script states the rest as rules: the staging push, what the `-NoBuild` guard
compares and why content rather than a commit, how a checker stage reports, why the plan's stderr is
folded into its stdout, and the exact shape of the panel's state files.

## The readings cache: one tree, one reading

`scripts/merge-readings.mjs --read` measures one plan set on one tree and caches what it measured. The
merge position asks for the same reading more than once per judgement (the tree the merge lands on,
the merged tree) and a night of merges re-measures the same tip; the rows cannot differ, and the host
should not be asked to produce them again.

The key contains the identity of the thing cached and everything a reading depends on:

| component | value |
|---|---|
| tree | `git -C <tree> rev-parse HEAD` — the commit, never the directory and never an mtime |
| plan selection | the `<count>-<hash>` from `scripts/plan-set.mjs`, the same value the baseline cache uses |
| jobs | the runner's `-Jobs` width |
| tier | `release` or `debug` — two installs of one commit are two binaries |
| tool | sha256 over `scripts/merge-readings.mjs`, `scripts/plan-set.mjs` and the runner |

A hit is replayed with a line that says the reading came from the cache and names the full tree OID;
a miss runs `camel-remote` (or `--runner`'s stand-in) and stores what came back. A dirty working tree
is neither served nor stored: a reading of commit C is not a reading of C plus an uncommitted edit.
An entry whose transcript holds no row block is not stored either — a build that stopped wrote no
reading.

**Who creates it, and how it is cleared:** the first successful store creates the cache root, its
entries and `MANIFEST.md` (which repeats this). It lives *outside every work tree* — the default is
`D:\Temp\camel-readings-cache`, `--readings-cache` or `CAMEL_READINGS_CACHE` names another one, and a
path inside a checkout is refused, because an entry there would be a file the next measurement
carries as a change. Clearing it is always safe; every entry can be measured again:

```powershell
Remove-Item -LiteralPath D:\Temp\camel-readings-cache -Recurse -Force
```

The self-test that pins the behaviour down — hit, miss per key component, dirty tree not stored, a
clone and an mtime touch still hitting — is `internal/tools/merge-readings-cache.test.mjs`; it uses a
stand-in runner and needs no build host.

## Cleaning up: the record, not the guess

Twice today a cleanup deleted something that had a user, and both times the criterion was the same
sentence — *it looks like nobody is using it*:

* a lane checkout still waiting for CI, deleted because `ahead==0 && status clean` cannot tell
  "pushed and finished" from "pushed and working";
* the shared staging bare `~/camel-work/Camel.git`, deleted by a lane clearing a temporary checkout.
  Every lane pushes through that repository, and it looks exactly like a directory nobody owns.

So rule 12 (`internal/collab/rules.md`) is not a convention here, it is a mechanism:

| Where | What |
|---|---|
| root | `~/camel-work/tmp` — the only place a run may put a temporary checkout, replay tree or sandbox |
| one run | `~/camel-work/tmp/<lane>-<time>/` |
| record | `~/camel-work/manifests/<lane>-<time>.tsv` — **outside** the root, because clearing the root must not delete the record the clear is judged by |
| checkouts | `~/camel-work/<name>` — the persistent per-worktree cache, which cannot live under the root because its build directory is reused across runs. Its creation record is `~/camel-work/.records/<name>`, written by the run that creates or takes over it. |

`camel-cleanup.sh` is the only door a run's own temporaries may go out through, and it refuses
everything else by construction:

```bash
# a run creates and records
run=$(bash internal/tools/camel-cleanup.sh init my-lane --purpose "replay of the 2026-11-23 gate" \
        | awk -F'\t' '$1=="runId"{print $2}')
bash internal/tools/camel-cleanup.sh add "$run" ~/camel-work/tmp/$run/replay --kind replay-tree

# a cleanup removes, and only this
bash internal/tools/camel-cleanup.sh rm ~/camel-work/tmp/$run/replay
bash internal/tools/camel-cleanup.sh prune "$run"          # every recorded path, then the run dir if empty
```

`rm` resolves `..`, an absolute spelling and every symlink to a real path **before** comparing, so
`tmp/x/../../Camel.git` is judged as the path it is (outside the root, refused, printed) and not as a
string that starts with the root. A path that is under the root but in no manifest is refused too:
being under the root is not being mine. The manifest stays after a prune — it is the evidence.

The host sweep applies the same idea to the persistent checkouts: it removes one only when
`~/camel-work/.records/<name>` exists, on top of the three liveness readings it already took (a free
per-checkout lock, no process inside, nothing written in ten minutes). A checkout with no record is
named and left, and `-Clean` is the explicit way to reclaim it.

## The token pool: one process, one token

`host-tokens.sh` hands out the build host's budget per **process** rather than per run. A token is a
`flock` on one of `$CAMEL_TOKEN_DIR/token-NN.lock` (default `/tmp/camel-tokens`, `$CAMEL_TOKENS` files,
default 24), held on an inherited descriptor and returned by the kernel when the holder exits -- so a
killed run cannot carry one away and there is no reclamation pass. `take N`, `take-all`, `run N -- CMD`,
`show` and `self-test` are the whole interface.

Who takes one:

| caller | how | what it holds |
| --- | --- | --- |
| each test case | `internal/tools/host-case.sh`, the one line of shell in front of every case on the host | one token for the case's whole process tree, returned when it exits |
| a build | `camel-run build` -> `host-tokens.sh run N -- ninja` | `N` tokens (default 8) |
| the clang syntax sweep | `scripts/check-clang-syntax.mjs` -> `host-tokens.sh run N -- docker ...` | `min(--jobs, pool)` for the container's life, which is also its `-P` |

Queuing is not failing. A taker that finds the pool empty waits, printing who holds what; when the wait
runs out (`CAMEL_TOKEN_WAIT_S`, default 1800) it exits **6** -- nothing was measured -- which is
neither `2` (this tool was asked something it cannot do) nor `1` (a reading came back red). A test case
never pays that price for a full pool: the runner reports it as `tokenWaitSeconds` and moves on.

The readings are the pool's own, and this is the same rule as the lock files below: `fuser` cannot tell
an open descriptor from a held lock, so "held" comes from `/proc/locks` matched against the inodes the
pool cached in `$CAMEL_TOKEN_DIR/inodes`, and the high-water mark comes from the `peak-<n>` markers the
pool writes as it hands tokens out (`show` prints `free=`, `held=`, `peak=`). The runner prints the
same thing as one line per plan run:

```
[TOKEN] pool /tmp/camel-tokens peak=24, cases with a token 1187, queued 3, tokenWaitSeconds=4.2
```

`peak` is the number to check against "two test runs and a build must stay under the pool size": it is
the largest number of tokens held at once, recorded by the pool rather than guessed by the caller.

## `camel-build.ps1`

One build at a time on this workstation, under a machine-wide lock, because several worktrees building
at once is every core compiling for ten minutes and a machine that takes the load badly. The lock is
held only by builds; a test may overlap one.

| Parameter | Meaning |
|---|---|
| `-Path <worktree>` | Required. What to build. |
| `-Parallel <n>` | Compiler processes. Default 16 of the machine's logical cores. |
| `-Config Release\|Debug` | Which build. Both take the same lock. |
| `-Check <seconds>` | Do not build; hold the lock for that many seconds. A way to watch the queue, not a check. |
| `-StateDir <dir>`, `-LocalConfig <file>` | Override the settings file. |

Exit code: the build's own; `3` when the lock was not obtained in four hours; `2` when the path is not
a Camel worktree.

## `camel-test.ps1`

One plan run at a time, under a lock of its own, so a plan is not measured while five other worktrees
are measuring theirs. It runs the plans in the order given and reports the first non-zero exit code.

| Parameter | Meaning |
|---|---|
| `-Path <worktree>` | Required. |
| `-Plan <target>[,...]` | Required. One or more plan targets. |
| `-Jobs <n>` | Parallel width. Default 8. |
| `-Tier <name>` | The runner's tier. |
| `-TimeoutMinutes <n>` | How long to wait for the lock. Default 120. |
| `-StateDir <dir>`, `-LocalConfig <file>` | Override the settings file. |

Exit code: the last non-zero plan's, or 0.

## `watch-fsh-dev.ps1`

Reads `origin`'s `fsh-dev` ref every `-IntervalSeconds` (default 60), exits 0 and prints the new commits
when it moves, and exits 3 when `-Hours` (default 8) is reached with the branch unchanged. A missing
baseline (the remote was unreachable when it started) is exit 2. `-Repo` or the settings file's
`watchRepo` names the checkout it reads.

## The panel's files

`build-state.json`, `test-state.json` and `run-registry.jsonl`, all in `stateDir`:

- the two state files hold one holder each plus a short `recent` list, and every write is a
  read-modify-write under a named mutex (`Global\CamelBuildState`, `Global\CamelTestState`) so a local
  helper and a host run cannot lose each other's update;
- a host run's holder carries `remote`, the host alias, which is how the panel tells a host run from a
  local one;
- `run-registry.jsonl` is one compact JSON line per run snapshot, keyed by `runId`, so several
  concurrent host runs are a list instead of one overwritten slot;
- a host run's line carries the width it actually ran at: `jobs` (the plan, from the plan slot or,
  for a `time_sensitive = true` plan, the whole host), `buildJobs` (the build, from the build slot),
  and `jobsRequested` (what `-Jobs` said on the command line). They are separate because they are
  separate facts. The host script sets the budget from the slot number it holds — 8 per build slot,
  16 for the plan slot, 32 for a run holding all three, which is the core count — and `-Jobs` can
  only narrow it: `-Jobs 32` in a build slot builds 8-wide, `-Jobs 4` builds 4-wide. So a `jobs`
  that disagrees with `jobsRequested` is the mechanism working. A convention ("load is high, lower
  `-Jobs` by hand") is not a mechanism: it is forgotten exactly when it matters.
- all three are advisory: a locked or unwritable file never fails a build or a plan. The write is
  `camel-gate-state.ps1#Publish-CamelGateState`: one atomic rename over the old document, a short
  retry while a reader has the file open, and then a warning and a dropped update — never an
  exception, and never the run's exit code. `camel-build.ps1` and `camel-test.ps1` used to write
  through `Move-Item -Force`, which on 2026-11-25 threw ERROR_ALREADY_EXISTS from a progress line
  after `cmake --build` had finished and reported a completed build as exit 1.
- a run's line carries the token pool as the run read it on the host: `tokens` (how many tokens that
  run held), `tokensPeak` (the highest share seen while it ran — a sampled peak, resolution one
  heartbeat), and `pool` (`{ size, inUse }`, what the pool itself said at that reading). The host
  reports the pool every 10 s whether or not the work prints anything, and a row written for that
  report also carries `stageSeconds`, the time the current stage has been running: a heartbeat whose
  only new value is `updatedAt` says the writer is alive, not that the run is doing anything. A row
  from before the pool, or from a local run that draws nothing from it, has **no** `tokens` key at
  all — absent means "not reported", and is never written as `0`, because zero reads as an empty
  pool and would invite a run that the pool cannot support.
- `queuedAt` is the moment the run started waiting for admission, and `queuedAtKind` names the gate
  it waited on (`slot` for a host build/test slot, `local` for a local helper's machine-wide mutex).
  **A row without `queuedAtKind` is a transition-period row and may only be read as `slot`; every row
  written from now on says which gate it was.** That sentence is the reading rule, not a remark: the
  default is right for the old rows by accident (they were all slot waits), and a default that
  happens to be right is the kind of inference a reader stops checking. One field is not asked to
  mean two things across one file.
- the local helpers (`camel-build.ps1`, `camel-test.ps1`) write the registry too, so a local run is
  not a run the panel cannot see. Their rows have no `tokens`: the pool is the build host's, and a
  local run draws nothing from it.
- `run-registry.ps1` is the one writer of that file; all three callers use it, because a second copy
  of the append-and-compact rule is how two writers end up disagreeing about one file.
## The two DSH plugins

Two directories here are loaded as plugins by the agent runtime rather than run as scripts. Their
source lives here so what they compute is reviewable, and the profile links to these directories
rather than holding copies:

| Directory | What it computes | Who reads it |
|---|---|---|
| `camel-lanes/` | the lane table's gates, plus `GET /camel/state` and `POST /camel/say` | every step of a coordinating session |
| `camel-oversight/` | the decision-maker channel, the watch over the coordinator, and the band above its composer | the coordinator session and the owner |

```powershell
$base = "$env:USERPROFILE\.dsh\profiles\web\node_modules\@local"
New-Item -ItemType Junction -Path "$base\camel-lanes" -Target "<repo>\internal\tools\camel-lanes"
```

`camel-oversight` is linked to a checkout of its own that tracks `origin/develop`, not to a lane's working tree; [its README](camel-oversight/README.md) says how, and how it reloads.

`camel-lanes` reads the same three files this document describes, and takes their directory from
`camel-gate.local.json`'s `stateDir` rather than holding a second copy of that decision -- two
readers naming different directories is indistinguishable from an idle machine.

`run-registry.jsonl` is the one that carries concurrent runs, one line per snapshot keyed by
`runId`. The two state files are a single slot each, so a coordinator reading only those sees one
run where there are three.
## A new worktree needs its own settings file

`camel-gate.local.json` is git-ignored, so `git worktree add` does not copy it, and a run in the new
tree stops with exit 2 **before** it transfers anything -- it never reaches the host, so nothing is
queued and no slot is taken. That failure looks like a lane that has not started, which is what three
lanes looked like on 2026-11-23 until one of them said so.

After `git worktree add`, copy the file:

```powershell
Copy-Item D:\Projects\Camel\internal\tools\camel-gate.local.json `
          <new worktree>\internal\tools\camel-gate.local.json
```

## `patches/`：装/更新被补丁的第三方包后要重打

`camel-oversight` 与 DSH 的 `dsh-git-panel` 都住在 profile 的 `node_modules` 里，而 `dsh-git-panel`
每次读主检出的改动都跑 `git status --porcelain=v1 -z --untracked-files=all`。`git status` 默认刷新
索引、拿 `index.lock`，于是它会把一次正在进行的合并挡成 `fatal: Unable to write index`。
`internal/tools/patches/dsh-git-panel-optional-locks.patch` 给它的 `runner.run` 选项加上
`GIT_OPTIONAL_LOCKS: "0"`。

**装过或更新过 `dsh-git-panel` 之后要重打这个补丁**，否则 `index.lock` 会悄悄回来。重打是一次性的：

```
git -C C:\Users\<你>\.dsh\profiles\web\node_modules\@xbzbing\dsh-git-panel apply --unidiff-zero <patch>
```

上游还没修（提 issue 需要负责人点头）。