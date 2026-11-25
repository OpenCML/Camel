# The key a baseline is stored under, and what that key may contain (D-1124-78's neighbour, the
# baseline note: "baseline keyed by tip sha").
#
# A baseline is "what the plans read on THIS commit". So its key is the commit -- and the plan set it
# was measured over -- and nothing else: not the seat that measured it, not the path it happened to
# be checked out at, not the run that happened to make it. On 2026-11-24 three seats merged onto one
# `integration` tip and each built the same tree, ran the same plans and stored the same rows: three
# worktrees, three host builds, three runs, one reading.
#
# `Get-BaselineCheckout` is the decision both merge tools make before they measure: which checkout
# holds the baseline for a tip, and what branch it is on. Everything else in this file is about that
# function's answers:
#
#   1. the same sha twice: the second call is a reuse (`tip`), not a rebuild;
#   2. a different sha: a different tree (`fresh`), so the old reading cannot be read as this one's;
#   3. the legacy fixed-name tree: a path holding another commit is called stale, said out loud with
#      the sentence `stale baseline beside the tip: rebuilding`, and moved -- never read;
#   4. nothing in the key is about who measured it: two seats that name different legacy paths and
#      branches still resolve the same tip to one path and one branch, and that branch spells the
#      sha;
#   5. the wiring: both tools call the decision, and the reading's own directory is the git common
#      directory's `camel-readings`, which every worktree of this repository shares.
#
# The rows themselves are filed by `scripts/merge-readings.mjs` as `<sha12>-<planSetHash>-v<normal>
# .jsonl`: the commit, the plan set, the reading's version -- no seat. This test does not re-derive
# that name; it pins the half the tools own, which is the tree and the branch.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-baseline.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-baseline.test.ps1 -Keep

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

# --- the shipped decision, read out of the script ----------------------------
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$laneScript = Join-Path $repoRoot 'internal\tools\merge-lane.ps1'
$batchScript = Join-Path $repoRoot 'internal\tools\merge-batch.ps1'

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($laneScript, [ref] $tokens, [ref] $parseErrors)
Check 'merge-lane.ps1 parses' (@($parseErrors).Count -eq 0) (($parseErrors | ForEach-Object { "$($_.Extent.StartLineNumber): $($_.Message)" }) -join '; ')
$found = @($ast.FindAll({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-BaselineCheckout'
        }, $true))
Check 'merge-lane.ps1 defines Get-BaselineCheckout once' ($found.Count -eq 1) "found $($found.Count)"
if ($found.Count -eq 1) { Invoke-Expression $found[0].Extent.Text }

# --- a fixture repository, and three commits --------------------------------
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-baseline-key-$PID")
New-Item -ItemType Directory -Force -Path $scratch | Out-Null
$repo = Join-Path $scratch 'repo'
New-Item -ItemType Directory -Force -Path $repo | Out-Null
& git init --quiet --initial-branch=integration $repo 2>&1 | Out-Null
Invoke-Git $repo config user.name 'baseline key test' | Out-Null
Invoke-Git $repo config user.email 'baseline-key@example.invalid' | Out-Null
Invoke-Git $repo config commit.gpgsign false | Out-Null
$shas = @()
foreach ($name in 'one', 'two', 'three') {
    Write-FixtureFile -Repo $repo -Name 'a.txt' -Text $name
    Invoke-Git $repo add a.txt | Out-Null
    Invoke-Git $repo commit -q -m "the tip named $name" | Out-Null
    $shas += Get-GitText $repo rev-parse HEAD
}
$first = $shas[0]
$second = $shas[1]
$third = $shas[2]

$root = Join-Path $scratch 'camel-baseline'

# A baseline tree made the way the tools make it: a worktree of this repository, at the sha, on its
# own branch. Only HEAD matters to the decision, so the branch name here is free.
function New-BaselineTree {
    param([string] $Path, [string] $Sha)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    Invoke-Git $repo worktree add -q --detach $Path $Sha | Out-Null
}

# --- 1. the same sha twice is one tree ---------------------------------------
$planOne = Get-BaselineCheckout -Tip $first -Root $root
Check 'a tip with no tree yet is a build' ($planOne.Kind -eq 'fresh') "kind=$($planOne.Kind)"
Check 'and its path is named by the sha' ($planOne.Path -eq (Join-Path $root $first)) $planOne.Path
Check 'and its branch is named by the sha' ($planOne.Branch -eq "camel-baseline-$($first.Substring(0, 12))") $planOne.Branch
New-BaselineTree -Path $planOne.Path -Sha $first
$planAgain = Get-BaselineCheckout -Tip $first -Root $root
Check 'the same sha a second time is a reuse, not a rebuild' ($planAgain.Kind -eq 'tip') "kind=$($planAgain.Kind)"
Check 'and it is the same path' ($planAgain.Path -eq $planOne.Path) "$($planAgain.Path) vs $($planOne.Path)"
Check 'and it says nothing at all: there is nothing to rebuild' (@($planAgain.Lines).Count -eq 0) ($planAgain.Lines -join ' | ')

# --- 2. a different sha is a different tree ----------------------------------
$planOther = Get-BaselineCheckout -Tip $second -Root $root
Check 'a different sha is a different tree' ($planOther.Kind -eq 'fresh' -and $planOther.Path -ne $planOne.Path) "kind=$($planOther.Kind) path=$($planOther.Path)"
Check 'and its branch is the other sha, not the first tree branch' ($planOther.Branch -ne $planOne.Branch) "$($planOther.Branch) vs $($planOne.Branch)"
Check 'and the first tree is left exactly where it was' ((Get-GitText $planOne.Path rev-parse HEAD) -eq $first) (Get-GitText $planOne.Path rev-parse HEAD)

# --- 3. the legacy fixed path: checked against the tip before it is used -----
$legacy = Join-Path $scratch 'camel-baseline-legacy'
New-BaselineTree -Path $legacy -Sha $first
$legacyStale = Get-BaselineCheckout -Tip $second -LegacyPath $legacy -LegacyBranch 'camel-baseline'
Check 'a fixed-name tree holding another commit is stale' ($legacyStale.Kind -eq 'stale') "kind=$($legacyStale.Kind)"
Check 'and the line says so in the words the brief asked for' `
    (@($legacyStale.Lines).Count -gt 0 -and $legacyStale.Lines[0] -eq 'stale baseline beside the tip: rebuilding') ($legacyStale.Lines -join ' | ')
Check 'and it keeps the same path (it is moved, not replaced)' ($legacyStale.Path -eq $legacy) $legacyStale.Path
Check 'and the detail names the sha it held and the tip it would measure' `
    ($legacyStale.Lines -join ' ' -match $first.Substring(0, 9) -and $legacyStale.Lines -join ' ' -match $second.Substring(0, 9)) ($legacyStale.Lines -join ' | ')
Check 'and reading the decision moved nothing' ((Get-GitText $legacy rev-parse HEAD) -eq $first) (Get-GitText $legacy rev-parse HEAD)
$legacyFresh = Get-BaselineCheckout -Tip $first -LegacyPath (Join-Path $scratch 'camel-baseline-not-there') -LegacyBranch 'camel-baseline'
Check 'a fixed-name path that is not there yet is a build' ($legacyFresh.Kind -eq 'fresh') "kind=$($legacyFresh.Kind)"
$legacyTip = Get-BaselineCheckout -Tip $first -LegacyPath $legacy -LegacyBranch 'camel-baseline'
Check 'a fixed-name path that holds the tip is used as it stands' ($legacyTip.Kind -eq 'tip') "kind=$($legacyTip.Kind)"

# --- 4. nothing in the key is about who measured it --------------------------
# Two seats: different legacy trees, different legacy branches, and one shared root. The tip is the
# same, so the answer is the same tree on the same branch -- the key is the commit, not the seat.
$seatA = Get-BaselineCheckout -Tip $third -Root $root -LegacyPath (Join-Path $scratch 'seat-a') -LegacyBranch 'merge2-baseline'
$seatB = Get-BaselineCheckout -Tip $third -Root $root -LegacyPath (Join-Path $scratch 'seat-b') -LegacyBranch 'merge3b-baseline'
Check 'two seats resolving one tip get one tree' ($seatA.Path -eq $seatB.Path -and $seatA.Branch -eq $seatB.Branch) "$($seatA.Path) / $($seatB.Path)"
Check 'and the branch spells the sha and nothing else' `
    ($seatA.Branch -match "^camel-baseline-$($third.Substring(0, 12))$") $seatA.Branch
Check 'and no seat name survives in the key' `
    ($seatA.Path -notmatch 'seat|merge2|merge3b' -and $seatA.Branch -notmatch 'seat|merge2|merge3b') "$($seatA.Path) $($seatA.Branch)"

# --- 5. the wiring -----------------------------------------------------------
# A function nothing calls would leave the tools on the old shape and every check above green.
function Appears {
    param([string] $Path, [string] $Pattern)
    $text = Get-Content -LiteralPath $Path -Raw
    return [bool]($text -match $Pattern)
}
Check 'merge-lane.ps1 asks for the plan before the warm baseline' `
    (Appears $laneScript 'Get-BaselineCheckout -Tip \$originTarget') 'no Get-BaselineCheckout for the warm baseline'
Check 'merge-lane.ps1 asks for the plan before the seed' `
    (Appears $laneScript 'Get-BaselineCheckout -Tip \$developTip') 'no Get-BaselineCheckout -Tip $developTip call'
Check 'and both of its trees are checked out on the branch the plan names' `
    (Appears $laneScript 'BranchName \$warmPlan\.Branch' -and (Appears $laneScript 'BranchName \$baselinePlan\.Branch')) 'a caller still passes -BaselineBranch'
Check 'merge-batch.ps1 names its baseline tree by the sha when a root is set' `
    (Appears $batchScript 'Join-Path \$baselineRoot \$base') 'no <baselineRoot>\<sha> in the batch'
Check 'and its helper ref follows the sha, not the scratch root, in that case' `
    (Appears $batchScript 'probe/batch-base-\$\(\$base\.Substring\(0, 12\)\)') 'the batch ref is not sha-keyed'
Check 'and both tools read the readings from the repository common directory' `
    ((Appears $laneScript "--git-common-dir'\)\.Path 'camel-readings'|--git-common-dir'\)\.Path 'camel-readings'") -or
        (Appears $laneScript 'camel-readings')) 'merge-lane does not use a common-dir cache'
Check 'the batch reads them from the common directory too' (Appears $batchScript 'camel-readings') 'merge-batch does not use a common-dir cache'

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0
