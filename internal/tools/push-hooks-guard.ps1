# Whether a push from a tree would be checked at all.
#
# `core.hooksPath` is `.husky/_` in this repository, and a RELATIVE value is resolved against the
# worktree that is pushing -- so every worktree needs its own `.husky/_`, and only the tree husky
# last ran in has one. A tree made with `git worktree add` never does. Git treats a hooks directory
# that does not exist as "there are no hooks": the push succeeds, silently, and every rule installed
# in that directory is skipped -- the develop lock, the arrival shape, the merge record, the
# process-trace checker. That is not a rule that failed; it is a rule that never ran, and nothing
# said so.
#
# So the question here is not "does a file exist" but "does the guard speak". The hook is run the way
# git runs it -- same interpreter (`sh` beside `git --exec-path`), same working directory (the
# worktree), same PATH (git's own `/usr/bin` and `/mingw64/bin` first, because the hook calls `cat`,
# `tr` and `sed`) -- with an empty stdin and one ref that no rule is about, so the develop lock and
# the arrival shape both return early and what is left is the checker that reads this tree. Its own
# reading is the proof: `scanned N files, M comment lines, no trace`, or
# `process traces in comments: ...`.
#
# An empty reading with exit 0 is the failure this check exists for, and it is not a pass: it is
# exactly what a hooks directory whose `.husky/pre-push` is absent, or does not delegate, produces.
# A hook that is present but reads nothing is not a guard.
#
# What the reading SAYS is not this check's verdict. A tree whose comments carry traces answers with
# its list and exit 1; that answer is reported as a warning and is not turned into a second refusal
# here, because the push is about to run the same checker for real, and a guard that answers two
# questions answers the second one twice.
#
# `core.hooksPath` unset is out of scope, and deliberately: git then runs `$GIT_DIR/hooks`, which a
# repository may leave empty by design -- a bare repository, a fixture it built for a test -- so an
# absent `pre-push` there is the normal state and not a broken promise. A configured hooks path is
# what promises a guard.

# `sh` as git would run it. On Windows a hook runs under the `sh` that Git for Windows installs
# beside git, and that one is not on PATH; asking git for its exec path and stepping out of
# `mingw64/libexec/git-core` finds the same interpreter without a guess about a version.
function Resolve-GitSh {
    $execPath = "$(& git --exec-path 2>$null)".Trim()
    if (-not $execPath) { return '' }
    $gitRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $execPath))
    foreach ($candidate in @(
            (Join-Path $gitRoot 'usr/bin/sh.exe'),
            (Join-Path $gitRoot 'bin/sh.exe'),
            (Join-Path $gitRoot 'usr/bin/sh'))) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return ''
}

# The directory git would take hooks from, as an absolute path. `--git-path hooks` already honours
# `core.hooksPath` and resolves a relative value against the worktree; the join below is for a git
# too old to answer with an absolute path, so a caller on one machine and a caller on another get
# the same string.
function Resolve-GitHooksDir {
    param([Parameter(Mandatory)][string] $Tree)

    $dir = "$(& git -C $Tree rev-parse --git-path hooks 2>$null)".Trim()
    if (-not $dir) { return '' }
    if (-not [System.IO.Path]::IsPathRooted($dir)) { $dir = Join-Path $Tree $dir }
    return $dir
}

# Run the hook with an empty stdin and one ref no rule is about. Returns the exit code and the two
# streams, or $null when the child could not be started or did not answer in time.
function Invoke-PushHookProbe {
    param(
        [Parameter(Mandatory)][string] $Tree,
        [Parameter(Mandatory)][string] $HookFile,
        [string] $Sha = '0000000000000000000000000000000000000000',
        [int] $TimeoutSeconds = 300
    )

    $sh = Resolve-GitSh
    if (-not $sh) { return $null }
    $execPath = "$(& git --exec-path 2>$null)".Trim()
    $gitRoot = if ($execPath) { Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $execPath)) } else { '' }

    $savedPath = $env:PATH
    if ($gitRoot) { $env:PATH = "$gitRoot\usr\bin;$gitRoot\mingw64\bin;$env:PATH" }
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $sh
        $psi.WorkingDirectory = $Tree
        $psi.UseShellExecute = $false
        $psi.RedirectStandardInput = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        # The hook names its directory from `$0` and reads the tree from `git rev-parse
        # --show-toplevel`, so it gets an absolute path and a working directory inside the tree:
        # a relative `$0` would be resolved against whatever directory the caller happened to be in.
        foreach ($argument in @(
                $HookFile, 'origin', 'https://example.invalid/x',
                'refs/heads/x', $Sha, 'refs/heads/x', $Sha)) {
            $psi.ArgumentList.Add($argument)
        }
        $process = [System.Diagnostics.Process]::Start($psi)
        # Closed rather than left open: git writes the refs it is about to send on stdin, and this
        # probe sends none. An open stdin would block the hook's `cat` until the timeout.
        $process.StandardInput.Close()
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            try { $process.Kill() } catch { }
            return [pscustomobject]@{ Exit = $null; TimedOut = $true; Stdout = $stdout; Stderr = $stderr; Sh = $sh }
        }
        return [pscustomobject]@{ Exit = $process.ExitCode; TimedOut = $false; Stdout = $stdout; Stderr = $stderr; Sh = $sh }
    } catch {
        return $null
    } finally {
        $env:PATH = $savedPath
    }
}

# The sentences a hooks directory that is in force answers with. A probe that comes back with one of
# these spoke; a probe that comes back with nothing, or with something else, did not.
$script:PushHookGuardReadingPattern = 'process traces in comments:|scanned \d+ files, \d+ comment lines'

function Test-PushHookGuard {
    param(
        [Parameter(Mandatory)][string] $Tree,
        # Only for the refusal: the branch the caller was about to write.
        [Parameter(Mandatory)][string] $Target
    )

    $result = {
        param($Ok, $Kind, $HooksPath, $HookFile, $Exit, $Output, $Message)
        [pscustomobject]@{
            Ok = $Ok; Kind = $Kind; Tree = "$Tree".Trim(); Target = $Target
            HooksPath = $HooksPath; HookFile = $HookFile; Exit = $Exit
            Output = @($Output); Message = @($Message)
        }
    }

    $treeFull = "$(& git -C $Tree rev-parse --show-toplevel 2>$null)".Trim()
    if (-not $treeFull) {
        return (& $result $false 'no worktree' '' '' $null @() @(
                "refusing to push to $Target`: '$Tree' is not inside a worktree, so there is no tree whose hooks could be read and no answer to whether this push is guarded."))
    }
    # Git answers with forward slashes; the paths in the message are the ones a reader will type.
    if ([System.IO.Path]::DirectorySeparatorChar -eq '\') { $treeFull = $treeFull -replace '/', '\' }

    $hooksPath = "$(& git -C $treeFull config --get core.hooksPath 2>$null)".Trim()
    if (-not $hooksPath) {
        return (& $result $true 'no hooks path' '' '' $null @() @(
                "no 'core.hooksPath' is configured in $treeFull, so git runs the hooks directory of the repository itself: a repository may leave that empty on purpose, so there is no promise here for this check to hold anyone to."))
    }

    $hookDir = Resolve-GitHooksDir -Tree $treeFull
    if (-not $hookDir) {
        return (& $result $false 'no hooks directory' $hooksPath '' $null @() @(
                "refusing to push to $Target`: 'core.hooksPath' is '$hooksPath' but git cannot say which directory that is, so whether a guard would run cannot be answered. Nothing was pushed."))
    }
    $hookFile = Join-Path $hookDir 'pre-push'

    if (-not (Test-Path -LiteralPath $hookFile -PathType Leaf)) {
        return (& $result $false 'no hook file' $hooksPath $hookFile $null @() @(
                "refusing to push to $Target from ${treeFull}: the hooks directory git would use has no 'pre-push', so this push would run no rule at all.",
                "   'core.hooksPath' is '$hooksPath', which resolves to $hookDir.",
                "   There is no $hookFile.",
                "   A hooks directory that does not exist is not an error to git: the push would succeed, with the develop lock, the arrival shape, the merge record and the process-trace check all skipped and nothing said.",
                "   What installs it: cd '$treeFull' ; npx husky        (the repository's `prepare` script runs the same command)",
                "   Nothing was pushed."))
    }

    if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
        return (& $result $false 'no node' $hooksPath $hookFile $null @() @(
                "refusing to push to $Target from ${treeFull}: 'node' is not on PATH, so the checker this hook delegates to cannot be run and the hook cannot be shown to be a guard rather than an empty file.",
                "   Nothing was pushed."))
    }

    $sha = "$(& git -C $treeFull rev-parse --verify --quiet 'HEAD^{commit}' 2>$null)".Trim()
    if (-not $sha) { $sha = '0000000000000000000000000000000000000000' }

    $probe = Invoke-PushHookProbe -Tree $treeFull -HookFile $hookFile -Sha $sha
    if (-not $probe) {
        return (& $result $false 'not run' $hooksPath $hookFile $null @() @(
                "refusing to push to $Target from ${treeFull}: '$hookFile' could not be run, so whether a rule would run on this push cannot be answered. A guard that was not observed is not a guard that passed.",
                "   Nothing was pushed."))
    }
    $combined = @()
    if ($probe.Stdout) { $combined += @("$($probe.Stdout)" -split "`r?`n") }
    if ($probe.Stderr) { $combined += @("$($probe.Stderr)" -split "`r?`n") }
    $combined = @($combined | Where-Object { "$_".Trim() })

    if ($probe.TimedOut) {
        return (& $result $false 'timeout' $hooksPath $hookFile $null $combined @(
                "refusing to push to $Target from ${treeFull}: '$hookFile' did not answer with an empty stdin, so it was stopped and this push was not started.",
                "   Nothing was pushed."))
    }

    $spoke = @($combined | Where-Object { $_ -match $script:PushHookGuardReadingPattern })
    if ($spoke.Count -eq 0) {
        # Two shapes, and a reader acts differently on each: nothing at all means the hooks directory
        # does not delegate anywhere, and something unrecognised means the chain behind it ran and
        # failed -- a hook and the checker it calls taken from different commits, or `node` missing.
        if ($combined.Count -eq 0) {
            return (& $result $false 'no reading' $hooksPath $hookFile $probe.Exit $combined @(
                    "refusing to push to $Target from ${treeFull}: the hook exited $($probe.Exit) and printed nothing.",
                    "   An empty reading is this check's failure and not its pass: it is what a '$hookFile' that does not delegate produces, and every rule behind it is then skipped in silence.",
                    "   What installs it: cd '$treeFull' ; npx husky        (the repository's `prepare` script runs the same command)",
                    "   Nothing was pushed."))
        }
        $message = @(
            "refusing to push to $Target from ${treeFull}: the hook ran (exit $($probe.Exit)) and printed nothing this checker recognises, so the chain behind the hook is broken rather than absent.",
            "   What it said:")
        $message += @($combined | Select-Object -Last 6 | ForEach-Object { "     $_" })
        $message += @(
            "   The reading a working guard answers with is one of `scanned N files, M comment lines, no trace` or `process traces in comments: ...`.",
            "   Two causes look like this and the lines above tell them apart: the tree's `.husky/pre-push` and the checker it calls are from different commits, or an interpreter one of them needs is missing. Bring the tree up to date and run 'npx husky' in it.",
            "   Nothing was pushed.")
        return (& $result $false 'no reading' $hooksPath $hookFile $probe.Exit $combined $message)
    }

    # The guard spoke. Whether the tree passes it is the hook's to say on the real push, and this
    # reports it rather than refusing on it.
    $verdict = @($spoke | ForEach-Object { "   $_" })
    if ($probe.Exit -ne 0) {
        $verdict += "   warning: the hook exited $($probe.Exit), so the real push will be refused by the same reading."
    }
    return (& $result $true 'guarded' $hooksPath $hookFile $probe.Exit $combined $verdict)
}