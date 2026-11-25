# `-FetchFile -Latest`: the pieces of it that can be decided without a host.
#
# The trouble this answers: a run's failure log lives in a directory named after the clock
# (`test/results/log/<stamp>/fail/<case>.nvm/stdout.txt`), the console keeps one truncated line of
# it, and `-FetchFile` takes a path that must already be known. So a reader who has just been told
# "output does not contain ...; [stdout] ... [Error]: ... (the r..." cannot ask for the rest of the
# sentence, and two lanes tonight wrote "推断" where they had a measurement.
#
# `-Latest` makes the run a name the tool resolves, not a name the caller has to know: the suffix is
# resolved against the newest run's log directory, and the file lands locally with the same tail.
#
# Kept here rather than inside `camel-remote.ps1` so that the three decisions -- which run is newest,
# what a suffix may name, where the file lands -- are pure and can be read and tested without a host
# (the same reason `camel-remote-args.ps1` is a file of its own). The tool dot-sources this file and
# asks the host for the one command `New-CamelLatestRunCommand` writes.

# The two words a refusal is written with, so that a caller who typed the flag wrong is answered by
# the flag's own name and by a shape they can copy.
$script:CamelFetchUsage =
    'camel-remote: -FetchFile -Latest needs a suffix inside the run log directory ' +
    '(for example -FetchFile fail/transforms/x.nvm/stdout.txt -Latest)'

# Which of a run directory's children is the newest run. The names are the run's clock
# (`20261124_123343_3556634`), so the order the names sort in is the order the runs happened in --
# read ordinally, because a culture sort of `20261124_9320_1` and `20261124_9320_2` is not the
# clock's order and the run named second would answer first.
#
# No run at all answers the empty string rather than a guess: `-Latest` with nothing to name is a
# refusal the caller writes, not something to resolve to '' and then fetch from `/`.
function Select-CamelLatestRun {
    [CmdletBinding()]
    param(
        [string[]]$Runs
    )
    if ($null -eq $Runs -or $Runs.Count -eq 0) {
        return ''
    }
    $sorted = [string[]]@($Runs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($sorted.Count -eq 0) {
        return ''
    }
    [array]::Sort($sorted, [System.StringComparer]::Ordinal)
    return $sorted[$sorted.Count - 1]
}

# What a suffix may name: a file *inside* the run's log directory, written the way the runner writes
# it (`fail/<case>.<backend>/stdout.txt`). Not the live checkout, not anything above the run: a suffix
# that climbs (`../..`) or that names an absolute path (`/etc/...`) is refused here, by the flag's own
# name, because `-Latest` is a way to read a log and not a way to fetch any file on the host.
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
        throw "camel-remote: -FetchFile -Latest takes '/' between the parts of the path, not '\' (got '$text')"
    }
    if ($text.StartsWith('/')) {
        throw "camel-remote: -FetchFile -Latest names a file inside the run log directory, not an absolute path (got '$text')"
    }
    while ($text.StartsWith('./')) {
        $text = $text.Substring(2)
    }
    foreach ($part in $text.Split('/')) {
        if ($part -eq '' -or $part -eq '.' -or $part -eq '..') {
            throw "camel-remote: -FetchFile -Latest refuses a path that is empty, names this directory or climbs out of the run (got '$Suffix')"
        }
    }
    return $text
}

# Where the file is read from and where it lands. The host path is the newest run's own directory; the
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

# The one command the host is asked to name the newest run. `find` with `-maxdepth 1 -type d` answers
# the run directories and nothing else, and `sort | tail -n 1` is the clock's order the names already
# carry. It does not glob: a log root whose glob matches nothing would otherwise hand `find` a literal
# `*` and print that back as a run name.
function New-CamelLatestRunCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$LogRootOnHost
    )
    $root = $LogRootOnHost.TrimEnd('/')
    return "find '$root' -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort | tail -n 1"
}