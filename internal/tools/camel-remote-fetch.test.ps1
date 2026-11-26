# `-FetchFile` names one file of one run, and the run's log directory used to be named after a clock
# the caller could not know. Taking "the newest directory" instead fetched the wrong file whenever
# another, shorter runner invocation appeared in the same host checkout after the real one: its
# nearly empty `summary.json` was read in place of the full one.
#
# What is fixed here: every plan run the tool starts has its own name, the host script hands it to the
# runner as CAMEL_TEST_RUN_NAME, and `-FetchFile <suffix> -FromThisRun` reads the suffix from exactly
# that run's directory. The pieces that decide the answer -- what a run is called, what a suffix may
# name, where the file lands, and the one question the host is asked -- are pure and are tested here.
#
#   powershell -NoProfile -File internal/tools/camel-remote-fetch.test.ps1
#
# Everything here is local: no host, no build, no checkout modified.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# The helper is the thing under test: when it lacks a function below, every check that needs it is red.
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

Write-Host 'camel-remote-fetch: -FetchFile <suffix> -FromThisRun'

# --- 1. what a run is called -------------------------------------------------
# A plain name, the same rule the runner applies to CAMEL_TEST_RUN_NAME; and one per invocation.
$nameA = New-CamelRunName
$nameB = New-CamelRunName
Check 'a made run name is a plain name' ((Test-CamelRunName -Name $nameA) -and (Test-CamelRunName -Name $nameB)) "$nameA / $nameB"
Check 'a made run name carries the local clock and the local pid' ($nameA -match ('^\d{8}_\d{6}_' + $PID + '_[a-z0-9]{4}$')) $nameA
Check 'two invocations get two names' ($nameA -ne $nameB) "$nameA / $nameB"
Check 'a name with a path separator is not a run name' (-not (Test-CamelRunName -Name 'a/b'))
Check 'a name that climbs is not a run name' ((-not (Test-CamelRunName -Name '..')) -and (-not (Test-CamelRunName -Name '../x')))
Check 'a backslash name is not a run name' (-not (Test-CamelRunName -Name 'a\b'))
Check 'an empty name is not a run name' (-not (Test-CamelRunName -Name ''))

# --- 2. what a suffix may name ----------------------------------------------
# A suffix names a file *inside* the run's log directory (`fail/<case>.nvm/stdout.txt`). It may not
# name the live checkout, and it may not climb out of the run: `-FromThisRun` is a way to read a log,
# not a way to fetch any file on the host.
Check 'a failure log suffix is kept as written' `
    ((Resolve-CamelFetchSuffix -Suffix 'fail/transforms/x.nvm/stdout.txt') -eq 'fail/transforms/x.nvm/stdout.txt')
Check 'a leading ./ is dropped' `
    ((Resolve-CamelFetchSuffix -Suffix './fail/x/stdout.txt') -eq 'fail/x/stdout.txt')
$empty = Refusal { Resolve-CamelFetchSuffix -Suffix '' }
Check 'an empty suffix is refused, by the flag it came from' ($empty -like '*-FetchFile*FromThisRun*') $empty
$rooted = Refusal { Resolve-CamelFetchSuffix -Suffix '/etc/passwd' }
Check 'a rooted suffix is refused' ($rooted -like '*-FetchFile*FromThisRun*') $rooted
$climb = Refusal { Resolve-CamelFetchSuffix -Suffix 'fail/../../src/main.cpp' }
Check 'a suffix that climbs out of the run is refused' ($climb -like '*-FetchFile*FromThisRun*') $climb
$backslash = Refusal { Resolve-CamelFetchSuffix -Suffix 'fail\x\stdout.txt' }
Check 'a backslash suffix is refused: the host splits on /' ($backslash -like '*-FetchFile*FromThisRun*') $backslash

# --- 3. where it lands -------------------------------------------------------
# The host path is the run's own directory; the local path keeps the suffix as its tail, so a reader
# finds the file where the same words name it on the host.
$paths = Get-CamelFetchPaths -LogRootOnHost '/srv/camel/test/results/log' `
    -Run '20261124_123343_3556634_ab12' -Suffix 'fail/probe/stdout.txt' `
    -LocalRoot 'C:\Projects\Camel-wt\refgen\test\results\fetched'
Check 'the host path is the named run plus the suffix' `
    ($paths.HostPath -eq '/srv/camel/test/results/log/20261124_123343_3556634_ab12/fail/probe/stdout.txt') `
    $paths.HostPath
Check 'the local path keeps the suffix as its tail' `
    ($paths.LocalPath -eq 'C:\Projects\Camel-wt\refgen\test\results\fetched\fail\probe\stdout.txt') `
    $paths.LocalPath

# --- 4. the one question the host is asked -----------------------------------
# One command about one directory: it prints that run's absolute path when the directory is there and
# nothing when it is not. It never lists the log root, so no other run can be answered in its place.
$root = '/srv/camel/test/results/log'
$cmd = New-CamelRunDirCommand -LogRootOnHost $root -Run '20261124_123343_3556634_ab12'
Check 'the command names the one run directory' ($cmd -like "*$root/20261124_123343_3556634_ab12*") $cmd
Check 'the command does not list the log root' (($cmd -notlike '*find*') -and ($cmd -notlike '*tail*') -and ($cmd -notlike '*sort*')) $cmd
$badRun = Refusal { New-CamelRunDirCommand -LogRootOnHost $root -Run '../x' }
Check 'a run name that climbs is refused before any command is written' ($badRun -like '*not a plain run name*') $badRun

# The command run for real against two run directories, the named one the older. Needs a POSIX shell;
# the host's own is one, and Git's bash stands in for it here.
. (Join-Path $PSScriptRoot 'git-bash.ps1')
$bash = Get-GitBash
if ($null -eq $bash) {
    Write-Host '  SKIP  the command against a planted tree: no Git for Windows bash found'
} else {
    $tree = Join-Path ([System.IO.Path]::GetTempPath()) ('camel-fetch-test-' + [guid]::NewGuid().ToString('N'))
    try {
        $logRoot = Join-Path $tree 'log'
        $older = '20261124_100000_111_aaaa'
        $newer = '20261124_110000_222_bbbb'
        foreach ($run in @($older, $newer)) {
            New-Item -ItemType Directory -Path (Join-Path $logRoot $run) -Force | Out-Null
        }
        Set-Content -LiteralPath (Join-Path (Join-Path $logRoot $older) 'summary.json') -Value 'full'
        Set-Content -LiteralPath (Join-Path (Join-Path $logRoot $newer) 'summary.json') -Value 'short'
        # Git's bash reads `C:/x/y` as a path as it is, so forward slashes are enough.
        $posixRoot = $logRoot -replace '\\', '/'

        $answer = (& $bash -c (New-CamelRunDirCommand -LogRootOnHost $posixRoot -Run $older) | Out-String).Trim()
        Check 'the named run answers with its own directory, though a newer one exists' ($answer -like "*$older") $answer
        $missing = (& $bash -c (New-CamelRunDirCommand -LogRootOnHost $posixRoot -Run '20261124_120000_333_cccc') | Out-String).Trim()
        Check 'a run directory that is not there answers nothing, not the newest one' ($missing -eq '') $missing
    } finally {
        Remove-Item -LiteralPath $tree -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# --- 5. the sentence a caller reads ----------------------------------------
# The whole point: a refusal names the flag, the shape of a suffix, and the words that ask for it.
Check 'the refusal says which words ask for it' `
    ($script:CamelFetchUsage -like '*-FetchFile *-FromThisRun*' -and $script:CamelFetchUsage -like '*fail/*') `
    $script:CamelFetchUsage

# --- 6. the tool itself ------------------------------------------------------
# The generated host script and the fetch after it have to agree on the run's name, and the old flag
# is gone. The host script is a string inside the tool, so its text is read.
$tool = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'camel-remote.ps1') -Raw
Check 'the host script exports the run name to the runner' ($tool -like '*export CAMEL_TEST_RUN_NAME=''$runName''*')
Check 'the run name is printed' ($tool.Contains('[camel-remote] run name: '))
Check 'the fetch asks the host about the run it named' ($tool -like '*New-CamelRunDirCommand -LogRootOnHost $remoteLogRoot -Run $runName*')
Check 'a missing run directory is reported and nothing is fetched' ($tool -like '*the host has no run directory $runName*nothing was fetched*')
Check 'the old flag is gone' (($tool -notmatch '\$Latest\b') -and ($tool -notmatch '-Latest\b'))

Write-Host ''
if ($script:failed -eq 0) {
    Write-Host "PASS camel-remote-fetch: $($script:checked) check(s)"
    exit 0
}
Write-Host "FAIL camel-remote-fetch: $($script:failed) of $($script:checked) check(s)"
exit 1
