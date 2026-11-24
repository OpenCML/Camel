<#
.SYNOPSIS
    Merge a lane branch into develop, and refuse to commit or push unless the merge actually happened.

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
    asserted first: `git fetch`, then local `develop` must be `origin/develop`. When it is not, the
    commits only local `develop` holds are printed. They are, in the case that made this necessary,
    the merge commits a previous run made and could not push -- which is why that merge's first parent
    was not the commit on origin, why the push was refused with a message about something else, and
    why local `develop` kept drifting one more merge per run. When the commits on its first-parent chain
    are only such merges (a merge commit, no gate record for it, and a tree that is exactly what a
    plain merge of its two parents produces), they are kept under a tag and local `develop` is moved
    back to `origin/develop` before this merge starts. Any other shape stops with nothing merged and
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

    Every stop after the merge says what git holds at that moment -- local `develop`, `origin/develop`
    and the commits only local `develop` holds -- instead of the constant sentence it used to print,
    which claimed nothing had been committed while a merge commit sat on the branch that origin did
    not have.

.PARAMETER Branch
    The lane branch to merge, e.g. lane/range-ri.

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
    Where the baseline is measured. It is a worktree of its own, detached at the tree the merge lands
    on, and it is kept between merges so the host's build of it stays warm.

.EXAMPLE
    .\merge-lane.ps1 -Branch lane/range-ri -Message "merge range-ri: ..." -WhatIf
#>
[CmdletBinding()]
param(
    # Neither is Mandatory: -WarmBaseline starts no merge and names no branch, and a Mandatory
    # parameter would make PowerShell prompt for one. A merge without them is refused below, by name.
    [string] $Branch = '',
    [string] $Message = '',
    # The merge seat's own worktree. Empty means "ask the settings file": `camel-gate.local.json`
    # under `mergeWorktree`, the same key `scripts/hooks/pre-push` reads to decide which tree may
    # push develop. Two merge positions and a constant here is how the tool came to merge in a tree
    # the hook would then refuse -- after the merge had run. `-MainCheckout` is the one-off override.
    [string] $MainCheckout = '',
    # Where the baseline is measured. It has to be a tree that is NOT the one being compared, and it
    # has to be a real checkout (the gate builds it), so it is a worktree of this repository detached
    # at the first parent. One fixed path and not one per merge: the host keeps the build of that
    # worktree between runs, and a fresh path per merge would mean a cold build per merge.
    [string] $BaselineWorktree = 'D:\Projects\Camel-wt\camel-baseline',
    [switch] $WhatIf,
    [switch] $NoPush,
    [switch] $WarmBaseline
)

$ErrorActionPreference = 'Stop'

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

    $branch = "$(& git symbolic-ref -q --short HEAD 2>$null)".Trim()
    $headHere = "$(& git rev-parse --verify HEAD 2>$null)".Trim()
    $local = "$(& git rev-parse --verify 'refs/heads/develop^{commit}' 2>$null)".Trim()
    $origin = "$(& git rev-parse --verify 'refs/remotes/origin/develop^{commit}' 2>$null)".Trim()
    $where = if ($branch) { "the branch $branch" } else { 'a detached HEAD' }

    if (-not $local -or -not $origin) {
        # One of the two ends is missing, so "same" cannot be answered at all. That is said plainly
        # rather than defaulted: a default here would be the same class of mistake as the constant.
        $localText = if ($local) { $local } else { 'unreadable' }
        $originText = if ($origin) { $origin } else { 'unreadable' }
        Write-Host "  Nothing was pushed. Local develop reads $localText and origin/develop reads $originText; with one end unreadable, whether they are the same commit cannot be stated."
        return
    }

    # `@( ... )` around the whole pipeline, not around the git call: one line of output is a string,
    # and a string indexed at [0] is its first character -- this said "the commit is d" once.
    $only = @(& git rev-list 'origin/develop..develop' 2>$null |
        ForEach-Object { "$_".Trim() } | Where-Object { $_ })
    $held = if ($only.Count -eq 0) {
        'Local develop holds no commit that origin/develop does not have, so it is behind rather than ahead.'
    } elseif ($only.Count -eq 1) {
        "The commit only local develop holds is $($only[0])."
    } else {
        "The commits only local develop holds are $($only -join ', ')."
    }

    if (-not $MergeCommit) {
        if ($local -eq $origin) {
            Write-Host "  Nothing was committed and nothing was pushed: local develop is still $local, which is origin/develop."
        } else {
            Write-Host "  Nothing was committed and nothing was pushed by this run: local develop is $local and origin/develop is $origin. $held"
        }
        return
    }
    if ($local -eq $origin -and $MergeCommit -ne $local) {
        # A trial on a scratch branch: the merge commit is real but it is not on develop, so develop
        # still reads what origin has and the push would publish nothing of it.
        Write-Host "  The merge was committed locally and not pushed: the merge commit is $MergeCommit on $where, and local develop is still $local, which is origin/develop."
        return
    }
    if ($local -eq $MergeCommit) {
        Write-Host "  The merge was committed locally and not pushed: local develop is $local, and origin/develop is $origin. $held Run this again after restoring local develop (see the message above)."
        return
    }
    Write-Host "  The merge was committed locally and not pushed: the merge commit is $MergeCommit on $where; local develop is $local, and origin/develop is $origin. $held"
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

# --- which tree this merges in ----------------------------------------------
# One name for one machine-local fact: the tree the pre-push hook will accept a push from. A caller
# that has already decided passes `-MainCheckout`; otherwise the settings file answers, and a file
# that names two trees stops the run here rather than at the push.
#
# These two refusals do not go through `Stop-With`: that one reports what git holds, and the git it
# would read is the directory this was started in, not the merge seat -- a wrong tree reported as
# fact is worse than no report. Nothing has been touched at this point, so there is nothing to state.
. (Join-Path $PSScriptRoot 'merge-lane-config.ps1')
try {
    $MainCheckout = Get-MergeWorktree -Config (Join-Path $PSScriptRoot 'camel-gate.local.json') -Explicit $MainCheckout
} catch {
    Write-Host ''
    Write-Host "STOPPED: the merge worktree could not be read" -ForegroundColor Red
    Write-Host "  $($_.Exception.Message)"
    Write-Host '  Nothing was merged and nothing was pushed.'
    Write-Host ''
    exit 1
}
if (-not (Test-Path -LiteralPath $MainCheckout)) {
    Write-Host ''
    Write-Host "STOPPED: the merge worktree is not there: $MainCheckout" -ForegroundColor Red
    Write-Host "  The settings file names the tree the pre-push hook accepts a push from; a rename leaves both pointing at nothing."
    Write-Host '  Nothing was merged and nothing was pushed.'
    Write-Host ''
    exit 1
}
# Said before the merge, not after: a merge takes twenty minutes and a refused push takes the result
# back to the start, so the tree is named while saying no is still free.
Write-Host "[merge-lane] merging in $MainCheckout (the worktree the pre-push hook accepts a push from)"

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
        [Parameter(Mandatory)][string] $Repo
    )

    $ourBranch = 'camel-baseline'
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

    # --- the start point: local develop has to be origin/develop ------------------
    #
    # This merges into the local branch and then pushes it. If local `develop` is not `origin/develop`,
    # the merge commit's first parent is not the commit that is on origin: the push is refused, and the
    # refusal names something else -- the pre-push hook's records, or a non-fast-forward -- so the reader
    # goes looking in the wrong place. The expensive half is what comes next: the merge commit stays on
    # local `develop`, so the following run starts one more merge wrong. Six of those were left in one
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

    $originDevelop = "$(& git rev-parse --verify 'refs/remotes/origin/develop^{commit}' 2>$null)".Trim()
    $localDevelop = "$(& git rev-parse --verify 'refs/heads/develop^{commit}' 2>$null)".Trim()
    if (-not $originDevelop -or -not $localDevelop) {
        Write-Host ''
        Write-Host 'STOPPED: the start point could not be read (exit 2, no reading taken)' -ForegroundColor Yellow
        Write-Host "  local develop reads '$(if ($localDevelop) { $localDevelop } else { 'nothing' })' and origin/develop reads '$(if ($originDevelop) { $originDevelop } else { 'nothing' })'."
        Write-Host '  This is not a defect in the lane: the assertion this merge begins with cannot be made, so nothing was merged and nothing was pushed.'
        Write-Host '  What repairs it: `git rev-parse develop` and `git rev-parse origin/develop` in this checkout. origin/develop missing after a successful fetch means the branch lives on another remote, or does not exist.'
        Write-Host ''
        exit 2
    }

    if ($localDevelop -ne $originDevelop) {
        # What the branch holds and origin does not: every commit, so the reader sees what a merge
        # brought with it as well. The judgement, though, is about develop's FIRST-PARENT chain only:
        # the commits a merge pulls in from a lane are not on that chain, were never merges this tool
        # made on develop, and print-and-refuse over them would refuse every real case (a lane commit
        # is not a merge commit, so it would always be "a person made it").
        $onlyLocal = @(& git rev-list --reverse 'origin/develop..develop' 2>$null |
            ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        $chain = @(& git rev-list --reverse --first-parent 'origin/develop..develop' 2>$null |
            ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        Write-Host ''
        Write-Host "local develop is not origin/develop: local develop is $($localDevelop.Substring(0,9)), origin/develop is $($originDevelop.Substring(0,9))" -ForegroundColor Red
        Write-Host "  $($onlyLocal.Count) commit(s) exist only on local develop (git log --oneline origin/develop..develop), $($chain.Count) of them on develop's first-parent chain:"
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
            Write-Host 'STOPPED: local develop is not origin/develop, and what it holds on its first-parent chain is not all merges this tool made and did not push (exit 2)' -ForegroundColor Red
            foreach ($why in $strangers) { Write-Host "  $why" }
            Write-Host ''
            Write-Host '  Not restoring local develop: which of those commits to keep is not a decision a merge tool makes.'
            Write-Host '  Move local develop to origin/develop yourself (after reading the commits above), or merge from a checkout whose develop is origin/develop.'
            Write-Host ''
            Write-GitState
            exit 2
        }

        # The one repair. It needs a checkout that may be moved: this one is on develop, at that
        # branch's tip, with no uncommitted change. Moving a branch under somebody else's checkout, or
        # resetting over work that is not committed, would be the repair losing the thing it repairs.
        $onBranch = "$(& git symbolic-ref -q --short HEAD 2>$null)".Trim()
        $headHere = "$(& git rev-parse --verify HEAD 2>$null)".Trim()
        $dirty = @(& git status --porcelain --untracked-files=no 2>$null)
        if ($onBranch -ne 'develop' -or $headHere -ne $localDevelop -or $dirty.Count -gt 0) {
            $why = if ($onBranch -ne 'develop') {
                "this checkout is on $(if ($onBranch) { "the branch $onBranch" } else { 'a detached HEAD' }), not on develop, so moving develop here would leave a checkout that disagrees with its own branch"
            } elseif ($headHere -ne $localDevelop) {
                "this checkout's HEAD ($headHere) is not local develop's tip ($localDevelop)"
            } else {
                "this checkout has $($dirty.Count) uncommitted change(s), and the repair would discard them"
            }
            Write-Host ''
            Write-Host 'STOPPED: local develop is not origin/develop, and this checkout is not one the repair may move it in (exit 2)' -ForegroundColor Red
            Write-Host "  $why."
            Write-Host "  What it holds are merges this tool made and did not push, so the repair would lose nothing: local develop $localDevelop, origin/develop $originDevelop."
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
        $strandedRef = "backup/orphan-$($localDevelop.Substring(0,10))"
        $tagRef = "refs/tags/$strandedRef^{commit}"
        $tagged = "$(& git rev-parse --verify $tagRef 2>$null)".Trim()
        if ($tagged -and $tagged -ne $localDevelop) {
            $strandedRef = "$strandedRef-$([string](Get-Date -Format 'yyyyMMdd-HHmmss'))"
            $tagRef = "refs/tags/$strandedRef^{commit}"
            $tagged = "$(& git rev-parse --verify $tagRef 2>$null)".Trim()
        }
        if ($tagged -ne $localDevelop) {
            & git tag $strandedRef $localDevelop 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) {
                Stop-With 'the commits local develop holds could not be kept under a tag' "git tag $strandedRef $localDevelop failed. Nothing is dropped without a record of where it went, so local develop was left where it is."
            }
        }
        & git reset --hard $originDevelop 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            Stop-With 'local develop could not be restored to origin/develop' "git reset --hard $originDevelop failed after those commits were saved under $strandedRef. Local develop is where it was; the ref is the record of what it holds."
        }
        $restored = "$(& git rev-parse --verify 'refs/heads/develop^{commit}' 2>$null)".Trim()
        if ($restored -ne $originDevelop) {
            Stop-With 'local develop was not restored to origin/develop' "after git reset --hard $originDevelop, local develop reads $restored. The commits it held are under $strandedRef."
        }
        Write-Host ''
        # The sentence the author asked for, with the one case that would make it false spelled out
        # instead of glossed: a branch that is behind origin holds no commit origin does not have.
        $heldText = if ($onlyLocal.Count -eq 0) {
            "it was behind origin/develop rather than ahead, so no commit is lost from it"
        } else {
            "the commits only it held are under $strandedRef"
        }
        Write-Host "  restored local develop from $localDevelop to $originDevelop, $heldText" -ForegroundColor Yellow
        Write-Host "  (nothing was dropped: $strandedRef still points at $localDevelop, and git log --oneline origin/develop..$strandedRef lists what it holds)"
        $localDevelop = $originDevelop
    }

    Write-Host "start point: local develop is $($localDevelop.Substring(0,9)), which is origin/develop"

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
        $warmProbe = Test-FastLayerBaseline -CacheDir $warmDir -Develop $originDevelop -Checkout $MainCheckout
        if ($warmProbe.Hit) {
            $warmProbe.Text -split "`n" | Where-Object { $_ } | ForEach-Object { "  $_" }
            Write-Host ''
            Write-Host "  a baseline covering the fast layer is already stored for develop $($originDevelop.Substring(0,9)); nothing was measured (exit 0)"
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
        $warmPath = $BaselineWorktree
        $warmProblem = Sync-BaselineWorktree -Path $warmPath -Sha $originDevelop -CommonDir $commonDir -Repo $MainCheckout
        $warmMeasured = ''
        if ($null -eq $warmProblem) {
            $warmMeasured = "$(& git -C $warmPath rev-parse HEAD 2>$null)".Trim()
            if ($warmMeasured -ne $originDevelop) {
                $warmProblem = "$warmPath is at $($warmMeasured.Substring(0, [Math]::Min(9, $warmMeasured.Length))), not at develop $($originDevelop.Substring(0,9)), so the baseline would come from another tree"
            }
        }
        if ($null -ne $warmProblem) {
            Write-Host ''
            Write-Host 'STOPPED: the fast layer could not be measured, so nothing was stored (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host "  $warmProblem"
            Write-Host "  What repairs it: the baseline is measured in a worktree held at develop ($($originDevelop.Substring(0,9))). Name one with -BaselineWorktree, or let this script create it."
            exit 2
        }
        $warmPlans = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-warm-plans-$PID.txt")
        [System.IO.File]::WriteAllText($warmPlans, "test/plans/smoke`ntest/plans/feat`n",
            (New-Object System.Text.UTF8Encoding($false)))
        $warmTranscript = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-warm-rows-$PID.txt")
        Write-Host ("  measuring the fast layer on develop {0} in {1} -- the run every merge after this one would otherwise do for itself ..." -f `
            $originDevelop.Substring(0,9), $warmPath)
        $warmRun = @(& pwsh -NoProfile -File (Join-Path $MainCheckout 'internal\tools\camel-remote.ps1') `
            -Path $warmPath -Plan @('test/plans/smoke', 'test/plans/feat') -ListKnown -NoGpuWatch 2>&1)
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
        $warmAccept = @(& node (Join-Path $MainCheckout 'scripts\merge-readings.mjs') --transcript $warmTranscript `
            --cache $warmDir --develop $originDevelop --plans $warmPlans --tree $warmMeasured --accept 2>&1)
        $warmAcceptExit = $LASTEXITCODE
        Remove-Item Env:\CAMEL_BASELINE_SECONDS -ErrorAction SilentlyContinue
        $warmAccept | ForEach-Object { "  $_" }
        Remove-Item -LiteralPath $warmPlans, $warmTranscript -Force -ErrorAction SilentlyContinue
        if ($warmAcceptExit -ne 0) {
            Write-Host ''
            Write-Host 'STOPPED: the fast layer was measured but could not be stored (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host "  $warmRows row(s) came back from develop $($originDevelop.Substring(0,9)) in $([int]$warmSeconds) s and the store refused them."
            Write-Host "  What repairs it: the comparer's own refusal is above; it names what about the rows or the cache it could not use."
            exit 2
        }
        Write-Host ("  measured: {0} case(s) across the fast layer on develop {1}, {2} already red there, {3:N0} s" -f `
            $warmRows, $originDevelop.Substring(0,9), $warmReds, $warmSeconds)
        Write-Host "  from now on a merge that lands on $($originDevelop.Substring(0,9)) and selects plans inside test/plans/smoke or test/plans/feat compares against this and measures only its own tree (exit 0)"
        exit 0
    }

    $head = (& git rev-parse --verify "$Branch^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0) {
        Stop-With "branch $Branch does not resolve to a commit" 'Check the name, and that the branch has commits.'
    }
    $head = "$head".Trim()

    Write-Host "merging $Branch at $head"

    # Test 1: the exit code.
    $mergeOutput = & git merge --no-ff $Branch -m "$Message`n`nLane branch: $Branch at $head" 2>&1
    $mergeExit = $LASTEXITCODE
    $mergeOutput | ForEach-Object { "  $_" }

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
    if ($headersExit -ne 0) {
        Stop-With 'the header check failed' 'A file without its two-line header does not go to develop, because it stops a whole CI run before the build starts.' $mergeCommit
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
    $syntax = & pwsh -NoProfile -File (Join-Path $MainCheckout 'internal\tools\camel-remote.ps1') `
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
    if ($selectExit -ne 0 -or $planTargets.Count -eq 0) {
        # Not a red and not a green: the selection itself did not answer, so the plans this merge
        # touches are unknown and nothing about the change has been measured. Same shape as the
        # clang step's exit 2, and for the same reason: `cannot select` must not read as `no reds`.
        Write-Host ''
        if ($planTargets.Count -eq 0 -and $widened) {
            Write-Host 'STOPPED: this merge selects no plan (exit 2, no reading taken)' -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: `--changed` named no plan for this change, so it widened to the whole fast layer, and a whole-suite reading is not a reading about this merge. Nothing was pushed.'
            Write-Host '  What repairs it: `node scripts/test.js --changed --list` is the selection itself and starts nothing -- read it. Either the change names no plan (then nothing about it is measured case by case, and that is a fact worth knowing before merging) or `test/plan-selection.json` has no rule for those files.'
        } else {
            Write-Host "STOPPED: the plans this merge touches could not be selected (exit $selectExit, no reading taken)" -ForegroundColor Yellow
            Write-Host '  This is not a defect in your tree: the selection is what failed, so nothing was pushed and nothing was learned about this change.'
            $selectOut | Select-Object -Last 3 | ForEach-Object { "  $_" }
            Write-Host '  What repairs it: `node scripts/test.js --changed --list` names the selection and starts no test; the line above is its own refusal, and the usual cause is a base branch that does not resolve in this checkout.'
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
    $probe = @(& node (Join-Path $MainCheckout 'scripts\merge-readings.mjs') --cache $compareDir `
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
        $baselinePath = $BaselineWorktree
        $worktreeProblem = Sync-BaselineWorktree -Path $baselinePath -Sha $developTip `
            -CommonDir $commonDir -Repo $MainCheckout
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
            # The same change, read from the other end: the seeding run lists it with `git diff
            # --name-only` against `-ChangedBase`, taken in the baseline tree, so the base has to be the
            # MERGE COMMIT. The files that differ between the merged tree and the baseline tree are
            # exactly the files this merge changes; a diff against the lane head also names the lane's
            # own files, because those two trees differ by more than the merge -- measured as
            # `smoke`-plus-`docs.links` (31 cases) where the merge selected `docs.links` (21), and the
            # comparison then refused, correctly, as two runs of different things.
            $seedRun = @(& pwsh -NoProfile -File (Join-Path $MainCheckout 'internal\tools\camel-remote.ps1') `
                -Path $baselinePath -Changed -ChangedBase $comparedTree -ListKnown -NoGpuWatch 2>&1)
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
            Write-Host "  This is not a defect in your tree: the baseline run on develop $($developTip.Substring(0, 9)) recorded no row, so nothing was learned about this change and nothing was pushed."
            Write-Host "  What repairs it: the row block must arrive with rows inside it. Re-run internal/tools/camel-remote.ps1 -Path $baselinePath -Changed -ChangedBase $head -ListKnown -NoGpuWatch and read what it prints; a build that failed or a plan set that starts no case writes no block. An empty baseline is never stored -- it would call every case fixed."
            Write-Host ''
            Write-GitState -MergeCommit $mergeCommit
            exit 2
        }
        # The seconds and the case count are the reading this step owes: "it measured" is only worth
        # something if it says how much. They travel into the entry's `.where` beside the shas.
        $env:CAMEL_BASELINE_SECONDS = [string][int]$seedSeconds
        $accept = @(& node (Join-Path $MainCheckout 'scripts\merge-readings.mjs') --transcript $seedTranscript `
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
    $rowsRun = @(& pwsh -NoProfile -File (Join-Path $MainCheckout 'internal\tools\camel-remote.ps1') `
        -Path $MainCheckout -Changed -ChangedBase $developTip -ListKnown -NoGpuWatch 2>&1)
    $rowsExit = $LASTEXITCODE
    # Byte for byte, without a BOM and with LF: the comparer splits on `\n` and matches the markers
    # exactly, and a transcript written by `>` in PowerShell would be UTF-16.
    [System.IO.File]::WriteAllText($transcript,
        (($rowsRun | ForEach-Object { "$_" }) -join "`n") + "`n",
        (New-Object System.Text.UTF8Encoding($false)))
    $rowsCount = @($rowsRun | Where-Object { "$_".TrimStart().StartsWith('{') }).Count
    Write-Host ("  {0} row(s) from the run, camel-remote exit {1}, {2} plan target(s){3}" -f `
        $rowsCount, $rowsExit, $planTargets.Count, $(if ($widened) { ' (the whole fast layer)' } else { '' }))

    $compare = @(& node (Join-Path $MainCheckout 'scripts\merge-readings.mjs') --transcript $transcript `
        --cache $compareDir --develop $developTip --plans $planSetFile --tree $comparedTree `
        --changed-files $changedFilesFile --message-file $messageFile 2>&1)
    $compareExit = $LASTEXITCODE

    # The same three-way convention as the clang step. 1 is a reading -- the cases that moved are
    # named above, and the message says which class each is. 2 is the absence of one: the rows or
    # the baseline were not there, and that says nothing about the tree.
    #
    # Both refusals say what is true at this point and not what Stop-With says: the merge commit is
    # already made in this checkout (that is step 1 of this script), and it is the push that has not
    # happened. A reader who acts on "nothing was committed" would look for a merge that is there.
    if ($compareExit -eq 1) {
        $compare | ForEach-Object { "  $_" }
        Write-Host ''
        Write-Host 'STOPPED: the plans this merge touches measured differently on the merged tree (exit 1: the cases are named above)' -ForegroundColor Red
        Write-Host "  The baseline is the tree this merge lands on (develop $($developTip.Substring(0, 9))) and this merge is the only thing that moved. `newly red` was green there and is red now; `still red, failing differently` was red both times and reads differently now, which is the move a count cannot see."
        Write-Host '  If the change makes a case fail on purpose, the merge that makes it fail that way is the one that carries the expectation with it; this one is refused until the two agree.'
        Write-Host ''
        Write-GitState -MergeCommit $mergeCommit
        exit 1
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

    for ($i = 1; $i -le 3; $i++) {
        & git push origin develop 2>&1 | Select-Object -Last 1
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Seconds 4
    }
    if ($LASTEXITCODE -ne 0) { Stop-With 'git push failed after 3 attempts' 'The merge is committed locally and not on origin.' $mergeCommit }

    $remote = ((& git ls-remote origin refs/heads/develop) -split "`t") | Select-Object -First 1
    $local = "$(& git rev-parse HEAD)".Trim()
    if ($remote -ne $local) {
        Stop-With 'origin/develop is not the commit just made' "local $local, remote $remote" $mergeCommit
    }

    # The commit this merge just created is the tree those rows were measured on, so they are that
    # commit's baseline: storing them now is what makes the next merge on it a comparison instead of
    # a second measurement. After the push and never before -- a baseline written for a commit
    # nothing else can see would be a key nobody asks for, and a failed push would leave one.
    # The directory is made here because the comparer refuses a cache that is not there (that
    # refusal is what keeps a missing baseline from reading as "every case fixed"): the first merge
    # on a machine has no cache yet, and creating it is this script's job, not the reader's.
    New-Item -ItemType Directory -Force -Path $compareDir | Out-Null
    & node (Join-Path $MainCheckout 'scripts\merge-readings.mjs') --transcript $transcript `
        --cache $compareDir --develop $local --plans $planSetFile --tree $local --accept 2>&1 |
        Select-Object -Last 1 | ForEach-Object { "  $_" }
    if ($LASTEXITCODE -ne 0) {
        # Not a refusal: the merge is already on origin, and a merge that did land is not undone by
        # a baseline that could not be written. The next merge on this commit measures instead of
        # comparing, which is slower and not wrong.
        Write-Host "  warning: the baseline for $($local.Substring(0,9)) was not stored; the next merge on that commit will measure instead of comparing"
    }

    Write-Host ''
    # Which of the two the next merge will do, read rather than left to be discovered as a slow
    # merge: a baseline for this commit covers the plans this run selected, and the fast layer covers
    # every merge whose selection stays inside it.
    $tipProbe = Test-FastLayerBaseline -CacheDir $compareDir -Develop $local -Checkout $MainCheckout
    if ($tipProbe.Hit) {
        Write-Host "  the next merge on $($local.Substring(0,9)) compares against that fast-layer baseline and measures only its own tree"
    } else {
        Write-Host "  the next merge on $($local.Substring(0,9)) measures a baseline of its own: internal\tools\merge-lane.ps1 -WarmBaseline measures the fast layer once on this commit, when the test slot is free, and every merge after it compares instead"
    }
    Write-Host "merged $Branch at $head; develop is $($local.Substring(0,9))" -ForegroundColor Green
    # The closing claim read back rather than asserted: after the push both ends are one sha, and the
    # line says which. A push that reported success and a ref that did not move is a reading worth
    # seeing, so the two are compared here instead of being implied by the word "merged".
    $localNow = "$(& git rev-parse --verify 'refs/heads/develop^{commit}' 2>$null)".Trim()
    $originNow = "$(& git rev-parse --verify 'refs/remotes/origin/develop^{commit}' 2>$null)".Trim()
    if ($localNow -and $localNow -eq $originNow -and $localNow -eq $local) {
        Write-Host "  local develop is $localNow, which is origin/develop."
    } else {
        Write-Host "  local develop is $localNow and origin/develop is $originNow; this push reported success, so these should be the one commit $local and are not -- read them before trusting this merge." -ForegroundColor Yellow
    }
}
finally {
    if ($transcript) { Remove-Item -LiteralPath $transcript -Force -ErrorAction SilentlyContinue }
    if ($seedTranscript) { Remove-Item -LiteralPath $seedTranscript -Force -ErrorAction SilentlyContinue }
    if ($planSetFile) { Remove-Item -LiteralPath $planSetFile -Force -ErrorAction SilentlyContinue }
    if ($changedFilesFile) { Remove-Item -LiteralPath $changedFilesFile -Force -ErrorAction SilentlyContinue }
    if ($messageFile) { Remove-Item -LiteralPath $messageFile -Force -ErrorAction SilentlyContinue }
    # (the changed-file list is not a temp file: both sides read it from git)
    Pop-Location
}
