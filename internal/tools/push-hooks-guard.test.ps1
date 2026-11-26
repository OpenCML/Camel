# The hook that has to run, checked where a push is about to be made.
#
# `core.hooksPath` here is `.husky/_`, a path relative to the worktree that pushes, so a worktree
# husky never ran in has no hooks at all -- and git treats a hooks directory it cannot find as "there
# are no hooks", which is not a failure: the push succeeds and every rule in that directory is
# skipped in silence. A file that exists is therefore not the claim. The claim is that a push made
# from this tree reaches the rules, and the only reading that says so is the rules' own output.
#
# What is locked down, against a throwaway repository and a checker the test writes itself:
#
#   1. no `core.hooksPath` -> the check says so and passes, because a repository with no configured
#      hooks directory is never promised a guard (a bare repository, a fixture) and an absent
#      `pre-push` there is the normal state;
#   2. `core.hooksPath` set and the directory absent -> refused, and the message names the file and
#      the command that installs it;
#   3. the directory present and the hook exiting 0 with no output -> refused. THIS IS THE CASE THE
#      CHECK EXISTS FOR: an empty reading with exit 0 is a push with no guard, and reading it as a
#      pass is the defect;
#   4. the hook delegating to a checker that prints its reading -> passes, and what it reports is the
#      checker's own line and not the hook's;
#   5. the checker answering with traces and exit 1 -> still passes, with the reading reported and a
#      warning beside it: whether a tree passes the checker is the checker's business on the real
#      push, and a guard that refuses on it would answer a second question twice.
#
# `git push --no-verify` runs no hook at all, so none of this can stop a push that asks to skip it;
# that is said in the hook's own header rather than pretended otherwise.
#
#   pwsh -NoProfile -File internal/tools/push-hooks-guard.test.ps1
#   pwsh -NoProfile -File internal/tools/push-hooks-guard.test.ps1 -Keep   (leave the fixture)

[CmdletBinding()]
param(
    [switch] $Keep
)

$ErrorActionPreference = 'Stop'

$script:failed = 0
$script:checked = 0

function Check {
    param([string] $Name, [bool] $Ok, [string] $Detail = '')
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
    param([string] $Repo, [Parameter(ValueFromRemainingArguments = $true)][string[]] $Args)
    $out = & git -C $Repo @Args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git $($Args -join ' ') failed in $Repo`: $($out -join ' ')" }
    @($out | ForEach-Object { "$_".Trim() })
}

function Write-FixtureFile {
    param([string] $Repo, [string] $Name, [string] $Text)
    $full = Join-Path $Repo $Name
    $dir = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    # A shell script has to keep its line endings and start with no byte-order mark, or the
    # interpreter git uses reads the first line as a command name.
    [System.IO.File]::WriteAllText($full, "$Text`n", (New-Object System.Text.UTF8Encoding($false)))
}

. (Join-Path $PSScriptRoot 'push-hooks-guard.ps1')

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("hooks-guard-" + [guid]::NewGuid().ToString('n').Substring(0, 12))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$repo = Join-Path $scratch 'tree'
$savedPath = $env:PATH

try {
    Push-Location $scratch
    try { Invoke-Git $scratch init --quiet --initial-branch=main $repo } finally { Pop-Location }
    Invoke-Git $repo config user.email 'probe@example.invalid' | Out-Null
    Invoke-Git $repo config user.name 'probe' | Out-Null
    Write-FixtureFile $repo 'a.txt' 'one line'
    Invoke-Git $repo add . | Out-Null
    Invoke-Git $repo commit --quiet -m 'fixture' | Out-Null

    # The checker the fixture's hook calls. It is a file the test owns, so the reading the guard
    # reports can only have come from it.
    Write-FixtureFile $repo 'scripts/check-process-traces.mjs' @'
const args = process.argv.slice(2);
if (args.includes('--tick')) {
  console.log('process traces in comments: 2 (scanned 12 files, 34 comment lines)');
  console.log('  a.cpp:1: a line the checker would list');
  process.exit(1);
}
console.log('scanned 12 files, 34 comment lines, no trace');
process.exit(0);
'@

    # husky's own shape: a stub in the hooks directory that names the tracked hook beside it by
    # position -- which is why a hooks directory in the wrong place finds nothing and says nothing.
    Write-FixtureFile $repo '.husky/pre-push' @'
top=$(git rev-parse --show-toplevel 2>/dev/null) || top='.'
if [ "$1" = "origin" ]; then
    node "$top/scripts/check-process-traces.mjs" --root "$top"
fi
'@
    Write-FixtureFile $repo '.husky/_/h' @'
n=$(basename "$0")
s=$(dirname "$(dirname "$0")")/$n
[ ! -f "$s" ] && exit 0
sh -e "$s" "$@"
'@

    function Set-Shim {
        param([string] $Text)
        Write-FixtureFile $repo '.husky/_/pre-push' "#!/usr/bin/env sh`n$Text"
    }

    $gitSh = Resolve-GitSh
    Check 'the interpreter git would use was found' ($gitSh -ne '') "Resolve-GitSh returned '$gitSh'"
    $hooksDir = Resolve-GitHooksDir -Tree $repo
    Check 'the hooks directory resolves inside the fixture when nothing is configured' `
        ($hooksDir -eq (Join-Path $repo '.git/hooks')) "got '$hooksDir'"

    # --- 1. no configured hooks path: nothing is promised ----------------------
    $bare = Test-PushHookGuard -Tree $repo -Target integration
    Check 'no core.hooksPath passes, and says it has nothing to hold anyone to' `
        ($bare.Ok -and $bare.Kind -eq 'no hooks path') "Ok=$($bare.Ok) Kind=$($bare.Kind)"

    # --- 2. configured, and the directory is not there -------------------------
    Invoke-Git $repo config core.hooksPath '.husky/_' | Out-Null
    $missing = Test-PushHookGuard -Tree $repo -Target integration
    $namesFile = @($missing.Message | Where-Object { $_ -match 'pre-push' }).Count -gt 0
    $namesFix = @($missing.Message | Where-Object { $_ -match 'npx husky' }).Count -gt 0
    Check 'a configured hooks path with no pre-push is refused' `
        ((-not $missing.Ok) -and $missing.Kind -eq 'no hook file') "Ok=$($missing.Ok) Kind=$($missing.Kind)"
    Check 'the refusal names the file it looked for' $namesFile ($missing.Message -join ' / ')
    Check 'the refusal names the command that installs it' $namesFix ($missing.Message -join ' / ')

    # --- 3. the shim runs and prints nothing: no guard -------------------------
    Set-Shim 'exit 0'
    $silent = Test-PushHookGuard -Tree $repo -Target integration
    Check 'a hook that exits 0 with an empty reading is refused, not passed' `
        ((-not $silent.Ok) -and $silent.Kind -eq 'no reading') "Ok=$($silent.Ok) Kind=$($silent.Kind) Exit=$($silent.Exit)"

    # --- 3b. present but pointing nowhere --------------------------------------
    Set-Shim '. "$(dirname "$0")/h"'
    Write-FixtureFile $repo '.husky/_/h' @'
n=$(basename "$0")
s=$(dirname "$(dirname "$0")")/$n
[ ! -f "$s" ] && exit 0
sh -e "$s" "$@"
'@
    Remove-Item -LiteralPath (Join-Path $repo '.husky/pre-push') -Force
    $nowhere = Test-PushHookGuard -Tree $repo -Target integration
    Check 'a shim whose tracked hook is absent is refused, not passed' `
        ((-not $nowhere.Ok) -and $nowhere.Kind -eq 'no reading') "Ok=$($nowhere.Ok) Kind=$($nowhere.Kind)"

    # --- 4. the real shape: the shim delegates, the checker answers ------------
    Write-FixtureFile $repo '.husky/pre-push' @'
top=$(git rev-parse --show-toplevel 2>/dev/null) || top='.'
if [ "$1" = "origin" ]; then
    node "$top/scripts/check-process-traces.mjs" --root "$top"
fi
'@
    $guarded = Test-PushHookGuard -Tree $repo -Target integration
    $reading = @($guarded.Message | Where-Object { $_ -match 'scanned 12 files, 34 comment lines, no trace' }).Count -gt 0
    Check 'a hook whose chain reaches the checker passes' `
        ($guarded.Ok -and $guarded.Kind -eq 'guarded') "Ok=$($guarded.Ok) Kind=$($guarded.Kind) Exit=$($guarded.Exit)"
    Check 'what it reports is the checker own line, not the hook' $reading ($guarded.Message -join ' / ')

    # --- 5. the checker answers with traces: reported, not refused -------------
    Write-FixtureFile $repo '.husky/pre-push' @'
top=$(git rev-parse --show-toplevel 2>/dev/null) || top='.'
if [ "$1" = "origin" ]; then
    node "$top/scripts/check-process-traces.mjs" --root "$top" --tick
fi
'@
    $red = Test-PushHookGuard -Tree $repo -Target integration
    $sawHits = @($red.Message | Where-Object { $_ -match 'process traces in comments: 2' }).Count -gt 0
    $warned = @($red.Message | Where-Object { $_ -match 'warning' }).Count -gt 0
    Check 'a reading that lists traces still passes the guard, because the guard is not the checker' `
        ($red.Ok -and $red.Kind -eq 'guarded') "Ok=$($red.Ok) Kind=$($red.Kind) Exit=$($red.Exit)"
    Check 'the traces it lists are reported' $sawHits ($red.Message -join ' / ')
    Check 'and the exit that will refuse the real push is said out loud' $warned ($red.Message -join ' / ')

    # --- 6. a confirmation that nothing above moved the machine's setting ------
    $pathNow = (Invoke-Git $repo config --get core.hooksPath) -join ''
    Check 'the check left core.hooksPath where it was' ($pathNow -eq '.husky/_') "got '$pathNow'"

    # --- 7. the target named is the one in the sentence ------------------------
    # A pass reports the checker's line and nothing of its own, so the target is read on a refusal --
    # which is where a reader needs to know which branch was not written.
    $namedPass = Test-PushHookGuard -Tree $repo -Target develop
    Check 'the result carries the target it was given' ($namedPass.Target -eq 'develop') "Target=$($namedPass.Target)"
    Remove-Item -LiteralPath (Join-Path $repo '.husky/_') -Recurse -Force
    $namedRefusal = Test-PushHookGuard -Tree $repo -Target develop
    Check 'the refusal says which branch was not written' `
        (((-not $namedRefusal.Ok) -and (@($namedRefusal.Message | Where-Object { $_ -match 'develop' }).Count -gt 0))) `
        ($namedRefusal.Message -join ' / ')
} finally {
    $env:PATH = $savedPath
    if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
    else { Write-Host "  fixture kept at $scratch" }
}

Write-Host ''
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0