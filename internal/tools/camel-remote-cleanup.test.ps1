# The host-checkout sweep: what it removes, and what it must refuse to remove.
#
# `camel-remote.ps1` removes host checkouts whose worktree is gone. On 2026-11-17 that removed trees
# whose worktree very much existed, three times, in the same minutes as a gate:
#
#   lane-e9full  died at 200/500 and at 25/500, the whole checkout gone
#   lane-e9gate  died at 300/500 with FileNotFoundError: .../out/latest/bin/camel, and even `uv_cwd`
#                failed -- the reading of a working directory that no longer existed
#
# and `e9gate`'s own note says what makes it a defect rather than bad luck: "e9full was in every
# `git worktree list`, so a correct judgement would not have deleted it."
#
# The reading was taken by
#
#     try { git -C $Path worktree list --porcelain 2>$null | ... } catch { $liveNames = @($name) }
#     if ($liveNames -notcontains $name) { $liveNames += $name }
#
# whose comment said "keep every checkout rather than delete the wrong one" and whose body said the
# opposite. PowerShell does not throw on a native command's non-zero exit, and the error went to
# `$null`, so the catch never ran; the list came back empty; the line after it appended this run's
# own name; and the host's sweep was told that exactly one checkout exists. It then removed every
# other one -- including the checkout being measured in that same minute.
#
# Part A injects the failed and partial `git worktree list` readings and checks the decision.
# Part B extracts the shipped host-side guard from camel-remote.ps1, runs it against a fake
# ~/camel-work through a real bash, and checks that it removes only what all three conditions allow.
#
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup.test.ps1
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup.test.ps1 -BashHost 88zjwei
#
# Without -BashHost, part B prints SKIP when there is no local bash either. A local `bash` is used
# when there is one.

[CmdletBinding()]
param(
    # ssh alias of a machine with bash, flock, cksum and find (the build host has all four).
    [string]$BashHost = '',
    # Keep the local scratch repository for inspection.
    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'camel-remote-live.ps1')

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

# --- Part A: the decision ----------------------------------------------------
Write-Host 'Part A: the reading that decides which host checkouts are removed'

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('wtclean-guard-' + [guid]::NewGuid().ToString('n').Substring(0, 8))
$repo = Join-Path $scratch 'main'
New-Item -ItemType Directory -Path $repo -Force | Out-Null
git -C $repo init -q
if ($LASTEXITCODE -ne 0) { throw "git init failed in $repo" }
git -C $repo -c user.name=test -c user.email=test@example.invalid commit -q --allow-empty -m base
if ($LASTEXITCODE -ne 0) { throw "git commit failed in $repo" }
foreach ($leaf in 'wt-a', 'wt-b') {
    git -C $repo worktree add -q (Join-Path $scratch $leaf) -b $leaf
    if ($LASTEXITCODE -ne 0) { throw "git worktree add failed for $leaf" }
}
$selfA = Join-Path $scratch 'wt-a'

# The pre-fix reading, kept as the thing being proved wrong. Only the `git` call is replaced by the
# injected probe; the try/catch and the two lines around it are the shipped text.
function Get-OldLiveNames {
    param([scriptblock]$GitProbe, [string]$Name)
    $liveNames = @()
    try {
        $liveNames = @(& $GitProbe | Where-Object { $_ -like 'worktree *' } |
            ForEach-Object { Split-Path -Leaf $_.Substring(9).Trim() })
    } catch {
        $liveNames = @($Name)
    }
    if ($liveNames -notcontains $Name) { $liveNames += $Name }
    return @($liveNames)
}

# The three lines of the pre-fix host loop: whatever LIVE does not name is removed.
function Get-OldSweepRemovals {
    param([string[]]$Live, [string[]]$HostDirs, [string[]]$Skip = @())
    $spelled = ' ' + ($Live -join ' ') + ' '
    return @($HostDirs | Where-Object { $Skip -notcontains $_ } |
        Where-Object { -not $spelled.Contains(' ' + $_ + ' ') })
}

# What `git worktree list` does when it cannot answer: exit 128, nothing on stdout, and -- the
# point of the whole file -- no exception.
$failingProbe = {
    $global:LASTEXITCODE = 128
    return @()
}
# A partial reading: it answers, about a repository that is not this one.
$foreignProbe = {
    $global:LASTEXITCODE = 0
    return @('worktree C:/somewhere-else/repo', 'HEAD 0000000', '')
}
# An answer that is empty but successful.
$emptyProbe = {
    $global:LASTEXITCODE = 0
    return @()
}

$hostDirs = @('Camel', 'wt-a', 'wt-b', 'rralive', 'rrabusy', 'verify', 'Camel.git', 'bin')

# 1. the defect itself, in the shipped logic: a failed query becomes one name, and that name is
#    "this run", so every other checkout is a removal candidate.
$oldOnFailure = @(Get-OldLiveNames -GitProbe $failingProbe -Name 'wt-a')
Check 'the old reading collapses a failed query to one name' `
    (($oldOnFailure.Count -eq 1) -and ($oldOnFailure[0] -eq 'wt-a')) ($oldOnFailure -join ',')
$oldRemovals = Get-OldSweepRemovals -Live $oldOnFailure -HostDirs $hostDirs -Skip @('Camel.git', 'bin')
Check 'a failed query removes every other checkout, live ones included' `
    (($oldRemovals -contains 'rralive') -and ($oldRemovals -contains 'rrabusy') -and
     ($oldRemovals -contains 'Camel') -and ($oldRemovals -contains 'wt-b')) `
    ('would remove: ' + ($oldRemovals -join ', '))

# 2. the new reading refuses to answer
$fail = Get-CamelLiveWorktrees -Path $selfA -GitProbe $failingProbe
Check 'a failed query is not trusted' (-not $fail.Trusted) ('trusted=' + $fail.Trusted)
Check 'a failed query says why' ($fail.Why -match 'exited 128') $fail.Why
Check 'a failed query produces no list to act on' `
    ((@($fail.Names) -join ',') -eq 'wt-a') (@($fail.Names) -join ',')

# 3. an empty-but-successful answer is not trusted either
$empty = Get-CamelLiveWorktrees -Path $selfA -GitProbe $emptyProbe
Check 'an empty answer is not trusted' (-not $empty.Trusted) ('trusted=' + $empty.Trusted)
Check 'an empty answer says so' ($empty.Why -match 'named no worktree') $empty.Why

# 4. an answer about another repository is not trusted
$foreign = Get-CamelLiveWorktrees -Path $selfA -GitProbe $foreignProbe
Check 'an answer that does not name this tree is not trusted' (-not $foreign.Trusted) ('trusted=' + $foreign.Trusted)
Check 'and the reason names this tree' ($foreign.Why -match 'wt-a') $foreign.Why

# 5. the real reading, and the sweep it justifies
$live = Get-CamelLiveWorktrees -Path $selfA
Check 'a real reading is trusted' $live.Trusted $live.Why
Check 'a real reading names every worktree of this repository' `
    (((@($live.Names) | Sort-Object) -join ',') -eq 'main,wt-a,wt-b') ((@($live.Names) | Sort-Object) -join ',')
$newRemovals = Get-OldSweepRemovals -Live $live.Names -HostDirs $hostDirs -Skip @('Camel.git', 'bin')
Check 'with a real reading no live worktree is a removal candidate' `
    (@($newRemovals | Where-Object { @('main', 'wt-a', 'wt-b') -contains $_ }).Count -eq 0) `
    ('candidates: ' + ($newRemovals -join ', '))
Check 'with a real reading the checkouts that are gone are still swept' `
    (@($newRemovals | Where-Object { @('rralive', 'rrabusy') -contains $_ }).Count -eq 2) `
    ('candidates: ' + ($newRemovals -join ', '))

# 6. (b) the directory, not only git's metadata: a checkout git does not name is still somebody's
$orphan = Join-Path $scratch 'wt-orphan'
New-Item -ItemType Directory -Path $orphan -Force | Out-Null
Set-Content -Path (Join-Path $orphan '.git') -Value 'gitdir: C:/nowhere'
$liveOrphan = Get-CamelLiveWorktrees -Path $selfA
Check 'a checkout beside the worktrees that git does not name is kept' `
    (@($liveOrphan.Names) -contains 'wt-orphan') ((@($liveOrphan.Names) | Sort-Object) -join ',')

# 7. and neither a plain scratch directory nor a *separate repository* beside the worktrees is read
#    as a worktree: both would exempt that name from the sweep for as long as the directory exists,
#    which is the "cleanup quietly turned off" failure this change is not allowed to introduce.
$junk = Join-Path $scratch '_logs'
New-Item -ItemType Directory -Path $junk -Force | Out-Null
$otherRepo = Join-Path $scratch 'another-project'
New-Item -ItemType Directory -Path (Join-Path $otherRepo '.git') -Force | Out-Null
$liveJunk = Get-CamelLiveWorktrees -Path $selfA
Check 'a directory that is not a checkout is not kept' `
    (-not (@($liveJunk.Names) -contains '_logs')) ((@($liveJunk.Names) | Sort-Object) -join ',')
Check 'a separate repository beside the worktrees is not kept' `
    (-not (@($liveJunk.Names) -contains 'another-project')) ((@($liveJunk.Names) | Sort-Object) -join ',')

# 8. the guard is in camel-remote.ps1 once, and it is not built out of PowerShell escaping
$srcText = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'camel-remote.ps1'))
$guardMatch = [regex]::Match($srcText, "(?s)\`$cleanupGuard = @'\r?\n(.*?)\r?\n'@")
Check 'camel-remote.ps1 carries the guard as a single-quoted here-string' $guardMatch.Success
$guard = if ($guardMatch.Success) { $guardMatch.Groups[1].Value } else { '' }
Check 'the guard needs no PowerShell escaping' (-not ($guard -match '`\$'))
Check 'the remote script is the one that carries the guard' `
    ([regex]::IsMatch($srcText, '(?m)^\$cleanupGuard$')) 'no line reading "$cleanupGuard" in the remote script'
Check 'the guard is defined once in the fragment' `
    (([regex]::Matches($guard, 'cleanup_host_checkouts\(\)')).Count -eq 1) 'the fragment defines it more than once'

# --- Part B: the guard, in bash ----------------------------------------------
Write-Host ''
Write-Host 'Part B: the guard itself, on a fake ~/camel-work'

if (-not $guard) {
    Check 'part B needs the guard fragment' $false 'it was not found in camel-remote.ps1'
} else {
    # The harness is written to a file and shipped as bytes: a pipeline through a Windows shell turns
    # every LF into CRLF, and a CR before a newline is enough to break `case ... esac`.
    #
    # What each fake checkout is for:
    #   live-a   named by LIVE                                   -> kept by (a)
    #   dead-b   absent, unlocked, unused, old                   -> removed
    #   busy-c   absent but a run holds its per-checkout lock    -> kept by (c1)
    #   fresh-d  absent but written within ten minutes           -> kept by (c3)
    #   probe-f  absent but a process has its cwd inside         -> kept by (c2)
    #   dead-e   absent, unlocked, unused, old                   -> removed
    #   Camel.git / bin / x-test-tmp                             -> never candidates
    $harness = @(
        'set -u',
        'T=$(mktemp -d)/camel-work',
        'O=$(dirname "$T")',
        'mkdir -p "$T"',
        'fails=0',
        'pass() { echo "PASS $1"; }',
        'bad() { echo "FAIL $1  ${2:-}"; fails=$((fails+1)); }',
        'mk() {',
        '    mkdir -p "$T/$1"',
        '    echo x > "$T/$1/file"',
        '    touch -d "2 hours ago" "$T/$1/file" "$T/$1"',
        '}',
        'for d in live-a dead-b busy-c fresh-d dead-e probe-f; do mk $d; done',
        'touch "$T/fresh-d/file"',
        'mkdir -p "$T/Camel.git/objects"; printf "ref: refs/heads/main\n" > "$T/Camel.git/HEAD"',
        'mkdir -p "$T/bin" "$T/x-test-tmp"',
        '# a live run holds its own checkout''s lock',
        'busykey=$(printf "%s" "$T/busy-c" | cksum | cut -d" " -f1)',
        'flock "/tmp/camel-tree-$busykey.lock" sleep 30 &',
        'lockpid=$!',
        '# a process working inside probe-f',
        '( cd "$T/probe-f" && exec sleep 30 ) &',
        'prockpid=$!',
        'sleep 0.3',
        '# the harness itself must not look like it is working inside the fake root',
        'cd /tmp',
        '',
        '# --- a reading that failed removes nothing at all',
        'cleanup_host_checkouts "$T" "live-a" "0" "Camel.git" > "$O/out0.txt" 2>&1',
        'reclaimed0=$reclaimed',
        'out=$(cat "$O/out0.txt")',
        'n=0',
        'for d in live-a dead-b busy-c fresh-d dead-e probe-f; do [ -d "$T/$d" ] && n=$((n+1)); done',
        '[ "$n" = 6 ] && pass "trusted=0 keeps all six" || bad "trusted=0 keeps all six" "kept=$n of 6"',
        '[ "$reclaimed0" = 0 ] && pass "trusted=0 reports no reclaim" || bad "trusted=0 reports no reclaim"',
        'case "$out" in *"not removing host checkouts this run"*) pass "trusted=0 prints why";; *) bad "trusted=0 prints why" "$out";; esac',
        '',
        '# --- a reading that answered removes only what nothing is using',
        'cleanup_host_checkouts "$T" "live-a Camel.git" "1" "Camel.git" > "$O/out1.txt" 2>&1',
        'reclaimed1=$reclaimed',
        'out=$(cat "$O/out1.txt")',
        'for d in live-a busy-c fresh-d probe-f Camel.git bin x-test-tmp; do',
        '    [ -d "$T/$d" ] && pass "kept $d" || bad "kept $d" "it was removed"',
        'done',
        '[ ! -d "$T/dead-b" ] && pass "removed dead-b (gone, unlocked, unused)" || bad "removed dead-b"',
        '[ ! -d "$T/dead-e" ] && pass "removed dead-e (gone, unlocked, unused)" || bad "removed dead-e"',
        '[ "$reclaimed1" = 1 ] && pass "the reclaim is reported" || bad "the reclaim is reported"',
        'case "$out" in *"keeping host checkout busy-c: a run holds its lock"*) pass "a held lock is named";; *) bad "a held lock is named" "$out";; esac',
        'case "$out" in *"keeping host checkout probe-f: a process is using it"*) pass "a process inside it is named";; *) bad "a process inside it is named" "$out";; esac',
        'case "$out" in *"keeping host checkout fresh-d: a file in it changed in the last 10 minutes"*) pass "a fresh checkout is named";; *) bad "a fresh checkout is named" "$out";; esac',
        '',
        'kill $lockpid $prockpid 2>/dev/null || true',
        'wait 2>/dev/null || true',
        'rm -rf "$O"',
        'echo "FAILS=$fails"',
        'exit "$fails"'
    ) -join "`n"
    $harness = $guard + "`n" + $harness

    $local = Join-Path ([IO.Path]::GetTempPath()) 'wtclean-guard-harness.sh'
    [IO.File]::WriteAllText($local, $harness)

    $raw = @()
    if ($BashHost) {
        $remoteName = 'wtclean-guard-test.sh'
        scp -q -o ConnectTimeout=15 $local "${BashHost}:/tmp/$remoteName"
        if ($LASTEXITCODE -ne 0) { throw "scp to $BashHost failed" }
        $raw = @(ssh -o ConnectTimeout=15 $BashHost "bash /tmp/$remoteName; echo RC=`$?")
        ssh -o ConnectTimeout=15 $BashHost "rm -f /tmp/$remoteName" | Out-Null
    } elseif (Get-Command bash -ErrorAction SilentlyContinue) {
        # A `bash` on PATH is not enough: this machine's resolves to one that cannot open a Windows
        # path at all, so the harness would "fail" for a reason that has nothing to do with the
        # guard. Read a one-line file first; only a bash that can do that runs part B.
        $probe = Join-Path ([IO.Path]::GetTempPath()) 'wtclean-guard-bashprobe.sh'
        [IO.File]::WriteAllText($probe, "echo BASHOK`n")
        $probeOut = @(& bash $probe 2>$null)
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
        if (@($probeOut) -contains 'BASHOK') {
            $raw = @(& bash $local)
            $raw += 'RC=' + $LASTEXITCODE
        } else {
            Write-Host '  SKIP  part B: the bash on PATH cannot read a Windows path; give -BashHost <ssh alias>'
        }
    } else {
        Write-Host '  SKIP  part B: no bash here; give -BashHost <ssh alias>'
    }

    if ($raw.Count -gt 0) {
        foreach ($line in $raw) {
            if ("$line" -like 'PASS *') { Check ("$line" -replace '^PASS ', '') $true }
            elseif ("$line" -like 'FAIL *') { Check ("$line" -replace '^FAIL ', '') $false }
        }
        $rc = @($raw | Where-Object { "$_" -like 'RC=*' }) | Select-Object -Last 1
        Check 'the bash guard exited 0' ("$rc" -eq 'RC=0') ("$rc")
    }
}

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
Write-Host ("{0} checks, {1} failed" -f $script:checked, $script:failed)
if ($script:failed -gt 0) { exit 1 }
exit 0