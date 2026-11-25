# The one reader of a plan's `time_sensitive` declaration, and what it reads.
#
# A plan whose readings are durations declares `time_sensitive = true` at the top level of its
# *.plan.toml, before any table header, and the gate then takes the plan slot and both build slots
# together (`camel-remote.ps1`, "a time-sensitive plan takes the whole host"). Until this file
# existed that read was sixteen lines inside a 4134-line script that cannot be started without a
# host, so the only thing that could be said about it was that it had never been compared with a
# second reading of the same files -- the shape this project keeps finding: one thing with one
# reader, and no way for the reader to be shown wrong.
#
# So the read lives here, unchanged, and two things follow from the move:
#
#   * `internal/tools/camel-remote-timesensitive.test.ps1` can call it with no host, no build and no
#     plan (`camel-remote-args.ps1` is here for the same reason), and
#   * a second, independent reader of the same input strings exists (`test/tools/time-sensitive-reader.mjs`),
#     so "the gate read this plan right" is now a comparison rather than a claim.
#
# The logic below is the logic that was in `camel-remote.ps1` before the move: the same two
# regular expressions, the same statements in the same order. It is *not* rewritten here, because a
# second parser is only useful while the first one still reads exactly what it read -- a refactor
# would make the cross-check a comparison of two new things.
#
# What it reads, sentence by sentence, is the table in `internal/reports/2026-11-25-time-sensitive-readers.cn.md`;
# the two sentences that matter are:
#
#   `^\s*\[`                                    the document's top level ends at the first line
#                                               whose first non-space character is `[` -- a table
#                                               header, and also anything else that looks like one
#   `^\s*time_sensitive\s*=\s*true\s*(#.*)?$`   the declaration: the key, `=`, the bare word
#                                               `true`, and an optional trailing comment

function Get-TimeSensitivePlans {
    <#
      The plan targets of `-Plan` that declare themselves time sensitive. `Root` is the worktree the
      relative targets are resolved against (the caller passes the resolved `$Path`), and `Plan` is
      the target list after `Split-CamelArgList` has split it: a *.plan.toml file, a plan directory
      (`smoke`), a missing path, or something that is not a file at all. A directory target carries
      no declaration of its own, which is why it is skipped rather than searched.
    #>
    param(
        [string]$Root = '.',
        [string[]]$Plan = @()
    )
    $declared = @()
    foreach ($p in $Plan) {
        $p = "$p".Trim()
        if (-not $p) { continue }
        $file = if ([System.IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $Root $p }
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
        foreach ($declLine in @(Get-Content -LiteralPath $file)) {
            if ($declLine -match '^\s*\[') { break }
            if ($declLine -match '^\s*time_sensitive\s*=\s*true\s*(#.*)?$') {
                $declared += $p
                break
            }
        }
    }
    $declared
}