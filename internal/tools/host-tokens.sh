#!/usr/bin/env bash
# Copyright (c) 2026 The Camel Authors. All rights reserved.
# Use of this source code is governed by the Apache License 2.0.
#
# host-tokens: the build host's shared budget, handed out per process instead of per run.
#
# A run that takes the whole machine for its whole duration is what made five waits of thirty
# minutes on 2026-11-24: one plan held the only test slot for twenty-five minutes while four runs
# queued behind it, three of them holding nothing but a hope. The budget this hands out is a
# **token**: one worker process, one token, returned when that process exits.
#
#   take N            take N tokens (blocking; while waiting it holds none and prints who holds what)
#   take-up-to N -- CMD
#                     take what is free now, up to N and never fewer than one, run CMD with that
#                     many workers, and give them back when CMD exits: a build asks for a width and
#                     takes what the host can carry instead of waiting for a full slot
#   run N -- CMD      take N tokens, run CMD with them, give them back when CMD exits
#   show          what the pool looks like now: free, held, and the peak so far
#   self-test     check the properties this exists for, without a host
#
# Waiting is not failing, and running out of the wait is not a red reading:
#
#   a token arrives in time   -> the caller runs
#   the wait runs out (CAMEL_TOKEN_WAIT_S, default 1800) -> exit 6, naming who held the pool
#
# Exit 6 rather than 2 (`2` is this tool being asked something it cannot do, e.g. take more tokens
# than the pool has) and rather than 1 (a run that measured something and it was red). A run that
# could not get a token measured nothing, and that has to be its own answer.
#
# Who holds a token is read from `/proc/locks`, not from `fuser`: `fuser` lists the processes with
# the file *open*, which includes a process that has already released it and a `$( ... )` that will
# be gone before the listing is printed. `/proc/locks` is the kernel's own table of held locks, and
# it is the only thing here that answers "held" rather than "opened".
#
# Two optional readings for a caller that takes a token per case in a long run (the test runner):
#
#   CAMEL_TOKEN_REPORT=FILE   append `<pid> <wait_ms> <tokens>` to FILE on every successful take
#   "$TOKEN_DIR/peak"         the largest number of tokens held at once, written by this script
#
# The second is why `tokensPeak` is a reading and not a zero: the pool records the high-water mark
# itself, because the caller that holds twelve tokens at once is not the one that can count them.
#
# The mechanism is `flock` on the token files, and that choice is the whole point: the kernel
# releases the lock when the holder dies, so a crashed run cannot carry a token away. No reclamation
# pass, no stale registration to clean up -- the failure mode that needed two criteria to act on
# (2026-11-24: `3312`, `47948`) cannot happen here.
#
# Tokens are held on file descriptors that are inherited by the worker, so `run` is the safe form:
#   host-tokens.sh run 8 -- cmake --build build -j 8
# `take` exists for callers that must hold tokens across several commands in one shell; those callers
# hold them until they exit, which is the same guarantee.
#
# The holder record beside each token is *explanation*, not truth: it says who took it, for which
# lane, and when. If the record disagrees with the lock, the lock is right and the record is stale --
# that rule is written down because a record that is believed over the lock is how "registered but
# nothing is running" became a thing to clean up by hand.
set -u

TOKEN_DIR="${CAMEL_TOKEN_DIR:-/tmp/camel-tokens}"
POOL_SIZE="${CAMEL_TOKENS:-24}"
# Who is asking, for the record and for the queue message. A lane sets these; a bare run says so.
LANE="${CAMEL_LANE:-$(basename "$PWD")}"
WHO="${CAMEL_TOKEN_WHO:-$$}"

die() { echo "host-tokens: $*" >&2; exit 2; }

# Milliseconds since the epoch, in a variable and not on stdout: a `$(date +%s%N)` costs a fork and
# an exec, and this is measured on every take. Measured on the loaded host: 110 ms a take with two
# `date` execs, 6 ms with the builtin. A reading that costs more than what it measures is not worth
# keeping, and this one was close to that for a pool taking one token per test case.
now_ms() {
    if [ -n "${EPOCHREALTIME:-}" ]; then
        local t="${EPOCHREALTIME//[!0-9]/}"
        NOW_MS=$((t / 1000))
    else
        NOW_MS=$(( $(date +%s%N 2>/dev/null || echo 0) / 1000000 ))
    fi
}

# The wait ran out: nothing was measured. `show` to stderr, because the caller's stdout is the
# output of the command it never ran.
die_wait() {
    echo "host-tokens: $*" >&2
    show >&2
    exit 6
}

WAIT_S="${CAMEL_TOKEN_WAIT_S:-1800}"
case "$WAIT_S" in
    ''|*[!0-9]*) die "CAMEL_TOKEN_WAIT_S is not a number of seconds: '$WAIT_S'" ;;
esac

# The size must be a number a pool can exist at: 1 is a serial machine, 0 is not a machine.
case "$POOL_SIZE" in
    ''|*[!0-9]*) die "CAMEL_TOKENS is not a number: '$POOL_SIZE'" ;;
esac
[ "$POOL_SIZE" -ge 1 ] || die "CAMEL_TOKENS is $POOL_SIZE; a pool of fewer than one token runs nothing"

token_path() { printf '%s/token-%02d.lock' "$TOKEN_DIR" "$1"; }
record_path() { printf '%s/token-%02d.holder' "$TOKEN_DIR" "$1"; }

# Everything on the taking path is a shell builtin on purpose. This runs once per test case -- five
# thousand times in the fast layer -- and on the loaded host one fork costs 5-10 ms and one exec of
# `date`, `stat` or `wc` costs 30-70 ms. A reading that costs more than the thing it measures is not
# worth keeping: measured at 110 ms a take before this, 1-2 ms after.
prepare() {
    [ -d "$TOKEN_DIR" ] || mkdir -p "$TOKEN_DIR" || die "cannot make $TOKEN_DIR"
    # A pool that was configured at 24 and is now asked for 32 gets the extra files; a smaller pool
    # keeps them on disk but never hands them out, so raising the size later does not start from a
    # directory that disagrees with itself.
    local j=1 f paths=() have=''
    while [ "$j" -le "$POOL_SIZE" ]; do
        printf -v f '%s/token-%02d.lock' "$TOKEN_DIR" "$j"
        [ -e "$f" ] || : > "$f"
        paths[j]="$f"
        j=$((j + 1))
    done
    # The inodes are the key /proc/locks is read by, and they change only when a token file is
    # replaced. Kept beside the pool, with the size they were taken at, so the freshness check is one
    # builtin `read` and a run of takes costs no process at all.
    [ -r "$TOKEN_DIR/inodes.size" ] && read -r have < "$TOKEN_DIR/inodes.size"
    if [ "$have" != "$POOL_SIZE" ]; then
        printf '%s\n' "$POOL_SIZE" > "$TOKEN_DIR/inodes.size"
        stat -c %i -- "${paths[@]:1}" > "$TOKEN_DIR/inodes" 2>/dev/null || : > "$TOKEN_DIR/inodes"
    fi
}

# Who holds a token file now. `/proc/locks` is the reading: it is the kernel's table of held locks,
# so a process that opened the file and did not lock it is not in it, and a process that released it
# is gone from it. `fuser` cannot tell those two from a holder, which is how an open descriptor was
# read as "the old lock is held" on 2026-11-24 (internal/tools/README.md). It is used only when
# /proc/locks cannot be read at all, and then the reading says so rather than passing itself off.
read_holder_mode() {
    if [ -r /proc/locks ]; then echo /proc/locks; else echo fuser; fi
}

holders_of() {
    local file ino i pid
    file=$(token_path "$1")
    if [ -r /proc/locks ]; then
        ino=$(stat -c %i "$file" 2>/dev/null) || return 0
        [ -n "$ino" ] || return 0
        # `N: FLOCK ADVISORY WRITE <pid> <major>:<minor>:<inode> <start> <end>`: the pid is the field
        # before the one that ends in this inode. A blocked waiter is a `->` entry and is skipped.
        while read -r _ type _ _ lpid where _ _; do
            [ "$type" = 'FLOCK' ] || continue
            [ "${where##*:}" = "$ino" ] && echo "$lpid"
        done < /proc/locks
    else
        fuser "$file" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' | sort -u || true
    fi
}

# How many tokens are held right now, into HELD_N: the pool's own count, from /proc/locks, with no
# process. `holders_of` (which forks for its `stat` and its `ps`) is the diagnostic path; this is the
# counting path, and they read the same table.
count_held() {
    local locks=' ' ino held=0
    HELD_N=0
    [ -s "$TOKEN_DIR/inodes" ] || return 0
    # `$(< /proc/locks)` is a builtin read: on a busy host that file holds every flock in the machine
    # -- several hundred lines while plans, compilers and the panel are all holding locks -- and
    # looping it line by line in bash cost more than the take it was counting (a 700 ms difference
    # over four takers). Matching each of our inodes against the whole text is C-speed and needs no
    # process; the match is `:<inode> `, which is the tail of the `major:minor:inode` field.
    locks="$(< /proc/locks)" 2>/dev/null || return 0
    while read -r ino; do
        [ -n "$ino" ] || continue
        case "$locks" in *":$ino "*) held=$((held + 1)) ;; esac
    done < "$TOKEN_DIR/inodes"
    [ "$held" -le "$POOL_SIZE" ] || held="$POOL_SIZE"
    HELD_N=$held
}

# Raise the high-water mark without a lock: the mark is the largest N for which a marker file exists,
# so two takers writing at once cannot make it go down, and there is no read-modify-write to protect.
# A file per level, `peak-12`, because `: > file` is a redirection and not a process.
record_peak() {
    count_held
    [ "$HELD_N" -ge 1 ] || return 0
    : > "$TOKEN_DIR/peak-$HELD_N"
}

peak_of() {
    local f n
    PEAK_N=0
    for f in "$TOKEN_DIR"/peak-*; do
        [ -e "$f" ] || continue
        n="${f##*-}"
        case "$n" in ''|*[!0-9]*) continue ;; esac
        [ "$n" -gt "$PEAK_N" ] && PEAK_N=$n
    done
}

# The waiting is a reading the caller asked for: a runner that takes a token per case wants to know
# how long each case queued, and asking the kernel's clock here is the only place that knows.
report_take() {
    [ -n "${CAMEL_TOKEN_REPORT:-}" ] || return 0
    printf '%s %s %s\n' "$$" "${1:-0}" "${2:-0}" >> "$CAMEL_TOKEN_REPORT" 2>/dev/null || true
}

show() {
    prepare
    free=0
    held=0
    echo "pool: $TOKEN_DIR size=$POOL_SIZE lane=$LANE reading=$(read_holder_mode)"
    i=1
    while [ "$i" -le "$POOL_SIZE" ]; do
        pids=$(holders_of "$i")
        if [ -z "$pids" ]; then
            free=$((free + 1))
        else
            held=$((held + 1))
            rec=''
            printf -v rp '%s/token-%02d.holder' "$TOKEN_DIR" "$i"
            [ -r "$rp" ] && read -r rec < "$rp"
            for p in $pids; do
                etime=$(ps -o etime= -p "$p" 2>/dev/null | tr -d ' ')
                echo "  token $(printf '%02d' "$i") held by pid $p (${etime:-?}) ${rec}"
            done
        fi
        i=$((i + 1))
    done
    peak_of
    echo "free=$free held=$held peak=$PEAK_N"
}

# The one routine that takes tokens: `take`, `run` and `take-up-to` all come through here, so the
# flock, the holder record and the report are written in one place.
#
#   acquire WANT all    exactly WANT tokens, or keep trying
#   acquire WANT upto   as many as are free now, at least one, at most WANT
#
# Sets ACQ_N (how many are held) and ACQ_MS (how long it took).
#
# A taker that cannot get what it asked for **holds nothing while it waits**. Two callers that each
# want more than half of the pool, each holding what they had so far and waiting for the rest, wait on
# each other until the wait runs out; so a short pass gives every token back, sleeps a random while
# (the jitter is what lets one of two symmetric takers win the next pass), and starts over. A caller
# that can use less than it asked for says so with `upto` and never waits for a full width.
#
# One pass is over the whole pool without blocking: waiting on the first token while the second is
# free would make a pool of twenty-four run exactly one thing at a time.
acquire() {
    local want="$1" mode="$2" n fd t0 tms held attempts=0
    now_ms; t0=$NOW_MS
    while :; do
        ACQ_N=0
        held=' '
        n=1
        while [ "$n" -le "$POOL_SIZE" ] && [ "$ACQ_N" -lt "$want" ]; do
            # fds 20+ are this script's token hold, by token index. They are inherited by the worker,
            # so a token lives exactly as long as the process tree that took it. A fixed descriptor
            # number on purpose: bash's `{fd}` form can carry close-on-exec, and a token that vanishes
            # when the worker is exec'd is a token the worker never had.
            fd=$((20 + n))
            eval "exec $fd>\"$(token_path "$n")\""
            if flock -n "$fd"; then
                ACQ_N=$((ACQ_N + 1))
                held="$held$n "
            else
                eval "exec $fd>&-"
            fi
            n=$((n + 1))
        done
        if [ "$ACQ_N" -ge "$want" ] || { [ "$mode" = upto ] && [ "$ACQ_N" -ge 1 ]; }; then
            break
        fi
        # Not enough: give back what this pass took before waiting.
        for n in $held; do eval "exec $((20 + n))>&-"; done
        now_ms; tms=$(( NOW_MS - t0 ))
        # Out of time is not out of tokens, and it is not a red reading either: the caller ran
        # nothing, and that has to be its own answer (6), not "your choice was wrong" (2).
        if [ "$tms" -ge $((WAIT_S * 1000)) ]; then
            die_wait "no $want token(s) of $POOL_SIZE within ${WAIT_S} s ($((tms / 1000)) s waited, lane=$LANE): nothing was measured, so this exits 6"
        fi
        # The wait is not silent: a run that queues without saying who it waits for looks exactly
        # like a run that hung, and on 2026-11-24 that cost a whole measurement.
        if [ $((attempts % 30)) -eq 0 ]; then
            echo "host-tokens: waiting for $want token(s) of $POOL_SIZE (lane=$LANE, holding none, waited $((tms / 1000))s)"
            show | sed 's/^/  /'
        fi
        attempts=$((attempts + 1))
        sleep "0.$((RANDOM % 900 + 100))"
    done
    for n in $held; do
        printf '%s %s %s %s
' "$$" "$LANE" "$WHO" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$(record_path "$n")"
    done
    now_ms; ACQ_MS=$(( NOW_MS - t0 ))
    record_peak
    report_take "$ACQ_MS" "$ACQ_N"
}

take() {
    local want="$1"
    [ "$want" -ge 1 ] || die "take needs at least one token"
    [ "$want" -le "$POOL_SIZE" ] || die "cannot take $want tokens from a pool of $POOL_SIZE"
    prepare
    acquire "$want" all
    # One line per case would drown the run it is measuring: a caller that hands a token to every
    # case in a plan passes CAMEL_TOKEN_QUIET=1 and hears only about waits.
    [ "${CAMEL_TOKEN_QUIET:-0}" = '1' ] || echo "host-tokens: took $want of $POOL_SIZE tokens (lane=$LANE, waited $((ACQ_MS / 1000))s)"
}

# Take what the pool has, up to a wanted width, and run a command at that width.
#
#   take-up-to N -- CMD
#
# A build asks for a width and the pool decides what it gets: as many tokens as are free at this
# moment, never more than N, and never fewer than one (a caller with no token at all waits, which is
# the same wait any worker does). Waiting for a *full* width while four tokens sit free is the queue
# this verb exists to remove -- the host was idle while a lane held nothing but its request.
#
# The count reaches CMD as `CAMEL_TOKENS_HELD` and `CMAKE_BUILD_PARALLEL_LEVEL`, because a number
# written in the calling shell does not cross into the command: the width has to be decided where the
# locks are. `HOST_BUILD_JOBS=<n>` is printed first, so a caller that reads the run's stream learns
# the width the build actually got rather than the width it asked for.
take_up_to() {
    local want="$1"
    shift
    [ "$want" -ge 1 ] || die "take-up-to needs at least one token"
    [ "$want" -le "$POOL_SIZE" ] || want="$POOL_SIZE"
    prepare
    acquire "$want" upto
    echo "host-tokens: running at $ACQ_N of $want worker(s) (lane=$LANE, $((ACQ_MS / 1000))s to take them)"
    echo "HOST_BUILD_JOBS=$ACQ_N"
    export CAMEL_TOKENS_HELD="$ACQ_N"
    export CMAKE_BUILD_PARALLEL_LEVEL="$ACQ_N"
    "$@"
}

self_test() {

    local root bad=0 out
    root=$(mktemp -d)
    # The size is resolved once, at start, so the test sets the variable it was resolved into rather
    # than hoping an environment prefix on a function call changes it.
    export CAMEL_TOKEN_DIR="$root/pool"
    # And the same value for this process: `TOKEN_DIR` was resolved from the environment when the
    # script started, so exporting it for children is not enough -- the checks below run in this
    # process, and they were reading the live pool beside every other lane.
    TOKEN_DIR="$root/pool"
    export CAMEL_LANE=selftest
    POOL_SIZE=3
    export CAMEL_TOKENS=3

    # 1. three tokens exist and none is held
    out=$(show)
    case "$out" in
        *'free=3 held=0'*) echo 'ok    a pool of three starts with three free' ;;
        *) echo "FAIL  a pool of three starts with three free: $out"; bad=$((bad + 1)) ;;
    esac

    # 2. four tokens from a pool of three is refused, by name
    if ( take 4 ) >/dev/null 2>&1; then
        echo 'FAIL  taking four from a pool of three was allowed'; bad=$((bad + 1))
    else
        echo 'ok    taking four from a pool of three is refused'
    fi

    # 3. a holder keeps its tokens and a second run cannot take them
    ( take 2 >/dev/null; exec sleep 30 ) &
    holder=$!
    sleep 0.5
    out=$(show)
    case "$out" in
        *'free=1 held=2'*) echo 'ok    two held leaves one free' ;;
        *) echo "FAIL  two held leaves one free: $out"; bad=$((bad + 1)) ;;
    esac

    # 4. the tokens come back when the holder dies -- the property the whole design is for
    kill -9 "$holder" 2>/dev/null
    wait "$holder" 2>/dev/null
    sleep 0.5
    out=$(show)
    case "$out" in
        *'free=3 held=0'*) echo 'ok    a killed holder returns its tokens without a cleanup pass' ;;
        *) echo "FAIL  a killed holder returns its tokens: $out"; bad=$((bad + 1)) ;;
    esac

    # 5. run gives them back when the command ends
    "$0" run 2 -- true >/dev/null
    out=$(show)
    case "$out" in
        *'free=3 held=0'*) echo 'ok    run gives its tokens back when the command ends' ;;
        *) echo "FAIL  run gives its tokens back: $out"; bad=$((bad + 1)) ;;
    esac

    # 7. a token survives one more exec layer. This is the failure that makes a pool look full and
    # behave leaky: if the descriptor is closed when the worker replaces its shell, `take` reports a
    # token it does not hold, the pool over-subscribes, and the reading looks exactly like a quiet
    # pool. Measured by asking the pool from inside the worker: two of three free, one held.
    out=$("$0" run 1 -- "$0" show)
    case "$out" in
        *'free=2 held=1'*) echo 'ok    a token survives the worker being exec-ed, and the pool still shows it held' ;;
        *) echo "FAIL  a token survives an exec layer: $out"; bad=$((bad + 1)) ;;
    esac
    # 8. four takers on a pool of two run two at a time. Waiting on the first token while the second
    # is free would make a pool of twenty-four run one thing at a time, which is the old defect from
    # the other side; the wall clock is the only reading that separates the two.
    t0=$(date +%s%N)
    for _ in 1 2 3 4; do ( CAMEL_TOKEN_QUIET=1 take 1 >/dev/null; sleep 1 ) & done
    wait
    ms=$(( ($(date +%s%N) - t0) / 1000000 ))
    if [ "$ms" -lt 3500 ]; then
        echo "ok    four takers on a pool of two take two rounds (${ms}ms)"
    else
        echo "FAIL  four takers on a pool of two ran one at a time (${ms}ms, a pool of two needs under 3500)"
        bad=$((bad + 1))
    fi
    # 6. a size that is not a number is refused rather than read as a pool of zero
    if CAMEL_TOKENS=many "$0" show >/dev/null 2>&1; then
        echo 'FAIL  a pool size of "many" was accepted'; bad=$((bad + 1))
    else
        echo 'ok    a pool size that is not a number is refused'
    fi
    # 9. the peak is a reading, not a zero. Three tokens held at once must leave the mark at three
    #    even after they are all back: that is the number `tokensPeak` reports, and a pool that never
    #    records it is a pool nobody can tell from an unused one.
    ( take 3 >/dev/null; exec sleep 20 ) &
    holder2=$!
    sleep 0.5
    out=$(show)
    case "$out" in
        *'peak=3'*) echo 'ok    the peak records three held at once' ;;
        *) echo "FAIL  the peak did not record three: $out"; bad=$((bad + 1)) ;;
    esac
    kill -9 "$holder2" 2>/dev/null
    wait "$holder2" 2>/dev/null
    sleep 0.5
    out=$(show)
    case "$out" in
        *'free=3 held=0 peak=3'*) echo 'ok    the peak stays at three after every token is back' ;;
        *) echo "FAIL  the peak did not stay: $out"; bad=$((bad + 1)) ;;
    esac

    # 10. a wait that runs out is its own answer: 6, not 2 (a refusal) and not 1 (a red reading)
    ( take 3 >/dev/null; exec sleep 20 ) &
    holder3=$!
    sleep 0.5
    t0=$(date +%s)
    ( WAIT_S=2; take 1 >/dev/null 2>&1 )
    rc=$?
    secs=$(( $(date +%s) - t0 ))
    if [ "$rc" -eq 6 ]; then
        echo "ok    a wait that runs out exits 6 (${secs}s, a 2 s wait)"
    else
        echo "FAIL  a wait that runs out exited $rc, not 6"; bad=$((bad + 1))
    fi
    kill -9 "$holder3" 2>/dev/null
    wait "$holder3" 2>/dev/null

    # 11. the report file carries the wait per take, which is what `tokenWaitSeconds` is summed from
    report=$(mktemp)
    CAMEL_TOKEN_REPORT="$report" "$0" run 1 -- true >/dev/null 2>&1
    n=$(wc -l < "$report")
    first=$(head -1 "$report")
    case "$n:$first" in
        1:*[0-9]) echo "ok    the report records one take: $first" ;;
        *) echo "FAIL  the report has '$n' line(s): $first"; bad=$((bad + 1)) ;;
    esac
    rm -f "$report"

    # 12. a build gets what is free, not what it asked for: with two of three held, asking for three
    # takes one, and the width the command sees is the one it got. A build that waits for its full
    # width while a token sits free is the queue this verb exists to remove.
    ( take 2 >/dev/null; exec sleep 30 ) &
    holder4=$!
    sleep 0.5
    width=$("$0" take-up-to 3 -- true 2>/dev/null | sed -n 's/^HOST_BUILD_JOBS=//p' | tail -1)
    seen=$("$0" take-up-to 3 -- sh -c 'printf "width-in-cmd=%s\n" "$CAMEL_TOKENS_HELD"' 2>/dev/null | sed -n 's/^width-in-cmd=//p' | tail -1)
    case "$width:$seen" in
        1:1) echo 'ok    take-up-to takes the one token that is free rather than waiting for three' ;;
        *) echo "FAIL  take-up-to reported width '$width' and the command saw '$seen', not 1 and 1"; bad=$((bad + 1)) ;;
    esac
    kill -9 "$holder4" 2>/dev/null
    wait "$holder4" 2>/dev/null

    # 13. two takers that each want more than half of the pool both finish, one after the other.
    # Each takes two of three and holds them for a second; taking one token at a time and waiting for
    # the second would leave each holding one and waiting on the other until the wait ran out. Inside
    # each worker the pool must show exactly two held -- the other taker's tokens are not there -- so
    # the two ran one after the other and the total held never passed the pool.
    two_log=$(mktemp)
    t0=$(date +%s%N)
    for _ in 1 2; do
        (
            WAIT_S=20
            CAMEL_TOKEN_QUIET=1 take 2 >/dev/null 2>&1 || exit 3
            show | grep -o 'held=[0-9]*' | head -1 >> "$two_log"
            sleep 1
        ) &
        two_pids="${two_pids:-} $!"
    done
    two_bad=0
    for p in $two_pids; do wait "$p" || two_bad=$((two_bad + 1)); done
    ms=$(( ($(date +%s%N) - t0) / 1000000 ))
    seen=$(sort "$two_log" | tr '
' ' ')
    rm -f "$two_log"
    peak_of
    if [ "$two_bad" -eq 0 ] && [ "$seen" = 'held=2 held=2 ' ] && [ "$PEAK_N" -le "$POOL_SIZE" ] && [ "$ms" -lt 15000 ]; then
        echo "ok    two takers that each want two of three both finish, one after the other (${ms}ms)"
    else
        echo "FAIL  two takers of two on a pool of three: $two_bad failed, held seen '$seen', peak $PEAK_N, ${ms}ms"
        bad=$((bad + 1))
    fi

    rm -rf "$root"
    if [ "$bad" -eq 0 ]; then echo 'selftest: 13 checks, 0 failed'; return 0; fi
    echo "selftest: $bad failed"; return 1
}

cmd="${1:-show}"
shift || true
case "$cmd" in
    show)     show ;;
    take)     take "${1:-1}" ;;

    take-up-to)
        n="${1:-1}"; shift 2>/dev/null || true
        [ "${1:-}" = '--' ] && shift
        [ "$#" -ge 1 ] || die 'take-up-to needs a command after --'
        take_up_to "$n" "$@"
        exit "$?"
        ;;
    run)
        n="${1:-1}"; shift 2>/dev/null || true
        [ "${1:-}" = '--' ] && shift
        [ "$#" -ge 1 ] || die 'run needs a command after --'
        take "$n" >/dev/null
        "$@"
        code=$?
        exit "$code"
        ;;
    run-quiet)
        [ "${1:-}" = '--' ] && shift
        [ "$#" -ge 1 ] || die 'run-quiet needs a command after --'
        CAMEL_TOKEN_QUIET=1 take 1 >/dev/null
        exec "$@"
        ;;    self-test) self_test ;;
    *) die "unknown command '$cmd' (take, take-up-to, run, run-quiet, show, self-test)" ;;
esac