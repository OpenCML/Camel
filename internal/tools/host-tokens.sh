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
#   take N        take N tokens (blocking; while waiting it prints who holds what)
#   run N -- CMD  take N tokens, run CMD with them, give them back when CMD exits
#   take-all      take every token (a time-sensitive measurement; the caller must say so)
#   show          what the pool looks like now: free, and who holds the rest
#   self-test     check the properties this exists for, without a host
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

# The size must be a number a pool can exist at: 1 is a serial machine, 0 is not a machine.
case "$POOL_SIZE" in
    ''|*[!0-9]*) die "CAMEL_TOKENS is not a number: '$POOL_SIZE'" ;;
esac
[ "$POOL_SIZE" -ge 1 ] || die "CAMEL_TOKENS is $POOL_SIZE; a pool of fewer than one token runs nothing"

token_path() { printf '%s/token-%02d.lock' "$TOKEN_DIR" "$1"; }
record_path() { printf '%s/token-%02d.holder' "$TOKEN_DIR" "$1"; }

prepare() {
    mkdir -p "$TOKEN_DIR" || die "cannot make $TOKEN_DIR"
    # A pool that was configured at 24 and is now asked for 32 gets the extra files; a smaller pool
    # keeps them on disk but never hands them out, so raising the size later does not start from a
    # directory that disagrees with itself.
    local j=1
    while [ "$j" -le "$POOL_SIZE" ]; do
        [ -e "$(token_path "$j")" ] || : > "$(token_path "$j")"
        j=$((j + 1))
    done
}

# Who holds a token file now. `fuser` is the reading; a file nobody has open is free.
holders_of() {
    fuser "$(token_path "$1")" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' || true
}

show() {
    prepare
    free=0
    held=0
    echo "pool: $TOKEN_DIR size=$POOL_SIZE lane=$LANE"
    i=1
    while [ "$i" -le "$POOL_SIZE" ]; do
        pids=$(holders_of "$i")
        if [ -z "$pids" ]; then
            free=$((free + 1))
        else
            held=$((held + 1))
            rec=''
            [ -e "$(record_path "$i")" ] && rec=$(cat "$(record_path "$i")" 2>/dev/null)
            for p in $pids; do
                etime=$(ps -o etime= -p "$p" 2>/dev/null | tr -d ' ')
                echo "  token $(printf '%02d' "$i") held by pid $p (${etime:-?}) ${rec}"
            done
        fi
        i=$((i + 1))
    done
    echo "free=$free held=$held"
}

# Take one token, blocking. The wait is not silent: a run that queues for thirty minutes without
# saying who it is waiting for is indistinguishable from a run that hung, and on 2026-11-24 that
# cost a whole measurement.
take_one() {
    local n="$1" waited=0 pid
    while :; do
        # -n first: the common case is a free pool and must not pay for a message.
        if flock -n "$n"; then
            printf '%s %s %s %s\n' "$$" "$LANE" "$WHO" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$(record_path "$1")"
            return 0
        fi
        if [ "$waited" -eq 0 ]; then
            echo "host-tokens: waiting for token $n; the pool holds $POOL_SIZE"
            show | sed 's/^/  /'
        fi
        sleep 2
        waited=$((waited + 2))
        if [ $((waited % 60)) -eq 0 ]; then
            echo "host-tokens: still waiting for token $n (${waited}s)"
        fi
    done
}

take() {
    local want="$1" taken=0 n fd got held=' ' waited=0
    [ "$want" -ge 1 ] || die "take needs at least one token"
    [ "$want" -le "$POOL_SIZE" ] || die "cannot take $want tokens from a pool of $POOL_SIZE"
    prepare
    while [ "$taken" -lt "$want" ]; do
        # One pass over the whole pool, non-blocking. Waiting on the *first* token while the second is
        # free would make a pool of twenty-four run exactly one thing at a time -- which is the defect
        # this replaces, arrived at from the other side.
        got=0
        n=1
        while [ "$n" -le "$POOL_SIZE" ] && [ "$got" -eq 0 ]; do
            case "$held" in
                *" $n "*) n=$((n + 1)); continue ;;
            esac
            # fds 20+ are this script's token hold, by token index. They are inherited by the worker,
            # so a token lives exactly as long as the process tree that took it. A fixed descriptor
            # number on purpose: bash's `{fd}` form can carry close-on-exec, and a token that vanishes
            # when the worker is exec'd is a token the worker never had.
            fd=$((20 + n))
            eval "exec $fd>\"$(token_path "$n")\""
            if flock -n "$fd"; then
                printf '%s %s %s %s\n' "$$" "$LANE" "$WHO" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$(record_path "$n")"
                taken=$((taken + 1))
                held="$held$n "
                got=1
            else
                eval "exec $fd>&-"
            fi
            n=$((n + 1))
        done
        if [ "$got" -eq 0 ]; then
            # The wait is not silent: a run that queues without saying who it waits for looks exactly
            # like a run that hung, and on 2026-11-24 that cost a whole measurement.
            echo "host-tokens: waiting for a token of $POOL_SIZE (lane=$LANE, holding $taken)"
            show | sed 's/^/  /'
            sleep 1
            waited=$((waited + 1))
            if [ $((waited % 60)) -eq 0 ]; then
                echo "host-tokens: still waiting for a token (${waited}s, lane=$LANE)"
            fi
        fi
    done
    # One line per case would drown the run it is measuring: a caller that hands a token to every
    # case in a plan passes CAMEL_TOKEN_QUIET=1 and hears only about waits.
    [ "${CAMEL_TOKEN_QUIET:-0}" = '1' ] || echo "host-tokens: took $want of $POOL_SIZE tokens (lane=$LANE)"
}

self_test() {
    local root bad=0 out
    root=$(mktemp -d)
    # The size is resolved once, at start, so the test sets the variable it was resolved into rather
    # than hoping an environment prefix on a function call changes it.
    export CAMEL_TOKEN_DIR="$root/pool"
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

    rm -rf "$root"
    if [ "$bad" -eq 0 ]; then echo 'selftest: 8 checks, 0 failed'; return 0; fi
    echo "selftest: $bad failed"; return 1
}

cmd="${1:-show}"
shift || true
case "$cmd" in
    show)     show ;;
    take)     take "${1:-1}" ;;
    take-all) take "$POOL_SIZE" ;;
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
    *) die "unknown command '$cmd' (take, take-all, run, run-quiet, show, self-test)" ;;
esac