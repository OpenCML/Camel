#!/bin/bash
# Runs every example of one directory and prints what each gives.
#   bash docs/design/graph-model/tools/run_examples.sh <dir> [passes...]
# For each *.cml with a `main`: the exit code, then stdout and stderr (first lines).
dir=${1:?directory}; shift
for f in "$dir"/*.cml; do
    grep -q 'func main' "$f" || continue
    echo "=== $f $*"
    timeout 60 camel "$f" "$@" > /tmp/gm_out.txt 2> /tmp/gm_err.txt
    echo "exit=$?"
    head -${GM_LINES:-12} /tmp/gm_out.txt
    sed 's/\x1b\[[0-9;]*m//g' /tmp/gm_err.txt | head -${GM_LINES:-12}
done
