# The one write `camel-build.ps1` and `camel-test.ps1` make to their state files, as three readings:
# the document lands, a reader holding the file open costs one update and nothing else, and a reader
# beside the publisher never sees half a document.
#
#   pwsh -NoProfile -File internal/tools/camel-gate-state.test.ps1
#
# Local only: no build, no plan, no checkout touched. Everything happens under %TEMP%.

[CmdletBinding()]
param(
    # Keep the fixture directory for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'camel-gate-state.ps1')

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

$root = Join-Path ([IO.Path]::GetTempPath()) "camel-gate-state-test-$PID"
Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $root -Force | Out-Null
$state = Join-Path $root 'build-state.json'

try {
    # --- the document lands, and lands whole -------------------------------------------------
    $ok = Publish-CamelGateState -Path $state -Json ('{"v":1,"pad":"' + ('a' * 4096) + '"}')
    Check 'a path that does not exist yet is created' ($ok -and (Test-Path -LiteralPath $state)) "returned $ok"
    $doc = $null
    try { $doc = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json } catch { }
    Check 'the file holds the published document' ($null -ne $doc -and $doc.v -eq 1)
    Check 'and no temp file is left beside it' (-not (Test-Path -LiteralPath "$state.$PID.tmp"))

    $ok = Publish-CamelGateState -Path $state -Json '{"v":2}'
    Check 'an existing file is replaced' ($ok -and (Get-Content -LiteralPath $state -Raw | ConvertFrom-Json).v -eq 2)

    # --- a reader holding the file open costs this one update, not the run --------------------
    $before = Get-Content -LiteralPath $state -Raw
    $reader = [System.IO.File]::Open(
        $state, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $ok = Publish-CamelGateState -Path $state -Json '{"v":3}' -Attempts 3 -RetryDelayMs 20
        $sw.Stop()
        Check 'a destination a reader holds open is reported, not thrown' (-not $ok) "returned $ok"
        Check 'the reader still has the complete old document' ((Get-Content -LiteralPath $state -Raw) -eq $before)
        Check 'and the temp file was cleaned up' (-not (Test-Path -LiteralPath "$state.$PID.tmp"))
        Check 'the publisher retried before giving up' ($sw.ElapsedMilliseconds -ge 35) "$($sw.ElapsedMilliseconds) ms"
    } finally {
        $reader.Dispose()
    }

    $ok = Publish-CamelGateState -Path $state -Json '{"v":4}'
    Check 'the next publish lands once the reader has gone' (
        $ok -and (Get-Content -LiteralPath $state -Raw | ConvertFrom-Json).v -eq 4)

    # --- a reader running beside the publisher never sees a half-written document -------------
    $a = '{"v":"a","pad":"' + ('a' * 32768) + '"}'
    $b = '{"v":"b","pad":"' + ('b' * 32768) + '"}'
    $null = Publish-CamelGateState -Path $state -Json $a
    $readerJob = Start-Job -ScriptBlock {
        param($Path, $Seconds)
        $reads = 0
        $torn = 0
        $other = 0
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
            # A reader that cannot open the file this instant (the publisher's rename is holding it
            # for a moment) is not a torn read; it simply reads again. Torn means the text that came
            # back was not a whole document.
            $text = $null
            try { $text = [System.IO.File]::ReadAllText($Path) } catch { }
            if ($null -ne $text) {
                try {
                    $doc = $text | ConvertFrom-Json
                    if ($doc.v -ne 'a' -and $doc.v -ne 'b') { $other++ }
                } catch {
                    $torn++
                }
                $reads++
            }
        }
        [pscustomobject]@{ reads = $reads; torn = $torn; other = $other }
    } -ArgumentList $state, 2

    $published = 0
    $toggle = $false
    while ($readerJob.State -in @('Running', 'NotStarted')) {
        $toggle = -not $toggle
        if (Publish-CamelGateState -Path $state -Json $(if ($toggle) { $a } else { $b })) { $published++ }
    }
    $reading = Receive-Job -Job $readerJob -Wait
    Remove-Job -Job $readerJob

    Check 'the publisher kept writing while a reader ran beside it' ($published -gt 10) "published $published"
    Check 'the reader read the document many times' ($reading.reads -gt 100) "read $($reading.reads)"
    Check 'no read saw a half-written or unparsable document' ($reading.torn -eq 0) "torn $($reading.torn) of $($reading.reads)"
    Check 'and every read was one of the two documents published' ($reading.other -eq 0) "other $($reading.other)"
} finally {
    if (-not $Keep) { Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host ''
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }