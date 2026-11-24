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

# --- what a selection's exit code and output mean for a merge -------------------
#
# The judgement used to be one line inside `merge-lane`:
#
#     $docsOnly = ($selectExit -eq 0 -and $planTargets.Count -eq 0) -and (Test-DocsOnlyMerge ...)
#
# Two clauses that cannot both hold. An empty selection is exit 2 by the runner's own rule --
# `test/runner.mjs#selectChangedTargets` refuses it instead of widening to the layer, and
# `test/plan-selection.json` states the same sentence in its own comment, "A change no rule matches is
# refused (exit 2)" -- so `$selectExit -eq 0` was false in exactly the case the branch was written
# for. A documentation-only merge therefore reached the "could not be selected" stop: `fsh-dev` on
# 2026-11-24 hit it twice in one evening, first through `camel-remote -Changed` and then through this
# tool, and both times the change was one question page and no plan could have read it.
#
# The exit code alone cannot answer it, and neither can the path list: exit 2 has two causes that mean
# opposite things, and they are told apart by the sentence the runner prints rather than by assuming
# the same number means the same thing.
#
#   * the no-rule refusal says the selection rules named nothing for *these paths*. If every changed
#     path is documentation, that is a fact about the change: no plan covers a page, and the merge has
#     nothing to measure. This is `docs-only` with the selection stage having refused, and the merge
#     records and advances on it (a merge that was never meant to be measured must not be refused as
#     if the measuring had gone wrong);
#   * every other exit 2 -- a base that does not resolve, selection rules that do not parse, a plan the
#     rules name that the tree does not hold -- says the change itself is unknown. That is the gate
#     being blind, and no path list turns it into a documentation merge. This is `unreadable`, and it
#     is refused whatever the extensions say.
#
# The sentence below is the runner's, verbatim (`test/runner.mjs`, `refuseChanged(...)` in
# `selectChangedTargets`); the test beside this file checks that the runner still prints it, so the
# marker cannot drift away from the message it is matching.
$script:NoRuleRefusal = 'so the selection is empty; an empty selection must not become a plan expansion'

function Test-NoRuleRefusal {
    param([string[]] $Output = @())
    $text = ($Output | ForEach-Object { "$_" }) -join "`n"
    return $text.Contains($script:NoRuleRefusal)
}

# The four verdicts, and what each licenses:
#
#   docs-only-listed    exit 0, no plan named, every path documentation -- the listed-empty change.
#   docs-only-refused   exit 2, the no-rule refusal, every path documentation -- the case the old
#                       conjunction could not reach.
#   plans               exit 0 and at least one plan named: the rows stage reads them.
#   no-plan             the selection answered and named nothing, and at least one path is not
#                       documentation (or the change is empty): a hole in the rules, not a reading.
#   unreadable          exit 2 that is not the no-rule refusal: base/rules/list unknown. Never
#                       documentation-only, whatever the path list looks like.
function Get-SelectionVerdict {
    param(
        [int] $SelectExit,
        [string[]] $SelectOutput = @(),
        [string[]] $ChangedFiles = @(),
        [int] $PlanCount = 0
    )
    if ($SelectExit -eq 0) {
        if ($PlanCount -gt 0) { return 'plans' }
        if (Test-DocsOnlyMerge -Files $ChangedFiles) { return 'docs-only-listed' }
        return 'no-plan'
    }
    if ($SelectExit -eq 2 -and (Test-NoRuleRefusal -Output $SelectOutput)) {
        if (Test-DocsOnlyMerge -Files $ChangedFiles) { return 'docs-only-refused' }
        return 'no-plan'
    }
    return 'unreadable'
}