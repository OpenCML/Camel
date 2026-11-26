# The queue's liveness rule, as readings: an entry whose process is alive is kept, an entry whose
# process was killed is not, and neither answer depends on a timer.
#
# The defect this file exists for: a waiter entry written by a process that had been killed was still
# shown to readers 85355 seconds later, naming a worktree that had committed forty minutes earlier.
# The queue said a build was coming that was not, and it stayed that way until somebody edited the
# file by hand -- removal happened only at acquire, at timeout, and (never) at exit.
#
#   pwsh -NoProfile -File internal/tools/camel-waiter.test.ps1
#
# Local only: no build, no plan, no checkout touched. Everything happens under %TEMP%, and the queue
# files are this test's own -- the real ones are `build-state.json` / `test-state.json` in the state
# directory, which this file never opens.

[CmdletBinding()]
param(
    # Keep the fixture directory for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'camel-waiter.ps1')

$script:checked = 0
$script:failed = 0
function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    $script:checked++
    if ($Ok) {
        Write-Host "  PASS  $Name"
    } else {
        $script:failed++
        Write-Host "  FAIL  $Name"
        if ($Detail) { Write-Host "        $Detail" }
    }
}

$root = Join-Path ([IO.Path]::GetTempPath()) "camel-waiter-test-$PID"
Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $root -Force | Out-Null
$queue = Join-Path $root 'build-state.json'

# The reader both queues use: parse the file, drop the entries whose process is gone.
function Read-Queue([string]$Path) {
    $state = Get-Content -Raw -Path $Path | ConvertFrom-Json
    # The shared rule, not a second copy of it: a test that filtered here would pass while the
    # helper the two queues call answered something else.
    return @(Get-CamelLiveWaiters $state.waiters)
}

try {
    # 1. A waiter this process made is alive, and stays alive as long as the process does.
    $mine = New-CamelWaiter -Worktree 'live-tree'
    Check 'a waiter made by this process reads as alive' (Test-CamelWaiterAlive $mine) "lock=$($mine.lock)"
    Check 'the entry carries the name of its liveness lock' ($null -ne $mine.PSObject.Properties['lock'] -and "$($mine.lock)" -ne '') "$($mine | ConvertTo-Json -Compress)"

    # 2. A waiter made by another process is alive while that process runs...
    $child = Start-Process -FilePath (Get-Process -Id $PID).Path -PassThru -ArgumentList @(
        '-NoProfile', '-Command',
        ". '$(Join-Path $PSScriptRoot 'camel-waiter.ps1')'; " +
        "`$w = New-CamelWaiter -Worktree 'ghost-tree'; " +
        "@{ waiters = @(`$w) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath '$queue'; " +
        "Start-Sleep -Seconds 120"
    )
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Test-Path $queue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    $written = $false
    if (Test-Path $queue) {
        $written = $true
    }
    Check 'the child wrote its queue entry' $written "no $queue after 30 s"
    if ($written) {
        $alive = Read-Queue $queue
        Check 'an entry whose process is running is kept' ($alive.Count -eq 1) "kept $($alive.Count) of 1"

        # 3. ...and is gone the moment the process dies, without waiting for a sweep or an interval.
        #    The reading is taken immediately after the kill: a rule that needed a timer would still
        #    show the entry here.
        Stop-Process -Id $child.Id -Force -ErrorAction SilentlyContinue
        [void]$child.WaitForExit(15000)
        # The operating system closes the dead process's handles as it tears the process down, which is
        # a moment after WaitForExit returns. The rule is about the next read, not about the instant
        # of the kill, so the reading waits for that moment and records how long it took -- a rule that
        # needed a sweep would still show the entry here, and the wait below is bounded.
        $killAt = Get-Date
        $after = Read-Queue $queue
        while ($after.Count -ne 0 -and ((Get-Date) - $killAt).TotalSeconds -lt 5) {
            Start-Sleep -Milliseconds 50
            $after = Read-Queue $queue
        }
        $lagMs = [int]((Get-Date) - $killAt).TotalMilliseconds
        Check 'an entry whose process was killed is not counted' ($after.Count -eq 0) "kept $($after.Count) of 1 after $lagMs ms"
        Write-Host "        (the entry left the queue $lagMs ms after the kill)"
        $dropped = Get-CamelLiveWaiters ((Get-Content -Raw -Path $queue | ConvertFrom-Json).waiters)
        Check 'the filter says how many it dropped' ($script:camelWaitersDropped -eq 1) "dropped $($script:camelWaitersDropped)"
        Check 'the filter returns the live entries it kept' (@($dropped).Count -eq 0) "returned $(@($dropped).Count)"
    }

    # 4. An entry that predates the lock is judged by its pid: this process is alive, a pid that is
    #    not a process is not.
    $old = [pscustomobject]@{ worktree = 'old-live'; pid = $PID; since = (Get-Date).ToString('o') }
    Check 'an entry with no lock and a live pid is kept' (Test-CamelWaiterAlive $old) "pid=$($old.pid)"
    $gone = [pscustomobject]@{ worktree = 'old-dead'; pid = 1; since = (Get-Date).ToString('o') }
    Check 'an entry with no lock and no such pid is dropped' (-not (Test-CamelWaiterAlive $gone)) 'pid 1 does not run as a Camel waiter'

    # 5. The way the queue file is read: a file carrying both, read the way `Read-State` reads it.
    @{ waiters = @($mine, $gone) } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $queue
    $kept = Read-Queue $queue
    Check 'a queue file carrying one live and one dead entry keeps one' ($kept.Count -eq 1) "kept $($kept.Count) of 2"
    Check 'the kept entry is the live one' ($kept.Count -eq 1 -and $kept[0].worktree -eq 'live-tree') "kept $($kept | ConvertTo-Json -Compress)"

    # 6. Nothing here waits for a timer: two reads a moment apart agree, and the second one is not
    #    what a sweep made. A rule that is wrong is wrong both times; a rule that is right does not
    #    need to be right later.
    $again = Read-Queue $queue
    Check 'two reads of one file agree' ($again.Count -eq $kept.Count) "first $($kept.Count), second $($again.Count)"
} finally {
    if ($null -ne $child) { Stop-Process -Id $child.Id -Force -ErrorAction SilentlyContinue }
    foreach ($handle in $script:camelWaiterHandles) {
        try { $handle.ReleaseMutex() } catch { }
        $handle.Dispose()
    }
    if (-not $Keep) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host ''
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }


