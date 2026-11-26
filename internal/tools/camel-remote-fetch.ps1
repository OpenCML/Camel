# `-FetchFile <suffix> -FromThisRun`: the pieces of it that can be decided without a host.
#
# The trouble this answers: a run's log lives in a directory on the host
# (`test/results/log/<run>/fail/<case>.nvm/stdout.txt`, `summary.json`), the console keeps one
# truncated line of it, and the run's directory name used to be a clock the caller could not know.
# Taking "the newest directory" instead was wrong whenever another, shorter runner invocation
# appeared in the same host checkout after the real one: its nearly empty `summary.json` was fetched
# in place of the full one.
#
# So the tool gives every plan run it starts its own name, hands it to the runner as
# `CAMEL_TEST_RUN_NAME`, and `-FromThisRun` reads the suffix from exactly that run's directory. A
# directory that is not there is reported and nothing is fetched; no other run is ever read instead.
#
# Kept here rather than inside `camel-remote.ps1` so that the decisions -- what a run is called, what
# a suffix may name, where the file lands -- are pure and can be read and tested without a host (the
# same reason `camel-remote-args.ps1` is a file of its own).

# The words a refusal is written with, so that a caller who typed the flag wrong is answered by the
# flag's own name and by a shape they can copy.
$script:CamelFetchUsage =
    'camel-remote: -FetchFile -FromThisRun needs a suffix inside the run log directory ' +
    '(for example -FetchFile fail/transforms/x.nvm/stdout.txt -FromThisRun)'

# A run's name: a plain name, the same rule the runner applies to `CAMEL_TEST_RUN_NAME`
# (`test/tools/run-tests.mjs`). No path separator and no dot-only name, so it can only ever name a
# direct child of the log directory.
function Test-CamelRunName {
    [CmdletBinding()]
    param(
        [string]$Name
    )
    return ($Name -match '^[A-Za-z0-9][A-Za-z0-9._-]*$')
}

# One run's name, unique per invocation: the local clock, the local process id and a few random
# characters. The clock keeps the names in the order the runs were started, the pid and the random
# tail keep two invocations in the same second apart.
function New-CamelRunName {
    [CmdletBinding()]
    param()
    $alphabet = 'abcdefghijklmnopqrstuvwxyz0123456789'
    $tail = -join (1..4 | ForEach-Object { $alphabet[(Get-Random -Maximum $alphabet.Length)] })
    return ('{0}_{1}_{2}' -f (Get-Date -Format 'yyyyMMdd_HHmmss'), $PID, $tail)
}

# What a suffix may name: a file *inside* the run's log directory, written the way the runner writes
# it (`fail/<case>.<backend>/stdout.txt`). Not the live checkout, not anything above the run: a suffix
# that climbs (`../..`) or that names an absolute path (`/etc/...`) is refused here, by the flag's own
# name, because `-FromThisRun` is a way to read a log and not a way to fetch any file on the host.
#
# A backslash is refused rather than translated: the host splits on `/`, so a suffix written with
# backslashes would arrive as one long file name and fail there instead of here.
function Resolve-CamelFetchSuffix {
    [CmdletBinding()]
    param(
        [string]$Suffix
    )
    $text = if ($null -eq $Suffix) { '' } else { $Suffix.Trim() }
    if ($text -eq '') {
        throw $script:CamelFetchUsage
    }
    if ($text.Contains('\')) {
        throw "camel-remote: -FetchFile -FromThisRun takes '/' between the parts of the path, not '\' (got '$text')"
    }
    if ($text.StartsWith('/')) {
        throw "camel-remote: -FetchFile -FromThisRun names a file inside the run log directory, not an absolute path (got '$text')"
    }
    while ($text.StartsWith('./')) {
        $text = $text.Substring(2)
    }
    foreach ($part in $text.Split('/')) {
        if ($part -eq '' -or $part -eq '.' -or $part -eq '..') {
            throw "camel-remote: -FetchFile -FromThisRun refuses a path that is empty, names this directory or climbs out of the run (got '$Suffix')"
        }
    }
    return $text
}

# Where the file is read from and where it lands. The host path is the named run's own directory; the
# local path keeps the suffix as its tail, so a reader finds the file where the same words name it on
# the host -- the alternative (flattening it to a bare `stdout.txt`) would make two cases of one run
# overwrite each other.
function Get-CamelFetchPaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$LogRootOnHost,
        [Parameter(Mandatory = $true)][string]$Run,
        [Parameter(Mandatory = $true)][string]$Suffix,
        [Parameter(Mandatory = $true)][string]$LocalRoot
    )
    $root = $LogRootOnHost.TrimEnd('/')
    $hostPath = "$root/$Run/$Suffix"
    $localPath = Join-Path $LocalRoot ($Suffix -replace '/', '\')
    return [pscustomobject]@{
        HostPath  = $hostPath
        LocalPath = $localPath
    }
}

# The one command the host is asked about the run: it prints the run directory's absolute path (what
# `scp` needs, since it hands the path to the remote side unexpanded) when that directory exists, and
# nothing when it does not. It names exactly one directory and looks at no other.
function New-CamelRunDirCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$LogRootOnHost,
        [Parameter(Mandatory = $true)][string]$Run
    )
    if (-not (Test-CamelRunName -Name $Run)) {
        throw "camel-remote: '$Run' is not a plain run name"
    }
    $root = $LogRootOnHost.TrimEnd('/')
    return "if [ -d `"$root/$Run`" ]; then ( cd `"$root/$Run`" && pwd ); fi"
}

# `-FetchFile <path>` without `-FromThisRun`: did this run write the file?
#
# The tool pushes the local copy of that file to the host on purpose, so that a two-run comparison
# finds the previous reading there. A run that does not write the file (a tool that now reports under
# another name) then leaves the pushed copy in place, and fetching it back would hand last run's
# reading over as this run's. So the host takes the file's modification time (or its absence) once the
# checkout and the untracked files are in place and before the plan starts, and compares it when the
# fetch comes. Unchanged means the run did not write it: nothing is sent, and the line says so.
#
# Both halves are generated here from the one path, so they cannot disagree about what is compared.
# The stamp is the nanosecond mtime, so a rewrite within the same second is still seen.
function New-CamelFetchStampCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Rel
    )
    return 'camel_fetch_stamp=$(stat -c %y "$R/' + $Rel + '" 2>/dev/null || echo absent)'
}

function New-CamelFetchStageCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Rel
    )
    $text = @'
camel_fetch_now=$(stat -c %y "$R/FETCHREL" 2>/dev/null || echo absent)
if [ "$camel_fetch_now" = "$camel_fetch_stamp" ] && [ "$camel_fetch_now" = absent ]; then
echo 'FETCH_MISSING=FETCHREL'
echo '[camel-remote] -FetchFile: the run did not write FETCHREL, and the host has none; nothing was fetched'
elif [ "$camel_fetch_now" = "$camel_fetch_stamp" ]; then
echo 'FETCH_UNCHANGED=FETCHREL'
echo '[camel-remote] -FetchFile: the run did not write FETCHREL; the file on the host is the copy this run pushed in; nothing was fetched'
elif [ -f "$R/FETCHREL" ]; then
echo '__CAMEL_REMOTE_FETCH_BEGIN__'
base64 "$R/FETCHREL"
echo '__CAMEL_REMOTE_FETCH_END__'
else
echo 'FETCH_MISSING=FETCHREL'
echo '[camel-remote] -FetchFile: the host has no FETCHREL (the run removed it); nothing was fetched'
fi
'@
    return $text.Replace('FETCHREL', $Rel)
}
