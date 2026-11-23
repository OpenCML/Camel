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

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0