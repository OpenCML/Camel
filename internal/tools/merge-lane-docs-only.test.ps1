# A documentation-only merge, end to end: the selection refuses (exit 2) -> docs-only -> record -> push.
#
# Why it exists: the docs-only branch of `merge-lane` was written against a conjunction that could
# never hold -- `$selectExit -eq 0` beside "an empty selection is exit 2" -- so the branch was
# unreachable and a one-page merge was refused as unmeasurable. `merge-lane-docs.test.ps1` checks the
# classifier in isolation, which is what B2 shipped; it cannot see whether the branch that reads the
# classifier is ever reached. This does: a real merge commit in a throwaway repository, the same
# `merge-lane.ps1` and `merge-record.mjs` this checkout ships, a real push, and the record file read
# back off disk.
#
# What is real and what is a stand-in, named so nobody reads more into a green here:
#
#   real      `internal/tools/merge-lane.ps1`, `internal/tools/merge-lane-docs.ps1`,
#             `scripts/merge-record.mjs`, `scripts/plan-set.mjs`, git, the merge commit, the push,
#             the ref on the scratch remote, the `measured: none` record;
#   stand-in  the clang gate (a stub `camel-remote.ps1` printing a pass line: the real one talks to
#             the build host) and the plan runner (`scripts/test.js`, a stub that prints the runner's
#             own no-rule refusal and exits 2, or exits 0 with nothing selected). The refusal
#             sentence is the one `test/runner.mjs` prints, and `merge-lane-docs.test.ps1` reads the
#             runner to check that it still does. `scripts/headers.mjs` is a stub too: the real
#             walker reads the whole tree and this fixture holds six files.
#
# The pre-push hook is not part of this fixture: it governs `refs/heads/develop` only, and these
# merges land on `integration`. The record is still asserted, because it is what the hook would read.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-docs-only.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-docs-only.test.ps1 -Keep   (leave the fixtures)

[CmdletBinding()]
param(
    # Keep the fixture trees for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

$script:checked = 0
$script:failed = 0

function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
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
    param([string] $Where, [string[]] $ArgList)
    $out = & git -C $Where @ArgList 2>&1
    return @{ Exit = $LASTEXITCODE; Text = (($out | ForEach-Object { "$_" }) -join "`n") }
}

function Write-Text {
    param([string] $Path, [string] $Text)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    [IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("camel-docs-only-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
Write-Host "scratch: $scratch"

# The two instruments this fixture replaces, and why each is a stub rather than the shipped file.
$clangStub = @'
param([string] $Path = '', [string[]] $Check = @(), [switch] $NoBuild, [switch] $AllowStale, [switch] $NoGpuWatch)
Write-Host '[camel-remote] clang-syntax: 0 unit(s) checked, no diagnostic (fixture stand-in)'
exit 0
'@

$headersStub = @'
// Stand-in for scripts/headers.mjs: the real walker reads every member file of a tree and this
// fixture holds six, none of them C/C++ or Camel. What this test is about is not the header check.
console.log('headers: every member file carries its two-line header (fixture stand-in)')
process.exit(0)
'@

# The runner stub's answer, per scenario. `$Body` is a fragment of JavaScript.
function New-RunnerStub {
    param([string] $Body)
    return @"
// Stand-in for the plan runner (`scripts/test.js --changed --list`) in a fixture with no plan tree.
// The refusal sentence is test/runner.mjs's own; merge-lane-docs.test.ps1 checks that it still is.
$Body
"@
}

function New-Fixture {
    param(
        [string] $Name,
        [hashtable] $LaneFiles,
        [string] $RunnerBody
    )
    $dir = Join-Path $scratch $Name
    $origin = Join-Path $dir 'origin.git'
    $work = Join-Path $dir 'work'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    & git init --quiet --bare --initial-branch=integration $origin 2>&1 | Out-Null
    & git clone --quiet $origin $work 2>&1 | Out-Null
    & git -C $work config user.email 'docs-only@example.invalid'
    & git -C $work config user.name 'docs only test'
    & git -C $work config commit.gpgsign false

    foreach ($rel in @('internal/tools/merge-lane.ps1', 'internal/tools/merge-lane-docs.ps1',
            'scripts/merge-record.mjs', 'scripts/plan-set.mjs', 'scripts/merge-readings.mjs')) {
        $to = Join-Path $work $rel
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath (Join-Path $repoRoot $rel) -Destination $to -Force
    }
    Write-Text (Join-Path $work 'internal/tools/camel-remote.ps1') $clangStub
    Write-Text (Join-Path $work 'scripts/headers.mjs') $headersStub
    Write-Text (Join-Path $work 'scripts/test.js') (New-RunnerStub -Body $RunnerBody)
    Write-Text (Join-Path $work 'internal/tools/camel-gate.local.json') `
        ((@{ sshHost = 'none'; mergeWorktree = $work } | ConvertTo-Json) + "`n")
    Write-Text (Join-Path $work 'README.md') "fixture for merge-lane's docs-only branch`n"

    Invoke-Git $work @('add', '-A') | Out-Null
    Invoke-Git $work @('commit', '-q', '-m', 'fixture base') | Out-Null
    Invoke-Git $work @('push', '-q', 'origin', 'HEAD:integration') | Out-Null

    Invoke-Git $work @('checkout', '-q', '-b', 'lane/x') | Out-Null
    foreach ($rel in @($LaneFiles.Keys)) { Write-Text (Join-Path $work $rel) $LaneFiles[$rel] }
    Invoke-Git $work @('add', '-A') | Out-Null
    Invoke-Git $work @('commit', '-q', '-m', 'lane change') | Out-Null
    Invoke-Git $work @('checkout', '-q', 'integration') | Out-Null
    Invoke-Git $work @('merge', '--no-ff', '-q', 'lane/x', '-m', 'merge lane/x: fixture') | Out-Null

    $merge = (Invoke-Git $work @('rev-parse', 'HEAD')).Text.Trim()
    $before = (Invoke-Git $work @('rev-parse', 'refs/remotes/origin/integration')).Text.Trim()

    $out = & pwsh -NoProfile -File (Join-Path $work 'internal/tools/merge-lane.ps1') `
        -MergeCommit $merge -Branch 'lane/x' -Target integration -MainCheckout $work `
        -BaselineWorktree (Join-Path $dir 'baseline') -BaselineBranch 'camel-baseline-docs' 2>&1
    $laneExit = $LASTEXITCODE
    $laneText = (($out | ForEach-Object { "$_" }) -join "`n")

    # Read the remote back, not the push's exit code: "the push returned 0" and "the ref moved to the
    # commit I made" are two readings and only the second one is the merge being on origin.
    $remoteLine = (Invoke-Git $work @('ls-remote', 'origin', 'refs/heads/integration')).Text.Trim()
    $remoteSha = if ($remoteLine) { ($remoteLine -split "`t")[0] } else { '' }

    return @{
        Dir      = $dir
        Work     = $work
        Merge    = $merge
        Before   = $before
        Exit     = $laneExit
        Text     = $laneText
        Remote   = $remoteSha
        Record   = (Join-Path $work '.git' 'camel-passed' "$merge.json")
    }
}

$noRuleRefusal = "const refusal = '[ERROR] --changed: no rule in test/plan-selection.json matches the 1 changed file(s): internal/questions/2026-11-24-x.cn.md, so the selection is empty; an empty selection must not become a plan expansion'`nconsole.error(refusal)`nprocess.exit(2)"
$baseUnreadable = "console.error(`"[ERROR] --changed: cannot read origin/develop (fatal: bad revision 'origin/develop')`")`nprocess.exit(2)"
$listedEmpty = "process.exit(0)"

# --- 1. the case that was unreachable: a no-rule refusal, every path documentation ------------
Write-Host ''
Write-Host 'scenario 1: one question page, the selection refuses with exit 2'
$docs = New-Fixture -Name 'refused' -RunnerBody $noRuleRefusal -LaneFiles @{
    'internal/questions/2026-11-24-x.cn.md' = "# a question`n`nA page no plan reads.`n"
}
Check 'the refused documentation merge is pushed (exit 0)' ($docs.Exit -eq 0) "exit $($docs.Exit): $($docs.Text)"
Check 'it says docs-only and that the selection refused' `
    ($docs.Text -match 'docs-only: nothing to measure \(selection refused: no rule matches, and every changed path is documentation\)') $docs.Text
Check 'the record says nothing was measured' `
    ((Test-Path -LiteralPath $docs.Record) -and ((Get-Content -LiteralPath $docs.Record -Raw | ConvertFrom-Json).measured -eq 'none')) $docs.Record
Check 'the record names docs-only as the gate that decided it' `
    ((Test-Path -LiteralPath $docs.Record) -and ((Get-Content -LiteralPath $docs.Record -Raw | ConvertFrom-Json).gates -contains 'docs-only'))
Check 'origin/integration is the merge commit that was read' ($docs.Remote -eq $docs.Merge) "remote $($docs.Remote), merge $($docs.Merge)"
Check 'the closing line names the branch and the merge' ($docs.Text -match 'merged lane/x at .+; origin/integration is .+ \(docs-only, nothing measured\)') $docs.Text

# --- 2. the regression that must not become docs-only: one tool in the change ------------------
Write-Host ''
Write-Host 'scenario 2: a question page beside a PowerShell tool'
$mixed = New-Fixture -Name 'mixed' -RunnerBody $noRuleRefusal -LaneFiles @{
    'internal/questions/2026-11-24-x.cn.md' = "# a question`n"
    'internal/tools/some-tool.ps1'         = "Write-Host 'a tool is not documentation'`n"
}
Check 'a change with one non-documentation file is still refused (exit 2)' ($mixed.Exit -eq 2) "exit $($mixed.Exit): $($mixed.Text)"
Check 'and the refusal is the selection one, not the docs-only line' `
    (($mixed.Text -match 'could not be selected') -and ($mixed.Text -notmatch 'docs-only: nothing to measure')) $mixed.Text
Check 'nothing was pushed' ($mixed.Remote -eq $mixed.Before) "remote $($mixed.Remote), before $($mixed.Before)"
Check 'and no record was written for it' (-not (Test-Path -LiteralPath $mixed.Record)) $mixed.Record

# --- 3. the old path that must keep working: the runner lists an empty selection, exit 0 -------
Write-Host ''
Write-Host 'scenario 3: the runner answers exit 0 with no plan (the listed-empty change)'
$listed = New-Fixture -Name 'listed' -RunnerBody $listedEmpty -LaneFiles @{
    'internal/backlog.md' = "# backlog`n`nA row.`n"
}
Check 'the listed-empty documentation merge is pushed (exit 0)' ($listed.Exit -eq 0) "exit $($listed.Exit): $($listed.Text)"
Check 'it says docs-only without the refusal parenthetical' `
    (($listed.Text -match 'docs-only: nothing to measure') -and ($listed.Text -notmatch 'selection refused')) $listed.Text
Check 'its record says nothing was measured' `
    ((Test-Path -LiteralPath $listed.Record) -and ((Get-Content -LiteralPath $listed.Record -Raw | ConvertFrom-Json).measured -eq 'none'))
Check 'origin/integration is its merge commit' ($listed.Remote -eq $listed.Merge) "remote $($listed.Remote), merge $($listed.Merge)"

# --- 4. the other exit 2: the change cannot be read, whatever the paths look like --------------
Write-Host ''
Write-Host 'scenario 4: a base that does not resolve, with documentation paths'
$blind = New-Fixture -Name 'blind' -RunnerBody $baseUnreadable -LaneFiles @{
    'internal/questions/2026-11-24-x.cn.md' = "# a question`n"
}
Check 'an unreadable base is refused even when every path is documentation (exit 2)' ($blind.Exit -eq 2) "exit $($blind.Exit): $($blind.Text)"
Check 'and the stop says the selection could not be read, not that there was nothing to measure' `
    (($blind.Text -match 'the selection could not be read') -and ($blind.Text -notmatch 'docs-only: nothing to measure')) $blind.Text
Check 'nothing was pushed for it' ($blind.Remote -eq $blind.Before) "remote $($blind.Remote), before $($blind.Before)"
Check 'and no record was written' (-not (Test-Path -LiteralPath $blind.Record)) $blind.Record

if (-not $Keep) { Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue }

Write-Host ''
Write-Host "$script:checked checks, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0