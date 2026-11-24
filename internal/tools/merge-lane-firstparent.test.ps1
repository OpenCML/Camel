# The shape a commit must have before it is pushed to a target branch (D-1124-78), and the one
# regression that matters beside it: a merge of the right shape still pushes.
#
# Why this exists: the three seats of 2026-11-24 merged `integration` into the seat and then
# fast-forwarded the seat onto `integration`. Every push was a fast-forward, so nothing refused it,
# and the first-parent chain of `integration` came to hold four lane commits and four "merge
# integration into the slots seat" commits. Every tool that reads arrivals by first parent -- the
# promotion report, the arrival count -- read those four lane commits as four arrivals. The rule
# since then: every commit that enters the target is a MERGE whose first parent is the tip the target
# held at that moment.
#
# What is locked down here:
#
#   1. a merge whose first parent IS the tip -> `arrived`, and a push is allowed;
#   2. a merge whose first parent is NOT the tip -> refused, and the message says what the operator
#      was doing (merging the target into the seat) and the command that redoes it on the tip, with
#      `rerere` so the resolutions already made are reused;
#   3. a single-parent commit -> refused as "a lane commit went straight into integration";
#   4. the push path is unchanged when the shape is right: `Invoke-PushWithRecovery` still pushes,
#      origin ends on the commit that travelled, and no recovery is claimed.
#
# The reading is `Test-PushFirstParent` in `internal/tools/merge-push-shape.ps1`, which both merge
# tools dot-source and call before every push -- `merge-lane.ps1` twice (before the push and before
# the push a recovery makes), `merge-batch.ps1` once. The wiring is checked too: a function that
# ships and is never called would let both tools keep pushing the old shape.
#
# Everything here is local: throwaway repositories with a bare origin beside them, no host, no build,
# no checkout of this repository is touched.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-firstparent.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-firstparent.test.ps1 -Keep   (leave the fixtures)

[CmdletBinding()]
param(
    # Keep the fixture trees for inspection.
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

function Initialize-FixtureRepo {
    param([string] $Repo)
    Invoke-Git $Repo config user.name 'merge-lane first-parent test' | Out-Null
    Invoke-Git $Repo config user.email 'merge-lane-first-parent@example.invalid' | Out-Null
    Invoke-Git $Repo config commit.gpgsign false | Out-Null
}

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-first-parent-test-$PID")
New-Item -ItemType Directory -Force -Path $scratch | Out-Null

# One pair per case: a bare origin whose branch is `integration` (the branch this rule was written
# for), a seat checkout holding the merge, and a writer that lands pages on origin.
function New-Scenario {
    param([string] $Name)
    $root = Join-Path $scratch $Name
    $origin = Join-Path $root 'origin.git'
    $work = Join-Path $root 'work'
    $writer = Join-Path $root 'writer'
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    & git init --quiet --bare --initial-branch=integration $origin 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "git init --bare $origin failed" }
    & git init --quiet --initial-branch=integration $work 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "git init $work failed" }
    Initialize-FixtureRepo -Repo $work
    Write-FixtureFile -Repo $work -Name 'a.txt' -Text 'a'
    Invoke-Git $work add a.txt | Out-Null
    Invoke-Git $work commit -q -m 'the base integration holds' | Out-Null
    $base = Get-GitText $work rev-parse HEAD
    Invoke-Git $work remote add origin $origin | Out-Null
    & git -C $work push --quiet -u origin integration 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'pushing integration to the fixture origin failed' }

    Invoke-Git $work checkout -q -B lane/probe $base | Out-Null
    Write-FixtureFile -Repo $work -Name 'probe.txt' -Text 'from the lane'
    Invoke-Git $work add probe.txt | Out-Null
    Invoke-Git $work commit -q -m 'probe: the lane change' | Out-Null
    $lane = Get-GitText $work rev-parse HEAD

    # The seat: the lane merged with the tip as the first parent -- the shape the rule asks for.
    Invoke-Git $work checkout -q -B seat/merge $base | Out-Null
    Invoke-Git $work merge -q --no-ff lane/probe -m "merge probe: the lane change`n`nLane branch: lane/probe at $lane" | Out-Null
    $merge = Get-GitText $work rev-parse HEAD

    return [pscustomobject]@{
        Origin = $origin; Work = $work; Writer = $writer
        Base = $base; Lane = $lane; Merge = $merge
    }
}

function Get-OriginTip {
    param([string] $Origin, [string] $Branch = 'integration')
    $out = & git --git-dir $Origin rev-parse "refs/heads/$Branch"
    return "$out".Trim()
}

# A page landing on `integration` while the merge is being read: the shape the ruling is about.
function Add-LandingCommit {
    param([string] $Origin, [string] $Dir, [string] $Path, [string] $Text, [string] $Subject)
    if (-not (Test-Path -LiteralPath (Join-Path $Dir '.git'))) {
        & git clone --quiet $Origin $Dir 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "cloning the fixture origin to $Dir failed" }
        Initialize-FixtureRepo -Repo $Dir
    }
    Invoke-Git $Dir fetch --quiet origin | Out-Null
    Invoke-Git $Dir reset --quiet --hard origin/integration | Out-Null
    Write-FixtureFile -Repo $Dir -Name $Path -Text $Text
    Invoke-Git $Dir add --all | Out-Null
    Invoke-Git $Dir commit -q -m $Subject | Out-Null
    $sha = Get-GitText $Dir rev-parse HEAD
    & git -C $Dir push --quiet origin integration 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "pushing the landing commit $Subject failed" }
    return $sha
}

# --- the shipped reading, and the shipped push path it feeds -------------------
$shapeFile = Join-Path $PSScriptRoot 'merge-push-shape.ps1'
$laneScript = Join-Path $repoRoot 'internal\tools\merge-lane.ps1'
$batchScript = Join-Path $repoRoot 'internal\tools\merge-batch.ps1'

$tokens = $null
$parseErrors = $null
$shapeAst = [System.Management.Automation.Language.Parser]::ParseFile($shapeFile, [ref] $tokens, [ref] $parseErrors)
Check 'merge-push-shape.ps1 parses' (@($parseErrors).Count -eq 0) (($parseErrors | ForEach-Object { "$($_.Extent.StartLineNumber): $($_.Message)" }) -join '; ')
$definitions = @($shapeAst.FindAll({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-PushFirstParent'
        }, $true))
Check 'and defines Test-PushFirstParent exactly once' ($definitions.Count -eq 1) "found $($definitions.Count)"
if ($definitions.Count -eq 1) { Invoke-Expression $definitions[0].Extent.Text }

# The wiring: both merge tools call the reading, and neither carries a second copy of it. A function
# that ships and is called nowhere is the shape of a check that runs in a test and not in a push.
function Get-PushShapeCalls {
    param([string] $Path)
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref] $tokens, [ref] $errors)
    return @($ast.FindAll({
                param($node)
                $node -is [System.Management.Automation.Language.CommandAst] -and
                "$($node.GetCommandName())" -eq 'Test-PushFirstParent'
            }, $true))
}

$laneCalls = @(Get-PushShapeCalls -Path $laneScript)
$batchCalls = @(Get-PushShapeCalls -Path $batchScript)
Check 'merge-lane.ps1 calls the reading before each of its two pushes' ($laneCalls.Count -eq 2) "calls=$($laneCalls.Count)"
Check 'merge-batch.ps1 calls the reading before its push' ($batchCalls.Count -eq 1) "calls=$($batchCalls.Count)"
Check 'and neither tool defines its own copy of the rule' `
    (@($laneCalls + $batchCalls | Where-Object { $_.Extent.Text -match 'function Test-PushFirstParent' }).Count -eq 0) 'a copy of the rule was found in a tool'

# --- 1. a merge of the tip is what an arrival is -------------------------------
$p = New-Scenario -Name 'arrived'
$arrived = Test-PushFirstParent -Tree $p.Work -Target integration -Sha $p.Merge
Check 'a merge whose first parent is the tip arrives' ($arrived.Ok -and $arrived.Kind -eq 'arrived') "kind=$($arrived.Kind) $($arrived.Message -join ' | ')"
Check 'and the reading names the tip it was checked against' ($arrived.Tip -eq $p.Base -and $arrived.FirstParent -eq $p.Base) "tip=$($arrived.Tip) first=$($arrived.FirstParent)"
Check 'and nothing moved on origin while it was read' ((Get-OriginTip -Origin $p.Origin) -eq $p.Base) (Get-OriginTip -Origin $p.Origin)

# --- 2. the tip moved: the seat's merge is not an arrival on it ----------------
$landed = Add-LandingCommit -Origin $p.Origin -Dir $p.Writer -Path 'internal/reports/2026-11-24-a-page.cn.md' -Text 'a page' -Subject 'reports: a page arrived'
$stale = Test-PushFirstParent -Tree $p.Work -Target integration -Sha $p.Merge
$staleText = @($stale.Message) -join "`n"
Check 'a merge whose first parent is no longer the tip is refused' ((-not $stale.Ok) -and $stale.Kind -eq 'first parent') "kind=$($stale.Kind)"
Check 'the refusal names both ends of the mismatch' `
    ($staleText -match "first parent is $($p.Base.Substring(0,9))" -and $staleText -match "origin/integration is $($landed.Substring(0,9))") $staleText
Check 'and it says the operator merged integration into the seat, not the lane into integration' `
    ($staleText -match '你在把 integration 合进席位，而不是把车道合进 integration') $staleText
Check 'and it gives the command that redoes it on the tip' `
    ($staleText -match 'checkout -B tmp origin/integration' -and $staleText -match 'merge --no-ff') $staleText
Check 'and it points at rerere for the resolutions already made' ($staleText -match 'rerere\.enabled true') $staleText
Check 'and it says nothing was pushed' ($staleText -match 'Nothing was pushed') $staleText
Check 'reading it does not push' ((Get-OriginTip -Origin $p.Origin) -eq $landed) (Get-OriginTip -Origin $p.Origin)

# --- 3. a lane commit with one parent is not a merge at all --------------------
$single = Test-PushFirstParent -Tree $p.Work -Target integration -Sha $p.Lane
$singleText = @($single.Message) -join "`n"
Check 'a single-parent commit is refused' ((-not $single.Ok) -and $single.Kind -eq 'single parent') "kind=$($single.Kind)"
Check 'and the refusal says a lane commit went straight in' ($singleText -match '一笔车道提交直接进了 integration') $singleText
Check 'and it gives the same redo shape' ($singleText -match 'checkout -B tmp origin/integration' -and $singleText -match 'merge --no-ff') $singleText
Check 'and reading it does not push either' ((Get-OriginTip -Origin $p.Origin) -eq $landed) (Get-OriginTip -Origin $p.Origin)

# --- 4. the push path itself is unchanged when the shape is right --------------
# The regression the whole check could break: `Invoke-PushWithRecovery` is the shipped push, and with
# a shape it accepts it must still push in one attempt, claim no recovery, and leave origin on the
# commit that travelled.
$tokens = $null
$parseErrors = $null
$laneAst = [System.Management.Automation.Language.Parser]::ParseFile($laneScript, [ref] $tokens, [ref] $parseErrors)
Check 'merge-lane.ps1 parses' (@($parseErrors).Count -eq 0) (($parseErrors | ForEach-Object { "$($_.Extent.StartLineNumber): $($_.Message)" }) -join '; ')
foreach ($name in @('Split-ChangeByPath', 'Format-RebaseLine', 'Test-PushRejection', 'Invoke-PushWithRecovery')) {
    $found = @($laneAst.FindAll({
                param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true))
    Check "merge-lane.ps1 still defines $name" ($found.Count -eq 1) "found $($found.Count)"
    if ($found.Count -eq 1) { Invoke-Expression $found[0].Extent.Text }
}

$q = New-Scenario -Name 'regression'
$push = Invoke-PushWithRecovery -Tree $q.Work -Target integration -ReRun {
    param([string[]] $ChangedFiles)
    return @{ Exit = 0; Targets = @(); Rows = 0; Reds = 0; Text = 'this scenario must not re-run anything' }
}
$pushLines = @($push.Lines | Where-Object { $_ })
Check 'the shipped push still pushes a merge of the right shape' ($push.Ok -and $push.Kind -eq 'pushed') "kind=$($push.Kind) $($pushLines -join ' | ')"
Check 'and it claims no recovery' (-not $push.Recovered) "recovered=$($push.Recovered)"
Check 'and origin is the commit that travelled' ((Get-OriginTip -Origin $q.Origin) -eq $q.Merge) "origin=$(Get-OriginTip -Origin $q.Origin) merge=$($q.Merge)"
Check 'and nothing was re-landed on top of it' ((Get-GitText $q.Work rev-parse HEAD) -eq $q.Merge) (Get-GitText $q.Work rev-parse HEAD)

# The same shipped push, handed a lane commit: the refusal is the tool's, not only the reading's.
Invoke-Git $q.Work checkout -q -B lane/probe $q.Lane | Out-Null
$notAMerge = Invoke-PushWithRecovery -Tree $q.Work -Target integration -ReRun {
    param([string[]] $ChangedFiles)
    return @{ Exit = 0; Targets = @(); Rows = 0; Reds = 0; Text = 'this scenario must not re-run anything' }
}
Check 'the shipped push refuses a single-parent commit by name' ((-not $notAMerge.Ok) -and $notAMerge.Kind -eq 'not a merge') "kind=$($notAMerge.Kind)"
Check 'and origin is still where it was' ((Get-OriginTip -Origin $q.Origin) -eq $q.Merge) (Get-OriginTip -Origin $q.Origin)

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0