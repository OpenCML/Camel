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
| `camel-gate.example.json` | The shape of the local settings file, placeholders only. |

## The machine-local settings file

None of these scripts names a host, a drive or a user's home. Those values come from one JSON file
that is **not** in version control:

```
internal/tools/camel-gate.local.json        # this machine's values; git-ignored
```

To create it:

1. Copy `internal/tools/camel-gate.example.json` to `internal/tools/camel-gate.local.json` beside it.
2. Fill in the placeholders. `sshHost` and `stateDir` are required; the rest have defaults.
3. Worktree-local: the file is git-ignored, so each checkout of the repository needs its own copy.

The example file gives the shape only -- every value in it is a placeholder, and no host, drive letter
or user's home appears anywhere in the repository. The real file is the one place those values live.

Rules the loader keeps:

- An unknown key is an error, not a silent no-op: a key that is misspelled would otherwise leave a
  run measuring the machine its owner thought they had changed.
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

## `camel-remote.ps1`

The gate entry. A run pushes the worktree's branch to the host's bare staging repository, writes the
uncommitted changes as a binary patch and the untracked files as a tar, sends all three in one `scp`,
and on the host puts the checkout on that commit, applies the patch, unpacks the tar, then builds and
runs what was asked. Every line comes back as it happens, the run's clock is split into transfer,
build and plan, the phases are published to the panel's state files, and the exit code is the build's
or the plan's own.

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
| `-Changed` | Run the plans the change touches (`--changed`) instead of a written list. The change is listed here, where the base branch is readable, and handed to the host as `--changed-files`; a base that cannot be read is exit 2 before the push, and it never widens to the whole fast layer. |
| `-ChangedBase <rev>` | The base for `-Changed`. Default `origin/develop`. Unreadable here is exit 2, naming the revision. |
| `-UpdateGolden` | Rewrite the named plans' goldens instead of checking them. |
| `-Clean` | Delete this worktree's host checkout, build directory included, and stop. |
| `-StackTrace` | Diagnostic build with assertion stack traces. Its readings do not decide green or red. |
| `-StateDir <dir>` | Override the settings file's `stateDir`. |
| `-LocalConfig <file>` | Use that settings file instead of the one beside the script. |
| `-LibStdCxxDir <dir>` | Override the settings file's `hostLibStdCxxDir`. |

Switches are spelled with one dash (`-NoBuild`), which is PowerShell's form; the two-dash form a shell
habit reaches for (`--NoBuild`) binds to the same switch, so it costs nothing here. What is refused is
a bare token that is not a switch at all: parameter binding is named-only, so `-Path <worktree> smoke`
stops with PowerShell's "no positional parameter accepts 'smoke'" instead of quietly becoming
`-LocalConfig smoke` -- a run that looks right and reads another settings file.

Exit codes: the build's or the plan's own; `4` when the `-NoBuild` guard refused or a host lock was
not obtained; `3` when the host is short of disk; `2` when the worktree, the push, the transfer or a
`-Check` name was wrong.

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
- all three are advisory: a locked or unwritable file never fails a build or a plan.