# What `merge-batch-sets.ps1` decides, checked without a host, a build or a lane: the pairwise file
# sets, the plan-set union and its hash, the target branch a batch lands on (`Resolve-BatchTarget`),
# and the scratch root it writes into (`Resolve-BatchScratchRoot`).
#
#   pwsh -NoProfile -File internal/tools/merge-batch-sets.test.ps1
#
# Prints `N checks, M failed` and exits 1 when any check failed -- the same shape as
# `camel-remote-args.test.ps1`, so a reader does not have to learn a second convention.
#
# The hash check runs `scripts/merge-readings.mjs#planSetHash` through node on the same list and
# requires the same string. It is the one check here that is not about this file's own idea of
# correctness: a record that names its plan set by a different hash than the baseline cache does
# cannot be joined to a baseline, and nothing downstream would say so.
#
# The scratch-root checks run the tool itself against a throwaway git repository whose settings file
# names a merge worktree but no scratchRoot, because "exit 2" is a property of the script and not of
# the resolver: the resolver throwing is only worth something if the script turns it into that exit.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'merge-batch-sets.ps1')

$checks = 0
$failed = 0

function Check {
    param([string] $What, [bool] $Holds, [string] $Detail = '')
    $script:checks++
    if ($Holds) {
        Write-Host "  ok   $What"
    } else {
        $script:failed++
        Write-Host "  FAIL $What" -ForegroundColor Red
        if ($Detail) { Write-Host "       $Detail" }
    }
}

function Lane {
    param([string] $Short, [string[]] $Files, [string[]] $Plans = @())
    [pscustomobject]@{ Name = "lane/$Short"; Short = $Short; Files = $Files; Plans = $Plans }
}

# --- the pairwise intersection ----------------------------------------------
$apart = @(
    (Lane 'a' @('src/x.cpp') @('p1')),
    (Lane 'b' @('src/y.cpp', 'docs/z.md') @('p2')),
    (Lane 'c' @('test/q.py') @('p3'))
)
$report = Get-BatchPairReport -Lanes $apart
Check 'three lanes make three pairs' ($report.Count -eq 3) "got $($report.Count)"
Check 'every disjoint pair reports 0 shared files' (@($report | Where-Object { $_.Shared.Count -ne 0 }).Count -eq 0) 'a pair reported a shared file'
Check 'the pairs are the ones given, in order' (($report | ForEach-Object { "$($_.A)&$($_.B)" }) -join ',' -eq 'a&b,a&c,b&c') (($report | ForEach-Object { "$($_.A)&$($_.B)" }) -join ',')

$overlap = @(
    (Lane 'a' @('src/x.cpp', 'src/shared.cpp')),
    (Lane 'b' @('src/y.cpp', 'src/shared.cpp')),
    (Lane 'c' @('src/x.cpp'))
)
$report2 = Get-BatchPairReport -Lanes $overlap
$hit = @($report2 | Where-Object { $_.Shared.Count -gt 0 })
Check 'a collision is found for each pair that has one' ($hit.Count -eq 2) "got $($hit.Count)"
Check 'a collision names the file, not only the count' (@($hit | ForEach-Object { $_.Shared }) -contains 'src/shared.cpp') 'the offending file is missing'
Check 'the pair report keeps empty pairs beside the colliding ones' ($report2.Count -eq 3) "got $($report2.Count)"
# One lane's whole set inside another's: the intersection is the smaller set, not "empty because the
# first element matched nothing".
$subset = @(
    (Lane 'a' @('src/x.cpp', 'src/y.cpp')),
    (Lane 'b' @('src/x.cpp'))
)
$report3 = Get-BatchPairReport -Lanes $subset
Check 'a set contained in another reports the contained file' ($report3[0].Shared.Count -eq 1 -and $report3[0].Shared[0] -eq 'src/x.cpp') (($report3[0].Shared) -join ',')

# --- the plan set -----------------------------------------------------------
$plans = Get-BatchPlanSet -Lanes @(
    (Lane 'a' @('f1') @('test/plans/feat/b.plan.toml', 'test/plans/feat/a.plan.toml')),
    (Lane 'b' @('f2') @('test/plans/feat/a.plan.toml', 'test/plans/feat/c.plan.toml')),
    (Lane 'c' @('f3') @())
)
Check 'the union has each plan once' ($plans.Count -eq 3) "got $($plans.Count)"
Check 'the union is sorted ordinally' (($plans -join ',') -eq 'test/plans/feat/a.plan.toml,test/plans/feat/b.plan.toml,test/plans/feat/c.plan.toml') ($plans -join ',')
$reordered = Get-BatchPlanSet -Lanes @(
    (Lane 'c' @('f3') @('test/plans/feat/c.plan.toml')),
    (Lane 'b' @('f2') @('test/plans/feat/b.plan.toml')),
    (Lane 'a' @('f1') @('test/plans/feat/a.plan.toml'))
)
Check 'the order the lanes are given does not change the set' (($plans -join ',') -eq ($reordered -join ','))

# --- the hash, against the comparer's own function ---------------------------
$names = @('test/plans/feat/modules/std.plan.toml', 'test/plans/feat/docs_links.plan.toml', 'test/plans/feat/parse/frontend.plan.toml')
$mine = Get-PlanSetHash -Names $names
$script = @'
const names = JSON.parse(process.env.PLAN_NAMES)
const list = [...new Set(names.map((n) => n.trim()).filter(Boolean))].sort()
let h = 0
for (const ch of list.join('\n')) h = (h * 31 + ch.codePointAt(0)) >>> 0
process.stdout.write(`${list.length}-${h.toString(16)}`)
'@
$scriptFile = Join-Path ([System.IO.Path]::GetTempPath()) ("merge-batch-sets-$PID.mjs")
[System.IO.File]::WriteAllText($scriptFile, $script, (New-Object System.Text.UTF8Encoding($false)))
try {
    $env:PLAN_NAMES = ($names | ConvertTo-Json -Compress)
    $theirs = "$(& node $scriptFile)".Trim()
    Remove-Item Env:\PLAN_NAMES -ErrorAction SilentlyContinue
    Check 'the plan-set hash equals the comparer''s own' ($mine.Hash -eq $theirs) "mine $($mine.Hash), merge-readings $theirs"
    Check 'the hash carries the count first' ($mine.Hash.StartsWith("$($names.Count)-")) $mine.Hash
} finally {
    Remove-Item -LiteralPath $scriptFile -Force -ErrorAction SilentlyContinue
}
$empty = Get-PlanSetHash -Names @()
Check 'an empty set still has a name' ($empty.Hash -eq '0-0') $empty.Hash

# --- the target: the branch a batch lands on --------------------------------
# The same parameter `merge-lane.ps1` carries, checked here without a host: the refs a target spells,
# the default, and the refusal that names the value instead of falling back to the default.
$defaultTarget = Resolve-BatchTarget
Check 'the default target is develop' ($defaultTarget.Name -eq 'develop') $defaultTarget.Name
Check 'the default is stated in the line the batch prints' ($defaultTarget.Statement -match 'develop' -and $defaultTarget.Statement -match 'default') $defaultTarget.Statement

$integration = Resolve-BatchTarget -Target 'integration'
Check '-Target integration reads its base from origin/integration' ($integration.Origin -eq 'origin/integration') $integration.Origin
Check '-Target integration would push the merge commit to integration' ($integration.Push -eq 'integration') $integration.Push
Check '-Target integration reads the pushed ref back from refs/heads/integration' ($integration.Remote -eq 'refs/heads/integration') $integration.Remote
Check 'a target named in another case is spelled the way its refs are' ((Resolve-BatchTarget -Target 'Integration').Origin -eq 'origin/integration') (Resolve-BatchTarget -Target 'Integration').Origin

# An unknown target stops and names the value it was given; the default is never the answer to a
# value the script does not know.
$refused = ''
try { Resolve-BatchTarget -Target 'trunk' | Out-Null } catch { $refused = "$_" }
Check 'an unknown target is refused and named' ($refused -match 'trunk') $refused

# The tool's own -Target, refused by its parameter before the body runs -- so this starts nothing and
# needs no host, no settings file and no lane.
$tool = Join-Path $PSScriptRoot 'merge-batch.ps1'
$said = @(& pwsh -NoProfile -File $tool -Branch 'lane/not-a-batch' -Target 'trunk' 2>&1)
$saidExit = $LASTEXITCODE
Check 'the script refuses an unknown -Target (exit 1)' ($saidExit -eq 1) "exit $saidExit"
Check 'the script''s refusal names the value it was given' ((@($said | ForEach-Object { "$_" }) -join ' ') -match 'trunk') (@($said | ForEach-Object { "$_" }) -join ' ')

# --- the scratch root: the seat's setting, never a path of this tool's own ----
# `-ScratchRoot` is read the way `mergeWorktree` is, from the seat's settings file. The tool used to
# assume `D:\Temp\camel-batch`; a batch then wrote into a directory the seat had not made, and what
# it measured was whatever was lying there.
$noScratch = ''
try { Resolve-BatchScratchRoot -ScratchRoot '' -Config ([pscustomobject]@{}) | Out-Null } catch { $noScratch = "$_" }
Check 'a seat with no scratch root is refused' ($noScratch -ne '') $noScratch
Check 'the refusal names the key it needs and the setting beside it' ($noScratch -match 'scratchRoot' -and $noScratch -match 'mergeWorktree') $noScratch
$fromSeat = Resolve-BatchScratchRoot -ScratchRoot '' -Config ([pscustomobject]@{ scratchRoot = 'D:\Temp\seat-scratch\' })
Check 'the settings file''s scratchRoot is used, trailing separator and all' ($fromSeat -eq 'D:\Temp\seat-scratch') $fromSeat
$explicit = Resolve-BatchScratchRoot -ScratchRoot 'D:\Temp\explicit-scratch\' -Config ([pscustomobject]@{ scratchRoot = 'D:\Temp\seat-scratch' })
Check '-ScratchRoot wins over the settings file' ($explicit -eq 'D:\Temp\explicit-scratch') $explicit

# The parameter carries no default of its own: the tool's declaration is what used to name a path
# nobody chose, and a check that only called the resolver would not notice it coming back.
$declared = @(Select-String -LiteralPath $tool -Pattern "\[string\] \`$ScratchRoot = '[^']")
Check 'the tool declares no built-in scratch path' ($declared.Count -eq 0) "`[$($declared.Count) declaration(s) of a default`]"

# And the script-level fact: a settings file with a mergeWorktree but no scratchRoot stops with 2.
# The repository is a throwaway one, so this needs no host, no lane and no settings file of its own.
$scratchRepo = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-batch-settings-$PID")
New-Item -ItemType Directory -Force -Path (Join-Path $scratchRepo 'internal\tools') | Out-Null
try {
    & git -C $scratchRepo init -q 2>&1 | Out-Null
    $repoTop = "$(& git -C $scratchRepo rev-parse --show-toplevel 2>$null)".Trim()
    $cfgJson = ([ordered]@{ mergeWorktree = $repoTop } | ConvertTo-Json -Compress)
    [System.IO.File]::WriteAllText((Join-Path $scratchRepo 'internal\tools\camel-gate.local.json'), $cfgJson,
        (New-Object System.Text.UTF8Encoding($false)))
    $said = @(& pwsh -NoProfile -File $tool -Branch 'lane/not-a-batch' -ToolTree $repoTop 2>&1)
    $saidExit = $LASTEXITCODE
    Check 'a settings file with no scratchRoot stops the batch (exit 2)' ($saidExit -eq 2) "exit $saidExit"
    Check 'and the refusal names the key, not a path it fell back to' ((@($said | ForEach-Object { "$_" }) -join ' ') -match 'scratchRoot') (@($said | ForEach-Object { "$_" }) -join ' ')
} finally {
    Remove-Item -LiteralPath $scratchRepo -Recurse -Force -ErrorAction SilentlyContinue
}

# --- the helper refs: the seat's name, and a collision said in words ----------------------------
# `probe/batch-base` and `probe/batch-merge` were literals. Three seats push to one target branch, so
# the second seat to start a batch stopped at `fatal: 'probe/batch-base' is already used by worktree
# at 'D:/Temp/camel-batch/camel-batch-base'` -- git talking about a directory nobody in that run
# owned. The names follow the scratch root now, and a collision is a sentence naming the tree.
$refA = Resolve-BatchRefName -ScratchRoot 'D:\Temp\camel-batch-3' -Which 'base'
$refA2 = Resolve-BatchRefName -ScratchRoot 'D:\Temp\camel-batch-3\' -Which 'base'
Check 'the same scratch root spells the same ref' ($refA -eq $refA2) "$refA vs $refA2"
$refB = Resolve-BatchRefName -ScratchRoot 'D:\Temp\camel-batch' -Which 'base'
Check 'two scratch roots never share a ref' ($refA -ne $refB) "both spelled $refA"
Check 'the name says which of the two trees it is' (($refA -like 'probe/batch-base-*') -and ((Resolve-BatchRefName -ScratchRoot 'D:\Temp\camel-batch-3' -Which 'merge') -like 'probe/batch-merge-*')) $refA
$noRootName = ''
try { Resolve-BatchRefName -ScratchRoot '' -Which 'base' | Out-Null } catch { $noRootName = "$_" }
Check 'a ref name with no scratch root is refused' ($noRootName -match 'scratch root') $noRootName
Check 'the tool names no helper ref as a literal any more' (@(Select-String -LiteralPath $tool -Pattern "'probe/batch-(base|merge)'").Count -eq 0) 'a literal spelling is back'

# A real collision, not a fabricated one: a throwaway repository, a worktree that really holds the
# name, and the refusal read out of `git worktree list --porcelain`. What is checked is the sentence,
# because the sentence is what a seat has to act on.
$refRepo = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-batch-refs-$PID")
$held = Join-Path $refRepo 'held-tree'
$mineTree = Join-Path $refRepo 'my-tree'
New-Item -ItemType Directory -Force -Path $refRepo | Out-Null
try {
    & git -C $refRepo init -q 2>&1 | Out-Null
    & git -C $refRepo config user.email 'batch@example.invalid' 2>&1 | Out-Null
    & git -C $refRepo config user.name 'batch test' 2>&1 | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $refRepo 'a.txt'), "one`n", (New-Object System.Text.UTF8Encoding($false)))
    & git -C $refRepo add -A 2>&1 | Out-Null
    & git -C $refRepo commit -q -m 'one' 2>&1 | Out-Null
    & git -C $refRepo worktree add -q -B $refA $held HEAD 2>&1 | Out-Null
    $porcelain = ((& git -C $refRepo worktree list --porcelain) -join "`n")
    $refusal = Get-BatchRefRefusal -Porcelain $porcelain -RefName $refA -Path $mineTree
    Check 'a ref another tree holds is refused' ($refusal -ne $null -and "$refusal" -ne '') "$refusal"
    Check 'and the refusal names the tree that holds it' (("$refusal" -replace '\\', '/').Contains(($held -replace '\\', '/'))) "$refusal"
    Check 'and it does not read like git''s fatal line' ("$refusal" -notmatch '^fatal:') "$refusal"
    Check 'the tree holding the name is not a collision with itself' ($null -eq (Get-BatchRefRefusal -Porcelain $porcelain -RefName $refA -Path $held)) 'the holder was reported as a collision'
    Check 'a free name is not a collision' ($null -eq (Get-BatchRefRefusal -Porcelain $porcelain -RefName 'probe/batch-base-free' -Path $mineTree)) 'a free name was refused'
} finally {
    Remove-Item -LiteralPath $refRepo -Recurse -Force -ErrorAction SilentlyContinue
}

# The batch writes the seat's settings file into every tree it measures, and the gate reads it there.
# The two tools therefore share one schema, and a key the batch writes is a key the gate must know --
# the first end-to-end run of 2026-11-24 stopped with `has an unknown key 'scratchRoot'` as the only
# line in its baseline transcript, and an empty transcript reads as "the tree is fine" to anyone in a
# hurry.
$gateConfig = Join-Path $PSScriptRoot 'camel-gate-config.ps1'
Check 'the gate knows the batch''s own settings key' (@(Select-String -LiteralPath $gateConfig -Pattern '^\s+scratchRoot\s+=').Count -eq 1) 'camel-gate-config.ps1 does not list scratchRoot'

Write-Host ''
Write-Host "$checks checks, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0