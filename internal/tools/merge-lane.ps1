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

.PARAMETER BaselineWorktree
    Where the baseline is measured. It is a worktree of its own, detached at the tree the merge lands
    on, and it is kept between merges so the host's build of it stays warm.

.EXAMPLE
    .\merge-lane.ps1 -Branch lane/range-ri -Message "merge range-ri: ..." -WhatIf
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Branch,
    [Parameter(Mandatory)][string] $Message,
    [string] $MainCheckout = 'D:\Projects\Camel',
    # Where the baseline is measured. It has to be a tree that is NOT the one being compared, and it
    # has to be a real checkout (the gate builds it), so it is a worktree of this repository detached
    # at the first parent. One fixed path and not one per merge: the host keeps the build of that
    # worktree between runs, and a fresh path per merge would mean a cold build per merge.
    [string] $BaselineWorktree = 'D:\Projects\Camel-wt\camel-baseline',
    [switch] $WhatIf,
    [switch] $NoPush
)

$ErrorActionPreference = 'Stop'

function Stop-With {
    param([string] $Why, [string] $How)
    Write-Host ''
    Write-Host "STOPPED: $Why" -ForegroundColor Red
    Write-Host "  $How"
    Write-Host ''
    Write-Host 'Nothing was committed and nothing was pushed.'
    exit 1
}

# The baseline's own checkout: a worktree of THIS repository, detached at the sha it must measure.
# Returns $null when the tree is ready, or a sentence saying why it is not -- the caller turns that
# into an exit 2, because a baseline that could not be taken is the gate being down and not a
# verdict about the tree being merged.
#
# It refuses anything it did not create: a path that holds a checkout of another repository, or a
# dirty checkout, is somebody else's tree, and `git checkout -f` over it is how a lane's work is lost.
function Sync-BaselineWorktree {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Sha,
        [Parameter(Mandatory)][string] $CommonDir,
        [Parameter(Mandatory)][string] $Repo
    )

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
            if ($onBranch) {
                # A labelled worktree belongs to a lane or to the merge position; this step only owns
                # the detached one it made. Refusing here is what keeps a mistyped -BaselineWorktree
                # from checking somebody's branch backwards.
                return "$target has the branch $onBranch checked out; the baseline is measured in a detached worktree, and checking that path out at $short would move a branch somebody is working on"
            }
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
    & git -C $Repo worktree add -q --detach $target $Sha 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { return "git worktree add --detach $target $short failed" }
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
            Write-Host 'Nothing was committed and nothing was pushed.'
            exit 1
        }

        if ($ageSeconds -lt 120) {
            Write-Host ''
            Write-Host "STOPPED: .git\index.lock is $([int]$ageSeconds) s old and no git process is running" -ForegroundColor Red
            Write-Host '  A lock younger than two minutes with no holder is most likely a git that has just'
            Write-Host '  exited; two minutes is longer than any single git command takes. Wait and retry.'
            Write-Host ''
            Write-Host 'Nothing was committed and nothing was pushed.'
            exit 1
        }

        Remove-Item $lock -Force
        Write-Host "  cleared an index.lock that was $([int]$ageSeconds) s old with no git process running"
    }

    $head = (& git rev-parse --verify "$Branch^{commit}" 2>$null)
    if ($LASTEXITCODE -ne 0) {
        Stop-With "branch $Branch does not resolve to a commit" 'Check the name, and that the branch has commits.'
    }
    $head = "$head".Trim()

    & git fetch origin --prune 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Stop-With 'git fetch failed' 'A merge against a stale origin is a merge against what you think is there.'
    }

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

    # A file without a header has already stopped one CI run before it built anything. The check is
    # cheap and local, so it runs here, before the push.
    $headers = & node (Join-Path $MainCheckout 'scripts\headers.mjs') --check 2>&1
    $headersExit = $LASTEXITCODE
    $headers | Select-Object -Last 2 | ForEach-Object { "  $_" }
    if ($headersExit -ne 0) {
        Stop-With 'the header check failed' 'A file without its two-line header does not go to develop, because it stops a whole CI run before the build starts.'
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
        Write-Host 'Nothing was committed and nothing was pushed.'
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
    $selectOut = @(& node (Join-Path $MainCheckout 'scripts\test.js') --changed --list 2>&1)
    $selectExit = $LASTEXITCODE
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
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
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
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
        exit 2
    }
    if ($developTip -eq $comparedTree) {
        Write-Host ''
        Write-Host 'STOPPED: HEAD^1 is HEAD, so there is no second tree to read (exit 2, no reading taken)' -ForegroundColor Yellow
        Write-Host '  This is not a defect in your tree: the baseline would be measured on the very tree it is compared with, which is 0 fixed, 0 new and 0 changed by construction -- a green that measured one tree twice.'
        Write-Host '  What repairs it: a merge commit whose first parent is develop. Nothing was pushed.'
        Write-Host ''
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
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
            Write-Host ("  no baseline for develop {0}: measuring it there, on a detached checkout at {1} (same plan set)" -f `
                $developTip.Substring(0, 9), $baselinePath)
            $seedStarted = Get-Date
            # The same plan set the merged run will select: `-Changed` lists the change against the
            # same base (`$head`, the lane) on a tree that does not have it yet, so the two runs differ
            # in the merge and in nothing else -- which is what makes the comparison a comparison.
            $seedRun = @(& pwsh -NoProfile -File (Join-Path $MainCheckout 'internal\tools\camel-remote.ps1') `
                -Path $baselinePath -Changed -ChangedBase $head -ListKnown -NoGpuWatch 2>&1)
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
            Write-Host "  What repairs it: the baseline is measured in a worktree detached at the first parent ($($developTip.Substring(0, 9))). Name one with -BaselineWorktree, or let this script create it; a path that holds somebody else's checkout is refused rather than checked out over."
            Write-Host ''
            Write-Host 'The merge commit is in this checkout and nothing was pushed.'
            exit 2
        }
        if ($seedRows -eq 0) {
            Write-Host ''
            Write-Host "STOPPED: the baseline could not be measured (exit 2, no reading taken; the run exited $seedExit)" -ForegroundColor Yellow
            Write-Host "  This is not a defect in your tree: the baseline run on develop $($developTip.Substring(0, 9)) recorded no row, so nothing was learned about this change and nothing was pushed."
            Write-Host "  What repairs it: the row block must arrive with rows inside it. Re-run internal/tools/camel-remote.ps1 -Path $baselinePath -Changed -ChangedBase $head -ListKnown -NoGpuWatch and read what it prints; a build that failed or a plan set that starts no case writes no block. An empty baseline is never stored -- it would call every case fixed."
            Write-Host ''
            Write-Host 'The merge commit is in this checkout and nothing was pushed.'
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
            Write-Host 'The merge commit is in this checkout and nothing was pushed.'
            exit 2
        }
        $accept | ForEach-Object { "  $_" }
        $seededFrom = $measuredTree
        Write-Host ("  baseline seeded from tree {0} (the first parent, checked out detached at {1}; NOT the tree above): {2} case(s), {3} already red on develop, {4:N0} s" -f `
            $measuredTree.Substring(0, 9), $baselinePath, $seedRows, $seedReds, $seedSeconds)
    } elseif ($probeExit -ne 0) {
        $probe | ForEach-Object { "  $_" }
        Write-Host ''
        Write-Host "STOPPED: whether a baseline exists could not be answered (exit $probeExit, no reading taken)" -ForegroundColor Yellow
        Write-Host '  This is not a defect in your tree: the cache was not read, so nothing was compared and nothing was pushed.'
        Write-Host '  What repairs it: the comparer''s own line is above; `--probe` reads one key and needs no rows, so a refusal here is about the cache directory or the plan set file, not about the tree.'
        Write-Host ''
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
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
        -Path $MainCheckout -Changed -ListKnown -NoGpuWatch 2>&1)
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
        --cache $compareDir --develop $developTip --plans $planSetFile --tree $comparedTree 2>&1)
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
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
        exit 1
    }
    if ($compareExit -ne 0) {
        $compare | ForEach-Object { "  $_" }
        Write-Host ''
        Write-Host "STOPPED: the row readings were not taken (exit $compareExit, no reading taken)" -ForegroundColor Yellow
        Write-Host "  This is not a defect in your tree: there were no rows to compare, so nothing was learned about this change. The run that should have recorded them exited $rowsExit; the comparer's own refusal is above."
        Write-Host "  What repairs it: the block must arrive with rows inside it. A run that selected no plan writes no block, and a plan set whose cases all start nothing writes an empty one. This script measures the baseline itself (tree $($developTip.Substring(0, 9)) against tree $($comparedTree.Substring(0, 9))) and there is no command to run by hand for it; if the refusal above is the baseline being this very tree, the two shas are the reading to look at."
        Write-Host ''
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
        exit 2
    }
    # The two shas on the one line that says "measured": a reader who trusts this line has to be able
    # to see that it was two trees, not one tree twice.
    Write-Host ("  measured: baseline tree {0} ({1}) against this tree {2}, no new red and no changed failure ({3:N0} s)" -f `
        $developTip.Substring(0, 9), $(if ($seededFrom) { 'seeded now' } else { 'from the cache' }), `
        $comparedTree.Substring(0, 9), ((Get-Date) - $rowsStarted).TotalSeconds)

    if ($NoPush) {
        Write-Host ''
        Write-Host "would push: every reading was taken and neither tree refused it. develop is still $($developTip.Substring(0,9)) and this merge commit ($($comparedTree.Substring(0,9))) is only in this checkout." -ForegroundColor Yellow
        Write-Host "  The baseline this merge would store for its own tree is not written: a key for a commit origin cannot see is a key nobody asks for. Nothing was pushed."
        Write-Host ''
        Write-Host "tried $Branch at $head; nothing was pushed (-NoPush)"
        exit 0
    }

    for ($i = 1; $i -le 3; $i++) {
        & git push origin develop 2>&1 | Select-Object -Last 1
        if ($LASTEXITCODE -eq 0) { break }
        Start-Sleep -Seconds 4
    }
    if ($LASTEXITCODE -ne 0) { Stop-With 'git push failed after 3 attempts' 'The merge is committed locally and not on origin.' }

    $remote = ((& git ls-remote origin refs/heads/develop) -split "`t") | Select-Object -First 1
    $local = "$(& git rev-parse HEAD)".Trim()
    if ($remote -ne $local) {
        Stop-With 'origin/develop is not the commit just made' "local $local, remote $remote"
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
    Write-Host "merged $Branch at $head; develop is $($local.Substring(0,9))" -ForegroundColor Green
}
finally {
    if ($transcript) { Remove-Item -LiteralPath $transcript -Force -ErrorAction SilentlyContinue }
    if ($seedTranscript) { Remove-Item -LiteralPath $seedTranscript -Force -ErrorAction SilentlyContinue }
    if ($planSetFile) { Remove-Item -LiteralPath $planSetFile -Force -ErrorAction SilentlyContinue }
    Pop-Location
}
