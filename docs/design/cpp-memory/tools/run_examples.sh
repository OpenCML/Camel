#!/bin/bash
# Run every .cml of a directory through a pass list and print exit code and output.
#   bash docs/design/cpp-memory/tools/run_examples.sh <dir> [passes ...]
set -u
DIR=${1:?directory}; shift
CAMEL=${CAMEL:-out/latest/bin/camel}
for f in "$DIR"/*.cml; do
    echo "== $f $*"
    timeout ${EX_TIMEOUT:-60} "$CAMEL" "$f" "$@" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-${EX_WIDTH:-400} | head -${EX_LINES:-30}
    echo "   exit=${PIPESTATUS[0]}"
done
