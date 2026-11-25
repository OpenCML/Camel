# The one recovery a refused push may make (the night documentation landed on `integration` three
# times while a merge was being read, and each time the whole eleven-minute run was repeated).
#
# What is locked down here, and what decides it:
#
#   1. the boundary is the FILE PATH. Four places hold pages (internal/backlog.md, internal/plans/,
#      internal/reports/, internal/questions/) and everything else is code -- including another file
#      under `internal/`, because `internal/tools/merge-lane.ps1` is a file two plans read and a
#      prefix rule on `internal/` would call it documentation while every line of the output looked
#      right. There is no flag that says "this looks harmless", and a change of nothing is not
#      documentation either.
#   2. a docs-only refusal is recovered once: the new tip is merged into the merge commit, the plans
#      the landed files select are re-run on the re-landed tree, and the push is made again.
#   3. a refusal that touches code is not recovered at all: it stops and says so, leaving the full
#      re-run to whoever started the merge.
#   4. a second refusal stops with the two shas and nothing else. One recovery per run; the same
#      action a third time is not persistence.
#   5. every refusal prints the one `rebase:` line, and the line says what was re-run -- a reader of
#      the conclusion must not be able to take a partial re-run for a measurement of a layer.
#   6. documentation that selects no plan stops and NAMES the paths. "No plan is affected" and "no
#      rule was written" are different facts with the same shape, and only the second is repaired by
#      writing a rule; the selection widening to the fast layer would otherwise read as a green.
#
# The functions are read out of `merge-lane.ps1` itself (by name, from its syntax tree) rather than
# copied here, so this measures the file that ships: a function renamed or deleted there fails here
# instead of quietly going untested. The four push scenarios run against throwaway repositories with
# a bare origin beside them; the plan re-run is the one step that is handed in, because it starts the
# build host and this file must not.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-push.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-push.test.ps1 -Keep   (leave the fixtures)
#
# Everything here is local: no host, no build, no checkout of this repository is touched.

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
    Invoke-Git $Repo config user.name 'merge-lane push test' | Out-Null
    Invoke-Git $Repo config user.email 'merge-lane-push@example.invalid' | Out-Null
    Invoke-Git $Repo config commit.gpgsign false | Out-Null
}

# A landing commit: the shape of the thing this whole recovery exists for -- somebody pushes a page
# to the target branch while a merge is being read.
function Add-LandingCommit {
    param(
        [string] $Origin,
        [string] $Dir,
        [string] $Path,
        [string] $Text,
        [string] $Subject
    )
    if (-not (Test-Path -LiteralPath (Join-Path $Dir '.git'))) {
        & git clone --quiet $Origin $Dir 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "cloning the fixture origin to $Dir failed" }
        Initialize-FixtureRepo -Repo $Dir
    }
    Invoke-Git $Dir fetch --quiet origin | Out-Null
    Invoke-Git $Dir reset --quiet --hard origin/develop | Out-Null
    Write-FixtureFile -Repo $Dir -Name $Path -Text $Text
    Invoke-Git $Dir add --all | Out-Null
    Invoke-Git $Dir commit -q -m $Subject | Out-Null
    $sha = Get-GitText $Dir rev-parse HEAD
    & git -C $Dir push --quiet origin develop 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "pushing the landing commit $Subject failed" }
    return $sha
}

# One repository pair per scenario: a bare origin, a seat checkout with a lane merged into it and not
# pushed, and a second checkout that is the documentation writer.
function New-Scenario {
    param([string] $Name)
    $root = Join-Path $scratch $Name
    $origin = Join-Path $root 'origin.git'
    $work = Join-Path $root 'work'
    $writer = Join-Path $root 'writer'
    New-Item -ItemType Directory -Path $work -Force | Out-Null
    & git init --quiet --bare --initial-branch=develop $origin 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "git init --bare $origin failed" }
    & git init --quiet --initial-branch=develop $work 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "git init $work failed" }
    Initialize-FixtureRepo -Repo $work
    Write-FixtureFile -Repo $work -Name 'a.txt' -Text 'a'
    Invoke-Git $work add a.txt | Out-Null
    Invoke-Git $work commit -q -m 'the base the merge lands on' | Out-Null
    $base = Get-GitText $work rev-parse HEAD
    Invoke-Git $work remote add origin $origin | Out-Null
    & git -C $work push --quiet -u origin develop 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'pushing develop to the fixture origin failed' }

    # The lane, and the merge a seat made in its own checkout. Nothing is pushed: the merge is the
    # commit whose push will be refused.
    Invoke-Git $work checkout -q -B lane/probe $base | Out-Null
    Write-FixtureFile -Repo $work -Name 'probe.txt' -Text 'from the lane'
    Invoke-Git $work add probe.txt | Out-Null
    Invoke-Git $work commit -q -m 'probe: the lane change' | Out-Null
    $lane = Get-GitText $work rev-parse HEAD
    Invoke-Git $work checkout -q -B seat/merge $base | Out-Null
    Invoke-Git $work merge -q --no-ff lane/probe -m "merge probe: the lane change`n`nLane branch: lane/probe at $lane" | Out-Null
    $merge = Get-GitText $work rev-parse HEAD

    return [pscustomobject]@{
        Origin = $origin; Work = $work; Writer = $writer
        Base = $base; Lane = $lane; Merge = $merge
    }
}

function Get-OriginTip {
    param([string] $Origin)
    $out = & git --git-dir $Origin rev-parse 'refs/heads/develop'
    return "$out".Trim()
}

# --- the shipped functions, read out of the script ----------------------------
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$laneScript = Join-Path $repoRoot 'internal\tools\merge-lane.ps1'

# The shape a push must have lives in its own file, beside both merge tools (D-1124-78), and the
# script under test dot-sources it. Loading it here is what lets `Invoke-PushWithRecovery` run at
# all: a function that calls a name nothing defined would fail in every scenario below for a reason
# that is not the recovery's.
. (Join-Path $PSScriptRoot 'merge-push-shape.ps1')
Check 'merge-push-shape.ps1 defines Test-PushFirstParent' (@(Get-Command Test-PushFirstParent -ErrorAction SilentlyContinue).Count -eq 1)

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($laneScript, [ref] $tokens, [ref] $parseErrors)
Check 'merge-lane.ps1 parses' (@($parseErrors).Count -eq 0) (($parseErrors | ForEach-Object { "$($_.Extent.StartLineNumber): $($_.Message)" }) -join '; ')

$wanted = @(
    'Split-ChangeByPath',
    'Format-RebaseLine',
    'Format-RecoveryConclusion',
    'Test-PushRejection',
    'Get-PlansForChangedFiles',
    'Invoke-PlansOnTree',
    'Invoke-PushWithRecovery'
)
foreach ($name in $wanted) {
    $found = @($ast.FindAll({
                param($node)
                $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true))
    Check "the script still defines $name" ($found.Count -eq 1) "found $($found.Count) definition(s) of $name"
    if ($found.Count -eq 1) { Invoke-Expression $found[0].Extent.Text }
}

# --- 1. the boundary is the path ---------------------------------------------
# The four places a page is written are documentation; everything else is code. The pairs below are
# the ones that decide it -- the second of each pair is what a rule written on `internal/` or on the
# word "harmless" would get wrong.
$docs = Split-ChangeByPath -Paths @('internal/backlog.md', 'internal/plans/2026-11-24-x.cn.md', 'internal/reports/2026-11-24-y.cn.md', 'internal/questions/2026-11-24-z.cn.md')
Check 'the backlog, the plans, the reports and the questions are documentation' ($docs.DocsOnly -and $docs.Documentation.Count -eq 4) (($docs.Code -join ', '))
Check 'and nothing in that list is called code' ($docs.Code.Count -eq 0) ($docs.Code -join ', ')

$tool = Split-ChangeByPath -Paths @('internal/tools/merge-lane.ps1')
Check 'a file under internal/ that a plan reads is NOT documentation' (-not $tool.DocsOnly) (($tool.Documentation -join ', '))
Check 'and it is named as code rather than dropped' (($tool.Code -join ',') -eq 'internal/tools/merge-lane.ps1') ($tool.Code -join ',')

$nearMiss = Split-ChangeByPath -Paths @('internal/plans-archive/x.md', 'internal/backlog.md.bak', 'docs/README.md')
Check 'a path that merely starts like a documentation root is code' (-not $nearMiss.DocsOnly) (($nearMiss.Documentation -join ', '))
Check 'and so is a page the reader-facing docs rule owns' (-not ($nearMiss.Code -notcontains 'docs/README.md')) ($nearMiss.Code -join ',')

$mixed = Split-ChangeByPath -Paths @('internal/backlog.md', 'src/kernel/runtime/node.cpp')
Check 'one code file among documentation is not a docs-only change' (-not $mixed.DocsOnly) (($mixed.Code -join ','))
Check 'and the documentation half is still named' (($mixed.Documentation -join ',') -eq 'internal/backlog.md') ($mixed.Documentation -join ',')

$windowsPath = Split-ChangeByPath -Paths @('internal\reports\x.cn.md')
Check 'a path spelled with the other separator is the same path' ($windowsPath.DocsOnly) (($windowsPath.Code -join ', '))

$empty = Split-ChangeByPath -Paths @()
Check 'a change of nothing is not a docs-only change' (-not $empty.DocsOnly) "docs=$($empty.Documentation.Count) code=$($empty.Code.Count)"

# --- 2. the line every refusal prints ----------------------------------------
$line = Format-RebaseLine -OldRemote '0123456789abcdef0123456789abcdef01234567' -NewRemote 'fedcba9876543210fedcba9876543210fedcba98' -Files 3 -DocsOnly $true -ReRan 1
Check 'the docs-only line has the one shape' ($line -match '^rebase: 012345678 -> fedcba987, 3 file\(s\), docs-only, re-ran 1 plan\(s\)$') $line
$lineCode = Format-RebaseLine -OldRemote '0123456789abcdef0123456789abcdef01234567' -NewRemote 'fedcba9876543210fedcba9876543210fedcba98' -Files 2 -DocsOnly $false -ReRan 0
Check 'the code-touching line differs in the one word and the count' ($lineCode -match '^rebase: 012345678 -> fedcba987, 2 file\(s\), touches code, re-ran 0 plan\(s\)$') $lineCode

Check 'a non-fast-forward refusal is a rejection' (Test-PushRejection -Text ' ! [rejected]        HEAD -> develop (fetch first)') 'fetch first'
Check 'so is the whole output git prints for it' (Test-PushRejection -Text "! [rejected]        HEAD -> develop (non-fast-forward)`nerror: failed to push some refs to 'origin'`nhint: Updates were rejected because the tip of your current branch is behind") 'non-fast-forward'
Check 'a hook refusing the commit is not the tip moving' (-not (Test-PushRejection -Text ' ! [remote rejected] HEAD -> develop (pre-push hook declined)')) 'remote rejected'

# --- 3. the selection the recovery re-runs ------------------------------------
# The real `test/plan-selection.json`, the real runner: the files that land on the target branch have to
# select a plan, or the re-run is the whole fast layer wearing the name of a partial one.
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('merge-lane-push-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
Write-Host "scratch: $scratch"

$listFile = Join-Path $scratch 'selection.txt'
function Write-ListFile {
    param([string] $Path, [string[]] $Lines)
    [System.IO.File]::WriteAllText($Path, (($Lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
}
# Each of the four documentation places on its own, because "the one rule matches all four" and "the
# rule matches only some of them" are the same result when one path is the whole test -- the other
# three would then widen to the fast layer in a real recovery and never be measured as anything.
$docsPaths = @(
    'internal/backlog.md'
    'internal/plans/2026-11-24-one-plan.cn.md'
    'internal/reports/2026-11-24-one-page.cn.md'
    'internal/questions/2026-11-24-one-question.cn.md'
)
foreach ($docsPath in $docsPaths) {
    Write-ListFile -Path $listFile -Lines @($docsPath)
    $oneDoc = Get-PlansForChangedFiles -Checkout $repoRoot -ListFile $listFile
    Check "$docsPath selects the documentation check and nothing else" `
        ($oneDoc.Exit -eq 0 -and -not $oneDoc.Widened -and ($oneDoc.Targets -join ',') -eq 'test/plans/feat/docs_links.plan.toml') `
        "targets=[$($oneDoc.Targets -join ', ')] widened=$($oneDoc.Widened)"
}

Write-ListFile -Path $listFile -Lines @('internal/reports/2026-11-24-one-page.cn.md', 'internal/backlog.md')
$selection = Get-PlansForChangedFiles -Checkout $repoRoot -ListFile $listFile
Check 'the documentation that lands selects a plan' ($selection.Exit -eq 0 -and $selection.Targets.Count -eq 1) "exit $($selection.Exit): $($selection.Targets -join ', ')"
Check 'all four together are still one plan, not four' (($selection.Targets -join ',') -eq 'test/plans/feat/docs_links.plan.toml') ($selection.Targets -join ',')
Check 'and the selection did not widen to the layer' (-not $selection.Widened) $selection.Text

Write-ListFile -Path $listFile -Lines @('src/kernel/runtime/node.cpp')
$codeSelection = Get-PlansForChangedFiles -Checkout $repoRoot -ListFile $listFile
Check 'a change to the compiler selects more than the documentation check' ($codeSelection.Targets.Count -gt 1 -and -not $codeSelection.Widened) "targets: $($codeSelection.Targets.Count)"

Write-ListFile -Path $listFile -Lines @('pypi/setup.py')
$unruledSelection = Get-PlansForChangedFiles -Checkout $repoRoot -ListFile $listFile
# A rule-less class used to widen to the whole fast layer. It is now a refusal that names the file
# and the missing rule (exit 2), so the check is that refusal -- not a widening, and not a plan.
Check 'a class no rule names is refused by name, not widened into the fast layer' `
    ($unruledSelection.Exit -eq 2 -and -not $unruledSelection.Widened -and $unruledSelection.Targets.Count -eq 0 -and
        $unruledSelection.Text -match 'no rule in test/plan-selection\.json matches' -and
        $unruledSelection.Text -match 'pypi/setup\.py') $unruledSelection.Text

# --- 4. a docs-only refusal is recovered, once, and pushed --------------------
$a = New-Scenario -Name 'docs-only'
$landedOne = Add-LandingCommit -Origin $a.Origin -Dir $a.Writer -Path 'internal/reports/2026-11-24-one-page.cn.md' -Text 'one page' -Subject 'reports: one page'

$script:rerunCalls = 0
$script:rerunFiles = @()
$greenRerun = {
    param([string[]] $ChangedFiles)
    $script:rerunCalls++
    $script:rerunFiles = @($ChangedFiles)
    return @{ Exit = 0; Targets = @('test/plans/feat/docs_links.plan.toml'); Rows = 21; Reds = 0; Text = 'rows: 21' }
}
$pushA = Invoke-PushWithRecovery -Tree $a.Work -Target develop -ReRun $greenRerun
$linesA = @($pushA.Lines | Where-Object { $_ })
$rebaseA = @($linesA | Where-Object { "$_".StartsWith('rebase:') })

Check 'a docs-only refusal is recovered' ($pushA.Ok -and $pushA.Kind -eq 'recovered') "kind=$($pushA.Kind)"
Check 'and the run says one recovery happened' ($pushA.Recovered -and $pushA.Sha -ne $a.Merge) "sha=$($pushA.Sha) merge=$($a.Merge)"
Check 'it prints exactly one rebase line' ($rebaseA.Count -eq 1) ($rebaseA -join ' / ')
Check 'the rebase line names the two tips, the file count, the class and the plan count' `
    ($rebaseA.Count -eq 1 -and $rebaseA[0] -match "^rebase: $($a.Base.Substring(0,9)) -> $($landedOne.Substring(0,9)), 1 file\(s\), docs-only, re-ran 1 plan\(s\)$") ($rebaseA -join ' / ')
Check 'the plans were re-run on the file that landed' ($script:rerunCalls -eq 1 -and ($script:rerunFiles -join ',') -eq 'internal/reports/2026-11-24-one-page.cn.md') "calls=$($script:rerunCalls) files=$($script:rerunFiles -join ',')"

$originAfterA = Get-OriginTip -Origin $a.Origin
$headA = Get-GitText $a.Work rev-parse HEAD
Check 'the push landed: origin is this checkout HEAD' ($originAfterA -eq $headA) "origin=$originAfterA head=$headA"
Check 'the landed tip is in what was pushed' (Test-Path (Join-Path $a.Work 'internal/reports/2026-11-24-one-page.cn.md')) 'the page is not in the tree'
$parentsA = @((Get-GitText $a.Work rev-list --parents -n 1 HEAD) -split ' ' | Where-Object { $_ })
# The shape, not only the content: the re-landed commit is the prepared merge (what this run read)
# merged INTO the tip that landed, so its first parent is that tip. The other order -- the new tip
# merged into the merge commit -- is the shape that made a seat's merge read as an arrival on
# `integration` (D-1124-78), and it is what this check used to accept.
Check 'the re-land is the prepared merge merged into the new tip' `
    ($parentsA.Count -eq 3 -and $parentsA[1] -eq $landedOne -and $parentsA[2] -eq $a.Merge) ($parentsA -join ' ')
Check 'the lane is still in what went to origin' (Test-Path (Join-Path $a.Work 'probe.txt')) 'probe.txt is not in the tree'
# The green line of a recovered run, which is the one place a partial re-run can claim too much: it
# names the plans it re-ran, and it says that no layer was measured.
$conclusion = Format-RecoveryConclusion -Branch 'lane/probe' -Head $a.Lane -Target develop -Local $headA `
    -Targets $pushA.Targets -Files $pushA.Files.Count -MeasuredTree $a.Merge
Check 'the conclusion line names the plans re-run' ($conclusion -match 'partial re-run: 1 plan\(s\) -- test/plans/feat/docs_links\.plan\.toml --') $conclusion
Check 'and it says no layer was measured' ($conclusion -match 'and no layer was measured') $conclusion
Check 'and it names the tree the merge itself was measured on' ($conclusion -match "measured on tree $($a.Merge.Substring(0,9))") $conclusion

# --- 5. a refusal that touches code is not recovered --------------------------
$b = New-Scenario -Name 'touches-code'
$landedB = Add-LandingCommit -Origin $b.Origin -Dir $b.Writer -Path 'src/kernel/runtime/node.cpp' -Text 'int x;' -Subject 'kernel: a line of code'
$script:rerunCalls = 0
$pushB = Invoke-PushWithRecovery -Tree $b.Work -Target develop -ReRun $greenRerun
$linesB = @($pushB.Lines | Where-Object { $_ })
$rebaseB = @($linesB | Where-Object { "$_".StartsWith('rebase:') })

Check 'a refusal that touches code does not recover' ((-not $pushB.Ok) -and $pushB.Kind -eq 'touches code') "kind=$($pushB.Kind)"
Check 'and the rebase line says touches code with nothing re-run' `
    ($rebaseB.Count -eq 1 -and $rebaseB[0] -match ", 1 file\(s\), touches code, re-ran 0 plan\(s\)$") ($rebaseB -join ' / ')
Check 'the line names the file that is code' (($linesB -join "`n") -match 'src/kernel/runtime/node\.cpp') ($linesB -join "`n")
Check 'no plan was started' ($script:rerunCalls -eq 0) "calls=$($script:rerunCalls)"
Check 'nothing was pushed' ((Get-OriginTip -Origin $b.Origin) -eq $landedB) (Get-OriginTip -Origin $b.Origin)
Check 'and the merge commit was left where it was' ((Get-GitText $b.Work rev-parse HEAD) -eq $b.Merge) (Get-GitText $b.Work rev-parse HEAD)

# --- 6. a second refusal stops with the shas ---------------------------------
$c = New-Scenario -Name 'second-rejection'
$landedC1 = Add-LandingCommit -Origin $c.Origin -Dir $c.Writer -Path 'internal/backlog.md' -Text 'one row' -Subject 'collab: one row'
# The recovery runs between the two pushes, so this is where the branch moves the second time: a
# writer pushes another page while the re-landed tree is being read.
$lateRerun = {
    param([string[]] $ChangedFiles)
    $script:lateLanded = Add-LandingCommit -Origin $c.Origin -Dir $c.Writer -Path 'internal/backlog.md' -Text 'a second row' -Subject 'collab: a second row'
    return @{ Exit = 0; Targets = @('test/plans/feat/docs_links.plan.toml'); Rows = 21; Reds = 0; Text = 'rows: 21' }
}
$script:lateLanded = ''
$pushC = Invoke-PushWithRecovery -Tree $c.Work -Target develop -ReRun $lateRerun
$landedC2 = $script:lateLanded
$linesC = @($pushC.Lines | Where-Object { $_ })
$rebaseC = @($linesC | Where-Object { "$_".StartsWith('rebase:') })

Check 'a second refusal stops' ((-not $pushC.Ok) -and $pushC.Kind -eq 'second rejection') "kind=$($pushC.Kind)"
Check 'it prints one rebase line per refusal and no third' ($rebaseC.Count -eq 2) ($rebaseC -join ' / ')
Check 'the second line re-ran nothing' ($rebaseC.Count -eq 2 -and $rebaseC[1] -match ', docs-only, re-ran 0 plan\(s\)$') ($rebaseC -join ' / ')
Check 'and it reads from the first tip that landed to the second' `
    ($rebaseC.Count -eq 2 -and $rebaseC[1] -match "^rebase: $($landedC1.Substring(0,9)) -> $($landedC2.Substring(0,9)), ") ($rebaseC -join ' / ')
Check 'the stop reports the two shas' (($pushC.Shas -join ',') -eq "$landedC1,$landedC2") ($pushC.Shas -join ',')
Check 'nothing of this run reached origin' ((Get-OriginTip -Origin $c.Origin) -eq $landedC2) (Get-OriginTip -Origin $c.Origin)
Check 'and the re-landed commit is still on this checkout' ((Get-GitText $c.Work rev-parse HEAD) -ne $pushC.Shas[1]) (Get-GitText $c.Work rev-parse HEAD)

# --- 7. documentation landed but the selection names no plan -------------------
# "Which files does this select?" has two answers that must not look alike: no plan is affected, and
# no rule was written for these paths. The second one is the shape the four documentation paths had
# before test/plan-selection.json gained its rule, and it arrives as a widening -- the whole fast
# layer -- which is a full run wearing the name of a partial one. So the stop names the paths.
$d = New-Scenario -Name 'no-plan'
$landedD = Add-LandingCommit -Origin $d.Origin -Dir $d.Writer -Path 'internal/reports/2026-11-24-no-rule.cn.md' -Text 'a page' -Subject 'reports: a page no rule reads'
$script:rerunCalls = 0
$widenedRerun = {
    param([string[]] $ChangedFiles)
    $script:rerunCalls++
    return @{ Exit = 2; Targets = @(); Rows = 0; Reds = 0; Text = 'no rule matched; the selection is the whole fast layer' }
}
$pushD = Invoke-PushWithRecovery -Tree $d.Work -Target develop -ReRun $widenedRerun
$linesD = @($pushD.Lines | Where-Object { $_ })
$rebaseD = @($linesD | Where-Object { "$_".StartsWith('rebase:') })

Check 'a selection that names no plan does not push' ((-not $pushD.Ok) -and $pushD.Kind -eq 'no plan') "kind=$($pushD.Kind)"
Check 'it still prints its rebase line' ($rebaseD.Count -eq 1 -and $rebaseD[0] -match ', 1 file\(s\), docs-only, re-ran 0 plan\(s\)$') ($rebaseD -join ' / ')
Check 'and the stop names the paths that landed' (($linesD -join "`n") -match 'internal/reports/2026-11-24-no-rule\.cn\.md') ($linesD -join "`n")
Check 'and says no rule names them' (($linesD -join "`n") -match 'none of them is named by a rule') ($linesD -join "`n")
Check 'nothing was pushed' ((Get-OriginTip -Origin $d.Origin) -eq $landedD) (Get-OriginTip -Origin $d.Origin)

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0