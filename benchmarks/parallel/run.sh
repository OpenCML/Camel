#!/bin/bash
# Runs the parallel-scheduler cases on std::fvm, std::nvm and std::prl (1 thread, 4 threads, the
# default number of threads) with prl_bench.py, and prints one table per case.
#
#   benchmarks/parallel/run.sh [case ...]         # from the repository root; all cases by default
#
# Environment:
#   REPS=5                    rounds per case (configurations are interleaved within a round)
#   BUILDS="new=$CAMEL_HOME"  one or two builds, "name=CAMEL_HOME name=CAMEL_HOME"; with two,
#                             the table gives the ratio of the first to the second
#   CONFIGS="..."             overrides the configurations (prl_bench.py --config syntax)
#   OUT=tmp/prl_bench         directory for the per-case JSON (every sample); with RESUME=1 the
#                             rounds are added to what is already there
#
# The load average of the machine is recorded for every run and printed in the header of each
# table; on a busy machine only the comparison within a table means anything.
set -u
here=$(cd "$(dirname "$0")" && pwd)
reps=${REPS:-5}
out=${OUT:-tmp/prl_bench}
builds=${BUILDS:-new=${CAMEL_HOME:?CAMEL_HOME is not set}}
configs=${CONFIGS:-std::fvm std::nvm std::prl:1,4 std::prl}
mkdir -p "$out"

cases=("$@")
if [ ${#cases[@]} -eq 0 ]; then
    for f in "$here"/cases/*.cml; do cases+=("$(basename "$f" .cml)"); done
fi

status=0
for name in "${cases[@]}"; do
    file="$here/cases/$name.cml"
    [ -f "$file" ] || { echo "no such case: $name" >&2; status=2; continue; }
    args=()
    for b in $builds; do args+=(--build "$b"); done
    for c in $configs; do
        # The first line of a case may name passes that must run before the scheduler.
        pre=$(sed -n 's|^// *passes: *||p' "$file" | head -1)
        if [ -n "$pre" ]; then c="${pre// /+}+$c"; fi
        args+=(--config "$c")
    done
    [ -n "${RESUME:-}" ] && args+=(--resume)
    echo "## $name  ($(date -u +%FT%TZ), loadavg $(cut -d' ' -f1-3 /proc/loadavg))"
    python3 "$here/prl_bench.py" "${args[@]}" --reps "$reps" --quiet --timeout "${TIMEOUT:-120}" \
        --json "$out/$name.json" "$file" || status=$?
    echo
done
exit $status
