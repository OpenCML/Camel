# The lists `camel-remote.ps1` takes on its command line, and what a badly-given one means.
#
# Every list on that command line (`-Plan`, `-AstOnly`, `-AstDrop`) can arrive through
# `pwsh -File <script> ...`, where the caller's shell has already decided how the words split. Three
# forms look the same to the person typing them and are three different things to the script:
#
#   -AstOnly a,b                 one word "a,b"          -> two globs, after the split below
#   -AstOnly 'a','b'             still one word "a,b"    -> the same two globs
#   -AstOnly a -AstOnly b        rejected by PowerShell  -> "parameter 'AstOnly' is specified more
#                                                          than once"
#
# The second line is the one that cost a lane a recording: `-AstOnly 'a','b'` was documented as the
# array form, arrived as the single glob `a,b`, selected none of the record's files, and a narrowing
# that selects nothing is the bulk re-record this tool's whole `-AstOnly` exists to avoid. The third
# line cannot be fixed inside the script -- the engine binds the parameters before the body runs --
# so a caller that repeats the parameter still gets the engine's own refusal, and the documentation
# says so instead of promising a form that cannot work.
#
# `Split-CamelArgList` therefore splits on commas exactly as `-Plan` already did, and the two
# functions below turn a list into the argument text the host receives. They are here, in their own
# file, because `internal/tools/camel-remote-args.test.ps1` checks them without a host: the binding
# rule is the thing that was wrong, so it is the thing that can be measured without one.

# Every element split on commas, trimmed, empties dropped. `$null` and `$empty` both give an empty
# array, so a caller cannot tell "given nothing" from "given nothing that names anything" -- which is
# what the caller's own bound-parameter test is for.
function Split-CamelArgList {
    param([string[]]$Values = @())
    @($Values | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# `--only '<glob>'` per element, for `check.py snapshot --update`. The globs are single-quoted because
# the whole text travels into a shell script; `Get-CamelGlobProblems` refuses a glob that would leave
# that quoting.
function Get-AstOnlyArgs {
    param([string[]]$AstOnly = @())
    (@($AstOnly | ForEach-Object { " --only '$_'" }) -join '')
}

# The same for `--drop`, the rows a recording is allowed to lose.
function Get-AstDropArgs {
    param([string[]]$AstDrop = @())
    (@($AstDrop | ForEach-Object { " --drop '$_'" }) -join '')
}

# Every `.cml` the tree holds, relative to `$Root`, `/`-separated, so a glob can be matched against
# the names the record spells. Cached per root: a run asks once, and the walk is 4000-odd files.
$script:CamelCmlCache = @{}
function Get-CamelCmlPaths {
    param([string]$Root)
    if ($script:CamelCmlCache.ContainsKey($Root)) { return $script:CamelCmlCache[$Root] }
    $paths = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter '*.cml' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName.Substring($Root.Length).TrimStart('\', '/') -replace '\\', '/' })
    $script:CamelCmlCache[$Root] = $paths
    return $paths
}

# `fnmatch.fnmatchcase`, which is what `check.py` matches against: `*` crosses `/`, `?` is one
# character, and a character class is not modelled here (a glob that contains `[` is left to the host
# rather than refused on a reading this function cannot make).
function Get-CamelGlobRegex {
    param([string]$Glob)
    $pattern = [System.Text.StringBuilder]::new()
    [void]$pattern.Append('^')
    foreach ($ch in $Glob.ToCharArray()) {
        if ($ch -eq '*') {
            [void]$pattern.Append('.*')
        } elseif ($ch -eq '?') {
            [void]$pattern.Append('.')
        } else {
            [void]$pattern.Append([regex]::Escape([string]$ch))
        }
    }
    [void]$pattern.Append('$')
    return $pattern.ToString()
}

# Why this list may not be handed to the recording stage, as one line per problem. Empty means it may.
#
# Three refusals, each naming the flag rather than the stage it would reach:
#   * the flag was given and, after the split, names nothing -- the empty array a shell can produce;
#   * a glob names no `.cml` in this tree -- `check.py` would answer "selected none" on the host, and a
#     narrowing that selects nothing is a bulk re-record (that is the whole reason `-AstOnly` exists);
#   * a glob carries a `'`, which would end the single-quoted word it is pasted into.
function Get-CamelGlobProblems {
    param(
        [string[]]$Globs = @(),
        [string]$Flag,
        [string]$Root,
        # The caller saw the parameter on its command line, whatever it ended up holding.
        [switch]$Named
    )
    $problems = @()
    if ($Globs.Count -eq 0) {
        if ($Named) {
            $problems += "$Flag was given no glob. Through ``pwsh -File`` a list is one word: write '$Flag a,b', or give the array in a PowerShell session ('$Flag a','b')."
        }
        return $problems
    }
    $cml = Get-CamelCmlPaths -Root $Root
    foreach ($glob in $Globs) {
        if ($glob.Contains("'")) {
            $problems += "$Flag '$glob' contains a quote, which would end the shell word it is passed in."
            continue
        }
        if ($glob.Contains('[')) { continue }
        $regex = Get-CamelGlobRegex -Glob $glob
        $hit = @($cml | Where-Object { [regex]::IsMatch($_, $regex) })
        if ($hit.Count -eq 0) {
            $problems += "$Flag '$glob' names none of this tree's $($cml.Count) .cml file(s); a recording narrowed to nothing rewrites the whole record, so it is refused here instead."
        }
    }
    return $problems
}