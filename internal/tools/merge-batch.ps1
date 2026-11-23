<#
.SYNOPSIS
    Merge several lanes into develop in one pass: measure once, compare once, push once.

.DESCRIPTION
    One merge costs two plan runs -- the plans the change touches, read once on the tree the merge
    lands on and once on the merged tree -- and a lane that reaches the kernel names seventeen plans,
    so a single merge is about half an hour. With twenty-odd lanes queued, the row readings and not
    the merges are the queue. This tool takes three or four lanes at a time when they touch no file in
    common, merges them one after another into ONE temporary worktree, and spends the same two plan
    runs on all of them:

      1. the lanes, and the files each one changes, read as numbers -- every pair of those file sets
         must be empty, or the batch is refused before anything is merged;
      2. one temporary worktree, each lane merged into it in turn;
      3. the plan set is the union of the lanes' `--changed`, run once on a checkout of the first
         parent and once on the merged tree;
      4. no new red and no changed failure -> one push;
      5. otherwise the batch is bisected by dropping one lane at a time, and the remainder is
         MEASURED again (a reading, not an inference): the drop whose comparison comes back empty is
         the culprit, it leaves the batch, and the rest is pushed.

    Two things this file refuses to leave to memory:

    * the baseline is a tree of the FIRST PARENT of the merged commit, never the merged tree. A
      baseline taken on the merged tree compares that tree with itself, answers zero difference for
      every case, and reads exactly like a clean merge. Both shas are printed, and the push re-reads
      both, because a batch is where the two can drift: the second batch's first parent is not the
      first batch's;
    * the gate that measures is the gate from BEFORE the merge. A lane that edits `internal/tools/*`
      puts its own version of the measuring tools into the merged tree, and the failure is silent: on
      2026-11-24 a lane's `camel-remote.ps1` reported `-Changed` as zero changed files, so the
      baseline seeded nothing and said nothing. Every measurement here therefore runs
      `camel-remote.ps1` from `-ToolTree` (this tool's own tree) with `-Path` pointed at the tree being
      read, and the plan set travels explicitly as `-Plan` -- never `-Changed`, which is the selection
      that answered "0 files" in the first place.

    One pass record is written per merge commit, not one for the tip: `scripts/hooks/pre-push` walks
    the first-parent chain of what a push publishes and refuses a merge commit with no record, so a
    batch that recorded only its tip would be refused after everyone had concluded it passed.

    Exit codes, the three-way convention the other gates use: 0 measured and pushed, 1 measured and
    refused (the cases are named), 2 not measured (nothing was learned about the tree).

.PARAMETER Branch
    The lanes to merge, in the order they are merged. One is a single merge in the old shape, which is
    what a batch is compared against.

.PARAMETER Message
    The merge commit subject for the first lane. Without it each merge is subject
    `merge <name>: <the lane's tip subject>` and its body lists the commits the merge brings.

.PARAMETER ToolTree
    The checkout whose `internal/tools` and `scripts` run the gates. Default: the tree this script is
    in. It must not be the merged tree -- see the second bullet above.

.PARAMETER MergeWorktree
    The tree that pushes `develop`, from the settings file's `mergeWorktree` by default. The push runs
    there and nowhere else, because `scripts/hooks/pre-push` allows `develop` from that tree only.

.PARAMETER ScratchRoot
    Where the two temporary worktrees live. Default `D:\Temp\camel-batch`. Each is created once with a
    creation record (rule 12, `internal/collab/rules.md`) and removed only after a push, and only when
    the merge worktree's `develop` is the commit just pushed -- a tree holding an unpushed merge is
    the one thing a cleanup must not delete.

.PARAMETER Jobs
    The plan runner's width, narrowed by the host slot as usual.

.PARAMETER NoPush
    Stop after the comparison and the pass records, leaving the merged tree for a person to push.

.PARAMETER WhatIf
    Read the lanes, the file sets, the pairwise intersections and the plan set, print them, and stop
    before creating anything.

.EXAMPLE
    .\merge-batch.ps1 -Branch lane/comprehension-shadowed,lane/refgen-body,lane/generator-sweep
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string[]] $Branch,
    [string] $Message = '',
    [string] $ToolTree = '',
    [string] $MergeWorktree = '',
    [string] $ScratchRoot = 'D:\Temp\camel-batch',
    [int] $Jobs = 8,
    [int] $SlotWaitSeconds = 1800,
    [switch] $NoPush,
    [switch] $WhatIf
)

$ErrorActionPreference = 'Stop'

# `pwsh -File` makes `-Branch a,b` one word, and `-Branch a -Branch b` is a PowerShell binding error
# (the engine refuses it before this body runs). So the list is split on commas exactly as
# `-Plan`/`-AstOnly` are elsewhere in this directory (camel-remote-args.ps1), and both spellings
# `-Branch 'a,b'` and `-Branch a,b` mean the same two lanes.
$Branch = @($Branch | ForEach-Object { "$_".Split(',') } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Branch.Count -eq 0) { Write-Host '[batch] -Branch names no lane'; exit 1 }

$script:pushHappened = $false
$script:pushedSha = ''
$script:mergeTip = ''
$script:mergeTree = ''
$script:createdTrees = @()
$script:timings = @()
$script:dropped = $null
$script:mergesPushed = 0
$script:lanes = @()
$script:base = ''
$script:baseShort = ''
$script:loadStart = ''

# --- the three ways a batch can stop ---------------------------------------
# The convention every gate here uses, because the code that calls this one branches on it: 1 is a
# reading about the tree, 2 is the absence of a reading. Both stop the batch, and only one of them
# says anything about the change.
function Stop-With {
    param([string] $Why, [string] $How, [int] $Code = 1)
    Write-Host ''
    if ($Code -eq 1) { Write-Host "STOPPED: $Why" -ForegroundColor Red }
    else { Write-Host "STOPPED: $Why (exit 2, no reading taken)" -ForegroundColor Yellow }
    Write-Host "  $How"
    Write-Host ''
    if ($script:pushHappened) {
        Write-Host "The batch is on origin/develop at $($script:pushedSha) and is not undone by this."
    } elseif ($script:mergeTip) {
        Write-Host "The merged tree is $($script:mergeTip) in $($script:mergeTree) and nothing was pushed."
    } else {
        Write-Host 'Nothing was merged and nothing was pushed.'
    }
    # A refusal after the two plan runs is where the numbers matter most -- they are what the batch
    # cost to learn this -- so the timing table is printed on the way out and not only on success.
    if ($script:timings.Count -gt 0) { Write-BatchSummary }
    exit $Code
}

function Write-Text {
    param([string] $Path, [string] $Text)
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    # No BOM and LF: the comparer splits on `\n` and matches its markers exactly.
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-Git {
    param([string] $Dir, [string[]] $GitArgs)
    $out = @(& git -C $Dir @GitArgs 2>&1)
    $code = $LASTEXITCODE
    $text = (($out | ForEach-Object { "$_" }) -join "`n").Trim()
    return [pscustomobject]@{ Code = $code; Text = $text; Lines = $out }
}

function Get-LoadAverage {
    # One number for "what else the host was doing": a duration read beside another lane's build is
    # not the same reading as the same duration taken alone.
    $r = @(& ssh -o BatchMode=yes -o ConnectTimeout=10 $script:sshHost 'cut -d" " -f1 /proc/loadavg' 2>&1)
    if ($LASTEXITCODE -ne 0) { return 'unreadable' }
    return "$($r | Select-Object -First 1)".Trim()
}

function Write-BatchSummary {
    Write-Host ''
    Write-Host '─────────────────────────────────────────────────────────────────'
    $names = @($script:lanes | ForEach-Object { $_.Short })
    Write-Host ("[batch] {0} lane(s) in this batch: {1}" -f $script:lanes.Count, ($names -join ', '))
    if ($script:dropped) { Write-Host ("[batch] dropped by the bisect: {0}" -f $script:dropped.Short) }
    $pushed = 'nothing'
    if ($script:pushHappened) { $pushed = "$($script:mergesPushed) merge commit(s), tip $($script:mergeTip.Substring(0, 9))" }
    elseif ($NoPush) { $pushed = 'nothing (-NoPush)' }
    Write-Host ("[batch] pushed: {0}" -f $pushed)
    foreach ($t in $script:timings) {
        Write-Host ("[batch]   {0,-30} {1,7:N1} min  {2,5} row(s)  exit {3}" -f $t.Label, ($t.Seconds / 60), $t.Rows, $t.Exit)
    }
    $total = ($script:timings | Measure-Object -Property Seconds -Sum).Sum
    if ($null -eq $total) { $total = 0 }
    Write-Host ("[batch]   {0,-30} {1,7:N1} min  ({2} run(s))" -f 'total', ($total / 60), $script:timings.Count)
    $tipShort = ''
    if ($script:mergeTip) { $tipShort = $script:mergeTip.Substring(0, 9) }
    Write-Host ("[batch] baseline {0} -> merged {1}; host load {2} at the start, {3} at the end" -f $script:baseShort, $tipShort, $script:loadStart, (Get-LoadAverage))
    Write-Host '─────────────────────────────────────────────────────────────────'
}

# --- the arguments that are files on this machine ----------------------------
$script:sshHost = '88zjwei'
if (-not $ToolTree) { $ToolTree = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
$ToolTree = (Resolve-Path $ToolTree).Path

$top = (Invoke-Git $ToolTree @('rev-parse', '--show-toplevel')).Text
if ($top -ne ($ToolTree -replace '\\', '/')) {
    # A directory that is not a git top-level answers about whatever repository is above it
    # (agents/branching.md): the reading looks exactly like this project's and is about another one.
    Stop-With "$ToolTree is not a git worktree's top level (git says $top)" 'Point -ToolTree at the checkout the tools are meant to run from.' 2
}

$cfgPath = Join-Path $ToolTree 'internal\tools\camel-gate.local.json'
if (-not (Test-Path -LiteralPath $cfgPath)) {
    Stop-With "no settings file at $cfgPath" 'Copy internal/tools/camel-gate.local.json from the main checkout; it is git-ignored and a fresh worktree does not get one.' 2
}
$cfg = Get-Content -LiteralPath $cfgPath -Raw | ConvertFrom-Json
if (-not $MergeWorktree) { $MergeWorktree = $cfg.mergeWorktree }
if (-not $MergeWorktree) {
    Stop-With 'the settings file names no mergeWorktree' "Add ``mergeWorktree`` to ${cfgPath}: only that tree may push develop, so the batch pushes from it and nowhere else." 2
}
$MergeWorktree = "$MergeWorktree".TrimEnd('\', '/')
if ($cfg.sshHost) { $script:sshHost = $cfg.sshHost }

$gate = Join-Path $ToolTree 'internal\tools\camel-remote.ps1'
$comparer = Join-Path $ToolTree 'scripts\merge-readings.mjs'
$selector = Join-Path $ToolTree 'scripts\test.js'
foreach ($needed in @($gate, $comparer, $selector)) {
    if (-not (Test-Path -LiteralPath $needed)) { Stop-With "the gate is incomplete: $needed is missing" 'Run this from a full checkout of the repository.' 2 }
}

$script:mergeTree = Join-Path $ScratchRoot 'camel-batch-merge'
$baseTree = Join-Path $ScratchRoot 'camel-batch-base'
$passedDir = Join-Path (Invoke-Git $ToolTree @('rev-parse', '--path-format=absolute', '--git-common-dir')).Text 'camel-passed'

Push-Location $ToolTree
try {
    New-Item -ItemType Directory -Force -Path $ScratchRoot | Out-Null

    # --- 0. a clean, current starting point ---------------------------------
    $dirty = @((Invoke-Git $ToolTree @('status', '--porcelain')).Lines | Where-Object { "$_".Trim() })
    if ($dirty.Count -gt 0) {
        Write-Host "[batch] the tool tree has $($dirty.Count) uncommitted change(s); the gate sends the"
        Write-Host '[batch] working tree and not only the commit, so this would measure a mixture'
        $dirty | Select-Object -First 5 | ForEach-Object { "    $_" }
        Stop-With "$ToolTree is not clean" "Commit or discard those changes: a reading taken over somebody else's edit is not a reading of these lanes." 2
    }

    $fetch = Invoke-Git $ToolTree @('fetch', 'origin', '--prune')
    if ($fetch.Code -ne 0) {
        Stop-With 'git fetch failed' "A batch against a stale origin is a batch against what you think is there. $($fetch.Text)" 2
    }

    $script:base = (Invoke-Git $ToolTree @('rev-parse', 'origin/develop')).Text
    $script:baseShort = $script:base.Substring(0, 9)
    $base = $script:base

    Write-Host ''
    Write-Host "[batch] $($Branch.Count) lane(s) against develop $($script:baseShort)" -ForegroundColor Cyan

    # --- 1. the lanes, read as numbers before anything is merged -------------
    # Every lane's changed-file set is the lane's OWN changes: `merge-base..tip` is the three-dot
    # diff. The two-dot `git diff origin/develop` -- what `--changed` reads in a lane's own tree --
    # also names the files develop moved without that lane, so for a batch it would make two lanes
    # look as if they collide over develop's own edits.
    $lanes = @()
    foreach ($name in $Branch) {
        $trimmed = "$name".Trim()
        if (-not $trimmed) { continue }
        $tip = Invoke-Git $ToolTree @('rev-parse', '--verify', "$trimmed^{commit}")
        if ($tip.Code -ne 0) { Stop-With "branch $trimmed does not resolve to a commit" 'Check the name; a lane branch is pushed to origin before it is merged.' 1 }
        $tipSha = $tip.Text

        $ahead = [int](Invoke-Git $ToolTree @('rev-list', '--count', "$base..$tipSha")).Text
        if ($ahead -le 0) {
            Stop-With "$trimmed has no commit that develop does not already have" 'Nothing to merge: an empty merge commit would be a claim with no content.' 1
        }
        $behind = [int](Invoke-Git $ToolTree @('rev-list', '--count', "$tipSha..$base")).Text
        $common = (Invoke-Git $ToolTree @('merge-base', $base, $tipSha)).Text
        if (-not $common) { Stop-With "$trimmed shares no merge-base with origin/develop" 'An unrelated history has no "its changes" to read.' 1 }

        $files = @((Invoke-Git $ToolTree @('diff', '--name-only', $common, $tipSha)).Lines |
            ForEach-Object { "$_".Trim().Replace('\', '/') } | Where-Object { $_ })
        if ($files.Count -eq 0) {
            Stop-With "$trimmed changes no file" 'A lane whose commits change no file has nothing for any plan to read.' 1
        }

        # The plans this lane's change selects, without a checkout of it: the same selection code the
        # gate's `--changed` runs, given the file list as a file (`test/runner.mjs`
        # `--changed-files`), which is what a run on another machine is given too.
        $listFile = Join-Path $ScratchRoot ("changed-{0}.txt" -f ($trimmed -replace '[^A-Za-z0-9]+', '-'))
        Write-Text $listFile (($files -join "`n") + "`n")
        $select = @(& node $selector --changed --changed-files $listFile --list 2>&1)
        $selectCode = $LASTEXITCODE
        $prefix = $ToolTree.TrimEnd('\', '/') + '\'
        $plans = @($select | ForEach-Object { "$_".Trim() } | Where-Object { $_ -and $_.StartsWith($prefix) } |
            ForEach-Object { $_.Substring($prefix.Length) -replace '\\', '/' })
        $widened = @($select | Where-Object { "$_" -match '^\(the whole fast layer' }).Count -gt 0
        if ($selectCode -ne 0) {
            $last = (@($select | Select-Object -Last 2) | ForEach-Object { "$_".Trim() }) -join ' / '
            Stop-With "the plans $trimmed touches could not be selected (exit $selectCode)" "node scripts/test.js --changed --changed-files <list> --list is the selection itself and starts nothing. It said: $last" 2
        }
        if ($plans.Count -eq 0) {
            $why = 'no rule in test/plan-selection.json ties its files to a plan'
            if ($widened) { $why = 'it widened to the whole fast layer, and a whole-suite reading is not a reading about this lane' }
            Stop-With "$trimmed selects no plan" "Nothing about this lane would be measured case by case: $why. That is the verdict a single merge gets too, and a lane carried into a batch unread is the hole this refusal keeps shut." 1
        }

        $lanes += [pscustomobject]@{
            Name = $trimmed
            Short = ($trimmed -replace '^lane/', '')
            Tip = $tipSha
            Ahead = $ahead
            Behind = $behind
            Files = $files
            Plans = $plans
        }
    }
    if ($lanes.Count -eq 0) { Stop-With 'no lane was named' 'Give -Branch at least one lane branch.' 1 }
    $script:lanes = $lanes

    foreach ($lane in $lanes) {
        Write-Host ("  {0,-34} {1} {2,3} file(s) {3,3} plan(s), {4} commit(s) ahead, {5} behind" -f `
            $lane.Name, $lane.Tip.Substring(0, 9), $lane.Files.Count, $lane.Plans.Count, $lane.Ahead, $lane.Behind)
        $lane.Files | ForEach-Object { "      $_" }
    }

    # --- 2. the pairwise reading: no two lanes touch one file ----------------
    # An intersection of the sets, not an impression of the diff: "they look like they touch
    # different things" is a guess, and the conflict it misses is found after two plan runs. Every
    # pair is printed, the empty ones included, because a "0 shared files" that was never printed is
    # indistinguishable from a pair nobody compared.
    $pairCount = 0
    $collisions = @()
    for ($i = 0; $i -lt $lanes.Count; $i++) {
        for ($j = $i + 1; $j -lt $lanes.Count; $j++) {
            $pairCount++
            $shared = @($lanes[$i].Files | Where-Object { $lanes[$j].Files -contains $_ })
            Write-Host ("  pair {0} & {1}: {2} shared file(s)" -f $lanes[$i].Short, $lanes[$j].Short, $shared.Count)
            if ($shared.Count -gt 0) { $collisions += ("{0} & {1}: {2}" -f $lanes[$i].Short, $lanes[$j].Short, ($shared -join ', ')) }
        }
    }
    if ($collisions.Count -gt 0) {
        $collisions | ForEach-Object { "    $_" }
        Stop-With "$($collisions.Count) pair(s) of lanes change the same file" 'A batch is only as cheap as its lanes are separate: two lanes on one file need a merge of their own, because only then is the case that moved attributable to one change.' 1
    }
    Write-Host ("[batch] {0} pair(s), every one with 0 shared file(s)" -f $pairCount)

    # --- 3. the plan set: the union of the lanes' `--changed` ----------------
    $planSet = @($lanes | ForEach-Object { $_.Plans } | Sort-Object -Unique)
    $lanes | ForEach-Object { Write-Host ("  {0}: {1} plan(s)" -f $_.Short, $_.Plans.Count) }
    Write-Host ("[batch] plan set: {0} plan(s), the union of the lanes' --changed" -f $planSet.Count)
    $planSetFile = Join-Path $ScratchRoot 'plan-set.txt'
    Write-Text $planSetFile (($planSet -join "`n") + "`n")

    if ($WhatIf) {
        Write-Host ''
        Write-Host "[batch] -WhatIf: $($lanes.Count) lane(s) would be merged into one temporary tree at $($script:baseShort)" -ForegroundColor Yellow
        Write-Host "[batch] and $($planSet.Count) plan(s) would be run twice -- once on $($script:baseShort), once on the merge."
        exit 0
    }

    # --- 4. the two trees ---------------------------------------------------
    # The baseline tree is a checkout of the FIRST PARENT, and it is a different tree with a
    # different sha on purpose: seeding the baseline on the merged tree and comparing is comparing a
    # tree with itself, which answers zero difference for every case and reads like a clean merge.
    function Initialize-Tree {
        param([string] $Path, [string] $BranchName, [string] $Sha)
        $normalized = ($Path -replace '\\', '/')
        $registered = @((Invoke-Git $ToolTree @('worktree', 'list', '--porcelain')).Lines |
            Where-Object { "$_" -like 'worktree *' } | ForEach-Object { ("$_".Substring(9).Trim()) -replace '\\', '/' })
        if ($registered -notcontains $normalized) {
            if (Test-Path -LiteralPath $Path) {
                Stop-With "$Path exists but is not a registered worktree" 'A directory with no .git of its own makes git answer about the repository above it. Move it out of the way and run again.' 2
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
            $add = Invoke-Git $ToolTree @('worktree', 'add', '-B', $BranchName, $Path, $Sha)
            if ($add.Code -ne 0) { Stop-With "could not create the temporary worktree $Path" "$($add.Text)" 2 }
            # Rule 12: what a cleanup may delete is what a run recorded creating.
            Add-Content -LiteralPath (Join-Path $ScratchRoot 'manifest.jsonl') -Value (
                (ConvertTo-Json -Compress -InputObject ([pscustomobject]@{
                            path = $Path; branch = $BranchName; sha = $Sha; pid = $PID; at = (Get-Date).ToString('o')
                        })))
            $script:createdTrees += $Path
        } else {
            $co = Invoke-Git $Path @('checkout', '--force', '-B', $BranchName, $Sha)
            if ($co.Code -ne 0) { Stop-With "could not move $Path to $Sha" "$($co.Text)" 2 }
            $rs = Invoke-Git $Path @('reset', '--hard', $Sha)
            if ($rs.Code -ne 0) { Stop-With "could not reset $Path to $Sha" "$($rs.Text)" 2 }
        }
        # Every checkout the gate reads needs the machine-local settings file: it is git-ignored, so
        # `worktree add` does not copy it and a run in the new tree stops with exit 2 before it
        # transfers anything.
        Copy-Item -LiteralPath $cfgPath -Destination (Join-Path $Path 'internal\tools\camel-gate.local.json') -Force
    }

    $script:loadStart = Get-LoadAverage
    Write-Host ''
    Write-Host "[batch] baseline tree: $baseTree (develop $($script:baseShort), a separate tree from the merge)"
    Initialize-Tree -Path $baseTree -BranchName 'probe/batch-base' -Sha $base

    # --- 5. measure one tree with the plan set, given explicitly ------------
    # `-Plan` and never `-Changed`: the plan set is already decided (the lanes' union), and `-Changed`
    # is the selection that reported zero files when a lane had replaced the gate.
    function Measure-Tree {
        param([string] $Path, [string] $Label, [string] $TranscriptPath)
        Write-Host "[batch] $Label ..." -NoNewline
        $started = Get-Date
        $run = @(& pwsh -NoProfile -File $gate -Path $Path -Plan ($planSet -join ',') -Jobs $Jobs `
                -ListKnown -NoGpuWatch -SlotWaitSeconds $SlotWaitSeconds 2>&1)
        $code = $LASTEXITCODE
        $seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)
        Write-Host (" {0:N1} min, exit {1}" -f ($seconds / 60), $code)
        Write-Text $TranscriptPath ((($run | ForEach-Object { "$_" }) -join "`n") + "`n")

        $rows = @()
        $inside = $false
        foreach ($line in $run) {
            $text = "$line".Trim()
            if ($text -eq '__CAMEL_REMOTE_ROWS_START__') { $inside = $true; continue }
            if ($text -eq '__CAMEL_REMOTE_ROWS_END__') { $inside = $false; continue }
            if ($inside -and $text.StartsWith('{')) { $rows += $text }
        }
        $summary = @($run | Where-Object { "$_" -match '\[SUMMARY\]' } | Select-Object -Last 1)
        $script:timings += [pscustomobject]@{ Label = $Label; Seconds = $seconds; Rows = $rows.Count; Exit = $code }
        $detail = ''
        if ($summary.Count -gt 0) { $detail = '; ' + "$($summary[0])".Trim() }
        Write-Host ("[batch]   {0} row(s) from the row channel{1}" -f $rows.Count, $detail)
        return [pscustomobject]@{ Code = $code; Rows = $rows; Seconds = $seconds; Transcript = $TranscriptPath }
    }

    $baseRowsFile = Join-Path $ScratchRoot 'base-rows.jsonl'
    $baseRun = Measure-Tree -Path $baseTree -Label "baseline $($script:baseShort) (first parent)" -TranscriptPath (Join-Path $ScratchRoot 'base-transcript.txt')
    if ($baseRun.Rows.Count -eq 0) {
        Stop-With 'the baseline run produced no rows' "The row channel printed nothing between its markers, so nothing was measured on $($script:baseShort) and there is nothing to compare against. The transcript is $($baseRun.Transcript): read the run's own lines there. A gate that did not run is not a tree that is fine." 2
    }
    Write-Text $baseRowsFile (($baseRun.Rows -join "`n") + "`n")

    # --- 6. the merged tree -------------------------------------------------
    function Merge-IntoTree {
        param($Lanes)
        Initialize-Tree -Path $script:mergeTree -BranchName 'probe/batch-merge' -Sha $base
        $made = @()
        $first = $true
        foreach ($lane in $Lanes) {
            # What HEAD is before this merge. Each merge's first parent must be exactly this, and the
            # first one's must be the baseline: that chain is what makes the merged tree and the
            # baseline two ends of one comparison instead of two unrelated trees.
            $expectedFirstParent = (Invoke-Git $script:mergeTree @('rev-parse', 'HEAD')).Text
            $subjects = @((Invoke-Git $ToolTree @('log', '--reverse', '--format=%h %s', "$base..$($lane.Tip)")).Lines |
                ForEach-Object { "$_".Trim() } | Where-Object { $_ })
            $tipSubject = (Invoke-Git $ToolTree @('log', '-1', '--format=%s', $lane.Tip)).Text
            $subject = "merge $($lane.Short): $tipSubject"
            if ($first -and $Message) { $subject = $Message }
            $first = $false
            $body = @("Lane branch: $($lane.Name) at $($lane.Tip)")
            if ($Lanes.Count -gt 1) {
                $body += "Batch: $($Lanes.Count) lane(s) merged together into one temporary tree and measured once over the union of their plans."
            }
            if ($subjects.Count -gt 0) { $body += ''; $body += 'Integrated commits:'; $body += ($subjects | ForEach-Object { "  $_" }) }
            $msgFile = Join-Path $ScratchRoot ("message-{0}.txt" -f ($lane.Short -replace '[^A-Za-z0-9]+', '-'))
            Write-Text $msgFile (($subject + "`n`n" + ($body -join "`n") + "`n"))

            $merge = Invoke-Git $script:mergeTree @('merge', '--no-ff', '-F', $msgFile, $lane.Tip)
            if ($merge.Code -ne 0) {
                $conflicted = @((Invoke-Git $script:mergeTree @('diff', '--name-only', '--diff-filter=U')).Lines | Where-Object { "$_".Trim() })
                if ($conflicted.Count -gt 0) {
                    Invoke-Git $script:mergeTree @('merge', '--abort') | Out-Null
                    Stop-With "$($lane.Name) does not merge cleanly into the batch" ("{0} file(s) conflicted: {1}. The lanes touch no file in common, so this is develop having moved under one of them: resolve it in that lane, push it, and batch again." -f $conflicted.Count, ($conflicted -join ', ')) 1
                }
                Stop-With "git merge exited $($merge.Code) for $($lane.Name)" "git merge exited non-zero with no conflicted file -- the shape that reads like success. $($merge.Text)" 1
            }
            $in = Invoke-Git $script:mergeTree @('merge-base', '--is-ancestor', $lane.Tip, 'HEAD')
            if ($in.Code -ne 0) {
                Stop-With "git merge exited 0 but $($lane.Name) is not in HEAD" "That is the case the exit code cannot see; HEAD is $((Invoke-Git $script:mergeTree @('rev-parse', '--short', 'HEAD')).Text)." 1
            }
            $sha = (Invoke-Git $script:mergeTree @('rev-parse', 'HEAD')).Text
            $parents = @((Invoke-Git $script:mergeTree @('rev-list', '--parents', '-n', '1', 'HEAD')).Text.Split(' ') | Where-Object { $_ })
            if ($parents.Count -lt 3) { Stop-With "the merge of $($lane.Name) is not a merge commit" "HEAD $($sha.Substring(0, 9)) has $($parents.Count - 1) parent(s)." 1 }
            if ($parents[1] -ne $expectedFirstParent) {
                Stop-With "the merge of $($lane.Name) landed on $($parents[1]) and HEAD was $expectedFirstParent" 'A merge whose first parent is not what HEAD was means something else moved the branch. That is a tool fault, not a lane fault: report it rather than merging.' 2
            }
            $script:mergeTip = $sha
            $made += [pscustomobject]@{ Lane = $lane; Sha = $sha; FirstParent = $parents[1]; SecondParent = $parents[2] }
            Write-Host ("  merged {0} at {1} -> {2} (first parent {3})" -f $lane.Short, $lane.Tip.Substring(0, 9), $sha.Substring(0, 9), $parents[1].Substring(0, 9))
        }
        $head = (Invoke-Git $script:mergeTree @('rev-parse', 'HEAD')).Text
        # The identity the whole tool rests on: walking the merged commit's FIRST-PARENT chain back
        # must arrive at the baseline. Checking only HEAD's first parent would pass a batch whose
        # first merge landed somewhere else and whose last one landed on the second, so the chain is
        # walked to its root.
        $chain = @((Invoke-Git $script:mergeTree @('rev-list', '--first-parent', $head)).Lines |
            ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        if ($chain.Count -eq 0 -or $chain[$chain.Count - 1] -ne $base) {
            $root = if ($chain.Count -gt 0) { $chain[$chain.Count - 1] } else { '(unreadable)' }
            Stop-With "the merged tree's first-parent chain ends at $root and the baseline is $base" 'The comparison would be between two unrelated trees. That is a tool fault, not a lane fault: report it rather than merging.' 2
        }
        Write-Host ''
        Write-Host "[batch] merged tree $($head.Substring(0, 9)); its first-parent chain ($($chain.Count) commit(s)) ends at $($base.Substring(0, 9)) -- the tree the baseline was measured on"
        Write-Host "[batch] baseline $($base.Substring(0, 9)) and merged tree $($head.Substring(0, 9)) are two different commits" -ForegroundColor Cyan
        return [pscustomobject]@{ Tip = $head; Merges = $made }
    }

    function Compare-Rows {
        param([string] $Current, [string] $Label)
        $outDir = Join-Path $ScratchRoot ("diff-{0}" -f ($Label -replace '[^A-Za-z0-9]+', '-'))
        if (Test-Path -LiteralPath $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
        # The three sets are `scripts/merge-readings.mjs`' judgement and not this file's: `fixed` is
        # reported and never refuses, `new` and `changed` refuse. A second definition of green here
        # would be a second answer to the only question this stage asks.
        $cmp = @(& node $comparer --current $Current --baseline $baseRowsFile --plans $planSetFile --out $outDir 2>&1)
        $code = $LASTEXITCODE
        foreach ($line in $cmp) {
            $text = "$_".Trim()
            if ($text) { Write-Host "[batch]   $text" }
        }
        return [pscustomobject]@{ Code = $code; Lines = $cmp; OutDir = $outDir }
    }

    $built = Merge-IntoTree -Lanes $lanes
    $script:mergeTip = $built.Tip

    # --- 7. measure the merged tree, compare --------------------------------
    $mergeRowsFile = Join-Path $ScratchRoot 'merge-rows.jsonl'
    $mergeRun = Measure-Tree -Path $script:mergeTree -Label "merged $($script:mergeTip.Substring(0, 9))" -TranscriptPath (Join-Path $ScratchRoot 'merge-transcript.txt')
    if ($mergeRun.Rows.Count -eq 0) {
        Stop-With 'the merged run produced no rows' "Nothing was measured on the merged tree, so nothing is known about it. The transcript is $($mergeRun.Transcript)." 2
    }
    Write-Text $mergeRowsFile (($mergeRun.Rows -join "`n") + "`n")

    Write-Host "[batch] comparison: merged $($script:mergeTip.Substring(0, 9)) against $($script:baseShort) (baseline rows in $baseRowsFile) ..."
    $comparison = Compare-Rows -Current $mergeRowsFile -Label 'batch'

    # --- 8. bisect inside the batch ----------------------------------------
    # Not an inference from which lane "looks likely": each trial drops one lane, rebuilds the batch
    # from the first parent and MEASURES the remainder again. The trial whose comparison comes back
    # empty names the culprit -- and it is also the reading under which the rest is pushed, which is
    # why the remainder is left in the tree instead of being thrown away.
    $dropped = $null
    if ($comparison.Code -eq 1 -and $lanes.Count -gt 1) {
        Write-Host ''
        Write-Host '[batch] the batch moved cases; dropping one lane at a time and measuring the rest' -ForegroundColor Yellow
        $found = $false
        foreach ($candidate in $lanes) {
            $rest = @($lanes | Where-Object { $_.Name -ne $candidate.Name })
            if ($rest.Count -eq 0) { continue }
            Write-Host ''
            Write-Host ("[batch] trial: without {0} ({1} lane(s) left)" -f $candidate.Short, $rest.Count)
            $trial = Merge-IntoTree -Lanes $rest
            $slug = $candidate.Short -replace '[^A-Za-z0-9]+', '-'
            $trialRows = Measure-Tree -Path $script:mergeTree -Label ("without {0}" -f $candidate.Short) -TranscriptPath (Join-Path $ScratchRoot "transcript-without-$slug.txt")
            if ($trialRows.Rows.Count -eq 0) {
                Write-Host '[batch]   no rows: this trial measured nothing, so it names nothing' -ForegroundColor Yellow
                continue
            }
            $trialRowsFile = Join-Path $ScratchRoot "rows-without-$slug.jsonl"
            Write-Text $trialRowsFile (($trialRows.Rows -join "`n") + "`n")
            $trialCmp = Compare-Rows -Current $trialRowsFile -Label "without-$slug"
            if ($trialCmp.Code -eq 0) {
                Write-Host ("[batch]   {0} gone: the comparison is empty again, re-measured on {1}" -f $candidate.Short, $trial.Tip.Substring(0, 9)) -ForegroundColor Green
                $dropped = $candidate
                $built = $trial
                $script:mergeTip = $trial.Tip
                $script:dropped = $dropped
                $comparison = $trialCmp
                $mergeRowsFile = $trialRowsFile
                $found = $true
                break
            }
            if ($trialCmp.Code -ne 1) {
                Stop-With "the trial without $($candidate.Short) was not measured (exit $($trialCmp.Code))" "A trial that could not be read says nothing about that lane, so nothing can be attributed. The transcripts are in $ScratchRoot." 2
            }
            Write-Host ("[batch]   {0} gone: still not empty, so it is not the only cause" -f $candidate.Short)
        }
        if (-not $found) {
            Stop-With 'the batch moved cases and no single lane accounts for it' 'Every one-lane-removed trial was measured and each still moved cases: this needs two of the lanes together, which is not a batch. Nothing was pushed; the trials are in the transcripts in '"$ScratchRoot." 1
        }
    }
    elseif ($comparison.Code -eq 1) {
        Stop-With "the merge of $($lanes[0].Name) moved cases" 'The cases are named above. One lane is not a batch: this is the ordinary refusal, and it stops with the merge commit in the temporary tree and nothing pushed.' 1
    }
    if ($comparison.Code -ne 0) {
        Stop-With "the row readings were not taken (exit $($comparison.Code))" "Nothing was learned about this tree. The comparer's own refusal is above; the transcripts are in $ScratchRoot." 2
    }

    # --- 9. the cheap gates, on the merged tree -----------------------------
    # The header check costs a second, and a file without its two-line header stops a CI run before it
    # builds anything. The clang syntax check is CI's compiler over this tree's compile lines; both run
    # from the tool tree with `-Path` pointed at the merged tree, so a lane that edits the gate cannot
    # change the gate that reads it.
    $headers = @(& node (Join-Path $ToolTree 'scripts\headers.mjs') --check 2>&1)
    if ($LASTEXITCODE -ne 0) {
        $headers | Select-Object -Last 4 | ForEach-Object { "    $_" }
        Stop-With 'the header check failed' 'A file without its two-line header does not go to develop: it stops a whole CI run before the build starts.' 1
    }
    Write-Host '[batch] headers: ok'

    $syntax = @(& pwsh -NoProfile -File $gate -Path $script:mergeTree -Check clang-syntax -NoBuild -AllowStale -NoGpuWatch 2>&1)
    $syntaxCode = $LASTEXITCODE
    $syntax | Where-Object { "$_" -match 'clang-syntax:|check |STOPPED|refused|error:' } | Select-Object -Last 6 | ForEach-Object { Write-Host "[batch]   $("$_".Trim())" }
    if ($syntaxCode -eq 1) {
        Stop-With 'the clang syntax check is red (exit 1: the compiler ran and reported a diagnostic)' 'CI builds with clang 19 on linux and macos, so a tree that fails its syntax pass is red there however green the local build was.' 1
    }
    if ($syntaxCode -ne 0) {
        Stop-With 'the clang syntax check did not run' 'This is not a defect in the lanes: the check could not measure the tree, so nothing was learned about it. The commonest cause is that the merged tree has no compile database on the host, which only a build writes.' 2
    }
    Write-Host '[batch] clang-syntax: ok'

    # --- 10. one pass record per merge commit -------------------------------
    # The pre-push hook walks the first-parent chain of what a push publishes and requires a record
    # for EVERY merge commit in it. A batch that wrote one record for its tip would be refused for the
    # merges underneath it -- refused after everybody had concluded the batch passed. So one record
    # per merge commit, each naming its own parents, in the directory `scripts/merge-record.mjs`
    # reads (`<git-common-dir>/camel-passed/<sha>.json`).
    $diff = Get-Content -LiteralPath (Join-Path $comparison.OutDir 'diff.json') -Raw | ConvertFrom-Json
    $sortedNames = @(Get-Content -LiteralPath $planSetFile | ForEach-Object { "$_".Trim() } | Where-Object { $_ } | Sort-Object -Unique)
    $hash = 0
    foreach ($ch in ($sortedNames -join "`n").ToCharArray()) { $hash = (($hash * 31) + [int][char]$ch) -band 0xFFFFFFFF }
    $planSetHash = "{0}-{1:x}" -f $sortedNames.Count, $hash
    New-Item -ItemType Directory -Force -Path $passedDir | Out-Null
    foreach ($m in $built.Merges) {
        $record = [ordered]@{
            sha          = $m.Sha
            firstParent  = $m.FirstParent
            secondParent = $m.SecondParent
            laneBranch   = $m.Lane.Name
            tree         = $script:mergeTree
            planSet      = [ordered]@{ names = $sortedNames; hash = $planSetHash; count = $sortedNames.Count }
            baseline     = [ordered]@{
                sha    = $base
                source = 'measured on a separate checkout of the first parent, in this batch'
                file   = $baseRowsFile
                cases  = @($baseRun.Rows).Count
            }
            readings     = [ordered]@{
                cases   = @($baseRun.Rows).Count
                fixed   = @($diff.fixed).Count
                new     = @($diff.new).Count
                changed = @($diff.changed).Count
            }
            cases        = [ordered]@{ fixed = @($diff.fixed); new = @($diff.new); changed = @($diff.changed) }
            gates        = @('headers', 'clang-syntax', 'merge-readings')
            measuredAt   = (Get-Date).ToString('o')
            host         = $env:COMPUTERNAME
            writtenBy    = 'internal/tools/merge-batch.ps1'
        }
        Write-Text (Join-Path $passedDir "$($m.Sha).json") (($record | ConvertTo-Json -Depth 8) + "`n")
    }
    Write-Host ("[batch] pass records: {0} written for {0} merge commit(s), in {1}" -f $built.Merges.Count, $passedDir)
    $built.Merges | ForEach-Object { Write-Host ("  {0}  first parent {1}  {2}" -f $_.Sha.Substring(0, 9), $_.FirstParent.Substring(0, 9), $_.Lane.Short) }

    # --- 11. the push, from the merge worktree and nowhere else -------------
    # `scripts/hooks/pre-push` allows `refs/heads/develop` from the tree the settings file names and
    # from no other, so the push is made there (`<sha>:develop`, which is a fast-forward from the
    # commit this batch measured against) and never from the merged tree, whose name the hook does
    # not know.
    function Get-PushVerdict {
        if (-not (Test-Path -LiteralPath $MergeWorktree)) {
            return [pscustomobject]@{ Ok = $false; Why = "the merge worktree $MergeWorktree does not exist; the settings file's mergeWorktree names the tree that pushes develop" }
        }
        $localDev = Invoke-Git $MergeWorktree @('rev-parse', '--verify', '--quiet', 'develop^{commit}')
        if ($localDev.Code -ne 0 -or $localDev.Text -eq $base) { return [pscustomobject]@{ Ok = $true; Why = '' } }
        $inBatch = Invoke-Git $MergeWorktree @('merge-base', '--is-ancestor', $localDev.Text, $script:mergeTip)
        if ($inBatch.Code -eq 0) { return [pscustomobject]@{ Ok = $true; Why = '' } }
        # The branch pointer there carries commits this batch does not. The push below is still a
        # fast-forward on origin, so it would be accepted -- and it would leave that checkout's
        # `develop` diverged from origin, which is somebody else's work to reconcile. That is the one
        # state a merge position must not create from a side lane, so it is refused and named.
        $why = "That develop carries commits this batch does not, so pushing the batch would leave it diverged.`n" +
        "  Finish that push first (agents/dispatch.md 8.1: already merged and not pushed is more expensive`n" +
        "  than not merged), then run this batch again -- a batch based on an older develop is not a`n" +
        "  fast-forward. The merged tree is kept at $($script:mergeTree) ($($script:mergeTip)), and this is`n" +
        "  the push it would have run:`n" +
        "      git -C $MergeWorktree push origin $($script:mergeTip):develop"
        return [pscustomobject]@{ Ok = $false; Why = "the merge worktree's develop is $($localDev.Text.Substring(0, 9)) and the batch's first parent is $($script:baseShort)`n  $why" }
    }

    $verdict = Get-PushVerdict

    if ($NoPush) {
        Write-Host ''
        Write-Host '[batch] -NoPush: everything was measured and recorded; nothing was pushed.' -ForegroundColor Yellow
        if ($verdict.Ok) {
            Write-Host "[batch]   a push would go from $MergeWorktree as:"
        } else {
            Write-Host '[batch]   and a push would be refused anyway:' -ForegroundColor Yellow
            Write-Host "  $($verdict.Why)"
        }
        Write-Host "[batch]   git -C $MergeWorktree push origin $($script:mergeTip):develop"
        $script:mergesPushed = $built.Merges.Count
        Write-BatchSummary
        exit 0
    }

    if (-not $verdict.Ok) { Stop-With "the push was refused before it ran" $verdict.Why 1 }

    $remoteNow = ((& git -C $ToolTree ls-remote origin refs/heads/develop) -split "`t") | Select-Object -First 1
    if ($remoteNow -ne $base) {
        Stop-With "origin/develop moved to $remoteNow while this batch was being measured" "The comparison was against $($script:baseShort), so a push now would publish a tree nothing compared. Run the batch again against the new develop." 2
    }

    $pushOk = $false
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $push = @(& git -C $MergeWorktree push origin "$($script:mergeTip):develop" 2>&1)
        if ($LASTEXITCODE -eq 0) { $pushOk = $true; break }
        $push | Select-Object -Last 2 | ForEach-Object { "    $_" }
        Start-Sleep -Seconds 4
    }
    if (-not $pushOk) { Stop-With 'git push failed after 3 attempts' 'The merged tree is still here and still recorded; nothing about it changed.' 2 }
    $script:pushHappened = $true
    $script:pushedSha = $script:mergeTip
    $script:mergesPushed = $built.Merges.Count

    $remoteAfter = ((& git -C $ToolTree ls-remote origin refs/heads/develop) -split "`t") | Select-Object -First 1
    if ($remoteAfter -ne $script:mergeTip) {
        Stop-With 'origin/develop is not the commit just pushed' "local $($script:mergeTip), remote $remoteAfter" 1
    }
    Write-Host ''
    Write-Host "[batch] pushed $($script:mergeTip.Substring(0, 9)) to develop ($($built.Merges.Count) merge commit(s))" -ForegroundColor Green

    # --- 12. the trees go, and only after the push -------------------------
    # Deletion judged by a rule and not by "it looks unused": the merge worktree's develop must be the
    # commit just pushed, which is also the only reading under which this tree holds nothing that
    # exists nowhere else.
    $devAfter = (Invoke-Git $MergeWorktree @('rev-parse', '--verify', '--quiet', 'develop^{commit}')).Text
    if ($devAfter -and $devAfter -ne $script:mergeTip) {
        Write-Host "[batch] warning: keeping $ScratchRoot -- the merge worktree's develop is $($devAfter.Substring(0, 9)), not the pushed $($script:mergeTip.Substring(0, 9))" -ForegroundColor Yellow
        Write-Host '[batch]   that is somebody''s unpushed work in that checkout, not this run''s; the trees are kept.'
    } elseif ($script:createdTrees.Count -gt 0) {
        foreach ($path in $script:createdTrees) {
            Invoke-Git $ToolTree @('worktree', 'remove', '--force', $path) | Out-Null
            Write-Host "[batch] removed $path (created by this run; nothing in it exists only there)"
        }
        foreach ($br in @('probe/batch-base', 'probe/batch-merge')) {
            Invoke-Git $ToolTree @('branch', '-D', $br) | Out-Null
        }
    }

    Write-BatchSummary
}
finally {
    Pop-Location
}