#!/usr/bin/env bash
# Copyright (c) 2026 The Camel Authors. All rights reserved.
# Use of this source code is governed by the Apache License 2.0.
#
# host-case: the one line of shell in front of every test case on the build host.
#
# Two things belong in front of a case and nowhere else, which is why they are here and not in the
# runner:
#
#   ulimit -c 0   A crashing case is still a red case -- that does not change. What changes is how
#                 long the machine spends writing a core dump and handing it to `apport` before the
#                 next case starts. On 2026-11-24 a plan run held the host's only test slot for
#                 fifty-one minutes, and nobody could say how much of that was `apport`. The limit
#                 is inherited, so setting it here covers the case's whole process tree.
#                 It is this process's own limit: no host setting (`core_pattern`, `/etc/security`)
#                 is touched, because a gate that reconfigures the machine is not a gate.
#
#   a token       One worker process, one token, from the same pool the build and the clang check
#                 use. Taken only when a pool is configured (`CAMEL_TOKEN_DIR` set and the token
#                 tool present), so a lane on a machine without one runs exactly as before.
#
#                 Under a plan run that already holds its width (`CAMEL_TOKENS_COVERED=1`) the token
#                 tool takes nothing; that rule lives in `host-tokens.sh`, in one place.
#
# `exec` is deliberate: after this line the case *is* this process, so the token is held for exactly
# the case's life and returned by the kernel when it exits -- including when it is killed.
set -u

ulimit -c 0 2>/dev/null || true

here=$(dirname "$0")
tokens="$here/host-tokens.sh"
if [ -n "${CAMEL_TOKEN_DIR:-}" ] && [ -f "$tokens" ] && command -v flock >/dev/null 2>&1; then
    exec bash "$tokens" run-quiet -- "$@"
fi

exec "$@"