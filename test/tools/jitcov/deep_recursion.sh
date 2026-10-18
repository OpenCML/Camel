#!/bin/bash
# Deep recursion under raised limits, small machine stacks and every back end: each run must end
# with the result or with a located RuntimeDiag::MaxRecursionDepthExceeded, never by a signal.
#
#     test/tools/jitcov/deep_recursion.sh            (from the repository root; camel on PATH)
#
# Prints one line per run and a final count; exits 1 when a run ended by a signal or without
# either outcome. It is a manual check (it spends minutes on recursions of up to a million
# levels); the test plan test/plans/feat/consistency/limits.plan.toml keeps the short ones.
set -u
DIR=test/cases/consistency/limits
bad=0
total=0
check() { # stack_kib limit n case pipeline...
    local stack=$1 limit=$2 n=$3 case=$4
    shift 4
    local out rc
    out=$( (
        [ "$stack" = default ] || ulimit -s "$stack"
        N=$n timeout 120 camel --max-recursion-depth "$limit" "$DIR/$case" "$@" 2>&1
    ) )
    rc=$?
    total=$((total + 1))
    local verdict
    if [ $rc -ge 124 ]; then
        verdict="BAD (exit $rc: signal or timeout)"
        bad=$((bad + 1))
    elif [ $rc -eq 0 ]; then
        verdict="result"
    elif echo "$out" | grep -q "MaxRecursionDepthExceeded" && echo "$out" | grep -Eq "\.cml:[0-9]+:[0-9]+"; then
        verdict="diagnostic ($(echo "$out" | grep -o 'Maximum recursion depth ([^)]*)' | head -1))"
    else
        verdict="BAD (exit $rc without the diagnostic)"
        bad=$((bad + 1))
    fi
    printf '%-8s limit=%-8s N=%-8s %-20s %-24s %s\n' "$stack" "$limit" "$n" "$case" "$*" "$verdict"
}

for stack in default 8192 1024 256; do
    for limit in 100000 1000000; do
        for n in 50000 900000; do
            for pipeline in "std::nvm" "std::fvm" "std::jit" "std::prl" "std::opt::inline std::jit" "std::opt std::jit" "std::opt::inline std::prl"; do
                # shellcheck disable=SC2086
                check "$stack" "$limit" "$n" depth_direct.cml $pipeline
            done
            for pipeline in "std::jit" "std::opt::inline std::jit" "std::prl"; do
                # shellcheck disable=SC2086
                check "$stack" "$limit" "$n" depth_isolated.cml $pipeline
                # shellcheck disable=SC2086
                check "$stack" "$limit" "$n" depth_map.cml $pipeline
                # shellcheck disable=SC2086
                check "$stack" "$limit" "$n" depth_value.cml $pipeline
            done
        done
    done
done
echo "runs: $total, bad: $bad"
[ $bad -eq 0 ]
