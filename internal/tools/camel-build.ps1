# Serialized Camel build with one shared state file.
#
# Several agents work in separate worktrees at once. Their builds must not run at the same time
# (a machine with every core compiling for ten minutes), so every build takes one machine-wide
# mutex. Because only one build runs at a time, that build may use many cores: the compiler
# limit defaults to 16 of the 28 logical cores, leaving room for the agents that are reading
# code and running tests meanwhile.
#
#   pwsh -NoProfile -File internal/tools/camel-build.ps1 -Path <worktree> [-Parallel 16]
#   pwsh -NoProfile -File internal/tools/camel-build.ps1 -Path <worktree> -Check 5
#
# Test runs have a lock of their own (camel-test.ps1): a build and a test may overlap. The blue
# screens this machine keeps taking come from its Windows build (0x50 PAGE_FAULT_IN_NONPAGED_AREA
# twice within an hour), not from the load, so serialising the two kinds of work against each other
# bought nothing.
#
# The holder and everyone waiting are published to build-state.json, in the directory the local
# settings file names (`stateDir`; see camel-gate.example.json), so a reader (the desk panel, panel-deck)
# can show who is compiling and who is waiting:
#
#   { "updatedAt": iso,
#     "holder": { "worktree": "h9-unicode", "path": "...", "pid": 123,
#                 "queuedAt": iso, "startedAt": iso,
#                 "done": 12, "total": 453, "phase": "compiling", "lastLine": "..." } | null,
#     "waiters": [ { "worktree": "h4a-constas", "pid": 456, "since": iso } ],
#     "recent": [ { "worktree": "...", "queuedAt": iso, "startedAt": iso, "finishedAt": iso,
#                   "exit": 0, "seconds": 600 } ] }
#
# A waiter is shown only while its process exists: it removes itself on every way out of the wait, and
# every touch of the file drops the entries of processes that are gone (`Select-LiveCamelWaiters`).
#
# "recent" keeps the last 30 builds, which is what a timeline of the queue is drawn from: the gap
# between queuedAt and startedAt is the time the build waited for the lock.
#
# Exit code is the build's exit code (0 for -Check). A state file that cannot be replaced -- a reader
# has it open, or the directory is unwritable -- is a warning and never changes that code: the state
# files are advisory, and the write itself is `camel-gate-state.ps1#Publish-CamelGateState`.

param(
    [Parameter(Mandatory = $true)][string]$Path,
    # The local settings file; empty means camel-gate.local.json beside this script, which is the
    # only place this machine's paths may appear.
    [string]$LocalConfig = '',
    # The directory holding build-state.json. Empty means the local settings file decides.
    [string]$StateDir = '',
    # Appended to the names of the machine-wide locks. Empty is the one lock every real build takes; a
    # test that must hold a lock without blocking real builds names its own.
    [string]$LockScope = '',
    [int]$Parallel = 16,
    [int]$Check = 0,
    # Release is what every ordinary task builds; Debug exists so the assertions that are compiled
    # out of a release build can actually run (task H30). Both take the same machine-wide lock, so a
    # debug build cannot overlap a release one.
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'camel-gate-config.ps1')
# The run registry's writer, the same one the host runner uses (`internal/tools/run-registry.ps1`).
. (Join-Path $PSScriptRoot 'run-registry.ps1')
. (Join-Path $PSScriptRoot 'camel-gate-state.ps1')
$cfg = Get-CamelGateConfig -LocalConfig $LocalConfig -ScriptRoot $PSScriptRoot -Override @{ stateDir = $StateDir }
if (-not $cfg.stateDir) { Stop-CamelGateConfig 'camel-build' $cfg 'stateDir' }
$statePath = Join-Path $cfg.stateDir 'build-state.json'
$worktree = Split-Path -Leaf (Resolve-Path $Path)
# Builds only. Test runs take Global\CamelTestSerialized in camel-test.ps1.
$buildMutex = New-Object System.Threading.Mutex($false, "Global\CamelBuildSerialized$LockScope")
$stateMutex = New-Object System.Threading.Mutex($false, "Global\CamelBuildState$LockScope")
$acquired = $false
# True from the moment this process is in the queue file's waiters until it has taken itself out.
$waiting = $false
$startedAt = $null
# How many state updates this run could not publish. Advisory: reported at the end, never the exit
# code, and the first one is said out loud when it happens.
$script:stateUnpublished = 0

# --- the run registry -------------------------------------------------------
# The panel's one list of runs is `run-registry.jsonl`, and a local build that is missing there is a
# build nobody can see. So this helper publishes the same row shape the host runner does. What it
# cannot publish is the one thing a local build does not have: `tokens`. The token pool lives on the
# build host; a build run here draws nothing from it, so the key is *absent* from these rows and a
# reader is told "not reported" -- writing 0 would say the pool was empty, which is a different fact.
$runRegistryPath = Join-Path $cfg.stateDir 'run-registry.jsonl'
$runRegistryMutex = New-Object System.Threading.Mutex($false, 'Global\CamelRunRegistry')
$runStartedAt = Get-Date
$runId = "$PID-$($runStartedAt.ToString('yyyyMMddHHmmss'))"
$stageStartedAt = $runStartedAt
$publishedPhase = $null
$lastRunPublish = [datetime]::MinValue
$buildRow = @{
    runId           = $runId
    pid             = $PID
    worktree        = $worktree
    path            = $Path
    remote          = 'local'
    local           = $true
    startedAt       = $runStartedAt.ToString('o')
    updatedAt       = $runStartedAt.ToString('o')
    phase           = 'queued'
    state           = $null
    slots           = 0
    slot            = '0'
    noslotWaitedFor = $null
    overlapping     = @{ atStart = $null; atEnd = $null }
    loadAtStart     = $null
    loadAtEnd       = $null
    queuedAt        = $runStartedAt.ToString('o')
    # Local admission: the machine-wide mutex this helper takes, not a host slot and not a token.
    queuedAtKind    = 'local'
    queueSeconds    = 0
    buildStartedAt  = $null
    buildFinishedAt = $null
    planStartedAt   = $null
    finishedAt      = $null
    exit            = $null
    buildExit       = $null
    done            = 0
    total           = 0
    jobs            = $Parallel
    jobsRequested   = $Parallel
    buildJobs       = $Parallel
    tier            = ''
    plan            = @()
    target          = ''
    lastLine        = "[camel-build] waiting for the machine-wide build lock ($worktree)"
    stageSeconds    = 0
}

function Publish-LocalBuild([string]$phase, [string]$lastLine = '') {
    try {
        $now = Get-Date
        if ($phase -ne $publishedPhase) {
            $script:publishedPhase = $phase
            $script:stageStartedAt = $now
        }
        $buildRow.phase = $phase
        $buildRow.stageSeconds = [int](($now - $stageStartedAt).TotalSeconds)
        $buildRow.updatedAt = $now.ToString('o')
        if ($lastLine) { $buildRow.lastLine = $lastLine }
        [void](Add-RunRegistryRow -Path $runRegistryPath -Mutex $runRegistryMutex -Row $buildRow)
        $script:lastRunPublish = $now
    } catch {
        # A registry write must never fail the build it describes.
    }
}
Publish-LocalBuild 'queued'

function Read-State {
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

# Every state change is a read-modify-write under one mutex, so two builds cannot lose an update.
function Edit-State([scriptblock]$change) {
    # Same abandoned-mutex rule as the work lock below: an exception still means we hold it.
    try {
        $null = $stateMutex.WaitOne([TimeSpan]::FromSeconds(30))
    } catch [System.Threading.AbandonedMutexException] {
    }
    try {
        $state = Read-State
        # Every touch of the file, under its mutex, first drops the waiters whose process is gone
        # (`camel-gate-state.ps1#Select-LiveCamelWaiters`), so a crash that skipped a `finally` is
        # cleaned by whoever comes next.
        $state.waiters = Select-LiveCamelWaiters $state.waiters
        $change.Invoke($state)
        $state.updatedAt = (Get-Date).ToString('o')
        $json = $state | ConvertTo-Json -Depth 6
        # Not `Move-Item -Force`: it reports a destination another process holds open as
        # ERROR_ALREADY_EXISTS and, under `$ErrorActionPreference = 'Stop'`, that ended a build that
        # had succeeded. A publish that cannot complete is counted and dropped; the next progress
        # line carries the newer state.
        if (-not (Publish-CamelGateState -Path $statePath -Json $json)) {
            $script:stateUnpublished++
            if ($script:stateUnpublished -eq 1) {
                Write-Host "[camel-build] WARNING: build state not published (a reader kept $statePath busy); the state files are advisory and this build's result stands"
            }
        }
    } finally {
        $stateMutex.ReleaseMutex()
    }
}

function Add-Waiter {
    $script:waiting = $true
    Edit-State {
        param($state)
        $waiters = @($state.waiters | Where-Object { $_ -and $_.pid -ne $PID })
        $waiters += [pscustomobject]@{
            worktree = $worktree
            pid      = $PID
            since    = (Get-Date).ToString('o')
        }
        $state.waiters = $waiters
    }
}

function Remove-Waiter {
    Edit-State {
        param($state)
        $state.waiters = @($state.waiters | Where-Object { $_ -and $_.pid -ne $PID })
    }
    $script:waiting = $false
}

function Set-Holder($holder) {
    Edit-State {
        param($state)
        $state.holder = $holder
    }
}

function Update-Progress([string]$line) {
    if ($line -match '^\[(\d+)/(\d+)\]') {
        $done = [int]$Matches[1]
        $total = [int]$Matches[2]
        $buildRow.done = $done
        $buildRow.total = $total
        $buildRow.lastLine = $line.Trim()
        # The compiler prints several lines a second; the registry needs a heartbeat, not a transcript.
        if (((Get-Date) - $lastRunPublish).TotalSeconds -ge 3) {
            Publish-LocalBuild 'building' $line.Trim()
        }
        Edit-State {
            param($state)
            if ($null -ne $state.holder) {
                $state.holder.done = $done
                $state.holder.total = $total
                $state.holder.phase = 'compiling'
                $state.holder.lastLine = $line.Trim()
            }
        }
    } elseif ($line -match '^\[(STEP|DONE|FAIL)\]') {
        Edit-State {
            param($state)
            if ($null -ne $state.holder) {
                $state.holder.phase = $Matches[1].ToLower()
                $state.holder.lastLine = $line.Trim()
            }
        }
    }
}

try {
    if ($Check -le 0 -and -not (Test-Path (Join-Path $Path 'package.json'))) {
        Write-Host "[camel-build] not a Camel worktree: $Path"
        exit 2
    }

    $queuedAt = Get-Date
    Add-Waiter
    Write-Host "[camel-build] $worktree is waiting for the build lock"
    $acquired = $false
    # A process killed while it held the lock (a build interrupted by a crash, or a stopped agent)
    # leaves the mutex abandoned, and WaitOne reports that as an exception -- but the exception means
    # the wait *succeeded* and this process now owns the mutex. Treating it as an error would exit
    # without ever releasing, and every later waiter would inherit the abandoned state.
    try {
        $acquired = $buildMutex.WaitOne([TimeSpan]::FromMinutes(240))
    } catch [System.Threading.AbandonedMutexException] {
        Write-Host '[camel-build] took over the build lock from a process that died holding it'
        $acquired = $true
    }
    if (-not $acquired) {
        Write-Host '[camel-build] timed out waiting for the build lock'
        exit 3
    }
    Remove-Waiter
    $startedAt = Get-Date
    Set-Holder ([pscustomobject]@{
        worktree  = $worktree
        config    = $Config
        path      = $Path
        pid       = $PID
        startedAt = $startedAt.ToString('o')
        queuedAt  = $queuedAt.ToString('o')
        done      = 0
        total     = 0
        phase     = if ($Check -gt 0) { 'check' } else { 'starting' }
        lastLine  = ''
    })
    Write-Host "[camel-build] $worktree has the lock"
    $buildRow.buildStartedAt = $startedAt.ToString('o')
    $buildRow.queueSeconds = [int](($startedAt - $runStartedAt).TotalSeconds)
    Publish-LocalBuild $(if ($Check -gt 0) { 'checks' } else { 'building' }) "[camel-build] building $worktree ($Config)"

    if ($Check -gt 0) {
        Start-Sleep -Seconds $Check
        $code = 0
    } else {
        Write-Host "[camel-build] building $worktree ($Config) with $Parallel compiler processes"
        $env:CMAKE_BUILD_PARALLEL_LEVEL = "$Parallel"
        Push-Location $Path
        try {
            & npm run $(if ($Config -eq 'Debug') { 'debug' } else { 'build' }) 2>&1 | ForEach-Object {
                $line = "$_"
                Update-Progress $line
                Write-Host $line
            }
            $code = $LASTEXITCODE
        } finally {
            Pop-Location
        }
    }

    $seconds = [math]::Round(((Get-Date) - $startedAt).TotalSeconds)
    Set-Holder $null
    Edit-State {
        param($state)
        $recent = @($state.recent | Where-Object { $_ } | Select-Object -First 29)
        $state.recent = @([pscustomobject]@{
            worktree   = $worktree
            queuedAt   = $queuedAt.ToString('o')
            startedAt  = $startedAt.ToString('o')
            finishedAt = (Get-Date).ToString('o')
            exit       = $code
            seconds    = $seconds
        }) + $recent
    }
    Write-Host "[camel-build] done ($worktree), exit $code, $seconds s"
    $buildRow.buildFinishedAt = (Get-Date).ToString('o')
    $buildRow.finishedAt = $buildRow.buildFinishedAt
    $buildRow.exit = $code
    $buildRow.buildExit = $code
    Publish-LocalBuild $(if ($code -eq 0) { 'done' } else { 'fail' }) "[camel-build] done, exit $code"
    # Its own line, after the build's result and never inside it: how many state updates the panel
    # did not get is a fact about the panel, not about what the compiler did.
    if ($script:stateUnpublished -gt 0) {
        Write-Host "[camel-build] $($script:stateUnpublished) state update(s) were not published (advisory; the exit code above is the build's)"
    }
    exit $code
} finally {
    # Every way out of the wait -- the lock taken, the timeout, an error, Ctrl-C -- removes this
    # process from the waiters; the removal in the body is the same call, made early.
    if ($waiting) {
        try { Remove-Waiter } catch { }
    }
    if ($acquired) {
        if ($null -ne $startedAt -and $Check -le 0) {
            $null = $startedAt
        }
        $buildMutex.ReleaseMutex()
    }
    $buildMutex.Dispose()
    $stateMutex.Dispose()
}