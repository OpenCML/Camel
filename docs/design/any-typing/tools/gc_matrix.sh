#!/bin/bash
# gc_matrix.sh <file.cml>...
# Runs each program (whose main takes `n`, the number of strings to allocate between making a
# value and using it) under five collector settings and prints one line per run: the exit code
# and the first line of output. Run from the repository root.
#
#   bash docs/design/any-typing/tools/gc_matrix.sh docs/design/any-typing/examples/t0[7-9]*.cml
#
# BACKENDS="std::nvm std::fvm std::jit std::prl" chooses back ends (default nvm and fvm);
# PASSES="std::macro" puts passes in front of the back end; N and NSTRESS set `n` for the plain
# and the forced-collection runs (200000 and 300); T is the time limit of one run in seconds.
CAMEL=${CAMEL:-$( [ -x out/latest/bin/camel ] && echo out/latest/bin/camel || echo camel )}
BACKENDS=${BACKENDS:-"std::nvm std::fvm"}
PASSES=${PASSES:-}
N=${N:-200000}
NSTRESS=${NSTRESS:-300}
T=${T:-60}
ulimit -c 0
run() { # label n env...
    local label=$1 n=$2; shift 2
    local out rc
    out=$(env "$@" timeout "$T" "$CAMEL" "$FILE" --arg n="$n" $PASSES $BACKEND 2>&1); rc=$?
    printf '  %-28s n=%-7s exit=%-3s %s\n' "$label" "$n" "$rc" \
        "$(printf '%s' "$out" | sed 's/\x1b\[[0-9;]*m//g' | grep -v '^$' | head -1 | cut -c1-100)"
}
for FILE in "$@"; do
    for BACKEND in $BACKENDS; do
        echo "== $(basename "$FILE") $PASSES $BACKEND"
        run "mark-sweep (default)"       "$N"       X=1
        run "generational"               "$N"       CAMEL_GC=generational
        run "mark-sweep, forced major"   "$NSTRESS" CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=major
        run "generational, forced minor" "$NSTRESS" CAMEL_GC=generational CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=minor
        run "poison"                     "$NSTRESS" CAMEL_GC_POISON=1
    done
done
