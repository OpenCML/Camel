# `-FetchFile` names one file of one run, and the run's log directory is named after the clock: a
# caller that wants the log of the failure it just read has no way to spell it. Tonight two lanes
# each had to downgrade a conclusion to "推断" because the one line the console keeps is a prefix.
#
# What is fixed here: `-FetchFile <suffix> -Latest` resolves the suffix against the **newest** run's
# log directory on the host, so the file a reader just saw named can be fetched without knowing the
# timestamp. The pieces that decide the answer -- which run is newest, what a suffix may name, and
# where the file lands -- are pure and are tested here; the host is asked one command, by the tool.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-fetch.test.ps1
#
# Everything here is local: no host, no build, no checkout modified.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# The helper is the thing under test: without it every check below is red, which is what the first
# run of this file is for (the tool did not have `-Latest` before this change).
$helper = Join-Path $PSScriptRoot 'camel-remote-fetch.ps1'
if (Test-Path $helper) {
    . $helper
}

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

function Refusal {
    param([scriptblock]$Do)
    try {
        & $Do | Out-Null
        return ''
    } catch {
        return $_.Exception.Message
    }
}

Write-Host 'camel-remote-fetch: -FetchFile <suffix> -Latest'

# --- 1. which run is newest --------------------------------------------------
# The names are the run's clock, so the order the names sort in is the order the runs happened in.
# Read ordinally: a culture sort of `20261124_123343_3556634` and `20261124_9320_1` would put the
# shorter second field first, and the run that happened second would be named first.
$runs = @('20261124_122434_3256799', '20261124_123343_3556634', '20261124_123619_3581394')
Check 'the newest of three runs is named' `
    ((Select-CamelLatestRun -Runs $runs) -eq '20261124_123619_3581394') `
    "got '$((Select-CamelLatestRun -Runs $runs))'"
Check 'a culture sort would name 9320 last, the ordinal answer does not' `
    ((Select-CamelLatestRun -Runs @('20261124_9320_1', '20261124_9320_2')) -eq '20261124_9320_2')
Check 'no run at all answers nothing, and says so' `
    ((Select-CamelLatestRun -Runs @()) -eq '') `
    "got '$((Select-CamelLatestRun -Runs @()))'"
Check 'a plain null answers nothing too' ((Select-CamelLatestRun -Runs $null) -eq '')

# --- 2. what a suffix may name ----------------------------------------------
# A suffix names a file *inside* the run's log directory (`fail/<case>.nvm/stdout.txt`). It may not
# name the live checkout, and it may not climb out of the run: `-Latest` is a way to read a log, not
# a way to fetch any file on the host.
Check 'a failure log suffix is kept as written' `
    ((Resolve-CamelFetchSuffix -Suffix 'fail/transforms/x.nvm/stdout.txt') -eq 'fail/transforms/x.nvm/stdout.txt')
Check 'a leading ./ is dropped' `
    ((Resolve-CamelFetchSuffix -Suffix './fail/x/stdout.txt') -eq 'fail/x/stdout.txt')
$empty = Refusal { Resolve-CamelFetchSuffix -Suffix '' }
Check 'an empty suffix is refused, by the flag it came from' ($empty -like '*-FetchFile*Latest*') $empty
$rooted = Refusal { Resolve-CamelFetchSuffix -Suffix '/etc/passwd' }
Check 'a rooted suffix is refused' ($rooted -like '*-FetchFile*Latest*') $rooted
$climb = Refusal { Resolve-CamelFetchSuffix -Suffix 'fail/../../src/main.cpp' }
Check 'a suffix that climbs out of the run is refused' ($climb -like '*-FetchFile*Latest*') $climb
$backslash = Refusal { Resolve-CamelFetchSuffix -Suffix 'fail\x\stdout.txt' }
Check 'a backslash suffix is refused: the host splits on /' ($backslash -like '*-FetchFile*Latest*') $backslash

# --- 3. where it lands -------------------------------------------------------
# The host path is the run's own directory; the local path keeps the suffix as its tail, so a reader
# finds the file where the same words name it on the host.
$paths = Get-CamelFetchPaths -LogRootOnHost '/srv/camel/test/results/log' `
    -Run '20261124_123343_3556634' -Suffix 'fail/probe/stdout.txt' `
    -LocalRoot 'D:\Projects\Camel-wt\refgen\test\results\fetched'
Check 'the host path is the newest run plus the suffix' `
    ($paths.HostPath -eq '/srv/camel/test/results/log/20261124_123343_3556634/fail/probe/stdout.txt') `
    $paths.HostPath
Check 'the local path keeps the suffix as its tail' `
    ($paths.LocalPath -eq 'D:\Projects\Camel-wt\refgen\test\results\fetched\fail\probe\stdout.txt') `
    $paths.LocalPath

# --- 4. the one command the host is asked -----------------------------------
# One command, and it must not depend on a glob matching: a run directory that matches nothing has to
# answer nothing rather than leave `find` printing a literal `*` that then reads as a run name.
$cmd = New-CamelLatestRunCommand -LogRootOnHost '/srv/camel/test/results/log'
Check 'the command names the log root' ($cmd -like "*/srv/camel/test/results/log*") $cmd
Check 'the command asks for directories only' (($cmd -like '* -type d*') -and ($cmd -like '*maxdepth 1*')) $cmd
Check 'the command ends on one line' ($cmd -like '*tail -n 1*') $cmd

# --- 5. the sentence a caller reads ----------------------------------------
# The whole point: a refusal names the flag, the shape of a suffix, and the two words that ask for it.
$sentence = 'camel-remote: -FetchFile -Latest needs a suffix inside the run log directory'
Check 'the refusal says which words ask for it' `
    ($script:CamelFetchUsage -like '*-FetchFile *-Latest*' -and $script:CamelFetchUsage -like '*fail/*') `
    $script:CamelFetchUsage

Write-Host ''
if ($script:failed -eq 0) {
    Write-Host "PASS camel-remote-fetch: $($script:checked) check(s)"
    exit 0
}
Write-Host "FAIL camel-remote-fetch: $($script:failed) of $($script:checked) check(s)"
exit 1