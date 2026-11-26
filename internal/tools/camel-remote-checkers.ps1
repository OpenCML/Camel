# The per-checker block of the host script `camel-remote.ps1` generates, as functions.
#
# A checker is a row of the `$checkers` table in that script: a script, its arguments, and a few
# marks. The host runs one block per row, and a block whose command came out as an interpreter with no
# script (`node` alone) reads its program from standard input and never returns -- while it holds the
# checkout's lock. Two rules keep that from being writable:
#
#   * a row without a script is refused on this side, by the row's name, before anything is pushed or
#     transferred (`Get-CamelCheckerProblem`, and `Get-CamelCheckerBlock` throws on the same test);
#   * every checker runs with standard input from /dev/null, so a checker that does ask for input gets
#     end-of-file at once instead of waiting.
#
# They are here, in their own file, because `internal/tools/camel-remote-args.test.ps1` reads the
# generated text without a host.

# Why this row cannot be run, as one line; `$null` when it can. The row is whatever the table holds
# under the name, so a name the table does not have arrives as `$null`.
function Get-CamelCheckerProblem {
    param([string]$Name, $Spec)
    if ($null -eq $Spec) {
        return "checker '$Name' has no row in the checkers table, so there is no command to run for it"
    }
    if ([string]::IsNullOrWhiteSpace([string]$Spec.script)) {
        return "checker '$Name' has a row with no script, so its command would be an interpreter reading standard input"
    }
    return $null
}

# The command a row runs, as the host shell receives it. `$VerifyTag` names the install a compiling
# checker is pointed at (`out/<tag>/bin/camel`, relative to the checkout, which is the working
# directory by then) and `$Jobs` is the width a compiling checker may narrow to the tokens it gets.
function Get-CamelCheckerCommand {
    param([string]$Name, $Spec, [string]$VerifyTag, [int]$Jobs)
    $problem = Get-CamelCheckerProblem -Name $Name -Spec $Spec
    if ($problem) { throw $problem }
    $camelArg = if ($Spec.compiler) { " --camel `"out/$VerifyTag/bin/camel`"" } else { '' }
    # Every row but the generated-file pair is a node script; those two name their own interpreter.
    $interp = if ($Spec.interpreter) { $Spec.interpreter } else { 'node' }
    $jobsArg = if ($Spec.compiles) { " --jobs $Jobs" } else { '' }
    return "$interp $($Spec.script) $(@($Spec.args) -join ' ')$camelArg$jobsArg"
}

# One block: a header naming the checker and its command, the command's own stdout and stderr, and a
# footer naming its exit code. The footer is what the workstation reads back; `checkfail` carries the
# first failure out of the stage. A `reportOnly` row captures the output so a count can be reduced out
# of it, and its exit code never reaches `checkfail`.
function Get-CamelCheckerBlock {
    param([string]$Name, $Spec, [string]$VerifyTag, [int]$Jobs, [string]$RemoteScratch)
    $shownCommand = Get-CamelCheckerCommand -Name $Name -Spec $Spec -VerifyTag $VerifyTag -Jobs $Jobs
    # Every checker runs under a token, and `run-quiet` hands it back the moment the checker is gone.
    # Standard input is /dev/null: a checker has nothing to read from the caller, and one that tried
    # would otherwise wait for a terminal nobody is at.
    $runUnderToken = "timeout 2400 bash `"`$R/internal/tools/host-tokens.sh`" run-quiet -- $shownCommand < /dev/null"
    if ($Spec.reportOnly) {
        # The output is captured so the reading can be reduced out of the checker's own summary, and
        # its exit code is kept out of `checkfail`, so a report that finds something does not turn the
        # gate red. The reading is `unknown` when the checker printed no summary (a missing script, an
        # early refusal) -- never a zero this run did not measure.
        $readingFile = "$RemoteScratch/check-$Name.out"
        return @"

echo '[camel-remote] --- check ${Name}: $shownCommand ---'
out='$readingFile'
$runUnderToken > "`$out" 2>&1
c=`$?
cat "`$out"
n=`$(sed -nE '$($Spec.findings)' "`$out" | head -n 1)
rm -f "`$out"
echo "check=${Name} findings=`${n:-unknown} mode=report"
echo "[camel-remote] --- ${Name}: exit `$c ---"
"@
    }
    return @"

echo '[camel-remote] --- check ${Name}: $shownCommand ---'
$runUnderToken 2>&1
c=`$?
if [ `$c -ne 0 ] && [ `$checkfail -eq 0 ]; then checkfail=`$c; fi
echo "[camel-remote] --- ${Name}: exit `$c ---"
"@
}
