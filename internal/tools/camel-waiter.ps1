# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

# A waiter is a claim on a queue, and the claim has to end with the process that made it.
#
# The build and test state files carry a `waiters` list that whoever is waiting writes and everyone
# else reads. An entry left by a process that was killed (a build interrupted by a crash, an agent
# stopped between two commands) stays in that list for ever: one entry was still being shown to
# readers 85355 seconds after its process had gone, describing a worktree that had committed forty
# minutes earlier. A queue nobody is in is worse than an empty one -- it says a build is coming that
# is not, and it is read as the reason a slot cannot be had.
#
# The liveness signal is the operating system's, not a timer's: each waiter creates a named mutex
# and holds it for as long as it lives, and the kernel releases it when the process exits for any
# reason. A reader asks `WaitOne(0)` on that name -- taken means nobody holds it, so the process is
# gone -- which costs one syscall and needs no sweep, no interval and no clock. A periodic sweep was
# the alternative and it is a worse rule: it turns "is it there" into "was it there least recently",
# so the answer a reader gets depends on when the sweeper last ran.
#
# The name carries the pid because that is what makes two waiters distinct, and the entry carries the
# name so that a reader does not have to guess it. An entry written before this lock existed has no
# name; it is judged by its pid, which is a weaker signal (a pid is reused) but still a signal, and
# the alternative -- keeping every such entry -- is the defect itself.

$script:camelWaiterPrefix = 'Global\CamelQueueWaiter-'
# The handles are kept for the life of the process that holds them: a mutex whose last handle is
# collected is released, which would make a live waiter read as a dead one.
$script:camelWaiterHandles = @()
$script:camelWaitersDropped = 0

function New-CamelWaiter {
    param(
        [string]$Worktree,
        $Plan = $null
    )
    $lock = "$script:camelWaiterPrefix$PID"
    $mutex = New-Object System.Threading.Mutex($false, $lock)
    # The claim is this process's: taking it here means a reader sees "alive" from the moment the
    # entry is written, with no window in which the entry exists and the signal does not.
    [void]$mutex.WaitOne(0)
    $script:camelWaiterHandles += $mutex
    $record = [ordered]@{
        worktree = $Worktree
        pid      = $PID
        since    = (Get-Date).ToString('o')
        lock     = $lock
    }
    if ($null -ne $Plan) { $record.plan = @($Plan) }
    return [pscustomobject]$record
}

function Test-CamelWaiterAlive {
    param($Waiter)
    if ($null -eq $Waiter) { return $false }
    # A record naming this process is alive by definition, and it is answered here rather than by the
    # lock: a mutex is recursive within a thread, so `WaitOne(0)` on a name this thread already holds
    # succeeds, and reading that as "nobody holds it" would call the caller's own entry dead.
    $own = 0
    if ([int]::TryParse("$($Waiter.pid)", [ref]$own) -and $own -eq $PID) { return $true }
    $lock = $null
    if ($Waiter.PSObject.Properties['lock']) { $lock = $Waiter.lock }
    if ($lock) {
        $mutex = $null
        try {
            $mutex = New-Object System.Threading.Mutex($false, [string]$lock)
            $held = $false
            try {
                # An abandoned mutex is acquired with an exception: the holder died while holding it,
                # so the exception is the answer "gone" and not an error.
                $held = $mutex.WaitOne(0)
            } catch [System.Threading.AbandonedMutexException] {
                $held = $true
            }
            if (-not $held) { return $true }
            # Nobody holds the name: this waiter's process is gone. Give the name back so closing the
            # handle leaves nothing held.
            try { $mutex.ReleaseMutex() } catch { }
            return $false
        } catch {
            # A name this process may not open says nothing about the waiter; fall through to the pid.
        } finally {
            if ($null -ne $mutex) { $mutex.Dispose() }
        }
    }
    $id = 0
    if (-not [int]::TryParse("$($Waiter.pid)", [ref]$id)) { return $false }
    return $null -ne (Get-Process -Id $id -ErrorAction SilentlyContinue)
}

# The live entries of a queue, in the order they arrived. A dead entry is not counted, and the count
# of what was dropped is left in `$script:camelWaitersDropped` so the caller can say it.
function Get-CamelLiveWaiters {
    param($Waiters)
    $live = @()
    $script:camelWaitersDropped = 0
    foreach ($waiter in @($Waiters)) {
        if ($null -eq $waiter) { continue }
        if (Test-CamelWaiterAlive $waiter) {
            $live += $waiter
        } else {
            $script:camelWaitersDropped++
        }
    }
    # Returned unwrapped: every caller asks for the list with @(...), and a one-element list
    # wrapped here would arrive as one object that is itself an array.
    return $live
}

