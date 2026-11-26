# The guard for `expect_diagnostic_file`, the one plan field that says *which file* a diagnostic
# came from.
#
# Why it exists: `expect_diagnostic_name`, `_code`, `_line` and `_column` say what a diagnostic says
# and where in it a position is, and none of them says which file reported it -- so a pin written
# against one file is satisfied by the same sentence in any file the program loaded. The field
# closes that hole (`operator_overloads.conflict_import` was green for weeks while it pinned a line
# in `plus_c.cml` and the entry's own file carried a different diagnostic at the same
# coordinates), and it ships with four refusals: a list, a number, an empty string, and an entry
# that pairs it with `expect_no_diagnostics` are plans the runner cannot read, refused where the
# plan is read, not where the run fails.
#
# A field with no standing test is a field that can be removed by accident. The commit that added it
# carried its readings in the message and nothing that runs; this file is the part that runs.
# It is here, in the tool self-test family, and not inside `check-plan-fields.mjs`, because it needs
# no build and no plan of this tree: every fixture is written under the system temp directory and
# removed again, and the entries use `tool = "node"` with a probe that prints one diagnostic in the
# runner's own short format, so the comparison under test is the runner's and not the compiler's.
#
# What it proves, in the two directions the field has and the third it must not have:
#   (a) the file the diagnostic is in, written down, passes;
#   (b) another file, written down, fails, and the failure names the file that was required and the
#       file the diagnostic was actually reported in;
#   (c) a list, a number, an empty string, and `expect_no_diagnostics` beside it are refused while
#       the plan is read -- the program never starts (the fixture marker is the witness);
#   (d) an entry that does not write the field keeps exactly the meaning it had, even when the
#       diagnostic comes from another file.
#
#   pwsh -NoProfile -File internal/tools/run-tests-diagnostic-file.test.ps1
#   pwsh -NoProfile -File internal/tools/run-tests-diagnostic-file.test.ps1 -Keep   (leave the fixture)
#
# No build is made and no plan of this repository is run. The runner's own preflight still asks for
# an install (`--camel`): when this tree has no `out/latest/bin/camel.exe`, the node interpreter is
# named instead, and no entry here ever hands anything to it.

[CmdletBinding()]
param(
    # Keep the fixture tree for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
# A child that exits 1 is this test's normal reading, not an exception: keep Windows PowerShell's
# older native-command semantics where that switch exists.
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$script:checked = 0
$script:failed = 0

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

function ConvertTo-PlainText {
    param([string]$Text)
    # [char]27, not "`e": Windows PowerShell 5.1 has no `e escape.
    $Text -replace ([string][char]27 + '\[[0-9;]*m'), ''
}

$script:repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:runner = Join-Path $script:repo 'test\tools\run-tests.mjs'
$script:marker = $null

# --- the install the runner's preflight asks for ------------------------------------------------
# Nothing in this fixture runs the compiler: every entry says `tool = "node"`. `--camel` is still
# needed because `preflightBuild` refuses to start a run without an install, so an existing path is
# enough. A real install is preferred so a reader never mistakes this run for a measured one.
$realInstall = Join-Path $script:repo 'out\latest\bin\camel.exe'
if (Test-Path -LiteralPath $realInstall) {
    $script:camel = $realInstall
} else {
    $script:camel = (Get-Command node -ErrorAction Stop).Source
    Write-Host "[INFO] no out/latest/bin/camel.exe in this tree: --camel names $($script:camel), and no entry here runs it"
}

# A failed run keeps its scratch root as evidence. This fixture produces failures on purpose, so it
# takes back the roots it made -- exactly the ones the run named, under this checkout's tmp/test --
# and leaves a root another run is holding alone.
function Remove-KeptScratch {
    param([string]$Text)
    $root = [System.IO.Path]::GetFullPath((Join-Path $script:repo 'tmp\test')) + [System.IO.Path]::DirectorySeparatorChar
    foreach ($match in [regex]::Matches($Text, 'test scratch kept: ([^\r\n]+)')) {
        $kept = $match.Groups[1].Value.Trim()
        try { $full = [System.IO.Path]::GetFullPath($kept) } catch { continue }
        if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase) -and $full -match '\\run-\d+$') {
            Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Invoke-PlanRun {
    param([string]$Plan)
    $out = & node $script:runner $Plan --camel $script:camel --jobs 1 2>&1
    $code = $LASTEXITCODE
    $text = ConvertTo-PlainText ((@($out | ForEach-Object { "$_" })) -join "`n")
    Remove-KeptScratch $text
    [pscustomobject]@{ Code = $code; Text = $text }
}

function Reset-Marker {
    Remove-Item -LiteralPath $script:marker -Force -ErrorAction SilentlyContinue
}

function Test-Marker {
    Test-Path -LiteralPath $script:marker
}

# --- the fixture --------------------------------------------------------------------------------
# One program reports one diagnostic, in `plus_c.cml`, at 3:1. It also writes a marker, so "the plan
# was refused before the program started" is a reading and not an inference from the exit code.
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ('camel-diagnostic-file-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$script:marker = Join-Path $scratch 'marker.txt'
New-Item -ItemType Directory -Path $scratch -Force | Out-Null

function Write-FixtureFile {
    param([string]$Name, [string]$Text)
    [System.IO.File]::WriteAllText(
        (Join-Path $scratch $Name),
        "$Text`n",
        (New-Object System.Text.UTF8Encoding($false))
    )
}

$probe = @'
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

// The diagnostic the runner reads: one line in the short form, reported in `plus_c.cml`.
const here = path.dirname(fileURLToPath(import.meta.url))
fs.writeFileSync(path.join(here, 'marker.txt'), 'the probe ran\n')
const file = path.join(here, 'plus_c.cml')
console.log(`${file}:3:1: [error]: a conflict (name=SemanticDiag::OperatorOverloadConflict, code=0x01234567)`)
'@
Write-FixtureFile -Name 'probe.mjs' -Text $probe
Write-FixtureFile -Name 'plus_c.cml' -Text 'let plus_c = 1'
Write-FixtureFile -Name 'conflict_import.cml' -Text 'let conflict_import = 1'

# One plan per reading, each with the same entry and a different `expect_diagnostic_file` line. The
# plan is a file of its own so a shape the loader refuses cannot hide behind a sibling entry.
function New-Plan {
    param([string]$File, [string]$Id, [string]$Name, [string]$Field)
    $text = @'
id = "<ID>"
tier = "fast"

[[tests]]
name = "<NAME>"
kind = "behavior"
tool = "node"
case = "probe.mjs"
args = ["${case}"]
expect_exit = 0
expect_diagnostic_name = "SemanticDiag::OperatorOverloadConflict"
expect_diagnostic_line = 3
expect_diagnostic_column = 1
<FIELD>
'@
    $text = $text.Replace('<ID>', $Id).Replace('<NAME>', $Name).Replace('<FIELD>', $Field)
    $path = Join-Path $scratch $File
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
    $path
}

$rightPlan = New-Plan -File 'right.plan.toml' -Id 'guard.right' -Name 'right_file' `
    -Field 'expect_diagnostic_file = "plus_c.cml"'
$wrongPlan = New-Plan -File 'wrong.plan.toml' -Id 'guard.wrong' -Name 'wrong_file' `
    -Field 'expect_diagnostic_file = "conflict_import.cml"'

$absField = "expect_diagnostic_file = '" + ((Join-Path $scratch 'plus_c.cml') -replace '\\', '/') + "'"
$absolutePlan = New-Plan -File 'absolute.plan.toml' -Id 'guard.absolute' -Name 'absolute_file' -Field $absField

$unwrittenPlan = New-Plan -File 'unwritten.plan.toml' -Id 'guard.unwritten' -Name 'no_file_field' -Field ''

$plus = Join-Path $scratch 'plus_c.cml'
$import = Join-Path $scratch 'conflict_import.cml'

# --- (a) the right file ------------------------------------------------------------------------
Reset-Marker
$right = Invoke-PlanRun $rightPlan
Check '(a) the required file, written down, passes' ($right.Code -eq 0) "exit $($right.Code)`n$($right.Text)"
Check '(a) the entry is PASS and the summary says nothing failed' `
    ($right.Text -match '\[PASS\] right_file\b' -and $right.Text -match '\[SUMMARY\] total: 1, pass: 1, fail: 0') $right.Text
Check '(a) the program ran: the fixture marker exists' (Test-Marker)

# The value may also be absolute, as written. Forward slashes keep it a TOML literal string and the
# runner resolves both sides (`path.resolve`) before it compares.
Reset-Marker
$absolute = Invoke-PlanRun $absolutePlan
Check '(a) an absolute path to the same file passes' `
    ($absolute.Code -eq 0 -and $absolute.Text -match '\[PASS\] absolute_file\b') "exit $($absolute.Code)`n$($absolute.Text)"

# --- (b) another file ---------------------------------------------------------------------------
Reset-Marker
$wrong = Invoke-PlanRun $wrongPlan
Check '(b) the wrong file fails the entry' ($wrong.Code -eq 1) "exit $($wrong.Code)`n$($wrong.Text)"
Check '(b) the failure is the entry, not the plan: [FAIL] wrong_file' ($wrong.Text -match '\[FAIL\] wrong_file\b') $wrong.Text
Check '(b) the message names the file the entry requires' `
    ($wrong.Text.IndexOf($import, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) $wrong.Text
Check '(b) the message names the file the diagnostic was reported in' `
    ($wrong.Text.IndexOf($plus, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) $wrong.Text
Check '(b) and says the second is where it is, not the file required' `
    ($wrong.Text -match 'that diagnostic is reported in .* not in the file the entry requires') $wrong.Text
Check '(b) the summary counts one failed entry' ($wrong.Text -match '\[SUMMARY\] total: 1, pass: 0, fail: 1') $wrong.Text
Check '(b) the program did run: the fixture marker exists' (Test-Marker)

# --- (d) no field written: the old meaning, unchanged -------------------------------------------
# The diagnostic is in `plus_c.cml` and the entry's own program is `probe.mjs`; without the field the
# pin says "this sentence, at 3:1, anywhere in what the run printed" and that is still what it means.
Reset-Marker
$unwritten = Invoke-PlanRun $unwrittenPlan
Check '(d) an entry that does not write the field keeps its old meaning and passes' `
    ($unwritten.Code -eq 0 -and $unwritten.Text -match '\[PASS\] no_file_field\b') "exit $($unwritten.Code)`n$($unwritten.Text)"

# --- (c) the shapes refused where the plan is read ----------------------------------------------
# Each of these is a plan the runner cannot read. The reading is the same shape every time: exit 1,
# one `[FAIL] <invalid plan>` row (the plan, not an entry), the key named with the plan file and the
# test, and -- the part that separates "refused where read" from "failed where run" -- no marker.
$shapes = @(
    @{
        Key = 'array'; Field = 'expect_diagnostic_file = ["plus_c.cml"]'
        Reading = 'a list would let any of several files satisfy the pin'
        Keys = 'expect_diagnostic_file'
    },
    @{
        Key = 'number'; Field = 'expect_diagnostic_file = 3'
        Reading = 'expect_diagnostic_file must be a string naming one file, got 3'
        Keys = 'expect_diagnostic_file'
    },
    @{
        Key = 'empty'; Field = 'expect_diagnostic_file = ""'
        Reading = 'an empty string names no file'
        Keys = 'expect_diagnostic_file'
    },
    @{
        Key = 'conflict'; Field = "expect_diagnostic_file = `"plus_c.cml`"`nexpect_no_diagnostics = true"
        Reading = 'cannot both require a diagnostic in a named file and require no diagnostic at all'
        Keys = 'expect_no_diagnostics, expect_diagnostic_file'
    }
)

foreach ($shape in $shapes) {
    $plan = New-Plan -File "$($shape.Key).plan.toml" -Id "guard.$($shape.Key)" -Name $shape.Key -Field $shape.Field
    Reset-Marker
    $run = Invoke-PlanRun $plan
    $planName = [System.IO.Path]::GetFileName($plan)
    Check "(c) $($shape.Key): the plan is refused, exit 1" ($run.Code -eq 1) "exit $($run.Code)`n$($run.Text)"
    Check "(c) $($shape.Key): the refusal is the plan's, [FAIL] <invalid plan>" `
        ($run.Text -match '\[FAIL\] <invalid plan>') $run.Text
    Check "(c) $($shape.Key): the message names the plan file" `
        ($run.Text.IndexOf($planName, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) $run.Text
    Check "(c) $($shape.Key): the message names the entry and the key(s): $($shape.Keys)" `
        ($run.Text -match ("test '" + [regex]::Escape($shape.Key) + "'") -and
            ($shape.Keys.Split(',') | ForEach-Object { $run.Text.Contains($_.Trim()) }) -notcontains $false) $run.Text
    Check "(c) $($shape.Key): the message says what is wrong" ($run.Text.Contains($shape.Reading)) $run.Text
    Check "(c) $($shape.Key): the program never started -- the fixture marker is absent" (-not (Test-Marker)) $run.Text
}

# --- result --------------------------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
else { Write-Host "fixture kept: $scratch" }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0