#!/bin/bash
# What std::fvm prints for a program, and what the program std::cpp writes for it prints.
#   bash docs/design/cpp-memory/tools/compare.sh <file.cml | directory> [passes before the last pass ...]
# For a directory, every .cml in it. Run from the repository root.
set -u
T=$(dirname "$0"); C=${CAMEL:-out/latest/bin/camel}
export CPPMEM_OUT=${CPPMEM_OUT:-/tmp/cppmem}
strip() { sed 's/\x1b\[[0-9;]*m//g'; }
one() {
    f=$1; shift; s=$(basename "$f" .cml)
    echo "==== $f $*"
    echo "[std::fvm]"
    timeout ${EX_TIMEOUT:-120} "$C" "$f" "$@" std::fvm 2>&1 | strip | cut -c1-240 | head -${EX_LINES:-8}; echo "   exit=${PIPESTATUS[0]}"
    rm -f "$CPPMEM_OUT/$s"
    build=$(bash "$T/cppbuild.sh" "$f" "$@" 2>&1 | strip)
    if [ -x "$CPPMEM_OUT/$s" ]; then
        echo "[std::cpp, built and run]"
        timeout ${EX_TIMEOUT:-120} "$CPPMEM_OUT/$s" 2>&1 | strip | cut -c1-240 | head -${EX_LINES:-8}; echo "   exit=${PIPESTATUS[0]}"
    else
        echo "[std::cpp: no executable]"
        echo "$build" | grep -E "error|refused|failed" | cut -c1-300 | head -4
    fi
}
target=${1:?file or directory}; shift
if [ -d "$target" ]; then for f in "$target"/*.cml; do one "$f" "$@"; done; else one "$target" "$@"; fi
