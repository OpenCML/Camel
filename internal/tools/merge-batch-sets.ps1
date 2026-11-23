# The two readings a batch's composition rests on, in their own file so they can be checked without
# a host, a build or a lane: which pairs of lanes touch a file in common, and what the plans they
# select add up to.
#
# They are separated out for the reason `camel-remote-args.ps1` is: this is the part that fails
# quietly. "These lanes look like they touch different things" is an impression, and the pair it gets
# wrong is discovered two plan runs later as a conflict; a plan set that is not the union of what the
# lanes select is a measurement of something else, reported as if it were about them. Neither is
# visible by looking at a merge.
#
# `merge-batch-sets.test.ps1` checks both, plus the plan-set hash against the one
# `scripts/merge-readings.mjs` computes, because a record that names a plan set by a different hash
# than the baseline cache does is a record nobody can join to a baseline.

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