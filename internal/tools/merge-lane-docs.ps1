# What "a merge with nothing to measure" means, in one place.
#
# A merge whose every changed file is documentation selects no plan: `test/plan-selection.json` has no
# rule for `internal/questions/`, and it does not want one -- there is no behavioural plan to name for
# a file nothing executes. But `--changed` answers that with an empty selection, and an empty
# selection has been exit 2 ("could not measure") since a run that selects nothing was found to be
# indistinguishable from a run that learned nothing. The two are not the same fact:
#
#   * `--changed` named no plan and the change is not documentation -- the selection is wrong, the
#     merge is not measured, and exit 2 is correct;
#   * `--changed` named no plan because the change IS documentation -- there was nothing to measure,
#     and that is a conclusion a merge can be allowed to reach.
#
# The list below is the whole of what "documentation" means here, and it is deliberately a list and
# not a pattern: a file that is added to it has to be a file whose change no plan can turn red on.
# `*.md` is included because a markdown file is read by the docs plans' walkers when it is one of
# theirs and by nothing when it is not; `.ps1`, `.mjs`, `.js`, `.json`, `.toml`, `.cml`, `.cpp` are not
# included, and a merge that mixes one of those with documentation stays exit 2.

$script:DocsOnlyRoots = @(
    'docs/',
    'internal/questions/',
    'internal/reports/',
    'internal/collab/',
    'internal/plans/',
    'internal/backlog.md'
)

# A file under a documentation root is documentation only if it is not something that executes or that
# a plan reads as config. `internal/questions/run.ps1` sits under a documentation root and is a script:
# the root says where a file lives, the extension says what it is, and the extension wins -- otherwise a
# tool placed beside a page would be pushed unmeasured because of its folder.
$script:CodeExtensions = @(
    '.ps1', '.psm1', '.mjs', '.js', '.cjs', '.json', '.toml', '.cml', '.cpp', '.cc', '.cxx',
    '.h', '.hpp', '.py', '.sh', '.bat', '.cmd', '.yml', '.yaml'
)

function Test-DocsOnlyPath {
    param([string] $Path)
    $p = "$Path".Trim().TrimStart('./').Replace('\', '/')
    if (-not $p) { return $false }
    $ext = [System.IO.Path]::GetExtension($p).ToLowerInvariant()
    if ($script:CodeExtensions -contains $ext) { return $false }
    if ($ext -eq '.md') { return $true }
    foreach ($root in $script:DocsOnlyRoots) {
        if ($p -eq $root -or $p.StartsWith($root)) { return $true }
    }
    return $false
}

# Every file, not most of them: one non-documentation file in the change is a file a plan could turn
# red on, and a rule that asked "are most of these documentation" would be a rule that measures a merge
# by its majority.
function Test-DocsOnlyMerge {
    param([string[]] $Files)
    $list = @($Files | Where-Object { "$_".Trim() })
    if ($list.Count -eq 0) { return $false }
    foreach ($file in $list) {
        if (-not (Test-DocsOnlyPath -Path $file)) { return $false }
    }
    return $true
}