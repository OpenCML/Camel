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
# The refusal pass reads the same corpus the four cells do: a `refused` number that counts one of
# the four directories and a breakdown that counts another is two rulers wearing one name.
find $CORPORA -name '*.cml' | sort > "$out/all.txt"
: > "$out/reasons.txt"
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
        # The reason itself, word for word: the classes below are its first sentence's shape, so
        # the ruler has to carry the sentence and not only the operator the sentence happened to
        # name.
        msg=$(sed -n "s/^error\[RuntimeDiag::TranslationRefused\]: Translation pass 'std::cpp' refused the graph: //p" "$out/o.$key" | head -1 | tr -d '\r')
        printf 'M|%s|%s\n' "$f" "$msg" >> "$out/reasons.txt"
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
# ---- the refused cell, split into the two piles a refused program can be in ---------------
#
# `refused` is one cell but not one answer: a program is refused either because the target cannot
# have what it asks for -- a shaped array, a Device, an exported model, the block space of the
# runtime, a module's own kernel -- or because this writer has not written the rule yet, and the
# second is a defect. "拒绝的销账不是把话说得更客气，是把每一条分成'该拒'和'该修'两堆。" The
# classes below are the refusal sentences' own shapes (the first sentence, with the names and types
# it carries taken out), one class per shape, and each class is judged by one question: does the
# language require this target to have it?
#
# One class is judged so far: an operator that has no rule for its operand type. Its reason names
# the operator, so the split there is the operator's: the operators whose values do not exist in a
# standalone C++ program at all (`tensor:`, `Device<>` through `tiles:`, a differentiated function,
# an exported model, a module's own kernel table) are the target's boundary, and the ones that run
# on a value the target does carry (`array:sort_l`, `array:sum_l` and the like on `int[]`) are the
# writer's gap. The classes not judged yet are printed unjudged, not folded into either pile.
echo "==== refused, by the first sentence of the reason (count | class | one program) ===="
python3 - "$out/reasons.txt" <<'PY'
import collections
import re
import sys

rows = [l.rstrip("\n").split("|", 2) for l in open(sys.argv[1], encoding="utf-8") if l.strip()]

CLASSES = [
    ("no entry: no call of 'main'", r"^the program has no call of .main. std::cpp can find"),
    ("an operator that has no rule for its operand type", r"uses the operator .[^']*. on a value of type .*which has no translation in std::cpp yet"),
    ("an operator that has no rule for its result type", r"uses the operator .[^']*., whose result of type .*has no translation in std::cpp yet"),
    ("an operator that has no rule for several value types", r"uses the operator .[^']*. on values .*std::cpp does not translate it for yet"),
    ("a declared type the writer does not carry", r"std::cpp translates numbers \(real and complex\)"),
    ("a function value the writer does not carry", r"makes a function value of type .*std::cpp does not translate yet"),
    ("a conversion the writer does not carry", r"converts a value of type .*which std::cpp does not translate yet"),
    ("a draw into a shaped array", r"fill[s]? a shaped array"),
    ("the block counters of the runtime", r"reads the block counters"),
    ("a value whose rank is open", r"walks a value whose rank is open"),
    ("a callee that writes the captured variables", r"writes captured variables"),
    ("a record indexed by a computed name", r"indexes a struct by a name the program computes"),
]


def first(message):
    message = message.strip()
    cut = message.find(". ")
    return message[: cut + 1] if cut >= 0 else message


def classify(message):
    text = message.strip()
    for name, pattern in CLASSES:
        if re.search(pattern, text):
            return name
    return "other: " + first(text)[:60]


counts = collections.Counter()
example = {}
for tag, program, message in rows:
    name = classify(message)
    counts[name] += 1
    example.setdefault(name, (program, first(message)))

for name, n in counts.most_common():
    program, sentence = example[name]
    print("%4d  %-52s %s" % (n, name, program))
    print("      verbatim: %s" % sentence[:150])

# The operators whose value does not exist in a standalone C++ program at all: a shaped array
# (`tensor:`), a Device (`tiles:`), a differentiated function (`autodiff:`), an exported model
# (`onnx:`), a module's own kernel table (`nn:`, `ckpt:`), a plot (`pyplot:`), the module's type
# table (`json:`), and the operators of the test modules the cases declare for themselves.
BOUNDARY = (
    "tensor:", "autodiff:", "tiles:", "nn:", "onnx:", "ckpt:", "json:", "pyplot:",
    "test_attrs:", "test_support:", "device:",
)
judged = "an operator that has no rule for its operand type"
boundary = collections.Counter()
gap = collections.Counter()
for tag, program, message in rows:
    if classify(message) != judged:
        continue
    found = re.search(r"uses the operator '([^']*)'", message)
    op = found.group(1) if found else "(none)"
    (boundary if op.startswith(BOUNDARY) else gap)[op] += 1
print()
print("==== %d  %s -- judged ====" % (counts[judged], judged))
print("  the target does not have it: %d" % sum(boundary.values()))
for op, n in boundary.most_common():
    print("    %4d  %s" % (n, op))
print("  to fix (the target carries these values): %d" % sum(gap.values()))
for op, n in gap.most_common():
    print("    %4d  %s" % (n, op))
PY
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