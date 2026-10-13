#!/bin/bash
# The hand-written demonstration of section 2.3: a raw pointer across a safepoint, and the two
# ways of telling the collector about it.
#   bash docs/design/cpp-memory/tools/poll_demo.sh [n]
# Prints, per workload, mode and collector setting, the first lines of output and the exit code.
# The right answers for the default n = 2000000:
#   keep   the string kept from the start / 22888896
#   loop   22888896
#   calls  15568000
D=docs/design/cpp-memory; T=$D/tools; E=$D/examples
N=${1:-2000000}
export CPPMEM_OUT=${CPPMEM_OUT:-/tmp/cppmem}
exe=$(bash $T/cppbuild.sh $E/handwritten/roots.cpp 2>&1 | tail -1)
for w in keep loop calls; do
    for mode in raw raw-poll shadow handle; do
        for env in "CAMEL_GC=mark-sweep" "CAMEL_GC=generational" "CAMEL_GC_POISON=1" \
                   "CAMEL_GC=mark-sweep CAMEL_GC_STRESS_SAFEPOINT=1000 CAMEL_GC_STRESS_MODE=major"; do
            env $env timeout 300 $exe $w $mode $N > $CPPMEM_OUT/demo.out 2>&1
            rc=$?
            out=$(sed 's/\x1b\[[0-9;]*m//g' $CPPMEM_OUT/demo.out | tr -c '[:print:]\n' '?' | head -2 | tr '\n' ' ' | cut -c1-110)
            printf '%-5s %-9s %-80s %s (exit %s)\n' $w $mode "[$env]" "$out" "$rc"
        done
    done
done
