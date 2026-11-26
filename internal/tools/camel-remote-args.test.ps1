# The lists `camel-remote.ps1` takes, and the two ways a list used to mean something else.
#
# `-AstOnly` selects which lines a `-UpdateAstHashes` recording rewrites. Given as a comma-separated
# array through `pwsh -File`, it arrived as one glob named `a,b`, selected none of the record's files,
# and a narrowing that selects nothing is the bulk re-record the flag exists to avoid -- 4252 files
# rewritten where two were named. Repeated as `-AstOnly a -AstOnly b` it was refused by the engine,
# which is loud, and the documentation taught the array form as the workaround.
#
# The fix is one rule and two guards: every list parameter splits on commas like `-Plan` already did,
# and a `-AstOnly` that names no `.cml` in this tree is refused here, by the flag's own name, instead
# of on the host as "--only selected none".
#
#   pwsh -NoProfile -File internal/tools/camel-remote-args.test.ps1
#
# Everything here is local: no host, no build, no checkout modified.

[CmdletBinding()]
param(
    # Keep the fixture tree for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'camel-remote-args.ps1')
. (Join-Path $PSScriptRoot 'camel-remote-checkers.ps1')

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

# --- 1. the split ------------------------------------------------------------
# The three forms a caller can type, after `pwsh -File` has already decided how they split. The
# repeated form is the engine's refusal and is not reachable from here; what is reachable is that the
# two working forms give the same list.
$two = Split-CamelArgList -Values @('a,b')
Check 'a comma in one word is two globs' ($two.Count -eq 2 -and $two[0] -eq 'a' -and $two[1] -eq 'b') ($two -join '|')
$array = Split-CamelArgList -Values @('a', 'b')
Check 'the array form gives the same two globs' (($array -join '|') -eq ($two -join '|')) ($array -join '|')
$padded = Split-CamelArgList -Values @(' a , b ')
Check 'each glob is trimmed' (($padded -join '|') -eq 'a|b') ($padded -join '|')
$mixed = Split-CamelArgList -Values @('a', 'b,c')
Check 'a mixed word and list is three globs' (($mixed -join '|') -eq 'a|b|c') ($mixed -join '|')
$none = Split-CamelArgList -Values @('', ' ', ',')
Check 'empty words and bare commas name nothing' ($none.Count -eq 0) ($none -join '|')

$onlyArgs = Get-AstOnlyArgs -AstOnly $two
Check 'each glob becomes its own --only' ($onlyArgs -eq " --only 'a' --only 'b'") $onlyArgs
Check 'no globs means no argument text' ((Get-AstOnlyArgs -AstOnly @()) -eq '') (Get-AstOnlyArgs -AstOnly @())
$dropArgs = Get-AstDropArgs -AstDrop @('test/cases/parse/gone.cml')
Check 'a dropped row becomes --drop' ($dropArgs -eq " --drop 'test/cases/parse/gone.cml'") $dropArgs
$entryArgs = Get-EntryArgs -Entry @('fold.constant_budget.*', 'plan/one')
Check 'each entry becomes its own --entry' ($entryArgs -eq " --entry 'fold.constant_budget.*' --entry 'plan/one'") $entryArgs
Check 'no entries means no argument text' ((Get-EntryArgs -Entry @()) -eq '') (Get-EntryArgs -Entry @())

# --- 2. a narrowing that selects nothing -------------------------------------
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-args-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path (Join-Path $scratch 'test/cases/parse') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $scratch 'stdlib/nested') -Force | Out-Null
Set-Content -Path (Join-Path $scratch 'test/cases/parse/one.cml') -Value 'func main(): int { return 0 }'
Set-Content -Path (Join-Path $scratch 'test/cases/parse/two.cml') -Value 'func main(): int { return 0 }'
Set-Content -Path (Join-Path $scratch 'stdlib/nested/deep.cml') -Value 'func main(): int { return 0 }'

$problems = @(Get-CamelGlobProblems -Globs @('test/cases/parse/one.cml') -Flag '-AstOnly' -Root $scratch -Named)
Check 'a glob that names a .cml is accepted' ($problems.Count -eq 0) ($problems -join ' / ')

$problems = @(Get-CamelGlobProblems -Globs @('test/cases/parse/*.cml') -Flag '-AstOnly' -Root $scratch -Named)
Check 'a wildcard that selects both cases is accepted' ($problems.Count -eq 0) ($problems -join ' / ')

# `check.py` matches with `fnmatch.fnmatchcase`, where `*` crosses a `/`: a glob written for the
# record covers a nested case too, and refusing it here would block a call the host accepts.
$problems = @(Get-CamelGlobProblems -Globs @('stdlib/*.cml') -Flag '-AstOnly' -Root $scratch -Named)
Check 'a star crosses a directory the way fnmatch does' ($problems.Count -eq 0) ($problems -join ' / ')

$problems = @(Get-CamelGlobProblems -Globs @('nothing/**') -Flag '-AstOnly' -Root $scratch -Named)
Check 'a glob that names no .cml is refused' ($problems.Count -eq 1 -and $problems[0] -like "-AstOnly 'nothing/**' names none*") ($problems -join ' / ')
Check 'and the refusal counts what it searched' ($problems.Count -eq 1 -and $problems[0] -like '*3 .cml file(s)*') ($problems -join ' / ')

$problems = @(Get-CamelGlobProblems -Globs @() -Flag '-AstOnly' -Root $scratch -Named)
Check 'the flag given no glob is refused' ($problems.Count -eq 1 -and $problems[0] -like '-AstOnly was given no glob*') ($problems -join ' / ')

# Not given at all is not a problem: a bulk re-record is a legitimate call, and its own guard (the
# row-drop check in camel-remote.ps1) is what makes it safe.
$problems = @(Get-CamelGlobProblems -Globs @() -Flag '-AstOnly' -Root $scratch)
Check 'the flag not being given is not a problem' ($problems.Count -eq 0) ($problems -join ' / ')

$problems = @(Get-CamelGlobProblems -Globs @("test/cases/parse/it's.cml") -Flag '-AstOnly' -Root $scratch -Named)
Check 'a glob carrying a quote is refused' ($problems.Count -eq 1 -and $problems[0] -like "*contains a quote*") ($problems -join ' / ')

$problems = @(Get-CamelGlobProblems -Globs @('test/cases/parse/[o]ne.cml') -Flag '-AstOnly' -Root $scratch -Named)
Check 'a character class is left to the host' ($problems.Count -eq 0) ($problems -join ' / ')

# --- 3. the runner line of a -UpdateGolden run -------------------------------
# `-Entry a,b` reached the host as an empty pattern because a loop elsewhere in the script reused the
# name `$entry`, and PowerShell variable names ignore case: the loop assigned the `-Entry` parameter.
# Section 5 reads the script for that whole class; here is the line the host receives.
$one = Get-UpdateGoldenCommand -PlanArgs " 'p.plan.toml'" -Entry @('cse_written_variable.by_*') -TierArg '' -ListKnownArg ''
Check 'one entry reaches the runner line' ($one -eq 'cd $R/ && node test/tools/run-tests.mjs --update-golden  ''p.plan.toml'' --entry ''cse_written_variable.by_*'' --jobs $CAMEL_JOBS') $one
$both = Get-UpdateGoldenCommand -PlanArgs " 'p.plan.toml'" -Entry (Split-CamelArgList -Values @('cse_written_variable.by_cpp,cse_written_variable.by_camel')) -TierArg " --tier 'fast'" -ListKnownArg ''
Check 'two entries reach the runner line, each its own word' ($both -eq 'cd $R/ && node test/tools/run-tests.mjs --update-golden  ''p.plan.toml'' --entry ''cse_written_variable.by_cpp'' --entry ''cse_written_variable.by_camel'' --jobs $CAMEL_JOBS --tier ''fast''') $both
$none = Get-UpdateGoldenCommand -PlanArgs " 'p.plan.toml'" -Entry @()
Check 'no entry means no --entry word' ($none -notlike '*--entry*') $none

# --- 4. the checker blocks, from the real table ------------------------------
# The table is read out of the script itself, so the check is about the rows the gate runs and not a
# copy that can drift from them.
$scriptPath = Join-Path $PSScriptRoot 'camel-remote.ps1'
$parseTokens = $null
$parseErrors = $null
$scriptAst = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$parseTokens, [ref]$parseErrors)
Check 'camel-remote.ps1 parses' ($parseErrors.Count -eq 0) (($parseErrors | ForEach-Object { $_.Message }) -join ' / ')
$tableAssignment = $scriptAst.Find({
        param($n)
        $n -is [System.Management.Automation.Language.AssignmentStatementAst] -and
        $n.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
        $n.Left.VariablePath.UserPath -eq 'checkers'
    }, $true)
Check 'the checkers table is found' ($null -ne $tableAssignment)
$checkers = & ([scriptblock]::Create($tableAssignment.Right.Extent.Text))
Check 'the table has rows' ($checkers.Count -gt 5) "$($checkers.Count)"

$block = Get-CamelCheckerBlock -Name 'check-doc-links' -Spec $checkers['check-doc-links'] -VerifyTag 'latest' -Jobs 8 -RemoteScratch '/tmp/s'
Check 'the doc-links header names its script and argument' ($block.Contains("echo '[camel-remote] --- check check-doc-links: node scripts/check-doc-links.mjs --citations ---'")) $block
Check 'the doc-links command is the script and not a bare interpreter' ($block -match 'run-quiet -- node scripts/check-doc-links\.mjs --citations < /dev/null 2>&1') $block
Check 'a wall checker feeds checkfail' ($block.Contains('checkfail=$c')) $block

$census = Get-CamelCheckerBlock -Name 'capture-census' -Spec $checkers['capture-census'] -VerifyTag 'debug' -Jobs 8 -RemoteScratch '/tmp/s'
Check 'a compiling report-only checker is pointed at the tag and reads no input' ($census.Contains('run-quiet -- node scripts/check-capture-census.mjs  --camel "out/debug/bin/camel" < /dev/null > "$out" 2>&1')) $census
Check 'a report-only checker stays out of checkfail' (-not $census.Contains('checkfail=$c')) $census

$bare = @($checkers.Keys | Where-Object {
        $block = Get-CamelCheckerBlock -Name $_ -Spec $checkers[$_] -VerifyTag 'latest' -Jobs 8 -RemoteScratch '/tmp/s'
        $block -notmatch 'run-quiet -- \S+ \S+' -or $block -notmatch '< /dev/null'
    })
Check 'every row of the table gives a command with a script and no input' ($bare.Count -eq 0) ($bare -join ', ')

Check 'a name the table lacks is a problem naming it' ((Get-CamelCheckerProblem -Name 'no-such' -Spec $checkers['no-such']) -like "*'no-such'*") (Get-CamelCheckerProblem -Name 'no-such' -Spec $checkers['no-such'])
Check 'a row without a script is a problem naming it' ((Get-CamelCheckerProblem -Name 'bad' -Spec @{ args = @('--x') }) -like "*'bad'*no script*") (Get-CamelCheckerProblem -Name 'bad' -Spec @{ args = @('--x') })
Check 'a row with a script is not a problem' ($null -eq (Get-CamelCheckerProblem -Name 'check-doc-links' -Spec $checkers['check-doc-links']))
$thrown = ''
try { [void](Get-CamelCheckerBlock -Name 'no-such' -Spec $checkers['no-such'] -VerifyTag 'latest' -Jobs 8 -RemoteScratch '/tmp/s') } catch { $thrown = $_.Exception.Message }
Check 'the generator refuses a missing row instead of writing a bare interpreter' ($thrown -like "*'no-such'*") $thrown

# --- 5. no variable of the script may be a parameter by another spelling -----
# PowerShell variable names ignore case, so `$entry = ...` in the body assigns the `[string[]]$Entry`
# parameter: the value is converted to the parameter's type (a table row became the text
# "System.Collections.Hashtable"; its `.script` was empty) and the caller's own words were replaced by
# whatever the loop saw last. Only a script-scope assignment or loop variable can do it (a function has
# its own scope), and the one allowed form is a parameter normalising itself (`$Plan = @(... $Plan)`).
$paramNames = @($scriptAst.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath })
$insideFunction = {
    param($node)
    for ($p = $node.Parent; $null -ne $p; $p = $p.Parent) {
        if ($p -is [System.Management.Automation.Language.FunctionDefinitionAst]) { return $true }
    }
    return $false
}
$clashes = @($scriptAst.FindAll({
            param($n)
            $name = $null
            if ($n -is [System.Management.Automation.Language.ForEachStatementAst]) {
                $name = $n.Variable.VariablePath.UserPath
            } elseif ($n -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                $n.Left -is [System.Management.Automation.Language.VariableExpressionAst]) {
                $name = $n.Left.VariablePath.UserPath
                $mentionsItself = @($n.Right.FindAll({
                            param($m)
                            $m -is [System.Management.Automation.Language.VariableExpressionAst] -and
                            $m.VariablePath.UserPath -eq $name
                        }, $true)).Count -gt 0
                if ($mentionsItself) { return $false }
            }
            if ($null -eq $name -or $paramNames -notcontains $name) { return $false }
            return -not (& $insideFunction $n)
        }, $true) | ForEach-Object { "line $($_.Extent.StartLineNumber): $($_.Extent.Text.Split("`n")[0].Trim())" })
Check 'no script-scope assignment or loop variable shares a parameter name' ($clashes.Count -eq 0) ($clashes -join ' / ')
# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0