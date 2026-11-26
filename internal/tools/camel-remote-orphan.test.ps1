# A run on the build host ends when the process that started it is gone, and the host keeps a copy
# of what the run said.
#
# `camel-remote.ps1` starts a plan as `ssh <host> "bash run.sh"`. There is no terminal, so when the
# workstation side dies (the ssh client is killed, the session is stopped, the machine sleeps) the
# host side gets no hangup: sshd's session process exits, `run.sh` is reparented, and everything under
# it keeps running. One such run sat for 5 h 14 min on 30 ms of CPU, blocked on a slot, holding host
# test slot 1 while another run queued behind it, and the only record of what it had said went down
# the ssh pipe that no longer had a reader.
#
# The host-side guard is a single-quoted here-string in camel-remote.ps1 (`$orphanGuard`). This file
# extracts it and runs it through a real bash:
#
#   * the fragment parses (`bash -n`), and the remote script carries it and ends its watchdog;
#   * a run whose starter is killed has every descendant gone -- including one that moved to its own
#     session -- and one line in the host log saying why; a run whose starter lives is not touched;
#   * the host copy of stdout and stderr equals what the stream carried, byte for byte, keeps what
#     came before when the stream's reader goes away, and keeps only the newest 20 pairs.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-orphan.test.ps1
#
# The behaviour checks need a bash that can read /proc/<pid>/stat (the build host always can; Git's
# bash on Windows has a partial /proc). When it cannot, those checks are counted as skipped and the
# summary says so -- a part that silently does not run is green by being unread.

[CmdletBinding()]
param(
    # The script that carries the guard; the default is the one beside this file.
    [string]$Source = '',
    [switch]$Keep
)

$ErrorActionPreference = 'Continue'
if (-not $Source) { $Source = Join-Path $PSScriptRoot 'camel-remote.ps1' }
$script:failed = 0
$script:checked = 0
$script:skipped = 0
$behaviourChecks = 9

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

$srcText = [IO.File]::ReadAllText($Source)
$m = [regex]::Match($srcText, "(?s)\`$orphanGuard = @'\r?\n(.*?)\r?\n'@")
Check 'camel-remote.ps1 carries the guard as a single-quoted here-string' $m.Success
$guard = if ($m.Success) { $m.Groups[1].Value -replace "`r`n", "`n" } else { '' }
Check 'the remote script carries the guard after setting the directory it writes to' `
    ([regex]::IsMatch($srcText, '(?m)^orphan_dir="[^\r\n]*\.remote-runs"\r?\n\$orphanGuard\r?$')) `
    'no line reading "$orphanGuard" right after orphan_dir'
Check 'the exit trap that replaces the guard''s still ends the watchdog' `
    ([regex]::IsMatch($srcText, '(?s)finish_probe\(\) \{.*?\n\s*orphan_end\r?\n\}')) 'finish_probe does not call orphan_end'

. (Join-Path $PSScriptRoot 'git-bash.ps1')
$bash = Get-GitBash
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('camel-orphan-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
function ToBash([string]$p) { $p -replace '\\', '/' }
$utf8 = New-Object System.Text.UTF8Encoding($false)
function WriteLf([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, ($text -replace "`r`n", "`n"), $utf8)
}

if ($guard -and $bash) {
    WriteLf (Join-Path $scratch 'syntax.sh') ("orphan_dir=x`n" + $guard + "`n")
    & $bash -n (ToBash (Join-Path $scratch 'syntax.sh')) 2>$null
    Check 'the fragment passes bash -n' ($LASTEXITCODE -eq 0)
} elseif (-not $bash) {
    $script:skipped++
    Write-Host '  SKIP  the fragment passes bash -n -- no Git for Windows bash found'
} else {
    Check 'the fragment passes bash -n' $false 'no fragment'
}

# --- behaviour ----------------------------------------------------------------
$canRun = $false
$why = ''
if (-not $guard) { $why = 'no fragment' }
elseif (-not $bash) { $why = 'no Git for Windows bash found' }
else {
    WriteLf (Join-Path $scratch 'probe.sh') ('read -r s < /proc/$$/stat && set -- ${s##*) } && [ -n "$2" ] && bash -c ''read -r t < /proc/$PPID/stat && echo yes; :''')
    $probe = & $bash (ToBash (Join-Path $scratch 'probe.sh')) 2>$null
    if ("$probe".Trim() -eq 'yes') { $canRun = $true } else { $why = '/proc/<pid>/stat is not readable in this bash' }
}

if (-not $canRun) {
    $script:skipped += $behaviourChecks
    Write-Host ("  SKIP  {0} behaviour checks not run -- {1}" -f $behaviourChecks, $why)
} else {
    $w = ToBash $scratch
    # The guard as shipped: it begins itself at the top of whatever sources it, taking its starter from
    # `$PPID` exactly as the top of the remote script does.
    WriteLf (Join-Path $scratch 'guard.sh') ($guard + "`n")

    # The stub run: the guard on top as in the remote script, then a child that stays, a grandchild,
    # and one in its own session; each records its pid. Then it waits, like a run queued for a slot.
    WriteLf (Join-Path $scratch 'stub.sh') (@'
set -u
orphan_dir="__W__/runs"
. "__W__/guard.sh"
echo "line-out-1"
echo "line-err-1" >&2
bash -c 'echo $$ > "__W__/pid.child"; exec sleep 300' &
bash -c 'bash -c "echo \$\$ > __W__/pid.grand; exec sleep 300" & wait' &
if command -v setsid >/dev/null 2>&1; then
    setsid bash -c 'echo $$ > "__W__/pid.session"; exec sleep 300' &
else
    bash -c 'echo $$ > "__W__/pid.session"; exec sleep 300' &
fi
echo $$ > "__W__/pid.run"
wait
'@ -replace '__W__', $w)

    # A run that talks and ends; the second half comes after the stream's reader has left (case D).
    WriteLf (Join-Path $scratch 'talk.sh') (@'
set -u
orphan_dir="__W__/runs"
. "__W__/guard.sh"
for i in 1 2 3; do echo "out $i"; echo "err $i" >&2; done
sleep 2
for i in 4 5 6; do echo "out $i"; echo "err $i" >&2; done
echo "__CAMEL_REMOTE_EXIT__=0"
'@ -replace '__W__', $w)

    WriteLf (Join-Path $scratch 'drive.sh') (@'
set -u
export CAMEL_ORPHAN_POLL_S=1 CAMEL_ORPHAN_GRACE_S=2
W="__W__"
alive() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null; }
pid_of() { cat "$W/pid.$1" 2>/dev/null; }
wait_for_pids() {
    for i in $(seq 1 40); do
        [ -s "$W/pid.run" ] && [ -s "$W/pid.child" ] && [ -s "$W/pid.grand" ] && [ -s "$W/pid.session" ] && return 0
        sleep 0.25
    done
    return 1
}
res() { echo "RESULT $1 $2"; }
logged() { cat "$W"/runs/*.err 2>/dev/null | grep -c 'is gone'; }
killall_recorded() {
    for f in run child grand session; do p=$(pid_of $f); [ -n "$p" ] && kill -9 "$p" 2>/dev/null; done
    rm -f "$W"/pid.*
}

# A: the starter (a shell that started run.sh and waits for it) is killed.
rm -rf "$W/runs"; rm -f "$W"/pid.*
bash -c "bash $W/stub.sh > $W/a.out 2> $W/a.err & wait" &
reader=$!
if wait_for_pids; then res started 1; else res started 0; fi
kill -9 "$reader" 2>/dev/null
sleep 6
gone=1
for f in run child grand session; do alive "$(pid_of $f)" && gone=0; done
res a_gone $gone
res a_logged "$(logged)"
killall_recorded

# B: the starter lives; nothing is touched over several polls.
rm -rf "$W/runs"
bash -c "bash $W/stub.sh > $W/b.out 2> $W/b.err & wait" &
reader=$!
wait_for_pids
sleep 5
kept=1
for f in run child grand session; do alive "$(pid_of $f)" || kept=0; done
res b_untouched $kept
res b_not_logged "$(logged)"
kill -9 "$reader" 2>/dev/null
killall_recorded

# C: the copy equals the stream; 25 old pairs are pruned so that 20 pairs stay.
rm -rf "$W/runs"; mkdir -p "$W/runs"
for i in $(seq 1 25); do
    : > "$W/runs/old-$i.out"; : > "$W/runs/old-$i.err"
    touch -d "2020-01-01 00:00:$(printf %02d $i)" "$W/runs/old-$i.out" "$W/runs/old-$i.err"
done
bash "$W/talk.sh" > "$W/c.out" 2> "$W/c.err"
sleep 1
cp_out=$(ls "$W"/runs/*.out | grep -v old- | head -1)
cp_err=$(ls "$W"/runs/*.err | grep -v old- | head -1)
if cmp -s "$W/c.out" "$cp_out"; then res copy_out 1; else res copy_out 0; fi
if cmp -s "$W/c.err" "$cp_err"; then res copy_err 1; else res copy_err 0; fi
res pairs "$(ls "$W"/runs/*.out | wc -l)"

# D: the stream's reader leaves after the first line; the host copy still has all seven lines.
rm -rf "$W/runs"
bash "$W/talk.sh" 2>/dev/null | head -n 1 > /dev/null
sleep 4
res kept_after_reader "$(cat "$W"/runs/*.out 2>/dev/null | wc -l)"
'@ -replace '__W__', $w)

    $raw = & $bash (ToBash (Join-Path $scratch 'drive.sh')) 2>&1
    $r = @{}
    foreach ($l in $raw) { if ("$l" -match '^RESULT (\S+) (\d+)') { $r[$Matches[1]] = [int]$Matches[2] } }
    $detail = (($raw | Out-String) -replace "`r", '').Trim()
    Check 'the stub run started its three children' ($r['started'] -eq 1) $detail
    Check 'starter killed: the run, a child, a grandchild and one in its own session are all gone' ($r['a_gone'] -eq 1) $detail
    Check 'starter killed: the host log has the line saying so' ($r['a_logged'] -eq 1) $detail
    Check 'starter alive: run and descendants are untouched' ($r['b_untouched'] -eq 1) $detail
    Check 'starter alive: nothing is logged as gone' ($r['b_not_logged'] -eq 0) $detail
    Check 'the host copy of stdout equals the stream' ($r['copy_out'] -eq 1) $detail
    Check 'the host copy of stderr equals the stream' ($r['copy_err'] -eq 1) $detail
    Check 'only the newest 20 pairs stay (19 old + this run)' ($r['pairs'] -eq 20) $detail
    Check 'the host copy keeps all of stdout after the stream''s reader left' ($r['kept_after_reader'] -eq 7) $detail
}

if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue } else { Write-Host "kept: $scratch" }
Write-Host ''
Write-Host ("{0} checks, {1} failed, {2} skipped" -f $script:checked, $script:failed, $script:skipped)
if ($script:failed -gt 0) { exit 1 }
exit 0
