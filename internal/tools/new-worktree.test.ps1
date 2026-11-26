# The worktree pool, against a throwaway repository with a bare `origin`.
#
# A worktree that is made once and handed from lane to lane keeps its git-ignored build outputs, so
# the next lane's build is incremental. What has to hold for that to be safe:
#
#   1. a slot with a tracked change, or an untracked source file, is refused by `-Reuse` and nothing in
#      it moves: the branch, the change and the file are all still there afterwards;
#   2. a clean slot is switched to the new branch at the base's tip; an ignored `build/marker` is
#      still there; a file that the old branch ignored and the new one does not (so it is untracked
#      and not ignored once the switch is done) is gone, because the next lane must not inherit it;
#   3. the settings file is copied into the reused slot and the push guard is read -- the same two
#      functions the add path calls;
#   4. a path that is not a worktree of the repository is refused;
#   5. `-Release` refuses a dirty slot and otherwise detaches it at the integration tip, leaving the
#      branch for whoever merged it to delete;
#   6. `-Pool <dir>` lists each slot under the directory with its branch, whether it is clean and whether
#      the branch is merged, and calls exactly the clean detached ones free.
#
#   pwsh -NoProfile -File internal/tools/new-worktree.test.ps1
#   pwsh -NoProfile -File internal/tools/new-worktree.test.ps1 -Keep   (leave the fixture)

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
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & git -C $Repo @Args 2>&1
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($exit -ne 0) { throw "git $($Args -join ' ') failed in $Repo`: $($out -join ' ')" }
    @($out | ForEach-Object { "$_".Trim() })
}

function Write-FixtureFile {
    param([string] $Repo, [string] $Name, [string] $Text)
    $full = Join-Path $Repo $Name
    $dir = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [System.IO.File]::WriteAllText($full, "$Text`n", (New-Object System.Text.UTF8Encoding($false)))
}

$script = Join-Path $PSScriptRoot 'new-worktree.ps1'
$shell = (Get-Process -Id $PID).Path

# Runs the script under test in a child shell and returns its exit code and every line it printed.
function Invoke-Script {
    param([string[]] $Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = @(& $shell -NoProfile -File $script @Arguments 2>&1 | ForEach-Object { "$_" })
    $exit = $LASTEXITCODE
    $ErrorActionPreference = $previous
    [pscustomobject] @{ Exit = $exit; Text = ($out -join "`n"); Lines = $out }
}

$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("worktree-pool-" + [guid]::NewGuid().ToString('n').Substring(0, 12))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$origin = Join-Path $scratch 'origin.git'
$repo = Join-Path $scratch 'main'
$pool = Join-Path $scratch 'pool'

try {
    Invoke-Git $scratch init --quiet --bare --initial-branch=integration $origin | Out-Null
    Invoke-Git $scratch clone --quiet $origin $repo | Out-Null
    Invoke-Git $repo config user.email 'probe@example.invalid' | Out-Null
    Invoke-Git $repo config user.name 'probe' | Out-Null
    Invoke-Git $repo config core.autocrlf false | Out-Null
    Invoke-Git $repo switch --quiet -c integration | Out-Null
    Write-FixtureFile $repo '.gitignore' "build/`ninternal/tools/camel-gate.local.json"
    Write-FixtureFile $repo 'src/a.txt' 'one'
    Write-FixtureFile $repo 'internal/tools/keep.txt' 'keeps the directory'
    Invoke-Git $repo add . | Out-Null
    Invoke-Git $repo commit --quiet -m 'base' | Out-Null
    Invoke-Git $repo push --quiet origin integration | Out-Null
    Write-FixtureFile $repo 'internal/tools/camel-gate.local.json' '{"mergeWorktree":"x"}'

    # A branch that also ignores `gen/`, so a slot on it can hold an ignored `gen/` file that the
    # integration line does not ignore.
    Invoke-Git $repo switch --quiet -c old-line | Out-Null
    Write-FixtureFile $repo '.gitignore' "build/`ninternal/tools/camel-gate.local.json`ngen/"
    Invoke-Git $repo commit --quiet -am 'ignore gen' | Out-Null
    Invoke-Git $repo switch --quiet integration | Out-Null

    $s1 = Join-Path $pool 'slot-1'
    $s2 = Join-Path $pool 'slot-2'
    $s3 = Join-Path $pool 'slot-3'
    foreach ($pair in @(@($s1, 'lane/one', 'old-line'), @($s2, 'lane/two', 'origin/integration'), @($s3, 'lane/three', 'origin/integration'))) {
        $made = Invoke-Script @('-Path', $pair[0], '-Branch', $pair[1], '-Base', $pair[2], '-FromCheckout', $repo, '-NoFetch', '-NoHusky')
        Check "the add path still makes $($pair[0] | Split-Path -Leaf) on $($pair[1])" ($made.Exit -eq 0) $made.Text
    }

    # Build outputs a lane left behind in slot 1, and a file only the old branch ignores.
    Write-FixtureFile $s1 'build/marker' 'kept'
    Write-FixtureFile $s1 'gen/stale.txt' 'ignored on the old branch only'

    # --- 1. a slot that is not clean is refused and not touched --------------
    Write-FixtureFile $s2 'src/a.txt' 'edited'
    $dirty = Invoke-Script @('-Reuse', $s2, '-Branch', 'lane/next', '-FromCheckout', $repo, '-NoFetch')
    $branchAfter = (Invoke-Git $s2 branch --show-current) -join ''
    Check 'a slot with a tracked change is refused (exit 2)' ($dirty.Exit -eq 2) "exit $($dirty.Exit): $($dirty.Text)"
    Check 'the refusal says what is there' ($dirty.Text -match 'src/a\.txt') $dirty.Text
    Check 'the refused slot is still on its branch with its change' `
        (($branchAfter -eq 'lane/two') -and ((Get-Content -LiteralPath (Join-Path $s2 'src/a.txt') -Raw).Trim() -eq 'edited')) "branch '$branchAfter'"
    Check 'the branch it was asked to take was not created' (((Invoke-Git $repo branch --list 'lane/next') -join '') -eq '')
    Invoke-Git $s2 checkout --quiet -- src/a.txt | Out-Null

    Write-FixtureFile $s2 'src/new.txt' 'unfinished work'
    $untracked = Invoke-Script @('-Reuse', $s2, '-Branch', 'lane/next', '-FromCheckout', $repo, '-NoFetch')
    Check 'a slot with an untracked source file is refused (exit 2)' ($untracked.Exit -eq 2) "exit $($untracked.Exit): $($untracked.Text)"
    Check 'the untracked file is still there' (Test-Path -LiteralPath (Join-Path $s2 'src/new.txt'))
    Remove-Item -LiteralPath (Join-Path $s2 'src/new.txt') -Force

    # --- 4. not a worktree of this repository --------------------------------
    $outside = Join-Path $scratch 'plain-dir'
    New-Item -ItemType Directory -Force -Path $outside | Out-Null
    $notTree = Invoke-Script @('-Reuse', $outside, '-Branch', 'lane/next', '-FromCheckout', $repo, '-NoFetch')
    Check 'a directory that is not a worktree of this repository is refused (exit 2)' ($notTree.Exit -eq 2) "exit $($notTree.Exit): $($notTree.Text)"

    # --- 2 + 3. a clean slot is taken ----------------------------------------
    $settings = Join-Path $s1 'internal/tools/camel-gate.local.json'
    if (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings -Force }
    $take = Invoke-Script @('-Reuse', $s1, '-Branch', 'lane/next', '-Base', 'origin/integration', '-FromCheckout', $repo, '-NoFetch')
    Check 'a clean slot is taken (exit 0)' ($take.Exit -eq 0) "exit $($take.Exit): $($take.Text)"
    Check 'the slot is on the new branch' (((Invoke-Git $s1 branch --show-current) -join '') -eq 'lane/next')
    $tip = (Invoke-Git $repo rev-parse origin/integration) -join ''
    Check 'the new branch starts at the base tip' (((Invoke-Git $s1 rev-parse HEAD) -join '') -eq $tip)
    Check 'the ignored build output is still there' (Test-Path -LiteralPath (Join-Path $s1 'build/marker'))
    Check 'the file only the old branch ignored is gone' (-not (Test-Path -LiteralPath (Join-Path $s1 'gen/stale.txt')))
    Check 'the settings file was copied into the slot' (Test-Path -LiteralPath $settings)
    Check 'the push guard was read' ($take.Text -match 'push guard|guard|hooks') $take.Text
    Check 'the slot has nothing to commit afterwards' (@(Invoke-Git $s1 status --porcelain).Count -eq 0)

    # --- 5. release ----------------------------------------------------------
    Write-FixtureFile $s1 'src/b.txt' 'work'
    $refused = Invoke-Script @('-Release', $s1, '-FromCheckout', $repo, '-NoFetch')
    Check 'release refuses a slot with an untracked source file (exit 2)' ($refused.Exit -eq 2) "exit $($refused.Exit): $($refused.Text)"
    Check 'the refused release left the branch' (((Invoke-Git $s1 branch --show-current) -join '') -eq 'lane/next')
    Remove-Item -LiteralPath (Join-Path $s1 'src/b.txt') -Force

    $release = Invoke-Script @('-Release', $s1, '-FromCheckout', $repo, '-NoFetch')
    Check 'release of a clean slot succeeds (exit 0)' ($release.Exit -eq 0) "exit $($release.Exit): $($release.Text)"
    Check 'the released slot is detached at the integration tip' `
        ((((Invoke-Git $s1 branch --show-current) -join '') -eq '') -and (((Invoke-Git $s1 rev-parse HEAD) -join '') -eq $tip))
    Check 'the branch it carried is left for whoever merged it' (((Invoke-Git $repo branch --list 'lane/next') -join '') -ne '')
    Check 'the ignored build output survived the release' (Test-Path -LiteralPath (Join-Path $s1 'build/marker'))

    # --- 6. the listing ------------------------------------------------------
    # slot-1 is released (free); slot-2 is on lane/two, clean and with nothing of its own (busy: it holds
    # a branch); slot-3 gets a commit that is not in the integration line and a change (busy, dirty).
    Write-FixtureFile $s3 'src/c.txt' 'lane work'
    Invoke-Git $s3 add src/c.txt | Out-Null
    Invoke-Git $s3 commit --quiet -m 'lane work' | Out-Null
    Write-FixtureFile $s3 'src/a.txt' 'edited again'
    $list = Invoke-Script @('-Pool', $pool, '-FromCheckout', $repo, '-NoFetch')
    Check 'the listing exits 0' ($list.Exit -eq 0) "exit $($list.Exit): $($list.Text)"
    $row1 = @($list.Lines | Where-Object { $_ -match 'slot-1' })
    $row2 = @($list.Lines | Where-Object { $_ -match 'slot-2' })
    $row3 = @($list.Lines | Where-Object { $_ -match 'slot-3' })
    Check 'the released slot is listed as free and detached' (($row1.Count -eq 1) -and ($row1[0] -match '\bfree\b') -and ($row1[0] -match 'detached')) ($row1 -join ' / ')
    Check 'a clean slot on a branch is busy and shows its branch' (($row2.Count -eq 1) -and ($row2[0] -match '\bbusy\b') -and ($row2[0] -match 'lane/two')) ($row2 -join ' / ')
    Check 'a dirty slot with an unmerged branch is busy, dirty and not merged' `
        (($row3.Count -eq 1) -and ($row3[0] -match '\bbusy\b') -and ($row3[0] -match 'dirty') -and ($row3[0] -match 'merged=no')) ($row3 -join ' / ')
    Check 'a branch with nothing beyond the integration line is reported merged' (($row2.Count -eq 1) -and ($row2[0] -match 'merged=yes')) ($row2 -join ' / ')
    Check 'the main checkout is not listed as a slot' (-not ($list.Lines | Where-Object { $_ -match [regex]::Escape($repo) }))
} finally {
    if (-not $Keep) {
        if (Test-Path -LiteralPath $repo) { & git -C $repo worktree prune 2>$null }
        Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
    } else { Write-Host "  fixture kept at $scratch" }
}

Write-Host ''
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0
