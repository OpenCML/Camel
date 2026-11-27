#!/usr/bin/env bash
# Copyright (c) 2026 The Camel Authors. All rights reserved.
# Use of this source code is governed by the Apache License 2.0.
#
# camel-run: the one place on the build host that admits a run, and says what it holds.
#
# THE HOST'S ONLY ADMISSION IS THE TOKEN POOL (`host-tokens.sh`). A token is one worker process; a
# build takes what is free, up to the width it asks for, and a plan run does exactly the same. There
# is no second mechanism -- no test slot, no single lock, no reserved seat. Two ways to admit a run
# that do not know about each other is the same thing as none, and the host is shared: it is only
# asked whether a run is correct, nothing on it is timed, so a run that is small should not queue
# behind a count of runs when the pool has room.
#
# Any `flock` written inline in a generated `run.sh` is a defect for the same reason.
#
# Failures that close, each a place where falling back would be easy and wrong:
#
#   1. this script absent, or not executable        -> the caller exits; it does not inline old code
#   2. the installed sha disagrees with the caller's expectation
#                                                  -> exits and names both, rather than running unknown code
#   3. the install cannot be verified (no checkout, no `.git`, `origin/integration` never fetched)
#                                                  -> exits and says which of the three is missing
#   4. a needed tool is missing (`flock`, `date`)    -> exits and names it
#   5. the installed bytes are not the bytes the install record was written for
#                                                  -> exits and prints both digests (see `version_check`)
#
# One thing opens, because it must not block:
#
#   * no token pool yet              -> report `tokens: unavailable` and run the command unchanged
#
# A plan run:
#
#   * asks for its job count (`CAMEL_JOBS`, which the generated `run.sh` sets from `-Jobs`), takes up to
#     that many tokens with the pool's own `take-up-to` -- as many as are free, never fewer than one --
#     and runs the command with the number it got as `CAMEL_JOBS`. `HOST_JOBS=<n>` says that number.
#   * runs everything under it inside that hold (`CAMEL_TOKENS_COVERED=1`): a case, a nested sweep or
#     any `host-tokens.sh` call below the run takes nothing more (that rule is in `host-tokens.sh`),
#     because a descendant waiting for tokens its ancestors' siblings hold is hold-and-wait.
#   * gives the tokens back when the command ends. The kernel returns a flock when its holder dies, so
#     a run that is killed returns them too.
#
# Waiting is not silent: the pool's taker prints who holds what while it waits, and the wait limit
# is `CAMEL_TOKEN_WAIT_S` (1800 s).
#
# Exit codes, because a run that measured nothing must not look like one that measured a red:
#   0  the command ran; its own code is what the caller sees
#   3  this script could not name its version, or the install is behind the bytes, or a tool is missing
#   4  no token within the wait -- the run measured nothing; `HOST_STATE=noslot` is printed with it
#
# Subcommands:
#   plan      take tokens for the run's job count, then run the command that follows `--`
#   build     take tokens for a build (`CAMEL_BUILD_TOKENS`, default 8), then run the command
#   status    print the pool as it is now
#   self-test check the plan admission against a pool of its own (needs `flock` and `/proc/locks`)
#   install   idempotent: put this script and the pool's taker (`host-tokens.sh`) in the install
#             directory and record `<version> <sha256 of this script> <sha256 of the taker>` beside
#             them, print `installed <version> (...) at <time>`. It refuses a checkout with uncommitted
#             changes to either file: the version would name bytes the commit does not contain.
#
# Usage from a generated run.sh:
#   CAMEL_JOBS=16 "${CAMEL_RUN:=<home>/camel-work/bin/camel-run}" plan --lane "$name" --branch "$branch" -- \
#       bash plan.sh
set -u

SELF="${BASH_SOURCE[0]}"
INSTALL_DIR="${CAMEL_RUN_HOME:-$HOME/camel-work/bin}"
INSTALLED="$INSTALL_DIR/camel-run"
VERSION_FILE="$INSTALL_DIR/camel-run.sha"
POOL_DIR="${CAMEL_TOKEN_DIR:-/tmp/camel-tokens}"
POOL_SIZE="${CAMEL_TOKENS:-24}"
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

# The taker of the same version as this script. A checkout keeps it at `internal/tools/host-tokens.sh`,
# one directory above this file; an install keeps it beside the installed copy, because `install`
# copies the two together and records both. Which layout this is follows from the same question
# `version_check` asks: git can name a checkout's version and cannot name an install's.
taker_path() {
    local dir
    dir=$(dirname "$SELF")
    if [ -n "$(sha_of)" ]; then
        printf '%s/../host-tokens.sh' "$dir"
    else
        printf '%s/host-tokens.sh' "$dir"
    fi
}

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
    local dir have recorded want want_taker mine mine_taker taker
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
    want_taker=$(awk 'NR == 1 { print $3 }' "$VERSION_FILE" 2>/dev/null)
    mine=$(digest_of "$SELF")
    taker=$(taker_path)
    mine_taker=$(digest_of "$taker")
    if [ -z "$recorded" ] || [ -z "$want" ] || [ -z "$want_taker" ] || [ -z "$mine" ]; then
        die "the install record $VERSION_FILE cannot name these bytes: it holds '${recorded:-}' '${want:-}' '${want_taker:-}' and $SELF has no digest of its own. Reinstall with a version that records this script and its taker (camel-run.sh install)"
    fi
    if [ "$mine" != "$want" ]; then
        die "install did not keep up: $VERSION_FILE says $recorded, written for bytes $want, but $SELF is $mine. Reinstall from that checkout (camel-run.sh install); these bytes have no version until then, and a run will not take a token on a version nobody can name"
    fi
    if [ -z "$mine_taker" ]; then
        die "the install is incomplete: $taker is missing, and a run needs the taker of the version it is. Reinstall from a checkout (camel-run.sh install), which copies both"
    fi
    if [ "$mine_taker" != "$want_taker" ]; then
        die "install did not keep up: $VERSION_FILE says $recorded, written for a taker $want_taker, but $taker is $mine_taker. Reinstall from that checkout (camel-run.sh install)"
    fi
    VERSION="$recorded"
}

check_version() {
    # The caller may name the version it expects (a run.sh records the one that generated it). A
    # disagreement is refused with both values, because "which tools took this reading" is the question
    # that cost half a day.
    local want="${CAMEL_RUN_EXPECT:-}"
    local have
    version_check
    have="$VERSION"
    if [ -n "$want" ] && [ "$want" != "$have" ]; then
        die "version mismatch: this script is $have, the run expects $want. Reinstall from origin/integration, or run with CAMEL_RUN_EXPECT=$have if $have is known good"
    fi
}

check_tools() {
    for t in flock date awk sha256sum; do
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


load_line() {
    local l
    l=$(awk '{print $1" "$2" "$3}' /proc/loadavg 2>/dev/null || echo '')
    echo "loadAtStart: $l"
}


# A plan run takes tokens the way a build does and runs with the number it got.
#
# The job count the run asks for is `CAMEL_JOBS`; the pool hands out what is free, at least one, at
# most that. The command then runs as a child of the pool's taker, so the tokens live exactly as long
# as it does and the kernel returns them when it dies. The command is not `exec`-ed from here: the
# caller's last lines report the run's own exit, and a replaced shell prints nothing.
#
# The taker exits 6 when no token came within the wait, and the command may exit 6 for its own reasons.
# A marker file the child touches before it starts the command is what tells them apart: no marker,
# no run -- that is `noslot` (exit 4), a run that measured nothing.
cmd_plan() {
    local want="${CAMEL_JOBS:-16}" taker marker code
    check_tools
    check_version
    say "CAMEL_RUN $VERSION lane=$LANE branch=${BRANCH:-?} plan jobs=$want"
    case "$want" in
        ''|*[!0-9]*) die "CAMEL_JOBS is not a number: '$want'" ;;
    esac
    [ "$want" -ge 1 ] || die "CAMEL_JOBS is $want; a plan run needs at least one job"
    [ "${1:-}" = '--' ] && shift
    [ "$#" -ge 1 ] || return 0
    if ! pool_ready; then
        echo 'tokens: unavailable'
        echo 'tokensPeak: 0'
        exec "$@"
    fi
    taker=$(taker_path)
    [ -x "$taker" ] || die "token pool present but $taker is missing: refusing to run outside the pool"
    marker=$(mktemp "${TMPDIR:-/tmp}/camel-run-plan.XXXXXX") || die "cannot make a marker file"
    rm -f "$marker"
    echo "tokensWanted: $want"
    pool_report
    load_line
    # `HOST_JOBS` is the label the local side records as the width the run ran at; the taker prints
    # it with the count it took, ahead of the command.
    CAMEL_TAKE_LABEL=HOST_JOBS "$taker" take-up-to "$want" -- \
        bash -c 'export CAMEL_JOBS="$CAMEL_TOKENS_HELD" CAMEL_TOKENS_COVERED=1; : > "$1"; shift; exec "$@"' \
        camel-run-plan "$marker" "$@"
    code=$?
    if [ ! -e "$marker" ]; then
        rm -f "$marker"
        if [ "$code" = 6 ]; then
            say "no token within ${CAMEL_TOKEN_WAIT_S:-1800} s (CAMEL_TOKEN_WAIT_S): the host's pool is held by other runs, so this run measured nothing and exits 4"
            echo 'HOST_NOSLOT=tokens'
            echo 'HOST_STATE=noslot'
            exit 4
        fi
        exit "$code"
    fi
    rm -f "$marker"
    exit "$code"
}
# A build takes tokens the same way a plan run does: what is free, up to the width it asks for, and
# that width is what it compiles at (`ninja -j k`). Concurrency between runs is the pool's business.


# A build takes tokens the same way a plan run does: what is free, up to the width it asks for, and
# that width is what it compiles at (`ninja -j k`). Concurrency between runs is the pool's business.
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
    taker=$(taker_path)
    [ -x "$taker" ] || die "token pool present but $taker is missing: refusing to build outside the pool"
    # `tokensWanted`, not `tokensPeak`: the peak is what the pool recorded while this build was
    # running, and a line printed before the tokens are taken is a claim about the future.
    echo "tokensWanted: $want"
    load_line
    [ "$#" -ge 1 ] && [ "$1" = '--' ] && shift
    # What it takes is what is free, not what was asked for: a build that queued while the pool held
    # free tokens is the wait this command exists to remove, and the width it got is printed as
    # `HOST_BUILD_JOBS` by the taker.
    exec "$taker" take-up-to "$want" -- "$@"
}
cmd_status() {
    check_tools
    # The same resolution a plan run does, so an install that fell behind is seen here -- by the
    # cheapest command there is -- rather than on the run it would have refused.
    version_check
    say "CAMEL_RUN $VERSION"
    pool_report
    echo "tokensPeak: ${POOL_PEAK:-0}"
    load_line
}

cmd_install() {
    # Idempotent: running it twice changes nothing after the first time, and it says so either way.
    mkdir -p "$INSTALL_DIR" || die "cannot create $INSTALL_DIR"
    local here sha digest taker taker_digest changed=0 src dst
    here=$(cd "$(dirname "$SELF")" && pwd)
    sha=$(self_sha)
    [ -z "$sha" ] && die "cannot name this script's version; install from a checkout of origin/integration"
    taker=$(taker_path)
    [ -f "$taker" ] || die "the checkout at $here has no taker at $taker, so the install would be a script that cannot take a token. Install from a full checkout"
    # The record names the bytes, so it may only be written for bytes its checkout actually holds. A
    # copy edited on the way in would install under a commit it is not -- which is the one thing the
    # record exists to prevent, and the reason `version_check` compares digests at all.
    if [ -n "$(git -C "$here" status --porcelain --untracked-files=no -- "$SELF" "$taker" 2>/dev/null)" ]; then
        die "the checkout at $here has uncommitted changes to this script or its taker, so $sha would name bytes that commit does not contain. Commit them, then install"
    fi
    digest=$(digest_of "$SELF")
    taker_digest=$(digest_of "$taker")
    if [ -z "$digest" ] || [ -z "$taker_digest" ]; then
        die "cannot compute a digest of $SELF or $taker, so the record could not be checked. Refusing to install"
    fi
    # The script and its taker travel together: a run resolves the taker beside the installed copy,
    # and a taker of another version would admit runs by rules this version does not hold. `mv` over
    # the destination replaces whatever is there, a link included, with a file of these bytes.
    for src in "$SELF" "$taker"; do
        if [ "$src" = "$SELF" ]; then dst="$INSTALLED"; else dst="$INSTALL_DIR/host-tokens.sh"; fi
        if [ -f "$dst" ] && [ ! -L "$dst" ] && cmp -s "$src" "$dst"; then continue; fi
        cp "$src" "$dst.tmp" && chmod 755 "$dst.tmp" && mv -f "$dst.tmp" "$dst" || die "install failed at $dst"
        changed=$((changed + 1))
    done
    # `<version> <sha256 of this script> <sha256 of its taker>`: the version, and the bytes it is a
    # version of.
    printf '%s %s %s\n' "$sha" "$digest" "$taker_digest" > "$VERSION_FILE"
    echo "installed $sha (camel-run $digest, host-tokens.sh $taker_digest) at $(date -u +%Y-%m-%dT%H:%M:%SZ) (from $here; $([ "$changed" = 0 ] && echo 'unchanged' || echo "$changed file(s) replaced"))"
}

# The plan admission, against a pool of this test's own (four tokens in a temporary directory, so
# nothing here touches the host's pool). It needs what the pool needs -- `flock` and a readable
# `/proc/locks` -- and says so and stops where either is missing, instead of reporting a pass it
# could not have measured.
#
# Every wait is a bounded poll on the pool's own `show`, never a clock reading: a loaded host
# stretches every wait, so "it took under N seconds" would only say how busy the machine was.
cmd_self_test() {
    local root taker bad=0 out pids sleeper i code
    if ! command -v flock >/dev/null 2>&1; then
        echo 'skip  camel-run self-test: no flock on this machine (the pool is flock on token files); run it on the build host'
        return 0
    fi
    if [ ! -r /proc/locks ]; then
        echo 'skip  camel-run self-test: /proc/locks is not readable here (the pool reads held tokens from it); run it on the build host'
        return 0
    fi
    taker=$(taker_path)
    [ -x "$taker" ] || die "self-test needs $taker"
    version_check
    root=$(mktemp -d "${TMPDIR:-/tmp}/camel-run-selftest.XXXXXX") || die "cannot make a test directory"
    export CAMEL_TOKEN_DIR="$root/pool" CAMEL_TOKENS=4 CAMEL_LANE=selftest CAMEL_TOKEN_WAIT_S=120
    POOL_DIR="$root/pool"
    "$taker" show >/dev/null 2>&1

    # Wait until the pool shows PATTERN, at most about 30 s.
    settle() {
        i=0
        while :; do
            out=$("$taker" show 2>/dev/null)
            case "$out" in *"$1"*) return 0 ;; esac
            i=$((i + 1))
            [ "$i" -lt 300 ] || return 1
            sleep 0.1
        done
    }
    # Every descendant of a pid, newest last, for killing a whole run.
    tree_of() {
        local p
        echo "$1"
        for p in $(ps -o pid= --ppid "$1" 2>/dev/null); do tree_of "$p"; done
    }

    # 1. an empty pool: a run asking for three gets three, and its command sees that width
    out=$(CAMEL_JOBS=3 "$SELF" plan -- bash -c 'echo "in:$CAMEL_JOBS:$CAMEL_TOKENS_COVERED"; "$1" show | tail -1' _ "$taker" 2>&1)
    case "$out" in
        *'HOST_JOBS=3'*'in:3:1'*'free=1 held=3'*) echo 'ok    a plan run on an empty pool takes the three it asked for and runs at three' ;;
        *) echo "FAIL  a plan run on an empty pool: $out"; bad=$((bad + 1)) ;;
    esac
    settle 'free=4 held=0' && echo 'ok    the tokens are back when the run ends' || { echo "FAIL  tokens not back after a run: $out"; bad=$((bad + 1)); }

    # 2. a short pool: three of four held elsewhere, a run asking for three gets the one that is free
    ( "$taker" take 3 >/dev/null; exec sleep 60 ) &
    sleeper=$!
    settle 'free=1 held=3'
    out=$(CAMEL_JOBS=3 "$SELF" plan -- bash -c 'echo "in:$CAMEL_JOBS"' 2>&1)
    case "$out" in
        *'HOST_JOBS=1'*'in:1'*) echo 'ok    on a short pool a plan run takes what is free and runs with that many jobs' ;;
        *) echo "FAIL  a plan run on a short pool: $out"; bad=$((bad + 1)) ;;
    esac

    # 3. nothing free: the wait runs out, which is exit 4 with state=noslot, and the command never ran
    ( "$taker" take 1 >/dev/null; exec sleep 60 ) &
    pids="$sleeper $!"
    settle 'free=0 held=4'
    out=$(CAMEL_TOKEN_WAIT_S=2 CAMEL_JOBS=2 "$SELF" plan -- bash -c 'echo ran-the-command' 2>&1)
    code=$?
    case "$code:$out" in
        4:*'HOST_STATE=noslot'*) ;;
        *) echo "FAIL  a plan run with no token: exit $code: $out"; bad=$((bad + 1)) ;;
    esac
    case "$out" in
        *ran-the-command*) echo 'FAIL  the command ran without a token'; bad=$((bad + 1)) ;;
        *) [ "$code" = 4 ] && echo 'ok    a plan run that gets no token says state=noslot, exits 4, and does not run the command' ;;
    esac
    for i in $pids; do kill -9 "$i" 2>/dev/null; done
    wait 2>/dev/null
    settle 'free=4 held=0' || { echo "FAIL  the test's own holders did not release: $out"; bad=$((bad + 1)); }

    # 4. the command's own exit 6 is its own, not a wait that ran out
    CAMEL_JOBS=1 "$SELF" plan -- bash -c 'exit 6' >/dev/null 2>&1
    code=$?
    [ "$code" = 6 ] && echo "ok    the command's own exit code 6 reaches the caller as 6" || { echo "FAIL  exit 6 became $code"; bad=$((bad + 1)); }

    # 5. a run killed mid-way returns its tokens
    ( CAMEL_JOBS=2 "$SELF" plan -- sleep 61 >/dev/null 2>&1 ) &
    sleeper=$!
    settle 'free=2 held=2'
    for i in $(tree_of "$sleeper" | sort -rn); do kill -9 "$i" 2>/dev/null; done
    wait "$sleeper" 2>/dev/null
    if settle 'free=4 held=0'; then
        echo 'ok    a plan run killed mid-way gives its tokens back'
    else
        echo "FAIL  a killed plan run kept its tokens: $out"; bad=$((bad + 1))
    fi

    local checks=6
    # 6. an install carries its taker: the installed copy resolves `host-tokens.sh` beside itself and
    #    its record holds both digests, and a taker changed after the install is refused by name.
    #    Only a checkout can install, so the installed copy skips this.
    if [ -n "$(sha_of)" ]; then
        checks=$((checks + 2))
        out=$(CAMEL_RUN_HOME="$root/bin" bash "$SELF" install 2>&1 && CAMEL_RUN_HOME="$root/bin" bash "$root/bin/camel-run" status 2>&1)
        code=$?
        case "$code:$out" in
            0:*'CAMEL_RUN '*)
                if [ -f "$root/bin/host-tokens.sh" ] && [ "$(awk 'NR == 1 { print NF }' "$root/bin/camel-run.sha")" = 3 ]; then
                    echo 'ok    an install copies the taker beside the script and records both digests'
                else
                    echo "FAIL  the install left no taker or a record without its digest: $(ls "$root/bin" 2>&1)"; bad=$((bad + 1))
                fi
                ;;
            *) echo "FAIL  an install from this checkout: exit $code: $out"; bad=$((bad + 1)) ;;
        esac
        echo '# changed after the install' >> "$root/bin/host-tokens.sh"
        out=$(CAMEL_RUN_HOME="$root/bin" bash "$root/bin/camel-run" status 2>&1)
        code=$?
        case "$code:$out" in
            3:*'written for a taker'*) echo 'ok    an installed copy refuses a taker that is not the one its record names' ;;
            *) echo "FAIL  a changed taker was not refused: exit $code: $out"; bad=$((bad + 1)) ;;
        esac
    else
        echo 'skip  the install checks: this copy is not in a checkout, and only a checkout can install'
    fi

    rm -rf "$root"
    if [ "$bad" -eq 0 ]; then echo "selftest: $checks checks, 0 failed"; return 0; fi
    echo "selftest: $bad failed"; return 1
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
    self-test) cmd_self_test ;;
    *) die "unknown subcommand '$cmd' (plan, build, status, self-test, install)" ;;
esac
