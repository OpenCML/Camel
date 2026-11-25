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
# A caller that already holds the host's test slots names them in `CAMEL_TEST_SLOT_HELD` and this
# script does not ask for one again. That is not a convenience: a time-sensitive plan (a `perf/**`
# reading) takes build-1, build-2 and all three test slots in the generated `run.sh` before its
# build, and the plan step is this script in a *child* process. Locks live on descriptors, so the
# child does not hold what the parent holds -- and it asked for the same files, so it queued behind
# the process that started it. The lane's probe measured it on 2026-11-25 (`perf/fib`, commit
# 1834e5b6b6): the run said `hold taken on build-1, build-2 and test-1, test-2, test-3`, then
# answered its own hold with "1 and 2 are held, and slot 3 is the merge seat's alone, so this run
# queues rather than borrow it", and waited until the lane killed it at fifteen minutes. The hold was
# taken twice by one run.
#
# The names are read back, never believed: `CAMEL_TEST_SLOT_HELD` is a claim about locks, and the
# reading that decides is `/proc/locks` plus the parent chain. A name whose lock no ancestor holds
# exits 3, because running a reading under a hold that only exists in a variable is worse than not
# running.
#
# The waits are not silent. Every `CAMEL_SLOT_ANNOUNCE_S` (60 s) a waiting run prints the slot it
# wants and who holds each slot, and the first line names the deadline `CAMEL_SLOT_WAIT_S` (1800 s).
# A wait whose holder is this process's own ancestor can never end -- the parent is waiting for this
# child -- so it is refused at once with that holder named, instead of being discovered at the
# deadline. Both of those are the second half of the defect above: the run was silent for fifteen
# minutes while it queued, and the queue it sat in could not move.
#
# Exit codes, because a run that measured nothing must not look like one that measured a red:
#   0  the command ran; its own code is what the caller sees
#   3  this script could not name its version, or the install is behind the bytes, or a tool is
#      missing, or a lock it was told it holds is not held by an ancestor, or the slot it waits for
#      is held by its own parent (a wait that cannot end)
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
#
# From a caller that already holds every test slot (a time-sensitive plan):
#   CAMEL_TEST_SLOT_HELD='1 2 3' "$CAMEL_RUN" plan --lane ... -- bash plan.sh
set -u

SELF="${BASH_SOURCE[0]}"
INSTALL_DIR="${CAMEL_RUN_HOME:-$HOME/camel-work/bin}"
INSTALLED="$INSTALL_DIR/camel-run"
VERSION_FILE="$INSTALL_DIR/camel-run.sha"
POOL_DIR="${CAMEL_TOKEN_DIR:-/tmp/camel-tokens}"
POOL_SIZE="${CAMEL_TOKENS:-24}"
OLD_LOCK=/tmp/camel-host-test.lock
SLOT_WAIT_S="${CAMEL_SLOT_WAIT_S:-1800}"
# How often a waiting run repeats "still waiting, and this is who holds it". The wait above is the
# deadline; this is the interval. Silence for the length of the deadline is what a hang looks like,
# and on 2026-11-25 a run sat in exactly that for fifteen minutes (see the header).
SLOT_ANNOUNCE_S="${CAMEL_SLOT_ANNOUNCE_S:-60}"
LANE="${CAMEL_LANE:-$(basename "$PWD")}"
BRANCH="${CAMEL_BRANCH:-}"

say() { printf '[camel-run] %s\n' "$*"; }
die() { printf '[camel-run] %s\n' "$*" >&2; exit 3; }

case "$SLOT_WAIT_S" in
    ''|*[!0-9]*) die "CAMEL_SLOT_WAIT_S is not a number of seconds: '$SLOT_WAIT_S'" ;;
esac
case "$SLOT_ANNOUNCE_S" in
    ''|*[!0-9]*) die "CAMEL_SLOT_ANNOUNCE_S is not a number of seconds: '$SLOT_ANNOUNCE_S'" ;;
esac
[ "$SLOT_ANNOUNCE_S" -ge 1 ] || die "CAMEL_SLOT_ANNOUNCE_S is $SLOT_ANNOUNCE_S; a waiting run that announces never is a run that looks hung"

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

# The pool's own reading, read the pool's way: `/proc/locks` for what is held -- `fuser` cannot tell an
# open descriptor from a held lock (internal/tools/README.md), and this line used to count open fds as
# "in use" -- and the pool's `peak-<n>` markers for the high-water mark it recorded while handing
# tokens out. Both live beside the tokens, so this needs no tool and no second opinion.
pool_report() {
    if ! pool_ready; then
        echo 'tokens: unavailable'
        return
    fi
    local held=0 free=0 ino inodes locks=' ' peak=0 f n mine=0
    if [ -s "$POOL_DIR/inodes" ]; then
        locks="$(< /proc/locks)" 2>/dev/null || locks=' '
        while read -r ino; do
            [ -n "$ino" ] || continue
            case "$locks" in *":$ino "*) held=$((held + 1)) ;; esac
        done < "$POOL_DIR/inodes"
    fi
    free=$((POOL_SIZE - held))
    [ "$free" -ge 0 ] || free=0
    for f in "$POOL_DIR"/peak-*; do
        [ -e "$f" ] || continue
        n="${f##*-}"
        case "$n" in ''|*[!0-9]*) continue ;; esac
        [ "$n" -gt "$peak" ] && peak=$n
    done
    # `mine` is what this process is holding, read from its own descriptors, not from an environment
    # variable that nothing sets: the pool hands out fds 20+ by token index. The descriptor has to
    # point at a token file -- an open fd in that range is not a token, and counting one reported
    # `mine=1` for a process that held nothing.
    n=1
    while [ "$n" -le "$POOL_SIZE" ]; do
        n=$((n + 1))
        target=$(readlink "/proc/$$/fd/$((19 + n))" 2>/dev/null || true)
        case "$target" in */token-*.lock) mine=$((mine + 1)) ;; esac
    done
    POOL_PEAK=$peak
    echo "tokens: mine=$mine peak=$peak"
    echo "pool: size=$POOL_SIZE inUse=$held free=$free"
}

# Is an flock held on this file? `/proc/locks` is the answer, and it is the only honest one: the file
# being *open* is not the file being *locked*, and the difference is a defect this repository has
# already paid for once (internal/tools/README.md: an open descriptor was read as "the old lock is
# held"). The inode is the key, matched as `:<inode> `, which is the tail of the `major:minor:inode`
# field -- one builtin read of the whole table per question, no process.
file_is_locked() {
    local file="$1" ino locks=' '
    ino=$(stat -c %i "$file" 2>/dev/null) || return 1
    [ -n "$ino" ] || return 1
    locks="$(< /proc/locks)" 2>/dev/null || return 1
    case "$locks" in *":$ino "*) return 0 ;; esac
    return 1
}

# Which processes have this file open, live ones only. This is the other half of the reading, and the
# two halves answer different questions: `/proc/locks` says *whether* it is locked, and this says
# *who could be holding it*. Neither alone is enough. `flock(1)` acquires on an inherited descriptor
# and then exits, so the pid `/proc/locks` names is usually gone a millisecond later and cannot be
# asked anything: a first version of this listing that trusted those pids answered "slot 1: free (no
# process holds it)" on 2026-11-25 about a slot the reporting run's own parent was holding, and a run
# behind that reading waits for as long as its deadline. The processes with the descriptor open are
# the ones that can be named.
# A pid already gone is dropped (`ps -o args=` empty), because this script's own `$( )` substitutions
# inherit every descriptor and would otherwise appear as blockers.
openers_of_file() {
    local one args
    for one in $(fuser "$1" 2>/dev/null | tr -s ' ' '\n' | grep -E '^[0-9]+$' | sort -u); do
        args=$(ps -o args= -p "$one" 2>/dev/null)
        [ -n "$args" ] && echo "$one"
    done
    return 0
}

# Is this pid this process, or one of its ancestors? A lock held by an ancestor is the one wait that
# cannot end: the parent started this process and is waiting for it, so waiting for the parent to let
# go would wait for this process to finish first. Read from /proc/<pid>/stat with builtins -- `ps`
# would fork twice per step and this is asked inside every announce.
#
# The command name is skipped by cutting at the *last* `) `: it may contain spaces and parentheses
# (`bash (deleted)`), so counting fields over the whole line names the wrong field. What follows is
# `state ppid ...`, and the parent is the second word of that.
is_ancestor_or_self() {
    local want="$1" p=$$ rest guard=0
    while [ -n "$p" ] && [ "$guard" -lt 128 ]; do
        [ "$p" = "$want" ] && return 0
        [ "$p" -le 1 ] && return 1
        read -r rest < "/proc/$p/stat" 2>/dev/null || return 1
        rest="${rest##*) }"
        rest="${rest#* }"
        p="${rest%% *}"
        case "$p" in ''|*[!0-9]*) return 1 ;; esac
        guard=$((guard + 1))
    done
    return 1
}

# Is this slot locked, and held (opened) by this process or an ancestor? False for a free slot and
# for a stranger's. Both halves are needed: a stranger holding it is an ordinary queue, an ancestor
# holding it is the wait that cannot end.
slot_held_by_ancestor() {
    local one file
    file="/tmp/camel-host-test-$1.lock"
    file_is_locked "$file" || return 1
    for one in $(openers_of_file "$file"); do
        is_ancestor_or_self "$one" && return 0
    done
    return 1
}

# Every candidate held by this process or an ancestor. That is the wait that can never end; one free
# or stranger-held candidate is an ordinary queue and must still be waited for.
slots_all_held_by_ancestor() {
    local n
    for n in $1; do
        slot_held_by_ancestor "$n" || return 1
    done
    return 0
}

slots_report() {
    local n file pids one args shown
    for n in 1 2 3; do
        file="/tmp/camel-host-test-$n.lock"
        if ! file_is_locked "$file"; then
            echo "slot $n: free"
            continue
        fi
        shown=0
        pids=$(openers_of_file "$file" | tr '\n' ' ')
        for one in $pids; do
            args=$(ps -o args= -p "$one" 2>/dev/null)
            [ -n "$args" ] || continue
            if is_ancestor_or_self "$one"; then
                echo "slot $n: held by pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' ')) $(printf '%s' "$args" | cut -c1-80) [this run's own parent, or this process: it cannot be released while this process waits]"
            else
                echo "slot $n: held by pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' ')) $(printf '%s' "$args" | cut -c1-80)"
            fi
            shown=1
        done
        [ "$shown" = 0 ] && echo "slot $n: held, and no process in this namespace has it open (a container, or a namespace of its own)"
    done
}

load_line() {
    local l
    l=$(awk '{print $1" "$2" "$3}' /proc/loadavg 2>/dev/null || echo '')
    echo "loadAtStart: $l"
}

old_lock_holders() {
    # Who holds the old lock, this process excluded: our own descriptor is open whether or not the
    # lock was taken, and naming ourselves as the blocker is the one answer that is certainly wrong.
    # An ancestor is named and marked, because that is a holder this run can never outwait.
    local pids one args shown=0
    pids=$(openers_of_file "$OLD_LOCK" | grep -vx "$$" || true)
    for one in $pids; do
        args=$(ps -o args= -p "$one" 2>/dev/null)
        [ -n "$args" ] || continue
        if is_ancestor_or_self "$one"; then
            echo "old lock $OLD_LOCK: pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' ')) $(printf '%s' "$args" | cut -c1-100) [this run's own parent]"
        else
            echo "old lock $OLD_LOCK: pid $one ($(ps -o etime= -p "$one" 2>/dev/null | tr -d ' ')) $(printf '%s' "$args" | cut -c1-100)"
        fi
        shown=1
    done
    [ "$shown" = 0 ] && echo "old lock $OLD_LOCK: no other process holds it"
    return 0
}

old_lock_held_by_ancestor() {
    local one
    file_is_locked "$OLD_LOCK" || return 1
    for one in $(openers_of_file "$OLD_LOCK"); do
        is_ancestor_or_self "$one" && return 0
    done
    return 1
}

# The hold the caller names in CAMEL_TEST_SLOT_HELD, read back instead of believed. Each name must be
# a host test slot and must be held by this process's own parent chain -- that is what makes the
# caller's hold this run's hold. A name that fails either test refuses the run (exit 3): the one
# thing this script must never do is run a reading under a hold that exists only in a variable.
inherited_hold() {
    local n bad=0
    for n in $1; do
        case "$n" in
            1|2|3) ;;
            *) say "CAMEL_TEST_SLOT_HELD names '$n'; the host has test slots 1, 2 and 3"
               bad=1
               continue ;;
        esac
        slot_held_by_ancestor "$n" && continue
        say "CAMEL_TEST_SLOT_HELD says this run holds test-$n, but no ancestor of this process holds /tmp/camel-host-test-$n.lock; refusing to run as if the host were this run's alone"
        slots_report
        bad=1
    done
    [ "$bad" = 0 ]
}

merge_seat() {
    case "$BRANCH" in seat/merge|seat/merge/*|seat/merge-*) echo 1 ;; *) echo 0 ;; esac
}

cmd_plan() {
    local slot=0 n fd got told=0 announced=0 waited=0 candidates started inherited held_list old_lock_waited
    check_tools
    check_version
    say "CAMEL_RUN $VERSION lane=$LANE branch=${BRANCH:-?}"
    inherited="${CAMEL_TEST_SLOT_HELD:-}"
    if [ -n "$inherited" ]; then
        # Read back before anything is waited for: a hold that does not exist is not a hold, and this
        # run would otherwise report a measurement taken under an exclusion nobody took.
        inherited_hold "$inherited" || exit 3
    fi
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
    # The wait is a loop of non-blocking attempts and not `flock -w`: a blocking call cannot say
    # anything while it blocks, and the one thing this wait must not do is look like a hang. Each
    # round it asks, and every `SLOT_ANNOUNCE_S` it prints who holds the lock instead of leaving the
    # reader with a silence as long as the deadline.
    exec 7>"$OLD_LOCK"
    started=$(date +%s)
    announced=0
    told=0
    while ! flock -n -s 7; do
        waited=$(( $(date +%s) - started ))
        if old_lock_held_by_ancestor; then
            say "the old lock $OLD_LOCK is held by this run's own parent, so waiting for it cannot end: refusing rather than queueing behind the process that started this run (nothing was measured)"
            old_lock_holders
            echo "oldLockWaitSeconds=$waited"
            exit 3
        fi
        if [ "$told" = 0 ]; then
            say "waiting for a shared hold on $OLD_LOCK; this run gives up after ${SLOT_WAIT_S} s (CAMEL_SLOT_WAIT_S) and repeats this line every ${SLOT_ANNOUNCE_S} s (CAMEL_SLOT_ANNOUNCE_S)"
            old_lock_holders
            told=1
            announced=$waited
        fi
        if [ "$waited" -ge "$SLOT_WAIT_S" ]; then
            say "no shared hold on $OLD_LOCK within ${SLOT_WAIT_S} s (waited ${waited} s): a run of the old shape is taking the host alone, so this run measured nothing and exits 5"
            old_lock_holders
            echo "oldLockWaitSeconds=$waited"
            exit 5
        fi
        if [ $((waited - announced)) -ge "$SLOT_ANNOUNCE_S" ]; then
            say "still waiting for a shared hold on $OLD_LOCK (${waited}s of ${SLOT_WAIT_S}s); who holds it:"
            old_lock_holders
            announced=$waited
        fi
        sleep 2
    done
    old_lock_waited=$(( $(date +%s) - started ))
    echo "oldLockWaitSeconds=$old_lock_waited"
    # The slot's clock starts here, not at the shared hold: `lockWaitSeconds` is the slot wait, and a
    # number that quietly also carried the old-lock wait would answer two questions at once (they are
    # different ones: "did an old run block me" and "were the three slots all taken").
    started=$(date +%s)

    # A caller that holds the host's test slots itself (a time-sensitive plan takes all three before
    # its build) says so in CAMEL_TEST_SLOT_HELD, and this takes none of them again. The hold is real
    # -- it is read back from /proc/locks and the parent chain -- and the reading names its holder, so
    # a registry entry this run writes still says which slots the measurement was taken under.
    if [ -n "$inherited" ]; then
        held_list=$(printf '%s' "$inherited" | tr ' ' '\n' | sort -n | tr '\n' '&')
        held_list="${held_list%&}"
        say "test slots $held_list are held by this run's caller (CAMEL_TEST_SLOT_HELD); not asking for one a second time"
        echo "HOST_TEST_WAIT_END=$held_list"
        echo "HOST_TEST_SLOT=$held_list"
        echo "lockFrom=inherited@$PPID"
        echo "lockWaitSeconds=0"
        pool_report
        echo "tokensPeak: ${POOL_PEAK:-0}"
        load_line
        if [ "$#" -ge 1 ]; then
            [ "$1" = '--' ] && shift
            exec "$@"
        fi
        return 0
    fi

    # `CAMEL_EXCLUSIVE` asks for every slot, so the loop's unit is the whole set rather than one: a
    # partial hold is released and the set retried, because two runs holding one slot each would wait
    # on each other until the deadline. This is also the only place the slots are opened -- the block
    # that used to live below re-opened the same files on fresh descriptors, and `flock` is per open
    # file description, so a process's second `open` of a file it already locked is refused by its own
    # lock: that block answered "exclusive: slot 1 busy" about itself, on every run, and reported a
    # busy host that was its own hold.
    if [ "${CAMEL_EXCLUSIVE:-0}" = 1 ]; then
        candidates='1 2 3'
    fi
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
        if [ "$got" = 1 ] && [ "${CAMEL_EXCLUSIVE:-0}" = 1 ]; then
            for n in 1 2 3; do
                [ "$n" = "$slot" ] && continue
                fd=$((20 + n))
                eval "exec $fd>/tmp/camel-host-test-$n.lock"
                flock -n "$fd" || { got=0; break; }
            done
            if [ "$got" = 0 ]; then
                for n in 1 2 3; do fd=$((20 + n)); eval "exec $fd>&-"; done
                slot=0
            fi
        fi
        [ "$got" = 1 ] && break
        # Every slot this run may take is held by its own parent: the parent is waiting for this
        # process, so no amount of waiting frees one. Refused before the queue line is printed, and
        # at once, because the deadline is thirty minutes of a run that could never start -- the
        # shape of the 2026-11-25 defect.
        if slots_all_held_by_ancestor "$candidates"; then
            say "every slot this run may take ($candidates) is held by this run's own parent, so waiting for one cannot end: refusing rather than queueing behind the process that started this run (nothing was measured)"
            slots_report
            exit 3
        fi
        if [ "$told" = 0 ]; then
            if [ "$(merge_seat)" = 1 ]; then
                say "waiting for a host test slot (1, 2, 3; 3 is the merge seat's)"
            else
                say "waiting for a host test slot: 1 and 2 are held, and slot 3 is the merge seat's alone, so this run queues rather than borrow it"
            fi
            echo "HOST_TEST_WAIT_START=$candidates $(date -u +%Y-%m-%dT%H:%M:%SZ)"
            slots_report
            say "this run gives up after ${SLOT_WAIT_S} s (CAMEL_SLOT_WAIT_S) and repeats the listing every ${SLOT_ANNOUNCE_S} s (CAMEL_SLOT_ANNOUNCE_S)"
            told=1
            announced=$(($(date +%s) - started))
        fi
        waited=$(( $(date +%s) - started ))
        if [ "$waited" -ge "$SLOT_WAIT_S" ]; then
            say "no host test slot within ${SLOT_WAIT_S} s (waited ${waited} s; this run may take $candidates)"
            slots_report
            exit 4
        fi
        if [ $((waited - announced)) -ge "$SLOT_ANNOUNCE_S" ]; then
            say "still waiting for a host test slot (${waited}s of ${SLOT_WAIT_S}s; this run may take $candidates); who holds what:"
            slots_report
            announced=$waited
        fi
        sleep 2
    done
    waited=$(( $(date +%s) - started ))

    # The reading names what was held, not the first slot that was taken: an exclusive run holds all
    # three, and "1" for that run is a number a reader would compare with an ordinary run's.
    if [ "${CAMEL_EXCLUSIVE:-0}" = 1 ]; then
        slot='1&2&3'
    fi
    echo "HOST_TEST_WAIT_END=$slot"
    echo "HOST_TEST_SLOT=$slot"
    echo "lockFrom=$0@$VERSION"
    echo "lockWaitSeconds=$waited"
    pool_report
    echo "tokensPeak: ${POOL_PEAK:-0}"
    load_line
    [ "${CAMEL_EXCLUSIVE:-0}" = 1 ] && echo 'exclusive: true'

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
    # `tokensWanted`, not `tokensPeak`: the peak is what the pool recorded while this build was
    # running, and a line printed before the tokens are taken is a claim about the future.
    echo "tokensWanted: $want"
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
    echo "tokensPeak: ${POOL_PEAK:-0}"
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
