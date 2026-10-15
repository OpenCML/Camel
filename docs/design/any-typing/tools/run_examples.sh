#!/bin/bash
# run_examples.sh [backend]      (default std::fvm)
# Runs every example of docs/design/any-typing/examples once and prints the first lines of its
# output and its exit code. Programs whose main takes `n` get --arg n=1000 (no collection runs;
# the collector experiments are gc_matrix.sh). Run from the repository root.
B=${1:-std::fvm}
D=docs/design/any-typing
CAMEL=${CAMEL:-$( [ -x out/latest/bin/camel ] && echo out/latest/bin/camel || echo camel )}
ulimit -c 0
for f in $D/examples/*.cml; do
    args=()
    grep -q '^func main(n: int)' "$f" && args=(--arg n=1000)
    echo "== $(basename "$f") $B"
    out=$(timeout 30 "$CAMEL" "$f" "${args[@]}" $B 2>&1); rc=$?
    printf '%s\n' "$out" | sed 's/\x1b\[[0-9;]*m//g' | grep -v '^$' | grep -v '^help:' | head -${LINES_MAX:-6}
    echo "exit=$rc"
done
