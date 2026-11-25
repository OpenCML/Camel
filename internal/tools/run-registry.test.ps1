# The registry's one write, as two readings: with no reader the compaction lands and the file holds
# the newest lines plus the new row, and while a reader holds `run-registry.jsonl` open the compaction
# is dropped -- reported, never thrown -- with the old file still whole and no temp file left in
# stateDir. The second reading is the defect this file exists for: `Move-Item -Force` reported the held
# destination as ERROR_ALREADY_EXISTS (183) and, under `$ErrorActionPreference = 'Stop'`, that
# exception threw away a compaction already written and left `run-registry.jsonl.<pid>.tmp` behind.
#
#   pwsh -NoProfile -File internal/tools/run-registry.test.ps1
#
# Local only: no build, no plan, no checkout touched. Everything happens under %TEMP%.

[CmdletBinding()]
param(
    # Keep the fixture directory for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'run-registry.ps1')

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

$root = Join-Path ([IO.Path]::GetTempPath()) "camel-run-registry-test-$PID"
Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $root -Force | Out-Null
$registry = Join-Path $root 'run-registry.jsonl'
$encoding = New-Object System.Text.UTF8Encoding($false)
# The test's own mutex: the registry's is `Global\CamelRunRegistry` and a live run may hold it.
$mutex = New-Object System.Threading.Mutex($false, "Local\CamelRunRegistryTest-$PID")

function Write-RegistryRows {
    param([string]$Path, [int]$Count)
    $lines = @(1..$Count | ForEach-Object { '{{"runId":"r{0}","pad":"{1}"}}' -f $_, ('p' * 90) })
    [System.IO.File]::WriteAllLines($Path, [string[]]$lines, $encoding)
}

try {
    # --- a path that does not exist yet ---------------------------------------------------------
    $ok = Add-RunRegistryRow -Path $registry -Mutex $mutex -Row @{ runId = 'first'; phase = 'queued' }
    Check 'a path that does not exist yet is created' ($ok -and (Test-Path -LiteralPath $registry)) "returned $ok"
    $rows = @(Get-Content -LiteralPath $registry)
    Check 'and holds the one row' ($rows.Count -eq 1 -and ($rows[0] | ConvertFrom-Json).runId -eq 'first') "rows $($rows.Count)"

    # --- with no reader: the compaction lands, and the file holds what it should -----------------
    Remove-Item -LiteralPath $registry -Force
    Write-RegistryRows -Path $registry -Count 6
    $ok = Add-RunRegistryRow -Path $registry -Mutex $mutex -Row @{ runId = 'a'; phase = 'build' } `
        -CompactBytes 200 -KeepLines 2
    Check 'a compaction with no reader lands and is reported published' ($ok -eq $true) "returned $ok"
    $rows = @(Get-Content -LiteralPath $registry)
    Check 'the compacted file keeps only the newest lines plus the new row' ($rows.Count -eq 3) "rows $($rows.Count)"
    Check 'the first of them is the newest line it kept' (
        ($rows[0] | ConvertFrom-Json).runId -eq 'r5') "first row $($rows[0])"
    Check 'and the last one is the row just appended' (
        ($rows[2] | ConvertFrom-Json).runId -eq 'a' -and ($rows[2] | ConvertFrom-Json).phase -eq 'build') "last row $($rows[2])"
    Check 'and no temp file is left beside it' (-not (Test-Path -LiteralPath "$registry.$PID.tmp"))

    # --- with a reader holding the file: dropped, not thrown ------------------------------------
    Write-RegistryRows -Path $registry -Count 6
    $before = [System.IO.File]::ReadAllBytes($registry)
    $reader = [System.IO.File]::Open(
        $registry, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $threw = $false
        $sw = [Diagnostics.Stopwatch]::StartNew()
        try {
            $ok = Add-RunRegistryRow -Path $registry -Mutex $mutex -Row @{ runId = 'b'; phase = 'build' } `
                -CompactBytes 200 -KeepLines 2
        } catch {
            $threw = $true
            $ok = $null
        }
        $sw.Stop()
        Check 'a destination a reader holds open is reported, not thrown' ((-not $threw) -and (-not $ok)) "threw $threw, returned $ok"
        Check 'the reader still has the complete old registry' (
            [System.Linq.Enumerable]::SequenceEqual([byte[]]$before, [byte[]][System.IO.File]::ReadAllBytes($registry)))
        Check 'and the temp file was cleaned up' (-not (Test-Path -LiteralPath "$registry.$PID.tmp"))
        Check 'the writer retried before giving up' ($sw.ElapsedMilliseconds -ge 900) "$($sw.ElapsedMilliseconds) ms"
        # The old bytes above are whole because neither the compaction nor the append happened: the
        # drop is one update, exactly as before, and this row is not in the file.
        Check 'and neither the compaction nor the append touched it' (
            -not (@(Get-Content -LiteralPath $registry) -match '"runId":"b"'))
    } finally {
        $reader.Dispose()
    }

    # --- the reader is gone: the next write compacts ---------------------------------------------
    $ok = Add-RunRegistryRow -Path $registry -Mutex $mutex -Row @{ runId = 'c'; phase = 'build' } `
        -CompactBytes 200 -KeepLines 2
    $rows = @(Get-Content -LiteralPath $registry)
    Check 'the next write compacts once the reader has gone' (
        $ok -eq $true -and $rows.Count -eq 3 -and ($rows[2] | ConvertFrom-Json).runId -eq 'c') "returned $ok, rows $($rows.Count)"

    # --- a file under the threshold is appended to, not compacted --------------------------------
    $below = Join-Path $root 'below.jsonl'
    Write-RegistryRows -Path $below -Count 2
    $ok = Add-RunRegistryRow -Path $below -Mutex $mutex -Row @{ runId = 'd' }
    $rows = @(Get-Content -LiteralPath $below)
    Check 'a file under the threshold is appended to, not compacted' (
        $ok -eq $true -and $rows.Count -eq 3 -and ($rows[0] | ConvertFrom-Json).runId -eq 'r1') "returned $ok, rows $($rows.Count)"
} finally {
    $mutex.Dispose()
    if (-not $Keep) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host ''
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }