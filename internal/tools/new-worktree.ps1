# The one place a worktree is made, so that "made" and "guarded" are the same step.
#
# `core.hooksPath` is `.husky/_`, and a relative value is resolved against the worktree that is
# pushing -- so a worktree has no hooks until husky runs inside it. `git worktree add` does not run
# `prepare`, nothing else does either, and git treats the resulting missing hooks directory as "there
# are no hooks": every push from that tree lands with the develop lock, the arrival shape, the merge
# record and the process-trace check silently skipped. A tree that is made by copying a command out
# of a page has no step in which that could be noticed.
#
# So the command is here instead, and it does the whole of "made": fetch (a cached `origin/develop`
# is what the page's `worktree add` used to take, which is a tree behind the tip before it exists),
# add the worktree, copy the machine-local settings file the hooks and the host tooling read, run
# husky, and then prove the guard speaks before reporting success. The last two are one step because
# "husky exited 0" is not the same claim as "the hook answers": a hooks directory can be present and
# still delegate to nothing.
#
#   pwsh -NoProfile -File internal/tools/new-worktree.ps1 `
#       -Path D:\Projects\Camel-wt\<name> -Branch <branch> [-Base origin/develop]
#
# `-Detach <ref>` makes the tree at that commit with no branch, for a caller that measures or publishes
# a commit rather than works on a line (the promotion job's trees). Everything else is the same step:
# the settings file, husky, the guard check. `-Branch` and `-Detach` exclude each other and one of them
# is required.
#
#   pwsh -NoProfile -File internal/tools/new-worktree.ps1 `
#       -Path <tree> -Detach <sha> [-FromCheckout <repo>] [-NoFetch]
#
# `-VerifyOnly` asks the same question about a tree that already exists, and creates nothing: it is
# what to run when a push from an unfamiliar tree is refused and the reason is not obvious.
#
#   pwsh -NoProfile -File internal/tools/new-worktree.ps1 -VerifyOnly -Path <tree>
#
# Exit codes: 0 the tree exists and its guard spoke; 2 refused before anything was made (the path or
# the branch is taken, the base does not resolve); 1 the tree exists and is NOT guarded -- the
# message says what is missing, and the tree is left in place because deleting it would delete
# whatever the caller has already put in it.

[CmdletBinding()]
param(
    # The tree to make, or -- with -VerifyOnly -- the tree to read.
    [string] $Path,
    # The branch the new tree checks out. Making a tree takes this or -Detach; ignored with -VerifyOnly.
    [string] $Branch,
    # A commit-ish the new tree is checked out at with no branch. Excludes -Branch.
    [string] $Detach,
    # What the branch starts from. `origin/develop` is the release line every tree is cut from.
    [string] $Base = 'origin/develop',
    # The repository to add the worktree to. Defaults to the one this script lives in, which is the
    # right answer for every caller: the script is run from a checkout of the repository it is in.
    [string] $FromCheckout,
    # Read an existing tree instead of making one.
    [switch] $VerifyOnly,
    # Make the tree and skip husky. For a fixture that is about to be deleted, and for nothing else:
    # the point of this script is that the two steps are one.
    [switch] $NoHusky,
    # Skip the fetch. For a caller that has just fetched.
    [switch] $NoFetch
)

$ErrorActionPreference = 'Stop'

function Write-Stop {
    param([string] $Reason, [string[]] $Detail, [int] $Code = 2)
    Write-Host ''
    Write-Host "STOPPED: $Reason (exit $Code)" -ForegroundColor Red
    foreach ($line in @($Detail)) { if ("$line".Trim()) { Write-Host "  $line" } }
    exit $Code
}

$here = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$repo = if ($FromCheckout) { $FromCheckout } else { $here }

$repoRoot = "$(& git -C $repo rev-parse --show-toplevel 2>$null)".Trim()
if (-not $repoRoot) {
    Write-Stop "the repository could not be read" @(
        "'$repo' is not inside a worktree, so there is nothing to add a worktree to.")
}

. (Join-Path $PSScriptRoot 'push-hooks-guard.ps1')

# --- the read-only mode: does this tree's guard speak? ------------------------
if ($VerifyOnly) {
    $treeToRead = if ($Path) { $Path } else { $repoRoot }
    $target = if ($Branch) { $Branch } else { 'the branch it pushes' }
    $guard = Test-PushHookGuard -Tree $treeToRead -Target $target
    Write-Host ''
    Write-Host ("push guard at {0}: {1}" -f $guard.Tree, $guard.Kind)
    foreach ($line in $guard.Message) { Write-Host $line }
    if ($guard.Ok) { exit 0 }
    exit 2
}

if (-not $Path) {
    Write-Stop "no -Path" @(
        'Give the tree to make, e.g. -Path D:\Projects\Camel-wt\<name>. This script names no default',
        'because a default path is a directory nobody chose, and the tree has to go somewhere a',
        'person can find it again.')
}
if ($Branch -and $Detach) {
    Write-Stop "-Branch and -Detach together" @(
        'A tree is either on a new branch or detached at a commit. Nothing was made.')
}
if (-not $Branch -and -not $Detach) {
    Write-Stop "no -Branch and no -Detach" @(
        'Give the branch the new tree checks out, or -Detach <ref> for a tree at a commit with no branch.',
        'This script names no default: a branch name is a policy, and one invented here would be a',
        'second place that policy is written.')
}
$detached = [bool] $Detach
$checkoutName = if ($detached) { "detached $Detach" } else { $Branch }

$pathFull = [System.IO.Path]::GetFullPath($Path)
if (Test-Path -LiteralPath $pathFull) {
    Write-Stop "$pathFull already exists" @(
        'A worktree is added to a path that does not exist yet, or to an empty one. Nothing was made.')
}
if (-not $detached -and "$(& git -C $repoRoot rev-parse --verify --quiet "refs/heads/$Branch" 2>$null)".Trim()) {
    Write-Stop "the branch '$Branch' already exists" @(
        "git worktree add -b refuses a branch that exists, and reusing one would move whatever is on it.",
        'Pick the branch this tree is for, or check the tree that already holds it out with',
        '`git worktree list`. Nothing was made.')
}

# --- the base, read after a fetch --------------------------------------------
# The page's command took whatever `origin/develop` the local cache held, which is a tree behind the
# tip by however long since the last fetch, and the first measurement inside it is then a reading
# about a base nobody has.
if (-not $NoFetch) {
    $fetch = @(& git -C $repoRoot fetch origin --prune 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Write-Stop "git fetch origin failed" @(
            ($fetch | ForEach-Object { "  $_" }),
            'A base that could not be refreshed is not a base: the tree would start from the last',
            'cache this checkout happened to hold. Nothing was made.')
    }
}
$startRef = if ($detached) { $Detach } else { $Base }
$baseSha = "$(& git -C $repoRoot rev-parse --verify --quiet "$startRef^{commit}" 2>$null)".Trim()
if (-not $baseSha) {
    Write-Stop "'$startRef' does not resolve to a commit" @(
        "The tree has to start from a commit, and '$startRef' is not one in this repository. Nothing was made.")
}

# --- make it ----------------------------------------------------------------
# git reports progress on stderr; under Windows PowerShell 5.1 a redirected stderr line is an error record,
# which `Stop` turns into a failure of a command that succeeded.
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$add = if ($detached) {
    @(& git -C $repoRoot worktree add --detach $pathFull $baseSha 2>&1)
} else {
    @(& git -C $repoRoot worktree add $pathFull -b $Branch $baseSha 2>&1)
}
$addExit = $LASTEXITCODE
$ErrorActionPreference = $previousPreference
if ($addExit -ne 0) {
    Write-Stop "git worktree add failed" @(
        ($add | ForEach-Object { "  $_" }),
        "Nothing was made at $pathFull.")
}
Write-Host "made $pathFull on $checkoutName at $($baseSha.Substring(0, 9))"

# The settings file is git-ignored and machine-local, so a new tree never has one. The pre-push hook
# reads `mergeWorktree` from it and refuses a develop push from a tree without it, and the host tools
# read the rest; a tree without it is a tree whose first refusal is about a missing file rather than
# about the change.
$settingsSource = Join-Path $repoRoot 'internal/tools/camel-gate.local.json'
$settingsTarget = Join-Path $pathFull 'internal/tools/camel-gate.local.json'
if (Test-Path -LiteralPath $settingsSource) {
    Copy-Item -LiteralPath $settingsSource -Destination $settingsTarget -Force
    Write-Host "copied internal/tools/camel-gate.local.json from $repoRoot"
} else {
    Write-Host "warning: $settingsSource does not exist, so the new tree has no settings file; a develop push from it will be refused until one is written" -ForegroundColor Yellow
}

# --- install the guard ------------------------------------------------------
# Only the repository's own installer is run: husky owns the files in that directory, and a copy
# written by hand here would be a second implementation of the shim, correct until husky changes it.
$hooksPathBefore = "$(& git -C $pathFull config --get core.hooksPath 2>$null)".Trim()

if (-not $NoHusky) {
    # The tree has no `node_modules` of its own until something installs them, so the local binary is
    # tried first (a tree that was set up some other way has it) and `npx` after it. `npx` asks for
    # the version this repository's package.json pins.
    $localHusky = Join-Path $pathFull 'node_modules/.bin/husky'
    $install = if (Test-Path -LiteralPath $localHusky) {
        @{ File = $localHusky; Arguments = @(); How = 'node_modules/.bin/husky' }
    } else {
        @{ File = 'npx'; Arguments = @('--yes', 'husky'); How = 'npx --yes husky' }
    }
    Write-Host "installing hooks: $($install.How)"
    Push-Location $pathFull
    try {
        $installOut = @(& $install.File @($install.Arguments) 2>&1)
        $installExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    foreach ($line in $installOut) { if ("$line".Trim()) { Write-Host "  $line" } }
    if ($installExit -ne 0) {
        Write-Host ''
        Write-Host "STOPPED: $($install.How) failed with exit $installExit, so the tree at $pathFull is NOT guarded (exit 1)" -ForegroundColor Red
        Write-Host "  The tree is left in place: deleting it would delete whatever is already in it."
        Write-Host "  What fixes it: cd '$pathFull' ; npx husky    -- and check again with -VerifyOnly."
        exit 1
    }
}

$hooksPathAfter = "$(& git -C $pathFull config --get core.hooksPath 2>$null)".Trim()
if ($hooksPathBefore -ne $hooksPathAfter) {
    # `core.hooksPath` is shared by every worktree of this repository, so a step that moves it does
    # something to all of them. Said out loud rather than passed over: the value belongs to the
    # machine, not to the tree that was just made.
    Write-Host "warning: core.hooksPath changed by the install: '$hooksPathBefore' -> '$hooksPathAfter' (that setting is shared by every worktree of this repository)" -ForegroundColor Yellow
}

# --- prove the guard speaks -------------------------------------------------
# The claim "husky exited 0" is about a directory; this claim is about a push.
$guard = Test-PushHookGuard -Tree $pathFull -Target $(if ($detached) { 'the commit it publishes' } else { $Branch })
if (-not $guard.Ok) {
    Write-Host ''
    Write-Host "STOPPED: the tree at $pathFull was made but its push guard does not answer (exit 1)" -ForegroundColor Red
    foreach ($line in $guard.Message) { Write-Host "  $line" }
    Write-Host "  What fixes it: cd '$pathFull' ; npx husky    -- and check again with -VerifyOnly."
    exit 1
}
foreach ($line in $guard.Message) { Write-Host $line }

Write-Host ''
Write-Host "ready: $pathFull on $checkoutName at $($baseSha.Substring(0, 9)), hooks at $($guard.HookFile)" -ForegroundColor Green
exit 0