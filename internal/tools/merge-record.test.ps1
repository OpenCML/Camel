# The two ends of the hole this pair closes: a gate that refuses a push and not a commit.
#
# A merge made in a checkout that had `integration` checked out is a commit on the local branch. When
# a gate refused the push, the commit stayed, and the next push of anything -- one backlog row --
# carried it, so the merge became public having passed nothing. The repair is in two parts and this
# test locks both of them down, on a scratch repository and a scratch remote, with no host, no build
# and no plan run:
#
#   1. the merge is made in a throwaway checkout detached at the commit it lands on, the push sends
#      the merge commit itself (`<sha>:refs/heads/integration`), and `integration` moves only after
#      the push returned 0. A refusal therefore leaves the branch where it was: nothing to clean up.
#   2. the pre-push hook refuses a push whose first-parent range carries a merge commit with no pass
#      record, and names that commit. The record is written by the merge tool after the gates, before
#      the push -- a record (`scripts/merge-record.mjs`) whose parents it checks against git.
#
# The pushes here go to `integration`, which is where merges arrive and therefore where the per-merge
# reading is asked for. A push to `develop` publishes a *tip* and is checked against the promotion
# record instead; that is `test/tools/promotion-record.test.mjs`.
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
$null = & git init --quiet --bare --initial-branch=integration $origin 2>&1
Check 'a scratch remote exists' (Test-Path (Join-Path $origin 'HEAD'))

$null = & git clone --quiet $origin $work 2>&1
& git -C $work checkout --quiet -B integration 2>&1 | Out-Null
& git -C $work config user.email 'test@example.invalid'
& git -C $work config user.name 'merge record test'
& git -C $work config commit.gpgsign false

foreach ($rel in @('scripts/hooks/pre-push', 'scripts/merge-record.mjs', 'scripts/plan-set.mjs')) {
    $to = Join-Path $work $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
    Copy-Item -LiteralPath (Join-Path $repoRoot $rel) -Destination $to -Force
}
# `mergeWorktree` names the tree that may push develop: the rule is not what this file measures, and
# it is written so that a develop push from here would be answerable; the pushes below all go to
# `integration`, which carries no tree rule.
New-Item -ItemType Directory -Force -Path (Join-Path $work 'internal/tools') | Out-Null
[System.IO.File]::WriteAllText((Join-Path $work 'internal/tools/camel-gate.local.json'),
    ((@{ sshHost = 'none'; mergeWorktree = $work } | ConvertTo-Json) + "`n"),
    (New-Object System.Text.UTF8Encoding($false)))
Set-Content -LiteralPath (Join-Path $work 'README.md') -Value 'scratch'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'scratch tree' 2>&1 | Out-Null
$base = (& git -C $work rev-parse HEAD).Trim()
& git -C $work push --quiet origin integration 2>&1 | Out-Null
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
& git -C $work checkout --quiet integration 2>&1 | Out-Null

# --- 1. the hole: a merge with no record is refused by name -------------------------------------
# This is the shape that shipped merges nobody read: merge, the gate says no, the merge stays on the
# local branch and something pushes it later. Here the merge itself is the commit being pushed, which
# is what the merge tool does, so a refusal has to name it and leave the remote where it was.
& git -C $work merge --quiet --no-ff 'lane/scratch' -m 'merge scratch lane' 2>&1 | Out-Null
$mergeOld = (& git -C $work rev-parse HEAD).Trim()
Set-Content -LiteralPath (Join-Path $work 'backlog.md') -Value 'one row'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'backlog: one row' 2>&1 | Out-Null

$push = Invoke-Git $work @('push', 'origin', "$($mergeOld):refs/heads/integration")
$shortMerge = $mergeOld.Substring(0, 9)
Check 'a push carrying an unrecorded merge is refused' ($push.Exit -ne 0) $push.Text
Check 'the refusal names the merge commit' ($push.Text -match [regex]::Escape($shortMerge)) $push.Text
Check 'and the refusal is the record rule, not the arrival shape' ($push.Text -match 'no reading that holds') $push.Text
Check 'the remote did not move' (((& git -C $origin rev-parse integration)).Trim() -eq $base)

# --- 2. the order that removes the need to clean up ---------------------------------------------
# Merge in a throwaway checkout detached at the commit it lands on, push the commit itself, and move
# `integration` only after the push returned 0 -- the merge tool's order, step for step, without the
# gates (which need the build host). The property under test is what is left when the push is refused.
& git -C $work reset --quiet --hard $base 2>&1 | Out-Null
$null = & git -C $work worktree add --quiet --detach $tree $base 2>&1
Check 'the throwaway checkout is detached' (((& git -C $tree rev-parse --abbrev-ref HEAD)).Trim() -eq 'HEAD')

& git -C $tree merge --quiet --no-ff $laneHead -m 'merge scratch lane, in a throwaway tree' 2>&1 | Out-Null
$mergeNew = (& git -C $tree rev-parse HEAD).Trim()
$parents = @(((& git -C $tree rev-list --parents -n 1 HEAD).Trim() -split ' ') | Where-Object { $_ })
Check 'the merge has two parents' ($parents.Count -eq 3) ($parents -join ' ')
Check 'its first parent is the commit it lands on' ($parents[1] -eq $base) ($parents[1])

$push = Invoke-Git $work @('push', 'origin', "$($mergeNew):refs/heads/integration")
Check 'the refused push names the merge it would have published' ($push.Text -match [regex]::Escape($mergeNew.Substring(0, 9))) $push.Text
$after = (& git -C $work rev-parse 'refs/heads/integration').Trim()
Check 'local integration is where it was: nothing to clean up' ($after -eq $base) "integration is $($after.Substring(0, 9)), expected $($base.Substring(0, 9))"
Check 'and the remote is where it was' (((& git -C $origin rev-parse integration)).Trim() -eq $base)

# --- 3. a record cannot disagree with git -------------------------------------------------------
$passedDir = Join-Path $work '.git/camel-passed'
$diffFile = Join-Path $scratch 'diff.json'

# The plan set's hash in the fixtures below is the value the shipped `scripts/plan-set.mjs` gives,
# not a string that happens to match: the writer is checked against that function, so a fixture
# carrying a hand-written hash would be testing a number against itself. The value is pinned as well
# as computed, because the same hash is a stored baseline cache key in `merge-readings.mjs` -- an
# algorithm that moved would re-key every baseline on every machine, silently, and that is a thing a
# test should say out loud rather than permit.
$planName = 'test/plans/smoke/core.plan.toml'
$planSetModule = 'file:///' + ((Join-Path $repoRoot 'scripts/plan-set.mjs') -replace '\\', '/')
$planHash = (& node -e "import('$planSetModule').then((m) => console.log(m.planSetHash(['$planName'])))" 2>&1 | Out-String).Trim()
Check 'the shipped plan-set hash is the pinned one' ($planHash -eq '1-fead0032') "[$planHash]"
# A hash that does not describe the names: the pair a hand-written or half-updated document holds.
$wrongPlanHash = "${planHash}x"

$diff = @{
    planSet  = @{ names = @($planName); hash = $planHash }
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
    planSet  = @{ names = @($planName); hash = $planHash }
    baseline = @{ sha = $base; file = 'x.jsonl'; cases = 12; normal = 1 }
    fixed    = @()
    new      = @(@{ name = 'x'; reading = 'boom' })
    changed  = @()
}
$failedFile = Join-Path $scratch 'diff-failed.json'
[System.IO.File]::WriteAllText($failedFile, (($diffFailed | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$out2 = @(& node @(Write-RecordArgs -FirstParent $base -Diff $failedFile) 2>&1)
Check 'a record with a newly red case is refused' ($LASTEXITCODE -eq 1) (($out2 | ForEach-Object { "$_" }) -join ' / ')

# A diff whose hash does not describe its names must not become a record. Otherwise the field is one
# the writer carries and nobody checks -- a document saying "these plans" beside a hash of some other
# set, both from the same writer -- and `verify` would later be reading two answers about one question.
# Exit 2 and not 1: this is not a refuted reading, it is an unreadable one.
$diffWrong = @{
    planSet  = @{ names = @($planName); hash = $wrongPlanHash }
    baseline = @{ sha = $base; file = 'x.jsonl'; cases = 12; normal = 1 }
    fixed    = @('a_case')
    new      = @()
    changed  = @()
}
$wrongFile = Join-Path $scratch 'diff-wrong-plan-set.json'
[System.IO.File]::WriteAllText($wrongFile, (($diffWrong | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$out3 = @(& node @(Write-RecordArgs -FirstParent $base -Diff $wrongFile) 2>&1)
$out3Exit = $LASTEXITCODE
$out3Text = (($out3 | ForEach-Object { "$_" }) -join ' / ')
Check 'a diff whose plan-set hash does not describe its names is refused' ($out3Exit -eq 2) $out3Text
Check 'and the refusal says which hash the names give and which the document states' `
    ($out3Text -match [regex]::Escape("that hash to $planHash, but it says $wrongPlanHash")) $out3Text
Check 'and no record was written for it' (-not (Test-Path (Join-Path $passedDir "$mergeNew.json")))

# --- 4. with the record, the same push is allowed -----------------------------------------------
$good = @(& node @(Write-RecordArgs -FirstParent $base -Diff $diffFile) 2>&1)
$goodExit = $LASTEXITCODE
Check 'the record is written' ($goodExit -eq 0 -and (Test-Path (Join-Path $passedDir "$mergeNew.json"))) (($good | ForEach-Object { "$_" }) -join ' / ')
$saved = Get-Content -LiteralPath (Join-Path $passedDir "$mergeNew.json") -Raw | ConvertFrom-Json
Check 'it names both commits' ($saved.sha -eq $mergeNew -and $saved.firstParent -eq $base)
Check 'it carries the plan set and its hash' ($saved.planSet.count -eq 1 -and $saved.planSet.hash -eq $planHash)
Check 'it carries the baseline and its source' ($saved.baseline.sha -eq $base -and $saved.baseline.source)
Check 'it carries the three counts' ($saved.readings.fixed -eq 1 -and $saved.readings.new -eq 0 -and $saved.readings.changed -eq 0)

$push = Invoke-Git $work @('push', 'origin', "$($mergeNew):refs/heads/integration")
Check 'the same push is allowed once the record exists' ($push.Exit -eq 0) $push.Text
Check 'the remote is the merge commit' (((& git -C $origin rev-parse integration)).Trim() -eq $mergeNew)

# --- 5. a plain commit after a recorded merge needs no record ------------------------------------
# Read by the checker itself and not through a push: the arrival shape refuses a push whose tip is a
# plain commit, so a range that carries one can only be read where the rule is asked, which is here.
& git -C $work reset --quiet --hard $mergeNew 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'backlog.md') -Value 'another row'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'backlog: another row' 2>&1 | Out-Null
$row = (& git -C $work rev-parse HEAD).Trim()
# The checker reads the repository it is run in, so it is run in the fixture and not in this checkout.
Push-Location -LiteralPath $work
try {
    $check = @(& node @((Join-Path $work 'scripts/merge-record.mjs'), 'verify', $mergeNew, $row) 2>&1)
    $checkExit = $LASTEXITCODE
} finally {
    Pop-Location
}
Check 'a backlog row on top of a recorded merge needs no record' ($checkExit -eq 0 -and ($check -join ' ') -match '0 merge commit') (($check | ForEach-Object { "$_" }) -join ' / ')

# --- 6. a record that is not a pass is not a pass -----------------------------------------------
# The hook reads the record too, not only its existence: a hand-written one with a red case in it
# must not open the door the writer refuses to open.
& git -C $work checkout --quiet -B 'lane/second' $base 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'second.txt') -Value 'second'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'lane: second change' 2>&1 | Out-Null
& git -C $work checkout --quiet -B 'merge/second' $mergeNew 2>&1 | Out-Null
& git -C $work merge --quiet --no-ff 'lane/second' -m 'merge second lane' 2>&1 | Out-Null
$second = (& git -C $work rev-parse HEAD).Trim()
$forged = @{
    sha = $second; firstParent = (& git -C $work rev-parse 'HEAD^1').Trim(); secondParent = 'x'
    planSet = @{ names = @('p'); hash = '1-70'; count = 1 }
    baseline = @{ sha = 'x'; source = 'forged' }
    readings = @{ cases = 1; fixed = 0; new = 2; changed = 0 }
}
New-Item -ItemType Directory -Force -Path $passedDir | Out-Null
[System.IO.File]::WriteAllText((Join-Path $passedDir "$second.json"), (($forged | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$push = Invoke-Git $work @('push', 'origin', "$($second):refs/heads/integration")
Check 'a record with a red case in it does not open the door' ($push.Exit -ne 0) $push.Text
Check 'and the refusal says the record is not a pass' ($push.Text -match 'not a pass') $push.Text

# --- 7. the record's own plan set has to be one set --------------------------------------------
# The writer refused a document whose hash did not describe its names, so a disagreeing pair that
# reached the disk did not come from the writer. That is the whole reason the check is also here:
# the hook reads a file, not the code path that meant to write it, and a record is a claim anyone
# with write access to `.git/camel-passed` can make. A hand-made record can still state a coherent
# set of plans -- nothing local can prove it was measured -- but it can no longer state an incoherent
# one and be waved through, and those are different amounts of trust, not the same one.
#
# The assertion names this commit: a refusal with the right sentence about the wrong commit would
# otherwise pass as a reading.
& git -C $work checkout --quiet -B 'lane/third' $base 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'third.txt') -Value 'third'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'lane: a third change' 2>&1 | Out-Null
& git -C $work checkout --quiet -B 'merge/third' $mergeNew 2>&1 | Out-Null
& git -C $work merge --quiet --no-ff 'lane/third' -m 'merge third lane' 2>&1 | Out-Null
$third = (& git -C $work rev-parse HEAD).Trim()
$incoherent = @{
    sha = $third; firstParent = (& git -C $work rev-parse 'HEAD^1').Trim(); secondParent = 'x'
    planSet = @{ names = @($planName); hash = $wrongPlanHash; count = 1 }
    baseline = @{ sha = 'x'; source = 'forged' }
    readings = @{ cases = 12; fixed = 1; new = 0; changed = 0 }
}
[System.IO.File]::WriteAllText((Join-Path $passedDir "$third.json"), (($incoherent | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$push = Invoke-Git $work @('push', 'origin', "$($third):refs/heads/integration")
Check 'a record whose plan-set hash does not describe its names does not open the door' ($push.Exit -ne 0) $push.Text
Check 'and the refusal is this commit, saying the two hashes differ' `
    ($push.Text -match [regex]::Escape($third.Substring(0, 9)) -and $push.Text -match [regex]::Escape('the set and its hash are not the same set')) $push.Text

# --- 8. a merge with nothing to measure is a record of its own ----------------------------------
# Every file a documentation-only merge touches is a file no plan covers, so `--changed` selects no
# plan and there is nothing to compare. That is not "the gate is down" (exit 2 with no reading) and it
# is not "measured and green" (a comparison with zeros in it): it is a third claim, and a push has to
# be able to tell it apart from the other two. The writer refuses `--measured none` unless it says why
# and names the gate that decided it, and `verify` refuses a hand-written one that carries rows.
& git -C $work checkout --quiet -B 'lane/docs' $base 2>&1 | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $work 'internal/questions') | Out-Null
Set-Content -LiteralPath (Join-Path $work 'internal/questions/scratch.md') -Value 'a question'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'lane: a question page' 2>&1 | Out-Null
$docsLane = (& git -C $work rev-parse HEAD).Trim()
# On top of the remote's tip: `integration` is where a docs-only merge lands too, and the arrival
# shape asks its first parent to be that tip.
& git -C $work checkout --quiet -B 'merge/docs' $mergeNew 2>&1 | Out-Null
$docsBase = (& git -C $work rev-parse HEAD).Trim()
& git -C $work merge --quiet --no-ff 'lane/docs' -m 'merge a question page' 2>&1 | Out-Null
$docsMerge = (& git -C $work rev-parse HEAD).Trim()

$docsArgs = @((Join-Path $work 'scripts/merge-record.mjs'), 'write',
    '--merge', $docsMerge, '--first-parent', $docsBase, '--second-parent', $docsLane,
    '--branch', 'lane/docs', '--tree', $work, '--out', $passedDir)

$noWhy = @(& node @($docsArgs + @('--measured', 'none', '--gate', 'rows', '--gate', 'docs-only')) 2>&1)
Check 'a docs-only record without a reason is refused' ($LASTEXITCODE -eq 2) (($noWhy | ForEach-Object { "$_" }) -join ' / ')
$noGate = @(& node @($docsArgs + @('--measured', 'none', '--why', 'documentation only')) 2>&1)
Check 'a docs-only record without the gate that decided it is refused' ($LASTEXITCODE -eq 2) (($noGate | ForEach-Object { "$_" }) -join ' / ')

$docsOut = @(& node @($docsArgs + @('--measured', 'none', '--why', 'documentation only', '--gate', 'docs-only')) 2>&1)
$docsExit = $LASTEXITCODE
Check 'a docs-only record is written' ($docsExit -eq 0 -and (Test-Path (Join-Path $passedDir "$docsMerge.json"))) (($docsOut | ForEach-Object { "$_" }) -join ' / ')
$savedDocs = Get-Content -LiteralPath (Join-Path $passedDir "$docsMerge.json") -Raw | ConvertFrom-Json
Check 'it says nothing was measured' ($savedDocs.measured -eq 'none') "$($savedDocs.measured)"
Check 'it says why' ($savedDocs.why -match 'documentation') "$($savedDocs.why)"
Check 'it names no plans and no rows' ($savedDocs.planSet.count -eq 0 -and $savedDocs.readings.cases -eq 0) "count=$($savedDocs.planSet.count) cases=$($savedDocs.readings.cases)"
Check 'it names the gate that decided it' ($savedDocs.gates -contains 'docs-only') ($savedDocs.gates -join ',')

$push = Invoke-Git $work @('push', 'origin', "$($docsMerge):refs/heads/integration")
Check 'a docs-only merge is pushed' ($push.Exit -eq 0) $push.Text
Check 'and the remote is that merge' (((& git -C $origin rev-parse integration)).Trim() -eq $docsMerge)

# --- 9. a hand-written docs-only record cannot claim rows ---------------------------------------
# The hook reads the file, not the writer. A record that says `measured: none` and carries rows is a
# document contradicting itself, and the whole point of the field is that the two readings are told
# apart -- so `verify` refuses it rather than reading the zeros.
& git -C $work checkout --quiet -B 'lane/docs2' $docsMerge 2>&1 | Out-Null
Set-Content -LiteralPath (Join-Path $work 'internal/questions/second.md') -Value 'another question'
& git -C $work add -A 2>&1 | Out-Null
& git -C $work commit --quiet -m 'lane: another question page' 2>&1 | Out-Null
& git -C $work checkout --quiet -B 'merge/docs2' $docsMerge 2>&1 | Out-Null
& git -C $work merge --quiet --no-ff 'lane/docs2' -m 'merge another question page' 2>&1 | Out-Null
$docsAfter = (& git -C $work rev-parse HEAD).Trim()
$forgedDocs = @{
    sha = $docsAfter; firstParent = (& git -C $work rev-parse 'HEAD^1').Trim(); secondParent = 'x'
    planSet = @{ names = @(); hash = '0-9e37'; count = 0 }
    baseline = @{ sha = 'x'; source = 'none (documentation only)'; cases = $null }
    readings = @{ cases = 12; fixed = 0; new = 0; changed = 0 }
    gates = @('headers', 'docs-only'); measured = 'none'; why = 'documentation only'
}
[System.IO.File]::WriteAllText((Join-Path $passedDir "$docsAfter.json"), (($forgedDocs | ConvertTo-Json -Depth 6) + "`n"), (New-Object System.Text.UTF8Encoding($false)))
$push = Invoke-Git $work @('push', 'origin', "$($docsAfter):refs/heads/integration")
Check 'a docs-only record carrying rows does not open the door' ($push.Exit -ne 0) $push.Text
Check 'and the refusal says the two readings disagree' ($push.Text -match 'nothing was measured and reports') $push.Text

# --- result ------------------------------------------------------------------
& git -C $work worktree remove --force $tree 2>&1 | Out-Null
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0
