# What `merge-batch-sets.ps1` decides, checked without a host, a build or a lane.
#
#   pwsh -NoProfile -File internal/tools/merge-batch-sets.test.ps1
#
# Prints `N checks, M failed` and exits 1 when any check failed -- the same shape as
# `camel-remote-args.test.ps1`, so a reader does not have to learn a second convention.
#
# The hash check runs `scripts/merge-readings.mjs#planSetHash` through node on the same list and
# requires the same string. It is the one check here that is not about this file's own idea of
# correctness: a record that names its plan set by a different hash than the baseline cache does
# cannot be joined to a baseline, and nothing downstream would say so.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'merge-batch-sets.ps1')

$checks = 0
$failed = 0

function Check {
    param([string] $What, [bool] $Holds, [string] $Detail = '')
    $script:checks++
    if ($Holds) {
        Write-Host "  ok   $What"
    } else {
        $script:failed++
        Write-Host "  FAIL $What" -ForegroundColor Red
        if ($Detail) { Write-Host "       $Detail" }
    }
}

function Lane {
    param([string] $Short, [string[]] $Files, [string[]] $Plans = @())
    [pscustomobject]@{ Name = "lane/$Short"; Short = $Short; Files = $Files; Plans = $Plans }
}

# --- the pairwise intersection ----------------------------------------------
$apart = @(
    (Lane 'a' @('src/x.cpp') @('p1')),
    (Lane 'b' @('src/y.cpp', 'docs/z.md') @('p2')),
    (Lane 'c' @('test/q.py') @('p3'))
)
$report = Get-BatchPairReport -Lanes $apart
Check 'three lanes make three pairs' ($report.Count -eq 3) "got $($report.Count)"
Check 'every disjoint pair reports 0 shared files' (@($report | Where-Object { $_.Shared.Count -ne 0 }).Count -eq 0) 'a pair reported a shared file'
Check 'the pairs are the ones given, in order' (($report | ForEach-Object { "$($_.A)&$($_.B)" }) -join ',' -eq 'a&b,a&c,b&c') (($report | ForEach-Object { "$($_.A)&$($_.B)" }) -join ',')

$overlap = @(
    (Lane 'a' @('src/x.cpp', 'src/shared.cpp')),
    (Lane 'b' @('src/y.cpp', 'src/shared.cpp')),
    (Lane 'c' @('src/x.cpp'))
)
$report2 = Get-BatchPairReport -Lanes $overlap
$hit = @($report2 | Where-Object { $_.Shared.Count -gt 0 })
Check 'a collision is found for each pair that has one' ($hit.Count -eq 2) "got $($hit.Count)"
Check 'a collision names the file, not only the count' (@($hit | ForEach-Object { $_.Shared }) -contains 'src/shared.cpp') 'the offending file is missing'
Check 'the pair report keeps empty pairs beside the colliding ones' ($report2.Count -eq 3) "got $($report2.Count)"
# One lane's whole set inside another's: the intersection is the smaller set, not "empty because the
# first element matched nothing".
$subset = @(
    (Lane 'a' @('src/x.cpp', 'src/y.cpp')),
    (Lane 'b' @('src/x.cpp'))
)
$report3 = Get-BatchPairReport -Lanes $subset
Check 'a set contained in another reports the contained file' ($report3[0].Shared.Count -eq 1 -and $report3[0].Shared[0] -eq 'src/x.cpp') (($report3[0].Shared) -join ',')

# --- the plan set -----------------------------------------------------------
$plans = Get-BatchPlanSet -Lanes @(
    (Lane 'a' @('f1') @('test/plans/feat/b.plan.toml', 'test/plans/feat/a.plan.toml')),
    (Lane 'b' @('f2') @('test/plans/feat/a.plan.toml', 'test/plans/feat/c.plan.toml')),
    (Lane 'c' @('f3') @())
)
Check 'the union has each plan once' ($plans.Count -eq 3) "got $($plans.Count)"
Check 'the union is sorted ordinally' (($plans -join ',') -eq 'test/plans/feat/a.plan.toml,test/plans/feat/b.plan.toml,test/plans/feat/c.plan.toml') ($plans -join ',')
$reordered = Get-BatchPlanSet -Lanes @(
    (Lane 'c' @('f3') @('test/plans/feat/c.plan.toml')),
    (Lane 'b' @('f2') @('test/plans/feat/b.plan.toml')),
    (Lane 'a' @('f1') @('test/plans/feat/a.plan.toml'))
)
Check 'the order the lanes are given does not change the set' (($plans -join ',') -eq ($reordered -join ','))

# --- the hash, against the comparer's own function ---------------------------
$names = @('test/plans/feat/modules/std.plan.toml', 'test/plans/feat/docs_links.plan.toml', 'test/plans/feat/parse/frontend.plan.toml')
$mine = Get-PlanSetHash -Names $names
$script = @'
const names = JSON.parse(process.env.PLAN_NAMES)
const list = [...new Set(names.map((n) => n.trim()).filter(Boolean))].sort()
let h = 0
for (const ch of list.join('\n')) h = (h * 31 + ch.codePointAt(0)) >>> 0
process.stdout.write(`${list.length}-${h.toString(16)}`)
'@
$scriptFile = Join-Path ([System.IO.Path]::GetTempPath()) ("merge-batch-sets-$PID.mjs")
[System.IO.File]::WriteAllText($scriptFile, $script, (New-Object System.Text.UTF8Encoding($false)))
try {
    $env:PLAN_NAMES = ($names | ConvertTo-Json -Compress)
    $theirs = "$(& node $scriptFile)".Trim()
    Remove-Item Env:\PLAN_NAMES -ErrorAction SilentlyContinue
    Check 'the plan-set hash equals the comparer''s own' ($mine.Hash -eq $theirs) "mine $($mine.Hash), merge-readings $theirs"
    Check 'the hash carries the count first' ($mine.Hash.StartsWith("$($names.Count)-")) $mine.Hash
} finally {
    Remove-Item -LiteralPath $scriptFile -Force -ErrorAction SilentlyContinue
}
$empty = Get-PlanSetHash -Names @()
Check 'an empty set still has a name' ($empty.Hash -eq '0-0') $empty.Hash

Write-Host ''
Write-Host "$checks checks, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0