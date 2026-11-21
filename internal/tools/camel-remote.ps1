# The gate entry: build and run Camel's plans (and its static checkers) on the build host.
#
# This workstation does not compile Camel (its Windows build takes the machine down), and the plans
# need a real toolchain, so every reading of "is this tree green" is taken there. This script is the
# one place that moves a tree to the host and starts work on it; nothing else in the repository
# reaches the host for a build or a plan.
#
# --- this file is the gate's only entry -------------------------------------
# 闸门入口只认 `internal/tools/camel-remote.ps1`；`<worktree root>/camel-remote.ps1` 是历史遗留副本，不要用
#
# Copies of this script were once kept at each worktree's root, from before the machine-specific
# values moved into camel-gate.local.json and before the -NoBuild guard could name the build
# configuration. Such a copy is wider than this file on three counts, and it says none of them: its
# guard has no `--config` to pass, so `-Tier debug -NoBuild` verifies and accepts an install built
# Release (the options fingerprint it compares is a Release one); the plan's stderr is not brought
# back, so a plan that wrote only its reason to stderr reaches the caller as a bare non-zero exit;
# and the host, the bare repository, the panel's state file and the libstdc++ directory are baked
# into it, so a run from another machine or another host reaches the one it names. A lane that runs
# the copy therefore believes it ran the gate while it read a different, laxer reading.
#
# What a run does, in order:
#   * pushes the worktree's branch to the host's bare staging repository, writes the uncommitted
#     changes with `git diff HEAD --binary`, and tars the untracked files;
#   * sends the patch, the tar and the shell script that drives the host in ONE scp, because every
#     ssh/scp pays for a connection and the Windows client has no ControlMaster to amortise it;
#   * on the host: takes the checkout's lock, drops the checkouts whose local worktree is gone,
#     puts the checkout on this run's commit, applies the patch, unpacks the untracked files, then
#     builds, runs the plans and runs the checkers;
#   * streams every line back, splits the run's clock into transfer / build / plan, publishes each
#     phase to the panel's state files, and exits with the build's or the plan's own exit code.
#
# The host keeps one checkout per worktree name, so its build directory stays incremental. Three
# things keep that affordable, and they matter more as the number of worktrees grows:
#
#   * a run whose tree is clean, whose checkout already sits on that exact commit AND whose host
#     checkout is itself clean skips the checkout and the patch entirely -- an unchanged re-run
#     costs one ninja pass and the plan; a host checkout left dirty by an earlier run is reset and
#     re-patched instead, because otherwise the run silently measures the earlier run's sources;
#   * at the start of each run the host removes the checkouts whose worktree no longer exists here
#     (each one holds a 130-220 MB build directory), prunes day-old scratch directories under /tmp,
#     prints the free space it has, and refuses to build under 5 GB. A checkout is removed only when
#     three readings agree: this machine's `git worktree list` answered and does not name it, no
#     directory of that name is left beside the worktrees, and nothing on the host is using it (its
#     per-checkout lock is free, no process's cwd or executable is inside it, it was not written in
#     the last ten minutes). A reading that failed is never read as "gone": the run then removes
#     nothing and says why. `internal/tools/camel-remote-cleanup.test.ps1` is the proof of that;
#   * `-Clean` drops one checkout by name when a task is finished.
#
# Exit code is the exit code of the build or of the plan run, or -- when -Check was given and the
# plan was green -- of the first checker that failed. 5 means the gpu observation read a case red
# that its baseline does not name (see "the gpu layer is observed, and never gates"; the known reds
# never decide a run). 4 means the -NoBuild guard refused before anything ran; 3 means the host is
# short of disk; 2 means the worktree, the push or the transfer failed, or -Check named a checker
# that does not exist.
#
# --- where the machine-specific values live ---------------------------------
# Nothing in this file names a host, a drive or a user's home. The ssh alias, the ssh/scp binaries,
# the directory holding the panel's state files, and the host's own layout (work root, bare
# repository, toolchain prefixes) come from camel-gate.local.json beside this file, which is
# git-ignored; camel-gate.example.json shows its shape. A run that cannot find a setting it needs
# stops with exit 2 and names the file, rather than guessing and reaching the wrong machine. The
# same settings can be overridden for one run by the matching parameter.
#
# --- the staging push -------------------------------------------------------
# The push is forced and the ref is dropped in the finally: that ref is this run's scratch. The host
# fetches it just-in-time and keeps its own branch. Without the force, reusing a worktree name at
# another commit is a non-fast-forward that stops the run before the host; without the delete, one
# ref per worktree name outlives every run and goes stale the moment its name is reused.
#
# --- -NoBuild is guarded ----------------------------------------------------
# A build writes out/<tag>/build-info.json -- from scripts/collect-out.js, through
# scripts/common.js: writeBuildInfo -- recording the HEAD it was built at, whether that tree was
# dirty, a SHA-256 over the content of every path the build reads (src, include, modules, tools,
# stdlib, third_party, cmake, CMakeLists.txt, conanfile.txt), and the build options it compiled with
# (scripts/common.js: buildOptions: the `-D` values a build would pass, the two environment
# overrides cmake/Options.cmake honours, the platform and the compiler selection). `-NoBuild`
# compares that stamp with the checkout it is about to verify, on the host, before the plan starts:
#
#   * same content, same options -> nothing is printed and the plan runs (a correct run gains no
#                          noise);
#   * different content -> exit 4, with both revisions (HEAD + dirty state), both fingerprints and
#                          the source files that moved;
#   * different options -> exit 4, with both fingerprint hashes and every option that moved
#                          (`ENABLE_FASTVM_JIT: built OFF, a rebuild would use ON`): the sources are
#                          the same bytes, but a no-JIT install and a JIT install are two different
#                          binaries and reusing one for the other answers a question the tree never
#                          asked;
#   * another configuration -> exit 4 for the debug tag only, which is the one tag the configuration
#                          follows from (out/debug is the Debug build); out/latest holds Release and
#                          RelWithDebInfo, so no `--config` is passed there and `-Tier debug
#                          -NoBuild` keeps verifying out/debug without building;
#   * no stamp, or a stamp from before the option record -> exit 4: it cannot be proven to match,
#                          so it is refused rather than trusted; build once without -NoBuild;
#   * -AllowStale       -> the mismatch is still printed, followed by "continuing anyway"; the run
#                          proceeds (explicit, loud, and never implied).
#
# Why content and not the commit: a commit that only touches docs/, test/ or scripts/ changes HEAD
# but not one byte the build consumes, and refusing there would train people to pass the override.
# test/ and docs/ changes are meant to take effect without a rebuild (a test case is read at plan
# time); `-NoBuild` reads out/<tag>/stdlib, which the build copies from stdlib/, so a stdlib/ change
# does move the fingerprint.
#
# `-Tier debug` makes the plan runner read out/debug, so the guard checks out/debug; every other run
# checks out/latest -- the guard checks the install the plan will actually use, not the one the last
# build wrote.
#
# A checkout that predates the guard has no scripts/build-info.js: the run prints a warning that it
# cannot be verified and continues. That is the one honest hole: an old branch cannot be guarded, and
# refusing would block every old worktree. With git unavailable the stamp records no revision and
# says so; the fingerprint still works (it is file content), so the guard stays effective.
#
# --- -Check runs the repository's own checkers there ------------------------
# The static checkers have to run against the tree this run just built and patched, and a lane that
# opens its own ssh session to do it carries a hand-written copy of every checker's command line
# (and of the environment it needs) that drifts from the repository's. `-Check <name>[,<name>]`
# runs them on the host instead, after the build and after the plan when one is given, and prints
# each checker's own stdout and stderr verbatim between a header naming it and a footer naming its
# exit code:
#
#   * the names are the table below, and every entry is the invocation the repository already uses,
#     so a lane runs what the gate runs and not a paraphrase. `-Check all` runs all of them in order,
#     except the entries marked `planStage`: those read the plan stage's own record and are asked for
#     by name beside the plan they read (`-Plan <target> -Check plan-debt`), because a check with no
#     reading of its own must refuse rather than pass over nothing;
#   * `all` is therefore the static layer and not the whole table, and the closing summary says which
#     entries that leaves out instead of leaving it to the reader: the line after "N run" names every
#     `planStage` entry this run skipped, and a run that named one prints that it is not part of
#     `all`. Both read the table as it stands, so a new `planStage` entry appears there by itself;
#   * there is a second marking that keeps an entry out of `all`, for the other reason a check can be
#     asked for by name rather than started by every gate: `namedOnly`. `e9-full` is that entry -- the
#     E9 differential at the tier its ratchet is defined at, 500 seeds and 31,680 cells, half an hour
#     on this host -- and it is out of `all` because a stage that every `all` started is a stage
#     people learn to skip, which is the same reason `test/tools/e9diff/README.md` gives for the tool
#     having no plan of the two slower tiers. Its reason is its cost, not a missing reading, so the
#     closing summary names it on a line of its own (`named-only check(s) not run by -Check all`)
#     rather than under the plan-stage line;
#   * an entry marked `reportOnly` is not a wall yet: it runs on every gate and prints one line a
#     lane can paste, `check=<name> findings=<n> mode=report`, where the number is the checker's own
#     count and never a second count kept here. Its exit code does not decide the run's, so `-Check
#     all` stays green while the number is still coming down; the closing summary tags it `report
#     only`, because a red check that does not fail the run must not be read as a green one either.
#     An entry becomes a wall by losing that marking, and the checker's own `--enforce` is the mode
#     it then runs in;
#   * the stages are build -> plan -> checks, so one call answers both questions and a failing plan
#     still gets its checkers (their readings are not thrown away with the plan's);
#   * the run exits non-zero when any checker failed, and the exit code is the plan's when it failed,
#     otherwise the first failing checker's. A name this script does not know is exit 2 before
#     anything runs: a check that was silently skipped is a green that proves nothing;
#   * -NoBuild's guard runs before the checkers, exactly as it does before a plan, so a checker that
#     reads the install cannot be pointed at another tree's build.
#
# The entries marked `compiler` start the tree's own `camel` binary, so they are pointed at this
# run's install with `--camel out/<tag>/bin/camel` -- the tag the run built and -NoBuild guarded,
# not the default. A current install carries the toolchain's libstdc++.so.6 in its `libs/` and
# finds it through its own `$ORIGIN` rpath, so no stage needs a library export of its own; the one
# global export further down serves older installs still sitting in host checkouts.
#
# --- the gpu layer is observed, and never gates a known red -----------------
# test/plans/gpu is in no timed gate. The fast layer is test/plans/smoke + test/plans/feat, and
# `gpu/**` is under neither, so the red cases of test/plans/gpu/tiles.plan.toml -- bit-exactness
# assertions that move with the driver and with whoever else is on the card, plus cases that have
# been red long enough that nobody remembers them being green -- were read by no gate at all. They
# ran only when a -Changed directory rule named that plan file and "a plan file named as a target
# runs in full" skipped the layer, which is why they stayed red. A red nobody reads is not one that
# was decided; it is one that was never looked at.
#
# So the gate reads them, and decides only what is news. `gpu-plans` reuses the `reportOnly` shape of
# the table below -- one machine-readable line, an exit code kept out of `checkfail` -- but it is a
# plan run and not a checker, so it is emitted as a block of its own after the checkers instead of a
# table row. `N run` in the closing summary is the static layer's count (20 under `-Check all`), and
# a twenty-first row would have changed a number every lane compares. The number it prints is the
# runner's own [SUMMARY] reduced to one line, never a second count kept here, and it is `unknown` --
# not zero -- when the runner printed no summary at all. It runs on the caller's -Jobs, after the
# checkers, and under the host's plan lock: it *is* a plan run and not a reader of one, so a number
# taken beside five other lanes would not be comparable.
#
# What it prints is `check=gpu-plans total=<n> pass=<n> fail=<n> skip=<n> red=[<names>]
# red_more=<n> seconds=<n> state=<read|noslot|nosummary> mode=report`. `state` says whether the layer
# was read at all: `noslot` is a host whose plan lock was never obtained (no reading, and no red
# invented from it), `nosummary` is an observation that could not read its own runner.
#
# --- and the ratchet is what keeps it from being background noise -------------
# A report-only reading with no ratchet is a reading nobody has to act on: the layer could grow a new
# red every week and every gate would still be green, and a list that is allowed to grow is a list
# nobody reads. So the red set is compared with one that was written down,
# `internal/tools/gpu-plans.exceptions.txt` (one case name per line, `#` for provenance, the same
# plain-text shape the repository's exception lists use), and the comparison is the hard part of the
# rule:
#
#   * a red the file names is a known red: printed on every gate, and it decides nothing. It is
#     known because a GPU reading depends on the card, the driver and the co-tenants (docs/
#     test-plans.md, "CUDA visibility", and "A card that is not device 0"): a bit-exactness assertion
#     that fails on this host must not block a merge;
#   * a red the file does not name is news, and news decides this run: exit 5. The file says what
#     was red when it was written, so a case that is red now and is not in it is a change -- and a
#     change is what a gate is for. `red_more>0` (a reading that named only part of its reds) is
#     news too: the names it did not print are exactly the ones nobody would look at, so the
#     comparison refuses rather than passing over them;
#   * a name in the file that is no longer red is accepted as it is, and the run says the file can be
#     trimmed. A fix must not have to wait for the bookkeeping to land in the same change, and a
#     stale entry is not a failure -- it is a line to delete;
#   * a sweep that was never taken (`state=noslot`) decides nothing and says so; a sweep whose runner
#     printed no summary (`state=nosummary`) is a fault in this observation, printed as one, and it
#     decides nothing either -- the exit code is for a gpu case that turned red, not for a host that
#     was busy or a tool that broke.
#
# **This does not stand in for the fix, and it does not guard the layer.** The line makes a red gpu
# case visible, the ratchet makes the *next* one visible without a person watching, and nothing here
# repairs a case: the names in the baseline stay red until someone fixes them. The layer becomes
# guarded by fixing them and trimming the file to empty; only then is this observation a wall, and it
# becomes one by having nothing left to report, not by losing `reportOnly`.
#
# Two selection traps, each of which would print the same false green this line exists to remove:
# `test/plans/gpu` is a directory, so it is read at a tier, and the default tier is `fast` while the
# cases this observation exists for are `tier = "slow"` -- so `--tier all` is passed rather than the
# caller's `-Tier`; and a run that selected nothing prints the same three numbers as a green one,
# which is why the reading above is `unknown` when there is no summary line to reduce.
#
# --- the plan's stderr travels with its stdout ------------------------------
# A plan's reason for being red is written to stderr by the runner ("this run selected no test at
# all", the ENOENT stack of a mistyped plan path). That reason has to arrive, or a red gate hands
# back an exit code and nothing a reader can act on -- indistinguishable from a run that measured
# nothing. So the plan is started with `2>&1` on the host, before anything leaves this script:
# the reason then travels on the same stream as RUN_EXIT and the markers, in the order it happened,
# instead of as a second connection-level side channel that arrives interleaved or not at all.
#
# --- what the panel sees ---------------------------------------------------
# The desk panel reads three files: build-state.json and test-state.json -- the same two the local
# helpers write, same field names, plus one extra field so the panel can tell a host run from a
# local one -- and run-registry.jsonl, one compact JSON line per run snapshot so several concurrent
# host runs are a list instead of one overwritten slot:
#
#   { "updatedAt": iso,
#     "holder": { "worktree": "verify", "config": "Release", "path": "<the worktree>", "pid": 123,
#                 "queuedAt": iso, "startedAt": iso,
#                 "done": 12, "total": 453, "phase": "compiling", "lastLine": "...",
#                 "remote": "<the host>" } | null,
#     "waiters": [ ... ],                 # never added here: a host run queues for nothing local
#     "recent": [ { "worktree": "...", "queuedAt": iso, "startedAt": iso, "finishedAt": iso,
#                   "exit": 0, "seconds": 120, "remote": "<the host>" } ] }
#
# The test file has the same shape, with camel-test.ps1's holder fields instead of config/done/total:
#
#     "holder": { "worktree": "...", "path": "...", "pid": 123, "queuedAt": iso, "startedAt": iso,
#                 "plan": ["smoke"], "target": "smoke", "tier": "", "jobs": 12,
#                 "phase": "running", "lastLine": "...", "remote": "<the host>" } | null,
#     "recent": [ { ..., "plan": ["smoke"], "tier": "", "remote": "<the host>" } ]
#
# `remote` is the host alias, so the panel shows one run per machine. The build holder is set before
# the first slow step (the push) and lasts through the host build; the plan then replaces it with a
# test holder. Every write takes the same Global\CamelBuildState / Global\CamelTestState mutex as the
# local helpers, so a local build and a host run cannot lose each other's update. A host run does NOT
# take Global\CamelBuildSerialized / Global\CamelTestSerialized: those locks mean "this machine's
# cores", and the work is on the host. It therefore never queues and never appears in `waiters`.
# Two host runs on different worktrees can run at once (the host serialises its own work); since the
# state files have one holder slot, the run that wrote the holder last is the one the panel shows,
# and a finishing run only clears the holder when that holder is still its own (same pid and remote).
#
# If the host run dies or this script is interrupted, the finally block clears the holder: a stale
# holder would tell the panel that work is still going on when it is not.
#
# --- usage ------------------------------------------------------------------
#
#   pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree>
#   pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> `
#        -Plan test/plans/feat/modules/graph.plan.toml -Jobs 16
#   pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan smoke -NoBuild
#   pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Config Debug -Plan smoke -Tier debug
#   pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Plan feat/opt -Check plan-debt
#   pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <worktree> -Clean

# Named binding only: a token that is not a switch must not become the next parameter by position.
# Without this, `-Path <worktree> smoke` binds `smoke` to -LocalConfig -- a run that looks right and
# reads another settings file, or stops with a message about a setting the caller never named.
[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    # The local settings file; empty means camel-gate.local.json beside this script. That file is the
    # only place a machine-specific value (the host, the ssh binaries, the state directory, the
    # host's layout) may appear -- see camel-gate.example.json.
    [string]$LocalConfig = '',
    # The directory holding build-state.json, test-state.json and run-registry.jsonl. Empty means
    # the local settings file decides.
    [string]$StateDir = '',
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release',
    # One plan file or plan directory per element; empty means "build only".
    [string[]]$Plan = @(),
    [int]$Jobs = 16,
    # 'debug' selects the runner's debug tier (Debug build required).
    [string]$Tier = '',
    # Skip the build and run the plan against whatever is already built there. Refuses (exit 4)
    # when out/<tag>/build-info.json says that install was built from different sources; see the
    # header. `-AllowStale` overrides the refusal explicitly.
    [switch]$NoBuild,
    # Run -NoBuild even when the install was built from different sources. The guard still prints
    # the mismatch and says it is continuing; this switch is never implied.
    [switch]$AllowStale,
    # Drop this worktree's checkout on the host, build directory included, and stop. The branch
    # itself lives in the bare repository and locally; only the compiled output is lost.
    [switch]$Clean,
    # Diagnostic build: assertion stack traces on (`npm run build -- --stack-trace`). Off by default;
    # **its readings do not decide green or red** -- use it to read a trace, then rebuild without it.
    [switch]$StackTrace,
    # Run the plans the change touches instead of a written list: `node scripts/test.js --changed`,
    # with `origin/develop` as the base unless -ChangedBase names another revision. This is the
    # per-merge gate; the full fast layer is for batch points, not for every merge.
    # The change is listed *here*, where the base branch exists, and travels with the patch as
    # `--changed-files`: the host holds the lane's commits but not the base branch (the staging
    # repository is not upstream and the host reaches no upstream), so a selection made there
    # answered "cannot read origin/develop" and every gate run took the widening to the whole fast
    # layer without printing a symptom. A base that cannot be read here is exit 2 before the push,
    # naming the base and the layer it declines to run; it is never a run of the whole layer.
    [switch]$Changed,
    [string]$ChangedBase = 'origin/develop',
    # Rewrite the goldens of the named plans instead of checking them: runs the golden tool,
    # not the runner, because a golden mismatch cannot be fixed through the checking path.
    [switch]$UpdateGolden,
    # Re-record the front-end AST snapshot instead of checking it: runs
    # `test/tools/parsecheck/check.py snapshot --update` on the host, after the build and before the
    # plan, and brings `test/plans/feat/parse/golden/ast_hashes.txt` back into this tree.
    #
    # This exists because `-UpdateGolden` cannot record that file: `--update-golden` rewrites only
    # `kind = "snapshot"` goldens, while `frontend.ast_snapshot` is `tool = "python"` and the runner
    # never passes `--update` to it. A `.cml` case added to the tree therefore had no way of getting
    # into the record through the gate at all -- the record had to be made by hand on the host, which
    # a lane cannot do, and a lane that skipped it left a red window on the base branch. The record is
    # a file of *this* tree, so the tool runs where the sources are and the file travels back on the
    # same stream as the reading (base64, between two markers), which needs no second connection.
    #
    # The stage runs before the plan on purpose: the plan reads the record, so a record that failed
    # must stop the run rather than be read later as a plan that failed. `-Plan <the parse plan>`
    # beside it is the verification, and needs no separate call.
    #
    # It prints the build time of the binary it records with -- `out/<tag>/bin/camel-parsecheck built
    # YYYY-MM-DD HH:mm` -- because the one failure mode this stage cannot show on its own is a stale
    # binary: with `-NoBuild` beside it, the record is taken by whatever the host built last, which may
    # be days older than the tree asking for it, and the line then says `(NOT REBUILT -- may be
    # stale)` instead of looking like a fresh build. A record whose provenance is invisible is how a
    # golden gets silently re-recorded from the wrong parser.
    [switch]$UpdateAstHashes,
    # With -UpdateAstHashes, re-hash only these files (globs, repeatable) instead of the whole list.
    # `--only` restricts what is rewritten, not what the record must cover, so the check that follows
    # still reads every line: recording the whole list also *replaces* every line that no longer
    # matches, which would turn a golden into whatever the tree happens to produce, while a name
    # selected here can only move the lines it names.
    [string[]]$AstOnly = @(),
    # With -UpdateAstHashes, a `test/plans/feat/parse/golden/ast_hashes.txt` path this recording may
    # take the row out of (repeatable). A re-record is a whole-table rewrite: a row leaves the record
    # when the tree that records does not hold its file, which is how a bulk re-record on a tree
    # missing seven `.cml` files deleted their seven rows on 2026-11-17 (`agents/test.md`). The
    # write-back therefore refuses to drop a row while that file is still here, and refuses a bulk
    # re-record that drops any row at all; this switch is how a case that really is deleted leaves
    # the record, and each name is printed as it is written. Names are paths as the record spells
    # them (`test/cases/parse/x.cml`), and the host takes them as `--drop`.
    [string[]]$AstDrop = @(),
    # Run these of the repository's static checkers on the host, after the build and after the plan:
    # names from the table below, comma-separated, or `all` for every one of them. Each checker's
    # output is printed verbatim with its exit code, and the run exits non-zero when one failed.
    # `all` leaves out the entries marked `planStage` (they read the plan stage's own record and are
    # named beside the plan they read); the run's closing summary names the ones it left out.
    # An unknown name is exit 2 before anything runs; see "what the panel sees" and the -Check
    # section of the header for the stages and the exit code.
    [string[]]$Check = @(),
    # Leave the gpu observation out. Every run that takes a reading also runs test/plans/gpu once and
    # prints one `check=gpu-plans ... mode=report` line (see the header, "the gpu layer is observed,
    # and never gates"); it never decides the exit code. This switch is for a run that wants the
    # gate's own readings without that extra sweep -- and it names what it left out, because an
    # absent line and a green one read the same from the outside.
    [switch]$NoGpuWatch,
    # Which dirty tracked files a run may carry from the MAIN checkout. The patch this script sends
    # is `git diff HEAD`, i.e. the working tree, not the commit: a file somebody edited and did not
    # commit is built and measured as part of the change. In a lane's own worktree that is the point
    # -- the author is measuring the edit. In the main checkout it is never the point, because the
    # main checkout is where a commit is made, so a dirty file there belongs to somebody else's
    # unfinished work and a green gate would be reporting "measured" about a mixture of two changes.
    # A machine-local value (the gate settings file is git-ignored) usually belongs here.
    [string[]]$AllowDirty = @('internal/tools/camel-remote.ps1'),
    # The toolchain's libstdc++ directory, put first on the single LD_LIBRARY_PATH this run exports
    # (see the header). A current build carries its own copy in `libs/` with a `$ORIGIN` rpath, so
    # the export only serves the older installs still sitting in host checkouts. Empty means the
    # local settings file decides.
    [string]$LibStdCxxDir = '',
# A file inside this tree that the host sends back after the run: base64 between two markers on the
# same stream as RUN_EXIT, which is the channel `-UpdateAstHashes` already uses (one channel, so one
# place to fix it). Relative to -Path and it must stay inside the tree.
#
# What it is for: a check that needs two runs -- `-UpdateGolden` writes a record, the next run
# compares against it -- cannot leave the record on the host, because the host checkout is reset to
# the patch on every run: an untracked file written by one run is gone by the next (measured
# 2026-11-16: the golden written by `-UpdateGolden` was reported missing by the run right after it).
# Pair this with a path `.gitignore` covers (e.g. `out/trace-golden/...`), so a fetched record can
# never be committed by accident.
[string]$FetchFile = ''
)

$ErrorActionPreference = 'Stop'

# --- the machine-local settings --------------------------------------------
# Nothing below names a host or a path on this workstation: those come from
# camel-gate.local.json beside this script (git-ignored), and a run that needs a setting the file
# does not carry stops here, with the file to create, instead of reaching the wrong machine.
. (Join-Path $PSScriptRoot 'camel-gate-config.ps1')
# The host-checkout sweep's reading: which worktrees still exist, read so that a failed reading is
# kept apart from an empty one. Its own file because it is the one decision here that can delete
# another lane's work, and `camel-remote-cleanup.test.ps1` has to be able to call it.
. (Join-Path $PSScriptRoot 'camel-remote-live.ps1')
$cfg = Get-CamelGateConfig -LocalConfig $LocalConfig -ScriptRoot $PSScriptRoot -Override @{
    stateDir         = $StateDir
    hostLibStdCxxDir = $LibStdCxxDir
}
if (-not $cfg.sshHost) { Stop-CamelGateConfig 'camel-remote' $cfg 'sshHost' }
if (-not $cfg.stateDir) { Stop-CamelGateConfig 'camel-remote' $cfg 'stateDir' }
$sshHost = $cfg.sshHost
$sshExe = $cfg.sshExe
$scpExe = $cfg.scpExe
$bare = $cfg.hostBareRepo
# The bare repository's own directory name: the host's sweep of dead checkouts skips it by name.
$bareLeaf = Split-Path -Leaf $bare
# Stored as holder.remote and recent.remote; also how a writer recognises its own holder.
$remoteName = $sshHost

# `-File script.ps1 -Plan a,b` can arrive as the single string "a,b"; split and drop empties so
# `target = $Plan[0]` is a real target and the plan list matches what camel-test.ps1 publishes.
$Plan = @($Plan | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

# The checkers -Check knows, in the order `all` runs them. Every entry is the invocation the
# repository already uses, so this channel cannot drift into checking something else than the gate
# does; `compiler = $true` marks the entry that starts the tree's own `camel` binary and therefore
# has to be pointed at this run's install (see the header).
$checkers = [ordered]@{
    'diagnostics-catalog' = @{ script = 'scripts/diagnostics-catalog.mjs'; args = @('--check'); compiler = $false }
    'camel-for-ai'        = @{ script = 'scripts/camel-for-ai.mjs'; args = @('--check'); compiler = $false }
    'check-doc-links'     = @{ script = 'scripts/check-doc-links.mjs'; args = @('--citations'); compiler = $false }
    'headers'             = @{ script = 'scripts/headers.mjs'; args = @('--check'); compiler = $false }
    # A plan's fixtures can sit in a directory named after the plan, beside it: the short name still
    # reaches `D.plan.toml`, the full path reaches the fixtures and selects nothing. The runner
    # refuses that target now; this check keeps the pairing documented rather than discovered.
    'plan-dir-markers'    = @{ script = 'scripts/plan-dir-markers.mjs'; args = @('--check'); compiler = $false }
    'check-grammar'       = @{ script = 'scripts/check-grammar.mjs'; args = @('--check'); compiler = $false }
    'stdlib-reference'    = @{ script = 'scripts/stdlib-reference.mjs'; args = @('--verify-ops'); compiler = $true }
    # The conformance matrix: recorded readings compared with the implementation before and after
    # std::opt, on the four back ends. It starts the tree's binary, so it is a compiler entry too.
    'conformance'         = @{ script = 'scripts/check-conformance.mjs'; args = @(); compiler = $true }
    # The two generated files that are neither a document nor a golden: each marks its own tree
    # against its generator byte for byte, so a generated file that drifted from the script that
    # owns it fails here instead of at the next person who re-runs the generator. Both resolve
    # their paths from __file__, so the working directory does not matter; `interpreter` defaults
    # to node and is the only entry shape that is not a node script.
    'export-matrix'       = @{ interpreter = 'python3'; script = 'scripts/gen_export_matrix.py'; args = @('--check'); compiler = $false }
    'model-generators'    = @{ interpreter = 'python3'; script = 'benchmarks/resnetbn/tools/gen_model_cml.py'; args = @('--check'); compiler = $false }
    # The rule library decided by an SMT solver, the last static check of the gate. `--require-solver`
    # and not the bare form: this is the one checker whose dependency can be missing, and a check that
    # skips when its solver is absent is not a check. The script searches $CAMEL_Z3, `z3` on PATH and
    # ~/.local/bin/z3 itself, so the host's copy is found without an environment variable here; it
    # reads sources only and never starts the tree's binary, so it is not a compiler entry.
    'rule-proofs'         = @{ script = 'scripts/rule-proofs.mjs'; args = @('--require-solver'); compiler = $false }
    # The plan's own prose against the plan's own reading: a `desc`, a `known_fail_note` or a `#`
    # comment that says its case is red today must find that case running red in the run this gate
    # just took (`scripts/check-plan-debt.mjs`). It is the one checker whose evidence is the plan
    # stage and not the checkout, so it needs a plan in the same call -- `-Plan <target> -Check
    # plan-debt` -- and it is left out of `all`: with no plan selected it has no reading to judge
    # against, and it says so (exit 2) rather than passing over nothing.
    'plan-debt'           = @{ script = 'scripts/check-plan-debt.mjs'; args = @(); compiler = $false; planStage = $true }
    # The E9 differential at the tier its ratchet is defined at: 500 seeds, 99 cells each, 31,680
    # cells, and the three numbers a merge is judged by, compared with
    # `test/tools/e9diff/baseline.json`. Until this row existed that run was reachable only from a
    # hand-written shell on the host, which on the books is the same as not being reachable -- while
    # `test/tools/e9diff/README.md` says why it has no plan of the two slower tiers: "a full run is
    # 25 minutes on this host, and a gate that starts one by itself is a gate people learn to skip".
    # Both halves of that reason survive here: there is still no plan, and `all` still does not
    # start it. It is `namedOnly` -- asked for by name, like the plan-stage checks, and out of `all`
    # for its own reason: it is slow, and it is a reading somebody asks for, not a static layer. The
    # script adds no judgement of its own; the tool decides against its baseline and its exit code
    # is this entry's (`scripts/check-e9-full.mjs`).
    'e9-full'             = @{ script = 'scripts/check-e9-full.mjs'; args = @('--matrix', 'full'); compiler = $true; namedOnly = $true }
    # The whole static layer of the release gate, in the gate's own order and with the gate's own
    # criterion: this entry exists because the layer had never actually been run on the mainline.
    # The merges only ever ran plans, and the gate's static phase -- thirteen checks, four of which
    # start the tree's binary -- was measured for the first time on 2026-11-03, where all four of
    # those four failed with `GLIBCXX_3.4.36 not found`. The cause was not four defects but one:
    # `release-gate.mjs` appends `--camel <install>` to the four checks that read the install and
    # says nothing about the library that install needs, so they loaded the host's system
    # `libstdc++.so.6`, which is older than the one the binary links against. The install answers
    # that itself now (26888783c: the toolchain's copy in `libs/`, named by a `$ORIGIN` rpath), so
    # the gate's criteria are untouched and it runs in the same environment as every other command
    # in this script, with no per-stage prefix of its own.
    'gate-static'         = @{ script = 'scripts/release-gate.mjs'; args = @('--only', 'static'); compiler = $true }
    # A plan field the entry's kind never evaluates is not a test: the entry reads as a comparison and
    # makes none (`docs/test-plans.md`, "Fields a Kind Does Not Read"; the shape it was written for was
    # a `negative` entry carrying `compare_to`, green for seven months while it checked only an exit
    # code). It lives under test/tools/ because its subject is the plan tree, and its table is checked
    # against `validateTest` by `--verify-runner`, so the guard cannot outlive the code it describes.
    # A wall rather than a report-only entry: it is pure static analysis, deterministic, a few
    # seconds, and its count is already ratcheted in test/tools/plan-fields-baseline.json -- findings
    # may not grow past that file, and a missing baseline is itself a red rather than a pass over
    # nothing. 331 findings on 2026-11-14; the number coming down is the work item, and this entry
    # going red is what says it went up.
    'plan-fields'         = @{ script = 'test/tools/check-plan-fields.mjs'; args = @(); compiler = $false }
    # The front end's AST snapshot against the tree, before the plan that is red on it: every `.cml`
    # the tree holds, and the probes `probes.py` makes, hashed by `camel-parsecheck hashes` -- the
    # tool `frontend.ast_snapshot` itself starts -- with the line
    # `test/plans/feat/parse/golden/ast_hashes.txt` records for each compared with the reading the
    # file gives now. A `.cml` whose reading moved, or a `.cml` nobody registered, is a red on the
    # base branch that the merge position has to find and patch after the fact; that happened five
    # times (`1dad3104f` is the fifth: `4363 lines, 3 added, 3 removed`), and each time the repair
    # was right and the reader was late. This is the reader: one process, one line per stale file,
    # from the lane's own worktree, before the push.
    #
    # `compiler = $true`: it starts the install's `camel-parsecheck`, beside the `camel` the
    # `--camel` argument names, so it reads the install this run built and -NoBuild guarded. A
    # `.cml` edit is in the build stamp's fingerprint (`scripts/common.js`, `sourceFingerprint`),
    # so `-NoBuild` after one needs `-AllowStale` -- the parser's reading of a file does not depend
    # on the tree its binary was built from, which is the one thing that stamp cannot know.
    'ast-hashes'          = @{ script = 'scripts/check-ast-hashes.mjs'; args = @(); compiler = $true }
    # A comment says what the code does and why; it does not record how the project got there. This
    # checker reads comment text only -- after `//`, inside `/* */`, with `#` in CMake -- so a date or
    # a path that is data in a string literal is not its business, and it counts a comment that names
    # a dated ruling, a handover page, a role or a lane, a slice code, a host path. A wall with a
    # threshold of zero: the tree carries none of those comments, so the first one added is this
    # run's red, and the checker's own exit 1 -- with the `file:line` list it prints above it -- is
    # what the gate returns.
    'process-traces'      = @{ script = 'scripts/check-process-traces.mjs'; args = @(); compiler = $false }
    # --- report only -----------------------------------------------------------
    # The checks the target architecture asks for before they are walls: they run on every gate and
    # publish their own reading, and their exit code does not decide the run's. `findings` is the
    # `sed -E` expression that reduces the checker's own summary line to its count, so the number
    # printed here is the number the checker printed -- the list is one work item, and the number
    # coming down is what this line exists to show. `--enforce` is deliberately absent: the checker
    # offers it as the mode this entry becomes once its list is empty, and a lane that switched it
    # on now would gate the baseline each entry printed on 2026-11-14.
    'program-holders'       = @{ script = 'scripts/check-program-holders.mjs'; args = @(); compiler = $false; reportOnly = $true; findings = 's/.*check-program-holders: ([0-9]+) C\+\+ Program holder.*/\1/p' }
    # The three layering rows of the target architecture's section 7 (守护), each read as its own
    # script: only the semantic layer enumerates a kind of edge; an effect is answered by the fact
    # layer and not assembled from `isPure`/`hasExternalEffect`; an operator's URI is spelled in its
    # definition and its registry, not in business code. Each script's header comment states the
    # rule it derives its boundary from -- the declaring header, the fact that answers the
    # question, the files that define and register an operator -- because that boundary is the part
    # a reader has to be able to argue with.
    'edge-kind-layer'       = @{ script = 'scripts/check-edge-kind-layer.mjs'; args = @(); compiler = $false; reportOnly = $true; findings = 's/.*check-edge-kind-layer: ([0-9]+) edge-kind call.*/\1/p' }
    'effect-facts'          = @{ script = 'scripts/check-effect-facts.mjs'; args = @(); compiler = $false; reportOnly = $true; findings = 's/.*check-effect-facts: ([0-9]+) effect-atom call.*/\1/p' }
    'operator-uri-literals' = @{ script = 'scripts/check-operator-uri-literals.mjs'; args = @(); compiler = $false; reportOnly = $true; findings = 's/.*check-operator-uri-literals: ([0-9]+) operator URI literal.*/\1/p' }
    # The capture census: the two numbers the closure-capture work is accepted by -- `:var/new` nodes
    # and capture ports bound by reference -- read out of `std::jgir` (the JSON GIR; `std::stats`
    # carries neither an operator breakdown nor a capture table) over `test/cases/**`, with the
    # fingerprint of the file set it counted (`list_sha`, the paths and their content hashes) printed
    # beside them. It is a reading and not a wall: the author's target is 0/0 and there is no baseline
    # yet, so a red here would invent the threshold it claims to hold. `findings` reduces its summary
    # line to `capture_ports`, the number that line leads with; the script's `--enforce` is the mode
    # this entry becomes once the reading is 0/0. The script's header states the scope, the counting
    # rule and what the repository-wide corpus would add (`--all`).
    'capture-census'        = @{ script = 'scripts/check-capture-census.mjs'; args = @(); compiler = $true; reportOnly = $true; findings = 's/.*check-capture-census: ([0-9]+) reference capture port.*/\1/p' }
}

# The same "a,b can arrive as one string" split as -Plan, so `-Check all` and `-Check a,b` both
# work. An unknown name stops here: nothing has been pushed yet, and the alternative -- running the
# checkers that happen to match -- would report a green over the ones that were never run.
$Check = @($Check | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$checkNames = @()
foreach ($requested in $Check) {
    if ($requested -eq 'all') {
        # A `planStage` entry reads the plan stage's own record, so it is not part of the static
        # layer `all` means: it would refuse (exit 2) on every call that selects no plan, which is
        # what `-Check all` is for. It is asked for by name, beside the plan it reads.
        # A `namedOnly` entry is out of `all` for the other reason a check can be: it is a deliberate
        # reading -- `e9-full` is a 25-minute differential -- and a stage that every `all` started
        # would be a stage people learn to skip. Both marks mean "named by the caller", so both are
        # skipped here, and the closing summary names which ones and why.
        foreach ($known in $checkers.Keys) {
            if ($checkers[$known].planStage -or $checkers[$known].namedOnly) { continue }
            if ($checkNames -notcontains $known) { $checkNames += $known }
        }
    } elseif ($checkers.Contains($requested)) {
        if ($checkNames -notcontains $requested) { $checkNames += $requested }
    } else {
        Write-Host "[camel-remote] unknown -Check name '$requested'; known names: $(($checkers.Keys) -join ', '), all"
        exit 2
    }
}
$checkList = $checkNames -join ' '

# What `all` cannot mean, read out of the table above and never out of a list written beside it.
# Every entry marked `planStage` is a check whose evidence is the plan stage, and `all` has no plan
# to offer it. Typing those names here would just move "remember which checks are named-only" from a
# person's head into a second list that misses the next added entry; derived from `$checkers`, the
# closing line below picks a new `planStage` entry up with no edit of its own.
#
# `$planStageSkipped` is what this run left out, not what `all` leaves out: `-Check all,plan-debt`
# runs it, and a line claiming otherwise would be the same wrong reading this line exists to remove.
$checkHasAll = $Check -contains 'all'
$planStageNames = @($checkers.Keys | Where-Object { $checkers[$_].planStage })
$planStageSkipped = @($planStageNames | Where-Object { $checkNames -notcontains $_ })
# The same reading for the entries `all` leaves out because they are asked for, not because they
# have nothing to read: a run that printed one count over both reasons would say "named-stage" about
# a check whose reason is its cost.
$namedOnlyNames = @($checkers.Keys | Where-Object { $checkers[$_].namedOnly })
$namedOnlySkipped = @($namedOnlyNames | Where-Object { $checkNames -notcontains $_ })

# The gpu observation, beside the table and deliberately not in it: it is a plan run, so it is no
# row of `$checkers` -- and `N run` in the closing summary must stay the static layer's count (20
# under -Check all), which a twenty-first row would have changed for every lane that compares that
# number. It reuses the report-only shape instead (one machine-readable line, an exit code that never
# reaches `checkfail`) and is emitted as a block of its own after the checkers; the header section
# "the gpu layer is observed, and never gates a known red" states what it is, what it is not, and how
# its baseline ratchet decides.
#
# It rides any run that takes a reading -- a plan, -Changed, or the check stage -- and not a
# build-only run (nothing to compare) or a -UpdateGolden run (an authoring pass that rewrites goldens
# and reads none of them back). `-NoGpuWatch` is the explicit way out, and it says so out loud: a
# missing line and a green one read the same from the outside.
$gpuObservationName = 'gpu-plans'
# The recorded red set, read here rather than on the host: the worktree's copy is the one a lane
# edits when it records or clears a red, and this side is where the exit code is decided.
$gpuBaselineRel = 'internal/tools/gpu-plans.exceptions.txt'
$gpuBaselinePath = Join-Path $Path $gpuBaselineRel
$gpuReadingWanted = $Plan.Count -gt 0 -or $Changed -or $checkNames.Count -gt 0
$gpuWanted = (-not $NoGpuWatch) -and (-not $UpdateGolden) -and $gpuReadingWanted
if ($NoGpuWatch -and $gpuReadingWanted) {
    Write-Host "[camel-remote] -NoGpuWatch: the gpu plans were not run, so this run prints no gpu reading (test/plans/gpu is observed and never gates a known red; see the header)"
}

# `-UpdateAstHashes` may not take a row out of the AST record while the file that row records is
# still here, and a bulk re-record may not take one out at all. This is that rule as a function, so
# it can be read and exercised without a host: it takes the record's lines before and after and
# answers what would be dropped, in three classes. The write-back below prints the answer and writes
# nothing when it refuses.
#
# The three conditions of the failure -- the record gets shorter, the rows it loses still name a
# `.cml` file this tree has, and no `-AstDrop` named them -- are `StillHere` and `Refused`. The bulk
# condition is the one that does not depend on where a file is: on 2026-11-17 `eaa3ecb8c` recorded
# with the bulk flag on a tree that was missing the seven `test/cases/compile/var/loop_*.cml` a
# parallel lane had just added, and those rows went with the files (4400 lines to 4393). Those files
# were in neither that tree nor its base branch, so no `Test-Path` could have seen the loss; a bulk
# rewrite could have known it was rewriting the whole table. `27de8a3df` put the rows back by hand,
# and `agents/test.md` now carries the rule this function enforces.
function Get-AstDropVerdict {
    param(
        [AllowEmptyCollection()][string[]]$Before = @(),
        [AllowEmptyCollection()][string[]]$After = @(),
        [AllowEmptyCollection()][string[]]$Drop = @(),
        [string]$Root = '.',
        [bool]$Bulk = $true
    )
    # A line is `<16 hex> <ok|no> <path>`, and the question is about the paths: a hash that moved is
    # one line lost and one line gained, never a row that went, so a set of whole lines would call
    # every re-read row a deletion.
    $pathOf = { param($line) $fields = $line -split ' ', 3; if ($fields.Count -lt 3) { '' } else { $fields[2] } }
    $rows = [ordered]@{}
    foreach ($line in $Before) {
        $path = & $pathOf $line
        if ($path -and -not $rows.Contains($path)) { $rows[$path] = $line }
    }
    $kept = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($line in $After) {
        $path = & $pathOf $line
        if ($path) { [void]$kept.Add($path) }
    }
    # A probe is recorded under a name of the record's own (`probes/probe_xxx.cml`) and no file
    # answers to it: a line that goes is a probe probes.py no longer makes, not a file the
    # repository lost.
    $dropped = @($rows.Keys | Where-Object { -not $kept.Contains($_) -and -not $_.StartsWith('probes/') })
    $unnamed = @($dropped | Where-Object { $Drop -notcontains $_ })
    $stillHere = @($unnamed | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf })
    $deleted = @($unnamed | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf) })
    [pscustomobject]@{
        Rows      = $rows
        OldCount  = @($Before).Count
        NewCount  = @($After).Count
        Dropped   = $dropped
        Named     = @($dropped | Where-Object { $Drop -contains $_ })
        StillHere = $stillHere
        Deleted   = $deleted
        Unused    = @(@($Drop) | Where-Object { $_ -and $dropped -notcontains $_ })
        Bulk      = $Bulk
        Refused   = ($stillHere.Count -gt 0 -or ($Bulk -and $deleted.Count -gt 0))
    }
}

# A reading taken on a loaded host is not comparable to one taken on a quiet one: this machine also
# runs every other lane's build, and several lanes at -Jobs 8 is more work than there are cores. So
# every run states the host's load before and after, and says so when it is high enough that a
# number measured now should not be compared with a quiet-host number. It is a warning and never a
# different exit code: judging the number belongs to whoever reads it.
function Get-HostLoad {
    try {
        $raw = & $sshExe $sshHost 'cat /proc/loadavg' 2>$null
        if ("$raw" -match '^\s*([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)') {
            return @([double]$Matches[1], [double]$Matches[2], [double]$Matches[3])
        }
    } catch { }
    return $null
}
$loadStart = Get-HostLoad
if ($null -ne $loadStart) {
    Write-Host ("[load] {0} {1} {2} (1m/5m/15m) before run" -f $loadStart[0], $loadStart[1], $loadStart[2])
}


# The install the plan will actually use: `--tier debug` makes the plan runner read out/debug;
# every other run reads out/latest. The -NoBuild guard checks that directory (H149).
$verifyTag = if ($Tier -eq 'debug') { 'debug' } else { 'latest' }
# `--config` is passed only where the tag decides the configuration. out/debug holds the Debug build
# by construction (scripts/collect-out.js writes that tag from `npm run debug`), so naming it lets
# the guard refuse a Release install sitting in out/debug too. out/latest holds Release *and*
# RelWithDebInfo, and `-Tier debug -NoBuild` (which verifies out/debug without building) passes
# `-Config Release` by default, so naming a configuration there would refuse a correct install.
$guardConfig = if ($verifyTag -eq 'debug') { '--config Debug' } else { '' }
$guardArgs = if ($AllowStale) { "--allow-stale $guardConfig" } else { $guardConfig }

$Path = (Resolve-Path $Path).Path
if (-not (Test-Path (Join-Path $Path 'package.json'))) {
    Write-Host "[camel-remote] not a Camel worktree: $Path"
    exit 2
}
$name = Split-Path -Leaf $Path
$branch = (git -C $Path rev-parse --abbrev-ref HEAD).Trim()
if ($branch -eq 'HEAD') {
    Write-Host "[camel-remote] detached HEAD in $name; check out a branch first"
    exit 2
}
# `-AstOnly` selects which lines a recording rewrites; with no recording there is no stage that could
# read it, and a request nobody carries out must not look like one that ran.
if ($AstOnly.Count -gt 0 -and -not $UpdateAstHashes) {
    Write-Host "[camel-remote] -AstOnly names what -UpdateAstHashes rewrites; give -UpdateAstHashes, or drop -AstOnly"
    exit 2
}
# The same rule for the other side of the same stage: `-AstDrop` is a permission the write-back
# reads, and with no write-back there is nothing that could honour it.
if ($AstDrop.Count -gt 0 -and -not $UpdateAstHashes) {
    Write-Host "[camel-remote] -AstDrop names a row -UpdateAstHashes may lose; give -UpdateAstHashes, or drop -AstDrop"
    exit 2
}

# One checkout per worktree name, kept for its incremental build directory. `-Clean` throws that
# away when a task is finished; the stale ones are removed automatically at the start of every run
# (a checkout whose local worktree is gone is only disk).
if ($Clean) {
    $cleanScript = @"
: "`${HOME:=`$(getent passwd "`$(id -un)" | cut -d: -f6)}"
d=`$HOME/$($cfg.hostWorkRoot)/$name
# Never gate the removal on `[ -d ]`: a half-removed checkout (a leftover tmp/ or test/results/)
# makes the test false while the directory still exists, and the next clone then refuses a
# non-empty destination. Report what was there and remove it either way.
if [ -e "`$d" ]; then n=`$(du -sm "`$d" 2>/dev/null | cut -f1); rm -rf "`$d"; if [ -e "`$d" ]; then echo "still there after rm -rf: `$d"; else echo "removed `$n MB"; fi; else echo 'already gone'; fi
"@
    # A here-string carries its line endings verbatim, and a carriage return before a newline
    # makes the remote shell read `d` with a trailing CR: `[ -e "$d" ]` is false for a directory
    # that is really there, and the run reports 'already gone' without removing anything. The run
    # script goes through the same normalization when it is written out.
    $cleanScript = $cleanScript -replace "`r`n", "`n"
    $freed = & $sshExe -o ConnectTimeout=20 $sshHost $cleanScript 2>&1
    Write-Host "[camel-remote] host checkout $name`: $freed"
    exit 0
}

# --- published state --------------------------------------------------------
# Same read-modify-write rules as camel-build.ps1 / camel-test.ps1; the two files have a mutex
# each, and the two helpers already use these exact names.
$buildStatePath = Join-Path $cfg.stateDir 'build-state.json'
$testStatePath = Join-Path $cfg.stateDir 'test-state.json'
$buildStateMutex = New-Object System.Threading.Mutex($false, 'Global\CamelBuildState')
$testStateMutex = New-Object System.Threading.Mutex($false, 'Global\CamelTestState')

# --- run registry -----------------------------------------------------------
# The build/test state files hold ONE holder, so with several host runs going at once the panel
# showed whichever one wrote last and had no way to show the others, the slot wait, or the fact
# that two runs were compiling while the rest queued. This file is the missing list: one compact
# JSON line per run snapshot, keyed by runId, and the panel keeps the last line per run. Nothing
# here is read back by this script, and every write is best-effort: a locked or unwritable file
# must never fail a build or a plan.
$runRegistryPath = Join-Path $cfg.stateDir 'run-registry.jsonl'
$runRegistryMutex = New-Object System.Threading.Mutex($false, 'Global\CamelRunRegistry')
$runRegistryEncoding = New-Object System.Text.UTF8Encoding($false)

# ANSI colours: scripts/common.js always emits them, and over ssh (no console) they arrive verbatim.
# camel-build.ps1 matches plain console text, so strip the escapes before applying its patterns.
$ansiPattern = [char]27 + '\[[0-9;]*[A-Za-z]'

function Read-State([string]$statePath) {
    if (-not (Test-Path $statePath)) {
        return [pscustomobject]@{ updatedAt = $null; holder = $null; waiters = @(); recent = @() }
    }
    try {
        $state = Get-Content -Raw -Path $statePath | ConvertFrom-Json
    } catch {
        $state = $null
    }
    # A truncated or zero-filled file (a write interrupted by a crash) must not become an
    # object without the fields the callers set; rebuild it instead.
    $fresh = [pscustomobject]@{ updatedAt = $null; holder = $null; waiters = @(); recent = @() }
    if ($null -eq $state -or -not ($state.PSObject.Properties['waiters']) -or
        -not ($state.PSObject.Properties['recent']) -or -not ($state.PSObject.Properties['holder'])) {
        return $fresh
    }
    return $state
}

# Every state change is a read-modify-write under the file's mutex, so a local build and a host run
# cannot lose an update. The abandoned-mutex exception means the wait succeeded -- the previous
# holder died owning the mutex (a crash) and this process now owns it -- so it must not be treated
# as an error that skips the ReleaseMutex below.
function Edit-State([string]$statePath, [System.Threading.Mutex]$stateMutex, [scriptblock]$change) {
    try {
        $null = $stateMutex.WaitOne([TimeSpan]::FromSeconds(30))
    } catch [System.Threading.AbandonedMutexException] {
    }
    try {
        $state = Read-State $statePath
        $change.Invoke($state)
        $state.updatedAt = (Get-Date).ToString('o')
        $json = $state | ConvertTo-Json -Depth 6
        $tmp = "$statePath.$PID.tmp"
        Set-Content -Path $tmp -Value $json -Encoding utf8
        # The panel polls these files, and a reader that has the destination open makes Windows
        # refuse the replace ("cannot create a file when that file already exists"). With
        # $ErrorActionPreference = 'Stop' that non-terminating Move-Item error becomes an exception,
        # which must not kill a build or a plan: the published state is advisory, and the next
        # progress line tries again. Retry briefly, then drop this update.
        $published = $false
        for ($attempt = 0; $attempt -lt 25; $attempt++) {
            try {
                Move-Item -Force -Path $tmp -Destination $statePath
                $published = $true
                break
            } catch {
                Start-Sleep -Milliseconds 40
            }
        }
        if (-not $published) {
            Remove-Item -Path $tmp -Force -ErrorAction SilentlyContinue
            Write-Host "[camel-remote] reader kept $statePath busy; skipped one state update"
        }
    } finally {
        $stateMutex.ReleaseMutex()
    }
}

function Set-Holder([string]$statePath, [System.Threading.Mutex]$stateMutex, $holder) {
    Edit-State $statePath $stateMutex {
        param($state)
        $state.holder = $holder
    }
}

# Clear this process's holder (only if it is still ours) and optionally prepend a recent entry, in
# one read-modify-write so a reader never sees the holder gone with the history not yet written.
# The pid+remote guard matters because the state files hold one holder for the whole machine and
# host: another worktree's run (or a local build) may have replaced ours while we were running, and
# its holder is not ours to wipe.
function Complete-Run([string]$statePath, [System.Threading.Mutex]$stateMutex, $entry) {
    Edit-State $statePath $stateMutex {
        param($state)
        if ($null -ne $state.holder -and $state.holder.pid -eq $PID -and
            $state.holder.remote -eq $remoteName) {
            $state.holder = $null
        }
        if ($null -ne $entry) {
            $recent = @($state.recent | Where-Object { $_ } | Select-Object -First 29)
            $state.recent = @($entry) + $recent
        }
    }
}

# Progress lines update the holder without touching anything else. They only fire for our own
# holder, so a local build that took the slot meanwhile keeps its own numbers.
function Set-BuildPhase([string]$phase, [string]$line) {
    Edit-State $buildStatePath $buildStateMutex {
        param($state)
        if ($null -ne $state.holder -and $state.holder.pid -eq $PID -and
            $state.holder.remote -eq $remoteName) {
            $state.holder.phase = $phase
            $state.holder.lastLine = $line
            # A host run waits for its slot after the transfer, so the run's own start is not the
            # queue start and definitely not the build start. Keep the holder's clocks honest for
            # readers that only have this file: queuedAt = first slot-wait line, startedAt = first
            # build output.
            if ($null -ne $script:queueStartedAt) {
                $state.holder.queuedAt = $script:queueStartedAt.ToString('o')
            }
            if ($null -ne $script:buildStartedAt) {
                $state.holder.startedAt = $script:buildStartedAt.ToString('o')
            }
        }
    }
}

# Append one snapshot for this run. Best-effort by design: the panel can survive a missing
# registry (it falls back to the state files), but a build must not die because a monitor file
# was busy. The named mutex serialises appends between concurrent runs; the file is compacted
# only when it grows past half a megabyte.
function Publish-Run {
    try {
        if ($null -eq $run) { return }
        $run.updatedAt = (Get-Date).ToString('o')
        $line = ($run | ConvertTo-Json -Compress -Depth 5)
        $held = $false
        try {
            try {
                $held = $runRegistryMutex.WaitOne([TimeSpan]::FromSeconds(2))
            } catch [System.Threading.AbandonedMutexException] {
                $held = $true
            }
            if (-not $held) { return }
            $info = Get-Item -LiteralPath $runRegistryPath -ErrorAction SilentlyContinue
            if ($null -ne $info -and $info.Length -gt 524288) {
                $keep = @(Get-Content -LiteralPath $runRegistryPath -ErrorAction SilentlyContinue |
                    Select-Object -Last 300)
                $tmp = "$runRegistryPath.$PID.tmp"
                [System.IO.File]::WriteAllLines($tmp, [string[]]$keep, $runRegistryEncoding)
                Move-Item -Force -LiteralPath $tmp -Destination $runRegistryPath
            }
            [System.IO.File]::AppendAllText($runRegistryPath, $line + "`n", $runRegistryEncoding)
        } finally {
            if ($held) { $runRegistryMutex.ReleaseMutex() }
        }
    } catch {
        # Advisory state only: never let the monitor break the work it monitors.
    }
}

# Same two patterns camel-build.ps1 applies: "N/M" is the compiler progress, and the build script's
# [STEP] / [DONE] / [FAIL] lines are the coarser phase.
function Update-BuildProgress([string]$line) {
    $clean = ($line -replace $ansiPattern, '').Trim()
    if ($clean -match '^\[(\d+)/(\d+)\]') {
        $done = [int]$Matches[1]
        $total = [int]$Matches[2]
        Edit-State $buildStatePath $buildStateMutex {
            param($state)
            if ($null -ne $state.holder -and $state.holder.pid -eq $PID -and
                $state.holder.remote -eq $remoteName) {
                $state.holder.done = $done
                $state.holder.total = $total
                $state.holder.phase = 'compiling'
                $state.holder.lastLine = $clean
            }
        }
    } elseif ($clean -match '^\[(STEP|DONE|FAIL)\]') {
        $phase = $Matches[1].ToLower()
        Edit-State $buildStatePath $buildStateMutex {
            param($state)
            if ($null -ne $state.holder -and $state.holder.pid -eq $PID -and
                $state.holder.remote -eq $remoteName) {
                $state.holder.phase = $phase
                $state.holder.lastLine = $clean
            }
        }
    }
}

# camel-test.ps1 keeps the per-test status lines and drops the rest; the plan runner's output comes
# over the same ssh stream, so the same filter applies here.
function Update-TestProgress([string]$line) {
    $clean = ($line -replace $ansiPattern, '').Trim()
    if (-not $clean) { return }
    if ($clean -match '^(FAIL|pass |fail )|^\[|^PASS|^ok ') {
        Edit-State $testStatePath $testStateMutex {
            param($state)
            if ($null -ne $state.holder -and $state.holder.pid -eq $PID -and
                $state.holder.remote -eq $remoteName) {
                $state.holder.phase = 'running'
                $state.holder.lastLine = $clean
            }
        }
    }
}

# --- what has to travel -----------------------------------------------------
$startedAt = Get-Date
# The run's own start is neither the queue start nor the build start. queuedAt below stays the
# submission time only until the host prints its first wait line; buildStartedAt is the first build
# output line the host prints; buildFinishedAt is its BUILD_EXIT line. The panel draws the grey wait
# segment from the first to the second and the build segment from the second to the third.
$queuedAt = $startedAt
$queueStartedAt = $null
$buildStartedAt = $null
$buildFinishedAt = $null
$planStartedAt = $null
$buildExit = $null
$code = 2
# Phase marks for the summary line: transfer ends at the sync marker, the plan starts at its own.
$syncedAt = $null
# The checker stage's own clock and results. The start is the stage's marker; the results are read
# back from the per-checker footers, so the summary at the end names what actually ran instead of
# what was asked for.
$checksStartedAt = $null
$checkResults = @()
# The gpu observation's own reading, kept apart from the check results: it is not one of the checks
# and it does not pass through `$checkers`, but its `red=[...]` is what the ratchet below compares
# with the recorded red set. Null means the observation did not run (a -NoGpuWatch run, a build-only
# run) or never printed a reading.
$gpuReading = $null
# Why each red is red, by case name: the `-evidence` line that follows the reading. Empty when the
# host printed none (an older host script), which reads as "no reason recorded for any case".
$gpuEvidence = @{}
# The AST snapshot's own exit (`-UpdateAstHashes`) and the file it sent back. The record is written to
# this tree only after the host says it made one, so a failed stage leaves the tree as it was.
$astExit = $null
$astCapture = $null
$astPayloadText = $null
# The file `-FetchFile` asks for: same shape as the AST record above.
$fetchCapture = $null
$fetchPayloadText = $null
$lastRunPublish = [datetime]::MinValue

# One line per run for the panel's run registry. `slot` stays null (the host does not report which
# of the two locks it took); slots = 2 is the host layout this script itself creates below.
$run = @{
    runId           = "$PID-$($startedAt.ToString('yyyyMMddHHmmss'))"
    pid             = $PID
    worktree        = $name
    path            = $Path
    remote          = $remoteName
    startedAt       = $startedAt.ToString('o')
    updatedAt       = $startedAt.ToString('o')
    phase           = 'starting'
    slots           = 2
    slot            = $null
    queuedAt        = $null
    queueSeconds    = $null
    buildStartedAt  = $null
    buildFinishedAt = $null
    planStartedAt   = $null
    finishedAt      = $null
    exit            = $null
    buildExit       = $null
    done            = 0
    total           = 0
    jobs            = $Jobs
    tier            = $Tier
    plan            = @($Plan)
    target          = if ($Plan.Count -gt 0) { $Plan[0] } else { '' }
    lastLine        = "[camel-remote] pushing $branch to $sshHost"
}
Publish-Run

# -AllowStale has to be loud even when the guard then finds nothing: the caller asked for a run that
# may not describe the tree (H149). Without -NoBuild there is no guard to override.
if ($AllowStale -and $NoBuild) {
    Write-Host "[camel-remote] -AllowStale: a -NoBuild run will proceed even when out/$verifyTag was built from different sources"
} elseif ($AllowStale) {
    Write-Host "[camel-remote] -AllowStale has no effect without -NoBuild (a build always stamps the install it produces)"
}

try {
    # Published before the push: the push is already work the panel should show.
    Set-Holder $buildStatePath $buildStateMutex ([pscustomobject]@{
        worktree  = $name
        config    = $Config
        path      = $Path
        pid       = $PID
        queuedAt  = $queuedAt.ToString('o')
        startedAt = $startedAt.ToString('o')
        done      = 0
        total     = 0
        phase     = 'starting'
        lastLine  = "[camel-remote] pushing $branch to $sshHost"
        remote    = $remoteName
    })

    $scratch = Join-Path $env:TEMP "camel-remote-$name"
    if (Test-Path $scratch) { Remove-Item $scratch -Recurse -Force }
    New-Item -ItemType Directory -Path $scratch | Out-Null
    $stamp = Get-Date -Format 'HHmmss'
    $patchFile = Join-Path $scratch 'changes.patch'
    $untrackedTar = Join-Path $scratch 'untracked.tar'

    $sw = [Diagnostics.Stopwatch]::StartNew()

    # The change, listed on this side where the base branch is readable. It travels with the patch
    # (`--changed-files`) and it is the same list the runner would build for itself: `git diff
    # --name-only` against the base plus the untracked files. Two things depend on it -- `-Changed`
    # and the milestone stage's selection -- and both read a list that says "cannot read" rather
    # than a list that says nothing, so the widening to the whole layer has no branch left to take.
    # It is computed before the push: a base nobody can read must cost a push and a build nothing.
    $untracked = @(git -C $Path ls-files --others --exclude-standard)
    # A file `-FetchFile` brought back last run is named here even when it is ignored, because the two
    # halves of the two-run check need it to travel: the fetch writes it under a path `.gitignore`
    # covers (so a record can never be committed by accident), and `--exclude-standard` then leaves
    # it out of the tar -- the next run would compare against nothing. Naming it adds it to the tar
    # and nothing else: it is never staged, and a run without `-FetchFile` does not mention it.
    if ($FetchFile -ne '') {
        $fetchLocal = Join-Path $Path $FetchFile
        if (Test-Path -LiteralPath $fetchLocal) {
            $untracked = @($untracked | Where-Object { $_ -ne $FetchFile }) + @($FetchFile)
        }
    }
    $changedListFile = Join-Path $scratch 'changed.txt'
    if ($Changed -and -not $UpdateGolden) {
        $changedList = @()
        $listProblem = $null
        try {
            # Three different refusals used to read as one: a ref that does not resolve, a ref with
            # no merge-base (a shallow clone, another root's history -- `git diff` answers across
            # that and reports every file), and a diff that failed. Each names itself here.
            git -C $Path rev-parse --verify --quiet "$ChangedBase^{commit}" 2>$null | Out-Null
            if ($LASTEXITCODE -ne 0) {
                $listProblem = "cannot read $ChangedBase`: it does not resolve to a commit here"
            }
            if (-not $listProblem) {
                $common = @(git -C $Path merge-base $ChangedBase HEAD 2>$null)
                $commonOk = ($LASTEXITCODE -eq 0) -and (@($common | Where-Object { "$_".Trim() }).Count -gt 0)
                if (-not $commonOk) {
                    $listProblem = "cannot read $ChangedBase`: it shares no merge-base with HEAD (unrelated history, or a shallow clone), so the change against it is undefined"
                }
            }
            if (-not $listProblem) {
                $changedList = @(git -C $Path diff --name-only $ChangedBase 2>$null)
                if ($LASTEXITCODE -ne 0) { $listProblem = "cannot read $ChangedBase`: git diff --name-only against it failed" }
            }
        } catch {
            $listProblem = "cannot read $ChangedBase`: $($_.Exception.Message)"
        }
        if ($listProblem) {
            # What the fallback would have run, counted the way the run reads it: the report has to
            # carry the size of the thing that is not run, or "it failed" is itself unreadable.
            $layerPlans = @(git -C $Path ls-files --cached --others --exclude-standard -- test/plans/smoke test/plans/feat 2>$null |
                Where-Object { "$_" -like '*.plan.toml' })
            Write-Host "[camel-remote] ERROR -Changed: $listProblem"
            Write-Host "[camel-remote] ERROR -Changed: the plans this change touches are unknown, so none is selected and no plan runs. The fallback this replaces would have run the whole fast layer ($($layerPlans.Count) plan file(s) under test/plans/smoke and test/plans/feat), which reads exactly like a correct selection in every reading a gate takes."
            Write-Host "[camel-remote] ERROR -Changed: name the layer deliberately with -Plan all if that is the question."
            exit 2
        }
        $lines = @($changedList) + @($untracked) |
            ForEach-Object { "$_".Trim().Replace('\', '/') } |
            Where-Object { $_ }
        [System.IO.File]::WriteAllText($changedListFile, (($lines -join "`n") + "`n"),
            (New-Object System.Text.UTF8Encoding($false)))
        Write-Host ("[camel-remote] -Changed: {0} changed file(s) against {1}, listed here for the host" -f $lines.Count, $ChangedBase)
    }
    # --force, and the ref is dropped in the finally block: the staging branch is scratch. Without
    # the force, a worktree re-created at another commit (a rebuilt lane, an A/B run) is a
    # non-fast-forward to the same ref name, and the run stopped before it reached the host;
    # without the delete, one ref per worktree name outlived every run and went stale the moment
    # that name was reused -- which is where the staging repository's pile of lane-* refs came from.
    $pushOutput = @(git -C $Path push --quiet --force "$sshHost`:$bare" $branch 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $pushExit = $LASTEXITCODE
        Write-Host "[camel-remote] could not push $branch to $sshHost`:$bare (git exit $pushExit)."
        foreach ($line in $pushOutput) { if ("$line".Trim()) { Write-Host "[camel-remote]   $("$line".Trim())" } }
        # The usual cause is not the network: the staging bare repository keeps one branch per
        # worktree name, and an earlier run may have left refs/heads/<branch> on a commit that is
        # not an ancestor of the one being pushed (a reset branch, a rebased scratch tree, an A/B
        # run). Name that ref and the two commits instead of sending the reader to the host.
        # This script is not in a repository, so the comment beside a change is the only record of
        # it: the forced push above and the delete in the finally are what removed this cause. The
        # block below now reports a push that fails for some other reason.
        $remoteRef = @(git -C $Path ls-remote "$sshHost`:$bare" "refs/heads/$branch" 2>$null)
        $localSha = (git -C $Path rev-parse $branch).Trim()
        if ($remoteRef.Count -gt 0 -and $remoteRef[0]) {
            $remoteSha = ($remoteRef[0] -split '\s+')[0]
            git -C $Path cat-file -e "$remoteSha^{commit}" 2>$null
            $remoteKnown = ($LASTEXITCODE -eq 0)
            Write-Host "[camel-remote] staging ref refs/heads/$branch there is $($remoteSha.Substring(0, [Math]::Min(9, $remoteSha.Length))); local $branch is $($localSha.Substring(0, 9))."
            if ($remoteKnown) {
                git -C $Path merge-base --is-ancestor $remoteSha $localSha 2>$null
                if ($LASTEXITCODE -ne 0) {
                    Write-Host "[camel-remote] that is a non-fast-forward: the staging ref is not an ancestor of the local branch."
                } else {
                    Write-Host "[camel-remote] the staging ref is an ancestor of the local branch, so this is not a fast-forward problem."
                }
            } else {
                Write-Host "[camel-remote] that commit is not in this checkout, so the staging ref cannot be compared (another clone pushed it)."
            }
            Write-Host "[camel-remote] refs/heads/$branch there is the helper's scratch branch, not develop. Delete it and re-run:"
            Write-Host "[camel-remote]   ssh $sshHost `"git -C $bare branch -D $branch`""
        } else {
            Write-Host "[camel-remote] the staging repository has no refs/heads/$branch; check that $sshHost is reachable and $bare is writable."
        }
        exit 2
    }
    $stagingPushed = $true

    # --- what this run would measure, said before it travels --------------------
    # The patch below carries the WORKING TREE (`git diff HEAD`), not the commit, so every reading a
    # gate prints is about "the commit plus whatever else is uncommitted in this checkout". In a lane
    # worktree that is exactly what the author wants. In the main checkout it is an accident waiting
    # to happen: two of them in one night (a half-finished rename, and a probe dropped into
    # src/runtime/draft.cpp) turned a build into somebody else's compile error, and a green run would
    # have been just as wrong in the other direction. So the main checkout -- the checkout whose
    # --git-dir is the common directory, i.e. not a linked worktree -- refuses a dirty tree outside
    # $AllowDirty, and every run prints which files it is about to carry, so that "measured" can be
    # read off the output instead of assumed.
    $dirtyTracked = @()
    try {
        $dirtyTracked = @(git -C $Path status --porcelain --untracked-files=no 2>$null |
            ForEach-Object { $_.Substring(3).Trim() } |
            Where-Object { $_ -ne '' })
    } catch {
        $dirtyTracked = @()
    }
    $gitDir = ''
    $commonDir = ''
    try {
        $gitDir = (git -C $Path rev-parse --absolute-git-dir 2>$null).Trim()
        $commonDir = (git -C $Path rev-parse --path-format=absolute --git-common-dir 2>$null).Trim()
    } catch {
        $gitDir = ''
        $commonDir = ''
    }
    $isMainCheckout = ($gitDir -ne '' -and $gitDir -eq $commonDir)
    if ($dirtyTracked.Count -gt 0) {
        # A comma also separates, because `pwsh -File` hands every argument over as one string: the
        # array syntax the parameter asks for is read literally there, so a whitelist written that
        # way matches nothing and refuses every run. Measured, not predicted: it did exactly that.
        $allowed = @($AllowDirty | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
        $foreign = @($dirtyTracked | Where-Object {
            $rel = $_
            -not ($allowed | Where-Object { $rel -eq $_ -or $rel -like $_ })
        })
        $carried = @($dirtyTracked | Where-Object { $foreign -notcontains $_ })
        if ($carried.Count -gt 0) {
            Write-Host ("[camel-remote] carrying {0} allowed dirty file(s): {1}" -f $carried.Count, ($carried -join ', '))
        }
        if ($foreign.Count -gt 0) {
            Write-Host ("[camel-remote] {0} dirty file(s) that -AllowDirty does not name:" -f $foreign.Count)
            foreach ($rel in $foreign) { Write-Host ("[camel-remote]   {0}" -f $rel) }
            if ($isMainCheckout) {
                Set-BuildPhase 'starting' ("[camel-remote] refusing: {0} dirty file(s) in the main checkout" -f $foreign.Count)
                Write-Host '[camel-remote] refusing to send: the main checkout is dirty outside -AllowDirty, so a'
                Write-Host '[camel-remote] reading taken now would be about this commit plus somebody else''s unfinished'
                Write-Host '[camel-remote] work, and "this commit was measured" would not be true. Commit it, revert it,'
                Write-Host '[camel-remote] or name it in -AllowDirty if it really is part of this change.'
                exit 5
            }
            Write-Host '[camel-remote] continuing: this is a linked worktree, where a dirty tree is the point.'
        }
    }

    # `git diff --output=` writes the patch as raw bytes; `>` in PowerShell would write UTF-16.
    git -C $Path diff HEAD --binary --output=$patchFile 2>&1 | Out-Null

    if ($untracked.Count -gt 0) {
        # Paths with spaces and non-ASCII names travel through a list file, one per line.
        $listFile = Join-Path $scratch 'untracked.txt'
        Set-Content -Path $listFile -Value $untracked -Encoding utf8
        Push-Location $Path
        try {
            & tar -cf $untrackedTar -T $listFile 2>&1 | Out-Null
        } finally {
            Pop-Location
        }
    }
    # A clean tree has an empty patch, and Get-Content -Raw gives $null for a zero-byte file.
    $patchText = Get-Content $patchFile -Raw
    if ($null -eq $patchText) { $patchText = '' }
    $changedCount = ([regex]::Matches($patchText, '(?m)^diff --git ')).Count
    Write-Host ("[camel-remote] {0}: pushed {1}, {2} changed file(s), {3} untracked ({4:N1} s)" -f `
        $name, $branch, $changedCount, $untracked.Count, $sw.Elapsed.TotalSeconds)
    if ($changedCount -gt 0) {
        # The names, not just the count: a count cannot tell "my one file" from "mine and a probe".
        $changedNames = @([regex]::Matches($patchText, '(?m)^diff --git a/(\S+) ') |
            ForEach-Object { $_.Groups[1].Value })
        Write-Host ("[camel-remote] measuring: {0}" -f ($changedNames -join ', '))
    } else {
        Write-Host '[camel-remote] measuring: the commit alone (no tracked change)'
    }
    Set-BuildPhase 'starting' ("[camel-remote] {0}: {1} changed file(s), {2} untracked; sending to {3}" -f `
        $name, $changedCount, $untracked.Count, $sshHost)

    # --- carry it over ------------------------------------------------------
    # The patch, the untracked files and the run script travel in one scp: each ssh/scp costs a
    # connection, and there is no ControlMaster on the Windows client to amortise it.
    $remoteScratch = "/tmp/camel-remote-$name-$stamp"

    # What the checkout should be at afterwards, and which host checkouts are still wanted. `-s` on
    # the patch and `-f` on the tar are the fast path's guard: a clean tree needs neither.
    $headSha = (git -C $Path rev-parse HEAD).Trim()
    # Which host checkouts are still wanted. The old code here was
    #   try { git ... --porcelain 2>$null | ... } catch { $liveNames = @($name) }
    # whose comment said "keep every checkout rather than delete the wrong one" and whose body did
    # the opposite: a native command's non-zero exit does not throw, so the catch never ran, the
    # list came back empty, and the line after it appended this run's own name -- the sweep was then
    # told that exactly one checkout exists and removed all the others. On 2026-11-17 that killed
    # three measurements (25/500, 200/500, 300/500) on the host where they were running. The
    # reading now reports whether it answered, and an unanswered reading removes nothing.
    $live = Get-CamelLiveWorktrees -Path $Path
    $liveNames = @($live.Names)
    $liveTrustedFlag = if ($live.Trusted) { '1' } else { '0' }
    if ($live.Trusted) {
        # Printed every run: when a checkout does go, this is the line that says what the run knew.
        Write-Host ("[camel-remote] live worktrees: {0}" -f ($liveNames -join ' '))
    } else {
        Write-Host ("[camel-remote] not cleaning up host checkouts this run: {0}; 'could not read which worktrees exist' is not 'there are no other worktrees'" -f $live.Why)
    }

    Set-BuildPhase 'starting' "[camel-remote] syncing $name on $sshHost"

    # --- run it there -------------------------------------------------------
    $planArgs = ''
    foreach ($p in $Plan) {
        $p = $p.Trim()
        if ($p) { $planArgs += " '$p'" }
    }
    $tierArg = if ($Tier) { " --tier '$Tier'" } else { '' }
    # `-StackTrace` is the one entry point of a diagnostic build: it passes `--stack-trace` to build.js, which
# adds `-DCAMEL_STACK_TRACE=ON` and nothing else. Leave it off for any reading that decides green or red:
# a diagnostic build is a different build.
$stackTraceArg = if ($StackTrace) { ' -- --stack-trace' } else { '' }
$buildCmd = if ($Config -eq 'Debug') { "npm run debug$stackTraceArg" } else { "npm run build$stackTraceArg" }

    $runPlan = if ($UpdateGolden) {
        "cd `$R/ && node test/tools/run-tests.mjs --update-golden $planArgs --jobs $Jobs$tierArg"
    } elseif ($Changed) {
        # The list above travels with the patch: the host has the lane's commits but not the base
        # branch, so a selection made there answered "cannot read origin/develop" and the run fell
        # back to the whole fast layer. `CAMEL_TEST_BASE` stays for the sentences the runner prints
        # -- the list is the one taken against that base -- but the list is what it reads.
        "cd `$R/ && CAMEL_TEST_BASE='$ChangedBase' node scripts/test.js --changed --changed-files '$remoteScratch/changed.txt' --jobs $Jobs$tierArg"
    } elseif ($planArgs) {
        # No CAMEL_TEST_EXTRA_LIB_DIRS here any more: the install a 26888783c-or-later build writes
        # carries the toolchain's libstdc++.so.6 in its `libs/` and finds it through its own
        # `$ORIGIN` rpath (2026-11-03 15:35 ruling), so the plan step needs no extra directory --
        # and `-Changed` and `-UpdateGolden`, which never had one, now read exactly like this branch.
        "cd `$R/ && node scripts/test.js $planArgs --jobs $Jobs$tierArg"
    } else {
        ':'
    }

    # Markers delimit the phases for the state writer; they are consumed locally and not printed.
    # The plan marker is only emitted when a plan will actually run.
    $planMarker = if ($planArgs -or $Changed) { "echo '__CAMEL_REMOTE_PLAN_START__'" } else { ':' }

    # The editor grammar's snapshot tests skip without their dev dependencies, and this helper runs
    # `npm run build`, which is not the repo's install step. A fresh checkout would therefore skip
    # that plan silently, so install them once per checkout, only when a plan will actually run.
    # Idempotent and never fatal: without npm the plan skips, as it did before. `npm ci`, not
    # `npm install`: the scope snapshots record what the pinned versions produce, so the install
    # must be the lockfile's tree and never a fresh resolution of the ranges.
    $vscodeDeps = if ($planArgs -or $Changed) {
        "if [ -d `"`$R/editors/vscode`" ] && [ ! -d `"`$R/editors/vscode/node_modules/vscode-textmate`" ]; then" + "`n" +
        "    echo '[camel-remote] installing editors/vscode dev dependencies (npm ci) so the grammar snapshot runs'" + "`n" +
        "    ( cd `"`$R/editors/vscode`" && timeout 600 npm ci --no-audit --no-fund ) || echo '[camel-remote] editors/vscode install failed: the grammar snapshot will skip'" + "`n" +
        "fi"
    } else { ':' }

    # The AST snapshot record, when `-UpdateAstHashes` asks for one. A single-quoted here-string: the
    # shell's `$R` and `$?` must reach the host verbatim, and only the build tag is substituted
    # (`out/latest` for a Release run, `out/debug` for a Debug one -- the install this run built, not
    # whichever one happens to be newest). The recorded file leaves base64-encoded between two
    # markers, so it costs no second connection and cannot be mangled by the line filter above it.
    #
    # The `[machine]` line comes first and names the build time of the binary that does the recording:
    # a stale binary is the one failure mode this stage cannot show in its own output, and with
    # `-NoBuild` the record is taken by whatever the host built last. `-NoBuild` therefore appends
    # `(NOT REBUILT -- may be stale)` to that line rather than leaving it looking like a fresh build.
    $astBuiltNote = if ($NoBuild) { ' (NOT REBUILT -- may be stale)' } else { '' }
    $astOnlyArgs = ($AstOnly | ForEach-Object { " --only '$_'" }) -join ''
    # The rows the recording may lose, by path as the record spells it: the host's guard refuses a
    # drop it was not told about, and this is what tells it (`check.py`, `--drop`).
    $astDropArgs = ($AstDrop | ForEach-Object { " --drop '$_'" }) -join ''
# The file `-FetchFile` asks for, sent back the same way the AST record is: base64 between two
# markers, after the plan. It runs after `RUN_EXIT` so a record the plan wrote is complete, and it
# names the path when the host has nothing there -- a fetch that silently sends nothing would look
# exactly like a run whose comparison passed.
$fetchRel = ($FetchFile -replace '\\', '/')
$fetchStage = if ($FetchFile -ne '') {
@'
if [ -f "$R/FETCHREL" ]; then
echo '__CAMEL_REMOTE_FETCH_BEGIN__'
base64 "$R/FETCHREL"
echo '__CAMEL_REMOTE_FETCH_END__'
else
echo 'FETCH_MISSING=FETCHREL'
echo '[camel-remote] -FetchFile: the host has no FETCHREL (whatever writes it must run in this same run)'
fi
'@.Replace('FETCHREL', $fetchRel)
} else { ':' }
    $astStage = if ($UpdateAstHashes) {
        @'
echo '__CAMEL_REMOTE_AST_START__'
echo "[machine] out/TAG/bin/camel-parsecheck built $(date -r out/TAG/bin/camel-parsecheck '+%Y-%m-%d %H:%M')NOTE"
echo '[camel-remote] recording the AST snapshot (frontend.ast_snapshot) on the host'
CAMEL_HOME="$R/out/TAG" python3 test/tools/parsecheck/check.py snapshot --updateONLYDROP 2>&1
astc=$?
echo "AST_UPDATE_EXIT=$astc"
if [ $astc -ne 0 ]; then
    echo '__CAMEL_REMOTE_AST_FAILED__'
    exit $astc
fi
echo '__CAMEL_REMOTE_AST_FILE_BEGIN__'
base64 "$R/test/plans/feat/parse/golden/ast_hashes.txt"
echo '__CAMEL_REMOTE_AST_FILE_END__'
'@.Replace('TAG', $verifyTag).Replace('NOTE', $astBuiltNote).Replace('ONLY', $astOnlyArgs).Replace('DROP', $astDropArgs)
    } else { ':' }

    # One block per checker: a header naming it and the command it runs, the command's own stdout and
    # stderr verbatim, and a footer naming its exit code. The footer is also what this script reads
    # back, so the console shows one line per checker and the run can summarise them at the end;
    # `checkfail` carries the first failure out of the stage as the run's exit code.
    $checkBlocks = ''
    foreach ($checkName in $checkNames) {
        $entry = $checkers[$checkName]
        # No per-checker LD_LIBRARY_PATH prefix any more (2026-11-03 15:35 ruling): the install a
        # current build writes carries the toolchain's libstdc++.so.6 in `libs/` with a `$ORIGIN`
        # rpath, so the checker that starts the tree's binary starts it in the run's own
        # environment. `compiler` still decides the `--camel` argument below.
        # The install is named rather than left to the default (this tree's out/latest): a Debug run
        # builds out/debug, and this check must measure the tag the run built and -NoBuild guarded.
        # The path is relative to the checkout, which is the working directory by now, so the header
        # the host prints names a command a reader can run as it is.
        $camelArg = if ($entry.compiler) { " --camel `"out/$verifyTag/bin/camel`"" } else { '' }
        # Every entry but the generated-file pair is a node script; those two name their own
        # interpreter because their generators are python (see the table above).
        $interp = if ($entry.interpreter) { $entry.interpreter } else { 'node' }
        $shownCommand = "$interp $($entry.script) $($entry.args -join ' ')$camelArg"
        if ($entry.reportOnly) {
            # The same invocation, with one difference that is the whole point of the entry: its
            # output is captured so the reading can be reduced out of the checker's own summary, and
            # its exit code is kept out of `checkfail`, so a report that finds something does not
            # turn the gate red. The reading is `unknown` when the checker printed no summary (a
            # missing script, an early refusal) -- never a zero this run did not measure.
            $readingFile = "$remoteScratch/check-$checkName.out"
            $checkBlocks += @"

echo '[camel-remote] --- check ${checkName}: $shownCommand ---'
out='$readingFile'
$shownCommand > "`$out" 2>&1
c=`$?
cat "`$out"
n=`$(sed -nE '$($entry.findings)' "`$out" | head -n 1)
rm -f "`$out"
echo "check=${checkName} findings=`${n:-unknown} mode=report"
echo "[camel-remote] --- ${checkName}: exit `$c ---"
"@
            continue
        }
        $checkBlocks += @"

echo '[camel-remote] --- check ${checkName}: $shownCommand ---'
$shownCommand
c=`$?
if [ `$c -ne 0 ] && [ `$checkfail -eq 0 ]; then checkfail=`$c; fi
echo "[camel-remote] --- ${checkName}: exit `$c ---"
"@
    }

    # --- the gpu observation -------------------------------------------------
    # The shape of a `reportOnly` entry, for a thing that is not a checker: the plan run's output is
    # captured, one machine-readable line is printed, and the exit code never reaches `checkfail`.
    # Three differences are the reading's own, and each is why the line can be trusted:
    #   * it is a plan run (`scripts/test.js`, on the caller's -Jobs), and it takes the host's plan
    #     lock for itself -- it is a second plan run and not a reader of the first one, and a number
    #     taken while five other lanes are running plans is a different number;
    #   * `--tier all` rather than the caller's `-Tier`: `test/plans/gpu` is a directory, so it is read
    #     at a tier, and the default `fast` layer would leave out the slow cases this observation
    #     exists for (the header states this trap);
    #   * the reading is the runner's own [SUMMARY] reduced here, never a count of our own: `fail=`
    #     is its `fail:`, `red=` is the names its own `[FAIL]` lines print, and a run that printed no
    #     summary reads `unknown` because a zero nobody measured is the reading of a green run.
    # `awk` prints the failing tests' own block (the `[FAIL]` line and the indented detail under it)
    # and nothing about the tests that passed, because the line already counts those.
    $gpuBlock = ''
    if ($gpuWanted) {
        $gpuReadingFile = "$remoteScratch/$gpuObservationName.out"
        $shownGpu = "node scripts/test.js test/plans/gpu --jobs $Jobs --tier all"
        $gpuBlock = @"

# --- the gpu layer: observed, never gating (see the header) -----------------
echo '[camel-remote] --- $($gpuObservationName): $shownGpu ---'
exec 5>/tmp/camel-host-test.lock
if ! flock -w 1800 5; then
    # No slot is not a red: this observation decides nothing, so it says it did not read and goes.
    echo '[camel-remote] $($gpuObservationName): no host test slot within 1800 s, so the gpu layer was not read'
    echo "check=$($gpuObservationName) total=unknown pass=unknown fail=unknown skip=unknown red=[unknown] red_more=0 seconds=0 state=noslot mode=report"
else
    # The sweep's own clock, because the checks stage's seconds now carry it: a lane comparing that
    # number with an earlier run's has to be able to see what the extra time was.
    gpu_started_ns=`$(date +%s%N)
    $shownGpu > '$gpuReadingFile' 2>&1
    gpu_code=`$?
    gpu_ms=`$(( (`$(date +%s%N) - gpu_started_ns) / 1000000 ))
    gpu_seconds=`$(( gpu_ms / 1000 ))
    gpu_esc=`$(printf '\033')
    gpu_clean=`$(sed "s/`${gpu_esc}\[[0-9;]*m//g" '$gpuReadingFile')
    gpu_summary=`$(printf '%s\n' "`$gpu_clean" | grep -E '^\[SUMMARY\]' | head -n 1)
    if [ -n "`$gpu_summary" ]; then
        gpu_total=`$(printf '%s\n' "`$gpu_summary" | sed -nE 's/^\[SUMMARY\] total: ([0-9]+).*/\1/p')
        gpu_pass=`$(printf '%s\n' "`$gpu_summary" | sed -nE 's/.*, pass: ([0-9]+).*/\1/p')
        gpu_fail=`$(printf '%s\n' "`$gpu_summary" | sed -nE 's/.*, fail: ([0-9]+).*/\1/p')
        # The runner prints `skip:` and `known failures:` only when they are non-zero; an absent one
        # is a measured zero and not an unknown, so those two default to 0.
        gpu_skip=`$(printf '%s\n' "`$gpu_summary" | sed -nE 's/.*, skip: ([0-9]+).*/\1/p')
        gpu_skip=`${gpu_skip:-0}
        gpu_reds=`$(printf '%s\n' "`$gpu_clean" | sed -nE 's/^\[FAIL\] ([^ ]+).*/\1/p')
        gpu_nfail=`$(printf '%s\n' "`$gpu_reds" | grep -c . || true)
        # EVERY name, not the first twenty. A truncated list was read as the whole list by the
        # ratchet below, and that made `no longer red ... can be trimmed` print for recorded names
        # that were red in the very same run (2026-11-18: seven of fifteen). The count is in `fail=`;
        # `red_more` now says how many names the line withheld, and it is 0. `printf "%s", $0`, not
        # `printf "%s"`: a missing argument prints nothing, which read as "no red names" while
        # `fail=` said sixteen. The `$0` is escaped because this text is a PowerShell here-string and
        # an unescaped `$0` arrives at the host as nothing at all.
        gpu_red=`$(printf '%s\n' "`$gpu_reds" | awk 'NR>1{printf ","} {printf "%s", `$0}')
        gpu_more=0
        # WHY each red is red, one `<name>|<reason>` per fail, so a recorded entry can be checked
        # against the reason it was recorded for and not only against its name. The reason is the
        # diagnostic's own code (`code=0x040001B0`) when the failure printed one -- that is the
        # language's own name for the diagnostic and it does not drift with the wording -- and the
        # assertion's own words otherwise. `-` says the reading carried neither.
        gpu_evidence=''
        for gpu_dir in `$(printf '%s\n' "`$gpu_clean" | sed -nE 's/^[[:space:]]*\[INFO\] full output: (\S+).*/\1/p'); do
            gpu_case=`$(basename "`$gpu_dir")
            # The three files are all written for a failing case and a diagnostic may land in any of
            # them: a refusal written to stdout, a Python assertion to stderr. `resident_graph_transfers`
            # asserts `p.stderr` on a program whose refusal went to stdout, so its output.txt carries
            # the assertion and not the code -- reading only output.txt made it look like a red for a
            # different reason than the one recorded, which was this reading's own error.
            gpu_why=`$(grep -ohE 'code=0x[0-9A-Fa-f]{8}' "`$gpu_dir/output.txt" "`$gpu_dir/stdout.txt" "`$gpu_dir/stderr.txt" 2>/dev/null | head -n 1 | sed 's/^code=//')
            if [ -z "`$gpu_why" ]; then
                gpu_why=`$(grep -ohE 'AssertionError: .*' "`$gpu_dir/output.txt" "`$gpu_dir/stdout.txt" "`$gpu_dir/stderr.txt" 2>/dev/null | head -n 1 |
                    sed -e 's/^AssertionError: *//' -e 's/[^A-Za-z0-9]\{1,\}/-/g' -e 's/^-*//' | cut -c1-40)
            fi
            # A plan-level exact-output assertion prints neither a code nor an assertion, so the two
            # sides of its comparison are read from the test's own record: `meta.json` carries
            # `expect_stdout_exact differs: ... expected "...", got "..."` JSON-escaped onto one line.
            # Only the FIRST line of each side is kept -- that is where an outlined count moves, and a
            # window from the middle of a long output names nothing -- and the pair is the reason, so a
            # red that starts differing elsewhere is news. The arrow is `->` and not a character: this
            # string travels back to PowerShell (see the local side's `-evidence` parse) and is
            # compared byte for byte, so a byte an encoding may rewrite would turn every one of these
            # into "recorded for another reason" for good, which is silent rather than loud.
            if [ -z "`$gpu_why" ]; then
                gpu_diff=`$(grep -o 'differs: .*' "`$gpu_dir/meta.json" 2>/dev/null | head -n 1)
                gpu_exp=`$(printf '%s' "`$gpu_diff" | sed -nE 's/.*expected \\"([^\\]*).*/\1/p' |
                    sed -e 's/[^A-Za-z0-9]\{1,\}/-/g' -e 's/^-*//' | cut -c1-24)
                gpu_act=`$(printf '%s' "`$gpu_diff" | sed -nE 's/.*got \\"([^\\]*).*/\1/p' |
                    sed -e 's/[^A-Za-z0-9]\{1,\}/-/g' -e 's/^-*//' | cut -c1-24)
                if [ -n "`$gpu_exp" ] && [ -n "`$gpu_act" ]; then
                    gpu_why="exact-output-diff:`$gpu_exp->`$gpu_act"
                fi
            fi
            [ -z "`$gpu_why" ] && gpu_why=-
            gpu_evidence="`$gpu_evidence`$gpu_case|`$gpu_why;"
        done
        gpu_state=read
        if [ "`${gpu_nfail:-0}" -gt 0 ]; then
            printf '%s\n' "`$gpu_clean" | awk '/^\[FAIL\]/ {p=1} p && /^\[/ && !/^\[FAIL\]/ {p=0} p'
        fi
    else
        # No summary line: the runner refused before it measured anything (a target it could not
        # resolve, an install it could not start). Its tail is the reason, so the reason travels
        # instead of a zero this run did not measure.
        printf '%s\n' "`$gpu_clean" | tail -n 20
        gpu_total=unknown; gpu_pass=unknown; gpu_fail=unknown; gpu_skip=unknown
        gpu_red=unknown; gpu_more=0; gpu_state=nosummary
    fi
    rm -f '$gpuReadingFile'
    # `state` is what the local side ratchets on: `read` is a reading, the other two are "the layer
    # was not read" (a busy host, a runner that said nothing), which is never turned into a red.
    echo "check=$($gpuObservationName) total=`${gpu_total:-unknown} pass=`${gpu_pass:-unknown} fail=`${gpu_fail:-unknown} skip=`${gpu_skip:-unknown} red=[`${gpu_red}] red_more=`${gpu_more:-0} seconds=`${gpu_seconds:-0} state=`${gpu_state:-read} mode=report"
    # The reading says which cases are red; this line says why each one is. It is a second line on
    # purpose: the shape of the `check=` line above is what every lane and the tools' own tests read,
    # and a recorded entry can be checked against its reason without changing that shape.
    if [ -n "`$gpu_evidence" ]; then
        echo "check=$($gpuObservationName)-evidence [`${gpu_evidence%;}]"
    fi
    echo "[camel-remote] --- $($gpuObservationName): exit `$gpu_code ---"
fi
exec 5>&-
"@
    }

    # Housekeeping first: a checkout whose local worktree is gone is only disk, and this run needs
    # room to build. Which ones are gone was decided on the workstation, in camel-remote-live.ps1;
    # this half adds the one check only the host can make -- that nothing is using the checkout --
    # and does nothing at all when the workstation could not read its worktree list.
    #
    # Three conditions, and a checkout is removed only when all three hold:
    #   (a) the workstation's `git worktree list` answered and does not name it (trusted=1);
    #   (b) no directory with that name exists beside its worktrees (also trusted=1:
    #       camel-remote-live.ps1 folds the filesystem reading into that one flag);
    #   (c) nothing on the host is using it: its own per-checkout lock is free, no process's cwd or
    #       executable lies inside it, and nothing in it changed in the last ten minutes.
    # Anything else keeps the checkout and prints why. A failed reading is never read as "gone": the
    # cost of keeping a dead checkout is disk, and the cost of the opposite is somebody's whole run.
    #
    # A single-quoted here-string: the shell's `$` must reach the host verbatim, and this fragment is
    # also what internal/tools/camel-remote-cleanup.test.ps1 extracts and runs.
    $cleanupGuard = @'
# --- host cleanup guard: extracted and run by internal/tools/camel-remote-cleanup.test.ps1 ---
cleanup_host_checkouts() {
    local root="$1"
    local live="$2"
    local trusted="$3"
    local skip="$4"
    reclaimed=0
    if [ "$trusted" != 1 ]; then
        echo '[camel-remote] not removing host checkouts this run: the workstation could not say which worktrees exist, and a failed reading is not an empty one'
        return 0
    fi
    local d dd n key lockf busy p t
    for d in "$root"/*/; do
        [ -d "$d" ] || continue
        dd="${d%/}"
        n=$(basename "$dd")
        case "$n" in "$skip"|bin) continue;; esac
        # The runner's scratch root lives next to the checkouts and is regenerated every run:
        # removing it here is not a cleanup, and it would invalidate the cached size measurement.
        case "$n" in *-test-tmp) continue;; esac
        # A bare repository is not a checkout: HEAD and objects sit at its top level, and removing it
        # would take the staging remote every lane pushes through.
        if [ -f "$dd/HEAD" ] && [ -d "$dd/objects" ]; then continue; fi
        # (a)+(b) the workstation's readings, which are only worth acting on when they answered.
        case " $live " in *" $n "*) continue;; esac
        # (c1) The run's own lock. Every checkout this script creates is created while its holder has
        # the lock open, so a free lock means no run is inside this one -- which also covers a
        # worktree created a second after this run read its list, the case a snapshot cannot see.
        key=$(printf '%s' "$dd" | cksum | cut -d' ' -f1)
        lockf="/tmp/camel-tree-$key.lock"
        if ! flock -n "$lockf" true 2>/dev/null; then
            echo "[camel-remote] keeping host checkout $n: a run holds its lock (or its lock file cannot be opened)"
            continue
        fi
        # (c2) A build, a test, or a person inside it. The executable matters as much as the
        # directory: a plan runs out/latest/bin/camel from a checkout whose cwd can be elsewhere.
        busy=''
        for p in /proc/[0-9]*; do
            t=$(readlink "$p/cwd" 2>/dev/null) || t=''
            case "$t" in "$dd"|"$dd"/*) busy="$p cwd=$t"; break;; esac
            t=$(readlink "$p/exe" 2>/dev/null) || t=''
            case "$t" in "$dd"|"$dd"/*) busy="$p exe=$t"; break;; esac
        done
        if [ -n "$busy" ]; then
            echo "[camel-remote] keeping host checkout $n: a process is using it ($busy)"
            continue
        fi
        # (c3) Something in it changed within ten minutes: a run that has just handed over, or one
        # that died half-written. Ten minutes costs nothing; a wrong delete costs a whole run.
        if [ -n "$(find "$dd" -maxdepth 3 -mmin -10 -print -quit 2>/dev/null)" ]; then
            echo "[camel-remote] keeping host checkout $n: a file in it changed in the last 10 minutes"
            continue
        fi
        # The lock is taken again for the removal itself: a run that grabbed it in the last
        # millisecond must not have its checkout disappear underneath it.
        if flock -n "$lockf" rm -rf "$dd" 2>/dev/null; then
            reclaimed=1
            echo "[camel-remote] removing host checkout $n, its worktree is gone"
        else
            echo "[camel-remote] keeping host checkout $n: a run took its lock while it was being removed"
        fi
    done
}
# --- end host cleanup guard ---
'@
    $remoteScript = @"
set -u
R=`$HOME/camel-work/$name
# One lock per checkout, taken before anything touches the tree and released only at the end: two
# runs against the same checkout must not overlap at all, because the second one's build replaces
# out/latest underneath the first one's tests (2026-11-03: that, not the load, is what made a
# concurrent run exit 1 with a plan half-run). The key is a checksum of the path, so a name with
# spaces or a longer path cannot produce an invalid lock file name.
tree_key=`$(printf '%s' "`$R" | cksum | cut -d' ' -f1)
exec 6>"/tmp/camel-tree-`$tree_key.lock"
tree_wait_started_ns=`$(date +%s%N)
if ! flock -w 1800 6; then
    echo '[camel-remote] no slot on this checkout within 1800 s' >&2
    exit 4
fi
tree_wait_ms=`$(( (`$(date +%s%N) - tree_wait_started_ns) / 1000000 ))
if [ `$tree_wait_ms -gt 5000 ]; then
    echo "[camel-remote] waited `$(awk "BEGIN{printf \"%.1f\", `$tree_wait_ms/1000}") s for this checkout"
fi
C=`$HOME/$($cfg.hostClangEnv)
V=`$HOME/$($cfg.hostBuildEnv)
B=`$HOME/$($cfg.hostBenchEnv)
$cleanupGuard
cleanup_host_checkouts "`$HOME/$($cfg.hostWorkRoot)" '$($liveNames -join ' ')' '$liveTrustedFlag' '$bareLeaf'
find /tmp -maxdepth 1 -name 'camel-remote-*' -mtime +0 -exec rm -rf {} + 2>/dev/null || true
freekb=`$(df -P -k "`$HOME" 2>/dev/null | awk 'NR==2{print `$4}')
# The footprint of the checkouts is informational, but measuring it walks every file under
# ~/camel-work (43 GB here, ~50 s on a loaded host, once per run). Keep the number in a small
# cache and recompute it only when it is an hour old or a cleanup above actually removed something.
sizecache=/tmp/camel-checkout-size
sizeage=`$(( `$(date +%s) - `$(stat -c %Y "`$sizecache" 2>/dev/null || echo 0) ))
if [ "`$reclaimed" = 1 ] || [ ! -s "`$sizecache" ] || [ "`$sizeage" -gt 3600 ]; then
    du -sm "`$HOME/$($cfg.hostWorkRoot)" 2>/dev/null | cut -f1 > "`$sizecache"
fi
size_mb=`$(cat "`$sizecache" 2>/dev/null)
# `[ "" -lt 5242880 ]` is an error, not a small number: bash takes the else branch and the disk
# guard silently does not run, which is a broken reading that looks like "there is room". Say so
# instead, and never print the empty answer as `0 GB free`.
case "`$freekb" in
    ''|*[!0-9]*)
        echo "[camel-remote] host: free space unknown (df answered '`$freekb'), `${size_mb:-?} MB of checkouts; the low-disk guard did not run"
        ;;
    *)
        echo "[camel-remote] host: `$((freekb/1048576)) GB free, `${size_mb:-?} MB of checkouts"
        if [ "`$freekb" -lt 5242880 ]; then echo '[camel-remote] under 5 GB free on the host; run -Clean on the worktrees that are done'; exit 3; fi
        ;;
esac
mkdir -p "`$R"
# `git clone` refuses a non-empty destination, and a half-removed checkout (a leftover tmp/ or
# test/results/) leaves exactly that: on 2026-10-24 a lane's run died with exit 2 because the
# destination existed without a `.git`. Clear the path first; the branch's content comes from the
# bare repository and the patch below, so nothing of value is in the way.
if [ ! -d "`$R/.git" ]; then rm -rf "`$R"; mkdir -p "`$R"; git clone -q "$bare" "`$R" || { echo '[camel-remote] clone failed'; exit 2; }; fi
cd "`$R" || exit 2
cur=`$(git rev-parse HEAD 2>/dev/null || true)
# The fast path also requires the HOST checkout to be clean. It used to compare only the commit,
# so a checkout left dirty by an earlier run that carried uncommitted changes was reused as-is:
# a later run from a clean local tree then skipped the checkout, skipped the (empty) patch, and
# measured the previous run's sources. On 2026-10-23 that produced one false green and one false
# red in the same afternoon -- "SIGFPE is not mine, I reverted and rebuilt" rebuilt the host's
# patch. Tracked modifications are what matter, so `git status --porcelain` (which also sees the
# untracked leftovers of a previous tar) decides.
dirty=`$(git status --porcelain 2>/dev/null | head -c 1)
if [ "`$cur" = '$headSha' ] && [ -z "`$dirty" ] && [ ! -s '$remoteScratch/changes.patch' ] && [ ! -f '$remoteScratch/untracked.tar' ]; then
    echo '[camel-remote] checkout already at this commit, clean, with nothing to apply'
else
    git fetch -q origin || exit 2
    git checkout -q -f -B '$branch' 'origin/$branch' || exit 2
    git reset -q --hard || true
    git clean -qfd -e build -e out || true
    if [ -s '$remoteScratch/changes.patch' ]; then git apply --whitespace=nowarn '$remoteScratch/changes.patch' || { echo '[camel-remote] patch did not apply'; exit 2; }; fi
    if [ -f '$remoteScratch/untracked.tar' ]; then tar -xf '$remoteScratch/untracked.tar' -C "`$R" || exit 2; fi
fi
export PATH="`$HOME/$($cfg.hostLocalBin):`$B:`$V:`$C/bin:`$HOME/$($cfg.hostWorkRoot)/bin:`$PATH"
# Until 26888783c the tree's `camel` binary needed a libstdc++ newer than the host's system one
# (GLIBCXX_3.4.31/32/35/36 not found), and only two things ever provided it: the plan step, through
# CAMEL_TEST_EXTRA_LIB_DIRS (the runner's own mechanism), and the -Check entries marked `compiler`.
# Everything else in this script ran without it -- and so did every command a person typed over ssh
# by hand, which is the case that cost a lane half a night on 2026-11-03: the self-test's sweep
# fixture starts the binary itself, so all thirteen of its copies came back FAIL with `pass: 0`,
# a shape that reads exactly like "the width followed its neighbours". It was not the width; the
# binary could not start. The root fix is in the install now -- a build from 26888783c on copies
# the toolchain's libstdc++.so.6 into its `libs/` and names it with a `$ORIGIN` rpath -- and the
# 15:35 ruling of 2026-11-03 deleted those two per-stage exports. This one global export stays for
# the older installs still sitting in host checkouts; `$C/lib` stays after it, where it always was.
export CC=clang CXX=clang++
export LD_LIBRARY_PATH='$($cfg.hostLibStdCxxDir)'":`$C/lib:`${LD_LIBRARY_PATH:-}"
# (the generated shell runs under `set -u`: an rpath written as a bare dollar-variable here would
#  abort the run, so this line names the thing and not the syntax).
echo "[camel-remote] libstdc++: a current build carries libs/libstdc++.so.6 and finds it by its own rpath; this export is for older builds only"
export CMAKE_BUILD_PARALLEL_LEVEL=24 CAMEL_HOME="`$R/out/latest"
export CAMEL_TEST_TMP="`$HOME/$($cfg.hostWorkRoot)/$name-test-tmp"
# A failed test's own output is echoed to the console only under this variable
# (test/tools/run-tests.mjs: printFailedOutput). Without it a red gate prints nothing but
# "expected exit 0, got 1", so the gate hands back a red with no reason in it -- on 2026-10-23
# that hid "imports_all: median 92.8 ms exceeds ceiling 90 ms" behind a bare exit 1 for a day.
export CAMEL_TEST_SHOW_OUTPUT=1
# A gate run on a branch cut from a base that was already built compiles the same 600 translation
# units as the run before it. ccache turns those into cache hits across checkouts; the build script
# already knows how to use it (scripts/common.js: getCmakeCcacheFlags, CAMEL_USE_CCACHE=1), and CI
# enables it. The cache is per user and bounded; CCACHE_BASEDIR is the checkout, so the same source
# in another worktree hashes the same, and CCACHE_NOHASHDIR keeps the directory name out.
if command -v ccache >/dev/null 2>&1; then
    export CAMEL_USE_CCACHE=1
    export CCACHE_DIR="`$HOME/.cache/ccache-camel"
    export CCACHE_BASEDIR="`$R"
    export CCACHE_NOHASHDIR=true
    export CCACHE_MAXSIZE=20G
    echo "[camel-remote] ccache: `$(ccache --version | head -1)"
fi
# The split the plan runner prints: unless a build runs below, this run built nothing.
export CAMEL_GATE_BUILD_SECONDS=0
cd "`$R" || exit 2
echo '__CAMEL_REMOTE_SYNCED__'
if [ '$NoBuild' = 'True' ]; then
    if [ -f scripts/build-info.js ]; then
        # Captured rather than piped so the marker the guard prints past -AllowStale can be handed
        # to the plan run: a reading that replayed a refused install has to carry that fact on its
        # own lines (test/tools/provenance.mjs: staleAllowedMark), and the only copy of it is this
        # output.
        guard_out=`$(node scripts/build-info.js check --tag '$verifyTag' $guardArgs 2>&1)
        guard=`$?
        printf '%s\n' "`$guard_out"
        stale=`$(printf '%s\n' "`$guard_out" | grep -o '\[STALE-ALLOWED[^]]*\]' | head -1)
        if [ -n "`$stale" ]; then
            export CAMEL_GATE_STALE_ALLOWED="`$stale"
        fi
        if [ `$guard -ne 0 ]; then
            echo '__CAMEL_REMOTE_STALE_BINARY__'
            exit `$guard
        fi
    else
        echo "[camel-remote] WARNING: scripts/build-info.js is missing in this checkout; -NoBuild cannot verify out/$verifyTag (the tree predates the H149 guard)"
    fi
fi
if [ '$NoBuild' != 'True' ]; then
    # Two host builds at a time, twelve compile jobs each: one build alone was fine but eight
    # agents sharing one slot queued for tens of minutes, while three or more at 24 jobs each
    # thrashed the box (load 98 on 32 cores). Two slots of twelve keep the cores busy without
    # oversubscribing; the plan run afterwards is unaffected and still uses -Jobs.
    exec 9>/tmp/camel-host-build-1.lock
    exec 8>/tmp/camel-host-build-2.lock
    slot=
    if flock -n 9; then slot=1; elif flock -n 8; then slot=2; fi
    if [ -z `$slot ]; then
        echo '[camel-remote] waiting for a host build slot'
        waited=0
        while [ -z `$slot ]; do
            if flock -n 9; then slot=1; elif flock -n 8; then slot=2; else
                sleep 3; waited=`$((waited + 3))
                if [ `$((waited % 30)) -eq 0 ]; then echo "[camel-remote] still waiting for a host build slot (`${waited}s)"; fi
            fi
        done
    fi
    # A build that wedges must not hold a slot forever; the cap is generous.
    build_started_ns=`$(date +%s%N)
    timeout 2400 $buildCmd
    code=`$?
    build_ms=`$(( (`$(date +%s%N) - build_started_ns) / 1000000 ))
    # The plan runner folds this into its [SPLIT] line; it is the build's own clock, not this
    # script's (which also carries the slot wait and the npm wrapper).
    export CAMEL_GATE_BUILD_SECONDS=`$(awk "BEGIN{printf \"%.1f\", `$build_ms/1000}")
    echo "BUILD_SECONDS=`$CAMEL_GATE_BUILD_SECONDS"
    echo "BUILD_EXIT=`$code"
    # The slot is released as soon as the build ends: holding it through the plan run
    # made one agent's tests block every other agent's build for minutes.
    exec 9>&- 2>/dev/null; exec 8>&- 2>/dev/null
    if [ `$code -ne 0 ]; then echo '__CAMEL_REMOTE_BUILD_FAILED__'; exit `$code; fi
fi
# The AST snapshot record (only with -UpdateAstHashes): before the plan, because the plan reads the
# record this stage writes.
$astStage
$vscodeDeps
# One plan run at a time, the way the build has two slots and for the same reason: the readings a
# gate produces are only comparable when the machine is not carrying six other lanes' tests. The
# lock covers the plan and nothing else -- the transfer and the build are outside it, and the build
# has its own slots -- so a full queue costs waiting time, never a wrong number. Waiting is not a
# failure: only the plan's own exit code reaches the caller.
exec 7>/tmp/camel-host-test.lock
test_wait_started_ns=`$(date +%s%N)
if ! flock -w 1800 7; then
    echo '[camel-remote] no host test slot within 1800 s' >&2
    exit 4
fi
test_wait_ms=`$(( (`$(date +%s%N) - test_wait_started_ns) / 1000000 ))
if [ `$test_wait_ms -gt 2000 ]; then
    echo "[camel-remote] waited `$(awk "BEGIN{printf \"%.1f\", `$test_wait_ms/1000}") s for the host test slot"
fi
$planMarker
# The plan's stderr is folded into its own stdout here, on the host, before it leaves this script.
# The runner writes a red run's reason to stderr ("this run selected no test at all", the ENOENT
# stack of a mistyped plan path), so without this the caller gets `RUN_EXIT=1` and not one word of
# why -- a red that says nothing, indistinguishable from a run that measured nothing. Do not remove
# the `2>&1`: it is what puts the reason on the same stream as RUN_EXIT, in the order it happened.
$runPlan 2>&1
code=`$?
echo "RUN_EXIT=`$code"
$fetchStage
# Released here, before the checkers: they read a finished run and need no exclusion.
exec 7>&- 2>/dev/null
# --- the checkers -----------------------------------------------------------
# One stage, one report: each checker prints its own output verbatim between a header naming it and
# a footer naming its exit code. The stage carries the first failure out, so a red checker makes the
# run red even when the plan above was green (and a red plan keeps its own exit code).
if [ -n '$checkList' ]; then
    checkfail=0
    echo '__CAMEL_REMOTE_CHECKS_START__'
$checkBlocks
    if [ `$checkfail -ne 0 ]; then
        echo '__CAMEL_REMOTE_CHECKS_FAILED__'
        if [ `$code -eq 0 ]; then code=`$checkfail; fi
    fi
fi
# --- the gpu observation ----------------------------------------------------
# After the checkers and outside their `if`: it is not one of the checks, so it does not need -Check
# to be asked for, and it is not counted by `N run`. Its own block explains what it reads; the one
# thing it does not do is touch `code` -- `checkfail` is untouched by it, so no reading it prints can
# decide this run.
$gpuBlock
# The scratch directory goes with the run: doing it here saves a connection, and a run that dies
# before this line leaves something the day-old prune in /tmp sweeps up.
cd /tmp && rm -rf '$remoteScratch'
exit `$code
"@

    # The script travels as a file: a string piped into `ssh bash -s` arrives with the pipe's
    # encoding, and a CR before a newline makes `exit 0` fail with "numeric argument required".
    $scriptFile = Join-Path $scratch 'run.sh'
    [System.IO.File]::WriteAllText($scriptFile, ($remoteScript -replace "`r`n", "`n"),
        (New-Object System.Text.UTF8Encoding($false)))

    # One connection for the whole transfer: scp copies the scratch directory itself, so the remote
    # directory is created by scp and no separate mkdir round trip is needed. (Piping a tar through
    # the PowerShell pipeline would risk re-encoding it; scp keeps the bytes.)
    $scpOutput = & $scpExe -q -r $scratch "${sshHost}:$remoteScratch" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[camel-remote] scp failed: $scpOutput"
        exit 2
    }

    # Each line is streamed to the console *and* folded into the holder. The marker lines switch the
    # phase (sync -> build -> plan); everything else goes to the build parser until the plan starts,
    # then to the test parser, matching which local helper would have shown that line.
    & $sshExe -o ConnectTimeout=20 $sshHost "bash '$remoteScratch/run.sh'" 2>&1 |
        ForEach-Object {
            $line = "$_"
            # NOT $clean: PowerShell is case-insensitive and the script has a [switch]$Clean.
            $cleanLine = ($line -replace $ansiPattern, '').Trim()
            # -UpdateAstHashes sends the recorded file back on this stream, base64 between two markers.
            # It is a file and not a reading, so it is not printed: the lines are collected and the
            # file is written once the run has ended (the markers bracket the payload in order, so a
            # truncated stream leaves `$astCapture` non-null and nothing is written).
            if ($line -match '__CAMEL_REMOTE_AST_FILE_BEGIN__') {
                $astCapture = New-Object System.Text.StringBuilder
                return
            }
            if ($line -match '__CAMEL_REMOTE_AST_FILE_END__') {
                # The payload is complete: capture ends here and everything after this line (the plan's
                # own output) goes to the console again. Leaving `$astCapture` open would swallow the
                # plan and decode the two together -- which is how the first run of this switch failed.
                $astPayloadText = $astCapture.ToString()
                $astCapture = $null
                return
            }
            if ($null -ne $astCapture) {
                [void]$astCapture.Append($cleanLine)
                return
            }
            # -FetchFile sends one file back the same way the AST record travels: base64 between
            # two markers, collected here and written after the run (so a truncated stream writes
            # nothing rather than half a record).
            if ($line -match '__CAMEL_REMOTE_FETCH_BEGIN__') {
                $fetchCapture = New-Object System.Text.StringBuilder
                return
            }
            if ($line -match '__CAMEL_REMOTE_FETCH_END__') {
                $fetchPayloadText = $fetchCapture.ToString()
                $fetchCapture = $null
                return
            }
            if ($null -ne $fetchCapture) {
                [void]$fetchCapture.Append($cleanLine)
                return
            }
            if ($line -match '^AST_UPDATE_EXIT=(\d+)\s*$') {
                $astExit = [int]$Matches[1]
                $run.lastLine = if ($astExit -eq 0) {
                    '[camel-remote] the AST snapshot was recorded on the host'
                } else {
                    "[camel-remote] recording the AST snapshot failed on the host (exit $astExit)"
                }
                Publish-Run
                Write-Host $line
                return
            }
            if ($line -match '__CAMEL_REMOTE_PLAN_START__') {
                if ($null -eq $planStartedAt) {
                    $planStartedAt = Get-Date
                    # A plan runs only after a successful build (or with -NoBuild, no build at all).
                    if ($null -eq $buildExit) { $buildExit = 0 }
                    if ($NoBuild) {
                        # The sync-only build holder was useful while it lasted, but no build ran:
                        # clear it without inventing a build in `recent`.
                        Complete-Run $buildStatePath $buildStateMutex $null
                    } else {
                        # The build's own clock, not this process's: queue from the first slot-wait
                        # line to the first build output, build from there to BUILD_EXIT.
                        $buildStartForPanel = if ($null -ne $buildStartedAt) { $buildStartedAt } elseif ($null -ne $syncedAt) { $syncedAt } else { $startedAt }
                        $queueForPanel = if ($null -ne $queueStartedAt) { $queueStartedAt } else { $startedAt }
                        $buildEndForPanel = if ($null -ne $buildFinishedAt) { $buildFinishedAt } else { $planStartedAt }
                        $buildSeconds = [math]::Round(($buildEndForPanel - $buildStartForPanel).TotalSeconds)
                        Complete-Run $buildStatePath $buildStateMutex ([pscustomobject]@{
                            worktree   = $name
                            queuedAt   = $queueForPanel.ToString('o')
                            startedAt  = $buildStartForPanel.ToString('o')
                            finishedAt = $buildEndForPanel.ToString('o')
                            exit       = $buildExit
                            seconds    = $buildSeconds
                            remote     = $remoteName
                        })
                    }
                    # The plan's own clock starts now; queuedAt = startedAt because nothing queued.
                    Set-Holder $testStatePath $testStateMutex ([pscustomobject]@{
                        worktree  = $name
                        path      = $Path
                        pid       = $PID
                        queuedAt  = $planStartedAt.ToString('o')
                        startedAt = $planStartedAt.ToString('o')
                        plan      = @($Plan)
                        target    = $Plan[0]
                        tier      = $Tier
                        jobs      = $Jobs
                        phase     = 'running'
                        lastLine  = "[camel-remote] running $($Plan -join ', ') on $sshHost"
                        remote    = $remoteName
                    })
                    $run.phase = 'plan'
                    $run.planStartedAt = $planStartedAt.ToString('o')
                    $run.lastLine = "[camel-remote] running $($Plan -join ', ') on $sshHost"
                    Publish-Run
                }
                return
            }
            if ($line -match '__CAMEL_REMOTE_SYNCED__') {
                $syncedAt = Get-Date
                $what = if ($NoBuild) { "preparing the plan on $sshHost" } else { "building $Config on $sshHost" }
                Set-BuildPhase 'starting' "[camel-remote] synced $name; $what"
                $run.lastLine = "[camel-remote] synced $name; $what"
                Publish-Run
                return
            }
            if ($line -match '__CAMEL_REMOTE_BUILD_FAILED__') {
                Set-BuildPhase 'fail' "[camel-remote] build failed on $sshHost"
                $run.phase = 'fail'
                $run.lastLine = "[camel-remote] build failed on $sshHost"
                Publish-Run
                return
            }
            if ($line -match '__CAMEL_REMOTE_STALE_BINARY__') {
                Set-BuildPhase 'fail' "[camel-remote] -NoBuild refused: out/$verifyTag was built from different sources, options or configuration"
                $run.phase = 'fail'
                $run.lastLine = "[camel-remote] -NoBuild refused: out/$verifyTag was built from different sources, options or configuration"
                Publish-Run
                return
            }
            if ($line -match '__CAMEL_REMOTE_CHECKS_START__') {
                $checksStartedAt = Get-Date
                $run.phase = 'checks'
                $run.lastLine = "[camel-remote] running $($checkNames.Count) checker(s) on $sshHost"
                Publish-Run
                # Only a run with no plan still holds the build phase; a plan replaced the holder with
                # the test one, and either holder shows the run on the host.
                Set-BuildPhase 'checks' $run.lastLine
                return
            }
            # The gpu observation's reading: one machine-readable line, kept apart from the checks
            # because it is not one of them, and kept at all because its `red=[...]` is what the
            # ratchet below compares with the recorded red set. The line still falls through to the
            # tail below, because it is one of the lines a lane reads.
            if ($cleanLine -match '^check=([A-Za-z0-9._-]+) total=(\S+) pass=(\S+) fail=(\S+) skip=(\S+) red=\[([^\]]*)\] red_more=(\d+) seconds=(\S+) state=(\S+) mode=report$') {
                if ($Matches[1] -eq $gpuObservationName) {
                    $gpuReading = [pscustomobject]@{
                        total   = $Matches[2]
                        pass    = $Matches[3]
                        fail    = $Matches[4]
                        skip    = $Matches[5]
                        red     = @($Matches[6] -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
                        redMore = [int]$Matches[7]
                        seconds = $Matches[8]
                        state   = $Matches[9]
                    }
                }
            }
            # The reason line that follows the reading: `<name>|<reason>` per red, `;` between them.
            # A name is checked against the reason it was recorded for, so a case that is red for a
            # different reason than the one written down is news rather than a known red.
            if ($cleanLine -match '^check=([A-Za-z0-9._-]+)-evidence \[(.*)\]$') {
                if ($Matches[1] -eq $gpuObservationName) {
                    foreach ($entry in @($Matches[2] -split ';' | Where-Object { $_.Trim() })) {
                        $parts = $entry.Split('|', 2)
                        if ($parts.Count -eq 2) {
                            $gpuEvidence[$parts[0].Trim()] = $parts[1].Trim()
                        }
                    }
                }
            }
            # The per-checker footer: the name and the exit code of one checker, kept for the run's
            # own summary. The line itself is printed, because that is what a lane reads.
            if ($cleanLine -match '^\[camel-remote\] --- ([A-Za-z0-9._-]+): exit (\d+) ---$') {
                $checkResults += [pscustomobject]@{ name = $Matches[1]; exit = [int]$Matches[2] }
                $run.lastLine = $cleanLine
                Publish-Run
                Set-BuildPhase 'checks' $cleanLine
                Write-Host $line
                return
            }
            if ($line -match '__CAMEL_REMOTE_CHECKS_FAILED__') {
                $run.phase = 'fail'
                $run.lastLine = "[camel-remote] a checker failed on $sshHost"
                Publish-Run
                Set-BuildPhase 'fail' $run.lastLine
                return
            }
            if ($line -match '^BUILD_EXIT=(\d+)\s*$') {
                $buildExit = [int]$Matches[1]
                $buildFinishedAt = Get-Date
                $run.buildFinishedAt = $buildFinishedAt.ToString('o')
                $run.buildExit = $buildExit
                $run.phase = 'built'
                if ($buildExit -eq 0) {
                    Set-BuildPhase 'done' '[camel-remote] build finished on the host'
                    $run.lastLine = '[camel-remote] build finished on the host'
                } else {
                    Set-BuildPhase 'fail' "[camel-remote] build failed on the host (exit $buildExit)"
                    $run.lastLine = "[camel-remote] build failed on the host (exit $buildExit)"
                }
                Publish-Run
                Write-Host $line
                return
            }
            # The slot wait is a state of its own. The host prints this line immediately when both
            # locks are taken, then repeats it every 30 s with the elapsed count.
            if ($cleanLine -match '^\[camel-remote\] (?:still )?waiting for a host build slot(?: \((\d+)s\))?') {
                $waited = if ($Matches[1]) { [int]$Matches[1] } else { 0 }
                if ($null -eq $queueStartedAt) { $queueStartedAt = (Get-Date).AddSeconds(-$waited) }
                $run.phase = 'queued'
                $run.queuedAt = $queueStartedAt.ToString('o')
                $run.queueSeconds = $waited
                $run.lastLine = $cleanLine
                Set-BuildPhase 'queued' $cleanLine
                Publish-Run
                Write-Host $line
                return
            }
            # The first line after the sync marker is the build command's first output. That is the
            # build start: before it, the run was transferring or queued, and neither is compile time.
            if (-not $NoBuild -and $null -ne $syncedAt -and $null -eq $buildStartedAt -and
                $null -eq $planStartedAt) {
                $buildStartedAt = Get-Date
                $run.buildStartedAt = $buildStartedAt.ToString('o')
                $run.phase = 'building'
                $run.lastLine = $cleanLine
                Publish-Run
                Set-BuildPhase 'compiling' "[camel-remote] building $Config on $sshHost"
            }
            if ($null -eq $planStartedAt) {
                Update-BuildProgress $line
                if ($null -ne $buildStartedAt) {
                    $run.lastLine = $cleanLine
                    if ($cleanLine -match '^\[(\d+)/(\d+)\]') {
                        $run.done = [int]$Matches[1]
                        $run.total = [int]$Matches[2]
                    }
                    # Progress lines can arrive several times a second; the registry only needs a
                    # heartbeat, so throttle to one snapshot every few seconds.
                    if (((Get-Date) - $lastRunPublish).TotalSeconds -ge 3) {
                        Publish-Run
                        $lastRunPublish = Get-Date
                    }
                }
            } else {
                Update-TestProgress $line
                if (((Get-Date) - $lastRunPublish).TotalSeconds -ge 3) {
                    $run.lastLine = $cleanLine
                    $run.phase = 'plan'
                    Publish-Run
                    $lastRunPublish = Get-Date
                }
            }
            Write-Host $line
        }
    $code = $LASTEXITCODE
    $finishedAt = Get-Date
    $run.finishedAt = $finishedAt.ToString('o')
    $run.exit = $code
    $run.phase = if ($code -eq 0) { 'done' } else { 'fail' }
    Publish-Run

    # The AST snapshot record (`-UpdateAstHashes`): the host sent back the file it recorded, and this is
    # where it lands in this tree. All three conditions must hold -- the stage said it recorded, the
    # stream carried the payload to its end marker, and the run did not fail -- so a failed or truncated
    # stage leaves the file exactly as it was. The text is written as UTF-8 without a BOM and with LF
    # endings, because the record is compared byte for byte by `check.py` on a Linux host.
    #
    # A fourth condition now precedes the write: the record may not lose a row while the file that row
    # records is still here (`$astStillHere` below). This is the tree the record belongs to, so it is
    # the only place the question "does that file exist" has the answer that matters -- the host
    # records from its own checkout, which can be missing files this tree has (`eaa3ecb8c`,
    # `agents/test.md`). A row that goes has to be named by -AstDrop, and a bulk re-record may not
    # lose one at all; the refusal prints the rows it would have dropped, with their recorded hash,
    # and writes nothing.
# The file `-FetchFile` asked for. Written after the run, on the same rule as the AST record:
# the host sent it base64 between two markers, a payload that does not decode is reported and
# nothing is written, and a directory the path needs is created first.
if ($null -ne $fetchPayloadText -and $FetchFile -ne '') {
    $fetchPayload = ($fetchPayloadText -replace '\s', '')
    $fetchTarget = Join-Path $Path $FetchFile
    if ($fetchPayload.Length -eq 0 -or $fetchPayload.Length % 4 -ne 0 -or
        $fetchPayload -notmatch '^[A-Za-z0-9+/]*={0,2}$') {
        Write-Host ("[camel-remote] -FetchFile: the file did not arrive intact ({0} chars); nothing was written" -f $fetchPayload.Length)
        $code = 2
    } else {
        $fetchDir = Split-Path -Parent $fetchTarget
        if ($fetchDir -and -not (Test-Path -LiteralPath $fetchDir)) {
            New-Item -ItemType Directory -Path $fetchDir -Force | Out-Null
        }
        $fetchText = [System.Text.Encoding]::UTF8.GetString(
            [System.Convert]::FromBase64String($fetchPayload)) -replace "`r`n", "`n"
        [System.IO.File]::WriteAllText($fetchTarget, $fetchText, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host ("[camel-remote] -FetchFile: {0} bytes written to {1}" -f $fetchText.Length, $fetchTarget)
    }
}

    if ($null -ne $astPayloadText -and $astExit -eq 0) {
        $astPayload = ($astPayloadText -replace '\s', '')
        $astTarget = Join-Path $Path 'test/plans/feat/parse/golden/ast_hashes.txt'
        # A payload that is not base64 means the stream carried something else (a shell message on
        # stdout, a last line cut in half). Writing a half-decoded record would be worse than writing
        # none: it is reported, the file is left alone, and the run is red -- exit 2 is the transfer's
        # failure, which is what this is.
        if ($astPayload.Length -eq 0 -or $astPayload.Length % 4 -ne 0 -or
            $astPayload -notmatch '^[A-Za-z0-9+/]*={0,2}$') {
            Write-Host ("[camel-remote] AST snapshot: the record did not arrive intact ({0} chars, length mod 4 = {1}); the file was left alone: {2}" -f `
                $astPayload.Length, ($astPayload.Length % 4),
                $astPayload.Substring(0, [Math]::Min(80, $astPayload.Length)))
            $code = 2
        } else {
            $astText = [System.Text.Encoding]::UTF8.GetString(
                [System.Convert]::FromBase64String($astPayload)) -replace "`r`n", "`n"
            $astBefore = if (Test-Path -LiteralPath $astTarget) { @(Get-Content -LiteralPath $astTarget) } else { @() }
            $astNewLines = @($astText -split "`n" | Where-Object { $_ -ne '' })
            # What this write would take out of the record, and whether it is allowed to (`Get-AstDropVerdict`
            # above carries the rule). `$astBefore.Count` is the record's line count before the write, which
            # is the number a reader compares: a re-record that makes the file shorter is the reading the
            # guard exists to stop from being silent.
            $astVerdict = Get-AstDropVerdict -Before $astBefore -After $astNewLines -Drop $AstDrop -Root $Path -Bulk ($AstOnly.Count -eq 0)
            if ($astVerdict.Refused) {
                $astWhy = if ($astVerdict.StillHere.Count -gt 0) {
                    "and {0} of them name a .cml file this tree still has" -f $astVerdict.StillHere.Count
                } else {
                    'and this is a bulk re-record (no -AstOnly), which may not lose a row'
                }
                Write-Host ("[camel-remote] AST snapshot: refusing the write-back: the record would go from {0} line(s) to {1}, {2}" -f `
                    $astVerdict.OldCount, $astVerdict.NewCount, $astWhy)
                foreach ($astPath in @($astVerdict.StillHere + $astVerdict.Deleted)) {
                    Write-Host ("[camel-remote]   - {0}" -f $astVerdict.Rows[$astPath])
                }
                Write-Host "[camel-remote] re-record on a tree that holds them (git merge origin/develop first), or name the rows that really should go: -AstDrop '<path>'"
                Write-Host ("[camel-remote] nothing was written; {0} is unchanged ({1} line(s))" -f $astTarget, $astVerdict.OldCount)
                $code = 2
            } else {
                [System.IO.File]::WriteAllText($astTarget, $astText, (New-Object System.Text.UTF8Encoding($false)))
                $astAfter = @(Get-Content -LiteralPath $astTarget)
                $astBeforeSet = [System.Collections.Generic.HashSet[string]]::new()
                foreach ($astLine in $astBefore) { [void]$astBeforeSet.Add($astLine) }
                $astAfterSet = [System.Collections.Generic.HashSet[string]]::new()
                foreach ($astLine in $astAfter) { [void]$astAfterSet.Add($astLine) }
                $astAdded = @($astAfter | Where-Object { -not $astBeforeSet.Contains($_) })
                $astRemoved = @($astBefore | Where-Object { -not $astAfterSet.Contains($_) })
                Write-Host ("[camel-remote] AST snapshot: {0} lines recorded, {1} added and {2} removed ({3})" -f `
                    $astAfter.Count, $astAdded.Count, $astRemoved.Count, $astTarget)
                foreach ($astLine in $astAdded) { Write-Host ("[camel-remote]   + {0}" -f $astLine) }
                foreach ($astLine in $astRemoved) { Write-Host ("[camel-remote]   - {0}" -f $astLine) }
                if ($astVerdict.Deleted.Count -gt 0) {
                    Write-Host ("[camel-remote] AST snapshot: dropped {0} row(s) whose .cml file no longer exists (删掉 {0} 条，对应文件已不存在): {1}" -f `
                        $astVerdict.Deleted.Count, ($astVerdict.Deleted -join ', '))
                }
                if ($astVerdict.Named.Count -gt 0) {
                    Write-Host ("[camel-remote] AST snapshot: {0} row(s) dropped as named by -AstDrop: {1}" -f `
                        $astVerdict.Named.Count, ($astVerdict.Named -join ', '))
                }
                if ($astVerdict.Unused.Count -gt 0) {
                    Write-Host ("[camel-remote] AST snapshot: -AstDrop named {0} path(s) that no row was dropped for: {1}" -f `
                        $astVerdict.Unused.Count, ($astVerdict.Unused -join ', '))
                }
            }
        }
    }

    # One line per checker: the output above is the command's own, and this list says which name
    # each exit code belongs to, next to the run's exit code.
    #
    # The gpu observation's footer travels on the same regex as a checker's, so its reading arrives
    # in `$checkResults` too. It is taken out here rather than left in: `N run` below is the static
    # layer's count (20 under -Check all), and the observation is a plan run that is not one of those
    # checks -- a twenty-first name in that count would change a number every lane compares. It is
    # listed on its own line instead, with the same "report only" tag and a word about the count, so
    # that a green check summary is not read over a gpu layer somebody forgot to look at.
    $gpuObserved = @($checkResults | Where-Object { $_.name -eq $gpuObservationName })
    $checkResults = @($checkResults | Where-Object { $_.name -ne $gpuObservationName })
    if ($checkResults.Count -gt 0) {
        # A report-only entry's exit code does not decide the run, so it must not be counted in
        # `failed` here -- and it must not vanish either, because a number nobody sees cannot be
        # watched coming down. It is counted as run, and named as report only in the list below.
        $checkFailed = @($checkResults | Where-Object { $_.exit -ne 0 -and -not $checkers[$_.name].reportOnly })
        $checksSeconds = if ($null -ne $checksStartedAt) { [math]::Round(($finishedAt - $checksStartedAt).TotalSeconds) } else { 0 }
        Write-Host ("[camel-remote] checks: {0} run, {1} failed ({2} s)" -f `
            $checkResults.Count, $checkFailed.Count, $checksSeconds)
        foreach ($result in $checkResults) {
            $only = if ($checkers[$result.name].reportOnly) { ' (report only: does not decide the run)' } else { '' }
            Write-Host ("[camel-remote]   {0} exit {1}{2}" -f $result.name, $result.exit, $only)
        }

        # `N run` above is the static layer, not the whole table, and that count has been read as
        # "every check ran" while a named-stage check had not run at all. So the count is followed by
        # what it leaves out. The names come from `$planStageNames`, the table, so a new entry marked
        # `planStage` shows up here without this line being touched; a run with none to name prints
        # nothing, because then there is nothing the count is not counting.
        if ($checkHasAll -and $planStageSkipped.Count -gt 0) {
            Write-Host ("[camel-remote] checks: {0} named-stage check(s) not run by -Check all: {1} (use -Check <name>)" -f `
                $planStageSkipped.Count, ($planStageSkipped -join ', '))
        }
        # The other reason `all` leaves an entry out: the check is a deliberate reading and costs
        # minutes, so it is asked for by name. Named here, or a run that says "N run" would be read
        # as "everything ran" while the slowest reading in the table never started.
        if ($checkHasAll -and $namedOnlySkipped.Count -gt 0) {
            Write-Host ("[camel-remote] checks: {0} named-only check(s) not run by -Check all: {1} (use -Check <name>; each is a reading somebody asks for, not a stage of the static layer)" -f `
                $namedOnlySkipped.Count, ($namedOnlySkipped -join ', '))
        }
        # The other direction, for a run that named one: it did run, and it is still not part of
        # `all`. Without this line the same "all green" sentence is read over a check `all` never runs.
        foreach ($result in @($checkResults | Where-Object { $checkers[$_.name].planStage })) {
            Write-Host ("[camel-remote] checks: {0} is a named-stage check, not part of -Check all; it ran because it was named" -f $result.name)
        }
        foreach ($result in @($checkResults | Where-Object { $checkers[$_.name].namedOnly })) {
            Write-Host ("[camel-remote] checks: {0} is a named-only check, not part of -Check all; it ran because it was named" -f $result.name)
        }
    }
    # The gpu reading, named as what it is: one line, and the two things a reader could otherwise get
    # wrong -- that it was one of the checks above (it was not) and that a red there decides this run
    # (it does not). Printed after the check list so it is read after the count it is not part of.
    foreach ($result in $gpuObserved) {
        Write-Host ("[camel-remote]   {0} exit {1} (report only: does not decide the run, and is not one of the checks counted above)" -f `
            $result.name, $result.exit)
    }

    # --- the gpu ratchet -----------------------------------------------------
    # The reading alone would be background noise: a list that is allowed to grow is a list nobody
    # reads. So the red set is compared with the one written down in `$gpuBaselineRel`, and the
    # comparison decides the only thing this observation is allowed to decide -- a case that is red
    # now and was not recorded then. The four outcomes, all of them stated in the header section "the
    # gpu layer is observed, and never gates a known red":
    #   * every red is recorded -> the run is told so and nothing changes (a known bit-exactness red
    #     never blocks a merge);
    #   * a red is not recorded, or the reading named only part of its reds (`red_more>0`, so the
    #     names it did not print cannot be compared) -> exit 5, when nothing else already made the
    #     run red;
    #   * a recorded name is no longer red -> accepted, and the run says the file can be trimmed, so
    #     a fix never waits for the bookkeeping;
    #   * no reading at all (`state=noslot`, `state=nosummary`, or no observation) -> nothing is
    #     compared and nothing is invented, and the reason is printed when there is one.
    if ($null -ne $gpuReading) {
        $gpuBaselineFound = Test-Path -LiteralPath $gpuBaselinePath
        # An entry is `name` or `name :: reason`. The reason is compared, not printed and forgotten:
        # a name recorded for one failure and red for another is news (see the comparison below).
        $gpuBaseline = @()
        $gpuReason   = @{}
        if ($gpuBaselineFound) {
            foreach ($entry in @(Get-Content -LiteralPath $gpuBaselinePath |
                    ForEach-Object { "$_".Trim() } |
                    Where-Object { $_ -and -not $_.StartsWith('#') })) {
                if ($entry -match '^(\S+)(?:\s*::\s*(.*?))?\s*$') {
                    $entryName   = $Matches[1]
                    $entryReason = if ($Matches.ContainsKey(2)) { "$($Matches[2])".Trim() } else { '' }
                    $gpuBaseline += $entryName
                    $gpuReason[$entryName] = $entryReason
                }
            }
        }
        if ($gpuReading.state -ne 'read') {
            $why = if ($gpuReading.state -eq 'noslot') {
                'the host gave it no plan slot, so the gpu layer was not read'
            } else {
                'the sweep printed no summary, so the gpu layer was not read'
            }
            Write-Host "[camel-remote] gpu-plans: $why; nothing was compared with $gpuBaselineRel"
            if ($gpuReading.state -ne 'noslot') {
                # A sweep that cannot read its own runner is a fault in this observation. It is said
                # out loud and it decides nothing: the exit code is for a gpu case that turned red,
                # not for a tool that broke -- a merge must not wait on that, and the line above is
                # how the break is seen.
                Write-Host "[camel-remote] gpu-plans: that is a fault in this observation and not a gpu red; the layer is unguarded this run"
            }
        } else {
            $gpuReds = @($gpuReading.red)
            # A reading that named only part of its reds cannot be compared by name, and the names it
            # did not print are exactly the ones nobody would look at: they count as news, and -- this
            # is what was wrong before -- a recorded name it did not print is NOT evidence that the
            # name stopped being red. `red_more` is 0 when the reading named every red.
            $gpuIncomplete = $gpuReading.redMore -gt 0
            $gpuNew     = @($gpuReds | Where-Object { $gpuBaseline -notcontains $_ })
            $gpuStill   = @($gpuBaseline | Where-Object { $gpuReds -contains $_ })
            # Recorded for one reason, red for another: the entry says what the gate should accept,
            # and the gate now measures that claim instead of trusting the name. An entry with no
            # reason recorded cannot be checked, and says so rather than pretending.
            $gpuChanged = @($gpuStill | Where-Object {
                    $gpuReason[$_] -and $gpuEvidence.ContainsKey($_) -and
                    $gpuEvidence[$_] -ne '-' -and $gpuEvidence[$_] -ne $gpuReason[$_] })
            # A case whose failure printed no code and no assertion says nothing about why it is
            # red. That is not the same as "red for another reason": it is a reading that does not
            # reach the reason, and it is said to be that rather than turned into a verdict.
            $gpuUnchecked = @($gpuStill | Where-Object {
                    $gpuReason[$_] -and (-not $gpuEvidence.ContainsKey($_) -or $gpuEvidence[$_] -eq '-') })
            # Only a reading that named every red may say a recorded name is gone.
            $gpuGone = if ($gpuIncomplete) {
                @()
            } else {
                @($gpuBaseline | Where-Object { $gpuReds -notcontains $_ })
            }
            $gpuKnown = $gpuReds.Count - $gpuNew.Count
            Write-Host ("[camel-remote] gpu-plans: {0} red, {1} recorded, {2} new, {3} recorded for another reason, {4} recorded name(s) no longer red" -f `
                $gpuReds.Count, $gpuKnown, $gpuNew.Count, $gpuChanged.Count, $gpuGone.Count)
            if (-not $gpuBaselineFound) {
                Write-Host "[camel-remote] gpu-plans: there is no $gpuBaselineRel in this worktree, so every red above reads as new"
            }
            if ($gpuIncomplete) {
                Write-Host "[camel-remote] gpu-plans: the reading named only part of its reds ($($gpuReading.redMore) not named), so a recorded name it did not print is not shown to have stopped: nothing was called no longer red"
            }
            if ($gpuChanged.Count -gt 0) {
                foreach ($name in $gpuChanged) {
                    Write-Host "[camel-remote] gpu-plans: RED FOR ANOTHER REASON: $name was recorded for '$($gpuReason[$name])', red now for '$($gpuEvidence[$name])' -- the entry no longer says what is wrong, so this is news and not a known red"
                }
                if ($code -eq 0) { $code = 5 }
            }
            # An entry with no reason cannot be checked against anything, and says so rather than
            # passing for a verified one.
            $gpuNoReason = @($gpuStill | Where-Object { -not $gpuReason[$_] })
            if ($gpuNoReason.Count -gt 0) {
                Write-Host "[camel-remote] gpu-plans: recorded with no reason, so nothing checks what they are red for: $($gpuNoReason -join ', ') -- write 'name :: reason' in $gpuBaselineRel"
            }
            if ($gpuUnchecked.Count -gt 0) {
                Write-Host "[camel-remote] gpu-plans: the failure printed neither a diagnostic code nor an assertion, so the recorded reason could not be checked: $($gpuUnchecked -join ', ')"
            }
            if ($gpuNew.Count -gt 0 -or $gpuIncomplete) {
                $listing = if ($gpuNew.Count -gt 0) { $gpuNew -join ', ' } else { 'none named' }
                if ($gpuIncomplete) {
                    $listing += " (and $($gpuReading.redMore) more case(s) the line did not name)"
                }
                Write-Host "[camel-remote] gpu-plans: NOT RECORDED, so this run is red: $listing"
                Write-Host "[camel-remote] gpu-plans: fix the case, or record it in $gpuBaselineRel when it is red for the host's reason and not the code's (docs/test-plans.md, 'CUDA visibility')"
                if ($code -eq 0) { $code = 5 }
            }
            if ($gpuGone.Count -gt 0) {
                Write-Host "[camel-remote] gpu-plans: no longer red, and accepted as it is: $($gpuGone -join ', ') -- $gpuBaselineRel can be trimmed"
            }
        }
    }

    if ($null -ne $planStartedAt) {
        $seconds = [math]::Round(($finishedAt - $planStartedAt).TotalSeconds)
        Complete-Run $testStatePath $testStateMutex ([pscustomobject]@{
            worktree   = $name
            queuedAt   = $planStartedAt.ToString('o')
            startedAt  = $planStartedAt.ToString('o')
            finishedAt = $finishedAt.ToString('o')
            exit       = $code
            seconds    = $seconds
            plan       = @($Plan)
            tier       = $Tier
            remote     = $remoteName
        })
    } elseif (-not $NoBuild -or $checkNames.Count -gt 0) {
        # Build-only run, a checks run with no plan, or the build/link died before a plan started.
        # Record the build's own exit when there is one and no checker followed it; the ssh exit
        # code covers a dead connection and a red checker. The timestamps are the build's, so
        # history shows slot wait + compile, never transfer + queue folded into compile time; a
        # checks run is credited with its own stage, because it is work this entry must not hide.
        $recordedExit = if ($null -ne $astExit -and $astExit -ne 0) { $astExit }
            elseif ($null -ne $buildExit -and $checkNames.Count -eq 0) { $buildExit } else { $code }
        $buildStartForPanel = if ($null -ne $buildStartedAt) { $buildStartedAt } elseif ($null -ne $syncedAt) { $syncedAt } else { $startedAt }
        $queueForPanel = if ($null -ne $queueStartedAt) { $queueStartedAt } else { $startedAt }
        $buildEndForPanel = if ($null -ne $buildFinishedAt -and $checkNames.Count -eq 0) { $buildFinishedAt } else { $finishedAt }
        $seconds = [math]::Round(($buildEndForPanel - $buildStartForPanel).TotalSeconds)
        Complete-Run $buildStatePath $buildStateMutex ([pscustomobject]@{
            worktree   = $name
            queuedAt   = $queueForPanel.ToString('o')
            startedAt  = $buildStartForPanel.ToString('o')
            finishedAt = $buildEndForPanel.ToString('o')
            exit       = $recordedExit
            seconds    = $seconds
            remote     = $remoteName
        })
    } else {
        # -NoBuild with no plan and no -Check: nothing ran, so nothing enters `recent`.
        Complete-Run $buildStatePath $buildStateMutex $null
    }

    # The run script removed its own scratch directory.
    # Per-phase seconds, because "the run took 300 s" cannot tell a slow build from a slow plan.
    $transferEnd = if ($syncedAt) { $syncedAt } else { $finishedAt }
    $buildEnd = if ($planStartedAt) { $planStartedAt } elseif ($syncedAt) { $finishedAt } else { $transferEnd }
    $planEnd = if ($planStartedAt) { $finishedAt } else { $planStartedAt }
    $phases = "transfer {0:N1} s, build {1:N1} s" -f ($transferEnd - $startedAt).TotalSeconds, ($buildEnd - $transferEnd).TotalSeconds
    if ($null -ne $planStartedAt) { $phases += ", plan {0:N1} s" -f ($finishedAt - $planStartedAt).TotalSeconds }
    $loadEnd = Get-HostLoad
    if ($null -ne $loadEnd) {
        Write-Host ("[load] {0} {1} {2} (1m/5m/15m) after run" -f $loadEnd[0], $loadEnd[1], $loadEnd[2])
    }
    $loadLow = if ($null -ne $loadStart -and $null -ne $loadEnd) { [math]::Min($loadStart[0], $loadEnd[0]) }
        elseif ($null -ne $loadStart) { $loadStart[0] } elseif ($null -ne $loadEnd) { $loadEnd[0] } else { $null }
    if ($null -ne $loadLow) { $phases += ", load (min 1m) {0}" -f $loadLow }
    if ($null -ne $loadLow -and $loadLow -gt 8) {
        Write-Host ("[load] WARNING: 1m load {0} > 8 -- readings taken now are not comparable to quiet-host readings" -f $loadLow)
    }
    Write-Host ("[camel-remote] {0} exit {1} ({2}; total {3} s)" -f `
        $name, $code, $phases, [math]::Round($sw.Elapsed.TotalSeconds))
    exit $code
} finally {
    # The staging branch was this run's own scratch: the host checkout fetched it and keeps its
    # own branch, so the ref is dropped here -- after a normal finish, an exception or Ctrl-C
    # alike. Best-effort: a ref left behind means the next run force-pushes over it, never a
    # failed run, and the pile of stale lane-* refs this replaces is exactly what happened when
    # nothing ever deleted them.
    if ($stagingPushed) {
        try {
            git -C $Path push --quiet "$sshHost`:$bare" --delete "refs/heads/$branch" 2>&1 | Out-Null
        } catch {
        }
    }
    # Whatever happened -- normal finish, exception, Ctrl-C -- no holder may keep claiming that this
    # run is still going. This is a no-op after the normal path already completed the run. The two
    # files are cleared independently: a failure on one must not skip the other.
    foreach ($state in @(
        @{ path = $buildStatePath; mutex = $buildStateMutex },
        @{ path = $testStatePath; mutex = $testStateMutex }
    )) {
        try {
            Complete-Run $state.path $state.mutex $null
        } catch {
            Write-Host "[camel-remote] could not clear $($state.path): $_"
        }
    }
    # A run that died before its normal completion (push failure, Ctrl-C, an exception) would
    # otherwise stay "live" in the registry until the reader's staleness timeout. Record the end,
    # best-effort like every other registry write.
    try {
        if ($null -ne $run -and $null -eq $run.finishedAt) {
            $run.finishedAt = (Get-Date).ToString('o')
            if ($null -eq $run.exit) { $run.exit = $code }
            $run.phase = if ($run.exit -eq 0) { 'done' } else { 'fail' }
            Publish-Run
        }
    } catch {
    }
    $buildStateMutex.Dispose()
    $testStateMutex.Dispose()
    $runRegistryMutex.Dispose()
}
