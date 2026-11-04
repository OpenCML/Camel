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
#     prints the free space it has, and refuses to build under 5 GB;
#   * `-Clean` drops one checkout by name when a task is finished.
#
# Exit code is the exit code of the build or of the plan run, or -- when -Check was given and the
# plan was green -- of the first checker that failed. 4 means the -NoBuild guard refused before
# anything ran; 3 means the host is short of disk; 2 means the worktree, the push or the transfer
# failed, or -Check named a checker that does not exist.
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
#     so a lane runs what the gate runs and not a paraphrase. `-Check all` runs all of them in order;
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
    # Run these of the repository's static checkers on the host, after the build and after the plan:
    # names from the table below, comma-separated, or `all` for every one of them. Each checker's
    # output is printed verbatim with its exit code, and the run exits non-zero when one failed.
    # An unknown name is exit 2 before anything runs; see "what the panel sees" and the -Check
    # section of the header for the stages and the exit code.
    [string[]]$Check = @(),
    # The toolchain's libstdc++ directory, put first on the single LD_LIBRARY_PATH this run exports
    # (see the header). A current build carries its own copy in `libs/` with a `$ORIGIN` rpath, so
    # the export only serves the older installs still sitting in host checkouts. Empty means the
    # local settings file decides.
    [string]$LibStdCxxDir = ''
)

$ErrorActionPreference = 'Stop'

# --- the machine-local settings --------------------------------------------
# Nothing below names a host or a path on this workstation: those come from
# camel-gate.local.json beside this script (git-ignored), and a run that needs a setting the file
# does not carry stops here, with the file to create, instead of reaching the wrong machine.
. (Join-Path $PSScriptRoot 'camel-gate-config.ps1')
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
}

# The same "a,b can arrive as one string" split as -Plan, so `-Check all` and `-Check a,b` both
# work. An unknown name stops here: nothing has been pushed yet, and the alternative -- running the
# checkers that happen to match -- would report a green over the ones that were never run.
$Check = @($Check | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$checkNames = @()
foreach ($requested in $Check) {
    if ($requested -eq 'all') {
        foreach ($known in $checkers.Keys) { if ($checkNames -notcontains $known) { $checkNames += $known } }
    } elseif ($checkers.Contains($requested)) {
        if ($checkNames -notcontains $requested) { $checkNames += $requested }
    } else {
        Write-Host "[camel-remote] unknown -Check name '$requested'; known names: $(($checkers.Keys) -join ', '), all"
        exit 2
    }
}
$checkList = $checkNames -join ' '

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
    Set-BuildPhase 'starting' ("[camel-remote] {0}: {1} changed file(s), {2} untracked; sending to {3}" -f `
        $name, $changedCount, $untracked.Count, $sshHost)

    # --- carry it over ------------------------------------------------------
    # The patch, the untracked files and the run script travel in one scp: each ssh/scp costs a
    # connection, and there is no ControlMaster on the Windows client to amortise it.
    $remoteScratch = "/tmp/camel-remote-$name-$stamp"

    # What the checkout should be at afterwards, and which host checkouts are still wanted. `-s` on
    # the patch and `-f` on the tar are the fast path's guard: a clean tree needs neither.
    $headSha = (git -C $Path rev-parse HEAD).Trim()
    $liveNames = @()
    try {
        $liveNames = @(git -C $Path worktree list --porcelain 2>$null |
            Where-Object { $_ -like 'worktree *' } |
            ForEach-Object { Split-Path -Leaf $_.Substring(9).Trim() })
    } catch {
        # An old git without --porcelain: keep every checkout rather than delete the wrong one.
        $liveNames = @($name)
    }
    if ($liveNames -notcontains $name) { $liveNames += $name }

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
        $checkBlocks += @"

echo '[camel-remote] --- check ${checkName}: $shownCommand ---'
$shownCommand
c=`$?
if [ `$c -ne 0 ] && [ `$checkfail -eq 0 ]; then checkfail=`$c; fi
echo "[camel-remote] --- ${checkName}: exit `$c ---"
"@
    }

    # Housekeeping first: a checkout whose local worktree is gone is only disk, and this run needs
    # room to build. LIVE is the list of worktrees that still exist on the workstation.
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
LIVE='$($liveNames -join ' ')'
reclaimed=0
for d in "`$HOME/$($cfg.hostWorkRoot)"/*/; do
    n=`$(basename "`$d")
    case "`$n" in $bareLeaf|bin) continue;; esac
    # The runner's scratch root lives next to the checkouts and is regenerated every run: removing
    # it here is not a cleanup, and it must not invalidate the cached checkout-size measurement.
    case "`$n" in *-test-tmp) continue;; esac
    case " `$LIVE " in *" `$n "*) continue;; esac
    echo "[camel-remote] removing host checkout `$n, its worktree is gone"
    rm -rf "`$d"
    reclaimed=1
done
find /tmp -maxdepth 1 -name 'camel-remote-*' -mtime +0 -exec rm -rf {} + 2>/dev/null || true
freekb=`$(df -P -k "`$HOME" | awk 'NR==2{print `$4}')
# The footprint of the checkouts is informational, but measuring it walks every file under
# ~/camel-work (43 GB here, ~50 s on a loaded host, once per run). Keep the number in a small
# cache and recompute it only when it is an hour old or a cleanup above actually removed something.
sizecache=/tmp/camel-checkout-size
sizeage=`$(( `$(date +%s) - `$(stat -c %Y "`$sizecache" 2>/dev/null || echo 0) ))
if [ "`$reclaimed" = 1 ] || [ ! -s "`$sizecache" ] || [ "`$sizeage" -gt 3600 ]; then
    du -sm "`$HOME/$($cfg.hostWorkRoot)" 2>/dev/null | cut -f1 > "`$sizecache"
fi
echo "[camel-remote] host: `$((freekb/1048576)) GB free, `$(cat "`$sizecache" 2>/dev/null) MB of checkouts"
if [ "`$freekb" -lt 5242880 ]; then echo '[camel-remote] under 5 GB free on the host; run -Clean on the worktrees that are done'; exit 3; fi
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

    # One line per checker: the output above is the command's own, and this list says which name
    # each exit code belongs to, next to the run's exit code.
    if ($checkResults.Count -gt 0) {
        $checkFailed = @($checkResults | Where-Object { $_.exit -ne 0 })
        $checksSeconds = if ($null -ne $checksStartedAt) { [math]::Round(($finishedAt - $checksStartedAt).TotalSeconds) } else { 0 }
        Write-Host ("[camel-remote] checks: {0} run, {1} failed ({2} s)" -f `
            $checkResults.Count, $checkFailed.Count, $checksSeconds)
        foreach ($result in $checkResults) { Write-Host ("[camel-remote]   {0} exit {1}" -f $result.name, $result.exit) }
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
        $recordedExit = if ($null -ne $buildExit -and $checkNames.Count -eq 0) { $buildExit } else { $code }
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
