# Which checkout a merge lands in: the setting, and the two ways a run can fail to read it.
#
# Why it exists: `-MainCheckout` used to default to a path. The first default was the author's own
# checkout (iron rule 17 forbids working there); the repair pointed it at another seat's worktree, so
# the defect kept its shape and changed its victim -- a default that names an existing tree does not
# fail, it merges into somebody else's checkout and reports success. The tree is a machine-local path,
# so it now comes from this machine's settings file (`camel-gate.local.json`, key `mergeWorktree`, the
# key the pre-push hook already reads), and a run that cannot read it stops.
#
# What is checked here:
#
#   (a) with the key in the settings file, the resolved checkout is exactly the one an explicit
#       -MainCheckout names -- and an explicit path wins even when the file says something else, and
#       even when there is no file at all (that is the behaviour every other gate tool keeps);
#   (b) when the key is absent the run stops, and the reason is the KEY, not the file; when the file
#       is absent the reason is the FILE. The two sentences differ, because a reader acts differently
#       on each -- one means this machine was set up without a tree, the other that it was never set
#       up -- and one sentence for both would be the same silence in a new place.
#
# Two of the checks run the shipped script as a subprocess, because the sentence a seat sees is the
# script's own, not this function's. They stop before anything is read, fetched or merged: exit 2 and
# no repository touched.
#
# The functions are read out of `merge-lane.ps1` itself (by name, from its syntax tree) rather than
# copied here, so this measures the file that ships.
#
#   pwsh -NoProfile -File internal/tools/merge-lane-checkout.test.ps1
#   pwsh -NoProfile -File internal/tools/merge-lane-checkout.test.ps1 -Keep   (leave the fixtures)
#
# Everything here is local: no host, no build, no checkout of this repository is touched.

[CmdletBinding()]
param(
    # Keep the fixture tree for inspection.
    [switch] $Keep
)

$ErrorActionPreference = 'Stop'

$script:failed = 0
$script:checked = 0

function Check {
    param([string] $Name, [bool] $Ok, [string] $Detail = '')
    $script:checked++
    if ($Ok) {
        Write-Host "  PASS  $Name"
    } else {
        $script:failed++
        Write-Host "  FAIL  $Name"
        if ($Detail) { Write-Host "        $Detail" }
    }
}

function Write-Settings {
    param([string] $Path, [string] $Json)
    [System.IO.File]::WriteAllText($Path, $Json, (New-Object System.Text.UTF8Encoding($false)))
}

# --- the shipped function, read out of the script ----------------------------
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$laneScript = Join-Path $repoRoot 'internal\tools\merge-lane.ps1'
$scriptText = Get-Content -Raw -LiteralPath $laneScript

$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($laneScript, [ref] $tokens, [ref] $parseErrors)
Check 'merge-lane.ps1 parses' (@($parseErrors).Count -eq 0) (($parseErrors | ForEach-Object { "$($_.Extent.StartLineNumber): $($_.Message)" }) -join '; ')

$found = @($ast.FindAll({
            param($node)
            $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Resolve-MergeCheckout'
        }, $true))
Check 'the script still defines Resolve-MergeCheckout' ($found.Count -eq 1) "found $($found.Count) definition(s)"
if ($found.Count -eq 1) { Invoke-Expression $found[0].Extent.Text }

# The parameter itself, read from the syntax tree: no default value at all, which is the claim. A
# default that names a real tree is this whole page's subject, so the assertion is about the
# declaration and not about the value one run happened to use.
$mainParam = @($ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq 'MainCheckout' })
Check 'the script declares -MainCheckout' ($mainParam.Count -eq 1) "found $($mainParam.Count)"
$mainDefault = if ($mainParam.Count -eq 1 -and $mainParam[0].DefaultValue) { $mainParam[0].DefaultValue.Extent.Text.Trim() } else { '' }
Check 'and it has no path as a default' ((@("''", '""') -contains $mainDefault) -and $mainDefault) "default=[$mainDefault]"
Check 'the old seat default is gone from the file' (-not ($scriptText -match 'Camel-wt\\merge')) 'Camel-wt\merge is still there'

# --- the fixtures -------------------------------------------------------------
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('merge-lane-checkout-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$seat = Join-Path $scratch 'the-seat-tree'
$other = Join-Path $scratch 'another-tree'
New-Item -ItemType Directory -Path $seat -Force | Out-Null
New-Item -ItemType Directory -Path $other -Force | Out-Null
Write-Host "scratch: $scratch"

$withKey = Join-Path $scratch 'with-key.json'
Write-Settings -Path $withKey -Json ('{ "mergeWorktree": "' + $seat.Replace('\', '\\') + '" }')
$withOtherKey = Join-Path $scratch 'with-other-key.json'
Write-Settings -Path $withOtherKey -Json ('{ "mergeWorktree": "' + $other.Replace('\', '\\') + '" }')
$noKey = Join-Path $scratch 'no-key.json'
Write-Settings -Path $noKey -Json '{ "sshHost": "example.invalid" }'
$missingFile = Join-Path $scratch 'never-written.json'
$arrayKey = Join-Path $scratch 'array-key.json'
Write-Settings -Path $arrayKey -Json ('{ "mergeWorktree": ["' + $seat.Replace('\', '\\') + '"] }')
$twoTrees = Join-Path $scratch 'two-trees.json'
Write-Settings -Path $twoTrees -Json ('{ "mergeWorktree": "' + $seat.Replace('\', '\\') + ';' + $other.Replace('\', '\\') + '" }')
$goneTree = Join-Path $scratch 'gone-tree.json'
Write-Settings -Path $goneTree -Json ('{ "mergeWorktree": "' + (Join-Path $scratch 'not-there').Replace('\', '\\') + '" }')

# --- (a) the key in the file, and an explicit path ----------------------------
$byKey = Resolve-MergeCheckout -Given '' -ToolsRoot $PSScriptRoot -LocalConfig $withKey
Check 'the settings key names the checkout' ((-not $byKey.Problem) -and $byKey.Path -eq $seat) "problem=$($byKey.Problem) path=$($byKey.Path)"

$byArg = Resolve-MergeCheckout -Given $seat -ToolsRoot $PSScriptRoot -LocalConfig $withKey
Check 'and an explicit -MainCheckout resolves to the same tree' ((-not $byArg.Problem) -and $byArg.Path -eq $byKey.Path) "problem=$($byArg.Problem) path=$($byArg.Path)"

$argWins = Resolve-MergeCheckout -Given $seat -ToolsRoot $PSScriptRoot -LocalConfig $withOtherKey
Check 'an explicit path wins over what the file says' ((-not $argWins.Problem) -and $argWins.Path -eq $seat) "path=$($argWins.Path)"

$argOnly = Resolve-MergeCheckout -Given $seat -LocalConfig $missingFile
Check 'and an explicit path needs no settings file at all' ((-not $argOnly.Problem) -and $argOnly.Path -eq $seat) "problem=$($argOnly.Problem)"

$arrayForm = Resolve-MergeCheckout -Given '' -ToolsRoot $PSScriptRoot -LocalConfig $arrayKey
Check 'the key may be a one-element array, as the hook writes it' ((-not $arrayForm.Problem) -and $arrayForm.Path -eq $seat) "problem=$($arrayForm.Problem) path=$($arrayForm.Path)"

# --- (b) the two ways it can fail to be read ----------------------------------
$missing = Resolve-MergeCheckout -Given '' -ToolsRoot $PSScriptRoot -LocalConfig $missingFile
Check 'no settings file stops the run' ([bool]$missing.Problem -and -not $missing.Path) "problem=$($missing.Problem)"
Check 'and the reason is the file that is not there' ($missing.Problem -match 'there is no settings file at' -and $missing.Problem -match [regex]::Escape($missingFile)) $missing.Problem
Check 'and it does not claim the key is missing' (-not ($missing.Problem -match "has no 'mergeWorktree'")) $missing.Problem

$noKeyRun = Resolve-MergeCheckout -Given '' -ToolsRoot $PSScriptRoot -LocalConfig $noKey
Check 'a file without the key stops the run' ([bool]$noKeyRun.Problem -and -not $noKeyRun.Path) "problem=$($noKeyRun.Problem)"
Check 'and the reason is the key, named' ($noKeyRun.Problem -match "has no 'mergeWorktree'" -and $noKeyRun.Problem -match [regex]::Escape($noKey)) $noKeyRun.Problem
Check 'and it does not claim the file is missing' (-not ($noKeyRun.Problem -match 'there is no settings file')) $noKeyRun.Problem
Check 'the two failures read differently' ($missing.Problem -ne $noKeyRun.Problem) 'the same sentence for both'

$twoRun = Resolve-MergeCheckout -Given '' -ToolsRoot $PSScriptRoot -LocalConfig $twoTrees
Check 'a key that names more than one tree stops the run' ([bool]$twoRun.Problem -and $twoRun.Problem -match 'names 2 trees') $twoRun.Problem
Check 'and it does not pick the first one' (-not $twoRun.Path) "path=$($twoRun.Path)"

$gone = Resolve-MergeCheckout -Given '' -ToolsRoot $PSScriptRoot -LocalConfig $goneTree
Check 'a key that names a directory that is not there stops the run' ([bool]$gone.Problem -and $gone.Problem -match 'there is no directory there') $gone.Problem

# --- the sentence a seat actually sees ----------------------------------------
# The script's own stop, not this function's: two subprocess runs, both of which end before anything is
# read, fetched or merged.
function Invoke-MergeLane {
    param([string[]] $Extra)
    $out = @(& pwsh -NoProfile -File $laneScript -Branch 'lane/absent' -Message 'test: the checkout' @Extra 2>&1)
    return [pscustomobject]@{ Code = $LASTEXITCODE; Text = (@($out | ForEach-Object { "$_" }) -join "`n") }
}

$noFileRun = Invoke-MergeLane -Extra @('-LocalConfig', $missingFile, '-BaselineWorktree', (Join-Path $scratch 'baseline'))
Check 'the script stops with exit 2 when there is no settings file' ($noFileRun.Code -eq 2) "exit $($noFileRun.Code): $($noFileRun.Text)"
Check 'and it names that file' ($noFileRun.Text -match [regex]::Escape($missingFile)) $noFileRun.Text
Check 'and it says nothing was merged' ($noFileRun.Text -match 'nothing was read and nothing was merged') $noFileRun.Text

$noKeyRun2 = Invoke-MergeLane -Extra @('-LocalConfig', $noKey, '-BaselineWorktree', (Join-Path $scratch 'baseline'))
Check 'the script stops with exit 2 when the key is absent' ($noKeyRun2.Code -eq 2) "exit $($noKeyRun2.Code): $($noKeyRun2.Text)"
Check 'and it names the key' ($noKeyRun2.Text -match "no 'mergeWorktree'") $noKeyRun2.Text

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0