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
# A run that already holds its width says so with CAMEL_TOKENS_COVERED=1 (`camel-run plan` sets it),
# and that means every process under the run is inside the run's tokens. `take`, `take-up-to` and
# `run` then take nothing and run the command: a descendant that waited for more tokens while its
# ancestors held theirs would be hold-and-wait, and several plan runs on a nearly full pool would wait
# on each other for good. `take-up-to` reports the covering run's own width (`CAMEL_JOBS`, at most
# what was asked for), so a nested sweep runs no wider than the run that holds the tokens.
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
# is gone from it. `fuser` cannot tell those two from a holder: it reads an open descriptor as a held
# lock (internal/tools/README.md). It is used only when /proc/locks cannot be read at all, and then
# the reading says so rather than passing itself off.
read_holder_mode() {
    if [ -r /proc/locks ]; then echo /proc/locks; else echo fuser; fi
}

# `holders_of INDEX TABLE`: TABLE is one copy of /proc/locks, taken by the caller in one builtin read
# for the whole listing. Reading the file costs as much as the kernel takes to regenerate the table,
# and a `read` loop straight on it regenerates it for every line, so on a host with a few hundred
# locks a walk per token turned a listing of the pool into a minute; `count_held` reads it once
# for the same reason.
holders_of() {
    local file ino locks="$2"
    file=$(token_path "$1")
    if [ -r /proc/locks ]; then
        ino=$(stat -c %i "$file" 2>/dev/null) || return 0
        [ -n "$ino" ] || return 0
        # `N: FLOCK ADVISORY WRITE <pid> <major>:<minor>:<inode> <start> <end>`: the pid is the field
        # before the one that ends in this inode. A blocked waiter is a `->` entry and is skipped.
        while read -r _ type _ _ lpid where _ _; do
            [ "$type" = 'FLOCK' ] || continue
            [ "${where##*:}" = "$ino" ] && echo "$lpid"
        done <<< "$locks"
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
    local table=''
    [ -r /proc/locks ] && table="$(< /proc/locks)" 2>/dev/null
    i=1
    while [ "$i" -le "$POOL_SIZE" ]; do
        pids=$(holders_of "$i" "$table")
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
        # like a run that hung, and a silent queue has cost a whole measurement.
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
    [ "${CAMEL_TOKENS_COVERED:-0}" = 1 ] && return 0
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
# locks are. `HOST_BUILD_JOBS=<n>` (or the label in `CAMEL_TAKE_LABEL`, which a plan run sets to
# `HOST_JOBS`) is printed first, so a caller that reads the run's stream learns
# the width the build actually got rather than the width it asked for.
take_up_to() {
    local want="$1"
    shift
    [ "$want" -ge 1 ] || die "take-up-to needs at least one token"
    [ "$want" -le "$POOL_SIZE" ] || want="$POOL_SIZE"
    if [ "${CAMEL_TOKENS_COVERED:-0}" = 1 ]; then
        # Inside a covering run: nothing is taken, the width is the run's own, capped by the request.
        ACQ_N="${CAMEL_JOBS:-${CAMEL_TOKENS_HELD:-$want}}"
        case "$ACQ_N" in ''|*[!0-9]*) ACQ_N="$want" ;; esac
        [ "$ACQ_N" -le "$want" ] || ACQ_N="$want"
        [ "$ACQ_N" -ge 1 ] || ACQ_N=1
        ACQ_MS=0
    else
        prepare
        acquire "$want" upto
    fi
    echo "host-tokens: running at $ACQ_N of $want worker(s) (lane=$LANE, $((ACQ_MS / 1000))s to take them)"
    echo "${CAMEL_TAKE_LABEL:-HOST_BUILD_JOBS}=$ACQ_N"
    # The label names this one report line, so the command does not inherit it: a taker started
    # anywhere below (a build inside a plan run, a self-test run as a case) reports under its own.
    unset CAMEL_TAKE_LABEL
    export CAMEL_TOKENS_HELD="$ACQ_N"
    export CMAKE_BUILD_PARALLEL_LEVEL="$ACQ_N"
    "$@"
}

# --- evidence for the self-test -------------------------------------------------------------------
#
# The self-test judges order and concurrency from what the workers write, never from a clock: a busy,
# shared host stretches every wait, so "it took under N ms" says how loaded the machine was. A worker
# appends `start NAME WANT HELD` once it holds its tokens (HELD is the pool's own count of held
# tokens at that moment) and `end NAME WANT` before it lets them go, both under one lock, so the
# order of the lines is the order things happened in. Time appears only as a liveness bound, a limit
# long enough that reaching it means a hang.

# st_log FILE LINE: append one line, serialised.
st_log() { ( flock 9; printf '%s\n' "$2" >> "$1" ) 9> "$1.lock"; }

# st_starts FILE: how many workers have logged a start.
st_starts() { grep -c '^start' "$1" 2> /dev/null || true; }

# st_summary FILE: key=value readings of the log, for eval.
#   starts/ends/fails  workers that started, finished, could not take
#   maxopen            most workers holding at once
#   maxtokens          most tokens the open workers asked for at once
#   maxheld            largest pool-held count any worker saw when it started
#   heldwrong          workers whose own held count was not what they asked for
st_summary() {
    awk '
        $1 == "start" { s++; o++; t += $3; if (o > mo) mo = o; if (t > mt) mt = t; if ($4 > mh) mh = $4; if ($4 != $3) hw++ }
        $1 == "end"   { e++; o--; t -= $3 }
        $1 == "fail"  { f++ }
        END { printf "starts=%d ends=%d fails=%d maxopen=%d maxtokens=%d maxheld=%d heldwrong=%d\n", s+0, e+0, f+0, mo+0, mt+0, mh+0, hw+0 }
    ' "$1"
}

# st_worker FILE NAME WANT TAKER GATE: take WANT tokens with TAKER (`new` is `take`, `old` is
# `take_old`), log the start, hold until GATE workers have started (or the liveness bound), log the
# end. The tokens go back when the subshell running this exits.
st_worker() {
    local log="$1" name="$2" want="$3" taker="$4" gate="$5" i=0
    WAIT_S="${ST_WAIT:-120}"
    if [ "$taker" = old ]; then
        # A taker that waited out keeps what it holds until its twin has waited out too, so the twin
        # cannot finish on the tokens the first one gave up.
        take_old "$want" "$log.barrier" || { st_log "$log" "fail $name"; sleep 3; return 3; }
    else
        CAMEL_TOKEN_QUIET=1 take "$want" > /dev/null 2>&1 || { st_log "$log" "fail $name"; return 3; }
    fi
    count_held
    st_log "$log" "start $name $want $HELD_N"
    while [ "$(st_starts "$log")" -lt "$gate" ] && [ "$i" -lt 300 ]; do sleep 0.1; i=$((i + 1)); done
    [ "${ST_HOLD:-0}" = 0 ] || sleep "$ST_HOLD"
    st_log "$log" "end $name $want"
}

# The old taker, kept only as the negative control: one token per pass, holding what it has while it
# waits for the rest. `take_old WANT BARRIER` takes one token, then waits until two takers (this one
# and its twin) each hold one -- the situation two large takers reach by chance and this makes
# certain -- and then keeps going one token a pass, never giving anything back. Nothing is left for
# either to finish with, so it waits out (returns 6) instead of running.
take_old() {
    local want="$1" barrier="$2" n fd got=0 held=' ' t0 i=0
    prepare
    now_ms; t0=$NOW_MS
    while [ "$got" -lt "$want" ]; do
        n=1
        while [ "$n" -le "$POOL_SIZE" ]; do
            case "$held" in *" $n "*) n=$((n + 1)); continue ;; esac
            fd=$((20 + n))
            eval "exec $fd>\"$(token_path "$n")\""
            if flock -n "$fd"; then held="$held$n "; got=$((got + 1)); break; fi
            eval "exec $fd>&-"
            n=$((n + 1))
        done
        if [ "$got" -eq 1 ] && [ ! -e "$barrier.$$" ]; then
            : > "$barrier.$$"
            while [ "$(ls "$barrier".* 2> /dev/null | wc -l)" -lt 2 ] && [ "$i" -lt 100 ]; do sleep 0.1; i=$((i + 1)); done
        fi
        [ "$got" -ge "$want" ] && break
        now_ms
        [ $((NOW_MS - t0)) -lt $((WAIT_S * 1000)) ] || return 6
        sleep 0.1
    done
    return 0
}

# settle PATTERN: poll `show` into OUT until it contains PATTERN. The limit says the pool never got
# there; the caller's check then reports what it did see.
settle() {
    local i=0
    while :; do
        out=$(show)
        case "$out" in *"$1"*) return 0 ;; esac
        i=$((i + 1))
        [ "$i" -lt 300 ] || return 1
        sleep 0.1
    done
}

# two_takers_log LOG WANT TAKER: two takers of WANT tokens at once, joined; the evidence is in LOG.
two_takers_log() {
    local log="$1" want="$2" taker="$3" pids='' p k
    : > "$log"; rm -f "$log".barrier.*
    for k in 1 2; do
        ( ST_HOLD=1 st_worker "$log" "t$k" "$want" "$taker" 0 ) &
        pids="$pids $!"
    done
    for p in $pids; do wait "$p" || true; done
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
    # The test measures its own pool, so a cover it inherited does not apply to it: run inside a plan
    # run (as a case is), CAMEL_TOKENS_COVERED=1 would make every take below take nothing and every
    # check below read an empty pool.
    unset CAMEL_TOKENS_COVERED CAMEL_TOKENS_HELD CAMEL_TAKE_LABEL

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
    settle 'free=1 held=2'
    case "$out" in
        *'free=1 held=2'*) echo 'ok    two held leaves one free' ;;
        *) echo "FAIL  two held leaves one free: $out"; bad=$((bad + 1)) ;;
    esac

    # 4. the tokens come back when the holder dies -- the property the whole design is for
    kill -9 "$holder" 2>/dev/null
    wait "$holder" 2>/dev/null
    settle 'free=3 held=0'
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
    # the other side. Each worker holds until a second one has started, so a taker that serialises
    # the pool never sees a neighbour start and the log shows one open worker at a time; the liveness
    # bound only keeps that case from hanging.
    POOL_SIZE=2; prepare
    log=$(mktemp); : > "$log"
    pids=''
    for i in 1 2 3 4; do ( st_worker "$log" "w$i" 1 new 2 ) & pids="$pids $!"; done
    for p in $pids; do wait "$p" || true; done
    eval "$(st_summary "$log")"
    if [ "$starts" -eq 4 ] && [ "$ends" -eq 4 ] && [ "$fails" -eq 0 ] && [ "$maxtokens" -eq 2 ] && [ "$maxheld" -le 2 ]; then
        echo 'ok    four takers on a pool of two run two at a time'
    else
        echo "FAIL  four takers on a pool of two: $(st_summary "$log") (needs starts=4 ends=4 fails=0 maxtokens=2 maxheld<=2)"
        bad=$((bad + 1))
    fi
    rm -f "$log" "$log.lock"
    POOL_SIZE=3; prepare
    # 6. a size that is not a number is refused rather than read as a pool of zero
    if CAMEL_TOKENS=many "$0" show >/dev/null 2>&1; then
        echo 'FAIL  a pool size of "many" was accepted'; bad=$((bad + 1))
    else
        echo 'ok    a pool size that is not a number is refused'
    fi
    # 9. the peak is a reading, not a zero. Three tokens held at once must leave the mark at three
    #    even after they are all back: that is the number `tokensPeak` reports, and a pool that never
    #    records it is a pool nobody can tell from an unused one. It starts from an idle pool: a
    #    worker of check 8 still holding a token would let the holder below take two at a pass and
    #    give them back, so the mark would read two however the peak is recorded.
    settle 'free=3 held=0' || { echo "FAIL  the pool did not go idle after the four takers: $out"; bad=$((bad + 1)); }
    ( take 3 >/dev/null; exec sleep 20 ) &
    holder2=$!
    settle 'peak=3'
    case "$out" in
        *'peak=3'*) echo 'ok    the peak records three held at once' ;;
        *) echo "FAIL  the peak did not record three: $out"; bad=$((bad + 1)) ;;
    esac
    kill -9 "$holder2" 2>/dev/null
    wait "$holder2" 2>/dev/null
    settle 'free=3 held=0 peak=3'
    case "$out" in
        *'free=3 held=0 peak=3'*) echo 'ok    the peak stays at three after every token is back' ;;
        *) echo "FAIL  the peak did not stay: $out"; bad=$((bad + 1)) ;;
    esac

    # 10. a wait that runs out is its own answer: 6, not 2 (a refusal) and not 1 (a red reading)
    ( take 3 >/dev/null; exec sleep 20 ) &
    holder3=$!
    settle 'free=0 held=3'
    ( WAIT_S=2; take 1 >/dev/null 2>&1 )
    rc=$?
    if [ "$rc" -eq 6 ]; then
        echo 'ok    a wait that runs out exits 6'
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
    settle 'free=1 held=2'
    width=$("$0" take-up-to 3 -- true 2>/dev/null | sed -n 's/^HOST_BUILD_JOBS=//p' | tail -1)
    seen=$("$0" take-up-to 3 -- sh -c 'printf "width-in-cmd=%s\n" "$CAMEL_TOKENS_HELD"' 2>/dev/null | sed -n 's/^width-in-cmd=//p' | tail -1)
    case "$width:$seen" in
        1:1) echo 'ok    take-up-to takes the one token that is free rather than waiting for three' ;;
        *) echo "FAIL  take-up-to reported width '$width' and the command saw '$seen', not 1 and 1"; bad=$((bad + 1)) ;;
    esac
    labelled=$(CAMEL_TAKE_LABEL=HOST_JOBS "$0" take-up-to 3 -- true 2>/dev/null | sed -n 's/^HOST_JOBS=//p' | tail -1)
    case "$labelled" in
        1) echo 'ok    take-up-to reports the width under the caller''s own label (a plan run reads it as HOST_JOBS)' ;;
        *) echo "FAIL  take-up-to under CAMEL_TAKE_LABEL reported '$labelled', not 1"; bad=$((bad + 1)) ;;
    esac
    kill -9 "$holder4" 2>/dev/null
    wait "$holder4" 2>/dev/null

    # 12b. inside a covering run nothing is taken: with every token held, a nested take returns at
    # once and a nested take-up-to runs at the covering run's width instead of waiting for the pool.
    ( take 3 >/dev/null; exec sleep 30 ) &
    holder5=$!
    settle 'free=0 held=3'
    CAMEL_TOKENS_COVERED=1 timeout 20 "$0" run 3 -- true >/dev/null 2>&1
    rc=$?
    nested=$(CAMEL_TOKENS_COVERED=1 CAMEL_JOBS=2 timeout 20 "$0" take-up-to 3 -- sh -c 'printf "w=%s\n" "$CAMEL_TOKENS_HELD"' 2>/dev/null | sed -n 's/^w=//p' | tail -1)
    if [ "$rc" -eq 0 ] && [ "$nested" = 2 ]; then
        echo 'ok    under a covering run a nested take finishes at once on a full pool, and take-up-to reports the run'"'"'s width'
    else
        echo "FAIL  nested takers under CAMEL_TOKENS_COVERED: run exit $rc, take-up-to width '$nested' (needs 0 and 2)"; bad=$((bad + 1))
    fi
    kill -9 "$holder5" 2>/dev/null
    wait "$holder5" 2>/dev/null

    # 13. two takers that each want more than half of the pool both finish, one after the other.
    # Each takes two of three and holds them; taking one token at a time and waiting for the second
    # would leave each holding one and waiting on the other. The verdict is the log: both started and
    # finished, never two open at once (two and two do not fit in three), each saw exactly its own two
    # held, and the pool's held count never passed the pool.
    log=$(mktemp)
    ST_WAIT=120 two_takers_log "$log" 2 new
    eval "$(st_summary "$log")"
    peak_of
    if [ "$starts" -eq 2 ] && [ "$ends" -eq 2 ] && [ "$fails" -eq 0 ] && [ "$maxopen" -eq 1 ]         && [ "$heldwrong" -eq 0 ] && [ "$maxheld" -le "$POOL_SIZE" ] && [ "$PEAK_N" -le "$POOL_SIZE" ]; then
        echo 'ok    two takers that each want two of three both finish, one after the other'
    else
        echo "FAIL  two takers of two on a pool of three: $(st_summary "$log"), peak $PEAK_N"
        bad=$((bad + 1))
    fi
    rm -f "$log" "$log.lock"

    # 14. the negative control: the same two-large-takers check, run against the old taker (one token
    # per pass, holding while it waits, each holding one before either asks for a second). It must
    # fail, and by waiting out -- a taker that never got its tokens -- not by a comparison of times.
    # Both want all three, which is what makes the two certain to meet each other's holds.
    log=$(mktemp)
    ST_WAIT=4 two_takers_log "$log" 3 old
    eval "$(st_summary "$log")"
    if [ "$fails" -ge 1 ] && ! { [ "$starts" -eq 2 ] && [ "$ends" -eq 2 ] && [ "$maxopen" -eq 1 ]; }; then
        echo "ok    the old taker fails the two-large-takers check by waiting out ($(st_summary "$log"))"
    else
        echo "FAIL  the old taker passed the two-large-takers check: $(st_summary "$log")"
        bad=$((bad + 1))
    fi
    rm -f "$log" "$log.lock" "$log".barrier.*

    rm -rf "$root"
    if [ "$bad" -eq 0 ]; then echo 'selftest: 16 checks, 0 failed'; return 0; fi
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