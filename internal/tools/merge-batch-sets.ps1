# The two readings a batch's composition rests on, in their own file so they can be checked without
# a host, a build or a lane: which pairs of lanes touch a file in common, and what the plans they
# select add up to. The branch the batch lands on is here for the same reason: which refs a target
# spells is decided once, by name, and a value it does not know is refused here rather than defaulted.
# Where the batch writes its worktrees is here for that reason too: it is read from the seat's
# settings file, and a seat that names none is refused instead of given a path of this tool's choosing.
#
# They are separated out for the reason `camel-remote-args.ps1` is: this is the part that fails
# quietly. "These lanes look like they touch different things" is an impression, and the pair it gets
# wrong is discovered two plan runs later as a conflict; a plan set that is not the union of what the
# lanes select is a measurement of something else, reported as if it were about them. Neither is
# visible by looking at a merge.
#
# `merge-batch-sets.test.ps1` checks both, plus the plan-set hash against the one
# `scripts/merge-readings.mjs` computes, because a record that names a plan set by a different hash
# than the baseline cache does is a record nobody can join to a baseline. It checks the target and
# the scratch root the same way: by calling the resolvers, with no host involved.

# The branch a batch lands on, and every ref the run builds from it: the base it reads
# (`origin/<target>`), the refspec it pushes (`<target>`), and the remote ref it reads back
# (`refs/heads/<target>`). One function and one spelling, because the failure this prevents is a
# second place that says `develop` after the parameter said otherwise.
#
# `develop` is the release line and the default; `integration` is the shared branch the parallel
# merge seats feed. There is deliberately no "unknown target -> develop" here: a value this does not
# know stops with the value in the message. A refusal that fell back would read exactly like the
# default being right, which is the mistake this parameter exists to remove.
#
# The canonical spelling is returned, not the spelling given. The tool's `-Target` is validated with
# `[ValidateSet('develop', 'integration')]`, which compares case-insensitively, so `-Target
# Integration` reaches this function; building `origin/Integration` from the word as typed would ask
# for a ref that does not exist on a case-sensitive remote.
function Resolve-BatchTarget {
    param([string] $Target = 'develop')
    $known = @('develop', 'integration')
    $match = @($known | Where-Object { $_ -eq "$Target".Trim() })
    if ($match.Count -eq 0) {
        throw "unknown target '$Target': a batch lands on one of $($known -join ', '), and this script neither falls back nor guesses"
    }
    $name = $match[0]
    return [pscustomobject]@{
        Name      = $name
        Origin    = "origin/$name"
        Push      = $name
        Remote    = "refs/heads/$name"
        IsDefault = ($name -eq 'develop')
        # The line the tool prints before it measures anything. It names the target and says whether
        # it was defaulted: "wrote develop" and "was told develop" are different facts, and only one
        # of them is visible from the branch alone.
        Statement = if ($name -eq 'develop') { 'target: develop (the default)' } else { "target: $name (from -Target)" }
    }
}

# Where the batch's two worktrees and every transcript are written. `-ScratchRoot` may name it; when
# it does not, the settings file's `scratchRoot` does, and when neither does this refuses. There is
# no default, for the same reason `mergeWorktree` has none: the path is machine-local and belongs to
# the seat, and a path the tool writes into without anybody having said it may is a build's scratch
# where nothing is watching it -- the old `D:\Temp\camel-batch` was exactly that, and a seat that
# never made that directory got a batch that measured whatever happened to be lying there. The
# refusal names the key and the file, and never falls back to a path this checkout does not own.
function Resolve-BatchScratchRoot {
    param([string] $ScratchRoot = '', $Config)
    $given = "$ScratchRoot".Trim()
    if ($given) { return $given.TrimEnd('\', '/') }
    $fromFile = if ($Config -and $Config.scratchRoot) { "$($Config.scratchRoot)".Trim() } else { '' }
    if (-not $fromFile) {
        throw 'the settings file names no scratchRoot: the batch creates two worktrees and every transcript under one directory, so it needs a path it owns; add ``scratchRoot`` beside ``mergeWorktree``, because this script has no default to fall back to'
    }
    return $fromFile.TrimEnd('\', '/')
}

# The two refs a batch creates, named after the scratch root and not after the tool. Three seats push
# to one target branch, so a literal name is a name the second seat to start a batch cannot have: on
# 2026-11-24 the run stopped at `fatal: 'probe/batch-base' is already used by worktree at
# 'D:/Temp/camel-batch/camel-batch-base'` -- git talking about a directory the seat does not own, in a
# message nobody can act on. The leaf and a short hash of the whole path make the name the seat's own:
# two scratches never share one, and a re-run recognises its own leftovers instead of a stranger's.
#
# The hash is the same `h * 31 + c` fold `Get-PlanSetHash` uses, so this file needs no crypto
# provider and two machines reading the same path spell the same name.
function Resolve-BatchRefName {
    param([string] $ScratchRoot = '', [string] $Which = 'base')
    $root = "$ScratchRoot".Trim().TrimEnd('\', '/')
    if (-not $root) {
        throw 'a batch ref name needs the scratch root: without it the name is the tool''s, and every seat on the machine shares it'
    }
    $leaf = ($root -split '[\\/]')[-1]
    $leaf = "$leaf" -replace '[^A-Za-z0-9._-]', '-'
    $leaf = $leaf.Trim('-')
    if (-not $leaf) { $leaf = 'scratch' }
    $hash = [int64]0
    $mask = [int64]4294967295
    foreach ($ch in (($root -replace '\\', '/').ToLowerInvariant()).ToCharArray()) {
        $hash = (($hash * 31) + [int64][char]$ch) -band $mask
    }
    return "probe/batch-$Which-$leaf-$('{0:x}' -f $hash)"
}

# What to say when the ref a batch wants is already held by a tree that is not the one it is about to
# create. `$null` means the name is free, or the tree holding it is this batch's own and will be
# moved to the base the same way a re-run does. The sentence names the tree, because that is the only
# part a seat can act on: `fatal: '<ref>' is already used by worktree at '<path>'` is git describing
# its internals, and the run that hit it could not tell a stale scratch from a live one.
function Get-BatchRefRefusal {
    param([string] $Porcelain = '', [string] $RefName = '', [string] $Path = '')
    $holder = Get-BatchRefHolder -Porcelain $Porcelain -RefName $RefName
    if (-not $holder) { return $null }
    if ((($holder -replace '\\', '/').TrimEnd('/')) -eq (($Path -replace '\\', '/').TrimEnd('/'))) { return $null }
    return "the ref $RefName is held by the worktree $holder, and that tree is not this batch's own ($Path)"
}

# Which worktree holds a ref, read from `git worktree list --porcelain` as text. That is the reading
# that turns "cannot create the worktree" into "this name is another tree's": the tool can then say
# whose it is, which is the only sentence a seat can act on. `$null` when nothing holds it.
function Get-BatchRefHolder {
    param([string] $Porcelain = '', [string] $RefName = '')
    $path = $null
    foreach ($line in ("$Porcelain" -split "`n")) {
        $t = "$line".Trim()
        if ($t -like 'worktree *') {
            $path = $t.Substring(9).Trim()
        } elseif ($t -eq "branch refs/heads/$RefName" -and $path) {
            return $path
        }
    }
    return $null
}

# One entry per pair of lanes, in the order the lanes were given. `Shared` is the intersection of the
# two file sets -- the one reading that makes "they do not touch the same file" a fact.
function Get-BatchPairReport {
    param($Lanes)
    $pairs = @()
    for ($i = 0; $i -lt $Lanes.Count; $i++) {
        for ($j = $i + 1; $j -lt $Lanes.Count; $j++) {
            $shared = @($Lanes[$i].Files | Where-Object { $Lanes[$j].Files -contains $_ })
            $pairs += [pscustomobject]@{
                A      = $Lanes[$i].Short
                B      = $Lanes[$j].Short
                Shared = $shared
            }
        }
    }
    return $pairs
}

# The union of what the lanes select: order-independent, no name twice. Sorted with the ordinal
# comparer and not with culture rules, because the same list has to hash to the same value here and
# in `scripts/merge-readings.mjs`, which sorts JavaScript strings (code units) and would disagree with
# a culture sort over punctuation.
function Get-BatchPlanSet {
    param($Lanes)
    $names = @($Lanes | ForEach-Object { $_.Plans } | ForEach-Object { "$_".Trim() } | Where-Object { $_ } | Sort-Object -Unique)
    $ordinal = [string[]]$names
    [array]::Sort($ordinal, [System.StringComparer]::Ordinal)
    return @($ordinal)
}

# The plan set's name, in the shape `scripts/merge-readings.mjs#planSetHash` gives it: `<count>-<hex>`
# over the names joined by newlines, each character folded in with `h * 31 + c` on 32 bits.
function Get-PlanSetHash {
    param([string[]] $Names)
    $list = [string[]]@($Names | ForEach-Object { "$_".Trim() } | Where-Object { $_ } | Sort-Object -Unique)
    [array]::Sort($list, [System.StringComparer]::Ordinal)
    $hash = 0
    # `0xFFFFFFFF` is -1 as a PowerShell hex literal (Int32), and `-band -1` masks nothing: the value
    # then grows by 31 per character and overflows Int64 after thirteen of them. The mask is written
    # as a literal Int64 for that reason.
    $mask = [int64]4294967295
    foreach ($ch in ($list -join "`n").ToCharArray()) { $hash = (($hash * 31) + [int64][char]$ch) -band $mask }
    return [pscustomobject]@{
        Names = @($list)
        Count = $list.Count
        Hash  = ("{0}-{1:x}" -f $list.Count, $hash)
    }
}