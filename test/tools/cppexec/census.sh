#!/usr/bin/env bash
# Counts the programs of test/cases that the direct C++ writer refuses: one run of
# `camel <program> std::cpp` per program, and a program counts as refused when its output carries
# a TranslationRefused diagnostic -- one sentence, whatever the wording and whoever it names.
#
# The earlier count read two wordings of that diagnostic and missed the rest (the type-level
# refusals, and "on values ... does not translate it for yet"), which understated the number by
# about a third: the readings it produced are a trend, not a total. This script is the ruler the
# page cites.
#
#   CAMEL_HOME=out/latest bash test/tools/cppexec/census.sh
#
# A second argument-shaped form answers "why are these refused": give it an operator pattern and it
# re-reads those programs and counts the type their first refusal names, which is the operand the
# rule turned down.
#
#   CAMEL_HOME=out/latest bash test/tools/cppexec/census.sh ':op/slice_sub|:op/idx_arr'
#
# `CAMEL` overrides the binary (default: $CAMEL_HOME/bin/camel), `JOBS` the width (default 8).
set -u
root=$(cd "$(dirname "$0")/../../.." && pwd)
export CAMEL_HOME=${CAMEL_HOME:-$root/out/latest}
CAMEL=${CAMEL:-$CAMEL_HOME/bin/camel}
JOBS=${JOBS:-8}
cd "$root" || exit 1
out=$(mktemp -d)
find test/cases -name '*.cml' | sort > "$out/all.txt"
echo "programs: $(wc -l < "$out/all.txt")"

one() {
    f=$1
    key=$(printf '%s' "$f" | md5sum | cut -c1-10)
    "$CAMEL" "$f" std::cpp > "$out/o.$key" 2>&1
    rc=$?
    if grep -q "TranslationRefused" "$out/o.$key"; then
        op=$(sed -n "s/.*uses the operator '\([^']*\)'.*/\1/p" "$out/o.$key" | head -1)
        [ -z "$op" ] && op="(no operator named)"
        echo "OP|$op|$f" >> "$out/refused.txt"
    elif [ "$rc" -ge 128 ]; then
        echo "CRASH|rc=$rc|$f" >> "$out/refused.txt"
    fi
    rm -f "$out/o.$key"
}
export -f one
export out CAMEL

cat "$out/all.txt" | xargs -P "$JOBS" -I{} bash -c 'one "$@"' _ {}

echo "refused: $(cut -d'|' -f3 "$out/refused.txt" 2>/dev/null | sort -u | wc -l)"
echo "==== by operator (count) ===="
grep '^OP|' "$out/refused.txt" | cut -d'|' -f2 | sort | uniq -c | sort -rn
echo "==== crashes ===="
grep -c '^CRASH|' "$out/refused.txt" || true
echo "==== the list itself (op|program) ===="
cat "$out/refused.txt" 2>/dev/null || true
echo "==== the file it is in: $out/refused.txt ====" >&2

if [ $# -ge 1 ]; then
    echo "==== first operand type, per program whose operator matches: $1 ===="
    grep '^OP|' "$out/refused.txt" | grep -E "^OP\|$1\|" | cut -d'|' -f3 | while read -r f; do
        t=$("$CAMEL" "$f" std::cpp 2>&1 | grep -m1 -o "of type '[^']*'" | sed "s/^of type //")
        printf '%s\n' "${t:-(no type named)}"
    done | sort | uniq -c | sort -rn
fi