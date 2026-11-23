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
        Write-Host '  The commonest cause is that the gate checkout has no compile database (build/compile_commands.json), which only a build writes: run one on it -- internal/tools/camel-remote.ps1 -Path <that worktree>, without -NoBuild -- and merge again. The checker names the actual cause on the line above it printed; the others are a clang 19 image the host does not hold and a toolchain its compile lines cannot reach.'
        Write-Host ''
        Write-Host 'Nothing was committed and nothing was pushed.'
        exit 2
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

    Write-Host ''
    Write-Host "merged $Branch at $head; develop is $($local.Substring(0,9))" -ForegroundColor Green
}
finally {
    Pop-Location
}