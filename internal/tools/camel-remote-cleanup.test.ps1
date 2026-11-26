# The host-checkout sweep: what it removes, and what it must refuse to remove.
#
# `camel-remote.ps1` removes host checkouts whose worktree is gone. On 2026-11-17 that removed trees
# whose worktree very much existed, three times, in the same minutes as a gate:
#
#   died at 200/500 and at 25/500, the whole checkout gone
#   died at 300/500 with FileNotFoundError: .../out/latest/bin/camel, and even `uv_cwd`
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
#   pwsh -NoProfile -File internal/tools/camel-remote-cleanup.test.ps1 -BashHost <host>
#
# Part B needs a bash that can hold a flock and read /proc: the build host always has one, and a
# local `bash` is used when the one on PATH can read a Windows path. When neither is available part
# B does not vanish from the count -- its checks are counted as skipped and the summary says how
# many -- because a part that silently does not run is green by being unread, which is how the one
# real defect in it hid for two days.

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
# Checks this run did not make. They are counted separately and printed, never dropped: a reading
# that omits what it did not run cannot be told from one that ran it green.
$script:skipped = 0

# What part B checks when it can run: one per verdict the harness prints (the number below, which the
# first `Check` after the harness's output asserts), plus its exit code and that assertion itself.
# A run without a bash counts all of them as skipped, so the total is the same number either way and
# a skipped part B cannot pass for a smaller green one.
$script:partBChecks = 40
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
. (Join-Path $PSScriptRoot 'git-bash.ps1')
$guardMatch = [regex]::Match($srcText, "(?s)\`$cleanupGuard = @'\r?\n(.*?)\r?\n'@")
Check 'camel-remote.ps1 carries the guard as a single-quoted here-string' $guardMatch.Success
$guard = if ($guardMatch.Success) { $guardMatch.Groups[1].Value } else { '' }
Check 'the guard needs no PowerShell escaping' (-not ($guard -match '`\$'))
Check 'the remote script is the one that carries the guard' `
    ([regex]::IsMatch($srcText, '(?m)^\$cleanupGuard$')) 'no line reading "$cleanupGuard" in the remote script'
Check 'the guard is defined once in the fragment' `
    (([regex]::Matches($guard, 'cleanup_host_checkouts\(\)')).Count -eq 1) 'the fragment defines it more than once'
# The sweep of staging refs is the one whose grace period is a policy, not a mechanism: it must be
# the six hours the fragment documents, and the call must pass exactly that, because a shorter one
# reaps a merge seat's own `seat/<...>` ref while the seat is still fetching it.
Check 'the staging-ref sweep is called with a six-hour grace' `
    ([regex]::IsMatch($srcText, '(?m)^cleanup_staging_refs [^\r\n]*''360''$')) `
    'the call does not pass 360'
Check 'the sweep keeps a ref it cannot date rather than removing it' `
    ([regex]::IsMatch($guard, '(?s)age could not be read.*?continue')) 'no such branch in the fragment'

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
    #   dead-b   absent, unlocked, unused, old, recorded         -> removed
    #   busy-c   absent but a run holds its per-checkout lock    -> kept by (c1)
    #   fresh-d  absent but written within ten minutes           -> kept by (c3)
    #   probe-f  absent but a process has its cwd inside         -> kept by (c2)
    #   dead-e   absent, unlocked, unused, old, recorded         -> removed
    #   stray-g  absent, idle, old, but NO creation record       -> kept by (d) and named
    #   Camel.git / bin / x-test-tmp / tmp / .records            -> never candidates
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
        '    touch -d "8 hours ago" "$T/$1/file" "$T/$1"',
        '}',
        '# the creation record a run writes when it creates a checkout (rule 12)',
        'mkrec() { mkdir -p "$T/.records"; printf "%s\t%s\t%s\n" "test-run" "2026-11-24T00:00:00+08:00" "$T/$1" > "$T/.records/$1"; }',
        'for d in live-a dead-b busy-c fresh-d dead-e probe-f stray-g; do mk $d; done',
        'for d in dead-b dead-e; do mkrec $d; done',
        'touch "$T/fresh-d/file"',
        'mkdir -p "$T/Camel.git/objects"; printf "ref: refs/heads/main\n" > "$T/Camel.git/HEAD"',
        'mkdir -p "$T/bin" "$T/x-test-tmp"',
        '# the cleanup root and its record are bookkeeping, not checkouts: both must survive the sweep',
        'mkdir -p "$T/tmp/lane-20260101-000000/checkout"',
        'touch -d "8 hours ago" "$T/tmp/lane-20260101-000000/checkout"',
        '# a live run holds its own checkout''s lock, and a process works inside probe-f. Both have to',
        '# outlive the sweep, and the sweep has no bound this file can know: it forks twice per process',
        '# per candidate checkout (one `$(readlink ...)` for the cwd, one for the exe), so its cost is',
        '# the host''s process count times its load. On 2026-11-25 one candidate took 35 s under twelve',
        '# copies of this harness on two cores (~1800 processes on the host), and the whole sweep took',
        '# minutes. A `sleep <n>` is therefore a guess about the host, and that guess is exactly what',
        '# failed: the old 30 s lifetime expired in the middle of the sweep, the guard then found',
        '# nothing inside probe-f at the instant it looked -- which is correct -- and the check that',
        '# failed was the one asserting it had named the process. That reads like a guard ignoring a',
        '# process it can see, and that is how it looked like a defect twice.',
        '#',
        '# So the lifetime is not a duration. Each fixture reads from a fifo whose write end this script',
        '# holds (`exec 9<>` below): it ends when the write end closes, which happens at this script''s',
        '# exit or when it is killed and its descriptors close. Two further details: their stdio is on',
        '# /dev/null, because a background process holding the harness''s stdout keeps an `ssh` channel',
        '# open after the harness exits (measured: the run hung on exactly that); and the lock holder',
        '# takes the lock on a descriptor the subshell opened, then `exec`s, so the lock and the process',
        '# end together -- the idiom camel-remote.ps1 uses for a real checkout.',
        'busykey=$(printf "%s" "$T/busy-c" | cksum | cut -d" " -f1)',
        'mkfifo "$O/gate-probe" "$O/gate-lock"',
        '( cd "$T/probe-f" && exec cat "$O/gate-probe" ) </dev/null >/dev/null 2>&1 &',
        'prockpid=$!',
        '( exec 8>"/tmp/camel-tree-$busykey.lock"; flock -n 8; exec cat "$O/gate-lock" ) </dev/null >/dev/null 2>&1 &',
        'lockpid=$!',
        'trap ''kill $lockpid $prockpid 2>/dev/null'' EXIT',
        '# let both readers through, and keep the write ends open until this script ends. `<>` rather than',
        '# `>`: opening a fifo only for writing waits for a reader, and a fixture that failed to start',
        '# would then hang this run instead of failing it.',
        'exec 9<>"$O/gate-probe" 10<>"$O/gate-lock"',
        '# Wait until both are visible -- and then say so. These two readings are the conditions the',
        '# sweep is about to judge; a fixture that never established one must be reported as the',
        '# fixture failing, not as the guard failing to name something that was not there.',
        'i=0; lock_held=0; seen=0',
        'while [ "$i" -lt 300 ]; do',
        '    for p in /proc/[0-9]*; do',
        '        [ "$(readlink "$p/cwd" 2>/dev/null)" = "$T/probe-f" ] && seen=1 && break',
        '    done',
        '    flock -n "/tmp/camel-tree-$busykey.lock" true 2>/dev/null || lock_held=1',
        '    [ "$seen" = 1 ] && [ "$lock_held" = 1 ] && break',
        '    sleep 0.1; i=$((i+1))',
        'done',
        '[ "$seen" = 1 ] && pass "the fixture put a process inside probe-f" || bad "the fixture put a process inside probe-f" "waited $i rounds, nothing has its cwd there"',
        '[ "$lock_held" = 1 ] && pass "the fixture holds busy-c''s lock" || bad "the fixture holds busy-c''s lock" "waited $i rounds, the lock is free"',
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
        '# The fixture''s own state at the moment the sweep is asked: if this line is ever part of a red,',
        '# it says whether the process was there and the guard did not name it (the guard''s defect) or',
        '# whether the fixture left first (this file''s).',
        'fixture_before="probe-f: pid=$prockpid cwd=$(readlink /proc/$prockpid/cwd 2>/dev/null) state=$(cut -d" " -f3 /proc/$prockpid/stat 2>/dev/null)"',
        'cleanup_host_checkouts "$T" "live-a Camel.git" "1" "Camel.git" > "$O/out1.txt" 2>&1',
        'reclaimed1=$reclaimed',
        'out=$(cat "$O/out1.txt")',
        'for d in live-a busy-c fresh-d probe-f stray-g Camel.git bin x-test-tmp tmp .records; do',
        '    [ -d "$T/$d" ] && pass "kept $d" || bad "kept $d" "it was removed"',
        'done',
        '[ ! -d "$T/dead-b" ] && pass "removed dead-b (gone, unlocked, unused, recorded)" || bad "removed dead-b"',
        '[ ! -d "$T/dead-e" ] && pass "removed dead-e (gone, unlocked, unused, recorded)" || bad "removed dead-e"',
        '[ ! -f "$T/.records/dead-b" ] && pass "the record goes with the checkout it describes" || bad "record removed"',
        '[ "$reclaimed1" = 1 ] && pass "the reclaim is reported" || bad "the reclaim is reported"',
        'case "$out" in *"keeping host checkout busy-c: a run holds its lock"*) pass "a held lock is named";; *) bad "a held lock is named" "lock_held=$lock_held; $out";; esac',
        'case "$out" in *"keeping host checkout probe-f: a process is using it"*) pass "a process inside it is named";; *) bad "a process inside it is named" "seen=$seen, swept with [$fixture_before]; $out";; esac',
        'case "$out" in *"keeping host checkout fresh-d: a file in it changed in the last 10 minutes"*) pass "a fresh checkout is named";; *) bad "a fresh checkout is named" "$out";; esac',
        '# (d) the creation record: idle and old is not enough, the sweep must find a record',
        'case "$out" in *"keeping host checkout stray-g: no creation record"*) pass "an unrecorded checkout is kept and named";; *) bad "unrecorded checkout kept" "$out";; esac',
        '[ -d "$T/stray-g" ] && pass "the unrecorded checkout is still there" || bad "unrecorded checkout still there"',
        '[ -d "$T/tmp/lane-20260101-000000/checkout" ] && pass "the cleanup root is not swept" || bad "cleanup root swept"',
        '',
        '# --- staging refs follow the branches that exist here',
        '# The bare keeps one ref per branch that ever ran (that is what makes the next push of the',
        '# branch small), so the sweep is the only thing that ever removes one. What each fixture is for:',
        '#   live-one        named in the keep list            -> kept, twice',
        '#   camel-sha-*     a detached run''s commit entry      -> kept',
        '#   camel-baseline-* a baseline checkout''s entry      -> kept',
        '#   gone-one        not a branch here, old            -> removed and named',
        '#   young-unknown   not a branch here, written now    -> kept by the grace period',
        'S="$T/staging/Camel.git"',
        'git init -q --bare "$S" && pass "the staging fixture is a bare repository" || bad "the staging fixture is a bare repository"',
        'emptytree=$(git -C "$S" hash-object -t tree /dev/null)',
        'c1=$(git -C "$S" -c user.name=t -c user.email=t@e commit-tree "$emptytree" -m one)',
        'for r in live-one gone-one camel-sha-abc123 camel-baseline-abc123def456 young-unknown; do git -C "$S" update-ref "refs/heads/$r" "$c1"; done',
        'touch -d "8 hours ago" "$S/refs/heads/live-one" "$S/refs/heads/gone-one" "$S/refs/heads/camel-sha-abc123" "$S/refs/heads/camel-baseline-abc123def456"',
        'cleanup_staging_refs "$S" "live-one develop" "1" "360" > "$O/refs1.txt" 2>&1',
        'removed1=$staging_refs_removed',
        'out=$(cat "$O/refs1.txt")',
        'git -C "$S" rev-parse --verify --quiet refs/heads/gone-one >/dev/null && bad "a ref with no local branch is removed" || pass "a ref with no local branch is removed"',
        'case "$out" in *"removed staging ref refs/heads/gone-one: no local branch of that name"*) pass "the removed ref is named";; *) bad "the removed ref is named" "$out";; esac',
        'git -C "$S" rev-parse --verify --quiet refs/heads/live-one >/dev/null && pass "a ref whose branch exists is kept" || bad "a ref whose branch exists is kept"',
        'git -C "$S" rev-parse --verify --quiet refs/heads/camel-sha-abc123 >/dev/null && pass "a detached run''s commit ref is kept" || bad "a detached run''s commit ref is kept"',
        'git -C "$S" rev-parse --verify --quiet refs/heads/camel-baseline-abc123def456 >/dev/null && pass "a baseline checkout''s commit ref is kept" || bad "a baseline checkout''s commit ref is kept"',
        'git -C "$S" rev-parse --verify --quiet refs/heads/young-unknown >/dev/null && pass "a ref written inside the grace period is kept" || bad "a ref written inside the grace period is kept"',
        'case "$out" in *"staging refs: 5, removed 1"*) pass "the sweep reports both counts";; *) bad "the sweep reports both counts" "$out";; esac',
        '[ "$removed1" = 1 ] && pass "the reclaim is reported to the caller" || bad "the reclaim is reported to the caller" "removed=$removed1"',
        'cleanup_staging_refs "$S" "live-one develop" "1" "360" > "$O/refs2.txt" 2>&1',
        'removed2=$staging_refs_removed',
        'git -C "$S" rev-parse --verify --quiet refs/heads/live-one >/dev/null && pass "a second sweep still keeps it" || bad "a second sweep still keeps it"',
        '[ "$removed2" = 0 ] && pass "a second sweep removes nothing" || bad "a second sweep removes nothing" "removed=$removed2"',
        '# A branch list that could not be read is not a list of no branches: nothing may go.',
        'git -C "$S" update-ref refs/heads/gone-two "$c1"',
        'touch -d "8 hours ago" "$S/refs/heads/gone-two"',
        'cleanup_staging_refs "$S" "live-one" "0" "360" > "$O/refs3.txt" 2>&1',
        'removed3=$staging_refs_removed',
        'out=$(cat "$O/refs3.txt")',
        'git -C "$S" rev-parse --verify --quiet refs/heads/gone-two >/dev/null && pass "a failed branch reading removes nothing" || bad "a failed branch reading removes nothing" "$out"',
        'case "$out" in *"could not say which branches exist"*) pass "a failed branch reading says why";; *) bad "a failed branch reading says why" "$out";; esac',
        '[ "$removed3" = 0 ] && pass "a failed branch reading reports no reclaim" || bad "a failed branch reading reports no reclaim" "removed=$removed3"',
        'cleanup_staging_refs "$T/nope" "live-one" "1" "360" > "$O/refs4.txt" 2>&1',
        'out=$(cat "$O/refs4.txt")',
        'case "$out" in *"is not a repository this run can read"*) pass "a bare that cannot be read is named and left alone";; *) bad "a bare that cannot be read is named" "$out";; esac',
        '',
        '# Closing the gates is what ends the two fixtures: their readers see EOF and exit.',
        'exec 9>&- 10>&-',
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
    } elseif ($gitBash = Get-GitBash) {
        # A `bash` on PATH is not enough: this machine's resolves to one that cannot open a Windows
        # path at all, so the harness would "fail" for a reason that has nothing to do with the
        # guard. Read a one-line file first; only a bash that can do that runs part B.
        $probe = Join-Path ([IO.Path]::GetTempPath()) 'wtclean-guard-bashprobe.sh'
        [IO.File]::WriteAllText($probe, "echo BASHOK`n")
        $probeOut = @(& $gitBash $probe 2>$null)
        Remove-Item $probe -Force -ErrorAction SilentlyContinue
        if (@($probeOut) -contains 'BASHOK') {
            $raw = @(& $gitBash $local)
            $raw += 'RC=' + $LASTEXITCODE
        } else {
            Skip 'the bash on PATH cannot read a Windows path; give -BashHost <ssh alias>'
        }
    } else {
        Skip 'no bash on this machine; give -BashHost <ssh alias>'
    }

    if ($raw.Count -gt 0) {
        # The harness prints one verdict line per check, and `bad <name> <reason>` puts the reason
        # after two spaces with the rest of it -- the guard's own output -- on the lines that follow.
        # Those lines used to be dropped here, so a red named a check and never showed what the
        # guard said; that is why the reading was indistinguishable from the guard ignoring a
        # process it could see. They are the detail of the FAIL now.
        $verdicts = New-Object System.Collections.ArrayList
        foreach ($line in $raw) {
            $s = "$line"
            if ($s -like 'PASS *' -or $s -like 'FAIL *') {
                $ok = $s -like 'PASS *'
                [void]$verdicts.Add([pscustomobject]@{
                        Ok   = $ok
                        Text = ($s -replace '^(PASS|FAIL) ', '')
                        More = @()
                    })
            } elseif ($verdicts.Count -gt 0 -and -not $verdicts[$verdicts.Count - 1].Ok -and $s.Trim()) {
                $verdicts[$verdicts.Count - 1].More += $s
            }
        }
        foreach ($v in $verdicts) {
            $name = ($v.Text -split '  ', 2)[0]
            $detail = @($v.More)
            if ($v.Text.Length -gt $name.Length + 2) { $detail = @($v.Text.Substring($name.Length + 2)) + $detail }
            Check $name $v.Ok (@($detail) -join "`n")
        }
        # Part B's promise, in one cell: the number skipped below is this many plus the exit code and
        # this check, and a harness that grew or lost a check without the number moving fails here
        # rather than making a skipped run look like a smaller run.
        Check 'part B made the number of checks it promises' ($verdicts.Count -eq $script:partBChecks) `
            ("$($verdicts.Count) verdicts, $script:partBChecks promised")
        $rc = @($raw | Where-Object { "$_" -like 'RC=*' }) | Select-Object -Last 1
        Check 'the bash guard exited 0' ("$rc" -eq 'RC=0') ("$rc")
    }
}

# --- result ------------------------------------------------------------------
Write-Host ''
if (-not $Keep) { Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue }
$total = $script:checked + $script:skipped
if ($script:skipped -gt 0) {
    Write-Host ("{0} checks, {1} failed, {2} skipped" -f $total, $script:failed, $script:skipped)
} else {
    Write-Host ("{0} checks, {1} failed" -f $total, $script:failed)
}
if ($script:failed -gt 0) { exit 1 }
exit 0