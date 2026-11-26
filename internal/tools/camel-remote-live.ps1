# Which host checkouts a run may remove.
#
# `camel-remote.ps1` keeps one checkout per worktree name on the build host and, at the start of a
# run, removes the ones whose worktree is gone. That sweep is the only `rm -rf` in the tool that can
# destroy another lane's work, so the list it acts on is read here, once, and its readings are kept
# separate instead of being folded into one list:
#
#   (a) `git worktree list` answered on the workstation. A one-name list is a reading; a *failed*
#       reading is not an empty one, and it is not this run's own name either.
#   (b) The directory, not only git's metadata: a name the filesystem carries beside the worktrees
#       is kept as live even when git no longer names it (a deleted worktree stays listed until a
#       prune, and a hand-made directory is still somebody's checkout).
#   (c) On the host: that no process is inside the checkout. Read there, by the sweep itself.
#
# What went wrong three times, in the same minute as a gate: the sweep removed a
# checkout that was being measured, and the run died at 25/500, 200/500 and 300/500 with
# `FileNotFoundError: .../out/latest/bin/camel` and a `uv_cwd` that could not read a working
# directory which no longer existed. The reading that decided it came from the old inline code,
# which was a `try { git ... 2>$null | ... } catch { keep every checkout }`: the error went to
# `$null`, PowerShell does not throw on a native command's non-zero exit, so the catch that says
# "keep every checkout" never ran -- `$liveNames` came back empty, the next line appended this
# run's own name, and the sweep was told that exactly one checkout exists. The catch's comment was
# right and its unreachability was the defect.
#
# So `Trusted` is false unless every local reading answered, and the sweep removes nothing at all
# when it is false; it prints the reason instead. `camel-remote-cleanup.test.ps1` injects the fake
# `git worktree list` readings this comment describes.

function Get-CamelLiveWorktrees {
    [CmdletBinding()]
    param(
        # The worktree or checkout whose run is asking.
        [Parameter(Mandatory)][string]$Path,
        # Stands in for `git -C <Path> worktree list --porcelain` in the tests. It must set
        # $global:LASTEXITCODE the way a native command does, because that exit code is half of
        # the reading -- and the half the old code could not see.
        [scriptblock]$GitProbe = $null,
        # Siblings per root this reading will walk before it gives up on (b) for that root; see the
        # note at the scan itself. A parameter so a caller can state its own ceiling, and so a test
        # can drive the branch without a 23 000-entry directory.
        [int]$ScanCeiling = 4096
    )

    # Git prints a worktree path with forward slashes on Windows and `Split-Path` accepts both, but
    # the paths have to be compared in one spelling: a tree that "is not in the list" only because
    # of `\` versus `/` would be swept while it exists.
    $normalize = { param($p) (("$p") -replace '\\', '/').TrimEnd('/') }
    $selfFull = if (Test-Path -LiteralPath $Path) { (Resolve-Path -LiteralPath $Path).Path } else { $Path }
    $selfPath = & $normalize $selfFull
    $selfName = Split-Path -Leaf $selfPath

    $result = [pscustomobject]@{
        Names   = @($selfName)
        Paths   = @()
        Trusted = $false
        Why     = ''
        # Roots this run did not scan for (b), with their entry counts. Printed by the caller, never
        # folded into `Names`: "I did not look" and "nothing is there" must not read the same.
        Skipped = @()
    }

    # (a) Did the reading happen at all?
    $lines = @()
    if ($null -ne $GitProbe) {
        $lines = @(& $GitProbe)
    } else {
        $lines = @(git -C $Path worktree list --porcelain 2>$null)
    }
    $exit = $LASTEXITCODE
    if ($exit -ne 0) {
        $result.Why = "git worktree list exited $exit"
        return $result
    }

    $listed = @($lines | ForEach-Object { "$_" } |
        Where-Object { $_ -match '^\s*worktree\s+\S' } |
        ForEach-Object { ("$_" -replace '^\s*worktree\s+', '').Trim() } |
        Where-Object { $_ })
    if ($listed.Count -eq 0) {
        $result.Why = 'git worktree list named no worktree'
        return $result
    }

    # The list is about this repository only if this tree is in it. A partial read (a truncated
    # run, another repository answering) passes the count and fails here, which is what makes
    # "not in the list" safe to read as "gone".
    if (@($listed | Where-Object { (& $normalize $_) -ieq $selfPath }).Count -eq 0) {
        $result.Why = "git worktree list did not name $selfPath, so its answer is not about this tree"
        return $result
    }

    $names = @($listed | ForEach-Object { Split-Path -Leaf (& $normalize $_) })

    # (b) The directories beside the worktrees. `git worktree remove` deletes the directory too, so
    # a properly removed worktree is absent from both readings and this changes nothing; a worktree
    # whose directory was deleted by hand stays in git's list until a prune and is protected here,
    # which is the safe direction for a reading whose other use is `rm -rf`.
    #
    # Every parent is read, including the main checkout's: worktrees are usually created beside each
    # other, and a rule that skipped "the directory above the main checkout" quietly did nothing in
    # exactly that layout -- a guard that is off is worse than one that keeps too much.
    $roots = @($listed | ForEach-Object { Split-Path -Parent (& $normalize $_) } |
        Where-Object { $_ } | Sort-Object -Unique)
    foreach ($root in $roots) {
        try {
            # One stat per sibling, so the cost of this reading is the size of the directory beside
            # the worktrees, not the number of worktrees. A root that is a shared scratch area
            # rather than a row of checkouts (this machine has a worktree sitting directly in a
            # 23 000-entry temp directory) would cost seconds per run to answer a question about
            # hand-made copies. Above the ceiling below it is not scanned, and the run says so; every
            # *registered* worktree is covered by (a) regardless, and the sweep's other readings --
            # its lock, the host's processes, recent writes, the creation record -- are untouched.
            $entries = @([System.IO.Directory]::GetDirectories($root))
            if ($entries.Count -gt $ScanCeiling) {
                $result.Skipped += [pscustomobject]@{ Root = $root; Entries = $entries.Count; Ceiling = $ScanCeiling }
                continue
            }
            $dirs = $entries
        } catch {
            # No second reading means no proof that a name is absent, so nothing may be removed.
            $result.Why = "cannot read the directories under $root ($($_.Exception.Message))"
            return $result
        }
        foreach ($dir in $dirs) {
            # Only a *linked* worktree counts, and that is the one whose `.git` is a file pointing at
            # `<common>/worktrees/<name>`; a main checkout has `.git` as a directory. The worktree
            # root's parent holds unrelated checkouts too -- this machine's `D:\Projects` carries a
            # dozen of them -- and reading those as live worktrees would exempt their names from the
            # sweep forever. A scratch directory beside the worktrees (`_logs`, a lane's output
            # folder) is not a checkout at all and is not kept either.
            #
            # `File.Exists` rather than `Test-Path -PathType Leaf`: the same reading (a regular file,
            # not a container), for a twentieth of the cost. One worktree root here is a directory of
            # 23 000 entries, and this loop tests every entry -- the cmdlet plus `Join-Path` came to
            # 4.5 s of a 10.6 s transfer, measured per entry; the two .NET calls come to 0.6 s.
            if ([System.IO.File]::Exists($dir + '\' + '.git')) {
                $leaf = Split-Path -Leaf $dir
                if ($leaf -and ($names -notcontains $leaf)) { $names += $leaf }
            }
        }
    }

    if ($names -notcontains $selfName) { $names += $selfName }
    $result.Names = @($names | Sort-Object -Unique)
    $result.Paths = @($listed)
    $result.Trusted = $true
    return $result
}