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

.PARAMETER Branch
    The lane branch to merge, e.g. lane/range-ri.

.PARAMETER Message
    The merge commit message. The branch head is appended as a trailer.

.PARAMETER WhatIf
    Pass both checks, then stop before committing.

.EXAMPLE
    .\merge-lane.ps1 -Branch lane/range-ri -Message "merge range-ri: ..." -WhatIf
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Branch,
    [Parameter(Mandatory)][string] $Message,
    [string] $MainCheckout = 'D:\Projects\Camel',
    [switch] $WhatIf
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

Push-Location $MainCheckout
try {
    # A reader that refreshes the index takes the lock and a merge then refuses. Clearing it is only
    # safe when nothing is writing: deleting the lock under a git that is mid-write corrupts the index,
    # which is worse than a refused merge. So the lock is cleared only when it is older than two
    # minutes AND no git process is running at all. Otherwise this stops and prints the command lines,
    # because the answer to "who holds it" is something a person has to see, not something to guess at.
    $lock = Join-Path $MainCheckout '.git\index.lock'
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
    if (-not (Test-Path (Join-Path $MainCheckout '.git\MERGE_HEAD'))) {
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

    $developTip = "$(& git rev-parse 'HEAD^1' 2>$null)".Trim()
    $compareDir = Join-Path $MainCheckout '.git\camel-readings'
    $compare = @(& node (Join-Path $MainCheckout 'scripts\merge-readings.mjs') --transcript $transcript `
        --cache $compareDir --develop $developTip --plans $planSetFile 2>&1)
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
        Write-Host "  What repairs it: the block must arrive with rows inside it. A run that selected no plan writes no block, and a plan set whose cases all start nothing writes an empty one -- re-run internal/tools/camel-remote.ps1 -Path $MainCheckout -Changed -ListKnown -NoGpuWatch and read the block it prints. If the block is there and the baseline is what is missing, measure that plan set on a checkout of develop $($developTip.Substring(0, 9)) and store it: node scripts/merge-readings.mjs --transcript <that run's output> --cache $compareDir --develop $developTip --plans '$planSet' --accept"
        Write-Host ''
        Write-Host 'The merge commit is in this checkout and nothing was pushed.'
        exit 2
    }
    Write-Host ("  measured: develop {0} against this tree, no new red and no changed failure ({1:N0} s)" -f `
        $developTip.Substring(0, 9), ((Get-Date) - $rowsStarted).TotalSeconds)

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
        --cache $compareDir --develop $local --plans $planSetFile --accept 2>&1 | Select-Object -Last 1 |
        ForEach-Object { "  $_" }
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
    if ($planSetFile) { Remove-Item -LiteralPath $planSetFile -Force -ErrorAction SilentlyContinue }
    Pop-Location
}
