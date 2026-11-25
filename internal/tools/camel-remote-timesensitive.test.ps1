# The gate's reader of `time_sensitive` against a second, independent reader of the same files.
#
# The declaration decides whether a run takes the whole host. Exactly one program acts on it
# (`internal/tools/camel-remote-timesensitive.ps1`), so "the gate read that plan right" has never
# been anything but the gate agreeing with itself. This file is the comparison: every *.plan.toml in
# the tree is read twice -- once by that function, once by `test/tools/time-sensitive-reader.mjs`,
# which shares nothing with it but the file -- and the boundary shapes are read by both as well.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-timesensitive.test.ps1
#   pwsh -NoProfile -File internal/tools/camel-remote-timesensitive.test.ps1 -Keep   (leave the fixtures)
#
# It runs on this workstation and needs no host, no build and no plan, like the other `.test.ps1`
# files under `internal/tools/`: the build host has no pwsh, which is why the ledger records this
# class instead of a plan running it (`SELF_CHECK_EXCEPTIONS` in `scripts/plan-inventory.mjs`).
#
# ## What a failure means
#
#   a plan file the two readers disagree on   the gate's answer is not trustworthy for that file. The
#                                            row names the file, both answers and the declaration
#                                            each one saw. Nothing is fixed here: this file only reads.
#   a control row that stopped agreeing       a case where both readers are known to be right (the
#                                            plain declaration, `false`, a trailing comment) is the
#                                            positive control: if these agreed by being equally wrong,
#                                            the comparison would say so here and not in the tree.
#   a recorded divergence that went away      the case is in `$divergences` because the two readers
#                                            disagreed when the reading was taken. When they agree,
#                                            this is red on purpose: it says which row to delete and
#                                            which side moved. A debt that ended silently would leave
#                                            a table claiming a disagreement that is not there.
#
# The verdicts below are what the two readers answered on 2026-11-25; which side is right is decided
# by the file format, and that argument is in `internal/reports/2026-11-25-time-sensitive-readers.cn.md`
# (the table of this run), not in a comment per row.

[CmdletBinding()]
param(
    # Keep the fixture directory for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..')).Path
. (Join-Path $here 'camel-remote-timesensitive.ps1')
$reader = Join-Path $root 'test/tools/time-sensitive-reader.mjs'

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

function Write-Reading {
    param([string]$Line)
    Write-Host $Line
}

# The plan files of this tree, relative and with `/` separators, in one order both readers are given.
# Skipped directories are the ones that hold another tree's copy of the same names.
function Get-PlanFiles {
    param([string]$TreeRoot)
    $skip = '\\(\.git|node_modules|out)\\'
    $files = Get-ChildItem -LiteralPath $TreeRoot -Recurse -Filter '*.plan.toml' -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch $skip } |
        ForEach-Object { $_.FullName.Substring($TreeRoot.Length + 1).Replace('\', '/') } |
        Sort-Object
    @($files)
}

# The second reader, over one list of files: `path` -> its row. One node process, because the list is
# the population and a per-file process would let the two sides read different populations.
function Read-SecondReader {
    param([string]$TreeRoot, [string[]]$Files)
    $list = Join-Path $script:scratch 'files.txt'
    [System.IO.File]::WriteAllLines($list, [string[]]$Files)
    $json = & node $reader --json --root $TreeRoot --files-from $list
    if ($LASTEXITCODE -ne 0) {
        throw "the second reader exited $LASTEXITCODE on $($Files.Count) file(s): $json"
    }
    $document = $json | ConvertFrom-Json
    $byPath = @{}
    foreach ($row in $document.files) { $byPath["$($row.path)"] = $row }
    $byPath
}

$script:scratch = Join-Path ([System.IO.Path]::GetTempPath()) ("camel-ts-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $script:scratch -Force | Out-Null
try {

    # --- 1. the wiring: one implementation, called, not copied -------------------
    # The move of the read into its own file is only worth anything if the gate still calls it and
    # carries no copy of the sentences. Both halves are read here, because a second copy is exactly
    # the defect this file exists to catch (two readers, and the one that acts is unverified).
    $gatePath = Join-Path $here 'camel-remote.ps1'
    $helperPath = Join-Path $here 'camel-remote-timesensitive.ps1'
    $gateText = [System.IO.File]::ReadAllText($gatePath)
    $helperText = [System.IO.File]::ReadAllText($helperPath)
    $calls = [regex]::Matches($gateText, 'Get-TimeSensitivePlans').Count
    $copies = [regex]::Matches($gateText, [regex]::Escape("'^\s*time_sensitive\s*=\s*true")).Count
    $implementations = [regex]::Matches($helperText, [regex]::Escape("'^\s*time_sensitive\s*=\s*true")).Count
    Check 'the gate calls the shared reader' ($calls -ge 1) "Get-TimeSensitivePlans appears $calls time(s) in camel-remote.ps1"
    Check 'the gate carries no copy of the declaration pattern' ($copies -eq 0) "the pattern appears $copies time(s) in camel-remote.ps1"
    Check 'the shared reader carries it exactly once' ($implementations -eq 1) "the pattern appears $implementations time(s) in camel-remote-timesensitive.ps1"
    Check 'the gate dot-sources the file it calls' ($gateText.Contains("camel-remote-timesensitive.ps1")) 'camel-remote.ps1 never names camel-remote-timesensitive.ps1'

    # --- 1b. the target kinds: what the read is asked about ---------------------
    # The first five sentences of the reader are about the *target*, not about the file: an empty
    # one, a relative one, an absolute one, a directory, one that names nothing. A refactor of this
    # function is where those would go wrong silently -- the symptom being a plan that declares
    # itself time sensitive and is not read as one, which is the whole subject of this file.
    $targets = Join-Path $script:scratch 'targets'
    New-Item -ItemType Directory -Path (Join-Path $targets 'a-dir') -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $targets 'declares.plan.toml'), "time_sensitive = true`n")
    [System.IO.File]::WriteAllText((Join-Path $targets 'a-dir\inner.plan.toml'), "time_sensitive = true`n")
    $byName = @(Get-TimeSensitivePlans -Root $targets -Plan @('declares.plan.toml'))
    Check 'a relative target is resolved against the root and read' ($byName.Count -eq 1 -and $byName[0] -eq 'declares.plan.toml') ($byName -join ',')
    $byRooted = @(Get-TimeSensitivePlans -Root (Join-Path $script:scratch 'no-such-root') -Plan @((Join-Path $targets 'declares.plan.toml')))
    Check 'an absolute target is read without the root' ($byRooted.Count -eq 1) ($byRooted -join ',')
    $byDir = @(Get-TimeSensitivePlans -Root $targets -Plan @('a-dir'))
    Check 'a directory target carries no declaration of its own' ($byDir.Count -eq 0) ($byDir -join ',')
    $byNone = @(Get-TimeSensitivePlans -Root $targets -Plan @('', '  ', 'missing.plan.toml'))
    Check 'an empty or missing target is skipped' ($byNone.Count -eq 0) ($byNone -join ',')
    $trimmed = @(Get-TimeSensitivePlans -Root $targets -Plan @(' declares.plan.toml '))
    Check 'a target is trimmed before it is resolved' ($trimmed.Count -eq 1) ($trimmed -join ',')

    # --- 2. every plan file in the tree, read twice ------------------------------
    $files = Get-PlanFiles -TreeRoot $root
    Write-Reading "[tree] $($files.Count) plan file(s) under $root"
    $declaredByGate = @(Get-TimeSensitivePlans -Root $root -Plan $files)
    $second = Read-SecondReader -TreeRoot $root -Files $files
    Check 'the two readers were given the same files' ($second.Count -eq $files.Count) "the gate read $($files.Count) file(s) and the second reader $($second.Count)"

    $mismatches = @()
    $gateTrue = @()
    foreach ($file in $files) {
        $gateSays = $declaredByGate -contains $file
        if ($gateSays) { $gateTrue += $file }
        $row = $second[$file]
        if ($null -eq $row) {
            $mismatches += "${file}: the second reader has no row"
            continue
        }
        $otherSays = [bool]$row.gate
        if ($gateSays -ne $otherSays) {
            $mismatches += ("{0}: gate={1} second={2} state={3} line={4}" -f $file, $gateSays, $otherSays, $row.state, $row.line)
        }
    }
    Write-Reading ("[tree] gate=true: {0} file(s){1}" -f $gateTrue.Count, $(if ($gateTrue.Count -gt 0) { ' -- ' + ($gateTrue -join ', ') } else { '' }))
    Check 'every plan file gets the same answer from both readers' ($mismatches.Count -eq 0) (($mismatches | Select-Object -First 8) -join "`n        ")
    Check 'the plan that declares itself time sensitive is found by both' (
        ($gateTrue -contains 'test/plans/perf/fib.plan.toml') -and
        ($second['test/plans/perf/fib.plan.toml']) -and ([bool]$second['test/plans/perf/fib.plan.toml'].gate)
    ) "gate contains it: $($gateTrue -contains 'test/plans/perf/fib.plan.toml'); second reader: $(if ($second['test/plans/perf/fib.plan.toml']) { $second['test/plans/perf/fib.plan.toml'].gate } else { 'no row' })"

    # --- 3. the boundary shapes, read by both ------------------------------------
    # Four kinds of input the dispatch names -- an empty value, a quoted value, mixed case, and a value
    # surrounded by whitespace -- plus a repeated marker, plus the two shapes the two readers turned out
    # to read differently. `agree` rows are the positive control (both are known to be right, so a
    # comparison that cannot tell these apart cannot tell the others apart either); `diverge` rows are
    # the recorded disagreements, and each one is red when it stops being one.
    $fixtures = @(
        @{ name = 'f01_empty_value'; kind = 'agree'; lines = @('id = "x"', 'time_sensitive ='); gate = $false; note = 'the value is missing: TOML has no empty value, and the runner refuses the plan' }
        @{ name = 'f02_quoted'; kind = 'agree'; lines = @('time_sensitive = "true"'); gate = $false; note = 'a string, not a boolean: the gate does not read it, the runner refuses it by type' }
        @{ name = 'f03_mixed_case'; kind = 'diverge'; lines = @('time_sensitive = True'); gate = $false; note = 'the gate reads it (PowerShell `-match` is case-insensitive); TOML spells the boolean lowercase only' }
        @{ name = 'f04_whitespace'; kind = 'agree'; lines = @('   time_sensitive   =   true   '); gate = $true; note = 'leading, trailing and inner whitespace' }
        @{ name = 'f05_duplicate'; kind = 'agree'; lines = @('time_sensitive = true', 'time_sensitive = true'); gate = $true; note = 'written twice: not TOML, so the runner refuses the plan; both readers still read the value' }
        @{ name = 'f06_bracket_in_string'; kind = 'diverge'; lines = @('desc = """', '[note]', '"""', 'time_sensitive = true'); gate = $true; note = 'a line starting with `[` inside a triple-quoted string: the gate stops scanning there and misses the declaration' }
        @{ name = 'f07_nested_in_table'; kind = 'agree'; lines = @('[defaults]', 'time_sensitive = true'); gate = $false; note = 'inside a table: the value belongs to that table, and [defaults] has no such field' }
        @{ name = 'f08_quoted_key'; kind = 'diverge'; lines = @('"time_sensitive" = true'); gate = $true; note = 'a quoted key is the same key to TOML, and not the same line to the gate' }
        @{ name = 'f09_in_prose'; kind = 'diverge'; lines = @('desc = """', 'time_sensitive = true', '"""'); gate = $false; note = 'the words inside a string: the gate reads them as a declaration, the second reader does not' }
        @{ name = 'f10_control_true'; kind = 'agree'; lines = @('time_sensitive = true'); gate = $true; note = 'the plain declaration, as test/plans/perf/fib.plan.toml writes it' }
        @{ name = 'f11_control_false'; kind = 'agree'; lines = @('time_sensitive = false'); gate = $false; note = 'the other boolean' }
        @{ name = 'f12_control_absent'; kind = 'agree'; lines = @('id = "x"'); gate = $false; note = 'no declaration at all' }
        @{ name = 'f13_trailing_comment'; kind = 'agree'; lines = @('time_sensitive = true # durations'); gate = $true; note = 'a comment after the value' }
        @{ name = 'f14_no_spaces'; kind = 'agree'; lines = @('time_sensitive=true'); gate = $true; note = 'no spaces around the `=`' }
        @{ name = 'f15_nested_array'; kind = 'diverge'; lines = @('ids = [', '  [1, 2],', ']', 'time_sensitive = true'); gate = $true; note = 'an array element starting with `[`: the gate takes it for the end of the top level' }
    )
    $fixtureDir = Join-Path $script:scratch 'fixtures'
    New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
    foreach ($fixture in $fixtures) {
        $file = Join-Path $fixtureDir ($fixture.name + '.plan.toml')
        [System.IO.File]::WriteAllText($file, (($fixture.lines -join "`n") + "`n"))
    }
    $fixtureNames = @($fixtures | ForEach-Object { $_.name + '.plan.toml' })
    $fixtureGate = @(Get-TimeSensitivePlans -Root $fixtureDir -Plan $fixtureNames)
    $fixtureSecond = Read-SecondReader -TreeRoot $fixtureDir -Files $fixtureNames

    Write-Reading ''
    Write-Reading '[boundary] fixture                        A      B      second state   problem'
    foreach ($fixture in $fixtures) {
        $file = $fixture.name + '.plan.toml'
        $row = $fixtureSecond[$file]
        $a = if ($fixtureGate -contains $file) { 'true ' } else { 'false' }
        $b = if ($null -ne $row -and [bool]$row.gate) { 'true ' } else { 'false' }
        $state = if ($null -ne $row) { $row.state } else { '?' }
        $problem = if ($null -ne $row -and $row.problem) { $row.problem } else { '' }
        Write-Reading ("           {0,-34}{1}  {2}  {3,-13}  {4}" -f $fixture.name, $a, $b, $state, $problem)
        $same = ($a.Trim() -eq $b.Trim())
        if ($fixture.kind -eq 'agree') {
            Check ("$($fixture.name): both readers say gate=$($fixture.gate) -- $($fixture.note)") (
                $same -and ($a.Trim() -eq $(if ($fixture.gate) { 'true' } else { 'false' }))
            ) "gate=$($a.Trim()) second=$($b.Trim()) state=$state"
        } else {
            Check ("$($fixture.name): still the recorded divergence (gate=$($a.Trim()), second=$($b.Trim())) -- $($fixture.note); delete this row when they agree") (
                -not $same
            ) "the two readers agree on gate=$($a.Trim()) now: this row recorded a disagreement and the disagreement is gone"
        }
    }
    Write-Reading ''

} finally {
    if ($Keep) {
        Write-Host "fixtures kept in $script:scratch"
    } else {
        Remove-Item -LiteralPath $script:scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "$script:checked checks, $script:failed failed"
exit $(if ($script:failed -gt 0) { 1 } else { 0 })