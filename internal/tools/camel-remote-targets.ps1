# What a `-Plan` target names, printed before anything is sent to the host.
#
# `-Plan` takes a plan file or a plan directory, and the two are not the same question: a plan file
# named as a target runs in full whatever `--tier` says, while a directory is read *at* the layer
# (`test/tools/run-tests.mjs#selectTier`). Neither fact was in the run's opening lines, so the first
# thing a reader saw was the build; the only line that said what was selected came from the runner,
# after the build, as `[tier] fast: 19 plan file(s), 217 declared test entries, 217 started` -- a
# number with no statement of what it was a number *of*. Two readings of that shape were taken on
# 2026-11-24: `-Plan <two directories>` was reported as the whole fast layer (2,763 cases / 113 plan
# files), and `-Plan test/plans` (the whole tree) exited 2 with a sentence about `--changed`, a flag
# the caller had not used.
#
# So a target is resolved here, on this side, before the push: what it names, how many plan files it
# reaches, and what the run does with them. The one rule this file keeps is that the sentence is
# about *this run's target*: it never mentions `--changed` unless the caller gave it, because a
# target refusal that names a flag nobody used is the same defect at one remove.
#
# `N plan file(s)` counts `*.plan.toml` under the target -- the unit the gate's own `-Changed`
# refusal line already counts (`test/plans/smoke` + `test/plans/feat`) and the unit a reader can
# check with `git ls-files`. It is deliberately not a case count: cases are selected by the layer,
# and re-deriving them here would be a second implementation of the layer rule.

# One record per value, in the order given:
#   Kind   'file' | 'directory' | 'tree' | 'missing'
#   Name   the value as the caller wrote it (for the sentence)
#   Full   the resolved path, `/`-separated and without a trailing separator
#   Plans  plan files the target reaches (a file counts 1, a missing path 0)
function Resolve-CamelPlanTarget {
    param(
        [AllowEmptyCollection()][string[]]$Values = @(),
        [string]$Root = '.'
    )
    $rootFull = if ($Root) { (Resolve-Path -LiteralPath $Root).Path } else { (Get-Location).Path }
    $plansRoot = ((Join-Path $rootFull 'test/plans') -replace '\\', '/').TrimEnd('/')
    $resolved = @()
    foreach ($value in @($Values)) {
        $name = "$value".Trim()
        if (-not $name) { continue }
        # `all` is the shorthand the runner reads as the plan tree (`test/runner.mjs#TARGETS`).
        $full = if ($name -eq 'all') {
            $plansRoot
        } elseif ([System.IO.Path]::IsPathRooted($name)) {
            $name
        } else {
            Join-Path $rootFull $name
        }
        $full = ($full -replace '\\', '/').TrimEnd('/')
        $kind = 'missing'
        $count = 0
        if (Test-Path -LiteralPath $full -PathType Leaf) {
            $kind = 'file'
            $count = 1
        } elseif (Test-Path -LiteralPath $full -PathType Container) {
            # A directory that *is* the plans root is not a subset of the tree: it is the layer
            # question, and a run that says "the whole tree" while starting one layer is the two
            # readings this file exists to keep apart.
            $kind = if ($full -eq $plansRoot) { 'tree' } else { 'directory' }
            $count = @(Get-ChildItem -LiteralPath $full -Recurse -File -Filter '*.plan.toml' -ErrorAction SilentlyContinue).Count
        }
        $resolved += [pscustomobject]@{
            Kind  = $kind
            Name  = $name
            Full  = $full
            Plans = $count
        }
    }
    return $resolved
}

# The one line about the plan selection, printed before the push. It states the three things a
# reader needs in order to compare it with the `[SUMMARY]` at the end: which targets, how many plan
# files they reach, and what the run will do with them.
function Format-CamelPlanTargetLine {
    param(
        [AllowEmptyCollection()]$Targets = @(),
        [string]$Tier = '',
        # `-UpdateGolden` rewrites the goldens of the named plans and reads no layer, so the layer
        # sentence would be wrong there.
        [switch]$Golden
    )
    $targets = @($Targets)
    if ($targets.Count -eq 0) { return $null }
    $tierName = if ("$Tier".Trim()) { $Tier.Trim() } else { 'fast (the runner default)' }
    $layer = if ($Golden) { 'the layer does not filter a recording' } else { "at tier $tierName" }
    $files = @($targets | Where-Object { $_.Kind -eq 'file' })
    $dirs = @($targets | Where-Object { $_.Kind -eq 'directory' })
    $tree = @($targets | Where-Object { $_.Kind -eq 'tree' })
    $missing = @($targets | Where-Object { $_.Kind -eq 'missing' })
    $parts = @()

    if ($files.Count -gt 0) {
        $names = ($files | ForEach-Object { $_.Name }) -join ', '
        $count = if ($files.Count -eq 1) { '1 plan file' } else { "$($files.Count) plan files" }
        $parts += "$count ($names): a plan file named as a target runs in full, whatever -Tier says"
    }
    if ($dirs.Count -gt 0) {
        $names = ($dirs | ForEach-Object { $_.Name }) -join ', '
        $plans = ($dirs | Measure-Object -Property Plans -Sum).Sum
        $word = if ($dirs.Count -eq 1) { 'directory' } else { 'directories' }
        $under = if ($dirs.Count -eq 1) { 'it' } else { 'those directories' }
        $parts += "you named $($dirs.Count) $word ($names): this run starts the plans under $under and " +
            "nothing else -- $plans plan file(s) $layer, a directory being read at the layer; it is not " +
            'a run of the whole plan tree'
    }
    if ($tree.Count -gt 0) {
        $names = ($tree | ForEach-Object { $_.Name }) -join ', '
        $plans = ($tree | Measure-Object -Property Plans -Sum).Sum
        $parts += "$names is the whole plan tree ($plans plan file(s)), not a directory subset: " +
            "a whole-tree target is a layer question -- it runs the plans of the layer -Tier names $layer"
    }
    if ($missing.Count -gt 0) {
        $names = ($missing | ForEach-Object { $_.Name }) -join ', '
        $parts += "$names names nothing in this tree (no such file or directory), so the runner will refuse it"
    }
    return "[camel-remote] plan target: " + ($parts -join '; ')
}

# Why a whole-tree target is refused before the push, as the lines to print. Empty means it may run.
#
# The tree is a layer question, and the runner answers it with `fast` when nothing names a layer
# (`test/tools/run-tests.mjs#selectTier`), so `-Plan test/plans` read as "the whole tree" in the
# command and as "the fast layer" in the output -- measured on 2026-11-24 as
# `[tier] fast: 387 plan file(s), 9919 declared test entries, 9919 started`, with nothing in the run
# saying one layer had been chosen for it. A run that measured the wrong layer cannot be told from a
# complete one afterwards, so the target is refused here instead: one word (`-Tier`) says what was
# meant, and this refusal costs no transfer, no build and no slot.
#
# The wording matters as much as the refusal. The reading that motivated this arrived as
# `[ERROR] --changed: the cases this change selects could not be computed` -- a sentence about a
# flag the caller had not given, which sends a reader to look at the change selection instead of at
# the target they typed. Every line here is about the target, and the last one says so.
function Get-CamelPlanTargetRefusal {
    param(
        [AllowEmptyCollection()]$Targets = @(),
        [string]$Tier = '',
        # `-UpdateGolden` rewrites the goldens of the plans it is given and reads no layer, so a
        # whole tree is a legitimate recording there and there is no layer to name.
        [switch]$Golden
    )
    $tree = @(@($Targets) | Where-Object { $_.Kind -eq 'tree' })
    if ($tree.Count -eq 0) { return @() }
    if ($Golden) { return @() }
    if ("$Tier".Trim()) { return @() }
    $names = ($tree | ForEach-Object { $_.Name }) -join ', '
    $plans = ($tree | Measure-Object -Property Plans -Sum).Sum
    return @(
        "[camel-remote] -Plan $names names the whole plan tree ($plans plan file(s)), and no -Tier says which layer of it: the whole tree is a layer question (fast|slow|milestone|all), and the runner quietly answers 'fast' when nothing names one, so the run would read as a complete reading of the tree while covering one layer.",
        '[camel-remote] name the layer with -Tier fast|slow|milestone|all, or name the plans you mean; nothing was sent to the host (exit 2).',
        '[camel-remote] this refusal is about the -Plan target; it is not a change selection, and no --changed was given or needed.'
    )
}