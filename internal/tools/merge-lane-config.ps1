# Which tree the merge seat is, read from the same settings file the pre-push hook reads.
#
# `scripts/hooks/pre-push` refuses to push `develop` from any tree `camel-gate.local.json` does not
# name, and `merge-lane.ps1` merges in the tree that file names. While each held its own copy of
# that fact -- a constant in the tool, a setting in the hook -- a machine with a second merge
# position made them disagree, and the disagreement surfaced as a refused push *after* a merge had
# already run: the expensive half of the work done, the result told to go back.
#
# There is no fallback by design. A missing file, a missing key, or a list of two is a refusal that
# names the file and the key, never a default: picking one of two merge positions is the failure
# being removed, so this asks instead of choosing. `-Explicit` is for a caller that has already
# decided -- a one-off run -- and it wins over the file.

function Get-MergeWorktree {
    param(
        # `internal/tools/camel-gate.local.json`, git-ignored, holding this machine's values.
        [Parameter(Mandatory = $true)][string]$Config,
        # A tree named on the command line. Empty means "ask the settings file".
        [string]$Explicit = ''
    )
    if ($Explicit) { return $Explicit }
    if (-not (Test-Path -LiteralPath $Config)) {
        throw "no settings file at $Config; it is git-ignored and holds this machine's 'mergeWorktree'"
    }
    try {
        $doc = Get-Content -Raw -LiteralPath $Config | ConvertFrom-Json
    } catch {
        throw "$Config could not be read as JSON ($($_.Exception.Message)); an unreadable setting is not an absent one"
    }
    if (-not ($doc.PSObject.Properties['mergeWorktree'])) {
        throw "$Config has no 'mergeWorktree'; leaving it out means no tree may push develop, so it names none"
    }
    # The same two shapes the hook accepts: a JSON array, or one string with ';' between the paths.
    $list = @()
    foreach ($one in @($doc.mergeWorktree)) {
        foreach ($part in ("$one" -split ';')) {
            $trimmed = $part.Trim()
            if ($trimmed) { $list += $trimmed }
        }
    }
    if ($list.Count -eq 0) {
        throw "$Config names no merge worktree under 'mergeWorktree'"
    }
    if ($list.Count -gt 1) {
        throw ("$Config names $($list.Count) merge worktrees ($($list -join '; ')); this tool merges in one " +
            'tree and will not pick one of them, so name the tree with -MainCheckout')
    }
    return $list[0]
}