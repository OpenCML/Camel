# The merge worktree comes from one place, and what the tool does with zero, one and two of them.
#
# Why it exists: `merge-lane.ps1` held the path as a constant while `scripts/hooks/pre-push` read it
# from `camel-gate.local.json`. On a machine with one merge position the two agreed by luck; with a
# second one the tool merged in a tree the hook would refuse to push from, and the refusal arrived
# after the merge had run. The reading is now shared, and the shapes it can meet are these.
#
# The three that matter are the answers to "which tree": one named tree is that tree, two named
# trees is a refusal (never a guess between them), and no name at all is a refusal that says what to
# write. A file that cannot be read is not an absent value either: it refuses rather than defaulting.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-config.test.ps1
#
# Everything here is local: it writes throwaway settings files and reads them back.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'merge-lane-config.ps1')

$script:failed = 0
$script:checked = 0

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

# The refusal's sentence, or '' when the call returned.
function Refusal([string]$Config, [string]$Explicit = '') {
    try {
        $null = Get-MergeWorktree -Config $Config -Explicit $Explicit
        return ''
    } catch {
        return $_.Exception.Message
    }
}

$dir = Join-Path ([System.IO.Path]::GetTempPath()) ("merge-lane-config-test-" + [guid]::NewGuid().ToString('n'))
New-Item -ItemType Directory -Force -Path $dir | Out-Null
try {
    $one = Join-Path $dir 'one.json'
    Set-Content -LiteralPath $one -Encoding utf8 -Value '{ "mergeWorktree": "D:\\Projects\\Camel-wt\\merge" }'
    $got = Get-MergeWorktree -Config $one
    Check 'one named tree is that tree' ($got -eq 'D:\Projects\Camel-wt\merge') "got '$got'"

    # The behaviour that must not change: with one entry, the tree is used as written, separators and
    # all. This is what every existing machine has.
    $oneForward = Join-Path $dir 'one-forward.json'
    Set-Content -LiteralPath $oneForward -Encoding utf8 -Value '{ "mergeWorktree": "D:/Projects/Camel-wt/merge/" }'
    $got = Get-MergeWorktree -Config $oneForward
    Check 'one named tree is returned verbatim' ($got -eq 'D:/Projects/Camel-wt/merge/') "got '$got'"

    $two = Join-Path $dir 'two.json'
    Set-Content -LiteralPath $two -Encoding utf8 -Value '{ "mergeWorktree": ["D:\\Projects\\Camel-wt\\merge", "D:\\Projects\\Camel-wt\\merge2"] }'
    $why = Refusal $two
    Check 'two named trees are refused, not guessed' ($why -ne '') 'the call returned a tree; picking one of two is the defect'
    Check 'the refusal names both trees' ($why -like '*merge2*' -and $why -like '*merge*') $why
    Check 'the refusal says how to choose' ($why -like '*-MainCheckout*') $why

    $semi = Join-Path $dir 'semi.json'
    Set-Content -LiteralPath $semi -Encoding utf8 -Value '{ "mergeWorktree": "D:\\a\\merge; D:\\a\\merge2" }'
    Check "a ';' list of two is refused the same way" ((Refusal $semi) -like '*2 merge worktrees*') (Refusal $semi)

    $blank = Join-Path $dir 'blank.json'
    Set-Content -LiteralPath $blank -Encoding utf8 -Value '{ "mergeWorktree": "  " }'
    Check 'a blank value names no tree and is refused' ((Refusal $blank) -ne '') 'a blank path is not a tree'

    $none = Join-Path $dir 'none.json'
    Set-Content -LiteralPath $none -Encoding utf8 -Value '{ "sshHost": "host" }'
    $why = Refusal $none
    Check 'a file without the key is refused, naming the key' ($why -like "*'mergeWorktree'*") $why

    $broken = Join-Path $dir 'broken.json'
    Set-Content -LiteralPath $broken -Encoding utf8 -Value '{ not json'
    $why = Refusal $broken
    Check 'a file that is not JSON is refused, not defaulted' ($why -like '*could not be read as JSON*') $why

    $absent = Join-Path $dir 'absent.json'
    $why = Refusal $absent
    Check 'a missing file is refused, naming it' ($why -like '*no settings file at*') $why

    # The one-off: a caller that has already decided is not asked again, and is not second-guessed
    # against the file -- that is what the pointer says, and the hook remains the last word.
    $got = Get-MergeWorktree -Config $two -Explicit 'D:\Projects\Camel-wt\merge'
    Check '-Explicit wins over a file that names two' ($got -eq 'D:\Projects\Camel-wt\merge') "got '$got'"
    $got = Get-MergeWorktree -Config $absent -Explicit 'D:\Projects\Camel-wt\merge'
    Check '-Explicit does not need the file' ($got -eq 'D:\Projects\Camel-wt\merge') "got '$got'"
} finally {
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failed -eq 0) {
    Write-Host "merge-lane-config: $($script:checked) checked, all pass"
    exit 0
}
Write-Host "merge-lane-config: $($script:checked) checked, $($script:failed) failed"
exit 1