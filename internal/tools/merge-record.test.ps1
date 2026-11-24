# The two ends of the hole this pair closes: a gate that refuses a push and not a commit.
#
# A merge made in the checkout that had `develop` checked out is a commit on the local branch. When a
# gate refused the push, the commit stayed, and the next push of anything -- one backlog row -- carried
# it, so the merge became public having passed nothing. The repair is in two parts and this test locks
# both of them down, on a scratch repository and a scratch remote, with no host, no build and no plan
# run:
#
#   1. the merge is made in a throwaway checkout detached at the commit it lands on, the push sends
#      the merge commit itself (`<sha>:refs/heads/develop`), and `develop` moves only after the push
#      returned 0. A refusal therefore leaves the branch where it was: there is nothing to clean up.
#   2. the pre-push hook refuses a push whose first-parent range carries a merge commit with no pass
#      record, and names that commit. The record is written by the merge tool after the gates, before
#      the push -- a record (`scripts/merge-record.mjs`) whose parents it checks against git.
#
#   pwsh -NoProfile -File internal/tools/merge-record.test.ps1 [-Keep]
#
# The hook is exercised through `core.hooksPath`, which is how husky installs it, so the file under
# test is the one that runs on a real push -- not a paraphrase of it.

[CmdletBinding()]
param(
    # Keep the scratch repository for inspection.
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

function Invoke-Git {
    param([string]$Where, [string[]]$ArgList)
    $out = & git -C $Where @ArgList 2>&1
    return @{ Exit = $LASTEXITCODE; Text = (($out | ForEach-Object { "$_" }) -join "`n") }
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-record-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
$origin = Join-Path $scratch 'origin.git'
$work = Join-Path $scratch 'work'
$tree = Join-Path $scratch 'throwaway'
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

Write-Host "scratch: $scratch"

# --- the scratch repository --------------------------------------------------
# The hook, the record writer and the settings it reads are copied from this checkout, so the test
# reads the files that ship and not a stand-in for them.
$null = & git init --quiet --bare --initial-branch=develop $origin 2>&1
Check 'a scratch remote exists' (Test-Path (Join-Path $origin 'HEAD'))

$null = & git clone --quiet $origin $work 2>&1
& git -C $work checkout --quiet -B develop 2>&1 | Out-Null
& git -C $work config user.email 'test@example.invalid'
& git -C $work config user.name 'merge record test'
& git -C $work config commit.gpgsign false

foreach ($rel in @('scripts/hooks/pre-push', 'scripts/merge-record.mjs')) {
    $to = Join-Path $work $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot $rel) -Destination $to -Force
}
# `mergeWorktree` names the tree that may push develop: here, the scratch clone. Written rather than
# copied, because the value is this machine's scratch path.
New-Item -ItemType Directory -Force -Path (Join-Path $work 'internal/tools') | Out-Null
[System.IO.File]::WriteAllText((Join-Path $work 'internal/tools/camel-gate.local.json'),
    ((@{ sshHost = 'none'; mergeWorktree = $work } | ConvertTo-Json) + "`n"),
    (New-Object System.Text.UTF8Encoding($false)))
Set-Content -LiteralPath (Join-Path $work 'README.md') -Value 'scratch'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'scratch tree' 2>&1 | Out-Null
$base = (& git -C $work rev-parse HEAD).Trim()
& git -C $work push --quiet origin develop 2>&1 | Out-Null
Check 'the scratch remote starts at the first commit' ($LASTEXITCODE -eq 0)

# husky installs the hook by pointing `core.hooksPath` at a directory; the file it puts there calls
# `scripts/hooks/pre-push`. That call is the one under test, so the wrapper is the same shape.
$hooks = Join-Path $scratch 'hooks'
New-Item -ItemType Directory -Force -Path $hooks | Out-Null
[System.IO.File]::WriteAllText((Join-Path $hooks 'pre-push'),
    "#!/bin/sh`ntop=`$(git rev-parse --show-toplevel 2>/dev/null) || exit 0`nexec sh `"`$top/scripts/hooks/pre-push`" `"`$@`"`n",
    (New-Object System.Text.UTF8Encoding($false)))
& git -C $work config core.hooksPath $hooks

# --- a lane, and a merge of it -----------------------------------------------
& git -C $work checkout --quiet -B 'lane/scratch' 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'lane.txt') -Value 'from the lane'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'lane: a change' 2>&1 | Out-Null
$laneHead = (& git -C $work rev-parse HEAD).Trim()
& git -C $work checkout --quiet develop 2>&1 | Out-Null

# --- 1. the hole: a refused merge survives on develop and the next push carries it --------------
# This is the shape that shipped merges nobody read: merge on develop, gate says no, then push
# something else.
& git -C $work merge --quiet --no-ff 'lane/scratch' -m 'merge scratch lane' 2>&1 | Out-Null
$mergeOld = (& git -C $work rev-parse HEAD).Trim()
Set-Content -LiteralPath (Join-Path $work 'backlog.md') -Value 'one row'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'backlog: one row' 2>&1 | Out-Null

$push = Invoke-Git $work @('push', 'origin', 'develop')
$shortMerge = $mergeOld.Substring(0, 9)
Check 'a push carrying an unrecorded merge is refused' ($push.Exit -ne 0) $push.Text
Check 'the refusal names the merge commit' ($push.Text -match [regex]::Escape($shortMerge)) $push.Text
Check 'the remote did not move' (((& git -C $origin rev-parse develop)).Trim() -eq $base)

# --- 2. the order that removes the need to clean up ---------------------------------------------
# Merge in a throwaway checkout detached at the commit it lands on, push the commit itself, and move
# develop only after the push returned 0 -- the merge tool's order, step for step, without the gates
# (which need the build host). The property under test is what is left when the push is refused.
& git -C $work reset --quiet --hard $base 2>&1 | Out-Null
$null = & git -C $work worktree add --quiet --detach $tree $base 2>&1
Check 'the throwaway checkout is detached' (((& git -C $tree rev-parse --abbrev-ref HEAD)).Trim() -eq 'HEAD')

& git -C $tree merge --quiet --no-ff $laneHead -m 'merge scratch lane, in a throwaway tree' 2>&1 | Out-Null
$mergeNew = (& git -C $tree rev-parse HEAD).Trim()
$parents = @(((& git -C $tree rev-list --parents -n 1 HEAD).Trim() -split ' ') | Where-Object { $_ })
Check 'the merge has two parents' ($parents.Count -eq 3) ($parents -join ' ')
Check 'its first parent is the commit it lands on' ($parents[1] -eq $base) ($parents[1])

$push = Invoke-Git $work @('push', 'origin', "$($mergeNew):refs/heads/develop")
Check 'the refused push names the merge it would have published' ($push.Text -match [regex]::Escape($mergeNew.Substring(0, 9))) $push.Text
$after = (& git -C $work rev-parse 'refs/heads/develop').Trim()
Check 'local develop is where it was: nothing to clean up' ($after -eq $base) "develop is $($after.Substring(0, 9)), expected $($base.Substring(0, 9))"
Check 'and the remote is where it was' (((& git -C $origin rev-parse develop)).Trim() -eq $base)

# --- 3. a record cannot disagree with git -------------------------------------------------------
$passedDir = Join-Path $work '.git/camel-passed'
$diffFile = Join-Path $scratch 'diff.json'
$diff = @{
    planSet  = @{ names = @('test/plans/smoke/core.plan.toml'); hash = '1-abc' }
    baseline = @{ sha = $base; file = 'x.jsonl'; cases = 12; normal = 1 }
    fixed    = @('a_case')
    new      = @()
    changed  = @()
}
[System.IO.File]::WriteAllText($diffFile, (($diff | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))

$record = @{
    diff = $diffFile; merge = $mergeNew; firstParent = $laneHead; secondParent = $laneHead
    branch = 'lane/scratch'; tree = $tree; out = $passedDir
}
# The same command twice: once naming the lane head as the merge's first parent, which git answers,
# and once naming the commit it really landed on.
function Write-RecordArgs {
    param([string]$FirstParent, [string]$Diff)
    return @((Join-Path $work 'scripts/merge-record.mjs'), 'write',
        '--diff', $Diff, '--merge', $record.merge, '--first-parent', $FirstParent,
        '--second-parent', $record.secondParent, '--branch', $record.branch, '--tree', $record.tree,
        '--out', $record.out, '--gate', 'rows')
}
$bad = @(& node @(Write-RecordArgs -FirstParent $laneHead -Diff $diffFile) 2>&1)
$badExit = $LASTEXITCODE
Check 'a record whose first parent disagrees with git is refused' ($badExit -eq 1) (($bad | ForEach-Object { "$_" }) -join ' / ')
Check 'and nothing was written' (-not (Test-Path (Join-Path $passedDir "$mergeNew.json")))

# A record whose reading is not a pass must not be writable either: the tool refuses such a merge
# before it reaches this call, and a caller that arrives anyway must not be able to certify it.
$diffFailed = @{
    planSet  = @{ names = @('test/plans/smoke/core.plan.toml'); hash = '1-abc' }
    baseline = @{ sha = $base; file = 'x.jsonl'; cases = 12; normal = 1 }
    fixed    = @()
    new      = @(@{ name = 'x'; reading = 'boom' })
    changed  = @()
}
$failedFile = Join-Path $scratch 'diff-failed.json'
[System.IO.File]::WriteAllText($failedFile, (($diffFailed | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$out2 = @(& node @(Write-RecordArgs -FirstParent $base -Diff $failedFile) 2>&1)
Check 'a record with a newly red case is refused' ($LASTEXITCODE -eq 1) (($out2 | ForEach-Object { "$_" }) -join ' / ')

# --- 4. with the record, the same push is allowed -----------------------------------------------
$good = @(& node @(Write-RecordArgs -FirstParent $base -Diff $diffFile) 2>&1)
$goodExit = $LASTEXITCODE
Check 'the record is written' ($goodExit -eq 0 -and (Test-Path (Join-Path $passedDir "$mergeNew.json"))) (($good | ForEach-Object { "$_" }) -join ' / ')
$saved = Get-Content -LiteralPath (Join-Path $passedDir "$mergeNew.json") -Raw | ConvertFrom-Json
Check 'it names both commits' ($saved.sha -eq $mergeNew -and $saved.firstParent -eq $base)
Check 'it carries the plan set and its hash' ($saved.planSet.count -eq 1 -and $saved.planSet.hash -eq '1-abc')
Check 'it carries the baseline and its source' ($saved.baseline.sha -eq $base -and $saved.baseline.source)
Check 'it carries the three counts' ($saved.readings.fixed -eq 1 -and $saved.readings.new -eq 0 -and $saved.readings.changed -eq 0)

$push = Invoke-Git $work @('push', 'origin', "$($mergeNew):refs/heads/develop")
Check 'the same push is allowed once the record exists' ($push.Exit -eq 0) $push.Text
Check 'the remote is the merge commit' (((& git -C $origin rev-parse develop)).Trim() -eq $mergeNew)

# --- 5. a plain commit after a recorded merge needs no record ------------------------------------
& git -C $work reset --quiet --hard $mergeNew 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'backlog.md') -Value 'another row'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'backlog: another row' 2>&1 | Out-Null
$push = Invoke-Git $work @('push', 'origin', 'develop')
Check 'a backlog row on top of a recorded merge is allowed' ($push.Exit -eq 0) $push.Text

# --- 6. a record that is not a pass is not a pass -----------------------------------------------
# The hook reads the record too, not only its existence: a hand-written one with a red case in it
# must not open the door the writer refuses to open.
& git -C $work checkout --quiet -B 'lane/second' $base 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'second.txt') -Value 'second'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'lane: second change' 2>&1 | Out-Null
& git -C $work checkout --quiet develop 2>&1 | Out-Null
& git -C $work merge --quiet --no-ff 'lane/second' -m 'merge second lane' 2>&1 | Out-Null
$second = (& git -C $work rev-parse HEAD).Trim()
$forged = @{
    sha = $second; firstParent = (& git -C $work rev-parse 'HEAD^1').Trim(); secondParent = 'x'
    planSet = @{ names = @('p'); hash = 'h'; count = 1 }
    baseline = @{ sha = 'x'; source = 'forged' }
    readings = @{ cases = 1; fixed = 0; new = 2; changed = 0 }
}
New-Item -ItemType Directory -Force -Path $passedDir | Out-Null
[System.IO.File]::WriteAllText((Join-Path $passedDir "$second.json"), (($forged | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$push = Invoke-Git $work @('push', 'origin', 'develop')
Check 'a record with a red case in it does not open the door' ($push.Exit -ne 0) $push.Text
Check 'and the refusal says the record is not a pass' ($push.Text -match 'not a pass') $push.Text

# --- result ------------------------------------------------------------------
& git -C $work worktree remove --force $tree 2>&1 | Out-Null
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0