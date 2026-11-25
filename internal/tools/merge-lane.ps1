<#
.SYNOPSIS
    Merge a lane branch into a target branch (`develop`, the default, or `integration`), and refuse to
    commit or push unless the merge actually happened.

.DESCRIPTION
    A merge can fail on one line among many, and a writer that looks for the word it expects -- "no
    conflicts" -- reads the failed merge as a successful one and pushes a commit that does not contain
    the branch. That happened once. So this asserts, before committing anything:

      1. git merge exited 0. The exit code, never the text.
      2. git merge-base --is-ancestor <branch head> HEAD holds -- the branch's commits are in HEAD.

    Either failing stops the script with nothing committed and nothing pushed, and says which one.
    The second covers the case the first cannot: a merge that reports success and merges nothing.

    The branch head goes into the commit message as a trailer, so a later reader can check the claim
    against the history rather than against a sentence in it.

    None of that helps if the branch is not where the merge thinks it is. So the start point is
    asserted first: `git fetch`, then HEAD must be `origin/<Target>` (the branch named by -Target).
    A seat works on a branch of its own -- two seats cannot check one branch out at once -- so HEAD is
    what is asserted, not the name of the local branch. When it is not the tip, the
    commits only HEAD holds are printed. They are, in the case that made this necessary,
    the merge commits a previous run made and could not push -- which is why that merge's first parent
    was not the commit on origin, why the push was refused with a message about something else, and
    why the branch kept drifting one more merge per run. When the commits on its first-parent chain
    are only such merges (a merge commit, no gate record for it, and a tree that is exactly what a
    plain merge of its two parents produces), they are kept under a tag and the checkout's branch is
    moved back to `origin/<Target>` before this merge starts. Any other shape stops with nothing merged and
    nothing reset.

    Once the merge commit exists, four stages run in order and the push is last: the header check,
    the clang syntax check, the row readings, then the push. Every stage uses the same three-way
    convention -- 0 means it measured and the tree is fine, 1 means it measured and the tree is not,
    2 means it could not measure at all -- and a 2 must read as "the gate is down", never as "your
    code is wrong", because a gate that waves a merge through while it is blind is exactly what the
    row stage exists to stop. The row readings compare this merge's plans case by case with the
    tree it lands on (`scripts/merge-readings.mjs`): the cases this change fixed, the cases it
    turned red, and the cases that were red both times and fail differently now.

    The baseline is what that comparison reads, and where it comes from is the whole of it: a
    baseline measured on the merged tree would be this change compared with itself -- 0 fixed, 0 new,
    0 changed, and a green that measured nothing. So when there is no baseline for the tree this
    merge lands on (its FIRST PARENT, i.e. develop as it stands before the merge), this script
    measures it itself: a worktree detached at that first parent, the same plan set, one run, stored
    with `--accept` under that sha. Two runs, two trees, and both shas are printed. The comparer
    refuses a baseline whose recorded tree is the tree it is comparing, so the mis-ordering cannot
    pass quietly; and the seeding step can fail, which is exit 2 and an empty baseline is never one.

    Every stop after the merge says what git holds at that moment -- HEAD, `origin/<Target>`
    and the commits only HEAD holds -- instead of the constant sentence it used to print,
    which claimed nothing had been committed while a merge commit sat on the branch that origin did
    not have.

.PARAMETER MainCheckout
    The merge seat's own worktree: the checkout a merge is made in, built in and measured in. Left
    empty, it is read from this machine's settings file (`internal/tools/camel-gate.local.json`,
    key `mergeWorktree`), which is where a machine-specific path belongs. There is no default path
    and no fallback: a run that cannot read the setting stops with exit 2 and says whether the file
    is missing or the key is, because a default that names a real tree does not fail -- it merges
    into somebody else's checkout.

.PARAMETER LocalConfig
    The settings file to read when it is not the one beside this script. Same parameter, same
    meaning, as camel-build.ps1, camel-test.ps1 and camel-remote.ps1.

.PARAMETER Branch
    The lane branch to merge, e.g. lane/range-ri.

.PARAMETER Target
    The branch this merge lands on and pushes to: `develop` (the default, the release line) or
    `integration` (the shared branch the parallel merge seats feed). The start point asserts that
    HEAD is `origin/<Target>`, not that a local branch of the target's name exists: two merge seats
    run at once, and only one of them can have a given branch checked out, so the second works on a
    seat branch that tracks the target. The start line prints the target and its tip, and every stop
    and the closing line read the same branch, so the reading says which branch a merge went to.

.PARAMETER Message
    The merge commit message. The branch head is appended as a trailer.

.PARAMETER WhatIf
    Pass both checks, then stop before committing.

.PARAMETER NoPush
    Take every reading -- the baseline, the merged tree, the comparison -- and stop before the push.
    It is how a merge is tried once without landing it: the merge commit stays on this checkout's
    HEAD (which is why a trial belongs on a scratch branch, never on develop), nothing reaches
    origin, and the baseline this merge would have stored for its own tree is not written, because a
    baseline for a commit nothing else can see is a key nobody asks for.

.PARAMETER WarmBaseline
    Start no merge: measure the fast layer (`test/plans/smoke` and `test/plans/feat`) once on the
    current develop commit, in the baseline worktree, and store the rows as that commit's baseline.
    This is the idle-slot half of D-1123-123: a baseline measured across a superset of the plans a
    merge selects lets that merge compare instead of measuring a baseline of its own, so the merge
    position runs one measurement (the merged tree) and not two. It asserts the same start point,
    pushes nothing and touches no branch; run it when the test slot is free, after a merge.

.PARAMETER BaselineWorktree
    Where the baseline is measured. It is a worktree of its own, held at the tree the merge lands
    on, and it is kept between merges so the host's build of it stays warm. Left empty, it is read
    from this machine's settings file (`baselineWorktree`), the same way -MainCheckout is, and a run
    that cannot read it stops with exit 2 instead of measuring a baseline in another seat's tree. The
    path may not exist yet: this script creates the worktree there on the first merge.

    This is the legacy shape: one fixed path per seat, moved to each tip. `-BaselineRoot` is the
    shape that shares the work (see below), and a machine that sets it does not need this one. When
    this path IS used, its HEAD is checked against the tip first: a tree holding another commit is
    said to be stale and moved, not read.

.PARAMETER BaselineRoot
    The directory the per-tip baseline checkouts live under. Each baseline tree is named by the
    commit it measures -- `<BaselineRoot>\<sha>` -- so the seats that land on one tip share one
    checkout, one host build and one reading instead of making three. Read from this machine's
    settings (`baselineRoot`) when not given. A run with neither this nor -BaselineWorktree stops
    with exit 2: a baseline measured in "some path" is a reading about somebody else's tree.

.EXAMPLE
    .\merge-lane.ps1 -Branch lane/range-ri -Message "merge range-ri: ..." -WhatIf
#>
[CmdletBinding()]
param(
    # Neither is Mandatory: -WarmBaseline starts no merge and names no branch, and a Mandatory
    # parameter would make PowerShell prompt for one. A merge without them is refused below, by name.
    [string] $Branch = '',
    [string] $Message = '',
    # The branch the merge lands on. `develop` is the release line; `integration` is the shared
    # branch the parallel merge seats feed. The merge asserts HEAD is the target's recorded tip and
    # pushes to the target, so this is the only thing that decides where a merge goes.
    [ValidateSet('develop', 'integration')]
    [string] $Target = 'develop',
    # The merge seat's own worktree. EMPTY means "read it from this machine's settings", which is the
    # only place a machine-specific path belongs (internal/tools/camel-gate.local.json,
    # `mergeWorktree`, git-ignored) -- the same key `scripts/hooks/pre-push` reads to decide which
    # tree may push develop, and `-MainCheckout` is the one-off override. It used to default to a path
    # on the author's workstation: the first default was the author's own checkout, which rule 17
    # forbids, and the repair -- another seat's tree -- kept the same defect with a different victim,
    # because a default that happens to name an existing tree does not fail, it merges into somebody
    # else's checkout. So there is no default now: a run that cannot read the setting stops and names
    # what is missing.
    [string] $MainCheckout = '',
    # The machine's settings file, when it is not the one beside this script. Same parameter as
    # camel-build.ps1, camel-test.ps1 and camel-remote.ps1, for the same reason: a run that has to
    # point at another file says so on its command line instead of editing the repository.
    [string] $LocalConfig = '',
    # Where the baseline is measured. It has to be a tree that is NOT the one being compared, and it
    # has to be a real checkout (the gate builds it), so it is a worktree of this repository detached
    # at the first parent. One fixed path and not one per merge: the host keeps the build of that
    # worktree between runs, and a fresh path per merge would mean a cold build per merge.
    #
    # Empty means "read it from this machine's settings" (`baselineWorktree`), the same way the merge
    # checkout is read, and for the same reason: a default that names a real directory is a baseline
    # measured in whoever's tree that is, and two seats sharing one baseline tree do not collide on
    # screen, they collide in the numbers. The path may not exist yet -- this script creates the
    # worktree there (Sync-BaselineWorktree) -- so "not there" is the first merge, not a refusal.
    [string] $BaselineWorktree = '',
    # The directory the per-tip baseline checkouts live under: the tree for a commit is
    # `<BaselineRoot>\<sha>`, so every seat that measures that commit uses the same tree. Empty means
    # "read `baselineRoot` from this machine's settings", and a machine that sets neither this nor
    # -BaselineWorktree is refused with exit 2 (the settings file says which to add).
    [string] $BaselineRoot = '',
    # One branch per baseline tree: git will not check one branch out twice, so a second merge
    # position beside this one gives its own tree its own branch (the batch tool's reason, ported).
    [string] $BaselineBranch = 'camel-baseline',
    [switch] $WhatIf,
    [switch] $NoPush,
    [switch] $WarmBaseline,
    # A merge commit the seat already made, because the merge needed a resolution this tool cannot
    # make: `git merge --no-ff` stopped on a conflict, the seat resolved the files and committed, and
    # from then on this tool's own merge step has nothing to add -- it would either be refused by the
    # start-point assertion (HEAD is no longer origin) or repeat a merge that now conflicts with its
    # own resolution. Given here, the four stages after the merge (clang, readings, record, push) run
    # exactly as they always do, on that commit; what is asserted instead of the merge is the shape a
    # merge this tool would have made has: first parent == origin/<Target>, second parent == the
    # lane's tip, HEAD == the commit itself. Anything else is refused, because a commit whose parents
    # are not that is not the merge it claims to be.
    [string] $MergeCommit = '',

    # A backfill may name the cases a historical merge could not pass. `-MergeCommit` records a merge
    # made earlier; its row reading compares that commit's tree with the commit before it, and a case
    # that was red at that snapshot and was fixed forward afterwards makes the record unwritable
    # forever -- the tree it measured no longer exists on the branch. The file names, for each such
    # case, the snapshot (which must be this merge commit), the commit that fixed it, and a run of
    # the tip whose `run.json` revision and case status are read here: the claim "green now" is
    # checked against a reading, not against the sentence that asked for the allowance. Only the
    # ordinary merge path is untouched by this switch -- it is refused without `-MergeCommit`.
    [string] $KnownAtSnapshot = '',
    # The same idea one layer up: a file that has no two-line header in the tree this merge made,
    # and was given one by a later commit. It names the file, the snapshot (this merge commit) and
    # the commit that wrote the header; the file has to be in this tree without a copyright line,
    # the fix has to be an ancestor of the tip named in the list, and every file the check names
    # has to have an entry. Also refused without `-MergeCommit`.
    [string] $KnownHeaderAtSnapshot = ''
)

$ErrorActionPreference = 'Stop'

# The shape a push must have (D-1124-78) lives beside this file, so both merge tools read one
# implementation: `merge-batch.ps1` dot-sources it too. Read here, called before every push.
. (Join-Path $PSScriptRoot 'merge-push-shape.ps1')

# --- which checkout a merge lands in -----------------------------------------
#
# --- the paths a merge is made in and measured in ------------------------------
#
# Both are machine-local paths, so both belong in this machine's settings file and nowhere else:
# internal/tools/camel-gate.local.json (git-ignored), keys `mergeWorktree` and `baselineWorktree`.
# `mergeWorktree` is the key the pre-push hook already reads to decide which tree may push develop,
# and sharing it is the point -- one setting says which tree is the merge position, so the hook that
# guards a push and the tool that makes the merge cannot disagree about it.
#
# Neither has a default any more, and that is the whole of the repair. -MainCheckout defaulted to the
# author's own checkout (rule 17 forbids working there); the repair pointed it at another seat's
# worktree, so the defect kept its shape and changed its victim. A default that happens to name an
# existing tree does not fail: the merge is made in somebody else's checkout and the run reports
# success. The baseline worktree had the same shape -- a path on this workstation -- with the same
# consequence one step later: the baseline is measured, and a worktree created, in a tree that
# belongs to whoever owns that path, and two seats share it without either of them saying so.
#
# So a run that cannot read a setting stops, and the ways to fail say different things, because a
# reader acts differently on each:
#
#   no settings file       this machine was never set up
#   no key in that file    it was set up without naming this tree
#   more than one value    the hook allows a list; a merge uses one, and "the first of them" is a guess
#   not an absolute path   a relative path resolves against the current directory, which is a guess
#   not a directory        (the merge position only) it must exist; the baseline tree is created there
#
# The last one is where the two keys differ, and the difference is a fact about the tools, not a
# weaker check for one of them: a merge position is a checkout that is already there, and a baseline
# worktree is made by this script (Sync-BaselineWorktree). Refusing a baseline path that does not
# exist yet would refuse the first merge on a fresh seat, which is the normal case and not a defect.
#
# Returns @{ Path; Problem }. `Problem` empty means `Path` is the answer.
function Resolve-SeatPath {
    param(
        [string] $Given = '',
        # The settings key, and the parameter that overrides it for one run. Both are named in every
        # refusal: the repair is one of the two, and the reader has to know which.
        [string] $Key = '',
        [string] $Parameter = '',
        [string] $LocalConfig = '',
        # Where camel-gate-config.ps1 lives (the settings loader, shared with the other gate tools).
        [string] $ToolsRoot = '',
        # The path may not exist yet: the baseline worktree is created at it by this script.
        [switch] $MayCreate,
        # The key may be absent and that is not a defect: `baselineRoot` is the newer shape, and a
        # machine still on `baselineWorktree` has no `baselineRoot` and must not be refused for it.
        # An absent optional key answers with an empty path; a malformed one is still a refusal.
        [switch] $Optional
    )

    # An explicit path is the answer, and the file is not even read: what a seat types on the command
    # line overrides the file for one run, which is the rule the other tools in this directory keep --
    # and it is how a machine with no settings file at all still merges.
    if ($Given) { return @{ Path = $Given; Problem = '' } }

    if (-not $ToolsRoot) { $ToolsRoot = $PSScriptRoot }
    . (Join-Path $ToolsRoot 'camel-gate-config.ps1')
    $cfg = Get-CamelGateConfig -LocalConfig $LocalConfig -ScriptRoot $ToolsRoot

    if (-not $cfg.localConfigFound) {
        if ($Optional) { return @{ Path = ''; Problem = '' } }
        return @{
            Path    = ''
            Problem = "there is no settings file at $($cfg.localConfigPath), and -$Parameter was not given. These paths are machine-local, so this run cannot name one: copy internal/tools/camel-gate.example.json to that path and set '$Key', or pass -$Parameter."
        }
    }
    $property = $cfg.PSObject.Properties[$Key]
    $value = if ($null -ne $property) { "$($property.Value)".Trim() } else { '' }
    if (-not $value) {
        if ($Optional) { return @{ Path = ''; Problem = '' } }
        return @{
            Path    = ''
            Problem = "the settings file at $($cfg.localConfigPath) has no '$Key', and -$Parameter was not given. That key is what says which tree this is, and nothing here may guess one: set it in that file, or pass -$Parameter."
        }
    }
    # A key may be a list for the hook's sake (a machine that keeps the merge tree in more than one
    # place). A merge uses one tree, and taking the first of several is the guess this exists to refuse.
    $trees = @($value -split ';' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($trees.Count -ne 1) {
        return @{
            Path    = ''
            Problem = "'$Key' in $($cfg.localConfigPath) names $($trees.Count) trees ($($trees -join '; ')), and this tool uses one tree. Pass -$Parameter to say which."
        }
    }
    $tree = $trees[0]
    if (-not [System.IO.Path]::IsPathRooted($tree)) {
        return @{
            Path    = ''
            Problem = "'$Key' in $($cfg.localConfigPath) names '$tree', which is not an absolute path. A path is read from that file or given on the command line, never resolved against whatever directory this happens to run in."
        }
    }
    if (-not $MayCreate -and -not (Test-Path -LiteralPath $tree -PathType Container)) {
        return @{
            Path    = ''
            Problem = "'$Key' in $($cfg.localConfigPath) names $tree, and there is no directory there. A merge position is a checkout that exists; nothing here will create one or use another."
        }
    }
    return @{ Path = $tree; Problem = '' }
}

# The two settings this script needs, each read through the one resolver above so the two cannot drift
# apart in what they check or in how a refusal reads.
function Resolve-MergeCheckout {
    param([string] $Given = '', [string] $LocalConfig = '', [string] $ToolsRoot = '')
    return Resolve-SeatPath -Given $Given -Key 'mergeWorktree' -Parameter 'MainCheckout' `
        -LocalConfig $LocalConfig -ToolsRoot $ToolsRoot
}

function Resolve-BaselineWorktree {
    param([string] $Given = '', [string] $LocalConfig = '', [string] $ToolsRoot = '')
    return Resolve-SeatPath -Given $Given -Key 'baselineWorktree' -Parameter 'BaselineWorktree' `
        -LocalConfig $LocalConfig -ToolsRoot $ToolsRoot -MayCreate
}

# The newer shape of the same setting: a directory the per-tip baseline checkouts live under, so a
# tip is measured once for every seat that lands on it. Optional on purpose -- a machine still on the
# legacy fixed path has no such key, and must not be refused for having the older arrangement.
function Resolve-BaselineRoot {
    param([string] $Given = '', [string] $LocalConfig = '', [string] $ToolsRoot = '')
    return Resolve-SeatPath -Given $Given -Key 'baselineRoot' -Parameter 'BaselineRoot' `
        -LocalConfig $LocalConfig -ToolsRoot $ToolsRoot -MayCreate -Optional
}

# The branch every reading in this script is about: the start point, the state a stop prints, the
# push, and the line that says the push landed. Read once here so Write-GitState -- which runs from
# stops all over the script, some before the start point is asserted -- reports the same branch.
$script:TargetBranch = $Target

# What git holds at the moment this is called, in the sentence that used to be a constant.
#
# The constant was `Nothing was committed and nothing was pushed`, and it was false six times in one
# day: each of those runs left the merge commit on local `develop` (952048856a, fa7d722407, 6d0af87f89,
# 0be4786c48, 542f52470a, 66041d0581 were left that way). A reader who believed the sentence went
# looking somewhere else, and four of those six cost twenty minutes before the local branch was read.
# A sentence about the repository is a reading, so it is read: local `develop`, `origin/develop`, the
# commits only local `develop` holds, and where this checkout stands.
#
# `-MergeCommit` is the merge commit this run made, passed by the callers that are past the merge in
# their own control flow -- not guessed here. Empty means the merge has not happened.
# Does the cache already hold a baseline that covers the fast layer for this commit? The merge position
# asks this between merges (to say which of the two the next merge will do) and -WarmBaseline asks it
# before spending a run. The list is what `--changed` widens to when a change names no plan, which is
# why it is the layer a superset baseline is measured across: every rule target in
# `test/plan-selection.json` sits under `test/plans/smoke` or `test/plans/feat`, and a selection that
# names something outside them (the GPU plans a tiles change adds) misses and measures as it always did.
function Test-FastLayerBaseline {
    param([string] $CacheDir, [string] $Develop, [string] $Checkout)

    $list = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-fastlayer-$PID.txt")
    [System.IO.File]::WriteAllText($list, "test/plans/smoke`ntest/plans/feat`n",
        (New-Object System.Text.UTF8Encoding($false)))
    try {
        $out = @(& node (Join-Path $Checkout 'scripts\merge-readings.mjs') --cache $CacheDir `
            --develop $Develop --plans $list --probe 2>&1)
        return @{ Hit = ($LASTEXITCODE -eq 0); Text = (($out | ForEach-Object { "$_" }) -join "`n") }
    } finally {
        Remove-Item -LiteralPath $list -Force -ErrorAction SilentlyContinue
    }
}
function Write-GitState {
    param([string] $MergeCommit = '')

    $t = $script:TargetBranch
    $branch = "$(& git symbolic-ref -q --short HEAD 2>$null)".Trim()
    $headHere = "$(& git rev-parse --verify HEAD 2>$null)".Trim()
    # A seat's checkout need not carry the target's name -- two seats cannot check one branch out at
    # once, so the second works on a seat branch that tracks it. A checkout with no local branch of
    # the target's name is therefore read at HEAD, which is the commit the push sends to origin.
    $localBranch = "$(& git rev-parse --verify "refs/heads/$t^{commit}" 2>$null)".Trim()
    $local = if ($localBranch) { $localBranch } else { $headHere }
    $localWho = if ($localBranch) { "local $t" } else { 'HEAD' }
    $localWhoCap = if ($localBranch) { "Local $t" } else { 'HEAD' }
    $origin = "$(& git rev-parse --verify "refs/remotes/origin/$t^{commit}" 2>$null)".Trim()
    $where = if ($branch) { "the branch $branch" } else { 'a detached HEAD' }

    if (-not $local -or -not $origin) {
        # One of the two ends is missing, so "same" cannot be answered at all. That is said plainly
        # rather than defaulted: a default here would be the same class of mistake as the constant.
        $localText = if ($local) { $local } else { 'unreadable' }
        $originText = if ($origin) { $origin } else { 'unreadable' }
        Write-Host "  Nothing was pushed. $localWhoCap reads $localText and origin/$t reads $originText; with one end unreadable, whether they are the same commit cannot be stated."
        return
    }

    # `@( ... )` around the whole pipeline, not around the git call: one line of output is a string,
    # and a string indexed at [0] is its first character -- this said "the commit is d" once.
    $only = @(& git rev-list "origin/$t..HEAD" 2>$null |
        ForEach-Object { "$_".Trim() } | Where-Object { $_ })
    $held = if ($only.Count -eq 0) {
        "$localWhoCap holds no commit that origin/$t does not have, so it is behind rather than ahead."
    } elseif ($only.Count -eq 1) {
        "The commit only $localWho holds is $($only[0])."
    } else {
        "The commits only $localWho holds are $($only -join ', ')."
    }

    if (-not $MergeCommit) {
        if ($local -eq $origin) {
            Write-Host "  Nothing was committed and nothing was pushed: $localWho is still $local, which is origin/$t."
        } else {
            Write-Host "  Nothing was committed and nothing was pushed by this run: $localWho is $local and origin/$t is $origin. $held"
        }
        return
    }
    if ($local -eq $origin -and $MergeCommit -ne $local) {
        # A trial on a scratch branch: the merge commit is real but it is not on the target, so the
        # target still reads what origin has and the push would publish nothing of it.
        Write-Host "  The merge was committed locally and not pushed: the merge commit is $MergeCommit on $where, and $localWho is still $local, which is origin/$t."
        return
    }
    if ($local -eq $MergeCommit) {
        Write-Host "  The merge was committed locally and not pushed: $localWho is $local, and origin/$t is $origin. $held Run this again after restoring $localWho (see the message above)."
        return
    }
    Write-Host "  The merge was committed locally and not pushed: the merge commit is $MergeCommit on $where; $localWho is $local, and origin/$t is $origin. $held"
}

function Stop-With {
    param([string] $Why, [string] $How, [string] $MergeCommit = '')
    Write-Host ''
    Write-Host "STOPPED: $Why" -ForegroundColor Red
    Write-Host "  $How"
    Write-Host ''
    Write-GitState -MergeCommit $MergeCommit
    exit 1
}

# --- which checkout measures a baseline, and under what name -----------------
#
# A baseline is "what the plans read on THIS commit". Its identity is therefore the commit and the
# plan set, and nothing else -- not the seat that measured it, not the path it happened to be
# checked out at. On 2026-11-24 three seats merged onto one `integration` tip and each built the
# same tree: three worktrees, three host builds, three runs, one reading. So the tree is named by
# the sha and shared: `<Root>\<sha>`, on a branch of the same name, and a seat that finds it ready
# uses it as it stands.
#
# The legacy shape (one fixed path per seat, moved to each tip) is kept, because a machine where it
# works is not repaired by a rename. It is read only after its HEAD is compared with the tip: a path
# holding another commit is a baseline for another tree, and it is moved -- out loud -- instead of
# being read.
#
# Returns @{ Path; Branch; Kind ('tip'|'fresh'|'stale'|'none'); Problem; Lines }. `Lines` are what
# the caller prints; this function decides and says nothing, so a test can call it without noise.
function Get-BaselineCheckout {
    param(
        [Parameter(Mandatory)][string] $Tip,
        # The directory the per-tip checkouts live under; empty means the legacy path is in use.
        [string] $Root = '',
        [string] $LegacyPath = '',
        [string] $LegacyBranch = 'camel-baseline'
    )

    if ($Tip.Length -lt 7) {
        return [pscustomobject]@{ Path = ''; Branch = ''; Kind = 'none'; Lines = @(); Problem = "'$Tip' is not a commit sha, so no baseline tree can be named for it" }
    }

    if ($Root) {
        $path = Join-Path $Root $Tip
        # 12 characters, the same prefix `merge-readings.mjs` uses for the reading's own file name:
        # one name for one commit, so a person reading a directory sees the same key the cache uses.
        $branch = "camel-baseline-$($Tip.Substring(0, 12))"
    } else {
        if (-not $LegacyPath) {
            return [pscustomobject]@{ Path = ''; Branch = ''; Kind = 'none'; Lines = @(); Problem = 'neither a baseline root nor a baseline worktree was named' }
        }
        $path = $LegacyPath
        $branch = $LegacyBranch
    }

    $held = ''
    if (Test-Path -LiteralPath $path) {
        $held = "$(& git -C $path rev-parse --verify HEAD 2>$null)".Trim()
    }
    if ($held -eq $Tip -and $held) {
        return [pscustomobject]@{ Path = $path; Branch = $branch; Kind = 'tip'; Lines = @(); Problem = '' }
    }
    if ($held) {
        $heldShort = if ($held.Length -gt 9) { $held.Substring(0, 9) } else { $held }
        $tipShort = $Tip.Substring(0, 9)
        $lines = @(
            'stale baseline beside the tip: rebuilding',
            "  $path holds $heldShort, and the tip being measured is ${tipShort}: a baseline for another tree is not reused, so it is moved to $tipShort and measured again."
        )
        return [pscustomobject]@{ Path = $path; Branch = $branch; Kind = 'stale'; Lines = $lines; Problem = '' }
    }
    return [pscustomobject]@{ Path = $path; Branch = $branch; Kind = 'fresh'; Lines = @(); Problem = '' }
}

# The baseline's own checkout: a worktree of THIS repository, held at the sha it must measure.
# Returns $null when the tree is ready, or a sentence saying why it is not -- the caller turns that
# into an exit 2, because a baseline that could not be taken is the gate being down and not a
# verdict about the tree being merged.
#
# A detached checkout is the obvious way to say "this tree and no branch", and `camel-remote` refuses
# it outright (`detached HEAD in <name>; check out a branch first`, because it stages a *branch* to
# the host and the host checks that branch out). So the tree is pinned by a scratch branch of this
# script's own, always at the first-parent sha; the tree is the same one a detached checkout would
# give, and the sha is read back out of the checkout afterwards instead of being asserted here.
#
# It refuses anything it did not create: a path that holds a checkout of another repository, a dirty
# checkout, or a branch somebody else has checked out. `git checkout -B` over a lane's worktree is
# how a lane's work is lost.
function Sync-BaselineWorktree {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Sha,
        [Parameter(Mandatory)][string] $CommonDir,
        [Parameter(Mandatory)][string] $Repo,
        [string] $BranchName = 'camel-baseline'
    )

    $ourBranch = $BranchName
    $short = $Sha.Substring(0, 9)
    try { $target = [System.IO.Path]::GetFullPath($Path) } catch { return "the baseline worktree path '$Path' is not a path" }
    $repoFull = (Resolve-Path -LiteralPath $Repo -ErrorAction SilentlyContinue).Path
    if ($repoFull -and $target -eq $repoFull) {
        # The one mistake this whole step exists to prevent, caught before it is made.
        return "the baseline worktree and the merge position are the same checkout ($repoFull); the baseline has to be measured on another tree, or the comparison is one tree against itself and every set comes out empty"
    }

    if (Test-Path -LiteralPath $target) {
        if (-not (Test-Path -LiteralPath (Join-Path $target '.git'))) {
            # An empty directory is not a worktree either, but it is harmless to adopt it.
            $entries = @(Get-ChildItem -LiteralPath $target -Force -ErrorAction SilentlyContinue)
            if ($entries.Count -gt 0) {
                return "$target is not a git checkout and holds $($entries.Count) entr(y/ies); the baseline is measured in a worktree of this repository, so name an empty path with -BaselineWorktree"
            }
        } else {
            $there = "$(& git -C $target rev-parse --git-common-dir 2>$null)".Trim()
            if (-not $there) {
                return "$target holds a .git that git cannot read; the baseline is measured in a worktree of this repository"
            }
            if (-not [System.IO.Path]::IsPathRooted($there)) { $there = Join-Path $target $there }
            $thereFull = (Resolve-Path -LiteralPath $there -ErrorAction SilentlyContinue).Path
            $commonFull = (Resolve-Path -LiteralPath $CommonDir -ErrorAction SilentlyContinue).Path
            if (-not $thereFull -or -not $commonFull -or $thereFull -ne $commonFull) {
                return "$target is a checkout of another repository ($there); the baseline is measured in a worktree of $Repo"
            }
            $dirty = @(& git -C $target status --porcelain 2>$null)
            if ($dirty.Count -gt 0) {
                return "$target has $($dirty.Count) uncommitted change(s); the baseline must be measured on a clean checkout of $short, and moving it would discard them"
            }
            $onBranch = "$(& git -C $target symbolic-ref -q --short HEAD 2>$null)".Trim()
            if ($onBranch -ne $ourBranch) {
                # A labelled worktree belongs to a lane or to the merge position; this step only owns
                # the one branch it made. Refusing here is what keeps a mistyped -BaselineWorktree
                # from checking somebody's branch backwards.
                $what = if ($onBranch) { "the branch $onBranch checked out" } else { 'a detached HEAD' }
                return "$target has $what; the baseline worktree is the one this script makes, on $ourBranch at the first parent, and moving it would discard a checkout somebody is working in"
            }
            $at = "$(& git -C $target rev-parse HEAD 2>$null)".Trim()
            if ($at -ne $Sha) {
                & git -C $target checkout -q -B $ourBranch $Sha 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { return "could not move $ourBranch in $target to $short" }
                $at = "$(& git -C $target rev-parse HEAD 2>$null)".Trim()
                if ($at -ne $Sha) { return "$target did not end up at $short (it is at $($at.Substring(0, [Math]::Min(9, $at.Length))))" }
            }
            return $null
            $at = "$(& git -C $target rev-parse HEAD 2>$null)".Trim()
            if ($at -ne $Sha) {
                & git -C $target checkout -q -f --detach $Sha 2>&1 | Out-Null
                if ($LASTEXITCODE -ne 0) { return "could not check $target out at $short" }
                $at = "$(& git -C $target rev-parse HEAD 2>$null)".Trim()
                if ($at -ne $Sha) { return "$target did not end up at $short (it is at $($at.Substring(0, [Math]::Min(9, $at.Length))))" }
            }
            return $null
        }
    }

    $parent = Split-Path -Parent $target
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    & git -C $Repo worktree add -q -B $ourBranch $target $Sha 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return "git worktree add -B $ourBranch $target $short failed (a branch of that name checked out elsewhere is the usual cause)" }
    $at = "$(& git -C $target rev-parse HEAD 2>$null)".Trim()
    if ($at -ne $Sha) { return "the new worktree $target is at $($at.Substring(0, [Math]::Min(9, $at.Length))), not $short" }
    return $null
}

# Three trees and three jobs, kept apart on purpose:
#   $toolRoot         the checkout this SCRIPT lives in -- the instruments (the gate entry, the
#                     comparer, the record writer) come from here, because the tree the merge makes
#                     is under judgement and a judged tree must not carry the instrument that judges
#                     it. `origin/<target>` does not carry these files at all, which is how the first
#                     full run failed with "Cannot find module ...merge-record.mjs"; an older comparer
#                     would have accepted `--accept` and ignored `--tree`, seeding a baseline nothing
#                     could check, and that run would have read as green.
#   $MainCheckout     the merge position: the seat checkout whose branch is the target, where the
#                     merge commit is made, which the gates measure, and from which the push is made
#                     (the hook names that tree for `develop`).
#   $BaselineWorktree the throwaway tree the baseline is measured in, held at the first parent on a
#                     branch of its own.
# The header check and the plan selection are the other way round on purpose: they are about the
# tree's own files and its own selection rules, so they run the tree's own copies.
$toolRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# The list that decides what docs-only means, in its own file so a test can read it without this
# script's merge, host and gates around it. Dot-sourced: it defines two functions and nothing else.
. (Join-Path $PSScriptRoot 'merge-lane-docs.ps1')
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot 'scripts\merge-readings.mjs'))) {
    Write-Host ''
    Write-Host 'STOPPED: this script is not in a checkout that carries the merge instruments (exit 2, nothing measured)' -ForegroundColor Yellow
    Write-Host "  $toolRoot has no scripts/merge-readings.mjs. The comparer and the record writer are run from the checkout this script lives in, so run it from the merge position's own checkout after pulling."
    Write-Host ''
    Write-Host "Nothing was pushed, and local $Target is where it was."
    exit 2
}

# --- the push, and the one recovery a refused push may make -------------------
#
# A merge takes minutes -- the baseline seed alone was 480 s of one 11-minute run -- and documentation
# is pushed to the target branch while it runs, by the lane that writes pages and by no merge seat.
# The push is then refused as a non-fast-forward, and until this existed the whole run was repeated:
# three times in one night, eleven minutes each, for commits that touch no code.
#
# The recovery re-lands the merge on the tip that landed and re-reads ONLY the plans those commits
# select. What decides that is the FILE PATH and nothing else: every path the new commits touch is
# either one of the four places a page is written, or it is not. There is no flag that says "this
# looks harmless", and there is no judgement about impact anywhere in the branch below -- the one
# thing a tool is bad at is deciding that a change cannot matter.
#
# The four are the documentation the merge seats actually see land: the backlog, the dated plans,
# the reports and the questions. `internal/` is NOT the boundary: `internal/tools/merge-lane.ps1` is
# under it and two plans read it (feat/docs_links.plan.toml by the walkers, and the file is code by
# any reading), so a prefix rule on `internal/` would call a code change documentation and re-use
# readings across it while every line of the output looked right. That is the mistake the comment
# inside the function exists to keep out; it was made once already, on the lane that tried this
# before this one.
#
# Which of the changed paths are documentation, and which are not. Both halves are returned: the
# refusal has to name the paths that are code, because "touches code" with no names sends the reader
# to `git diff` to find out which, and the next run is a careful merge that has to know.
function Split-ChangeByPath {
    param([string[]] $Paths = @())

    # Inside the function, not at script scope: the test reads this function out of this file's syntax
    # tree and runs it, and a definition that depends on a variable the caller happened to set is a
    # definition the test would measure half of.
    $docsRoots = @('internal/plans/', 'internal/reports/', 'internal/questions/')
    $docsFiles = @('internal/backlog.md')

    $docs = @()
    $code = @()
    foreach ($raw in @($Paths)) {
        # One separator, so a path written with the other one is the same path: git answers with `/`
        # on every platform, and a list assembled by hand on Windows would not.
        $path = "$raw".Trim().Replace('\', '/')
        if (-not $path) { continue }
        $isDoc = ($docsFiles -contains $path) -or (@($docsRoots | Where-Object { $path.StartsWith($_) }).Count -gt 0)
        if ($isDoc) { $docs += $path } else { $code += $path }
    }
    return @{
        Documentation = $docs
        Code          = $code
        # A change of nothing is not documentation: an empty diff is a rejection with nothing to
        # re-land, and reading it as "docs-only" would claim a recovery that moved no file.
        DocsOnly      = ($code.Count -eq 0 -and $docs.Count -gt 0)
    }
}

# The one line every rejection prints, whether or not this run recovers. One shape, so a reader (or a
# script) can grep `^rebase:` and get both ends of the move and what was done about it on one line.
function Format-RebaseLine {
    param([string] $OldRemote, [string] $NewRemote, [int] $Files, [bool] $DocsOnly, [int] $ReRan)

    # Nine characters, the width every other line in this script prints a sha at: one width for one
    # reading, so two lines about the same commit cannot look like two commits.
    $short = {
        param($s)
        $t = "$s".Trim()
        if ($t.Length -gt 9) { $t.Substring(0, 9) } else { $t }
    }
    return ('rebase: {0} -> {1}, {2} file(s), {3}, re-ran {4} plan(s)' -f `
            (& $short $OldRemote), (& $short $NewRemote), $Files,
        $(if ($DocsOnly) { 'docs-only' } else { 'touches code' }), $ReRan)
}

# The plans a list of changed paths selects -- the same selection the merge stage above uses for the
# change it brings, asked for by FILE LIST rather than by a base branch. The list is the difference
# between two commits the checkout has already fetched, so `--changed-files` is what answers it and
# `git diff` is not needed twice: a base ref would name a tree this checkout may not have.
function Get-PlansForChangedFiles {
    param([string] $Checkout, [string] $ListFile)

    $out = @(& node (Join-Path $Checkout 'scripts\test.js') --changed --changed-files $ListFile --list 2>&1)
    $exit = $LASTEXITCODE
    $prefix = $Checkout.TrimEnd('\') + '\'
    $targets = @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -and $_.StartsWith($prefix) } |
        ForEach-Object { $_.Substring($prefix.Length) -replace '\\', '/' })
    return @{
        Exit    = $exit
        Targets = $targets
        # The selection widens when no rule matches a changed file. That is not "no plan": it is "the
        # rules do not know what reads this", and the widened set is the whole fast layer -- a full
        # run wearing the name of a partial one.
        Widened = (@($out | Where-Object { "$_" -match '^\(the whole fast layer' }).Count -gt 0)
        Text    = (@($out | ForEach-Object { "$_" }) -join "`n")
    }
}

# One run of a named plan set on the tree that is about to be pushed, read the way the merge stage
# reads its own: exit code, rows, and the rows whose verdict is fail. No case-by-case comparison
# against a baseline here, and the difference is deliberate: the baseline is keyed by the tree the
# merge lands on and covers the plans THAT merge selected, and the point of a partial re-run is that
# these are different plans. What this answers is the question the recovery can answer -- do the
# plans that read the files that landed still pass on the tree we are about to push -- and the
# conclusion line says exactly that much and no more.
function Invoke-PlansOnTree {
    param([string] $Checkout, [string[]] $PlanTargets)

    $started = Get-Date
    $out = @(& pwsh -NoProfile -File (Join-Path $Checkout 'internal\tools\camel-remote.ps1') `
        -Path $Checkout -Plan $PlanTargets -ListKnown -NoGpuWatch 2>&1)
    $exit = $LASTEXITCODE
    return @{
        Exit    = $exit
        Rows    = @($out | Where-Object { "$_".TrimStart().StartsWith('{') }).Count
        Reds    = @($out | Where-Object { "$_".TrimStart().StartsWith('{') -and "$_" -match '"verdict"\s*:\s*"fail"' }).Count
        Seconds = ((Get-Date) - $started).TotalSeconds
        Text    = (@($out | ForEach-Object { "$_" }) -join "`n")
    }
}

# Whether a push failure is the tip moving under us. `[remote rejected]` is not this: that is a hook
# or a policy refusing the commit, and re-landing it on a newer tip would be repeating a refused
# action with extra steps. The three phrasings below are git's own for a ref that is not a
# fast-forward, whichever side noticed.
function Test-PushRejection {
    param([string] $Text)
    return ("$Text" -match 'non-fast-forward|fetch first|\[rejected\]|stale info')
}

# Push, and if the branch moved, make the one recovery this run is allowed. Returns a result instead
# of stopping: the caller prints `.Lines` and turns `.Kind` into the sentence that belongs to it, so
# every stop still reads the same state (and so this can be run against a throwaway repository).
#
#   Ok          pushed, with or without the recovery
#   Kind        'pushed' | 'recovered' | 'touches code' | 'no plan' | 'red' | 'conflict'
#               | 'second rejection' | 'tip did not move' | 'fetch failed' | 'failed'
#   Sha         the commit that is on origin after this (the merge commit, or the re-landed one)
#   Shas        old, new (the two ends of the refusal)
#   Targets     the plans the recovery re-ran; Rows and Reds are their readings
#   Lines       already indented where indentation belongs; the caller prints them verbatim
function Invoke-PushWithRecovery {
    param(
        [Parameter(Mandatory)][string] $Tree,
        [Parameter(Mandatory)][string] $Target,
        # The plans the changed files select, run on the re-landed tree. Throws nothing: returns
        # `.Targets` (empty when nothing could be named), `.Exit`, `.Rows`, `.Reds`.
        [Parameter(Mandatory)][scriptblock] $ReRun
    )

    $pushOnce = {
        $out = @(& git -C $Tree push origin "HEAD:$Target" 2>&1)
        return @{ Exit = $LASTEXITCODE; Text = (@($out | ForEach-Object { "$_" }) -join "`n") }
    }
    $head = { "$(& git -C $Tree rev-parse --verify 'HEAD^{commit}' 2>$null)".Trim() }
    # The commit this push expects origin to be on: the merge commit's first parent at entry (the
    # tree the merge lands on, which the start point asserted origin held), and after a re-land the
    # tip that was merged in. Read once and moved once, instead of read as `HEAD^1` twice -- after the
    # re-land `HEAD^1` is the merge commit, so the second reading would diff a lane's whole change
    # against the tip and call it code.
    $headAtEntry = (& $head)
    $basedOn = "$(& git -C $Tree rev-parse 'HEAD^1' 2>$null)".Trim()

    # --- what this push would publish, read before it is attempted ---------------
    # The ruling's shape (D-1124-78): the commit that would travel must be a MERGE whose first parent
    # is origin/$Target's tip as of this fetch. It is read here and not remembered from the merge
    # stage, because the reading is about what is published and the merge stage is minutes older.
    #
    # A `single parent` is not a race and nothing here recovers it: a lane commit that would enter the
    # target directly has no lane on a second parent, and the first-parent chain cannot tell it from
    # an arrival. A `first parent` that is not the tip is the target having moved -- the case this
    # function exists for -- and it is handed to the documentation-only rule below exactly as a
    # refused push is. The commit that rule re-lands is made on the new tip, so this check passes on
    # the second read as well.
    $shape = Test-PushFirstParent -Tree $Tree -Target $Target -Sha $headAtEntry
    if ($shape.Kind -eq 'single parent' -or $shape.Kind -eq 'fetch failed' -or $shape.Kind -eq 'no tip') {
        return @{
            Ok = $false
            Kind = $(if ($shape.Kind -eq 'single parent') { 'not a merge' } else { 'failed' })
            Recovered = $false; Sha = (& $head)
            Targets = @(); Rows = 0; Reds = 0; Shas = @($shape.FirstParent, $shape.Tip); Tries = 0
            Lines = @($shape.Message)
        }
    }

    # A rejection is read, never retried blind. A failure that is not a rejection is the old shape --
    # a network or a lock -- and keeps the old three attempts with four seconds between them. THOSE
    # THREE ARE THIS LOOP'S ALONE: the second push below is one attempt with no retry, so the most a
    # run can send is three attempts when nothing was refused, or one attempt plus one recovery plus
    # one attempt when a target moved. There is no product of two loops here.
    $attempt = @{ Exit = 1; Text = '' }
    $tries = 0
    if ($shape.Kind -eq 'arrived') {
        $attempt = & $pushOnce
        $tries = 1
        while ($attempt.Exit -ne 0 -and -not (Test-PushRejection -Text $attempt.Text) -and $tries -lt 3) {
            Start-Sleep -Seconds 4
            $tries++
            $attempt = & $pushOnce
        }
        if ($attempt.Exit -eq 0) {
            return @{
                Ok = $true; Kind = 'pushed'; Recovered = $false; Sha = (& $head)
                Targets = @(); Rows = 0; Reds = 0; Shas = @()
                Lines = @("pushed HEAD:$Target ($(& $head))")
            }
        }
        if (-not (Test-PushRejection -Text $attempt.Text)) {
            return @{
                Ok = $false; Kind = 'failed'; Recovered = $false; Sha = (& $head)
                Targets = @(); Rows = 0; Reds = 0; Shas = @(); Tries = $tries
                Lines = @("git push failed after $tries attempt(s)", ($attempt.Text -split "`n" | Where-Object { $_ }))
            }
        }
    }

    # --- the branch moved: what landed, and is it documentation -----------------
    $fetch = @(& git -C $Tree fetch origin --prune 2>&1)
    if ($LASTEXITCODE -ne 0) {
        return @{
            Ok = $false; Kind = 'fetch failed'; Recovered = $false; Sha = (& $head)
            Targets = @(); Rows = 0; Reds = 0; Shas = @()
            Lines = @('git fetch failed after the push was refused: what landed cannot be read, so nothing was re-landed and nothing was pushed.',
                ($fetch | ForEach-Object { "$_" }))
        }
    }
    # The tip this push is based on and the new tip origin holds. Two shas of two commits this
    # checkout holds -- the difference between them is the work that arrived.
    $old = $basedOn
    $new = "$(& git -C $Tree rev-parse --verify "refs/remotes/origin/$Target^{commit}" 2>$null)".Trim()
    if (-not $old -or -not $new) {
        return @{
            Ok = $false; Kind = 'tip did not move'; Recovered = $false; Sha = (& $head)
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Lines = @("the two ends of the refusal could not be read (the commit this push is based on reads '$old', origin/$Target reads '$new'), so what landed is unknown.", ($attempt.Text -split "`n" | Where-Object { $_ }))
        }
    }
    if ($old -eq $new) {
        # Nothing arrived, so there is no newer tip to re-land on and this is not the race this
        # recovery exists for. Said plainly instead of printing a `rebase:` line with nothing in it.
        return @{
            Ok = $false; Kind = 'tip did not move'; Recovered = $false; Sha = (& $head)
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Lines = @("the push was refused but origin/$Target is still $($old.Substring(0,9)), the commit this merge lands on: nothing arrived to re-land, so the refusal is not a non-fast-forward and this run does not recover from it.",
                ($attempt.Text -split "`n" | Where-Object { $_ }))
        }
    }

    $changed = @(& git -C $Tree diff --name-only "$old..$new" 2>$null) |
        ForEach-Object { "$_".Trim() } | Where-Object { $_ }
    $split = Split-ChangeByPath -Paths $changed

    if (-not $split.DocsOnly) {
        # Code, or a mix: a partial re-run of this tree is not a reading about it, and re-running the
        # whole layer is exactly what this recovery exists to avoid. So it stops here and leaves the
        # decision where it belongs -- a full run is the caller's, taken deliberately.
        return @{
            Ok = $false; Kind = 'touches code'; Recovered = $false; Sha = (& $head)
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Files = $changed
            Code = $split.Code
            Lines = @((Format-RebaseLine -OldRemote $old -NewRemote $new -Files $changed.Count -DocsOnly $false -ReRan 0),
                "  the commits that landed while this merge was read touch $($split.Code.Count) file(s) that are not documentation: $(($split.Code | Select-Object -First 8) -join ', ')$(if ($split.Code.Count -gt 8) { " and $($split.Code.Count - 8) more" } else { '' })") +
                @(if ($shape.Kind -eq 'first parent') { @($shape.Message | ForEach-Object { "  $_" }) } else { @() })
        }
    }

    # --- documentation only: re-land it in the shape an arrival must have --------
    # The ruling's recipe (D-1124-78), not "merge the new tip into the merge": the seat's branch is
    # moved to the new tip and the merge this run made -- the prepared merge, whose rows and clang
    # result are the reading -- is merged INTO it. The commit that travels then has origin/$Target's
    # tip as its first parent, which is what makes it an arrival. Merging the tip into the merge
    # commit would put the merge commit first and reproduce the chain the ruling is about: same tree,
    # wrong order, and the order is the thing that was wrong.
    $landedSubjects = @(& git -C $Tree log --oneline "$old..$new" 2>$null) | ForEach-Object { "$_".Trim() } | Where-Object { $_ }
    $body = @()
    $body += "Landed on origin/$Target while this merge was read, documentation only, so they are merged here instead of being read again:"
    foreach ($line in $landedSubjects) { $body += "  $line" }
    $body += 'Files:'
    foreach ($file in $split.Documentation) { $body += "  $file" }
    $mergeMessage = "merge $Branch at $($headAtEntry.Substring(0, 9)) into origin/$Target at $($new.Substring(0, 9)): the documentation that landed while this merge was read`n`n" + ($body -join "`n")
    # The branch the seat's checkout is on: the re-land moves THAT branch to the new tip, so the
    # checkout ends on the commit this run pushes. A detached HEAD has no name to move, so this run
    # gives it one -- the seat's identity matters less than the pushed commit being reachable.
    $seatBranch = "$(& git -C $Tree symbolic-ref -q --short HEAD 2>$null)".Trim()
    if (-not $seatBranch) { $seatBranch = 'camel-reland' }
    $move = @(& git -C $Tree checkout -q -B $seatBranch $new 2>&1)
    if ($LASTEXITCODE -ne 0) {
        return @{
            Ok = $false; Kind = 'conflict'; Recovered = $false; Sha = $headAtEntry
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Files = $changed
            Lines = @((Format-RebaseLine -OldRemote $old -NewRemote $new -Files $changed.Count -DocsOnly $true -ReRan 0),
                "  the seat's branch could not be moved to origin/$Target ($($new.Substring(0,9))) to re-land the merge on it -- $(($move | ForEach-Object { "$_" }) -join ' '). Nothing was pushed.")
        }
    }
    $merge = @(& git -C $Tree merge --no-ff -m $mergeMessage $headAtEntry 2>&1)
    $mergeExit = $LASTEXITCODE
    if ($mergeExit -ne 0) {
        $conflicted = @(& git -C $Tree diff --name-only --diff-filter=U 2>$null)
        # An unfinished merge left in the checkout is the state that made `merge-lane` refuse the next
        # run, so it is aborted here -- and the abort is not a repair of the conflict, it is leaving
        # the tree as this run found it. The abort leaves the branch on the new tip, so the branch is
        # put back on the merge commit: "nothing was pushed" has to mean the checkout is where it was.
        if ("$(& git -C $Tree rev-parse -q --verify MERGE_HEAD 2>$null)".Trim()) {
            & git -C $Tree merge --abort 2>&1 | Out-Null
        }
        & git -C $Tree checkout -q -B $seatBranch $headAtEntry 2>&1 | Out-Null
        return @{
            Ok = $false; Kind = 'conflict'; Recovered = $false; Sha = (& $head)
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Files = $changed
            Lines = @((Format-RebaseLine -OldRemote $old -NewRemote $new -Files $changed.Count -DocsOnly $true -ReRan 0),
                "  re-landing the merge on origin/$Target conflicted in $($conflicted.Count) file(s): $($conflicted -join ', '). The merge was aborted and $seatBranch is back on the merge commit; nothing was pushed.")
        }
    }
    $reLanded = (& $head)
    # A merge OF THE PREPARED MERGE INTO THE NEW TIP: first parent is origin/$Target at $new, second is
    # the merge this run made. This is the shape the pre-push reading below accepts, and the reason
    # the recovery is not that reading's exception.
    $parents = @("$(& git -C $Tree rev-list --parents -n 1 HEAD 2>$null)".Trim() -split '\s+' | Where-Object { $_ })
    if ($parents.Count -lt 3 -or $parents[1] -ne $new -or $parents[2] -ne $headAtEntry) {
        return @{
            Ok = $false; Kind = 'conflict'; Recovered = $false; Sha = $reLanded
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Lines = @((Format-RebaseLine -OldRemote $old -NewRemote $new -Files $changed.Count -DocsOnly $true -ReRan 0),
                "  the re-landed commit $($reLanded.Substring(0,9)) is not a merge of the merge this run made ($($headAtEntry.Substring(0,9))) into origin/$Target ($($new.Substring(0,9))) -- its parents are $($parents -join ', ') -- so it is not the tree the recovery describes and nothing was pushed.")
        }
    }

    $reRunReading = & $ReRun $changed
    $targets = @($reRunReading.Targets | Where-Object { $_ })
    # From here the push is based on the tip that was just merged in, and the second refusal's two
    # ends are measured from it (not from `HEAD^1`, which is now the merge commit this run made).
    $basedOn = $new
    $rebaseLine = Format-RebaseLine -OldRemote $old -NewRemote $new -Files $changed.Count -DocsOnly $true -ReRan $targets.Count
    if ($targets.Count -eq 0) {
        # No plan could be named: either the selection widened (then it is the whole fast layer and
        # not a partial re-run) or it answered nothing. Either way nothing measures the re-landed
        # tree, and a push now would be a push of a tree no plan read in this run.
        return @{
            Ok = $false; Kind = 'no plan'; Recovered = $false; Sha = $reLanded
            Targets = @(); Rows = 0; Reds = 0; Shas = @($old, $new)
            Files = $changed
            Lines = @($rebaseLine,
                "  the documentation that landed selected no plan this run could re-run, so the re-landed tree $($reLanded.Substring(0,9)) was not read by any plan. Nothing was pushed.",
                # Named, not summarised: "no plan" has two causes -- nothing was affected, and no rule
                # was written -- and they are told apart by looking at these paths, not by trusting the
                # sentence. Only the second is repaired by writing a rule, and a widened selection
                # looks like a measurement in every line but this one.
                ("  the files that landed: " + $(if ($changed.Count -le 8) { $changed -join ', ' } else { (($changed | Select-Object -First 8) -join ', ') + ", and $($changed.Count - 8) more" }) + " -- none of them is named by a rule in test/plan-selection.json"),
                (($reRunReading.Text -split "`n") | Where-Object { $_ } | Select-Object -Last 3))
        }
    }
    if ($reRunReading.Exit -ne 0 -or $reRunReading.Reds -gt 0) {
        # A red is a red wherever it is found: the plans that read the files that landed cannot go
        # red on the tree we are about to push and have that push happen.
        return @{
            Ok = $false; Kind = 'red'; Recovered = $false; Sha = $reLanded
            Targets = $targets; Rows = $reRunReading.Rows; Reds = $reRunReading.Reds; Shas = @($old, $new)
            Files = $changed
            Lines = @($rebaseLine,
                "  the plans that read what landed are red on the re-landed tree $($reLanded.Substring(0,9)) -- $($reRunReading.Reds) failing case(s) of $($reRunReading.Rows), camel-remote exit $($reRunReading.Exit). The re-land is on this checkout's HEAD and nothing was pushed.",
                (($reRunReading.Text -split "`n") | Where-Object { $_ } | Select-Object -Last 6))
        }
    }

    # --- push again, once, after the same reading ------------------------------
    # The re-land was made on the tip read above; between that read and this push the tip can move
    # again, and then the commit about to travel no longer has it as a first parent. The shape is
    # read here for exactly the reason it is read before the first push -- this is the push that
    # publishes a commit made after a target moved, and it is where the ruling's chain was broken
    # tonight. A second move inside one run is a stop, not another recovery: the run gets one.
    $shape2 = Test-PushFirstParent -Tree $Tree -Target $Target -Sha $reLanded
    if ($shape2.Kind -ne 'arrived') {
        $old2 = $new
        $new2 = $shape2.Tip
        $changed2 = if ($old2 -and $new2 -and $old2 -ne $new2) {
            @(& git -C $Tree diff --name-only "$old2..$new2" 2>$null) | ForEach-Object { "$_".Trim() } | Where-Object { $_ }
        } else { @() }
        $split2 = Split-ChangeByPath -Paths $changed2
        return @{
            Ok = $false; Kind = 'second rejection'; Recovered = $false; Sha = $reLanded
            Targets = $targets; Rows = $reRunReading.Rows; Reds = $reRunReading.Reds; Shas = @($old2, $new2)
            Files = $changed2
            Lines = @($rebaseLine,
                (Format-RebaseLine -OldRemote $old2 -NewRemote $new2 -Files $changed2.Count -DocsOnly $split2.DocsOnly -ReRan 0),
                "  the re-landed commit $($reLanded.Substring(0,9)) was made on origin/$Target at $($new.Substring(0,9)), and the tip is $($new2.Substring(0,9)) now: $(if ($shape2.Kind -eq 'first parent') { 'its first parent is no longer the tip' } else { $shape2.Message[0] }). Nothing was pushed.")
        }
    }
    $second = & $pushOnce
    if ($second.Exit -eq 0) {
        return @{
            Ok = $true; Kind = 'recovered'; Recovered = $true; Sha = (& $head)
            Targets = $targets; Rows = $reRunReading.Rows; Reds = $reRunReading.Reds; Shas = @($old, $new)
            Files = $changed
            Lines = @($rebaseLine,
                "  re-landed $($changed.Count) documentation file(s) on origin/$Target ($($new.Substring(0,9))) as $($reLanded.Substring(0,9)), re-ran $($targets.Count) plan(s): $($targets -join ', ') ($($reRunReading.Rows) case(s), $($reRunReading.Reds) red)",
                "pushed HEAD:$Target ($(& $head))")
        }
    }

    # A second refusal ends the run. The one recovery has been made; making it again is the same
    # action a third time, which is not persistence. The shas are the whole report, so the next
    # reader starts from the two commits rather than from a suggestion.
    if (-not (Test-PushRejection -Text $second.Text)) {
        return @{
            Ok = $false; Kind = 'failed'; Recovered = $false; Sha = (& $head)
            Targets = $targets; Rows = $reRunReading.Rows; Reds = $reRunReading.Reds; Shas = @($old, $new); Tries = 1
            Files = $changed
            Lines = @($rebaseLine, "git push failed after 1 attempt(s)", ($second.Text -split "`n" | Where-Object { $_ }))
        }
    }
    $old2 = $basedOn
    $null = @(& git -C $Tree fetch origin --prune 2>&1)
    $new2 = "$(& git -C $Tree rev-parse --verify "refs/remotes/origin/$Target^{commit}" 2>$null)".Trim()
    $changed2 = if ($old2 -and $new2 -and $old2 -ne $new2) {
        @(& git -C $Tree diff --name-only "$old2..$new2" 2>$null) | ForEach-Object { "$_".Trim() } | Where-Object { $_ }
    } else { @() }
    $split2 = Split-ChangeByPath -Paths $changed2
    return @{
        Ok = $false; Kind = 'second rejection'; Recovered = $false; Sha = (& $head)
        Targets = $targets; Rows = $reRunReading.Rows; Reds = $reRunReading.Reds; Shas = @($old2, $new2)
        Files = $changed2
        # Both lines: the first refusal's (with the recovery that was made) and this one, so what the
        # run did and what it then met are on one record. The stop the caller prints adds the shas.
        Lines = @($rebaseLine,
            (Format-RebaseLine -OldRemote $old2 -NewRemote $new2 -Files $changed2.Count -DocsOnly $split2.DocsOnly -ReRan 0))
    }
}

# The green line of a run that recovered. Its job is the one thing a partial re-run can lie about:
# coverage. So it names the plans that were re-run and the tree the merge's own readings belong to,
# and it says in as many words that no layer was measured -- a reader of this line must not be able to
# take it for a full measurement, which is the same defect as the `0 fixed, 0 new, 0 changed` green
# the row stage exists to make impossible.
function Format-RecoveryConclusion {
    param(
        [string] $Branch, [string] $Head, [string] $Target, [string] $Local,
        [string[]] $Targets, [int] $Files, [string] $MeasuredTree
    )

    $short = {
        param($s)
        $t = "$s".Trim()
        if ($t.Length -gt 9) { $t.Substring(0, 9) } else { $t }
    }
    return ('merged {0} at {1}; {2} is {3} (partial re-run: {4} plan(s) -- {5} -- selected by the {6} documentation file(s) that landed on origin/{2} while this merge was read; the plans this merge selected were measured on tree {7}, and no layer was measured)' -f `
            $Branch, $Head, $Target, (& $short $Local), @($Targets).Count, (@($Targets) -join ', '), $Files, (& $short $MeasuredTree))
}

# The two trees a merge needs, both read before anything is done in either. A stop here has touched
# no repository: no fetch, no lock, no branch, no build.
$mergeCheckout = Resolve-MergeCheckout -Given $MainCheckout -LocalConfig $LocalConfig -ToolsRoot $PSScriptRoot
if ($mergeCheckout.Problem) {
    Write-Host ''
    Write-Host 'STOPPED: the checkout a merge lands in is not named (exit 2, nothing was read and nothing was merged)' -ForegroundColor Yellow
    Write-Host "  $($mergeCheckout.Problem)"
    Write-Host ''
    exit 2
}
$MainCheckout = $mergeCheckout.Path

# The baseline tree is read here too, and not at the point it is used: a run that cannot name it would
# otherwise merge, build and measure before finding out, which is the eleven minutes this script
# exists to stop spending twice. Read the same way, refused the same way, stopped the same way.
$baselineRootCheck = Resolve-BaselineRoot -Given $BaselineRoot -LocalConfig $LocalConfig -ToolsRoot $PSScriptRoot
if ($baselineRootCheck.Problem) {
    Write-Host ''
    Write-Host 'STOPPED: the directory the baseline checkouts live under is not usable (exit 2, nothing was read and nothing was merged)' -ForegroundColor Yellow
    Write-Host "  $($baselineRootCheck.Problem)"
    Write-Host ''
    exit 2
}
$BaselineRoot = $baselineRootCheck.Path
if (-not $BaselineRoot) {
    # No root: the legacy shape. Its path is still required, and still refused when it cannot be read.
    $baselineCheckout = Resolve-BaselineWorktree -Given $BaselineWorktree -LocalConfig $LocalConfig -ToolsRoot $PSScriptRoot
    if ($baselineCheckout.Problem) {
        Write-Host ''
        Write-Host 'STOPPED: the tree the baseline is measured in is not named (exit 2, nothing was read and nothing was merged)' -ForegroundColor Yellow
        Write-Host "  $($baselineCheckout.Problem)"
        Write-Host '  Set `baselineRoot` to a directory and each tip is measured once for every seat that lands on it; or set `baselineWorktree` to one fixed tree, the older shape.'
        Write-Host ''
        exit 2
    }
    $BaselineWorktree = $baselineCheckout.Path
}

Push-Location $MainCheckout
try {
    # The two directories that are not `$MainCheckout\.git` once the merge position is a linked
    # worktree: the per-worktree git dir (the index, MERGE_HEAD) and the common one the worktrees
    # share (where the baseline cache belongs, because the cache is about develop commits and every
    # worktree of this repository sees the same ones). Hard-coding `\.git\` read the wrong place the
    # moment a merge was tried from anywhere but the main checkout.
    $gitDir = "$(& git rev-parse --absolute-git-dir 2>$null)".Trim()
    $commonDir = "$(& git rev-parse --git-common-dir 2>$null)".Trim()
    if (-not $gitDir -or -not $commonDir) {
        Stop-With 'this is not a git checkout' "git rev-parse did not answer for $MainCheckout."
    }
    if (-not [System.IO.Path]::IsPathRooted($commonDir)) { $commonDir = Join-Path $MainCheckout $commonDir }

    # A reader that refreshes the index takes the lock and a merge then refuses. Clearing it is only
    # safe when nothing is writing: deleting the lock under a git that is mid-write corrupts the index,
    # which is worse than a refused merge. So the lock is cleared only when it is older than two
    # minutes AND no git process is running at all. Otherwise this stops and prints the command lines,
    # because the answer to "who holds it" is something a person has to see, not something to guess at.
    $lock = Join-Path $gitDir 'index.lock'
    if (Test-Path $lock) {
        $ageSeconds = ((Get-Date) - (Get-Item $lock).LastWriteTime).TotalSeconds
        $gitProcs = @(Get-CimInstance Win32_Process -Filter "Name='git.exe'" -ErrorAction SilentlyContinue)

        if ($gitProcs.Count -gt 0) {
            Write-Host ''
            Write-Host "STOPPED: .git\index.lock exists and $($gitProcs.Count) git process(es) are running" -ForegroundColor Red
            foreach ($proc in $gitProcs) {
                $cmd = "$($proc.CommandLine)".Trim()
                if ($cmd -eq "") { $cmd = "(the command line is not readable for this process; the pid and the start time are below)" }
                $started = try { $proc.CreationDate } catch { "" }
                Write-Host "  pid=$($proc.ProcessId)  started=$started  $cmd"
            }
            Write-Host ''
            Write-Host '  Not deleting the lock: a git that is mid-write would corrupt the index, which is'
            Write-Host '  worse than a refused merge. Find which of these is a reader that refreshes the index'
            Write-Host '  (a status call is enough) and let it finish, or stop it yourself.'
            Write-Host ''
            Write-GitState
            exit 1
        }

        if ($ageSeconds -lt 120) {
            Write-Host ''
            Write-Host "STOPPED: .git\index.lock is $([int]$ageSeconds) s old and no git process is running" -ForegroundColor Red
            Write-Host '  A lock younger than two minutes with no holder is most likely a git that has just'
            Write-Host '  exited; two minutes is longer than any single git command takes. Wait and retry.'
            Write-Host ''
            Write-GitState
            exit 1
        }

        Remove-Item $lock -Force
        Write-Host "  cleared an index.lock that was $([int]$ageSeconds) s old with no git process running"
    }

    # --- the start point: HEAD has to be the target's tip ------------------------
    #
    # This merges in this checkout and then pushes HEAD to `$Target`. If HEAD is not `origin/$Target`,
    # the merge commit's first parent is not the commit that is on origin: the push is refused, and the
    # refusal names something else -- the pre-push hook's records, or a non-fast-forward -- so the reader
    # goes looking in the wrong place. The expensive half is what comes next: the merge commit stays on
    # the checkout's branch, so the following run starts one more merge wrong. Six of those were left in one
    # day (952048856a, fa7d722407, 6d0af87f89, 0be4786c48, 542f52470a, 66041d0581), every one of them
    # under a message that said nothing had been committed.
    #
    # So the start point is asserted before anything is merged, and there is exactly one repair: when
    # every commit on local `develop`'s first-parent chain that origin does not have is a merge this
    # tool made and did not push, those merges have to be re-made from the right parent anyway. They
    # are kept under a tag (rule 12: nothing is dropped without a record of where it went) and local
    # `develop` is moved back. Any other commit -- a report, a fix, anything a person wrote -- stops
    # the run with nothing merged and nothing reset: which of those to keep is not a decision a merge
    # tool makes.
    & git fetch origin --prune 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Stop-With 'git fetch failed' 'A merge against a stale origin is a merge against what you think is there.'
    }

    $originTarget = "$(& git rev-parse --verify "refs/remotes/origin/$Target^{commit}" 2>$null)".Trim()
    $headTip = "$(& git rev-parse --verify 'HEAD^{commit}' 2>$null)".Trim()
    $startBranch = "$(& git symbolic-ref -q --short HEAD 2>$null)".Trim()
    $localTarget = "$(& git rev-parse --verify "refs/heads/$Target^{commit}" 2>$null)".Trim()
    if (-not $originTarget -or -not $headTip) {
        Write-Host ''
        Write-Host 'STOPPED: the start point could not be read (exit 2, no reading taken)' -ForegroundColor Yellow
        Write-Host "  HEAD reads '$(if ($headTip) { $headTip } else { 'nothing' })' and origin/$Target reads '$(if ($originTarget) { $originTarget } else { 'nothing' })'."
        Write-Host '  This is not a defect in the lane: the assertion this merge begins with cannot be made, so nothing was merged and nothing was pushed.'
        Write-Host "  What repairs it: git rev-parse HEAD and git rev-parse origin/$Target in this checkout. origin/$Target missing after a successful fetch means the branch lives on another remote, or does not exist."
        Write-Host ''
        exit 2
    }

    # `-MergeCommit`: the seat merged and resolved by hand, and this run is the four stages after the
    # merge. What replaces the merge step's own two checks is the same question asked about a commit
    # that already exists -- is this the merge this tool would have made? Three answers, all read off
    # git rather than off the seat's word: the first parent is origin/<Target> (so the push is a
    # fast-forward and the reading is about a tree that lands), the second parent is the lane's tip
    # (so the record names the lane that came in), and HEAD is the commit (so what is measured and
    # pushed is the thing that was checked). Each refusal names which one is wrong, because the
    # repair differs: a wrong first parent means the seat started from another tip, a wrong second
    # parent means another lane was merged, and a HEAD that is something else means the seat is not
    # holding the merge it says it made.
    $mergeSecondParent = ''
    if ($MergeCommit) {
        $mc = "$(& git rev-parse --verify "$MergeCommit^{commit}" 2>$null)".Trim()
        if (-not $mc) {
            Stop-With "-MergeCommit '$MergeCommit' does not resolve to a commit" 'Pass the sha (or the ref) of the merge commit the seat made; nothing was measured and nothing was pushed.' 2
        }
        $mcParents = @("$(& git rev-list --parents -n 1 $mc 2>$null)".Trim() -split '\s+' | Where-Object { $_ })
        if ($mcParents.Count -lt 3) {
            Stop-With "-MergeCommit $($mc.Substring(0, 9)) is not a merge commit" "It has $($mcParents.Count - 1) parent(s); a merge of the lane into $Target has two. Nothing was measured and nothing was pushed." 2
        }
        $mcFirst = $mcParents[1]
        $mcSecond = $mcParents[2]
        $laneTip = "$(& git rev-parse --verify "$Branch^{commit}" 2>$null)".Trim()
        if (-not $laneTip) {
            Stop-With "-MergeCommit was given with -Branch $Branch, which does not resolve to a commit" 'The second parent is compared against the lane the merge names, so the lane has to be nameable. Nothing was measured and nothing was pushed.' 2
        }
        if ($mcFirst -ne $originTarget) {
            # Two shapes land here and they are not the same mistake. The first parent is an OLDER
            # origin/$Target: the seat merged integration into itself and then made its merge on top --
            # which puts integration's commits on this branch as if they were arrivals, and makes every
            # tool that reads the first-parent chain count them again. The first parent is a commit
            # origin never had: the seat started from another base entirely. Naming which one it is
            # matters, because the repair differs (redo on the new tip, or find the right base).
            & git merge-base --is-ancestor $mcFirst $originTarget 2>$null
            $integratedTheOtherWay = ($LASTEXITCODE -eq 0)
            if ($integratedTheOtherWay) {
                Stop-With "-MergeCommit $($mc.Substring(0, 9)) has $($mcFirst.Substring(0, 9)) as its first parent, and that is an older origin/$Target, not the tip $($originTarget.Substring(0, 9))" "You are merging integration into the seat, and then merging the lane on top of that: the push would not be a fast-forward, and on the first-parent chain integration's own commits would read as arrivals from a lane. Redo the merge on the current tip -- git checkout -B tmp origin/$Target, then git merge --no-ff $Branch with 'git config rerere.enabled true' replaying the resolution you already made -- and pass that commit. Nothing was measured and nothing was pushed." 2
            }
            Stop-With "-MergeCommit $($mc.Substring(0, 9)) has $($mcFirst.Substring(0, 9)) as its first parent, not origin/$Target $($originTarget.Substring(0, 9))" "Its first parent is the tip the merge lands on, and the push is a fast-forward only from origin/$Target. Merge the lane from a checkout whose HEAD is origin/$Target and pass that commit; nothing was measured and nothing was pushed." 2
        }
        if ($mcSecond -ne $laneTip) {
            Stop-With "-MergeCommit $($mc.Substring(0, 9)) has $($mcSecond.Substring(0, 9)) as its second parent, not the tip of $Branch $($laneTip.Substring(0, 9))" 'The second parent is the lane the record names, so a merge of another lane cannot be recorded as this one. Nothing was measured and nothing was pushed.' 2
        }
        if ($headTip -ne $mc) {
            Stop-With "-MergeCommit is $($mc.Substring(0, 9)) and HEAD is $($headTip.Substring(0, 9))" 'The readings and the push are about HEAD, so HEAD has to be the merge commit that was checked. Check the commit out here, or pass the one HEAD holds; nothing was measured and nothing was pushed.' 2
        }
        $mergeSecondParent = $mcSecond
        Write-Host "merging nothing: -MergeCommit $($mc.Substring(0, 9)) is already a merge of $($mcSecond.Substring(0, 9)) into origin/$Target $($originTarget.Substring(0, 9)), and its parents and HEAD are the shape this tool would have made"
    }

    # The assertion: this checkout's HEAD is the target's recorded tip, so the merge commit's first
    # parent is the commit on origin and the push is a fast-forward. A seat works on a branch of its
    # own, so the local branch need not be named like the target; HEAD is what is asserted, and the
    # local branch (when it exists) is read beside it in the messages below.
    $rangeName = if ($localTarget) { "local $Target" } else { 'HEAD' }
    $rangeRight = if ($localTarget) { $Target } else { 'HEAD' }
    if ($headTip -ne $originTarget -and -not $MergeCommit) {
        # What the branch holds and origin does not: every commit, so the reader sees what a merge
        # brought with it as well. The judgement, though, is about develop's FIRST-PARENT chain only:
        # the commits a merge pulls in from a lane are not on that chain, were never merges this tool
        # made on develop, and print-and-refuse over them would refuse every real case (a lane commit
        # is not a merge commit, so it would always be "a person made it").
        $onlyLocal = @(& git rev-list --reverse "origin/$Target..$rangeRight" 2>$null |
            ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        $chain = @(& git rev-list --reverse --first-parent "origin/$Target..$rangeRight" 2>$null |
            ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        Write-Host ''
        if ($localTarget) {
            Write-Host "local $Target is not origin/$Target`: local $Target is $($localTarget.Substring(0,9)), origin/$Target is $($originTarget.Substring(0,9))" -ForegroundColor Red
        } else {
            Write-Host "HEAD is not origin/$Target`: HEAD is $($headTip.Substring(0,9))$(if ($startBranch) { " on $startBranch" }), origin/$Target is $($originTarget.Substring(0,9))" -ForegroundColor Red
        }
        Write-Host "  $($onlyLocal.Count) commit(s) exist only on $rangeName (git log --oneline origin/$Target..$rangeRight), $($chain.Count) of them on $Target's first-parent chain:"
        foreach ($sha in $onlyLocal) {
            $fromLane = if ($chain -notcontains $sha) { '   (came in through a merge; not on the first-parent chain)' } else { '' }
            Write-Host "    $(& git log --oneline -n 1 $sha 2>$null)$fromLane"
        }

        # Three mechanical tests, all three required, and no reading that is a guess:
        #   1. it is a merge commit (`git rev-list --parents -n 1` gives three or more fields);
        #   2. no gate record exists for it (`.git/camel-passed/<sha>.json`), so no run passed it;
        #   3. the tree it holds is exactly the tree a plain merge of its two parents produces
        #      (`git merge-tree --write-tree <first parent> <second parent>`). This is what "it merged
        #      that lane onto that develop, and nothing else" means as a comparison of trees. It is
        #      NOT `git diff <first parent> <second parent>`: a lane that is behind develop differs
        #      from it in develop's own later changes, so that comparison calls every honest merge a
        #      stranger. A merge with a hand resolution, or with anything amended into it, is a tree
        #      merge-tree does not produce, and it does not qualify -- it carries a decision nobody
        #      recorded.
        # The subject is checked as well (`merge ...`), because a commit not written as a merge was
        # not made by this tool whatever its tree looks like.
        $strangers = @()
        foreach ($sha in $chain) {
            $short = $sha.Substring(0, 9)
            $parents = @("$(& git rev-list --parents -n 1 $sha 2>$null)".Trim() -split '\s+' | Where-Object { $_ })
            if ($parents.Count -lt 3) {
                $strangers += "$short is not a merge commit ($($parents.Count - 1) parent(s)); a person made it, and nothing here may drop it"
                continue
            }
            $firstParent = $parents[1]
            $secondParent = $parents[2]
            $record = Join-Path $commonDir "camel-passed\$sha.json"
            if (Test-Path -LiteralPath $record) {
                $strangers += "$short has a gate record ($record), so a gate read it; it is work, not a leftover"
                continue
            }
            $subject = "$(& git log -1 --format=%s $sha 2>$null)".Trim()
            if ($subject -notmatch '^merge\s') {
                $strangers += "$short does not describe itself as a merge (`"$subject`")"
                continue
            }
            $commitTree = "$(& git rev-parse --verify "$sha^{tree}" 2>$null)".Trim()
            $wouldBe = @(& git merge-tree --write-tree $firstParent $secondParent 2>$null)
            $wouldBeExit = $LASTEXITCODE
            $wouldBeTree = "$($wouldBe | Select-Object -First 1)".Trim()
            # An older git without `merge-tree --write-tree` answers with an error here, and an error
            # is not a clean merge: the commit is kept (the run stops) rather than guessed about.
            if ($wouldBeExit -ne 0 -or -not $wouldBeTree -or $wouldBeTree -ne $commitTree) {
                $strangers += "$short is not the tree a plain merge of $($secondParent.Substring(0,9)) into $($firstParent.Substring(0,9)) produces (merge-tree exit $wouldBeExit), so it carries a resolution nobody recorded"
            }
        }

        if ($strangers.Count -gt 0) {
            Write-Host ''
            Write-Host "STOPPED: HEAD is not origin/$Target, and what it holds on the first-parent chain is not all merges this tool made and did not push (exit 2)" -ForegroundColor Red
            foreach ($why in $strangers) { Write-Host "  $why" }
            Write-Host ''
            Write-Host "  Not restoring ${rangeName}: which of those commits to keep is not a decision a merge tool makes."
            Write-Host "  Move $rangeName to origin/$Target yourself (after reading the commits above), or merge from a checkout whose HEAD is origin/$Target."
            Write-Host ''
            Write-GitState
            exit 2
        }

        # The one repair. It needs a checkout that may be moved: this one is on the target branch, at
        # that branch's tip, with no uncommitted change. Moving a branch under somebody else's checkout,
        # or resetting over work that is not committed, would be the repair losing the thing it repairs.
        $onBranch = "$(& git symbolic-ref -q --short HEAD 2>$null)".Trim()
        $headHere = "$(& git rev-parse --verify HEAD 2>$null)".Trim()
        $dirty = @(& git status --porcelain --untracked-files=no 2>$null)
        if ($onBranch -ne $Target -or $headHere -ne $headTip -or $dirty.Count -gt 0) {
            $why = if ($onBranch -ne $Target) {
                "this checkout is on $(if ($onBranch) { "the branch $onBranch" } else { 'a detached HEAD' }), not on $Target, so moving $Target here would leave a checkout that disagrees with its own branch"
            } elseif ($headHere -ne $headTip) {
                "this checkout's HEAD ($headHere) is not the tip being asserted ($headTip)"
            } else {
                "this checkout has $($dirty.Count) uncommitted change(s), and the repair would discard them"
            }
            Write-Host ''
            Write-Host "STOPPED: HEAD is not origin/$Target, and this checkout is not one the repair may move it in (exit 2)" -ForegroundColor Red
            Write-Host "  $why."
            Write-Host "  What it holds are merges this tool made and did not push, so the repair would lose nothing: HEAD $headTip, origin/$Target $originTarget."
            Write-Host '  Not restoring, and nothing was merged or pushed.'
            Write-Host ''
            Write-GitState
            exit 2
        }


        # Kept under a tag with the name a person doing this by hand already uses (`backup/orphan-<sha>`,
        # which is what the merge seat writes before it moves develop back): one name for one thing, so a
        # later reader finds both kinds of record in one place. A tag that is already there for this
        # same commit is the record already written -- it is not rewritten, and a tag of that name
        # pointing somewhere else is not overwritten either (that tag is somebody else's record).
        $strandedRef = "backup/orphan-$($headTip.Substring(0,10))"
        $tagRef = "refs/tags/$strandedRef^{commit}"
        $tagged = "$(& git rev-parse --verify $tagRef 2>$null)".Trim()
        if ($tagged -and $tagged -ne $headTip) {
            $strandedRef = "$strandedRef-$([string](Get-Date -Format 'yyyyMMdd-HHmmss'))"
            $tagRef = "refs/tags/$strandedRef^{commit}"
            $tagged = "$(& git rev-parse --verify $tagRef 2>$null)".Trim()
        }
        if ($tagged -ne $headTip) {
            & git tag $strandedRef $headTip 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Stop-With 'the commits HEAD holds could not be kept under a tag' "git tag $strandedRef $headTip failed. Nothing is dropped without a record of where it went, so HEAD was left where it is."
            }
        }
        & git reset --hard $originTarget 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Stop-With "HEAD could not be restored to origin/$Target" "git reset --hard $originTarget failed after those commits were saved under $strandedRef. HEAD is where it was; the ref is the record of what it holds."
        }
        $restored = "$(& git rev-parse --verify "refs/heads/$Target^{commit}" 2>$null)".Trim()
        if ($restored -ne $originTarget) {
            Stop-With "HEAD was not restored to origin/$Target" "after git reset --hard $originTarget, local $Target reads $restored. The commits it held are under $strandedRef."
        }
        Write-Host ''
        # The sentence the author asked for, with the one case that would make it false spelled out
        # instead of glossed: a branch that is behind origin holds no commit origin does not have.
        $heldText = if ($onlyLocal.Count -eq 0) {
            "it was behind origin/$Target rather than ahead, so no commit is lost from it"
        } else {
            "the commits only it held are under $strandedRef"
        }
        Write-Host "  restored $rangeName from $headTip to $originTarget, $heldText" -ForegroundColor Yellow
        Write-Host "  (nothing was dropped: $strandedRef still points at $headTip, and git log --oneline origin/$Target..$strandedRef lists what it holds)"
        $headTip = $originTarget
    }

    $startWhere = if ($startBranch) { "on $startBranch" } else { 'on a detached HEAD' }
    Write-Host "start point: target $Target is $($originTarget.Substring(0,9)) (origin/$Target); HEAD is $($headTip.Substring(0,9)) $startWhere"

    # --- -WarmBaseline: the idle-slot run, D-1123-123 --------------------------
    # A merge needs "what the plans it selects read on the commit it lands on". Measured per merge,
    # that is one extra run per merge on top of the merged tree's own. Measured once across the fast
    # layer, the same rows answer every merge that lands on that commit and selects anything inside
    # the layer -- which is nearly every merge, and the layer `--changed` widens to when a change names
    # no plan.
    #
    # The run belongs here and not inside a merge because it is nobody's verdict: a merge that started
    # while it ran would wait on a measurement it does not need to wait for when a baseline is already
    # there. So this starts no merge, asserts the start point like every other path, measures develop
    # in the baseline worktree and stores the rows under that commit. It pushes nothing and moves no
    # branch.
    if ($WarmBaseline) {
        $warmDir = Join-Path $commonDir 'camel-readings'
        $warmStarted = Get-Date
        $warmProbe = Test-FastLayerBaseline -CacheDir $warmDir -Develop $originTarget -Checkout $toolRoot
        if ($warmProbe.Hit) {
            $warmProbe.Text -split "`n" | Where-Object { $_ } | ForEach-Object { "  $_" }
            Write-Host ''
            Write-Host "  a baseline covering the fast layer is already stored for $Target $($originTarget.Substring(0,9)); nothing was measured (exit 0)"
            exit 0
        }
        if ($warmProbe.Text -notmatch 'cache-miss') {
            $warmProbe.Text -split "`n" | Where-Object { $_ } | ForEach-Object { "  $_" }
            Write-Host ''
            Write-Host 'STOPPED: whether a fast-layer baseline exists could not be answered (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: the cache was not read, so nothing was measured and nothing was stored.'
            Write-Host '  What repairs it: the comparer''s own line is above; `--probe` reads one key and needs no rows.'
            exit 2
        }
        $warmPlan = Get-BaselineCheckout -Tip $originTarget -Root $BaselineRoot -LegacyPath $BaselineWorktree -LegacyBranch $BaselineBranch
        foreach ($line in @($warmPlan.Lines)) { if ($line) { Write-Host $line } }
        if ($warmPlan.Problem) {
            Write-Host ''
            Write-Host 'STOPPED: the fast layer could not be measured, so nothing was stored (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host "  $($warmPlan.Problem)"
            exit 2
        }
        $warmPath = $warmPlan.Path
        $warmProblem = Sync-BaselineWorktree -Path $warmPath -Sha $originTarget -CommonDir $commonDir -Repo $MainCheckout -BranchName $warmPlan.Branch
        $warmMeasured = ''
        if ($null -eq $warmProblem) {
            $warmMeasured = "$(& git -C $warmPath rev-parse HEAD 2>$null)".Trim()
            if ($warmMeasured -ne $originTarget) {
                $warmProblem = "$warmPath is at $($warmMeasured.Substring(0, [Math]::Min(9, $warmMeasured.Length))), not at $Target $($originTarget.Substring(0,9)), so the baseline would come from another tree"
            }
        }
        if ($null -ne $warmProblem) {
            Write-Host ''
            Write-Host 'STOPPED: the fast layer could not be measured, so nothing was stored (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host "  $warmProblem"
            Write-Host "  What repairs it: the baseline is measured in a worktree held at $Target ($($originTarget.Substring(0,9))). Name one with -BaselineWorktree, or let this script create it."
            exit 2
        }
        $warmPlans = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-warm-plans-$PID.txt")
        [System.IO.File]::WriteAllText($warmPlans, "test/plans/smoke`ntest/plans/feat`n",
            (New-Object System.Text.UTF8Encoding($false)))
        $warmTranscript = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-warm-rows-$PID.txt")
        Write-Host ("  measuring the fast layer on $Target {0} in {1} -- the run every merge after this one would otherwise do for itself ..." -f `
            $originTarget.Substring(0,9), $warmPath)
        # Joined, not a bare array: `pwsh -File` passes each element as its own argument, so
        # `-Plan @('test/plans/smoke', 'test/plans/feat')` arrives as `-Plan test/plans/smoke` plus a
        # stray positional `test/plans/feat`, and the run stops before measuring anything. The gate's
        # own `-Plan` reads a comma list; that is the form it is given here.
        $warmRun = @(& pwsh -NoProfile -File (Join-Path $toolRoot 'internal\tools\camel-remote.ps1') `
            -Path $warmPath -Plan 'test/plans/smoke,test/plans/feat' -ListKnown -NoGpuWatch 2>&1)
        $warmExit = $LASTEXITCODE
        $warmSeconds = ((Get-Date) - $warmStarted).TotalSeconds
        [System.IO.File]::WriteAllText($warmTranscript,
            (($warmRun | ForEach-Object { "$_" }) -join "`n") + "`n",
            (New-Object System.Text.UTF8Encoding($false)))
        $warmRows = @($warmRun | Where-Object { "$_".TrimStart().StartsWith('{') }).Count
        $warmReds = @($warmRun | Where-Object { "$_".TrimStart().StartsWith('{') -and "$_" -match '"verdict"\s*:\s*"fail"' }).Count
        if ($warmRows -eq 0) {
            Write-Host ''
            Write-Host "STOPPED: the fast layer was not measured (exit 2, no reading taken; the run exited $warmExit)" -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: no row came back, so there is no baseline to store and nothing will hit it.'
            Write-Host '  What repairs it: the row block must arrive with rows inside it; a build that failed or a plan set that starts no case writes no block.'
            exit 2
        }
        $env:CAMEL_BASELINE_SECONDS = [string][int]$warmSeconds
        $warmAccept = @(& node (Join-Path $toolRoot 'scripts\merge-readings.mjs') --transcript $warmTranscript `
            --cache $warmDir --develop $originTarget --plans $warmPlans --tree $warmMeasured --accept 2>&1)
        $warmAcceptExit = $LASTEXITCODE
        Remove-Item Env:\CAMEL_BASELINE_SECONDS -ErrorAction SilentlyContinue
        $warmAccept | ForEach-Object { "  $_" }
        Remove-Item -LiteralPath $warmPlans, $warmTranscript -Force -ErrorAction SilentlyContinue
        if ($warmAcceptExit -ne 0) {
            Write-Host ''
            Write-Host 'STOPPED: the fast layer was measured but could not be stored (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host "  $warmRows row(s) came back from $Target $($originTarget.Substring(0,9)) in $([int]$warmSeconds) s and the store refused them."
            Write-Host "  What repairs it: the comparer's own refusal is above; it names what about the rows or the cache it could not use."
            exit 2
        }
        Write-Host ("  measured: {0} case(s) across the fast layer on $Target {1}, {2} already red there, {3:N0} s" -f `
            $warmRows, $originTarget.Substring(0,9), $warmReds, $warmSeconds)
        Write-Host "  from now on a merge that lands on $($originTarget.Substring(0,9)) and selects plans inside test/plans/smoke or test/plans/feat compares against this and measures only its own tree (exit 0)"
        exit 0
    }

    $head = (& git rev-parse --verify "$Branch^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0) {
        Stop-With "branch $Branch does not resolve to a commit" 'Check the name, and that the branch has commits.'
    }
    $head = "$head".Trim()

    Write-Host "merging $Branch at $head"

    # Test 1: the exit code. Skipped for `-MergeCommit`: the merge exists and its shape was asserted
    # above, which is the same question `git merge` answers by exiting 0 and that assertion asks by
    # comparing parents. Running the merge anyway is not an option -- on a conflicted merge it would
    # stop on the resolution the seat just made, and on a clean one it would make a second merge
    # commit on top of the first.
    if ($MergeCommit) {
        $mergeExit = 0
        $mergeOutput = @()
    } else {
        $mergeOutput = & git merge --no-ff $Branch -m "$Message`n`nLane branch: $Branch at $head" 2>&1
        $mergeExit = $LASTEXITCODE
        $mergeOutput | ForEach-Object { "  $_" }
    }

    if ($mergeExit -ne 0) {
        $conflicted = @(& git diff --name-only --diff-filter=U 2>$null)
        $detail = if ($conflicted.Count -gt 0) {
            "$($conflicted.Count) file(s) conflicted: $($conflicted -join ', ')"
        } else {
            'git merge exited non-zero with no conflicted file -- the shape that reads like success'
        }
        Stop-With "git merge exited $mergeExit" $detail
    }

    # Test 2: the commits are in HEAD. This holds for a merge that exited 0 and merged nothing.
    & git merge-base --is-ancestor $head HEAD
    if ($LASTEXITCODE -ne 0) {
        Stop-With "merge-base --is-ancestor $head HEAD is false" "git merge exited 0 but the branch is not in HEAD; HEAD is $(& git rev-parse --short HEAD)."
    }

    if ($WhatIf) {
        Write-Host ''
        Write-Host "would commit and push; both checks passed (HEAD is $(& git rev-parse --short HEAD))" -ForegroundColor Yellow
        exit 0
    }

    # git merge with -m has already made the commit; a second commit has nothing to do. What is worth
    # checking is that HEAD is that merge and not something else.
    if (-not (Test-Path (Join-Path $gitDir 'MERGE_HEAD'))) {
        $parents = @(& git rev-list --parents -n 1 HEAD 2>$null | ForEach-Object { $_.Split(' ') })
        if ($parents.Count -lt 3) {
            Stop-With 'HEAD is not a merge commit' "HEAD $(& git rev-parse --short HEAD) has $($parents.Count - 1) parent(s). A merge that did not happen also lands the branch check, so this is checked separately."
        }
    }

    # The merge commit this run made, read once. Every stop from here on is past the merge in this
    # script's own control flow, and this is the sha those sentences have to account for: whether it is
    # only here, whether origin has it, and what else local develop holds.
    $mergeCommit = "$(& git rev-parse HEAD 2>$null)".Trim()

    # A file without a header has already stopped one CI run before it built anything. The check is
    # cheap and local, so it runs here, before the push.
    $headers = & node (Join-Path $MainCheckout 'scripts\headers.mjs') --check 2>&1
    $headersExit = $LASTEXITCODE
    $headers | Select-Object -Last 2 | ForEach-Object { "  $_" }
    # A backfill of a merge made earlier meets trees that today's header rule rejects: the rule has
    # grown since, and a file some later commit gave its two lines to is headless in the tree that
    # added it. Writing the header into this commit is rewriting history and skipping the commit
    # breaks the first-parent chain the next backfilled merge asserts, so the fact is recorded
    # instead of hidden: `-KnownHeaderAtSnapshot` names the file, the snapshot and the commit that
    # gave it its header, and this block re-establishes all three from the repository before it
    # lets the push through. Without the switch this is the refusal it always was.
    $headerAllowed = @()
    if ($headersExit -ne 0) {
        if (-not $KnownHeaderAtSnapshot) {
            Stop-With 'the header check failed' 'A file without its two-line header does not go to develop, because it stops a whole CI run before the build starts.' $mergeCommit
        }
        if (-not $MergeCommit) {
            Stop-With '-KnownHeaderAtSnapshot is only for a backfill' 'It records a merge made earlier, so it needs -MergeCommit. The ordinary merge path checks the tree it is merging and has nothing to be allowed for.'
        }
        $hlist = Get-Content -LiteralPath $KnownHeaderAtSnapshot -Raw | ConvertFrom-Json
        if (-not $hlist.tip -or -not @($hlist.entries).Count) {
            Stop-With "-KnownHeaderAtSnapshot $KnownHeaderAtSnapshot is not a list" 'It needs "tip" (a commit the fix has to be an ancestor of) and a non-empty "entries".'
        }
        $hTip = "$(& git rev-parse --verify "$($hlist.tip)^{commit}" 2>$null)".Trim()
        if (-not $hTip) { Stop-With "-KnownHeaderAtSnapshot tip $($hlist.tip) does not resolve to a commit" 'The evidence must name a commit this repository has.' }
        $failing = @($headers | ForEach-Object { "$_" } |
            Where-Object { $_ -match '^\s*(\S+): missing the two-line header' } |
            ForEach-Object { ($_ -replace '^\s*(\S+): missing the two-line header.*$', '$1').Trim() } | Select-Object -Unique)
        foreach ($entry in @($hlist.entries)) {
            if (-not $entry.file -or -not $entry.snapshot -or -not $entry.fixedAt) {
                Stop-With "-KnownHeaderAtSnapshot entry $(($entry | ConvertTo-Json -Compress)) is incomplete" 'Each entry needs file, snapshot and fixedAt.'
            }
            $snapSha = "$(& git rev-parse --verify "$($entry.snapshot)^{commit}" 2>$null)".Trim()
            $fixSha = "$(& git rev-parse --verify "$($entry.fixedAt)^{commit}" 2>$null)".Trim()
            if (-not $snapSha) { Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) names snapshot $($entry.snapshot), which is not a commit" 'The snapshot is the merge being recorded.' }
            if (-not $fixSha) { Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) names $($entry.fixedAt), which is not a commit" 'The fix has to be a commit this repository has.' }
            if ($snapSha -ne $mergeCommit) {
                Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) is red at $($snapSha.Substring(0, 9))" "This run records $($mergeCommit.Substring(0, 9)); an entry about another snapshot is stale."
            }
            & git merge-base --is-ancestor $fixSha $hTip 2>$null
            if ($LASTEXITCODE -ne 0) {
                Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) names $($fixSha.Substring(0, 9)), which is not an ancestor of $($hTip.Substring(0, 9))" 'A fix that is not on the line the evidence was read from did not reach the tip.'
            }
            # The three facts, read from the repository and not from the list: the file is in the
            # snapshot's tree and has no copyright line there, and it has one at the fix.
            $atSnap = @(& git show "$($snapSha):$($entry.file)" 2>$null | Select-Object -First 6)
            if (-not $atSnap.Count) { Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) is not in $($snapSha.Substring(0, 9))" 'The file the entry names has to be in the snapshot tree.' }
            if (@($atSnap | Where-Object { $_ -match 'Copyright' }).Count -gt 0) {
                Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) already has its header at $($snapSha.Substring(0, 9))" 'The header check did not fail on this file, so the entry is not about it.'
            }
            $atFix = @(& git show "$($fixSha):$($entry.file)" 2>$null | Select-Object -First 6)
            if (@($atFix | Where-Object { $_ -match 'Copyright' }).Count -eq 0) {
                Stop-With "-KnownHeaderAtSnapshot entry $($entry.file) has no header at $($fixSha.Substring(0, 9)) either" 'The entry names a fix that did not write the header.'
            }
            $headerAllowed += $entry.file
        }
        $uncovered = @($failing | Where-Object { $headerAllowed -notcontains $_ })
        if ($uncovered.Count -gt 0) {
            Stop-With "-KnownHeaderAtSnapshot does not cover $($uncovered -join ', ')" 'Every file the check names has to have an entry, or the push carries a headless file.'
        }
        $stale = @($headerAllowed | Where-Object { $failing -notcontains $_ })
        if ($stale.Count -gt 0) {
            Stop-With "-KnownHeaderAtSnapshot lists $($stale -join ', '), which the check did not name" 'A stale entry hides nothing here; it only makes the record say more than the check did.'
        }
        foreach ($entry in @($hlist.entries)) {
            Write-Host ("  allowed: {0} has no header at this snapshot ({1}); the header is at {2} -- it stops no CI run of this tree" -f `
                $entry.file, $mergeCommit.Substring(0, 9), "$($entry.fixedAt)".Substring(0, [Math]::Min(9, "$($entry.fixedAt)".Length)))
        }
        $headersExit = 0
    }

    # The local gate compiles with this workstation's toolchain; CI's Linux and macOS legs compile
    # with clang 19, and a diagnostic only that one reports (`-Wc++11-narrowing` at
    # `src/stdlib/onnx/lowering_grad.cpp:480`, D-1123-83) stayed invisible until a CI run a day
    # later. This runs CI's compiler over the merged tree through the gate's own entry, so the
    # reading is the gate's reading and not a paraphrase of it. It needs no build (`-NoBuild`) and
    # reads the compile lines CMake wrote, so `-AllowStale` is the honest pairing: the install this
    # host holds describes an older tree, and this check does not read it.
    Write-Host ''
    Write-Host 'clang syntax check (CI''s clang 19, this tree''s compile lines) ...'
    $syntax = & pwsh -NoProfile -File (Join-Path $toolRoot 'internal\tools\camel-remote.ps1') `
        -Path $MainCheckout -Check clang-syntax -NoBuild -AllowStale -NoGpuWatch 2>&1
    $syntaxExit = $LASTEXITCODE
    $syntax | Where-Object { $_ -match 'clang-syntax:|check |STOPPED|refused|error:' } | Select-Object -Last 8 |
        ForEach-Object { "  $_" }
    # Exit 1 and exit 2 are two different facts about two different things, and a gate that reports
    # them the same way teaches the next person the wrong one. 1 is a reading: the compiler ran and
    # said no. 2 is the absence of a reading: the check itself did not run (no compile database, no
    # container, no toolchain), so nothing has been said about this tree -- and that must read as
    # "the gate is down", never as "your code is wrong". Both refuse the merge: a gate that lets a
    # merge through while it is blind is the failure mode this whole lane exists to remove.
    if ($syntaxExit -eq 1) {
        Stop-With 'the clang syntax check is red (exit 1: the compiler ran and reported a diagnostic)' 'CI builds with clang 19 on linux and macos; a tree that fails its syntax pass is red there however green the local build was. The diagnostic is above: fix it.'
    }
    if ($syntaxExit -ne 0) {
        # Exit 2 rather than 1, and the same three lines Stop-With prints: the exit code is what a
        # script wrapping this merge branches on, and "the gate is down" must not be read as "the
        # tree is wrong" by whatever reads the code next.
        Write-Host ''
        Write-Host "STOPPED: the clang syntax check did not run (exit $syntaxExit, no reading taken)" -ForegroundColor Yellow
        # A message that only says "repair the gate" leaves the reader with no move when the thing
        # being repaired is what refused them: this is the one place the gate's internal state is
        # visible, so it names the state and the command that changes it. A checkout whose
        # `build/compile_commands.json` is gone cannot be merged into at all until it is built once,
        # and that includes a merge whose purpose is to fix the gate itself.
        Write-Host '  This is not a defect in your tree: the check itself could not measure it, so nothing was pushed and nothing was learned about this change.'
        Write-Host '  The commonest cause is that the gate checkout has no compile database (build/compile_commands.json), which only a build writes: run one on it -- internal/tools/camel-remote.ps1 -Path <that worktree>, without -NoBuild -- and merge again. The checker names the actual cause on the line above it printed; the others are a clang 19 image the host does not hold, a toolchain its compile lines cannot reach, and a commit whose first parent is the same tree (`the base and this tree are the same commit`: there is then no change to read).'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    }

    # --- the plans this merge touches, read case by case ------------------------
    # The clang step above asks whether the merged tree compiles. This asks what it *reads*: the
    # plans the change touches, run on the merged tree, compared case by case with the same plans
    # on the tree the merge lands on. A count of reds cannot answer it -- "three failed before and
    # three failed after" is what a tree that broke three new cases and fixed three old ones reads
    # like -- so the comparison is per case and its three sets are `fixed`, `new` and `changed`,
    # and `scripts/merge-readings.mjs` is where that judgement lives.
    #
    # The rows come back on camel-remote's own stream, between two markers (`-ListKnown`): that is
    # the whole of the channel, and a block that is absent is not a green -- it means the run that
    # was supposed to record the rows did not, and this stage refuses the merge rather than pass it
    # while blind.
    #
    # Which plans: the selection `--changed` makes, asked for here with `--list` (which starts
    # nothing). A selection that widens is refused, not measured: `--changed` widens when it can
    # name no plan, and the widened set is the whole fast layer -- a full-suite run whose readings
    # would then be compared against a baseline of everything, case by case, for a merge that
    # touches a handful of files. "The change names no plan" is a fact about the change or about the
    # selection rules, and neither is answered by running everything: it is the same zero as the
    # clang gate's, so it gets the same answer.
    Write-Host ''
    Write-Host 'row readings (the plans this merge touches, case by case) ...'
    $rowsStarted = Get-Date
    # The base of this selection is the tree the merge lands on (`HEAD^1`), not `origin/develop`.
    # That ref is not this sha and other people move it: it advanced between two runs of one identical
    # merge while this stage was being proved, the selection went from 2 plans to 22, and the second
    # comparison -- correctly -- refused for a plan set the baseline had never been measured under.
    # The change a merge *brings* is its first parent's difference from itself, and nothing moves that.
    # The sha is read the same way a few lines below, where it becomes the cache key; it is the same
    # commit, and that block is the one that refuses when git cannot name it.
    $savedBase = $env:CAMEL_TEST_BASE
    $env:CAMEL_TEST_BASE = "$(& git rev-parse 'HEAD^1' 2>$null)".Trim()
    try {
        $selectOut = @(& node (Join-Path $MainCheckout 'scripts\test.js') --changed --list 2>&1)
        $selectExit = $LASTEXITCODE
    } finally {
        if ($null -eq $savedBase) { Remove-Item Env:\CAMEL_TEST_BASE -ErrorAction SilentlyContinue }
        else { $env:CAMEL_TEST_BASE = $savedBase }
    }
    $prefix = $MainCheckout.TrimEnd('\') + '\'
    $planTargets = @($selectOut | ForEach-Object { "$_".Trim() } | Where-Object { $_ -and $_.StartsWith($prefix) } |
        ForEach-Object { $_.Substring($prefix.Length) -replace '\\', '/' })
    $widened = @($selectOut | Where-Object { "$_" -match '^\(the whole fast layer' }).Count -gt 0
    $planSet = $planTargets -join ','
    # The key is a list of plan names and not the names joined by commas: a single target spelled as
    # one comma-list is one string that may also be a path, and `--plans` reads an existing path as a
    # *list file* -- so a merge touching exactly one plan would key its baseline by that plan file's
    # own content, and editing the plan would silently ask for a different baseline. One name per
    # line has one meaning.
    $planSetFile = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-plans-$PID.txt")
    if ($planTargets.Count -gt 0) {
        [System.IO.File]::WriteAllText($planSetFile, ($planTargets -join "`n") + "`n",
            (New-Object System.Text.UTF8Encoding($false)))
    }
    # The change this merge brings, as `git diff --name-only HEAD^1` sees it in this checkout, and the
    # merge message. The comparison reads them to answer the one question the case sets cannot: a case
    # the baseline holds and this run does not is either a case this change removed -- then it is
    # `removed` and not `fixed`, and a red one has to be named in the message -- or the two runs
    # selected differently, which is a fact about the selection and exits 2. Without these two files
    # both readings look the same, and the gate used to call both of them "not a green".
    $changedFilesFile = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-changed-$PID.txt")
    $changedFiles = @(& git diff --name-only 'HEAD^1' 2>$null) |
        ForEach-Object { "$_".Trim().Replace('\', '/') } | Where-Object { $_ }
    [System.IO.File]::WriteAllText($changedFilesFile, (($changedFiles -join "`n") + "`n"),
        (New-Object System.Text.UTF8Encoding($false)))
    $messageFile = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-message-$PID.txt")
    [System.IO.File]::WriteAllText($messageFile, "$Message`n", (New-Object System.Text.UTF8Encoding($false)))
    # --- a merge with nothing to measure ----------------------------------------
    # `--changed` named no plan. If every file this merge changes is documentation, that is a fact
    # about the change and not a failure of the selection: no plan covers `internal/questions/`, and
    # none should -- nothing executes a page. The run is then recorded as what it is (`measured: none`,
    # with the reason and the gate that decided it) and pushed, because a merge that was never meant to
    # be measured must not be refused as if the measuring had gone wrong. Anything else stays exit 2:
    # a change with one non-documentation file in it is a change a plan could turn red on.
    #
    # How that is read is `merge-lane-docs.ps1`'s `Get-SelectionVerdict`, and the reason it is a
    # function there rather than a conjunction here is the defect this replaces: the judgement used to
    # be `$selectExit -eq 0 -and $planTargets.Count -eq 0 -and every path documentation`, and an empty
    # selection is exit 2 by the runner's own rule, so the conjunction could never hold and a
    # documentation-only merge was always refused as unmeasurable. Both documentation verdicts below
    # take the same branch; the refused one says in its own line that the selection refused, because
    # "no rule matches" and "could not read the change" are different facts and only the first one is
    # a documentation merge.
    $verdict = Get-SelectionVerdict -SelectExit $selectExit -SelectOutput $selectOut `
        -ChangedFiles $changedFiles -PlanCount $planTargets.Count
    if ($verdict -eq 'docs-only-listed' -or $verdict -eq 'docs-only-refused') {
        $firstParentSha = "$(& git rev-parse 'HEAD^1' 2>$null)".Trim()
        Write-Host ''
        if ($verdict -eq 'docs-only-refused') {
            Write-Host 'docs-only: nothing to measure (selection refused: no rule matches, and every changed path is documentation)' -ForegroundColor Yellow
        } else {
            Write-Host 'docs-only: nothing to measure' -ForegroundColor Yellow
        }
        Write-Host "  Every one of the $($changedFiles.Count) file(s) this merge changes is documentation: $($changedFiles -join ', ')"
        if ($verdict -eq 'docs-only-refused') {
            Write-Host '  `--changed` exited 2 because no rule in `test/plan-selection.json` matches these paths and the selection is empty; that refusal is about the rules, not about this merge, and the paths are the whole change.'
        } else {
            Write-Host '  `--changed` names no plan because no plan covers these paths, and running the suite anyway would read some other change.'
        }
        Write-Host "  The record says so in those words, and the merge is pushed as a merge that had nothing to measure."
        $docsWhy = 'documentation only: every file this merge changes is documentation, so no plan is selected and there is nothing to measure'
        if ($verdict -eq 'docs-only-refused') {
            $docsWhy = 'documentation only: every file this merge changes is documentation, so the selection names no plan (the runner refuses an empty selection with exit 2) and there is nothing to measure'
        }
        $passedDir = Join-Path $commonDir 'camel-passed'
        New-Item -ItemType Directory -Force -Path $passedDir | Out-Null
        $recordOut = @(& node (Join-Path $toolRoot 'scripts\merge-record.mjs') write `
            --measured none `
            --why $docsWhy `
            --merge $mergeCommit --first-parent $firstParentSha --second-parent $head --branch $Branch `
            --tree $MainCheckout --out $passedDir --gate headers --gate clang-syntax --gate docs-only 2>&1)
        $recordExit = $LASTEXITCODE
        $recordOut | ForEach-Object { "  $_" }
        if ($recordExit -ne 0) {
            Stop-With "the pass record for $($mergeCommit.Substring(0, 9)) could not be written (exit $recordExit)" 'The pre-push hook refuses a merge commit without one, so the push is not attempted.' $mergeCommit
        }
        $recordFile = Join-Path $passedDir "$mergeCommit.json"
        if ($NoPush) {
            Write-Host ''
            Write-Host 'would push: this merge had nothing to measure. Nothing was pushed (-NoPush).' -ForegroundColor Yellow
            Write-GitState -MergeCommit $mergeCommit
            Write-Host ''
            Write-Host "tried $Branch at $head; nothing was pushed (-NoPush)"
            exit 0
        }
        for ($i = 1; $i -le 3; $i++) {
            & git push origin "HEAD:$Target" 2>&1 | Select-Object -Last 1
            if ($LASTEXITCODE -eq 0) { break }
            Start-Sleep -Seconds 4
        }
        if ($LASTEXITCODE -ne 0) {
            Remove-Item -LiteralPath $recordFile -Force -ErrorAction SilentlyContinue
            Stop-With 'git push failed after 3 attempts' "The merge is committed locally and not on origin/$Target, and the pass record was removed with the push's claim." $mergeCommit
        }
        Write-Host ''
        Write-Host "merged $Branch at $head; origin/$Target is $mergeCommit (docs-only, nothing measured)" -ForegroundColor Green
        exit 0
    }
    # `unreadable` and `no-plan` are both refusals and they are not the same fact, so they do not
    # print the same stop. The first says the change itself is unknown -- a base, the rules or the
    # changed-file list could not be read -- and that is the gate being blind, not a verdict. The
    # second says the selection was read and named nothing while at least one path is not
    # documentation: a hole in `test/plan-selection.json` or an empty merge. Neither is a red, and
    # neither may read as one.
    if ($verdict -eq 'unreadable') {
        Write-Host ''
        Write-Host "STOPPED: the selection could not be read (exit $selectExit, no reading taken)" -ForegroundColor Yellow
        Write-Host '  This is not a verdict about your tree: the base, the selection rules or the changed-file list could not be read, so the change itself is unknown -- and whether every path is documentation is not a question an unknown change can answer. Nothing was pushed.'
        $selectOut | Select-Object -Last 3 | ForEach-Object { "  $_" }
        Write-Host '  What repairs it: `node scripts/test.js --changed --list` names the selection and starts no test; its own refusal is the line above, and the usual cause is a base branch that does not resolve in this checkout.'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    }
    if ($verdict -eq 'no-plan') {
        # Not a red and not a green: the selection answered and named no plan, so the plans this
        # merge touches are unknown and nothing about the change has been measured. Same shape as
        # the clang step's exit 2, and for the same reason: `cannot select` must not read as `no reds`.
        Write-Host ''
        if ($selectExit -eq 0) {
            Write-Host 'STOPPED: this merge changes no file, so no plan is selected (exit 0, no reading taken)' -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: `git diff --name-only HEAD^1` is empty, so there is no change to measure case by case and no baseline to compare it with. Nothing was pushed.'
            Write-Host '  What repairs it: check that the merge commit really brings the lane in -- a merge that reports success and changes nothing is the thing `merge-lane` exists to refuse.'
        } elseif ($planTargets.Count -eq 0 -and $widened) {
            Write-Host 'STOPPED: this merge selects no plan (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: `--changed` named no plan for this change, so it widened to the whole fast layer, and a whole-suite reading is not a reading about this merge. Nothing was pushed.'
            Write-Host '  What repairs it: `node scripts/test.js --changed --list` is the selection itself and starts nothing -- read it. Either the change names no plan (then nothing about it is measured case by case, and that is a fact worth knowing before merging) or `test/plan-selection.json` has no rule for those files.'
        } else {
            Write-Host "STOPPED: the plans this merge touches could not be selected (exit $selectExit, no reading taken)" -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: the selection named no plan for these paths, and at least one of them is not documentation, so this is a hole in `test/plan-selection.json` rather than a merge with nothing to measure. Nothing was pushed.'
            $selectOut | Select-Object -Last 3 | ForEach-Object { "  $_" }
            Write-Host '  What repairs it: `node scripts/test.js --changed --list` names the selection and starts no test; the line above is its own refusal. Add the rule the paths belong to, or name the layer deliberately on the command line.'
        }
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    }

    # --- the two trees this stage is about --------------------------------------
    # The cache key is the tree this merge LANDS ON: `HEAD^1`, develop as it stood before this merge.
    # Never the merge commit. The merge commit did not exist until a few lines ago, so no run could
    # have measured it: keyed by it, every merge would miss, and a gate that has to be handed a
    # baseline it can never have is a gate that refuses every merge -- the same uselessness as one
    # that passes every merge, and it hides the same thing. The rows this run read come off the
    # merged tree, and that sha travels beside the key (`--tree`) so the comparer can tell the one
    # failure the three sets cannot show: a baseline measured on the tree it is comparing.
    $comparedTree = "$(& git rev-parse HEAD 2>$null)".Trim()
    $developTip = "$(& git rev-parse 'HEAD^1' 2>$null)".Trim()
    if (-not $comparedTree -or -not $developTip) {
        Write-Host ''
        Write-Host 'STOPPED: the two trees this comparison needs could not be read (exit 2, no reading taken)' -ForegroundColor Yellow
        Write-Host '  This is not a defect in your tree: git did not answer for HEAD or HEAD^1, so which tree to compare with which is unknown and nothing was measured about this change.'
        Write-Host '  What repairs it: the merge commit is what makes HEAD^1 the tree to land on; this stop means HEAD is not in that shape. Nothing was pushed.'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    }
    if ($developTip -eq $comparedTree) {
        Write-Host ''
        Write-Host 'STOPPED: HEAD^1 is HEAD, so there is no second tree to read (exit 2, no reading taken)' -ForegroundColor Yellow
        Write-Host '  This is not a defect in your tree: the baseline would be measured on the very tree it is compared with, which is 0 fixed, 0 new and 0 changed by construction -- a green that measured one tree twice.'
        Write-Host '  What repairs it: a merge commit whose first parent is develop. Nothing was pushed.'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    }
    $compareDir = Join-Path $commonDir 'camel-readings'
    Write-Host ("  baseline key: develop {0} (the first parent, before this merge); this run reads tree {1} (the merge commit)" -f `
        $developTip.Substring(0, 9), $comparedTree.Substring(0, 9))

    # --- is there a baseline for that tree? --------------------------------------
    # A miss used to stop here and print a command for a person to run by hand -- with the merged
    # tree's own sha in it, so following it stored this merge's rows under this merge's key and the
    # comparison that followed was the tree against itself: 0/0/0, and a green that measured nothing.
    # So this asks the comparer's own key (it is the only place the key is written) and, on a miss,
    # measures the tree the merge lands on rather than asking anybody to.
    $probe = @(& node (Join-Path $toolRoot 'scripts\merge-readings.mjs') --cache $compareDir `
        --develop $developTip --plans $planSetFile --probe 2>&1)
    $probeExit = $LASTEXITCODE
    $probeText = ($probe | ForEach-Object { "$_" }) -join "`n"
    $seededFrom = $null

    if ($probeText -match 'cache-miss') {
        # The tree to measure is NOT this one. It is checked out detached at the first parent, in a
        # worktree of its own: the merge commit is already here, so checking develop out here would
        # move the branch the merge landed on, and measuring the merged tree is the defect this whole
        # step exists to remove. The worktree is kept between merges so the host's build of it stays
        # warm -- a cold build per merge is what would make people go around this gate.
        $baselinePlan = Get-BaselineCheckout -Tip $developTip -Root $BaselineRoot -LegacyPath $BaselineWorktree -LegacyBranch $BaselineBranch
        foreach ($line in @($baselinePlan.Lines)) { if ($line) { Write-Host $line } }
        $baselinePath = $baselinePlan.Path
        $worktreeProblem = if ($baselinePlan.Problem) { $baselinePlan.Problem } else {
            Sync-BaselineWorktree -Path $baselinePath -Sha $developTip `
                -CommonDir $commonDir -Repo $MainCheckout -BranchName $baselinePlan.Branch
        }
        # The sha this run will really read, taken from the checkout itself. `--tree $developTip`
        # alone would be an assertion about the source, and an assertion is exactly what a seeding
        # pointed at the wrong tree gets right while measuring the wrong one.
        $measuredTree = ''
        if ($null -eq $worktreeProblem) {
            $measuredTree = "$(& git -C $baselinePath rev-parse HEAD 2>$null)".Trim()
            if ($measuredTree -ne $developTip) {
                $worktreeProblem = "$baselinePath is at $($measuredTree.Substring(0, [Math]::Min(9, $measuredTree.Length))), not at the first parent $($developTip.Substring(0, 9)), so the baseline would come from another tree than the one this merge lands on"
            }
            if ($measuredTree -eq $comparedTree) {
                $worktreeProblem = "the baseline checkout is the merged tree ($($comparedTree.Substring(0, 9))); a baseline measured there is this change compared with itself"
            }
        }
        $seedTranscript = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-baseline-$PID.txt")
        $seedSeconds = 0
        $seedRows = 0
        $seedReds = 0
        if ($null -eq $worktreeProblem) {
            Write-Host ("  no baseline for develop {0}: measuring it there, in a worktree at {1} on the gate's own branch" -f `
                $developTip.Substring(0, 9), $baselinePath)
            $seedStarted = Get-Date
            # The selection has to be the one the merged tree's run made, not merely "the same change"
            # read by another selector. `-Changed` here asked `camel-remote.ps1`'s own selector, which
            # resolves directory rules to whole directories, while the merged run asked
            # `test.js --changed --list`, which lists plans -- and the two disagreed by 1509 cases
            # (`test/plans/feat/opt/**` plus `lit2.*` and `defun` on one side only), so the comparison
            # refused with "the two runs selected differently" and no reading could be taken for a
            # merge whose trees were both perfectly readable. The plan set the merged run used is
            # already in `$planTargets`; the seeding run is given exactly it.
            $seedTargets = @($planTargets)
            # The seeding run is measured by the gate of the tree it measures. A backfill's baseline
            # can be old enough that its runner has never heard of a subcommand this checkout's gate
            # asks every tree for (`--print-time-sensitive` arrived with the time-sensitive rework,
            # 9e369c01d): asking ee34a1d66's runner for it stops the seeding run with "unknown option
            # -- the caller is newer than this tree", which is a fact about the caller and not about
            # the tree. The tree under measurement brings its own gate, exactly as it brings its own
            # runner -- the reading is the snapshot's, taken by the rules in force at that snapshot.
            $seedGate = Join-Path $baselinePath 'internal\tools\camel-remote.ps1'
            if (-not (Test-Path -LiteralPath $seedGate)) { $seedGate = Join-Path $toolRoot 'internal\tools\camel-remote.ps1' }
            # A plan file the merge *adds* exists on the merged tree and nowhere else: asking the
            # first parent's checkout for it fails the whole seeding run (ENOENT), which says nothing
            # about the trees. Those targets are dropped here -- their cases are `added` in the
            # comparison, which is what a case that did not exist before the merge is.
            $absent = @($seedTargets | Where-Object { -not (Test-Path -LiteralPath (Join-Path $baselinePath ($_ -replace '/', '\'))) })
            if ($absent.Count -gt 0) {
                $seedTargets = @($seedTargets | Where-Object { $absent -notcontains $_ })
                Write-Host ("  seeding without {0} plan target(s) this merge adds, which the first parent's tree does not have: {1}" -f `
                    $absent.Count, ($absent -join ', '))
            }
            if ($seedTargets.Count -gt 0) {
                # `-File` binds one value to `-Plan` and treats the second target as a positional
                # argument, so the array has to be written into a command: this is the only form that
                # gives the child the plan set as a set. A target is a repository path (no quotes in
                # it); each is single-quoted here and every quote inside one is doubled.
                $quoted = @($seedTargets | ForEach-Object { "'" + ("$_" -replace "'", "''") + "'" }) -join ','
                $camelCommand = "& '" + $seedGate + "' -Path '" +
                    ($baselinePath -replace "'", "''") + "' -ListKnown -NoGpuWatch -Plan @(" + $quoted + ")"
                $seedRun = @(& pwsh -NoProfile -Command $camelCommand 2>&1)
            }
            else {
                # No selection to copy (the merged run widened, or listed nothing): fall back to the
                # change-driven selector rather than measure no plan at all.
                $seedRun = @(& pwsh -NoProfile -File $seedGate `
                    -Path $baselinePath -Changed -ChangedBase $comparedTree -ListKnown -NoGpuWatch 2>&1)
            }
            $seedExit = $LASTEXITCODE
            $seedSeconds = ((Get-Date) - $seedStarted).TotalSeconds
            [System.IO.File]::WriteAllText($seedTranscript,
                (($seedRun | ForEach-Object { "$_" }) -join "`n") + "`n",
                (New-Object System.Text.UTF8Encoding($false)))
            $seedRows = @($seedRun | Where-Object { "$_".TrimStart().StartsWith('{') }).Count
            $seedReds = @($seedRun | Where-Object { "$_".TrimStart().StartsWith('{') -and "$_" -match '"verdict"\s*:\s*"fail"' }).Count
        }
        if ($null -ne $worktreeProblem) {
            Write-Host ''
            Write-Host 'STOPPED: the baseline could not be measured, so nothing was compared (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: the tree the baseline comes from was not available, so nothing was learned about this change.'
            Write-Host "  $worktreeProblem"
            Write-Host "  What repairs it: the baseline is measured in a worktree held at the first parent ($($developTip.Substring(0, 9))). Name one with -BaselineWorktree, or let this script create it; a path that holds somebody else's checkout is refused rather than checked out over."
            Write-Host ''
            Write-GitState -MergeCommit $mergeCommit
            exit 2
        }
        if ($seedRows -eq 0) {
            Write-Host ''
Write-Host "STOPPED: the baseline could not be measured (exit 2, no reading taken; the run exited $seedExit)" -ForegroundColor Yellow
            # The run's own last lines are the only place that says why it measured nothing: without
            # them the refusal says "no rows" and the reader has to guess whether it was the plan
            # set, the slot, the host or the tree. The transcript is removed on the way out, so this
            # is the one moment they can be read.
            Write-Host '  the baseline run returned no rows; its last lines:' -ForegroundColor Yellow
            $seedRun | Select-Object -Last 6 | ForEach-Object { "    $_" }
            Write-Host "  This is not a defect in your tree: the baseline run on develop $($developTip.Substring(0, 9)) recorded no row, so nothing was learned about this change and nothing was pushed."
            Write-Host "  What repairs it: the row block must arrive with rows inside it. Re-run internal/tools/camel-remote.ps1 -Path $baselinePath -Changed -ChangedBase $head -ListKnown -NoGpuWatch and read what it prints; a build that failed or a plan set that starts no case writes no block. An empty baseline is never stored -- it would call every case fixed."
            Write-Host ''
            Write-GitState -MergeCommit $mergeCommit
            exit 2
        }
# The seconds and the case count are the reading this step owes: "it measured" is only worth
        # something if it says how much. They travel into the entry's `.where` beside the shas.
        $env:CAMEL_BASELINE_SECONDS = [string][int]$seedSeconds
        $accept = @(& node (Join-Path $toolRoot 'scripts\merge-readings.mjs') --transcript $seedTranscript `
            --cache $compareDir --develop $developTip --plans $planSetFile --tree $measuredTree --accept 2>&1)
        $acceptExit = $LASTEXITCODE
        Remove-Item Env:\CAMEL_BASELINE_SECONDS -ErrorAction SilentlyContinue
        if ($acceptExit -ne 0) {
            $accept | ForEach-Object { "  $_" }
            Write-Host ''
            Write-Host 'STOPPED: the baseline was measured but could not be stored (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host "  This is not a defect in your tree: $seedRows row(s) came back from develop $($developTip.Substring(0, 9)) in $([int]$seedSeconds) s and the store refused them, so nothing was pushed and nothing was learned about this change."
            Write-Host "  What repairs it: the comparer's own refusal is above; it names what about the rows or the cache it could not use."
            Write-Host ''
            Write-GitState -MergeCommit $mergeCommit
            exit 2
        }
        $accept | ForEach-Object { "  $_" }
        $seededFrom = $measuredTree
        Write-Host ("  baseline seeded from tree {0} (the first parent, in the worktree at {1} on the gate's own branch; NOT the tree above): {2} case(s), {3} already red on develop, {4:N0} s" -f `
            $measuredTree.Substring(0, 9), $baselinePath, $seedRows, $seedReds, $seedSeconds)
    } elseif ($probeExit -ne 0) {
        $probe | ForEach-Object { "  $_" }
        Write-Host ''
        Write-Host "STOPPED: whether a baseline exists could not be answered (exit $probeExit, no reading taken)" -ForegroundColor Yellow
        Write-Host '  This is not a defect in your tree: the cache was not read, so nothing was compared and nothing was pushed.'
        Write-Host '  What repairs it: the comparer''s own line is above; `--probe` reads one key and needs no rows, so a refusal here is about the cache directory or the plan set file, not about the tree.'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    } else {
        $probe | ForEach-Object { "  $_" }
        Write-Host ("  baseline hit, nothing measured twice ({0:N0} s)" -f ((Get-Date) - $rowsStarted).TotalSeconds)
    }

    # One measurement, one transcript. The rows are read out of it twice -- once for the comparison
    # and once, after the push, to store this commit's own reading as the baseline for the next
    # merge -- so the plan runs once and both readers read the same run.
$transcript = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-rows-$PID.txt")
    if ($planTargets.Count -gt 0) {
        # The same plan set, named the same way, as the baseline seeding run below: `-Changed` applies
        # the runner's tier rules to a plan file, `-Plan` runs the file whole, and the two sides then
        # measured 8 different cases (`parse.frontend` and `docs.links`) for one merge whose trees were
        # both readable. The set was computed once above; both runs are given it.
        $rowsQuoted = @($planTargets | ForEach-Object { "'" + ("$_" -replace "'", "''") + "'" }) -join ','
        $rowsCommand = "& '" + (Join-Path $toolRoot 'internal\tools\camel-remote.ps1') + "' -Path '" +
            ($MainCheckout -replace "'", "''") + "' -ListKnown -NoGpuWatch -Plan @(" + $rowsQuoted + ")"
        $rowsRun = @(& pwsh -NoProfile -Command $rowsCommand 2>&1)
    }
    else {
        $rowsRun = @(& pwsh -NoProfile -File (Join-Path $toolRoot 'internal\tools\camel-remote.ps1') `
            -Path $MainCheckout -Changed -ChangedBase $developTip -ListKnown -NoGpuWatch 2>&1)
    }
    $rowsExit = $LASTEXITCODE
    # Byte for byte, without a BOM and with LF: the comparer splits on `\n` and matches the markers
    # exactly, and a transcript written by `>` in PowerShell would be UTF-16.
    [System.IO.File]::WriteAllText($transcript,
        (($rowsRun | ForEach-Object { "$_" }) -join "`n") + "`n",
        (New-Object System.Text.UTF8Encoding($false)))
    $rowsCount = @($rowsRun | Where-Object { "$_".TrimStart().StartsWith('{') }).Count
    Write-Host ("  {0} row(s) from the run, camel-remote exit {1}, {2} plan target(s){3}" -f `
        $rowsCount, $rowsExit, $planTargets.Count, $(if ($widened) { ' (the whole fast layer)' } else { '' }))

    # The pass record is built from the comparer's own file -- the sets it judged, the plan set and
    # its hash, the baseline it compared against -- so `--out` is asked for here. Reassembling that
    # from the transcript would read a second time what was already read once, and the second reading
    # is where the two would disagree.
    $diffDir = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-merge-diff-$PID")
    New-Item -ItemType Directory -Force -Path $diffDir | Out-Null
    $compare = @(& node (Join-Path $toolRoot 'scripts\merge-readings.mjs') --transcript $transcript `
        --cache $compareDir --develop $developTip --plans $planSetFile --tree $comparedTree `
        --changed-files $changedFilesFile --message-file $messageFile --out $diffDir 2>&1)
    $compareExit = $LASTEXITCODE

    # The same three-way convention as the clang step. 1 is a reading -- the cases that moved are
    # named above, and the message says which class each is. 2 is the absence of one: the rows or
    # the baseline were not there, and that says nothing about the tree.
    #
    # Both refusals say what is true at this point and not what Stop-With says: the merge commit is
    # already made in this checkout (that is step 1 of this script), and it is the push that has not
    # happened. A reader who acts on "nothing was committed" would look for a merge that is there.
    if ($compareExit -eq 1) {
        # A backfill of a merge made earlier may find a case red at the snapshot it is recording and
        # fixed on the branch since. That is not a defect in the merge: the judgement `-MergeCommit`
        # applies ("this commit's tree against its first parent, no new red") is the judgement of the
        # moment, and the moment has passed. The allowance below re-establishes the fact from a
        # reading of the tip, one case at a time, and refuses the whole idea when anything about it
        # does not check out. Without `-KnownAtSnapshot` this is the refusal it always was.
        $allowed = @()
        if ($KnownAtSnapshot) {
            if (-not $MergeCommit) {
                Stop-With '-KnownAtSnapshot is only for a backfill' 'It records a merge made earlier, so it needs -MergeCommit. The ordinary merge path reads the tree it is merging and has nothing to be allowed for.'
            }
            $list = Get-Content -LiteralPath $KnownAtSnapshot -Raw | ConvertFrom-Json
            if (-not $list.tip -or -not $list.run -or -not @($list.entries).Count) {
                Stop-With "-KnownAtSnapshot $KnownAtSnapshot is not a list" 'It needs "tip" (the commit the evidence was read on), "run" (the run that read it) and a non-empty "entries".'
            }
            $tipSha = "$(& git rev-parse --verify "$($list.tip)^{commit}" 2>$null)".Trim()
            if (-not $tipSha) { Stop-With "-KnownAtSnapshot tip $($list.tip) does not resolve to a commit" 'The evidence must name a commit this repository has.' }
            $runJson = Join-Path $list.run 'run.json'
            $runSummary = Join-Path $list.run 'summary.json'
            foreach ($f in @($runJson, $runSummary)) {
                if (-not (Test-Path -LiteralPath $f)) { Stop-With "-KnownAtSnapshot run $($list.run) has no $(Split-Path -Leaf $f)" 'The evidence is a run of the gate: run.json and summary.json are what make it checkable.' }
            }
            $revision = "$((Get-Content -LiteralPath $runJson -Raw | ConvertFrom-Json).revision)".Trim()
            if ($revision -ne $tipSha) {
                Stop-With "-KnownAtSnapshot run $($list.run) measured $($revision.Substring(0, [Math]::Min(9, $revision.Length))), not the tip $($tipSha.Substring(0, 9))" 'A reading of another tree is not evidence about the tip.'
            }
            $summary = Get-Content -LiteralPath $runSummary -Raw | ConvertFrom-Json
            $diff = Get-Content -LiteralPath (Join-Path $diffDir 'diff.json') -Raw | ConvertFrom-Json
            $newRed = @($diff.new | ForEach-Object { if ($_ -is [string]) { $_ } elseif ($_.name) { "$($_.name)" } else { "$_" } })
            # The second shape of the same fact: a case this merge *adds* and that is red in its own
            # tree -- not a regression, because the first parent never had it. A backfill meets them
            # for the same reason it meets `new`: the tree was fixed forward. The record says which of
            # the two each entry is, because "red where it stood" and "added red" are different claims
            # and a reader counting them must be able to tell them apart.
            $addedRed = @($diff.addedRed | ForEach-Object { if ($_ -is [string]) { $_ } elseif ($_.name) { "$($_.name)" } else { "$_" } })
            $candidates = @($newRed + $addedRed)
            $named = @()
            foreach ($entry in @($list.entries)) {
                $removedAt = "$($entry.removedAt)".Trim()
                $isRemoval = [bool]$removedAt
                if (-not $entry.case -or -not $entry.snapshot -or (-not $entry.fixedAt -and -not $isRemoval)) {
                    Stop-With "-KnownAtSnapshot entry $(($entry | ConvertTo-Json -Compress)) is incomplete" 'Each entry needs case, snapshot and fixedAt -- or case, snapshot, removedAt and note for a case that was removed instead.'
                }
                $snapshot = "$(& git rev-parse --verify "$($entry.snapshot)^{commit}" 2>$null)".Trim()
                if ($snapshot -ne $mergeCommit) {
                    Stop-With "-KnownAtSnapshot entry $($entry.case) is red at $($entry.snapshot)" "This run records $($mergeCommit.Substring(0, 9)); an allowance for another snapshot says nothing about it."
                }
                if ($candidates -notcontains $entry.case) {
                    Stop-With "-KnownAtSnapshot entry $($entry.case) is neither new nor an added case that is red here" 'A stale allowance is refused rather than carried: it would let a later case through.'
                }
                if ($isRemoval) {
                    # The third outcome of a red case: the tree at the tip does not carry it at all. That
                    # is a fact about the tip, and it is checked the way the fix is -- from the
                    # repository: the name is gone from the plans, the commit that removed it is on the
                    # line to the tip, and that commit's own message is the one quoted in the entry.
                    # "removed" is written as removed; it is not a synonym for fixed.
                    if (-not $entry.note) { Stop-With "-KnownAtSnapshot entry $($entry.case) names removedAt without a note" 'The record quotes the removal commit'; }
                    $gone = "$(& git rev-parse --verify "$removedAt^{commit}" 2>$null)".Trim()
                    if (-not $gone) { Stop-With "-KnownAtSnapshot entry $($entry.case) names $removedAt, which is not a commit here" 'The record says which commit removed the case; a sha nobody can resolve is not that.' }
                    & git merge-base --is-ancestor $gone $tipSha 2>$null
                    if ($LASTEXITCODE -ne 0) { Stop-With "-KnownAtSnapshot entry $($entry.case) names $($gone.Substring(0, 9)) as the removal, and it is not on the tip $($tipSha.Substring(0, 9))" 'A removal that is not on the branch does not describe the tip.' }
                    $leaf = "$($entry.case)"
                    if ($leaf.Contains('/')) { $leaf = $leaf.Substring($leaf.IndexOf('/') + 1) }
                    $stillThere = @(& git grep -l -- $leaf $tipSha -- test/plans 2>$null)
                    if ($stillThere.Count -gt 0) {
                        Stop-With "-KnownAtSnapshot entry $($entry.case) is still in the plans at the tip $($tipSha.Substring(0, 9))" "A case that is there has to be green to be allowed; this one is only gone if nothing in test/plans names it."
                    }
                    $goneBody = (@(& git log -1 --format=%B $gone 2>$null) -join "`n")
                    if (-not $goneBody.Contains("$($entry.note)")) {
                        Stop-With "-KnownAtSnapshot entry $($entry.case) quotes a sentence $($gone.Substring(0, 9)) does not contain" 'The quote is what makes the removal checkable; a sentence the commit does not say is not that.'
                    }
                    $allowed += [pscustomobject]@{ case = $entry.case; kind = 'removed-red'; snapshot = $snapshot; removedAt = $gone; note = "$($entry.note)" }
                    continue
                }
                $fixing = "$(& git rev-parse --verify "$($entry.fixedAt)^{commit}" 2>$null)".Trim()
                if (-not $fixing) { Stop-With "-KnownAtSnapshot entry $($entry.case) names $($entry.fixedAt), which is not a commit here" 'The record says which commit fixed it; a sha nobody can resolve is not that.' }
                & git merge-base --is-ancestor $fixing $tipSha 2>$null
                if ($LASTEXITCODE -ne 0) { Stop-With "-KnownAtSnapshot entry $($entry.case) names $($entry.fixedAt) as the fixing commit, and it is not on the tip $($tipSha.Substring(0, 9))" 'A fix that is not on the branch does not make the case green there.' }
                $row = @($summary.test_timings | Where-Object { "$($_.plan)/$($_.test)" -eq $entry.case })
                if ($row.Count -eq 0) { Stop-With "-KnownAtSnapshot entry $($entry.case) was not read by $($list.run)" 'The run that is offered as evidence does not hold the case.' }
                if ("$($row[0].status)" -ne 'pass') {
                    Stop-With "-KnownAtSnapshot entry $($entry.case) is $($row[0].status) on the tip $($tipSha.Substring(0, 9)), not pass" 'The allowance is for a case that is green now; this one is not.'
                }
                $kind = if ($newRed -contains $entry.case) { 'new' } else { 'added-red' }
                $allowed += [pscustomobject]@{ case = $entry.case; kind = $kind; snapshot = $snapshot; fixedAt = $fixing }
            }
            $uncovered = @($candidates | Where-Object { $name = $_; -not ($allowed | Where-Object { $_.case -eq $name }) })
            if ($uncovered.Count -gt 0) {
                $compare | ForEach-Object { "  $_" }
                Stop-With "-KnownAtSnapshot does not cover $($uncovered -join ', ')" "Everything the comparison calls new or adds red has to be named in the list, with the commit that fixed it and a pass on the tip -- or with the commit that removed it. $($allowed.Count) of $($candidates.Count) were."
            }
            $compare | Where-Object { "$_".StartsWith('merge-readings:') } | ForEach-Object { "  $_" }
            foreach ($entry in $allowed) {
                if ($entry.kind -eq 'removed-red') {
                    Write-Host ("  allowed: {0} is red at this snapshot ({1}) and the tip does not carry it; case removed at {2}, not fixed" -f `
                        $entry.case, $mergeCommit.Substring(0, 9), $entry.removedAt.Substring(0, 9))
                }
                else {
                    Write-Host ("  allowed: {0} is {1} at this snapshot ({2}) and pass at {3}; fixed at {4}" -f `
                        $entry.case, $(if ($entry.kind -eq 'new') { 'red' } else { 'added red' }), $mergeCommit.Substring(0, 9), $tipSha.Substring(0, 9), $entry.fixedAt.Substring(0, 9))
                }
            }
            Write-Host ("  -KnownAtSnapshot: the record carries these as `knownAtSnapshot` (red at the snapshot, fixed later); no other case moved ({0:N0} s)" -f ((Get-Date) - $rowsStarted).TotalSeconds)
            # The comparer's exit describes a comparison that has now been judged acceptable, case by
            # case, with evidence. Leaving it at 1 would send this run into the "no reading taken"
            # path below, which is about a comparison that never happened.
            $compareExit = 0
        }
        else {
            $compare | ForEach-Object { "  $_" }
            Write-Host ''
            Write-Host 'STOPPED: the plans this merge touches measured differently on the merged tree (exit 1: the cases are named above)' -ForegroundColor Red
            Write-Host "  The baseline is the tree this merge lands on (develop $($developTip.Substring(0, 9))) and this merge is the only thing that moved. `newly red` was green there and is red now; `still red, failing differently` was red both times and reads differently now, which is the move a count cannot see."
            Write-Host '  If the change makes a case fail on purpose, the merge that makes it fail that way is the one that carries the expectation with it; this one is refused until the two agree.'
            Write-Host ''
            Write-GitState -MergeCommit $mergeCommit
            exit 1
        }
    }
    if ($compareExit -ne 0) {
        $compare | ForEach-Object { "  $_" }
        Write-Host ''
        Write-Host "STOPPED: the row readings were not taken (exit $compareExit, no reading taken)" -ForegroundColor Yellow
        Write-Host "  This is not a defect in your tree: there were no rows to compare, so nothing was learned about this change. The run that should have recorded them exited $rowsExit; the comparer's own refusal is above."
        Write-Host "  What repairs it: the block must arrive with rows inside it. A run that selected no plan writes no block, and a plan set whose cases all start nothing writes an empty one. This script measures the baseline itself (tree $($developTip.Substring(0, 9)) against tree $($comparedTree.Substring(0, 9))) and there is no command to run by hand for it; if the refusal above is the baseline being this very tree, the two shas are the reading to look at."
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 2
    }
    # The two shas on the one line that says "measured": a reader who trusts this line has to be able
    # to see that it was two trees, not one tree twice.
    #
    # The comparer's line is echoed even when it allowed the merge: a case this change added and a case
    # it removed are names the record has to carry, and a green that says only "no new red" is a green
    # whose case set the reader cannot see.
    $compare | Where-Object { "$_".StartsWith('merge-readings:') } | ForEach-Object { "  $_" }
    Write-Host ("  measured: baseline tree {0} ({1}) against this tree {2}, no new red and no changed failure ({3:N0} s)" -f `
        $developTip.Substring(0, 9), $(if ($seededFrom) { 'seeded now' } else { 'from the cache' }), `
        $comparedTree.Substring(0, 9), ((Get-Date) - $rowsStarted).TotalSeconds)

    # --- the third state: the reading arrived and the exit code did not ---------
    #
    # The gate prints every reading and then reports `exit unknown` (exit 7) when its closing session
    # did not return: the rows all arrived, and nothing says the run's own exit was 0. That is
    # neither a pass nor a red reading, and both of the things this script used to do with it were
    # wrong: no exit code meant no record at all (the reading existed on screen and nowhere else),
    # and a hand-killed ssh meant a merge could go on with no record behind it.
    #
    # So: the record is written and says `exit=unknown`, and the push is not attempted. The pre-push
    # hook reads that record and refuses it in exactly those words, so the answer is the same whether
    # the refusal happens here or at the hook. What repairs it is one more measurement of the same
    # merge commit, which now has a gate that cannot hang: the record is then overwritten with exit=0.
    if ($rowsExit -eq 7) {
        $passedDir = Join-Path $commonDir 'camel-passed'
        New-Item -ItemType Directory -Force -Path $passedDir | Out-Null
        $unknownRecord = @(& node (Join-Path $toolRoot 'scripts\merge-record.mjs') write `
            --diff (Join-Path $diffDir 'diff.json') --merge $mergeCommit --first-parent $developTip `
            --second-parent $head --branch $Branch --tree $MainCheckout --out $passedDir `
            --baseline-source $(if ($seededFrom) { "measured now on $($developTip.Substring(0, 9))" } else { "cached from an earlier measurement of $($developTip.Substring(0, 9))" }) `
            --gate headers --gate clang-syntax --gate rows --exit unknown 2>&1)
        $unknownRecordExit = $LASTEXITCODE
        $unknownRecord | ForEach-Object { "  $_" }
        Write-Host ''
        Write-Host 'STOPPED: the run printed every reading and never returned its exit code (exit unknown, 7)' -ForegroundColor Yellow
        Write-Host "  The rows above are this merge's reading and the comparison allowed them, so the reading is kept: its record is at $passedDir\$mergeCommit.json and says exit=unknown. It is not a pass -- nothing says the run''s checkers were green -- so nothing was pushed."
        if ($unknownRecordExit -ne 0) {
            Write-Host "  The record could not be written either (merge-record exit $unknownRecordExit); the transcript is at $transcript."
        }
        Write-Host '  What repairs it: measure this same merge commit again. The gate now cuts a session that stops answering after [SPLIT] and takes the exit code from the run''s own last line, so the re-run either lands a real verdict or says the same thing quicker; a green one overwrites this record with exit=0.'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 7
    }

    if ($NoPush) {
        Write-Host ''
        Write-Host 'would push: every reading was taken and neither tree refused it. Nothing was pushed (-NoPush).' -ForegroundColor Yellow
        Write-Host '  The baseline this merge would store for its own tree is not written: a key for a commit origin cannot see is a key nobody asks for.'
        # The trial is exactly the shape the fixed sentence used to describe wrongly: a real merge commit
        # on local develop, origin untouched. Said out of git, it is the same words a refused merge would
        # get -- which is the point: the state is the state, and whether it was this run's choice is the
        # line above ("-NoPush"), not a different sentence about the repository.
        Write-GitState -MergeCommit $comparedTree
        Write-Host ''
        Write-Host "tried $Branch at $head; nothing was pushed (-NoPush)"
        exit 0
    }

    # --- the push, and the one recovery a refused push may make ------------------
    # Everything above measured one tree: the merge commit. The target branch moves while a merge is
    # being measured -- a documentation commit is pushed by whoever writes pages, and no merge seat
    # writes them -- and the push then lands on a tip that is not the one this merge was read
    # against. Until this existed the only answer was to run the whole thing again: eleven minutes,
    # three times in one night, for commits that touch no code.
    #
    # What the recovery may do is decided by the file paths of the commits that landed (see
    # Split-ChangeByPath): merge them into the merge commit and re-run the plans they select, or stop
    # and say the push touches code. It is attempted once. Everything else about the push is what it
    # was: the verdict on whether to push at all is the row comparison above, and `new` and `changed`
    # empty is still what "the readings allow it" means.
    # What the plans of a changed-file list are, read into the fresh tree: `--changed` over the landed
    # file list, then the same run the row stage uses, on the plans that list selects. Both answers
    # come back as data, so a refusal that names nothing is a refusal and not an empty green.
    $rerunPlans = {
        param([string[]] $ChangedFiles)
        $listFile = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-rebase-files-$PID.txt")
        [System.IO.File]::WriteAllText($listFile, (($ChangedFiles -join "`n") + "`n"),
            (New-Object System.Text.UTF8Encoding($false)))
        try {
            $selection = Get-PlansForChangedFiles -Checkout $MainCheckout -ListFile $listFile
            if ($selection.Exit -ne 0 -or $selection.Widened -or $selection.Targets.Count -eq 0) {
                return @{ Exit = 2; Targets = @(); Rows = 0; Reds = 0; Text = $selection.Text }
            }
            $run = Invoke-PlansOnTree -Checkout $MainCheckout -PlanTargets $selection.Targets
            return @{ Exit = $run.Exit; Targets = $selection.Targets; Rows = $run.Rows; Reds = $run.Reds; Text = $run.Text }
        } finally {
            Remove-Item -LiteralPath $listFile -Force -ErrorAction SilentlyContinue
        }
    }

    # --- the record the pre-push hook reads -------------------------------------
    # A merge commit may reach the target only when a gate wrote down that it passed. The hook
    # (`scripts/hooks/pre-push`) walks the first-parent range of the push and refuses a merge commit
    # with no record, so the record is written before the push and removed if the push does not land:
    # a claim about a commit nobody can fetch has no reader, and one left behind would let a later
    # push of this commit pass on a reading taken now. That is the same mistake as a merge made on
    # local `develop` -- a judgement outliving the tree it was about.
    $passedDir = Join-Path $commonDir 'camel-passed'
    New-Item -ItemType Directory -Force -Path $passedDir | Out-Null
    # Splatted as two arguments: a single string with a space in it is one argument, and the record
    # writer would read it as a flag nobody passed.
    $knownArgs = @(if ($allowed.Count -gt 0) { '--known-at-snapshot'; $KnownAtSnapshot })
    # A different layer, a different switch: the reading allowance says a case was red and is not
    # any more, this one says a file had no header and has one later. They travel as separate
    # fields so the record never has to be read twice to know which of the two it is.
    if ($headerAllowed.Count -gt 0) {
        $knownArgs += @('--known-header-at-snapshot'; $KnownHeaderAtSnapshot)
    }
    $recordOut = @(& node (Join-Path $toolRoot 'scripts\merge-record.mjs') write `
        --diff (Join-Path $diffDir 'diff.json') --merge $mergeCommit --first-parent $developTip `
        --second-parent $head --branch $Branch --tree $MainCheckout --out $passedDir `
        --baseline-source $(if ($seededFrom) { "measured now on $($developTip.Substring(0, 9))" } else { "cached from an earlier measurement of $($developTip.Substring(0, 9))" }) `
        --gate headers --gate clang-syntax --gate rows --exit 0 @knownArgs 2>&1)
    $recordExit = $LASTEXITCODE
    $recordOut | ForEach-Object { "  $_" }
    if ($recordExit -ne 0) {
        Stop-With "the pass record for $($mergeCommit.Substring(0, 9)) could not be written (exit $recordExit)" 'The pre-push hook refuses a merge commit without one, so the push is not attempted.' $mergeCommit
    }
    $recordFile = Join-Path $passedDir "$mergeCommit.json"

    Write-Host ''
    Write-Host "pushing HEAD:$Target ..."

    $push = Invoke-PushWithRecovery -Tree $MainCheckout -Target $Target -ReRun $rerunPlans
    $push.Lines | Where-Object { $_ } | ForEach-Object { Write-Host $_ }

    if (-not $push.Ok) {
        # The record names a reading of the merge commit. A push that did not land leaves a claim
        # about a commit nobody can fetch, so it is removed with the failure rather than outliving it.
        Remove-Item -LiteralPath $recordFile -Force -ErrorAction SilentlyContinue
        switch ($push.Kind) {
            'touches code' {
                Stop-With "origin/$Target moved while this merge was read, and the commits that landed touch code" `
                    "$(if ($push.Shas.Count -eq 2) { "$($push.Shas[0].Substring(0,9)) -> $($push.Shas[1].Substring(0,9))" }): a partial re-run of that tree would be a reading about a change this gate never selected, so none was taken. A full run is a decision for whoever started this merge: the command above redoes the merge on the tip, and git config rerere.enabled true reuses the resolutions already made. Nothing was pushed." $mergeCommit
            }
            'not a merge' {
                # A lane commit that would enter the target without a merge: no recovery can make it
                # an arrival, because the lane it speaks for is not on a second parent at all. The
                # sentence and the redo command were printed above, by the reading itself.
                Stop-With "the commit about to be pushed to $Target is not a merge commit" `
                    "Every arrival must be a merge whose first parent is the tip of $Target; a single-parent commit goes straight in and the first-parent readers cannot tell it from an arrival. The redo command is above, and nothing was pushed." $push.Sha
            }
            'second rejection' {
                # The one recovery has been made and the branch moved again. The shas are the whole
                # report: a third attempt of the same action is not persistence, it is the loop this
                # stage was written to stop walking.
                Write-Host ''
                Write-Host "STOPPED: origin/$Target moved again after the one recovery this run may make; nothing further was attempted" -ForegroundColor Red
                Write-Host "  $($push.Shas[0]) -> $($push.Shas[1])"
                Write-Host ''
                exit 1
            }
            'no plan' {
                Write-Host ''
                Write-Host 'STOPPED: the documentation that landed selects no plan, so the re-landed tree was not read (exit 2, no reading taken)' -ForegroundColor Yellow
                Write-Host '  The re-land is on this checkout''s HEAD and nothing was pushed. The selection widened or answered nothing, and a widened selection is the whole fast layer: re-running it here is a full run wearing the name of a partial one.'
                Write-Host '  What repairs it: `node scripts/test.js --changed --changed-files <list> --list` is the selection and starts nothing; test/plan-selection.json needs a rule for the files that landed.'
                Write-Host ''
                Write-GitState -MergeCommit $push.Sha
                exit 2
            }
            'red' {
                Stop-With "the plans that read what landed are red on the re-landed tree (exit 1: $($push.Reds) failing case(s) of $($push.Rows))" `
                    "The re-land is on this checkout's HEAD and nothing was pushed: a red is a red wherever it is measured. Fix it, or re-read the merge from origin/$Target." $push.Sha
            }
            'conflict' {
                Stop-With "the documentation that landed could not be merged into the merge commit" `
                    'The merge was aborted, so this checkout is where it was and nothing was pushed. The lane and the landing wrote the same page: resolve which text is right, then merge again.' $push.Sha
            }
            'tip did not move' {
                Stop-With "the push was refused but origin/$Target did not move" `
                    'Nothing arrived to re-land, so this refusal is not a non-fast-forward race and this run does not recover from it. The push output is above; the merge is committed locally and not on origin.' $push.Sha
            }
            'fetch failed' {
                Stop-With 'git fetch failed after the push was refused' 'What landed cannot be read, so nothing was re-landed and nothing was pushed.' $push.Sha
            }
            default {
                Stop-With "git push failed after $($push.Tries) attempt(s)" "The merge is committed locally and not on origin/$Target." $push.Sha
            }
        }
    }
    if ($push.Recovered) { $mergeCommit = $push.Sha }

    $remote = ((& git ls-remote origin "refs/heads/$Target") -split "`t") | Select-Object -First 1
    $local = "$(& git rev-parse HEAD)".Trim()
    if ($remote -ne $local) {
        Remove-Item -LiteralPath $recordFile -Force -ErrorAction SilentlyContinue
        Stop-With "origin/$Target is not the commit just made" "local $local, remote $remote" $mergeCommit
    }

    # The commit this merge just created is the tree those rows were measured on, so they are that
    # commit's baseline: storing them now is what makes the next merge on it a comparison instead of
    # a second measurement. After the push and never before -- a baseline written for a commit
    # nothing else can see would be a key nobody asks for, and a failed push would leave one.
    # The directory is made here because the comparer refuses a cache that is not there (that
    # refusal is what keeps a missing baseline from reading as "every case fixed"): the first merge
    # on a machine has no cache yet, and creating it is this script's job, not the reader's.
    #
    # Not after a recovery. The rows in the transcript were read on the merge commit, and the commit
    # that landed is that merge plus the documentation that arrived -- a different tree. Storing them
    # under the landed commit's key would make the next merge compare against rows for a tree nobody
    # measured, which is the one failure the comparer cannot see (`--tree` would name the landed
    # commit, because that is the commit the cache is keyed by). So the key is left unset and the next
    # merge measures: slower, and true.
    New-Item -ItemType Directory -Force -Path $compareDir | Out-Null
    if ($push.Recovered) {
        Write-Host "  warning: no baseline was stored for $($local.Substring(0,9)): the rows this run read are the rows of $($comparedTree.Substring(0,9)), and the documentation that landed made the pushed tree a different one. The next merge on that commit measures instead of comparing"
    } else {
        & node (Join-Path $toolRoot 'scripts\merge-readings.mjs') --transcript $transcript `
            --cache $compareDir --develop $local --plans $planSetFile --tree $local --accept 2>&1 |
            Select-Object -Last 1 | ForEach-Object { "  $_" }
        if ($LASTEXITCODE -ne 0) {
            # Not a refusal: the merge is already on origin, and a merge that did land is not undone by
            # a baseline that could not be written. The next merge on this commit measures instead of
            # comparing, which is slower and not wrong.
            Write-Host "  warning: the baseline for $($local.Substring(0,9)) was not stored; the next merge on that commit will measure instead of comparing"
        }
    }

    Write-Host ''
    # Which of the two the next merge will do, read rather than left to be discovered as a slow
    # merge: a baseline for this commit covers the plans this run selected, and the fast layer covers
    # every merge whose selection stays inside it.
    $tipProbe = Test-FastLayerBaseline -CacheDir $compareDir -Develop $local -Checkout $toolRoot
    if ($tipProbe.Hit) {
        Write-Host "  the next merge on $($local.Substring(0,9)) compares against that fast-layer baseline and measures only its own tree"
    } else {
        Write-Host "  the next merge on $($local.Substring(0,9)) measures a baseline of its own: internal\tools\merge-lane.ps1 -WarmBaseline measures the fast layer once on this commit, when the test slot is free, and every merge after it compares instead"
    }
    # After a recovery the green line has to carry its coverage, and the coverage is not the layer the
    # merge selected: the plans re-run are the ones the landed documentation selects, nothing else was
    # measured a second time, and the row comparison above still speaks only for the tree it read. A
    # line that said "merged" alone would let a reader take a partial reading for a full one -- the
    # same shape as the `0 fixed, 0 new, 0 changed` green this stage was built to make impossible.
    if ($push.Recovered) {
        Write-Host (Format-RecoveryConclusion -Branch $Branch -Head $head -Target $Target -Local $local `
                -Targets $push.Targets -Files $push.Files.Count -MeasuredTree $comparedTree) -ForegroundColor Green
        Write-Host ("  those {0} plan(s): {1} case(s), {2} red, on the re-landed tree {3}" -f `
                $push.Targets.Count, $push.Rows, $push.Reds, $local.Substring(0, 9))
    } else {
        Write-Host "merged $Branch at $head; $Target is $($local.Substring(0,9))" -ForegroundColor Green
    }
    # The closing claim read back rather than asserted: after the push both ends are one sha, and the
    # line says which. A push that reported success and a ref that did not move is a reading worth
    # seeing, so the two are compared here instead of being implied by the word "merged".
    $headNow = "$(& git rev-parse --verify 'HEAD^{commit}' 2>$null)".Trim()
    $originNow = "$(& git rev-parse --verify "refs/remotes/origin/$Target^{commit}" 2>$null)".Trim()
    if ($headNow -and $headNow -eq $originNow -and $headNow -eq $local) {
        Write-Host "  origin/$Target is $originNow, which is this checkout's HEAD: the merge is on origin."
    } else {
        Write-Host "  HEAD is $headNow and origin/$Target is $originNow; this push reported success, so these should be the one commit $local and are not -- read them before trusting this merge." -ForegroundColor Yellow
    }
}
finally {
    if ($transcript) { Remove-Item -LiteralPath $transcript -Force -ErrorAction SilentlyContinue }
    if ($seedTranscript) { Remove-Item -LiteralPath $seedTranscript -Force -ErrorAction SilentlyContinue }
    if ($planSetFile) { Remove-Item -LiteralPath $planSetFile -Force -ErrorAction SilentlyContinue }
    if ($changedFilesFile) { Remove-Item -LiteralPath $changedFilesFile -Force -ErrorAction SilentlyContinue }
    if ($messageFile) { Remove-Item -LiteralPath $messageFile -Force -ErrorAction SilentlyContinue }
    if ($diffDir) { Remove-Item -LiteralPath $diffDir -Recurse -Force -ErrorAction SilentlyContinue }
    # (the changed-file list is not a temp file: both sides read it from git)
    Pop-Location
}
