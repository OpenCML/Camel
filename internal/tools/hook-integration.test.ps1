# The shape an arrival on `integration` must have (D-1124-78), checked where the push is made.
#
# `merge-lane.ps1` asserts the shape of the commit it is about to push, and that assertion cannot see
# a push it did not make. On 2026-11-24 the three seats pushed merges whose first parent was not the
# tip (they merged `integration` into the seat and fast-forwarded the seat onto it) and `50a77d7bf2`
# went in with a single parent -- none of which went through the tool. So the assertion is also in
# `scripts/hooks/pre-push`, which sees every ref that leaves this machine.
#
# What is locked down, against the real hook file and a throwaway repository:
#
#   1. a single-parent commit pushed to `refs/heads/integration` -> refused, and the refusal says
#      一笔车道提交直接进了 integration;
#   2. a merge whose first parent is not the remote tip -> refused, with the sentence about merging
#      integration into the seat and the command that redoes it, rerere included;
#   3. a merge whose first parent IS the remote tip -> allowed (the regression that matters most:
#      the merge lane's own pushes must keep working);
#   4. a push to another branch, and a delete of `integration`, are left alone.
#
# `git push --no-verify` runs no hook at all, so none of this can stop a push that asks to skip it.
# That is why the hook's header and `internal/tools/README.md` say so rather than pretending
# otherwise: a check that can be bypassed is only useful if a bypass is visible in the record.
#
# It needs `sh`, which git ships: the interpreter is found beside git itself, so this runs on a
# machine with Git for Windows and nothing else installed.
#
#   pwsh -NoProfile -File internal/tools/hook-integration.test.ps1
#   pwsh -NoProfile -File internal/tools/hook-integration.test.ps1 -Keep

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

function Get-GitText {
    param([string] $Repo, [Parameter(ValueFromRemainingArguments = $true)][string[]] $Args)
    ((Invoke-Git $Repo @Args) -join "`n").Trim()
}

function Write-FixtureFile {
    param([string] $Repo, [string] $Name, [string] $Text)
    $full = Join-Path $Repo $Name
    $dir = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [System.IO.File]::WriteAllText($full, "$Text`n", (New-Object System.Text.UTF8Encoding($false)))
}

# --- the interpreter git itself would use ------------------------------------
# A hook is run by `sh`; on Windows that is the one Git for Windows installs beside git, and it is
# not on PATH. Asking git for its exec path and stepping up out of mingw64/libexec/git-core is how
# the test finds the same interpreter without a guess about any one machine's Git version.
$execPath = (& git --exec-path).Trim()
$gitRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $execPath))
$shCandidates = @(
    (Join-Path $gitRoot 'usr/bin/sh.exe'),
    (Join-Path $gitRoot 'bin/sh.exe'),
    (Join-Path $gitRoot 'usr/bin/sh')
)
$sh = $shCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $sh) { throw "no sh found beside git (looked in $($shCandidates -join ', ')); the hook cannot be run" }

# The hook runs under git's environment, where `sh` finds git's own /usr/bin on PATH (`cat`, `tr`,
# `sed` are binaries there). A bare sh.exe started from PowerShell has only the machine's PATH, so
# the child gets the same two directories git gives a hook -- otherwise the test would fail on
# `cat: command not found` and read it as a refusal.
$savedPath = $env:PATH
$env:PATH = "$gitRoot\usr\bin;$gitRoot\mingw64\bin;$env:PATH"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$hook = Join-Path $repoRoot 'scripts\hooks\pre-push'
Check 'the hook file exists where husky calls it' (Test-Path -LiteralPath $hook) $hook

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-hook-integration-$PID")
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$repo = Join-Path $scratch 'repo'
New-Item -ItemType Directory -Force -Path $repo | Out-Null
& git init --quiet --initial-branch=integration $repo 2>&1 | Out-Null
Invoke-Git $repo config user.name 'hook integration test' | Out-Null
Invoke-Git $repo config user.email 'hook-integration@example.invalid' | Out-Null
Invoke-Git $repo config commit.gpgsign false | Out-Null

Write-FixtureFile -Repo $repo -Name 'a.txt' -Text 'a'
Invoke-Git $repo add a.txt | Out-Null
Invoke-Git $repo commit -q -m 'the base a tip had' | Out-Null
$base = Get-GitText $repo rev-parse HEAD

# The lane, and the merge of it: parents (base, lane) -- the shape the rule asks for. The merge is
# made on the base and left there, which is the moment a seat's merge is in: prepared on the tip
# that was, not on the one that will be.
Invoke-Git $repo checkout -q -B lane/probe $base | Out-Null
Write-FixtureFile -Repo $repo -Name 'probe.txt' -Text 'from the lane'
Invoke-Git $repo add probe.txt | Out-Null
Invoke-Git $repo commit -q -m 'probe: the lane change' | Out-Null
$lane = Get-GitText $repo rev-parse HEAD
Invoke-Git $repo checkout -q -B integration $base | Out-Null
Invoke-Git $repo merge -q --no-ff lane/probe -m 'merge probe: the lane change' | Out-Null
$merge = Get-GitText $repo rev-parse HEAD

# The tip moving on while the merge waits: a commit on the base, so it is a child of the base and
# not of the merge.
Invoke-Git $repo checkout -q -B integration $base | Out-Null
Write-FixtureFile -Repo $repo -Name 'b.txt' -Text 'b'
Invoke-Git $repo add b.txt | Out-Null
Invoke-Git $repo commit -q -m 'integration moved on' | Out-Null
$tip = Get-GitText $repo rev-parse HEAD
$parents = @((Get-GitText $repo rev-list --parents -n 1 $merge) -split ' ')
Check 'the fixture merge has the tip as its first parent' ($parents.Count -eq 3 -and $parents[1] -eq $base -and $parents[2] -eq $lane) ($parents -join ' ')

$zeros = '0000000000000000000000000000000000000000'

# The hook reads one line per ref from stdin, in the order git writes them, and runs in the tree
# doing the push: `git rev-parse --show-toplevel` is where it looks for the settings file.
function Invoke-Hook {
    param([string[]] $Lines)
    Push-Location $repo
    try {
        $out = ($Lines -join "`n") | & $sh $hook 2>&1
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    return [pscustomobject]@{ Code = $code; Text = (@($out) -join "`n") }
}

# --- 1. a single-parent commit is not an arrival ------------------------------
$single = Invoke-Hook @("refs/heads/probe $lane refs/heads/integration $base")
Check 'a single-parent commit pushed to integration is refused' ($single.Code -ne 0) "exit=$($single.Code) $($single.Text)"
Check 'and the refusal says a lane commit went straight in' ($single.Text -match '一笔车道提交直接进了 integration') $single.Text
Check 'and it names the commit and the branch' ($single.Text -match $lane -and $single.Text -match 'refs/heads/integration') $single.Text

# --- 2. a merge whose first parent is not the tip is refused ------------------
$stale = Invoke-Hook @("refs/heads/integration $merge refs/heads/integration $tip")
Check 'a merge whose first parent is not the remote tip is refused' ($stale.Code -ne 0) "exit=$($stale.Code) $($stale.Text)"
Check 'and the refusal names both ends of the mismatch' ($stale.Text -match "first parent is $base" -and $stale.Text -match $tip) $stale.Text
Check 'and it says the operator merged integration into the seat' ($stale.Text -match '你在把 integration 合进席位，而不是把车道合进 integration') $stale.Text
Check 'and it gives the command that redoes it' ($stale.Text -match 'checkout -B tmp origin/integration' -and $stale.Text -match 'merge --no-ff') $stale.Text
Check 'and it points at rerere for the resolutions already made' ($stale.Text -match 'rerere\.enabled true') $stale.Text

# --- 3. the merge lane's own push still passes --------------------------------
# The regression that matters most: the tool merges on the tip and pushes, and this hook must not be
# the thing that stops it.
$good = Invoke-Hook @("refs/heads/integration $merge refs/heads/integration $base")
Check 'a merge of the remote tip is allowed' ($good.Code -eq 0) "exit=$($good.Code) $($good.Text)"

# --- 4. other refs and deletes are left alone --------------------------------
$lanePush = Invoke-Hook @("refs/heads/probe $lane refs/heads/probe $lane")
Check 'a push to a lane branch is not spoken about' ($lanePush.Code -eq 0 -and $lanePush.Text.Trim() -eq '') "exit=$($lanePush.Code) $($lanePush.Text)"
$delete = Invoke-Hook @("(delete) $zeros refs/heads/integration $tip")
Check 'deleting integration is not a shape question' ($delete.Code -eq 0) "exit=$($delete.Code) $($delete.Text)"

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
$env:PATH = $savedPath
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0

