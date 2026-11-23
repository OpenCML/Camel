# The start point `merge-lane.ps1` asserts before it merges, and the two shapes it can meet there.
#
# Why it exists: local `develop` was ahead of `origin/develop` six times in one day, each time by a
# merge this tool had made and could not push. The next merge then had a first parent origin did not
# have, the push was refused with a message about something else, and the merge commit stayed on the
# local branch -- so the run after that started from a point that was wrong by one more merge.
#
# What is checked here is the guard and only the guard: a promise is repaired when it holds nothing
# but such merges, and it is left alone when it holds a commit a person made. Both runs are made in a
# throwaway repository with a bare origin beside it, and both stop at or just after the guard --
# `-Branch lane/absent` is deliberate, because the guard runs before the lane name is resolved and
# that keeps this test away from the host, the build and the gates.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-start.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-start.test.ps1 -Keep   (leave the fixture)
#
# Everything here is local: no host, no build, no checkout of this repository is touched.

[CmdletBinding()]
param(
    # Keep the fixture tree for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

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

# git, with its output as text and a refusal that names the command: a fixture that half-built is a
# test that passes for the wrong reason.
function Invoke-Git {
    param([string] $Repo, [Parameter(ValueFromRemainingArguments = $true)][string[]] $Args)
    $out = & git -C $Repo @Args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($Args -join ' ') failed in $Repo`: $($out -join ' ')" }
    @($out | ForEach-Object { "$_".Trim() })
}

function Get-GitText {
    param([string] $Repo, [Parameter(ValueFromRemainingArguments = $true)][string[]] $Args)
    ((Invoke-Git $Repo @Args) -join "`n").Trim()
}

function Write-FixtureFile {
    param([string] $Repo, [string] $Name, [string] $Text)
    [System.IO.File]::WriteAllText((Join-Path $Repo $Name), "$Text`n", (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-MergeLane {
    param([string] $Branch)
    $scriptPath = Join-Path $PSScriptRoot 'merge-lane.ps1'
    $out = & pwsh -NoProfile -File $scriptPath -Branch $Branch -Message 'test: the start point' `
        -MainCheckout $work -BaselineWorktree (Join-Path $scratch 'baseline') -NoPush 2>&1
    $code = $LASTEXITCODE
    return [pscustomobject]@{
        Code = $code
        Text = (@($out | ForEach-Object { "$_" }) -join "`n")
    }
}

# --- the fixture -------------------------------------------------------------
# One file on develop, one lane that adds a second file. Nothing is built and nothing is measured:
# the guard reads refs, so a repository of two files carries everything it reads.
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('merge-lane-start-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$work = Join-Path $scratch 'work'
$origin = Join-Path $scratch 'origin.git'
New-Item -ItemType Directory -Path $work -Force | Out-Null
& git init --quiet --bare $origin
if ($LASTEXITCODE -ne 0) { throw "git init --bare $origin failed" }
& git init --quiet --initial-branch=develop $work
if ($LASTEXITCODE -ne 0) { throw "git init $work failed" }
& git -C $work config user.name 'merge-lane start test'
& git -C $work config user.email 'merge-lane-start@example.invalid'
& git -C $work config commit.gpgsign false
Write-FixtureFile -Repo $work -Name 'a.txt' -Text 'a'
Invoke-Git $work add a.txt | Out-Null
Invoke-Git $work commit -q -m 'the one file' | Out-Null
Invoke-Git $work remote add origin $origin | Out-Null
& git -C $work push --quiet -u origin develop 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'pushing develop to the fixture origin failed' }
Invoke-Git $work checkout -q -b lane/probe | Out-Null
Write-FixtureFile -Repo $work -Name 'b.txt' -Text 'b'
Invoke-Git $work add b.txt | Out-Null
Invoke-Git $work commit -q -m 'the lane change' | Out-Null
& git -C $work push --quiet -u origin lane/probe 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'pushing the lane to the fixture origin failed' }
Invoke-Git $work checkout -q develop | Out-Null

# --- 1. a merge this tool made and did not push is repaired -------------------
# The shape the guard exists for: local develop holds one merge commit and the lane commit it pulled
# in, origin has neither, and no gate record exists for the merge.
Invoke-Git $work merge -q --no-ff lane/probe -m 'merge probe: made here and not pushed' | Out-Null
$seat = Get-GitText $work rev-parse develop
$base = Get-GitText $work rev-parse origin/develop
$run = Invoke-MergeLane -Branch 'lane/absent'

Check 'a merge alone above origin is recognised' ($run.Text -match 'local develop is not origin/develop') $run.Text
Check 'the extra commits are printed, and which of them is on the first-parent chain' `
    ($run.Text -match '2 commit\(s\) exist only on local develop' -and $run.Text -match 'came in through a merge; not on the first-parent chain') $run.Text
Check 'the one on the chain is told apart from the lane commit it brought' `
    ($run.Text -match '1 of them on develop.s first-parent chain') $run.Text
Check 'it is repaired rather than refused' ($run.Text -notmatch 'is not all merges this tool made') $run.Text
Check 'the repair names both ends and the ref' ($run.Text -match "restored local develop from $seat to $base, the commits only it held are under (backup/orphan-\S+)") $run.Text

$stranded = @(Invoke-Git $work for-each-ref 'refs/tags/backup/' --format='%(refname) %(objectname)')
Check 'the commits are kept under a tag, one tag' ($stranded.Count -eq 1) ($stranded -join ' / ')
Check 'and the tag holds the commit the branch held' ($stranded.Count -eq 1 -and $stranded[0].EndsWith($seat)) ($stranded -join ' / ')
$reachable = @(Invoke-Git $work log --oneline "origin/develop..$($stranded[0].Split(' ')[0])")
Check 'the merge is still reachable from that tag' ($reachable.Count -eq 2 -and ($reachable -join "`n") -match 'merge probe: made here and not pushed') ($reachable -join ' / ')

Check 'local develop is back on origin/develop' ((Get-GitText $work rev-parse develop) -eq $base) (Get-GitText $work rev-parse develop)

# The run stops right after the repair because `lane/absent` does not resolve; the exit code is the
# lane name's, and what it proves is that the repair happened before any merge was tried.
Check 'the run stopped after the repair, at the lane name' ($run.Code -eq 1 -and $run.Text -match 'STOPPED: branch lane/absent does not resolve to a commit') "exit $($run.Code)"
Check 'and it says the truth about develop: unchanged, equal to origin' `
    ($run.Text -match "Nothing was committed and nothing was pushed: local develop is still $base, which is origin/develop.") $run.Text

# --- 2. a commit a person made is not dropped --------------------------------
# Same branch ahead of origin, but one ordinary commit on it. Nothing here may decide to drop that,
# so the run stops and the branch is left exactly where it was.
$before = Get-GitText $work rev-parse develop
Write-FixtureFile -Repo $work -Name 'c.txt' -Text 'c'
Invoke-Git $work add c.txt | Out-Null
Invoke-Git $work commit -q -m 'a person wrote this, not the merge tool' | Out-Null
$plain = Get-GitText $work rev-parse develop
$run = Invoke-MergeLane -Branch 'lane/probe'

Check 'a commit a person made stops the run with exit 2' ($run.Code -eq 2) "exit $($run.Code)"
Check 'the stop names it and why it is not a leftover' `
    ($run.Text -match 'is not a merge commit \(1 parent\(s\)\); a person made it') $run.Text
Check 'the run says it is not restoring' ($run.Text -match 'Not restoring local develop') $run.Text
Check 'local develop was not touched' ((Get-GitText $work rev-parse develop) -eq $plain) (Get-GitText $work rev-parse develop)
Check 'no second tag was left behind' (@(Invoke-Git $work for-each-ref 'refs/tags/backup/').Count -eq 1) 'refs grew'
Check 'and the sentence is still about the true state' `
    ($run.Text -match "Nothing was committed and nothing was pushed by this run: local develop is $plain and origin/develop is $base") $run.Text

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0