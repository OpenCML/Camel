# (c2) costs one reading per sweep, not two per host process per candidate directory.
#
# For each candidate checkout `cleanup_host_checkouts` asks whether any process's cwd or executable
# is inside it. It used to answer that with two `readlink` calls per process on the host, so one
# candidate cost the host's process count times two forks and a sweep cost that times its candidate
# count, and the reading that judged the last candidate was as old as the sweep was long.
#
# On the build host (32 cores, 1 m load 15-20, ~1780 processes), the sweep
# extracted from this tree and run against a fixture of N empty candidates:
#
#     N = 1   7 042 ms        N = 2   14 636 ms       N = 4   27 537 ms      N = 40   290 120 ms
#
# and the same four with the snapshot this file is about: 89 / 110 / 121 / 424 ms, the snapshot
# itself 79-416 ms old when the sweep ended. That is two readings of one program, not an estimate.
#
# What this file checks, and why each one can fail:
#
#   part A  the guard ships as one single-quoted here-string, defines the snapshot once, and still
#           re-takes the per-checkout lock for the removal itself -- that lock, not a snapshot taken
#           seconds earlier, is what keeps a run that entered the checkout afterwards;
#   part B  three sweeps over a seven-candidate fixture whose processes are held open by a fifo, not
#           by a `sleep <n>`: a lifetime in seconds is a guess about a loaded host, and that guess is
#           what made the two-day flake in `camel-remote-cleanup.test.ps1`. `readlink` and `ls` are
#           shimmed on PATH and count their own invocations:
#
#             * `readlink` calls for a seven-candidate sweep: 0. **This is the check that goes red on
#               the shape this file exists to keep out** -- the old guard makes thousands;
#             * `ls` calls: 1, for seven candidates and for four alike (one snapshot per sweep);
#             * a process inside a checkout is still named: cwd, a subdirectory, an executable;
#             * `cand-1`'s process does not reach `cand-10` (a prefix is not a path);
#             * a process that has already exited is not named, and one that exits between two sweeps
#               stops being named;
#             * a process list that could not be read at all keeps the checkout and says so, instead
#               of being read as "nothing is using it";
#             * the sweep states how old its snapshot got.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup-sweep-cost.test.ps1
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup-sweep-cost.test.ps1 -BashHost <host>
#
# Part B needs a bash that can hold a fifo and read /proc. Without one its checks are counted as
# skipped and the summary says how many, so a run that did not make them cannot pass for a green one.

[CmdletBinding()]
param(
    # ssh alias of a machine with bash, flock, cksum and find (the build host has all four).
    [string]$BashHost = '',
    # Keep the harness on the host for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'

$script:failed = 0
$script:checked = 0
# Checks this run did not make. Counted separately and printed, never dropped: a reading that omits
# what it did not run cannot be told from one that ran it green.
$script:skipped = 0
# Part B's verdicts, the exit code and the promise check below. A harness that grows or loses a
# check without this number moving fails rather than making a skipped run look like a smaller run.
$script:partBChecks = 16
$script:partBTotal = $script:partBChecks + 2

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

function Skip {
    param([string]$Reason)
    Write-Host ("  SKIP  part B: {0} checks not run -- {1}" -f $script:partBTotal, $Reason)
    $script:skipped += $script:partBTotal
}

# --- Part A: the guard as shipped -------------------------------------------
Write-Host 'Part A: the shipped guard'

$srcText = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'camel-remote.ps1'))
. (Join-Path $PSScriptRoot 'git-bash.ps1')
$guardMatch = [regex]::Match($srcText, "(?s)\`$cleanupGuard = @'\r?\n(.*?)\r?\n'@")
Check 'camel-remote.ps1 carries the guard as a single-quoted here-string' $guardMatch.Success
$guard = if ($guardMatch.Success) { $guardMatch.Groups[1].Value } else { '' }
Check 'the snapshot and the per-directory query are defined once each' `
    ((([regex]::Matches($guard, 'proc_snapshot_take\(\)')).Count -eq 1) -and
     (([regex]::Matches($guard, 'proc_hit_for\(\)')).Count -eq 1)) `
    'the fragment defines one of them more than once, or not at all'
# The re-take is the sentence that makes a snapshot acceptable: the reading may be seconds old, the
# lock is taken at the moment of removal.
Check 'the removal still re-takes the per-checkout lock' `
    ([regex]::IsMatch($guard, '(?m)flock -n "\$lockf" rm -rf')) 'no line takes the lock again for the rm'

# --- Part B: the sweep, in bash ---------------------------------------------
Write-Host ''
Write-Host 'Part B: one snapshot per sweep, on a fake ~/camel-work'

if (-not $guard) {
    Check 'part B needs the guard fragment' $false 'it was not found in camel-remote.ps1'
} else {
    # Written to a file and shipped as bytes: a pipeline through a Windows shell turns every LF into
    # CRLF, and a CR before a newline is enough to break `case ... esac`.
    $harness = @(
        'set -u',
        'O=$(mktemp -d)',
        'T="$O/camel-work"',
        'fails=0',
        'pass() { echo "PASS $1"; }',
        'bad() { echo "FAIL $1  ${2:-}"; fails=$((fails+1)); }',
        'mk() { mkdir -p "$T/$1"; echo x > "$T/$1/f"; touch -d "2 hours ago" "$T/$1/f" "$T/$1"; }',
        '# the fixture: a process inside four different ways, two that are not, and the prefix trap',
        'for d in cand-cwd cand-exe cand-1 cand-10 cand-idle cand-exited; do mk $d; done',
        'mk cand-sub/inner',
        'touch -d "2 hours ago" "$T/cand-sub"',
        'REAL_READLINK=$(command -v readlink)',
        'REAL_LS=$(command -v ls)',
        'REAL_SLEEP=$(command -v sleep)',
        'REAL_TRUE=$(command -v true)',
        'REAL_CAT=$(command -v cat)',
        '# a shim per call the old shape made per process: it counts itself and then forwards. The',
        '# format is single-quoted so `$@` reaches the shim as `$@` and not as this shell''s arguments',
        'mkdir -p "$O/shim" "$O/dead" "$O/count"',
        ': > "$O/count/readlink"',
        ': > "$O/count/ls"',
        'printf ''#!/bin/sh\nprintf x >> %s/readlink\nexec %s "$@"\n'' "$O/count" "$REAL_READLINK" > "$O/shim/readlink"',
        'printf ''#!/bin/sh\nprintf x >> %s/ls\nexec %s "$@"\n'' "$O/count" "$REAL_LS" > "$O/shim/ls"',
        'printf ''#!/bin/sh\nexit 2\n'' > "$O/dead/ls"',
        'chmod +x "$O/shim/readlink" "$O/shim/ls" "$O/dead/ls"',
        '# fixtures that live until this script closes the write ends below, never for a guessed interval;',
        '# their stdio is on /dev/null so a background process holding this channel cannot outlive the run',
        'mkfifo "$O/g-cwd" "$O/g-sub" "$O/g-trap"',
        '( cd "$T/cand-cwd" && exec "$REAL_CAT" "$O/g-cwd" ) </dev/null >/dev/null 2>&1 &',
        'p_cwd=$!',
        '( cd "$T/cand-sub/inner" && exec "$REAL_CAT" "$O/g-sub" ) </dev/null >/dev/null 2>&1 &',
        'p_sub=$!',
        '( cd "$T/cand-1" && exec "$REAL_CAT" "$O/g-trap" ) </dev/null >/dev/null 2>&1 &',
        'p_trap=$!',
        'cp "$REAL_SLEEP" "$T/cand-exe/sleep" && touch -d "2 hours ago" "$T/cand-exe/sleep"',
        '( cd /tmp && exec "$T/cand-exe/sleep" 900 ) </dev/null >/dev/null 2>&1 &',
        'p_exe=$!',
        '# cand-exited: a process that ran inside it and had already ended before any sweep looked',
        '( cd "$T/cand-exited" && exec "$REAL_TRUE" )',
        'exec 9<>"$O/g-cwd" 10<>"$O/g-sub" 11<>"$O/g-trap"',
        'cd /tmp || exit 1',
        'i=0; seen=0',
        'while [ "$i" -lt 300 ]; do',
        '    seen=1',
        '    [ "$($REAL_READLINK /proc/$p_cwd/cwd 2>/dev/null)" = "$T/cand-cwd" ] || seen=0',
        '    [ "$($REAL_READLINK /proc/$p_sub/cwd 2>/dev/null)" = "$T/cand-sub/inner" ] || seen=0',
        '    [ "$($REAL_READLINK /proc/$p_trap/cwd 2>/dev/null)" = "$T/cand-1" ] || seen=0',
        '    [ "$($REAL_READLINK /proc/$p_exe/exe 2>/dev/null)" = "$T/cand-exe/sleep" ] || seen=0',
        '    [ "$seen" = 1 ] && break',
        '    sleep 0.1; i=$((i+1))',
        'done',
        '[ "$seen" = 1 ] && pass "the fixture put a process inside cand-cwd, cand-sub/inner, cand-1 and cand-exe" || bad "the fixture put a process inside cand-cwd, cand-sub/inner, cand-1 and cand-exe" "waited $i rounds and one of the four was not there"',
        '',
        '# --- one sweep, with readlink and ls counting themselves',
        'PATH="$O/shim:$PATH"',
        'export PATH',
        'cleanup_host_checkouts "$T" "" 1 "Camel.git" > "$O/run1.txt" 2>&1',
        'PATH=${PATH#"$O/shim:"}',
        'export PATH',
        'rls=$(wc -c < "$O/count/readlink")',
        'lsc=$(wc -c < "$O/count/ls")',
        'out=$(cat "$O/run1.txt")',
        'age1=${proc_snapshot_age_ms:-}',
        'lines1=${proc_snapshot_lines:-0}',
        '[ "$rls" -eq 0 ] && pass "the sweep resolves no /proc link one process at a time (0 readlink calls)" || bad "the sweep resolves no /proc link one process at a time (0 readlink calls)" "$rls readlink calls for seven candidates"',
        '[ "$lsc" -eq 1 ] && pass "one process-list snapshot served all seven candidates" || bad "one process-list snapshot served all seven candidates" "$lsc calls for seven candidates"',
        '# the answer has to come from a real reading of the host, not from a snapshot that read nothing',
        '[ "$lines1" -gt 1000 ] && pass "the snapshot read the hosts process table" || bad "the snapshot read the hosts process table" "$lines1 links, a host has thousands of processes"',
        '[ -n "$age1" ] && pass "the sweep states how old its snapshot got" || bad "the sweep states how old its snapshot got" "proc_snapshot_age_ms is empty after a sweep"',
        'echo "NOTE first sweep: $lines1 /proc links in one ls call, 7 candidates judged, snapshot $age1 ms old at the end"',
        'case "$out" in *"keeping host checkout cand-cwd: a process is using it"*) pass "a process whose cwd is the checkout is named";; *) bad "a process whose cwd is the checkout is named" "$out";; esac',
        'case "$out" in *"keeping host checkout cand-sub: a process is using it"*) pass "a process in a subdirectory of the checkout is named";; *) bad "a process in a subdirectory of the checkout is named" "$out";; esac',
        'case "$out" in *"keeping host checkout cand-exe: a process is using it"*) pass "a process whose executable is inside the checkout is named";; *) bad "a process whose executable is inside the checkout is named" "$out";; esac',
        'case "$out" in *"keeping host checkout cand-1: a process is using it"*) pass "a process whose cwd is the checkout is named (cand-1)";; *) bad "a process whose cwd is the checkout is named (cand-1)" "$out";; esac',
        '# the prefix trap: cand-1 is busy, and its process must not make cand-10 busy',
        'case "$out" in *"keeping host checkout cand-10: a process is using it"*) bad "cand-1s process does not reach cand-10" "$out";; *) pass "cand-1s process does not reach cand-10";; esac',
        'case "$out" in *"keeping host checkout cand-exited: a process is using it"*) bad "a process that has already exited is not named" "$out";; *) pass "a process that has already exited is not named";; esac',
        '',
        '# --- the same directory, one moment later: the process has ended, and it must stop being named',
        'kill $p_cwd 2>/dev/null',
        'exec 9>&-',
        'wait $p_cwd 2>/dev/null',
        'i=0',
        'while [ "$i" -lt 200 ] && [ -d "/proc/$p_cwd" ]; do sleep 0.05; i=$((i+1)); done',
        '[ -d "/proc/$p_cwd" ] && bad "the fixture ended its process before the second sweep" "pid $p_cwd is still in /proc" || pass "the fixture ended its process before the second sweep"',
        'cleanup_host_checkouts "$T" "" 1 "Camel.git" > "$O/run2.txt" 2>&1',
        'out2=$(cat "$O/run2.txt")',
        'case "$out2" in *"keeping host checkout cand-cwd: a process is using it"*) bad "a process that has exited stops being named" "$out2";; *) pass "a process that has exited stops being named";; esac',
        'case "$out2" in *"keeping host checkout cand-sub: a process is using it"*) pass "the second sweep still names the processes that are still there";; *) bad "the second sweep still names the processes that are still there" "$out2";; esac',
        '',
        '# --- a process list that could not be read is not an empty one',
        'PATH="$O/dead:$PATH"',
        'export PATH',
        'cleanup_host_checkouts "$T" "" 1 "Camel.git" > "$O/run3.txt" 2>&1',
        'PATH=${PATH#"$O/dead:"}',
        'export PATH',
        'out3=$(cat "$O/run3.txt")',
        'case "$out3" in *"the host''s process list could not be read"*) pass "a process list that could not be read is named";; *) bad "a process list that could not be read is named" "$out3";; esac',
        'case "$out3" in *"removing host checkout"*) bad "an unreadable process list removes nothing" "$out3";; *) pass "an unreadable process list removes nothing";; esac',
        '',
        'exec 10>&- 11>&-',
        'kill $p_sub $p_trap $p_exe 2>/dev/null || true',
        'wait 2>/dev/null || true',
        'rm -rf "$O"',
        'echo "FAILS=$fails"',
        'exit "$fails"'
    ) -join "`n"
    $harness = $guard + "`n" + $harness

    $local = Join-Path ([IO.Path]::GetTempPath()) 'sweep-cost-guard-harness.sh'
    [IO.File]::WriteAllText($local, $harness)

    $raw = @()
    if ($BashHost) {
        $remoteName = 'sweep-cost-guard-test.sh'
        scp -q -o ConnectTimeout=15 $local "${BashHost}:/tmp/$remoteName"
        if ($LASTEXITCODE -ne 0) { throw "scp to $BashHost failed" }
        $raw = @(ssh -o ConnectTimeout=15 $BashHost "bash /tmp/$remoteName; echo RC=`$?")
        if (-not $Keep) { ssh -o ConnectTimeout=15 $BashHost "rm -f /tmp/$remoteName" | Out-Null }
    } elseif ($gitBash = Get-GitBash) {
        # A `bash` on PATH is not enough: this machine's resolves to WSL, which cannot open a Windows
        # path at all, so the harness would "fail" for a reason that has nothing to do with the guard.
        $probe = Join-Path ([IO.Path]::GetTempPath()) 'sweep-cost-bashprobe.sh'
        [IO.File]::WriteAllText($probe, "echo BASHOK`nif command -v flock >/dev/null 2>&1; then echo FLOCKOK; fi`n")
        $probeOut = @(& $gitBash $probe 2>$null)
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
        if (@($probeOut) -contains 'BASHOK' -and @($probeOut) -contains 'FLOCKOK') {
            $raw = @(& $gitBash $local)
            $raw += 'RC=' + $LASTEXITCODE
        } else {
            Skip 'the bash here cannot read a Windows path or has no flock (Git for Windows has none); give -BashHost <ssh alias>'
        }
    } else {
        Skip 'no bash on this machine; give -BashHost <ssh alias>'
    }

    if ($raw.Count -gt 0) {
        foreach ($line in $raw) {
            $s = "$line"
            if ($s -like 'PASS *') { Check ($s -replace '^PASS ', '') $true }
            elseif ($s -like 'FAIL *') { Check ($s -replace '^FAIL ', '') $false }
            elseif ($s -like 'NOTE *') { Write-Host ("  ({0})" -f ($s -replace '^NOTE ', '')) }
        }
        $verdicts = @($raw | Where-Object { "$_" -like 'PASS *' -or "$_" -like 'FAIL *' })
        # Part B's promise in one cell: a harness that grew or lost a check without the number above
        # moving fails here, rather than making a skipped run look like a smaller green one.
        Check 'part B made the number of checks it promises' ($verdicts.Count -eq $script:partBChecks) `
            ("$($verdicts.Count) verdicts, $script:partBChecks promised")
        $rc = @($raw | Where-Object { "$_" -like 'RC=*' }) | Select-Object -Last 1
        Check 'the bash guard exited 0' ("$rc" -eq 'RC=0') ("$rc")
    }
}

# --- result ------------------------------------------------------------------
Write-Host ''
$total = $script:checked + $script:skipped
if ($script:skipped -gt 0) {
    Write-Host ("{0} checks, {1} failed, {2} skipped" -f $total, $script:failed, $script:skipped)
} else {
    Write-Host ("{0} checks, {1} failed" -f $total, $script:failed)
}
if ($script:failed -gt 0) { exit 1 }
exit 0
