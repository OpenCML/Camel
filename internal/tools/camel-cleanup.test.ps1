# The cleanup mechanism: what it removes, and what it must refuse to remove.
#
# Rule 12 of internal/collab/rules.md, and the two accidents behind it:
#
#   * the coordinator deleted a lane checkout still waiting for CI (`ahead==0 && status clean`
#     cannot tell "pushed and finished" from "pushed and working");
#   * a lane deleted the shared staging bare `~/camel-work/Camel.git` while clearing a host
#     temporary checkout.
#
# `camel-cleanup.sh` answers with a record instead of a guess: a run creates its temporaries under
# `$HOME/camel-work/tmp/<lane>-<time>/` and writes `$HOME/camel-work/manifests/<lane>-<time>.tsv`
# when it creates them; `rm` accepts a path only when, after resolving `..`, an absolute spelling
# and every symlink to a real path, it is both under the root and named in a manifest.
#
# This file runs that script -- the shipped file, as a program, not a copy of its logic -- against a
# fake home, and takes the six readings the lane asked for:
#
#   1  the configured shared bare is refused, and the refusal prints the path
#   2  a recorded directory of the run is removed
#   3  a directory under the root that no manifest names is refused and printed
#   4  the manifest is outside the root, and clearing the root does not delete it
#   5  `../`, an absolute path and a symlink pointing outside are all refused (one reading each)
#   6  the accident replayed: the bare is still there, before and after
#
# Nothing on the host is touched: the bare of reading 1 and 6 is `$HOME/camel-work/Camel.git` of a
# `mktemp -d` fake home, which the harness removes at the end.
#
#   pwsh -NoProfile -File internal/tools/camel-cleanup.test.ps1
#   pwsh -NoProfile -File internal/tools/camel-cleanup.test.ps1 -BashHost 88zjwei
#
# This machine's `bash` on PATH is WSL and cannot read a Windows path, so without -BashHost the run
# prints SKIP unless a bash that can read one is found.

[CmdletBinding()]
param(
    # ssh alias of a machine with bash, mktemp, readlink, awk and tac (the build host has all five).
    [string]$BashHost = ''
)

$ErrorActionPreference = 'Stop'

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

$cleanupScript = Join-Path $PSScriptRoot 'camel-cleanup.sh'
if (-not (Test-Path $cleanupScript)) { throw "missing $cleanupScript" }

# The harness is shipped as bytes and sources nothing: it runs `bash camel-cleanup.sh` the way any
# other caller does, so the entry point and its exit codes are what is under test.
$harness = @(
    'set -u',
    'D=$(cd "$(dirname "$0")" && pwd)',
    'CC=$D/camel-cleanup.sh',
    '[ -f "$CC" ] || { echo "FAIL the script was not shipped"; echo "RC=1"; exit 1; }',
    'T=$(mktemp -d)',
    'H=$T/home',
    'R=$H/camel-work/tmp',
    'M=$H/camel-work/manifests',
    'mkdir -p "$R" "$M"',
    '# the fake home''s shared staging bare: the thing the accident deleted. Never the real one.',
    'BARE=$H/camel-work/Camel.git',
    'mkdir -p "$BARE/objects"; printf "ref: refs/heads/main\n" > "$BARE/HEAD"',
    'fails=0',
    'checked=0',
    'pass() { checked=$((checked+1)); echo "PASS $1"; }',
    'bad() { checked=$((checked+1)); fails=$((fails+1)); echo "FAIL $1 ${2:-}"; }',
    'cc() { bash "$CC" --home "$H" "$@"; }',
    'before=$( [ -d "$BARE" ] && echo present || echo absent )',
    '',
    '# --- 1. the configured shared bare: refused, and the path printed',
    'out=$(cc rm "$BARE" 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "1a the shared bare is refused (rc=3)" || bad "1a rc=3" "rc=$rc out=$out"',
    'case "$out" in *"$BARE"*) pass "1b the refusal prints the path";; *) bad "1b prints the path" "$out";; esac',
    'case "$out" in *"outside the cleanup root"*) pass "1c the reason is the root boundary";; *) bad "1c root boundary" "$out";; esac',
    '',
    '# --- 2. a directory this run created and recorded: removed',
    'init=$(cc init lane-selftest --purpose "camel-cleanup.test.ps1")',
    'RUN=$(printf "%s\n" "$init" | awk -F"\t" "\$1==\"runId\"{print \$2}")',
    'RUNDIR=$(printf "%s\n" "$init" | awk -F"\t" "\$1==\"runDir\"{print \$2}")',
    'MANI=$(printf "%s\n" "$init" | awk -F"\t" "\$1==\"manifest\"{print \$2}")',
    '[ -n "$RUN" ] && [ -d "$RUNDIR" ] && pass "2a init made a run dir ($RUN)" || bad "2a init made a run dir" "run=$RUN dir=$RUNDIR"',
    '[ -f "$MANI" ] && pass "2b init wrote a manifest" || bad "2b init wrote a manifest" "$MANI"',
    'OWNDIR="$RUNDIR/checkout"',
    'mkdir -p "$OWNDIR"; echo x > "$OWNDIR/file"',
    'cc add "$RUN" "$OWNDIR" --kind checkout --purpose "the harness own checkout" >/dev/null 2>&1',
    'rc=$?',
    '[ "$rc" = 0 ] && pass "2c the run recorded its own directory" || bad "2c recorded" "rc=$rc"',
    'out=$(cc rm "$OWNDIR" 2>&1); rc=$?',
    '[ "$rc" = 0 ] && pass "2d a recorded directory is removed" || bad "2d removed" "rc=$rc out=$out"',
    '[ ! -e "$OWNDIR" ] && pass "2e it is gone" || bad "2e it is gone" "$OWNDIR still there"',
    'case "$out" in *"$MANI"*) pass "2f the removal names the manifest it acted on";; *) bad "2f names the manifest" "$out";; esac',
    '',
    '# --- 3. under the root but not in any manifest: refused, and printed',
    'STRAY="$RUNDIR/stray"',
    'mkdir -p "$STRAY"',
    'out=$(cc rm "$STRAY" 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "3a an unrecorded directory under the root is refused" || bad "3a refused" "rc=$rc out=$out"',
    'case "$out" in *"$STRAY"*) pass "3b the refusal prints the path";; *) bad "3b prints the path" "$out";; esac',
    'case "$out" in *"not named in any manifest"*) pass "3c being under the root is not being mine";; *) bad "3c reason" "$out";; esac',
    '[ -d "$STRAY" ] && pass "3d it is still there" || bad "3d still there"',
    'out=$(cc rm "$R" 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "3e the root itself is refused" || bad "3e root refused" "rc=$rc out=$out"',
    '',
    '# --- 4. the record is outside the root and survives clearing the root',
    'case "$MANI" in "$R"/*) bad "4a the manifest is outside the root" "$MANI";; *) pass "4a the manifest is outside the root";; esac',
    'case "$MANI" in "$R"/*) ;; *) pass "4b it lives under camel-work/manifests";; esac',
    'rm -rf "$R"/* 2>/dev/null',
    '[ -f "$MANI" ] && pass "4c clearing the root left the manifest" || bad "4c manifest survived the wipe" "$MANI"',
    'grep -qF -- "$OWNDIR" "$MANI" && pass "4d the surviving manifest still names what the run created" || bad "4d record survived the wipe" "$MANI"',
    '',
    '# --- 5. three traversal spellings, three readings',
    'mkdir -p "$R"',
    'init2=$(cc init lane-selftest2 --purpose "the traversal readings")',
    'RUN2=$(printf "%s\n" "$init2" | awk -F"\t" "\$1==\"runId\"{print \$2}")',
    'RUNDIR2=$(printf "%s\n" "$init2" | awk -F"\t" "\$1==\"runDir\"{print \$2}")',
    '',
    '# (a) `../` -- resolved first, then judged; a prefix comparison would pass this',
    'out=$(cd "$RUNDIR2" && cc rm "../../Camel.git" 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "5a ../ is refused (rc=3)" || bad "5a ../ refused" "rc=$rc out=$out"',
    'case "$out" in *"$H/camel-work/Camel.git"*) pass "5a2 it prints the path it resolved to";; *) bad "5a2 resolved path" "$out";; esac',
    '[ -d "$BARE" ] && pass "5a3 the bare is still there after the traversal" || bad "5a3 bare"',
    '',
    '# (b) an absolute path outside the root',
    'out=$(cc rm /tmp 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "5b an absolute path outside the root is refused" || bad "5b absolute refused" "rc=$rc out=$out"',
    'case "$out" in *"outside the cleanup root"*) pass "5b2 the reason is the boundary";; *) bad "5b2 reason" "$out";; esac',
    '[ -d /tmp ] && pass "5b3 /tmp is still there" || bad "5b3 /tmp"',
    '',
    '# (c) a symlink inside the root pointing outside it',
    'ln -s "$BARE" "$RUNDIR2/link-out"',
    'out=$(cc rm "$RUNDIR2/link-out" 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "5c a symlink pointing out of the root is refused" || bad "5c symlink refused" "rc=$rc out=$out"',
    'case "$out" in *"is a symlink"*) pass "5c2 the reason names the symlink";; *) bad "5c2 reason" "$out";; esac',
    'cc add "$RUN2" "$RUNDIR2/link-out" --kind link >/dev/null 2>&1; rc=$?',
    '[ "$rc" = 3 ] && pass "5c3 the record cannot be made to point at it either" || bad "5c3 add refused" "rc=$rc"',
    '[ -d "$BARE" ] && pass "5c4 the bare is still there after the symlink" || bad "5c4 bare"',
    '',
    '# (d) a `..` that lands back inside the root but outside the run dir',
    'OUTSIDE="$R/unrecorded-one"; mkdir -p "$OUTSIDE"',
    'out=$(cd "$RUNDIR2" && cc rm "../unrecorded-one" 2>&1); rc=$?',
    '[ "$rc" = 3 ] && pass "5d a .. that lands in the root but outside the run dir is refused" || bad "5d" "rc=$rc out=$out"',
    '[ -d "$OUTSIDE" ] && pass "5d2 it is still there" || bad "5d2 still there"',
    '',
    '# --- 6. the accident replayed: the bare is still there, before and after',
    '[ "$before" = present ] && pass "6a the bare was there before the run" || bad "6a before" "$before"',
    'after=$( [ -d "$BARE" ] && echo present || echo absent )',
    '[ "$after" = present ] && pass "6b the bare is still there after the run" || bad "6b after" "$after"',
    '[ -f "$BARE/HEAD" ] && [ -d "$BARE/objects" ] && pass "6c it is still the bare it was" || bad "6c contents"',

    '# --- 7. prune removes every recorded path, and only those',
    'init3=$(cc init lane-selftest3 --purpose "the prune reading")',
    'RUN3=$(printf "%s\n" "$init3" | awk -F"\t" "\$1==\"runId\"{print \$2}")',
    'RUNDIR3=$(printf "%s\n" "$init3" | awk -F"\t" "\$1==\"runDir\"{print \$2}")',
    'MANI3=$(printf "%s\n" "$init3" | awk -F"\t" "\$1==\"manifest\"{print \$2}")',
    'mkdir -p "$RUNDIR3/one/nested" "$RUNDIR3/two" "$RUNDIR3/stray"',
    'cc add "$RUN3" "$RUNDIR3/one/nested" --kind replay-tree >/dev/null 2>&1',
    'cc add "$RUN3" "$RUNDIR3/one" --kind replay-tree >/dev/null 2>&1',
    'cc add "$RUN3" "$RUNDIR3/two" --kind checkout >/dev/null 2>&1',
    'cc prune "$RUN3" >/dev/null 2>&1; rc=$?',
    '[ "$rc" = 0 ] && pass "7a prune exits 0" || bad "7a prune" "rc=$rc"',
    '[ ! -d "$RUNDIR3/one" ] && [ ! -d "$RUNDIR3/two" ] && pass "7b every recorded path is gone" || bad "7b gone"',
    '[ -d "$RUNDIR3/stray" ] && pass "7c a path that was never recorded is untouched" || bad "7c stray"',
    '[ -d "$RUNDIR3" ] && pass "7d the run dir is kept while something unrecorded is in it" || bad "7d run dir kept"',
    'rm -rf "$RUNDIR3/stray"',
    'cc prune "$RUN3" >/dev/null 2>&1',
    '[ ! -d "$RUNDIR3" ] && pass "7e the empty run dir is removed by the second prune" || bad "7e run dir gone"',
    '[ -f "$MANI3" ] && pass "7f the manifest is kept as the record" || bad "7f manifest kept"',
    '',
    '',
    '# the real staging bare of this host, read only: no test above may have touched it',
    'REAL=${HOME:-}/camel-work/Camel.git',
    'if [ -n "${HOME:-}" ] && [ -d "$REAL" ]; then pass "6d the host real shared bare ($REAL) is still present (read only)"; else echo "NOTE no real shared bare at ${REAL:-<no HOME>}; nothing to observe"; fi',
    '',
    'rm -rf "$T"',
    'echo "FAILS=$fails"',
    'exit "$fails"'
) -join "`n"

$localDir = Join-Path ([IO.Path]::GetTempPath()) ('camel-cleanup-selftest-' + [guid]::NewGuid().ToString('n').Substring(0, 8))
New-Item -ItemType Directory -Path $localDir | Out-Null
$localHarness = Join-Path $localDir 'run.sh'
[IO.File]::WriteAllText($localHarness, $harness)

$raw = @()
if ($BashHost) {
    $remoteDir = '/tmp/camel-cleanup-selftest-' + [guid]::NewGuid().ToString('n').Substring(0, 8)
    ssh -o ConnectTimeout=15 $BashHost "mkdir -p $remoteDir" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "cannot create $remoteDir on $BashHost" }
    scp -q -o ConnectTimeout=15 $cleanupScript "${BashHost}:$remoteDir/camel-cleanup.sh"
    if ($LASTEXITCODE -ne 0) { throw "scp of camel-cleanup.sh to $BashHost failed" }
    scp -q -o ConnectTimeout=15 $localHarness "${BashHost}:$remoteDir/run.sh"
    if ($LASTEXITCODE -ne 0) { throw "scp of the harness to $BashHost failed" }
    $raw = @(ssh -o ConnectTimeout=60 $BashHost "bash $remoteDir/run.sh; echo RC=`$?")
    ssh -o ConnectTimeout=15 $BashHost "rm -rf $remoteDir" | Out-Null
} elseif (Get-Command bash -ErrorAction SilentlyContinue) {
    # A `bash` on PATH is not enough: this machine's is WSL and cannot open a Windows path, so the
    # harness would fail for a reason that has nothing to do with the script.
    $probe = Join-Path $localDir 'probe.sh'
    [IO.File]::WriteAllText($probe, "echo BASHOK`n")
    $probeOut = @(& bash $probe 2>$null)
    if (@($probeOut) -contains 'BASHOK') {
        $raw = @(& bash $localHarness)
        $raw += 'RC=' + $LASTEXITCODE
    } else {
        Write-Host '  SKIP  the bash on PATH cannot read a Windows path; give -BashHost <ssh alias>'
    }
} else {
    Write-Host '  SKIP  no bash here; give -BashHost <ssh alias>'
}

if ($raw.Count -gt 0) {
    Write-Host 'The cleanup script, on a fake home (never the real one)'
    foreach ($line in $raw) {
        if ("$line" -like 'PASS *') { Check ("$line" -replace '^PASS ', '') $true }
        elseif ("$line" -like 'FAIL *') { Check ("$line" -replace '^FAIL ', '') $false }
        elseif ("$line" -like 'NOTE *') { Write-Host ("  NOTE  " + ("$line" -replace '^NOTE ', '')) }
    }
    $rc = @($raw | Where-Object { "$_" -like 'RC=*' }) | Select-Object -Last 1
    Check 'the cleanup script exited 0' ("$rc" -eq 'RC=0') ("$rc")
}

Remove-Item -Recurse -Force $localDir -ErrorAction SilentlyContinue

Write-Host ''
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0