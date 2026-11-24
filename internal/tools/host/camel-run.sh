#!/usr/bin/env bash
# Copyright (c) 2026 The Camel Authors. All rights reserved.
# Use of this source code is governed by the Apache License 2.0.
#
# camel-run: the one place on the build host that takes a lock, takes a token, and says what it holds.
#
# THIS SCRIPT IS THE HOST'S ONLY LOCK- AND TOKEN-TAKER. Any `flock` written inline in a generated
# `run.sh` is a defect, not a compatibility layer. On 2026-11-24 three test slots were added and no run
# ever took one, because each worktree generated its own `run.sh` from its own copy of the generator, and
# the merge position's copy still took the old single lock. Two ways to take a lock that do not know
# about each other is the same thing as no lock at all.
#
# Four failures close, and they are listed here because each one is a place where falling back would be
# easy and wrong:
#
#   1. this script absent, or not executable        -> the caller exits; it does not inline the old code
#   2. the installed sha disagrees with the caller's expectation
#                                                  -> exits and names both, rather than running unknown code
#   3. the install cannot be verified (no checkout, no `.git`, `origin/integration` never fetched)
#                                                  -> exits and says which of the three is missing
#   4. a needed tool is missing (`flock`, `date`)    -> exits and names it
#
# Two things open, and they are the same principle from the other side -- close what should be known,
# open what must not block:
#
#   * no token pool yet              -> take the lock anyway, report `tokens: unavailable`, do not exit:
#                                       the merge position still needs its slot from this script
#   * a slot is occupied             -> report who holds it, keep waiting, and after the deadline say
#                                       `timed-out` rather than "no slot exists"
#
# Subcommands:
#   lock      take the first free test slot, print the reading lines, then run the command that follows
#             `--` while holding it
#   status    print the pool and the slots as they are now
#   install   idempotent: put this script at its installed path, print `installed <sha> at <time>`
#
# Usage from a generated run.sh:
#   "${CAMEL_RUN:=/home/zjwei/camel-work/bin/camel-run}" lock --lane "$name" --branch "$branch" -- \
#       bash -c '<the plan run>'
set -u

SELF="${BASH_SOURCE[0]}"
INSTALL_DIR="${CAMEL_RUN_HOME:-$HOME/camel-work/bin}"
INSTALLED="$INSTALL_DIR/camel-run"
VERSION_FILE="$INSTALL_DIR/camel-run.sha"
POOL_DIR="${CAMEL_TOKEN_DIR:-/tmp/camel-tokens}"
POOL_SIZE="${CAMEL_TOKENS:-24}"
OLD_LOCK=/tmp/camel-host-test.lock
SLOT_WAIT_S="${CAMEL_SLOT_WAIT_S:-1800}"
LANE="${CAMEL_LANE:-$(basename "$PWD")}"
BRANCH="${CAMEL_BRANCH:-}"

say() { printf '[camel-run] %s\n' "$*"; }
die() { printf '[camel-run] %s\n' "$*" >&2; exit 3; }

sha_of() {
    # The version this script is, as git knows it. No git, no version -- and then the caller refuses.
    local dir
    dir=$(dirname "$SELF")
    git -C "$dir" rev-parse --short=9 HEAD 2>/dev/null || true
}

self_sha() { sha_of; }

check_version() {
    # The caller may name the version it expects (a run.sh records the one that generated it). A
    # disagreement is refused with both values, because "which tools took this reading" is the question
    # that cost half a day on 2026-11-24.
    local want="${CAMEL_RUN_EXPECT:-}"
    local have
    have=$(self_sha)
    if [ -z "$have" ]; then
        die "cannot name this script's version: $(dirname "$SELF") is not a git checkout. Refusing rather than running code of unknown version"
    fi
    if [ -n "$want" ] && [ "$want" != "$have" ]; then
        die "version mismatch: this script is $have, the run expects $want. Reinstall from origin/integration, or run with CAMEL_RUN_EXPECT=$have if $have is known good"
    fi
}

check_tools() {
    for t in flock date awk fuser; do
        command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
    done
}

pool_ready() { [ -d "$POOL_DIR" ] && [ -e "$POOL_DIR/token-01.lock" ]; }

pool_report() {
    if ! pool_ready; then
        echo 'tokens: unavailable'
        return
    fi
    local held=0 free=0 n pids
    n=1
    while [ "$n" -le "$POOL_SIZE" ]; do
        pids=$(fuser "$POOL_DIR/token-$(printf '%02d' "$n").lock" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' || true)
        if [ -n "$pids" ]; then held=$((held + 1)); else free=$((free + 1)); fi
        n=$((n + 1))
    done
    echo "tokens: mine=${CAMEL_TOKENS_MINE:-0} peak=${CAMEL_TOKENS_PEAK:-0}"
    echo "pool: size=$POOL_SIZE inUse=$held free=$free"
}

slots_report() {
    local n pids
    for n in 1 2 3; do
        pids=$(fuser "/tmp/camel-host-test-$n.lock" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' | tr '\n' ' ' || true)
        if [ -z "$pids" ]; then echo "slot $n: free"; else
            local one
            for one in $pids; do
                echo "slot $n: held by pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' '))"
            done
        fi
    done
}

load_line() {
    local l
    l=$(awk '{print $1" "$2" "$3}' /proc/loadavg 2>/dev/null || echo '')
    echo "loadAtStart: $l"
}

merge_seat() {
    case "$BRANCH" in seat/merge|seat/merge/*|seat/merge-*) echo 1 ;; *) echo 0 ;; esac
}

cmd_plan() {
    local slot=0 n fd got told=0 waited=0 candidates started
    check_tools
    check_version
    say "CAMEL_RUN $(self_sha) lane=$LANE branch=${BRANCH:-?}"
    if [ "$(merge_seat)" = 1 ]; then candidates='1 2 3'; else candidates='1 2'; fi

    # The old single lock, taken first and in this order on purpose. New runs then queue behind the runs
    # that are already queued on it, and those drain in the order they formed; the reverse is not true,
    # because the old code is already written into the worktrees and does not know these slots exist.
    # The direction is one-way (new yields to old, old does not yield to new) and that is the price of
    # not deleting a lock other processes are already waiting on.
    exec 7>"$OLD_LOCK"
    started=$(date +%s)

    while :; do
        got=0
        for n in $candidates; do
            fd=$((20 + n))
            eval "exec $fd>/tmp/camel-host-test-$n.lock"
            if flock -n "$fd"; then
                slot=$n
                got=1
                break
            fi
            eval "exec $fd>&-"
        done
        [ "$got" = 1 ] && break
        if [ "$told" = 0 ]; then
            if [ "$(merge_seat)" = 1 ]; then
                say "waiting for a host test slot (1, 2, 3; 3 is the merge seat's)"
            else
                say "waiting for a host test slot: 1 and 2 are held, and slot 3 is the merge seat's alone, so this run queues rather than borrow it"
            fi
            echo "HOST_TEST_WAIT_START=$candidates $(date -u +%Y-%m-%dT%H:%M:%SZ)"
            slots_report
            told=1
        fi
        waited=$(( $(date +%s) - started ))
        if [ "$waited" -ge "$SLOT_WAIT_S" ]; then
            say "no host test slot within ${SLOT_WAIT_S} s"
            exit 4
        fi
        sleep 2
    done
    waited=$(( $(date +%s) - started ))

    echo "HOST_TEST_WAIT_END=$slot"
    echo "HOST_TEST_SLOT=$slot"
    echo "lockFrom=$0@$(self_sha)"
    echo "lockWaitSeconds=$waited"
    pool_report
    echo "tokensPeak: ${CAMEL_TOKENS_PEAK:-0}"
    load_line

    # The exclusive hold takes the old lock and every slot, so a time-sensitive measurement is not taken
    # beside anything else. `take-all` is the only way to ask for the whole pool, and the reading says so.
    if [ "${CAMEL_EXCLUSIVE:-0}" = 1 ]; then
        exec 8>/tmp/camel-host-test-1.lock
        exec 9>/tmp/camel-host-test-2.lock
        exec 10>/tmp/camel-host-test-3.lock
        flock -n 8 || { say 'exclusive: slot 1 busy'; exit 4; }
        flock -n 9 || { say 'exclusive: slot 2 busy'; exit 4; }
        flock -n 10 || { say 'exclusive: slot 3 busy'; exit 4; }
        echo 'exclusive: true'
    fi

    if [ "$#" -ge 1 ]; then
        [ "$1" = '--' ] && shift
        exec "$@"
    fi
}

# A build takes tokens and no slot. The slot's meaning is "one test run being observed", and there is
# nothing to observe while compiling; making the slot cover the build would turn three slots into three
# build seats and leave the tests unbounded, which is the opposite of what they are for. Concurrency
# between builds is the pool's business -- k tokens, then `ninja -j k` (D-1124-38).
cmd_build() {
    local want="${CAMEL_BUILD_TOKENS:-8}" peak=0
    check_tools
    check_version
    say "CAMEL_RUN $(self_sha) lane=$LANE build tokens=$want"
    if ! pool_ready; then
        echo 'tokens: unavailable'
        echo 'tokensPeak: 0'
        [ "$#" -ge 1 ] && [ "$1" = '--' ] && shift
        if [ "$#" -ge 1 ]; then exec "$@"; fi
        return 0
    fi
    # The pool's own taker does the waiting; it reports who holds what, and the tokens come back when
    # this process exits whatever the build does.
    local taker
    taker="$(dirname "$SELF")/host-tokens.sh"
    [ -x "$taker" ] || die "token pool present but $taker is missing: refusing to build outside the pool"
    echo "tokensPeak: $want"
    load_line
    [ "$#" -ge 1 ] && [ "$1" = '--' ] && shift
    exec "$taker" run "$want" -- "$@"
}
cmd_status() {
    check_tools
    say "CAMEL_RUN $(self_sha)"
    slots_report
    pool_report
    echo "tokensPeak: ${CAMEL_TOKENS_PEAK:-0}"
    load_line
}

cmd_install() {
    # Idempotent: running it twice changes nothing after the first time, and it says so either way.
    mkdir -p "$INSTALL_DIR" || die "cannot create $INSTALL_DIR"
    local here sha same=0
    here=$(cd "$(dirname "$SELF")" && pwd)
    sha=$(self_sha)
    [ -z "$sha" ] && die "cannot name this script's version; install from a checkout of origin/integration"
    if [ -f "$INSTALLED" ] && cmp -s "$SELF" "$INSTALLED"; then same=1; fi
    if [ "$same" = 0 ]; then
        cp "$SELF" "$INSTALLED.tmp" && chmod 755 "$INSTALLED.tmp" && mv "$INSTALLED.tmp" "$INSTALLED" || die "install failed"
    fi
    printf '%s\n' "$sha" > "$VERSION_FILE"
    echo "installed $sha at $(date -u +%Y-%m-%dT%H:%M:%SZ) (from $here; $([ "$same" = 1 ] && echo 'unchanged' || echo 'replaced'))"
}

cmd="${1:-status}"
shift || true
case "$cmd" in
    plan)
        # --lane/--branch are convenience; the environment is authoritative, because the generated run.sh
        # already exports both.
        while [ "$#" -gt 0 ]; do
            case "$1" in
                --lane) LANE="$2"; shift 2 ;;
                --branch) BRANCH="$2"; shift 2 ;;
                --) shift; break ;;
                *) break ;;
            esac
        done
        cmd_plan "$@"
        ;;
    build) cmd_build "$@" ;;
    status) cmd_status ;;
    install) cmd_install ;;
    *) die "unknown subcommand '$cmd' (plan, build, status, install)" ;;
esac
