# What `docs-only` means, checked as a list rather than as an opinion.
#
# The classification is the whole judgement in `merge-lane`'s docs-only branch: if it says a `.ps1` is
# documentation, a merge that changes a gate script is pushed unmeasured. So it is one function, in its
# own file, tested here without a merge, a host or a build.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-docs.test.ps1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$script:checked = 0
$script:failed = 0

function Check {
    param([string]$Name, [bool]$Ok, [string]$Detail = '')
    $script:checked++
    if ($Ok) { Write-Host "  PASS  $Name" }
    else { $script:failed++; Write-Host "  FAIL  $Name"; if ($Detail) { Write-Host "        $Detail" } }
}

. (Join-Path $PSScriptRoot 'merge-lane-docs.ps1')

function Check-Paths {
    param([string]$Name, [string[]]$Files, [bool]$Expected)
    $got = Test-DocsOnlyMerge -Files $Files
    Check $Name ($got -eq $Expected) "expected $Expected, got $got for: $($Files -join ', ')"
}

Check-Paths 'a question page alone is documentation' @('internal/questions/2026-11-24-x.cn.md') $true
Check-Paths 'a report alone is documentation' @('internal/reports/2026-11-24-x.md') $true
Check-Paths 'the backlog alone is documentation' @('internal/backlog.md') $true
Check-Paths 'a page under docs/ is documentation' @('docs/language/04_execution_model.md') $true
Check-Paths 'a plan page under internal/plans/ is documentation' @('internal/plans/2026-11-24-x.cn.md') $true
Check-Paths 'a collab page is documentation' @('internal/collab/rules.md') $true
Check-Paths 'several documentation files are documentation' @('docs/a.md', 'internal/backlog.md', 'internal/questions/b.md') $true

Check-Paths 'a PowerShell tool is not documentation' @('internal/tools/merge-lane.ps1') $false
Check-Paths 'a node script is not documentation' @('scripts/test.js') $false
Check-Paths 'a plan file is not documentation' @('test/plans/feat/docs_links.plan.toml') $false
Check-Paths 'a source file is not documentation' @('src/core/graph.cpp') $false
Check-Paths 'a settings file is not documentation' @('internal/tools/camel-gate.local.json') $false
Check-Paths 'a documentation file beside a tool is not documentation' @('internal/questions/a.md', 'internal/tools/merge-lane.ps1') $false
Check-Paths 'a documentation file beside a node script is not documentation' @('docs/a.md', 'scripts/test.js') $false
Check-Paths 'a tool inside a documentation root is not documentation' @('internal/questions/run.ps1') $false
Check-Paths 'an empty change is not a documentation-only change' @() $false

# Backslash and leading `./` spellings come from different callers; one path, one answer.
Check 'a Windows-spelled documentation path is documentation' (Test-DocsOnlyPath -Path 'internal\questions\x.cn.md')
Check 'a ./ prefixed documentation path is documentation' (Test-DocsOnlyPath -Path './docs/a.md')
Check 'a Windows-spelled tool path is not documentation' (-not (Test-DocsOnlyPath -Path 'internal\tools\x.ps1'))

# --- the selection verdict: exit 2 has two causes that mean opposite things ---------------
# The marker is the runner's sentence, not a copy of it: the test reads `test/runner.mjs` and checks
# that the sentence this classifier matches is still printed there. A test that restated the string
# would keep passing after the message changed, which is exactly how the `$selectExit -eq 0`
# conjunction survived: both halves were individually plausible and the pair was never exercised.
$runnerSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot '..\..\test\runner.mjs') -Raw
# The runner builds that sentence across a string concatenation (`... must not ` + `'become ...'`),
# so the source is joined the way JavaScript joins it before the sentence is looked for. A check
# against the raw file would fail for the line break and say nothing about the message.
$runnerFlat = $runnerSource -replace '`\s*\+\s*\r?\n\s*''', ''
Check 'the no-rule marker is still the sentence test/runner.mjs prints' `
    $runnerFlat.Contains($script:NoRuleRefusal)

$noRule = @(
    '[ERROR] --changed: no rule in test/plan-selection.json matches the 1 changed file(s): internal/questions/x.cn.md, so the selection is empty; an empty selection must not become a plan expansion',
    '[ERROR] --changed: the plans this change touches are unknown, so none is selected and this run fails.'
)
$cannotReadBase = @("[ERROR] --changed: cannot read origin/develop (fatal: bad revision 'origin/develop')")
$cannotReadRules = @('[ERROR] --changed: cannot read the selection rules test/plan-selection.json (Unexpected token in JSON)')

function Check-Verdict {
    param([string]$Name, [string]$Expected, [hashtable]$Arguments)
    $got = Get-SelectionVerdict @Arguments
    Check $Name ($got -eq $Expected) "expected $Expected, got $got"
}

Check-Verdict 'a no-rule refusal whose every path is documentation is docs-only' 'docs-only-refused' `
    @{ SelectExit = 2; SelectOutput = $noRule; ChangedFiles = @('internal/questions/x.cn.md'); PlanCount = 0 }
Check-Verdict 'a no-rule refusal with one tool in the change is not docs-only' 'no-plan' `
    @{ SelectExit = 2; SelectOutput = $noRule; ChangedFiles = @('internal/questions/x.cn.md', 'internal/tools/merge-lane.ps1'); PlanCount = 0 }
Check-Verdict 'an unreadable base is not docs-only, whatever the paths are' 'unreadable' `
    @{ SelectExit = 2; SelectOutput = $cannotReadBase; ChangedFiles = @('docs/a.md'); PlanCount = 0 }
Check-Verdict 'selection rules that do not parse are not docs-only' 'unreadable' `
    @{ SelectExit = 2; SelectOutput = $cannotReadRules; ChangedFiles = @('docs/a.md'); PlanCount = 0 }
Check-Verdict 'an exit that is neither 0 nor 2 is not documentation' 'unreadable' `
    @{ SelectExit = 1; SelectOutput = $noRule; ChangedFiles = @('docs/a.md'); PlanCount = 0 }
Check-Verdict 'the listed-empty path stays docs-only' 'docs-only-listed' `
    @{ SelectExit = 0; SelectOutput = @(); ChangedFiles = @('internal/backlog.md'); PlanCount = 0 }
Check-Verdict 'a selection that names plans is read by the rows stage' 'plans' `
    @{ SelectExit = 0; SelectOutput = @('[INFO] --changed: 2 changed file(s) against a select 1 plan(s)'); ChangedFiles = @('src/a.cpp'); PlanCount = 1 }
Check-Verdict 'an empty change with no plan named is not docs-only' 'no-plan' `
    @{ SelectExit = 0; SelectOutput = @(); ChangedFiles = @(); PlanCount = 0 }

Write-Host ''
Write-Host "$script:checked checks, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0