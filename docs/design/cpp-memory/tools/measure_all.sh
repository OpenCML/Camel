#!/bin/bash
# The measurements of the report: times and peak memory of the emitted programs against the
# schedulers, what the collector did, and the hand-written root-keeping variants.
#   bash docs/design/cpp-memory/tools/measure_all.sh [perf] [memory] [roots]
# Run from the repository root. Every number depends on the load of the machine; `uptime` is
# printed with each section.
D=docs/design/cpp-memory; T=$D/tools; E=$D/examples; C=out/latest/bin/camel
export CPPMEM_OUT=${CPPMEM_OUT:-/tmp/cppmem}
R=${ROUNDS:-5}
M="python3 $T/measure.py"
WHAT=${*:-perf memory roots}
strip() { sed 's/\x1b\[[0-9;]*m//g'; }
has() { case " $WHAT " in *" $1 "*) return 0;; esac; return 1; }

if has perf; then
    echo "######## perf ($R rounds, median [min, max])"; uptime
    for s in a01_fib a07_fib_float a03_tail_loop a04_string_churn; do
        f=$E/accepted/$s.cml
        exe=$(bash $T/cppbuild.sh $f 2>&1 | tail -1)
        exe_inl=""
        CPPMEM_OUT=$CPPMEM_OUT/inl bash $T/cppbuild.sh $f std::opt::inline >/dev/null 2>&1 && exe_inl=$CPPMEM_OUT/inl/$s
        echo "---- $s"
        $M --rounds $R --label "std::nvm" -- $C $f std::nvm
        $M --rounds $R --label "std::fvm" -- $C $f std::fvm
        $M --rounds $R --label "std::opt::inline std::fvm" -- $C $f std::opt::inline std::fvm
        $M --rounds $R --label "std::jit" -- $C $f std::jit
        $M --rounds $R --label "std::opt::inline std::jit" -- $C $f std::opt::inline std::jit
        [ -x "$exe" ] && $M --rounds $R --label "std::cpp, clang -O2" -- $exe
        [ -n "$exe_inl" ] && [ -x "$exe_inl" ] && $M --rounds $R --label "std::opt::inline std::cpp, clang -O2" -- $exe_inl
    done
    echo "---- a01_fib with the scalar operators written as C++ operators (examples/handwritten/fib_direct.cpp)"
    direct=$(bash $T/cppbuild.sh $E/handwritten/fib_direct.cpp 2>&1 | tail -1)
    [ -x "$direct" ] && $M --rounds $R --label "hand-edited: operators inline" -- $direct
    echo "---- compile time of the emitted C++ (a01_fib)"
    $C $E/accepted/a01_fib.cml std::cpp > $CPPMEM_OUT/a01_fib.cpp
    $M --rounds 1 --label "clang++ -O2 a01_fib.cpp" -- bash $T/cppbuild.sh $CPPMEM_OUT/a01_fib.cpp
    wc -l $CPPMEM_OUT/a01_fib.cpp
    ls -la build/Release/libcamel.so $CPPMEM_OUT/a01_fib | awk '{print $5, $9}'
    echo "---- what the emitted executable needs at run time"; ldd $CPPMEM_OUT/a01_fib | sed 's/(0x[0-9a-f]*)//' | head -20
fi

if has memory; then
    echo "######## memory: a04_string_churn, two million short-lived strings"; uptime
    f=$E/accepted/a04_string_churn.cml
    exe=$(bash $T/cppbuild.sh $f 2>&1 | tail -1)
    for gc in mark-sweep generational; do
        echo "---- CAMEL_GC=$gc"
        CAMEL_GC=$gc $M --rounds 3 --label "std::fvm" -- $C $f std::fvm
        CAMEL_GC=$gc $M --rounds 3 --label "std::cpp" -- $exe
        echo "[std::fvm stats]"; CAMEL_GC=$gc CAMEL_GC_STATS=1 $C $f std::fvm 2>&1 | strip | cut -c1-700
        echo "[std::cpp stats]"; CAMEL_GC=$gc CAMEL_GC_STATS=1 $exe 2>&1 | strip | cut -c1-700
    done
    echo "---- a heap limit of 64 MB"
    echo "[std::fvm]"; CAMEL_GC_HEAP_LIMIT_MB=64 timeout 120 $C $f std::fvm 2>&1 | strip | cut -c1-300 | head -4; echo "   exit=${PIPESTATUS[0]}"
    echo "[std::cpp]"; CAMEL_GC_HEAP_LIMIT_MB=64 timeout 120 $exe 2>&1 | strip | cut -c1-300 | head -4; echo "   exit=${PIPESTATUS[0]}"
    echo "---- stress and poison settings change nothing in the emitted program (no safepoint is reached)"
    for env in "CAMEL_GC_POISON=1" "CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=major" "CAMEL_GC=generational CAMEL_GC_VERIFY=both CAMEL_GC_STRESS_SAFEPOINT=1 CAMEL_GC_STRESS_MODE=minor"; do
        echo "[$env] std::cpp a02_strings"; exe2=$(bash $T/cppbuild.sh $E/accepted/a02_strings.cml 2>&1 | tail -1)
        env $env CAMEL_GC_STATS=1 timeout 120 $exe2 2>&1 | strip | cut -c1-400 | head -4
    done
fi

if has roots; then
    echo "######## hand-written root keeping (examples/handwritten/roots.cpp)"; uptime
    exe=$(bash $T/cppbuild.sh $E/handwritten/roots.cpp 2>&1 | tail -1); echo "build: $exe"
    clang++ -std=c++23 -O2 $E/handwritten/rc.cpp -o $CPPMEM_OUT/rc && echo "build: $CPPMEM_OUT/rc"
    echo "---- soundness"; bash $T/poll_demo.sh
    for w in loop calls; do
        echo "---- workload $w: time and memory, n = 2,000,000 ($R rounds)"
        for gc in mark-sweep generational; do
            for mode in raw raw-poll shadow handle; do
                [ $mode = raw-poll ] && [ $gc = generational ] && continue
                CAMEL_GC=$gc $M --rounds $R --label "$w $mode ($gc)" -- $exe $w $mode 2000000
            done
        done
        $M --rounds $R --label "$w rc (no runtime)" -- $CPPMEM_OUT/rc $w rc 2000000
        $M --rounds $R --label "$w value (no runtime)" -- $CPPMEM_OUT/rc $w value 2000000
    done
fi

