#!/usr/bin/env bash
# Learnability evaluation: compile one round of learned solutions where Camel is built.
#
# The Windows checkout this harness is driven from never builds Camel, so the compiler lives on the
# build host and this script is what the round is compiled with. It takes a directory of `<id>.cml`
# files, one per task, compiles each, and prints one delimited block per task on stdout so the
# caller can read the reading (exit code, stdout, stderr) back without guessing.
#
#   CAMEL_BIN=... LD_LIBRARY_PATH=... bash remote-compile.sh <round-dir>
#
# `<id>.mode` in the directory selects what a pass means: `run` (default) runs the program, and
# `check-and-run` also runs `camel check` and reports it separately, for a task whose acceptance
# includes the code health report.
#
# Each task prints:
#   @@@TASK <id>
#   @@@EXIT <code>
#   @@@STDOUT / @@@STDERR   (the delimited text, verbatim)
#   @@@CHECK_EXIT / @@@CHECK_STDOUT / @@@CHECK_STDERR   (only under check-and-run)
#   @@@END
set -u

DIR="${1:?usage: remote-compile.sh <round-dir>}"
CAMEL_BIN="${CAMEL_BIN:-$HOME/camel-work/h425-ai-reference/out/latest/bin/camel}"
if [ -z "${CAMEL_HOME:-}" ]; then
    CAMEL_HOME="$(cd "$(dirname "$CAMEL_BIN")/../.." && pwd)"
fi
export CAMEL_HOME
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-$HOME/camel-eval/conda/envs/clang/lib}"

cd "$DIR" || exit 2

for f in *.cml; do
    [ -e "$f" ] || continue
    id="${f%.cml}"
    mode=run
    [ -f "$id.mode" ] && mode="$(cat "$id.mode")"
    # Every value is closed by its own newline before the next marker: a program that prints without a
# trailing newline (`print('x')`) would otherwise run its stdout into `@@@STDERR` and the reading
# would be taken from the wrong field. The trailing newline is added to the value, and the
# comparison trims it, so the compiler's bytes are still what is judged.
    echo "@@@TASK $id"
    "$CAMEL_BIN" "$f" --output-format json > "$id.stdout" 2> "$id.stderr"
    code=$?
    echo "@@@EXIT"
    echo "$code"
    echo "@@@STDOUT"
    cat "$id.stdout"; echo
    echo "@@@STDERR"
    cat "$id.stderr"; echo
    if [ "$mode" = "check-and-run" ]; then
        "$CAMEL_BIN" check "$f" > "$id.check.stdout" 2> "$id.check.stderr"
        check=$?
        echo "@@@CHECK_EXIT"
        echo "$check"
        echo "@@@CHECK_STDOUT"
        cat "$id.check.stdout"; echo
        echo "@@@CHECK_STDERR"
        cat "$id.check.stderr"; echo
    fi
    echo "@@@END"
done