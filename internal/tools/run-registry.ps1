# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

# The run registry's one writer.
#
# `run-registry.jsonl` is one compact JSON line per run snapshot, keyed by `runId`; a reader keeps the
# last line per run. Three callers publish into it -- the host runner (`camel-remote.ps1`) and the two
# local helpers (`camel-build.ps1`, `camel-test.ps1`) -- and a second copy of the append-and-compact
# rule is how two writers end up disagreeing about the same file. So the rule lives here, once.
#
# Every write is best-effort on purpose: the registry is a reading of the work, never a gate on it.
# A locked or unwritable file must not fail a build or a plan; the caller learns nothing (the function
# returns `$false` and the run goes on). The named mutex serialises appends between concurrent runs;
# the file is compacted only when it grows past half a megabyte, keeping the newest lines.

function Add-RunRegistryRow {
    param(
        # Absolute path of `run-registry.jsonl`.
        [Parameter(Mandatory = $true)][string]$Path,
        # The registry's own mutex (`Global\CamelRunRegistry`), shared by the three writers.
        [Parameter(Mandatory = $true)][System.Threading.Mutex]$Mutex,
        # The snapshot: a hashtable of the run's fields.
        [Parameter(Mandatory = $true)]$Row,
        [int]$CompactBytes = 524288,
        [int]$KeepLines = 300
    )
    $encoding = New-Object System.Text.UTF8Encoding($false)
    $held = $false
    try {
        try {
            $held = $Mutex.WaitOne([TimeSpan]::FromSeconds(2))
        } catch [System.Threading.AbandonedMutexException] {
            # The previous writer died holding it; the wait succeeded and this process now owns it.
            $held = $true
        }
        if (-not $held) { return $false }
        $info = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
        if ($null -ne $info -and $info.Length -gt $CompactBytes) {
            $keep = @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue |
                Select-Object -Last $KeepLines)
            $tmp = "$Path.$PID.tmp"
            [System.IO.File]::WriteAllLines($tmp, [string[]]$keep, $encoding)
            # Not `Move-Item -Force`: it reports a destination a reader holds open as
            # ERROR_ALREADY_EXISTS (183, "cannot create a file when that file already exists"). Under
            # `$ErrorActionPreference = 'Stop'` -- which every caller sets -- that exception discarded
            # the compaction that had already been written, skipped the append below, and left
            # `run-registry.jsonl.<pid>.tmp` behind in stateDir. Same mechanism as
            # `camel-gate-state.ps1#Publish-CamelGateState`, spelled out here because the registry is
            # JSONL and this file is dot-sourced by `camel-remote.ps1`, which does not carry that
            # helper: one rename of a temp file beside the destination (the overwrite overload, i.e.
            # MOVEFILE_REPLACE_EXISTING -- every caller is `pwsh`, so it exists), retried across the
            # milliseconds a reader holds the file. If it cannot land, the compaction is dropped and
            # the caller is told `$false`, never an exception.
            $published = $false
            for ($attempt = 0; $attempt -lt 25; $attempt++) {
                try {
                    [System.IO.File]::Move($tmp, $Path, $true)
                    $published = $true
                    break
                } catch {
                    Start-Sleep -Milliseconds 40
                }
            }
            if (-not $published) {
                Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
                return $false
            }
        }
        $line = ($Row | ConvertTo-Json -Compress -Depth 6)
        [System.IO.File]::AppendAllText($Path, $line + "`n", $encoding)
        return $true
    } catch {
        # Advisory state only: never let the monitor break the work it monitors.
        return $false
    } finally {
        if ($held) { $Mutex.ReleaseMutex() }
    }
}