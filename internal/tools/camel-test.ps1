<#
.SYNOPSIS
    Runs one Camel test plan under a machine-wide lock, so test runs from several worktrees cannot overlap.

.DESCRIPTION
    Builds are already serialised by camel-build.ps1. Test runs were not: three worktrees each starting
    `node scripts/test.js --jobs 8` put twenty-odd processes and their compilations on the machine at once.
    This wrapper takes a global mutex of its own and runs one plan at a time, with a modest parallel width,
    and it reports what it is waiting for. It shares nothing with camel-build.ps1: a build and a test may
    overlap, because the blue screens this machine takes come from its Windows build, not from the load.

    The holder and everyone waiting are published to test-state.json, in the directory the local settings
    file names (`stateDir`; see camel-gate.example.json), so a reader (the Camel panel) can show who is
    testing and who is waiting, exactly like build-state.json:

      { "updatedAt": iso,
        "holder": { "worktree": "h37-let", "path": "...", "pid": 123,
                    "queuedAt": iso, "startedAt": iso, "plan": ["smoke"], "target": "smoke",
                    "tier": "", "jobs": 8, "phase": "running", "lastLine": "..." } | null,
        "waiters": [ { "worktree": "h5-msg", "pid": 456, "since": iso, "plan": ["feat/para"] } ],
        "recent": [ { "worktree": "...", "queuedAt": iso, "startedAt": iso, "finishedAt": iso,
                      "exit": 0, "seconds": 120, "plan": ["smoke"], "tier": "" } ] }

    It also prints a one-line summary of the run (plan, jobs, exit code, seconds) so a caller can read the
    result without parsing the runner's output.

.EXAMPLE
    pwsh -NoProfile -File internal/tools/camel-test.ps1 -Path <worktree> -Plan smoke
    pwsh -NoProfile -File internal/tools/camel-test.ps1 -Path <worktree> `
        -Plan test/plans/feat/modules/graph.plan.toml -Jobs 6
#>
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string[]]$Plan,
    # The local settings file; empty means camel-gate.local.json beside this script, which is the
    # only place this machine's paths may appear.
    [string]$LocalConfig = '',
    # The directory holding test-state.json. Empty means the local settings file decides.
    [string]$StateDir = '',
    [int]$Jobs = 8,
    # Passed through to the runner, e.g. 'debug' for the debug tier.
    [string]$Tier = '',
    [int]$TimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'

# `-File script.ps1 -Plan a,b` arrives as the single string "a,b" rather than an array, so split each
# element again; a caller in-process passing a real array still works.
$Plan = @($Plan | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

if (-not (Test-Path (Join-Path $Path 'package.json'))) {
    Write-Host "[camel-test] not a Camel worktree: $Path"
    exit 2
}

. (Join-Path $PSScriptRoot 'camel-gate-config.ps1')
$cfg = Get-CamelGateConfig -LocalConfig $LocalConfig -ScriptRoot $PSScriptRoot -Override @{ stateDir = $StateDir }
if (-not $cfg.stateDir) { Stop-CamelGateConfig 'camel-test' $cfg 'stateDir' }
$statePath = Join-Path $cfg.stateDir 'test-state.json'
$worktree = Split-Path -Leaf (Resolve-Path $Path)
# Test runs only; builds take Global\CamelBuildSerialized in camel-build.ps1.
$mutex = New-Object System.Threading.Mutex($false, 'Global\CamelTestSerialized')
$stateMutex = New-Object System.Threading.Mutex($false, 'Global\CamelTestState')
$acquired = $false
$startedAt = $null

function Read-State {
    if (-not (Test-Path $statePath)) {
        return [pscustomobject]@{ updatedAt = $null; holder = $null; waiters = @(); recent = @() }
    }
    try {
        $state = Get-Content -Raw -Path $statePath | ConvertFrom-Json
    } catch {
        $state = $null
    }
    $fresh = [pscustomobject]@{ updatedAt = $null; holder = $null; waiters = @(); recent = @() }
    if ($null -eq $state -or -not ($state.PSObject.Properties['waiters']) -or
        -not ($state.PSObject.Properties['recent']) -or -not ($state.PSObject.Properties['holder'])) {
        return $fresh
    }
    return $state
}

# Every state change is a read-modify-write under one mutex, so two runs cannot lose an update.
function Edit-State([scriptblock]$change) {
    # Same abandoned-mutex rule as the work lock below: an exception still means we hold it.
    try {
        $null = $stateMutex.WaitOne([TimeSpan]::FromSeconds(30))
    } catch [System.Threading.AbandonedMutexException] {
    }
    try {
        $state = Read-State
        $change.Invoke($state)
        $state.updatedAt = (Get-Date).ToString('o')
        $json = $state | ConvertTo-Json -Depth 6
        $tmp = "$statePath.$PID.tmp"
        Set-Content -Path $tmp -Value $json -Encoding utf8
        Move-Item -Force -Path $tmp -Destination $statePath
    } finally {
        $stateMutex.ReleaseMutex()
    }
}

function Add-Waiter {
    Edit-State {
        param($state)
        $waiters = @($state.waiters | Where-Object { $_ -and $_.pid -ne $PID })
        $waiters += [pscustomobject]@{
            worktree = $worktree
            pid      = $PID
            since    = (Get-Date).ToString('o')
            plan     = @($Plan)
        }
        $state.waiters = $waiters
    }
}

function Remove-Waiter {
    Edit-State {
        param($state)
        $state.waiters = @($state.waiters | Where-Object { $_ -and $_.pid -ne $PID })
    }
}

function Set-Holder($holder) {
    Edit-State {
        param($state)
        $state.holder = $holder
    }
}

function Update-Holder([string]$line, [string]$target = '') {
    $trimmed = $line.Trim()
    if (-not $trimmed) { return }
    Edit-State {
        param($state)
        if ($null -ne $state.holder) {
            if ($target) { $state.holder.target = $target }
            $state.holder.phase = 'running'
            $state.holder.lastLine = $trimmed
        }
    }
}

try {
    $queuedAt = Get-Date
    Add-Waiter
    Write-Host "[camel-test] $worktree is waiting for the test lock"
    $acquired = $false
    # The exception an abandoned mutex raises means the wait *succeeded*: the previous holder died
    # owning it (a test or a build killed by a crash), and this process now owns it. Treating it as
    # an error would exit without releasing, and every later waiter would inherit the same state.
    try {
        $acquired = $mutex.WaitOne([TimeSpan]::FromMinutes($TimeoutMinutes))
    } catch [System.Threading.AbandonedMutexException] {
        Write-Host '[camel-test] took over the test lock from a process that died holding it'
        $acquired = $true
    }
    if (-not $acquired) {
        Remove-Waiter
        Write-Host '[camel-test] timed out waiting for the test lock'
        exit 3
    }
    Remove-Waiter
    $startedAt = Get-Date
    Set-Holder ([pscustomobject]@{
        worktree  = $worktree
        path      = $Path
        pid       = $PID
        queuedAt  = $queuedAt.ToString('o')
        startedAt = $startedAt.ToString('o')
        plan      = @($Plan)
        target    = $Plan[0]
        tier      = $Tier
        jobs      = $Jobs
        phase     = 'starting'
        lastLine  = ''
    })
    Write-Host "[camel-test] $worktree has the lock; $($Plan.Count) target(s), jobs $Jobs"

    Push-Location $Path
    try {
        $env:CAMEL_HOME = Join-Path $Path 'out\latest'
        $env:PATH = (Join-Path $env:CAMEL_HOME 'bin') + ';' + $env:PATH
        $code = 0
        foreach ($target in $Plan) {
            Write-Host "[camel-test] $target"
            Update-Holder "running $target" $target
            $arguments = @('scripts/test.js', $target, '--jobs', "$Jobs")
            if ($Tier) { $arguments += @('--tier', $Tier) }
            & node @arguments 2>&1 | ForEach-Object {
                $line = "$_"
                # Per-test status lines are what a reader wants to see; keep the writes rare.
                if ($line -match '^(FAIL|pass |fail )|^\[|^PASS|^ok ') { Update-Holder $line }
                Write-Host $line
            }
            $planCode = $LASTEXITCODE
            if ($planCode -ne 0) { $code = $planCode }
        }
    } finally {
        Pop-Location
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
            plan       = @($Plan)
            tier       = $Tier
        }) + $recent
    }
    Write-Host "[camel-test] done ($worktree): $($Plan -join ', '), exit $code, $seconds s"
    exit $code
} finally {
    if ($acquired) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
    $stateMutex.Dispose()
}