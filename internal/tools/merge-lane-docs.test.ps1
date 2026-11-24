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

Write-Host ''
Write-Host "$script:checked checks, $script:failed failed"
if ($script:failed -gt 0) { exit 1 }
exit 0