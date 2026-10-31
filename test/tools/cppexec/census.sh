#!/usr/bin/env bash
# Counts the programs of the corpus the direct C++ writer answers for: one run of
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
#
# --- the four cells -----------------------------------------------------------
# A refusal is one answer, not the only one. The ruler's other reading -- `run.py <directory>`,
# whose `differs` is "translated, built, run, and not the same output as std::fvm" -- gives the
# whole answer per program, and this script runs it over the corpus so the four readings are one
# run of one command:
#
#   same            translated, built, and the same output as std::fvm
#   refused         std::cpp refused the program (TranslationRefused)
#   differs         translated, built and run, but not the output std::fvm prints
#   does not build  the C++ compiler rejected what std::cpp wrote
#
# What the compiler says is a fault of the translation: a program the writer does not compile is a
# defect of the writer, and it is counted with `differs` -- never with `refused`. Counting a defect
# as a refusal lets the ruler excuse the translator: `does not build` and `differs` are the two
# cells a reader can fix, and they are reported first, with the compiler's own first line for each.
#
# These programs are NOT COMPARABLE and are named here, not skipped in silence: `run.py` reads them
# as differences of the translation, and they are not one:
#
#   linear/nvm/acc.cml                       prints the wall-clock seconds it measured
#   linear/nvm/fib.cml                       prints the wall-clock seconds it measured
#   perf/fib_recursive.cml                   prints the wall-clock seconds it measured
#   linear/jit/perf_fib_smoke.cml            prints the wall-clock seconds it measured
#   para/core/monte_carlo_pi.cml             prints the seconds the measurement took
#   para/core/unordered_foreach_timing.cml   prints the seconds the measurement took
#   modules/std/time/basic.cml               prints the current time (`now`)
#   consistency/generic_values/candidate_body_fails.cml
#                                            a fixture for the pass: `std::fvm` itself stops with
#                                            PassInternalError, so there is no reading of the
#                                            program to compare a translation against
#   examples/perf/fibs.cml                   prints the wall-clock seconds it measured
#
# A `differs` count over the corpus has to leave these out, or it counts the clock and a
# broken reference as defects of the writer. Two ways to close a hole in a ruler: make the case
# print nothing that moves, or write the hole down. The measurements print the measurement --
# that is what they are for -- so the hole is written down here, once, with the reason.
not_comparable=(
    test/cases/linear/nvm/acc.cml
    test/cases/linear/nvm/fib.cml
    test/cases/perf/fib_recursive.cml
    test/cases/linear/jit/perf_fib_smoke.cml
    test/cases/para/core/monte_carlo_pi.cml
    test/cases/para/core/unordered_foreach_timing.cml
    test/cases/modules/std/time/basic.cml
    test/cases/consistency/generic_values/candidate_body_fails.cml
    examples/perf/fibs.cml
)
# The corpus the four cells are read over. `CORPORA` overrides it (a space-separated list).
CORPORA=${CORPORA:-"test/cases benchmarks examples exemplars"}
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
echo "==== not comparable (named above; a differs count must leave these out) ===="
printf '%s\n' "${not_comparable[@]}"
printf '%s\n' "${not_comparable[@]}" > "$out/not_comparable.txt"

echo "==== the four cells, over: $CORPORA ===="
# The comparison is `run.py`'s: this script does not re-decide what `same` means, it counts the
# cells run.py answered and leaves the named programs out of `differs`.
python3 test/tools/cppexec/run.py --jobs "$JOBS" --timeout "${TIMEOUT:-60}" \
    --json "$out/records.json" $CORPORA > "$out/four.txt" 2>&1
four_rc=$?
echo "run.py exit: $four_rc   (its own report: $out/four.txt)"
python3 - "$out/records.json" "$out/not_comparable.txt" <<'PY'
import collections, json, sys

records = json.load(open(sys.argv[1], encoding="utf-8"))
named = {line.split()[0] for line in open(sys.argv[2], encoding="utf-8") if line.strip()}
counts = collections.Counter()
differs, builds, others = [], [], []
for rec in records:
    program = rec["program"].split(" ")[0]
    outcome = rec["outcome"].split(":")[0]
    if program in named:
        counts["not comparable"] += 1
        continue
    counts[outcome] += 1
    if outcome == "differs":
        differs.append((program, rec.get("detail", "")))
    elif outcome == "does not build":
        builds.append((program, rec.get("detail", "")))
    elif outcome not in ("same", "refused", "not a program"):
        others.append((program, rec["outcome"], rec.get("detail", "")))
total = len(records)
print("programs: %d   (compared: %d, not comparable: %d)"
      % (total, total - counts["not comparable"], counts["not comparable"]))
print("same:           %d" % counts["same"])
print("refused:        %d" % counts["refused"])
print("differs:        %d" % counts["differs"])
print("does not build: %d" % counts["does not build"])
print("defect (differs + does not build): %d" % (counts["differs"] + counts["does not build"]))
for name, n in sorted(counts.items()):
    if name not in ("same", "refused", "differs", "does not build", "not comparable"):
        print("%-15s %d" % (name + ":", n))
for title, rows in (("does not build", builds), ("differs", differs)):
    if rows:
        print("==== %s (%d) ====" % (title, len(rows)))
        for program, detail in rows:
            print("  %s: %s" % (program, detail))
for program, outcome, detail in others:
    print("  %s: %s: %s" % (program, outcome, detail))
PY

if [ $# -ge 1 ]; then
    echo "==== first operand type, per program whose operator matches: $1 ===="
    grep '^OP|' "$out/refused.txt" | grep -E "^OP\|$1\|" | cut -d'|' -f3 | while read -r f; do
        t=$("$CAMEL" "$f" std::cpp 2>&1 | grep -m1 -o "of type '[^']*'" | sed "s/^of type //")
        printf '%s\n' "${t:-(no type named)}"
    done | sort | uniq -c | sort -rn
fi