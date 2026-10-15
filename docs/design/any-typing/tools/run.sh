#!/bin/bash
# run.sh [-t secs] <file.cml> [passes...]   with optional environment in front:
#   CAMEL_GC=generational bash docs/design/any-typing/tools/run.sh docs/design/any-typing/examples/t07_gc_frame.cml std::nvm
# Prints the first lines of stdout+stderr and the exit code. Run from the repository root with
# `camel` on PATH (or out/latest/bin/camel present).
T=30
if [ "${1:-}" = "-t" ]; then T=$2; shift 2; fi
CAMEL=${CAMEL:-$( [ -x out/latest/bin/camel ] && echo out/latest/bin/camel || echo camel )}
out=$(timeout "$T" "$CAMEL" "$@" 2>&1); rc=$?
printf '%s\n' "$out" | sed 's/\x1b\[[0-9;]*m//g' | head -${LINES_MAX:-12}
echo "exit=$rc"
