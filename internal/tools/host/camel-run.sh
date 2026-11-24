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
#   5. the installed bytes are not the bytes the install record was written for
#                                                  -> exits and prints both digests. A version number
#                                                     that outlived its own file is the reading nobody
#                                                     can check, and the live directory is exactly the
#                                                     place it happens: on 2026-11-24 `camel-run` there
#                                                     was an older copy than the checkout it was
#                                                     installed from, and the record still named that
#                                                     checkout (see `version_check`)
#
# Two things open, and they are the same principle from the other side -- close what should be known,
# open what must not block:
#
#   * no token pool yet              -> take the lock anyway, report `tokens: unavailable`, do not exit:
#                                       the merge position still needs its slot from this script
#   * a slot is occupied             -> report who holds it, keep waiting, and after the deadline say
#                                       `timed-out` rather than "no slot exists"
#
# Exit codes, because a run that measured nothing must not look like one that measured a red:
#   0  the command ran; its own code is what the caller sees
#   3  this script could not name its version, or the install is behind the bytes, or a tool is missing
#   4  no test slot within the wait -- the run measured nothing (see SLOT_WAIT_S)
#   5  no shared hold on the old lock within the same wait -- an older run is taking the host alone,
#      and again the run measured nothing. Not 2 (a refused choice) and not 1 (a red reading).
#
# Subcommands:
#   lock      take the first free test slot, print the reading lines, then run the command that follows
#             `--` while holding it
#   status    print the pool and the slots as they are now
#   install   idempotent: put this script at its installed path and record
#             `<version> <sha256 of these bytes>` beside it, print `installed <version> (<digest>) at
#             <time>`. It refuses a checkout with uncommitted changes to this file: the version would
#             name bytes the commit does not contain.
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

digest_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

self_sha() { sha_of; }

# What version these bytes are, said only when it can be said truthfully; `VERSION` is the one name
# every message uses afterwards.
#
# The installed copy lives in a directory that is not a checkout, so git cannot name it there. The
# install record is the provenance then -- but a record is a name for the bytes that were installed,
# and it says nothing about a file that has changed since. A fallback that reads the record without
# checking it prints a version number about some other file, which is worse than refusing: the
# reader cannot tell it apart from a true one. So the record is believed only when this file's own
# digest is the digest the record was written for. A disagreement exits with both digests, because
# "install did not keep up" is the finding, and "unknown version" would hide it.
version_check() {
    local dir have recorded want mine
    VERSION=''
    dir=$(dirname "$SELF")
    have=$(sha_of)
    if [ -n "$have" ]; then
        VERSION="$have"
        return 0
    fi
    if [ ! -f "$VERSION_FILE" ]; then
        die "cannot name this script's version: $dir is not a git checkout and there is no install record at $VERSION_FILE. Run it from a checkout, or install it from one (camel-run.sh install)"
    fi
    recorded=$(awk 'NR == 1 { print $1 }' "$VERSION_FILE" 2>/dev/null)
    want=$(awk 'NR == 1 { print $2 }' "$VERSION_FILE" 2>/dev/null)
    mine=$(digest_of "$SELF")
    if [ -z "$recorded" ] || [ -z "$want" ] || [ -z "$mine" ]; then
        die "the install record $VERSION_FILE cannot name these bytes: it holds '${recorded:-}' '${want:-}' and $SELF has no digest of its own. Reinstall with a version that writes both (camel-run.sh install)"
    fi
    if [ "$mine" != "$want" ]; then
        die "install did not keep up: $VERSION_FILE says $recorded, written for bytes $want, but $SELF is $mine. Reinstall from that checkout (camel-run.sh install); these bytes have no version until then, and a run will not take a slot on a version nobody can name"
    fi
    VERSION="$recorded"
}

check_version() {
    # The caller may name the version it expects (a run.sh records the one that generated it). A
    # disagreement is refused with both values, because "which tools took this reading" is the question
    # that cost half a day on 2026-11-24.
    local want="${CAMEL_RUN_EXPECT:-}"
    local have
    version_check
    have="$VERSION"
    if [ -n "$want" ] && [ "$want" != "$have" ]; then
        die "version mismatch: this script is $have, the run expects $want. Reinstall from origin/integration, or run with CAMEL_RUN_EXPECT=$have if $have is known good"
    fi
}

check_tools() {
    for t in flock date awk fuser sha256sum; do
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
    local n pids one args shown
    for n in 1 2 3; do
        pids=$(fuser "/tmp/camel-host-test-$n.lock" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' | tr '\n' ' ' || true)
        if [ -z "$pids" ]; then echo "slot $n: free"; else
            # A pid that is already gone is not a holder: this script's own `$( )` substitutions inherit
            # every descriptor, so a raw `fuser` listing names processes that existed for a millisecond.
            # A slot reported as held by four dead pids reads like a queue that is not there.
            shown=0
            for one in $pids; do
                args=$(ps -o args= -p "$one" 2>/dev/null)
                [ -n "$args" ] || continue
                echo "slot $n: held by pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' ')) $(printf '%s' "$args" | cut -c1-80)"
                shown=1
            done
            [ "$shown" = 0 ] && echo "slot $n: free (an open descriptor, no process behind it)"
        fi
    done
}

load_line() {
    local l
    l=$(awk '{print $1" "$2" "$3}' /proc/loadavg 2>/dev/null || echo '')
    echo "loadAtStart: $l"
}

old_lock_holders() {
    # Who has the old lock open, this process excluded: our own descriptor is open whether or not the
    # lock was taken, and naming ourselves as the blocker is the one answer that is certainly wrong.
    local pids one args
    pids=$(fuser "$OLD_LOCK" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' | grep -vx "$$" || true)
    if [ -z "$pids" ]; then
        echo "old lock $OLD_LOCK: no other process has it open"
    else
        # Only pids that are still there: this script's own `$( )` substitutions inherit the descriptor
        # and are gone a moment later, so a listing taken without the check names processes that no
        # longer exist as if they were blocking us.
        for one in $pids; do
            args=$(ps -o args= -p "$one" 2>/dev/null)
            [ -n "$args" ] || continue
            echo "old lock $OLD_LOCK: pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' ')) $(printf '%s' "$args" | cut -c1-100)"
        done
    fi
}

merge_seat() {
    case "$BRANCH" in seat/merge|seat/merge/*|seat/merge-*) echo 1 ;; *) echo 0 ;; esac
}

cmd_plan() {
    local slot=0 n fd got told=0 waited=0 candidates started
    check_tools
    check_version
    say "CAMEL_RUN $VERSION lane=$LANE branch=${BRANCH:-?}"
    if [ "$(merge_seat)" = 1 ]; then candidates='1 2 3'; else candidates='1 2'; fi

    # The old single lock, taken first and in this order on purpose. New runs then queue behind the runs
    # that are already queued on it, and those drain in the order they formed; the reverse is not true,
    # because the old code is already written into the worktrees and does not know these slots exist.
    # The direction is one-way (new yields to old, old does not yield to new) and that is the price of
    # not deleting a lock other processes are already waiting on.
    #
    # It is a SHARED hold, and that is what lets one file serve both designs at once: an old-style run
    # takes it exclusively, so it excludes every new run for as long as it holds it, and two new runs
    # take it shared, so they do not exclude each other -- the three slots are what bounds them. An
    # exclusive hold here would turn three slots back into one, which is why it is `-s` and not `-x`.
    # The wait is the slot's wait, and running out of it exits 5: nothing was measured, which is
    # neither a refused choice (2) nor a red reading (1).
    exec 7>"$OLD_LOCK"
    started=$(date +%s)
    if ! flock -s -w "$SLOT_WAIT_S" 7; then
        waited=$(( $(date +%s) - started ))
        say "no shared hold on $OLD_LOCK within ${SLOT_WAIT_S} s (waited ${waited} s): a run of the old shape is taking the host alone, so this run measured nothing and exits 5"
        old_lock_holders
        echo "oldLockWaitSeconds=$waited"
        exit 5
    fi
    old_lock_waited=$(( $(date +%s) - started ))
    echo "oldLockWaitSeconds=$old_lock_waited"
    # The slot's clock starts here, not at the shared hold: `lockWaitSeconds` is the slot wait, and a
    # number that quietly also carried the old-lock wait would answer two questions at once (they are
    # different ones: "did an old run block me" and "were the three slots all taken").
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
    echo "lockFrom=$0@$VERSION"
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
    say "CAMEL_RUN $VERSION lane=$LANE build tokens=$want"
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
    # The same resolution a plan run does, so an install that fell behind is seen here -- by the
    # cheapest command there is -- rather than on the run it would have refused.
    version_check
    say "CAMEL_RUN $VERSION"
    slots_report
    pool_report
    echo "tokensPeak: ${CAMEL_TOKENS_PEAK:-0}"
    load_line
}

cmd_install() {
    # Idempotent: running it twice changes nothing after the first time, and it says so either way.
    mkdir -p "$INSTALL_DIR" || die "cannot create $INSTALL_DIR"
    local here sha digest same=0
    here=$(cd "$(dirname "$SELF")" && pwd)
    sha=$(self_sha)
    [ -z "$sha" ] && die "cannot name this script's version; install from a checkout of origin/integration"
    # The record names the bytes, so it may only be written for bytes its checkout actually holds. A
    # copy edited on the way in would install under a commit it is not -- which is the one thing the
    # record exists to prevent, and the reason `version_check` compares digests at all.
    if [ -n "$(git -C "$here" status --porcelain --untracked-files=no -- "$SELF" 2>/dev/null)" ]; then
        die "the checkout at $here has uncommitted changes to this file, so $sha would name bytes that commit does not contain. Commit them, then install"
    fi
    digest=$(digest_of "$SELF")
    [ -z "$digest" ] && die "cannot compute a digest of $SELF, so the record could not be checked. Refusing to install"
    if [ -f "$INSTALLED" ] && cmp -s "$SELF" "$INSTALLED"; then same=1; fi
    if [ "$same" = 0 ]; then
        cp "$SELF" "$INSTALLED.tmp" && chmod 755 "$INSTALLED.tmp" && mv "$INSTALLED.tmp" "$INSTALLED" || die "install failed"
    fi
    # `<version> <sha256 of those bytes>`: the version, and the bytes it is a version of.
    printf '%s %s\n' "$sha" "$digest" > "$VERSION_FILE"
    echo "installed $sha ($digest) at $(date -u +%Y-%m-%dT%H:%M:%SZ) (from $here; $([ "$same" = 1 ] && echo 'unchanged' || echo 'replaced'))"
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
