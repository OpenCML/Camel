# What a `-Plan` target names, as one sentence printed before the push.
#
# The two readings this pins, both taken by hand on 2026-11-24 (`internal/tools/camel-remote.ps1`):
#
#   * `-Plan <two directories>` was reported as the whole fast layer (2,763 cases / 113 plan files);
#   * `-Plan test/plans` (the whole tree) exited 2 with `--changed: the cases this change selects
#     could not be computed`, a sentence about a flag the caller never gave.
#
# `camel-remote.ps1` now says, in its first plan line, which target it read, how many plan files that
# target reaches, and whether the target is a directory, a plan file, or the whole tree. That line
# is what makes a widened run (`[tier] fast: 387 plan file(s)` for two small directories) readable as
# widened rather than as the directories' own contents.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-targets.test.ps1
#
# Everything here is local: no host, no build, no checkout modified.

[CmdletBinding()]
param(
    # Keep the fixture tree for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'camel-remote-targets.ps1')

$script:failed = 0
$script:checked = 0

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

# --- the fixture -------------------------------------------------------------
# A plan tree with two plan directories, one plan file, and one path that names nothing. The counts
# are small on purpose: the sentence must carry the number the walk produced, not a round one.
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-targets-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
foreach ($dir in 'test/plans/alpha', 'test/plans/beta', 'test/plans/alpha/deep') {
    New-Item -ItemType Directory -Path (Join-Path $scratch $dir) -Force | Out-Null
}
Set-Content -Path (Join-Path $scratch 'test/plans/alpha/one.plan.toml') -Value 'id = "a.one"'
Set-Content -Path (Join-Path $scratch 'test/plans/alpha/two.plan.toml') -Value 'id = "a.two"'
Set-Content -Path (Join-Path $scratch 'test/plans/beta/three.plan.toml') -Value 'id = "b.three"'
Set-Content -Path (Join-Path $scratch 'test/plans/alpha/deep/four.plan.toml') -Value 'id = "a.deep.four"'
# Not a plan: the count is `*.plan.toml`, so this file must not be counted.
Set-Content -Path (Join-Path $scratch 'test/plans/alpha/notes.md') -Value 'not a plan'

# --- 1. a directory target prints what it reaches ----------------------------
# The number in the sentence is the point of the whole file: "2,763 cases of the whole fast layer"
# and "19 plan files under the two directories I named" must not look the same, and the only way to
# tell them apart in the output is a count the reader can compare with `git ls-files`.
$twoDirs = @(Resolve-CamelPlanTarget -Values @('test/plans/alpha', 'test/plans/beta') -Root $scratch)
$line = Format-CamelPlanTargetLine -Targets $twoDirs -Tier ''
Check 'two directories is two records, both directories' ($twoDirs.Count -eq 2 -and @($twoDirs | Where-Object { $_.Kind -eq 'directory' }).Count -eq 2) (($twoDirs | ForEach-Object { "$($_.Kind):$($_.Plans)" }) -join ' ')
Check 'the count is every *.plan.toml under them (4), not a layer' ($line -like '*4 plan file(s)*') $line
Check 'the sentence says the run starts those directories and nothing else' ($line -like '*starts the plans under those directories and nothing else*') $line
Check 'and says it is not a run of the whole plan tree' ($line -like '*not a run of the whole plan tree*') $line
Check 'with no -Tier it names the runner default, so the layer is not hidden' ($line -like '*at tier fast (the runner default)*') $line
Check 'the line is one [camel-remote] line' ($line.StartsWith('[camel-remote] plan target: ') -and -not $line.Contains("`n")) $line

$oneDir = @(Resolve-CamelPlanTarget -Values @('test/plans/alpha') -Root $scratch)
$line = Format-CamelPlanTargetLine -Targets $oneDir -Tier ''
Check 'one directory is singular in the sentence and counts its own plans (3)' ($line -like '*you named 1 directory*' -and $line -like '*3 plan file(s)*' -and $line -like '*plans under it*') $line
Check 'a named -Tier replaces the default in the sentence' ((Format-CamelPlanTargetLine -Targets $oneDir -Tier 'slow') -like '*at tier slow*') (Format-CamelPlanTargetLine -Targets $oneDir -Tier 'slow')

# --- 2. a plan file is not read at a layer -----------------------------------
$fileTarget = @(Resolve-CamelPlanTarget -Values @('test/plans/alpha/two.plan.toml') -Root $scratch)
$line = Format-CamelPlanTargetLine -Targets $fileTarget -Tier 'slow'
Check 'a plan file is one plan file' ($fileTarget.Count -eq 1 -and $fileTarget[0].Kind -eq 'file' -and $fileTarget[0].Plans -eq 1) "$($fileTarget[0].Kind):$($fileTarget[0].Plans)"
Check 'and the sentence says it runs in full whatever -Tier says' ($line -like '*runs in full, whatever -Tier says*') $line

# --- 3. a path that names nothing --------------------------------------------
$gone = @(Resolve-CamelPlanTarget -Values @('test/plans/nothing-here') -Root $scratch)
$line = Format-CamelPlanTargetLine -Targets $gone -Tier ''
Check 'a path that names nothing says so, and says the runner will refuse it' ($gone[0].Kind -eq 'missing' -and $line -like '*names nothing in this tree*') $line

# --- 4. `all` and the plans root are the whole tree --------------------------
$treeTargets = @(Resolve-CamelPlanTarget -Values @('all') -Root $scratch)
Check 'the all shorthand is the plan tree' ($treeTargets.Count -eq 1 -and $treeTargets[0].Kind -eq 'tree') "$($treeTargets[0].Kind)"
$line = Format-CamelPlanTargetLine -Targets $treeTargets -Tier 'fast'
Check 'the tree is a layer question, not a directory subset' ($line -like '*whole plan tree (4 plan file(s))*' -and $line -like '*layer question*' -and $line -like '*at tier fast*') $line

# --- 5. nothing named is no line --------------------------------------------
Check 'no target means no sentence' ((Format-CamelPlanTargetLine -Targets @() -Tier '') -eq $null) 'a null line is printed as nothing'

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0