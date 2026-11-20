#!/bin/sh
# capture-all.sh -- every reading the ECOOP paper's section 6 and the e2e lane quote, in one run.
#
# Why one script: the paper presents ONE build, and today's readings are spread over several
# (`2d7c5c3a1` for the pin, the optimization table and the fusion, `02939ffb7` for the e2e). So the
# readings are gathered into one command that names the build it ran on and prints the command it
# ran, and the whole set is re-taken on the build a reader is handed.
#
# What it is NOT: a new measurement definition. Every block below is the command that produced the
# number in the report named at its head; nothing here changes an argument to make a number look
# better, and no file under src/, no expected value and no pass is touched. The nine blocks are the
# nine readings the dispatch of 2026-11-20 01:10 lists, in that order:
#
#   [1] the six writings of the case-study program, in the library's `grad` form, and the counterexample
#   [2] the scale of train.cml: graphs, nodes, edges, regions, bytes
#   [3] the three backends' output on the training program, and that they agree byte for byte
#   [4] the rows of tab:lang: the two prints in `main`, the print in `loss`, the deleted prints,
#       and `break_rate` (the edge-type check refusing the wrong value in the learning-rate slot)
#   [5] the optimization table on the MLP and on the char-transformer, and, beside its
#       accept/refuse column, what the paper's table asks for: the nodes before and after each
#       pass, the regions counted by kind (the `loop_body` column is the answer to "is LoopBody
#       > 0"), the same four passes on the case-study program whole, the median of nine fresh
#       processes per pass, and the transformer's output compared byte for byte
#   [6] the fusion: the strict writings reading, then loss-a and loss-b fused
#   [7] commit and normalization time, and what one commit costs against the size of the program
#       (the case, the MLP, the transformer and two bigger ones) and with one node changed on the
#       transformer (the floating blocks, see their heads; [5] floats now too)
#   [8] the two derivative graphs, loss-a against loss-b, and the SECOND derivative on the four
#       backends, which this build owes and does not have: that block says what it waits for
#   [9] the e2e lane, which is e2e/capture.sh run unchanged, so the two never drift apart
#
# [5], [7] and [8] each carry one reading more than their head says: the three the dispatch of
# 2026-11-20 16:10 adds -- items 6 and 14 of the author's list, and item 5's placeholder. The three
# go where their subject already is rather than into new blocks, so a reader of the paper's table
# finds the node counts and the times in the section that table is drawn from.
#
# Run it from anywhere; it moves to the worktree root itself:
#
#   sh docs/handover/reports/eval-round10/capture-all.sh \
#       > docs/handover/reports/eval-round10/capture-all.raw.txt 2>&1
#
# Repeatability: two runs of this script on one build differ only in the parts that are time --
# section [7] and the timing block that closes section [5] -- each of which is wall and CPU time
# plus the host's load line beside it, because a time with no load beside it cannot be read.
# Everything else is byte for byte the same: the MLP's
# own `seconds=` field is masked below because every run moves it, and the scratch directory is a
# fixed path rather than a fresh temp one because a diagnostic quotes it -- a path that changed per
# run would make two runs differ. PYTHONHASHSEED is pinned so tools that walk sets print in one order.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
cd "$ROOT" || exit 1

SCRATCH=/tmp/camel-capture-all
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || exit 1

export CAMEL_HOME="$ROOT/out/latest"
export LD_LIBRARY_PATH="$HOME/camel-eval/conda/envs/clang/lib:$CAMEL_HOME/libs:$CAMEL_HOME/lib"
export PYTHONHASHSEED=0
CAMEL="$CAMEL_HOME/bin/camel"

VARIANTS=docs/handover/reports/eval-round10/case-study-variants
PROBES=docs/handover/reports/eval-round10/probes
PROBES_ROOT=$ROOT/$PROBES
E2E=docs/handover/reports/eval-round10/e2e
MLP=showcase/c2-mlp-mnist/train.cml
TRANSFORMER=showcase/c3-char-transformer/train.cml
CASE=examples/case-study/train.cml

BUILD=$(git -C "$ROOT" rev-parse --short=9 HEAD)

banner() {
    echo
    echo "=== [$1] $2 ==="
    echo "# build=$BUILD"
}

# The last component of a pass name, or `baseline` for no pass: a tag for the scratch files.
leaf() {
    if [ -z "$1" ]; then
        echo baseline
    else
        echo "$1" | sed 's/.*:://'
    fi
}

# A command whose whole output is the reading.
run() {
    echo "### camel $*"
    "$CAMEL" "$@" 2>&1
    echo "### exit=$?"
    echo
}

runp() {
    pkg=$1
    shift
    echo "### CAMEL_PACKAGES=$pkg camel $*"
    CAMEL_PACKAGES="$pkg" "$CAMEL" "$@" 2>&1
    echo "### exit=$?"
    echo
}

# The same, but the output is kept as well, because a later line compares it with another command's.
runkept() {
    tag=$1
    shift
    echo "### camel $*"
    "$CAMEL" "$@" > "$SCRATCH/$tag.out" 2>&1
    st=$?
    sed -E 's/seconds=[0-9.]+/seconds=<masked>/g' "$SCRATCH/$tag.out" > "$SCRATCH/$tag.masked"
    echo "### exit=$st"
    cat "$SCRATCH/$tag.masked"
    echo
}

# What is printed about a canonical document: its exit code, byte count and both digests, plus
# whatever went to stderr. The document itself stays in the scratch directory for the comparisons.
doc_line() {
    tag=$1
    st=$2
    echo "doc=$tag exit=$st bytes=$(wc -c < "$SCRATCH/$tag.json") md5=$(md5sum < "$SCRATCH/$tag.json" | cut -d' ' -f1) sha256=$(sha256sum < "$SCRATCH/$tag.json" | cut -d' ' -f1)"
    if [ -s "$SCRATCH/$tag.err" ]; then
        echo "--- stderr ($tag)"
        cat "$SCRATCH/$tag.err"
        echo "--- end stderr ($tag)"
    fi
    echo
}

doc() {
    tag=$1
    shift
    echo "### camel $* > $tag.json"
    "$CAMEL" "$@" > "$SCRATCH/$tag.json" 2> "$SCRATCH/$tag.err"
    st=$?
    doc_line "$tag" "$st"
}

docp() {
    pkg=$1
    tag=$2
    shift 2
    echo "### CAMEL_PACKAGES=$pkg camel $* > $tag.json"
    CAMEL_PACKAGES="$pkg" "$CAMEL" "$@" > "$SCRATCH/$tag.json" 2> "$SCRATCH/$tag.err"
    st=$?
    doc_line "$tag" "$st"
}

# The first byte at which two files part, or `same`.
first_diff() {
    off=$(cmp -l "$1" "$2" 2>/dev/null | head -1 | awk '{print $1}')
    if [ -z "$off" ]; then
        echo "same"
    else
        echo "differs first_difference_byte=$off"
    fi
}

# The node count of a canonical document: the sum of `graphs[*].nodes`, the same count section [2]
# defines. It is a count of the document, so it is a determinate number and not a sample. A document
# that is not there -- a program that did not compile, a run that wrote nothing -- answers `?` and
# not 0, because a count of nothing and a count that failed are different readings.
nodes_of() {
    python3 -c 'import json,sys
d = json.load(open(sys.argv[1]))
sys.stdout.write(str(sum(len(g.get("nodes", [])) for g in d["graphs"])))' "$1" 2>/dev/null || printf '?'
}

# One pass, nine fresh processes, and the median of the pass window itself.
#
# The reading is the `pass` phase's own entry from --time-phases (`name=pass`, `detail=<the pass as
# the pipeline spelled it>`), so the terminator pass and the printing of the document are not in it.
# Wall follows the host's load and CPU time does not, which is why both are printed and why the
# caller prints the host's load line beside them: a time with no load beside it cannot be read.
#
# Every process's exit code is printed too. A pass that failed still leaves a `pass` phase in the
# file (the scope is entered before the pass runs), so without the exit code a failed window would
# read as a fast one -- the number would be real and would not be the pass.
#
#   $1 tag naming the phase files   $2 the program   $3 the pass
#   $4 the module roots, or `-` for none    $5 a note printed verbatim on the summary line
timed() {
    tag=$1
    prog=$2
    word=$3
    pkg=$4
    note=$5
    if [ "$pkg" = "-" ]; then
        echo "### camel $prog $word std::gir --time-phases=$SCRATCH/t-$tag-<n>.json   (nine times, a fresh process each)"
    else
        echo "### CAMEL_PACKAGES=$pkg camel $prog $word std::gir --time-phases=$SCRATCH/t-$tag-<n>.json   (nine times, a fresh process each)"
    fi
    i=1
    while [ "$i" -le 9 ]; do
        if [ "$pkg" = "-" ]; then
            "$CAMEL" "$prog" "$word" std::gir --time-phases="$SCRATCH/t-$tag-$i.json" \
                > "$SCRATCH/t-$tag-$i.out" 2>&1
        else
            CAMEL_PACKAGES="$pkg" "$CAMEL" "$prog" "$word" std::gir \
                --time-phases="$SCRATCH/t-$tag-$i.json" > "$SCRATCH/t-$tag-$i.out" 2>&1
        fi
        echo $? > "$SCRATCH/t-$tag-$i.st"
        i=$((i + 1))
    done
    python3 - "$tag" "$word" "$SCRATCH" "$note" <<'PY'
import json
import os
import statistics
import sys

tag, word, scratch, note = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
wall, cpu, failed = [], [], []
for i in range(1, 10):
    st_path = "%s/t-%s-%d.st" % (scratch, tag, i)
    ph_path = "%s/t-%s-%d.json" % (scratch, tag, i)
    st = open(st_path).read().strip() if os.path.exists(st_path) else "(no exit code)"
    if st != "0":
        failed.append("run%d exit=%s" % (i, st))
    hit = []
    if os.path.exists(ph_path):
        try:
            d = json.load(open(ph_path))
            hit = [p for p in d.get("phases", []) if p.get("name") == "pass" and p.get("detail") == word]
        except ValueError:
            hit = []
    if not hit:
        continue
    wall.append(hit[0]["wall_ns"] / 1e6)
    cpu.append(hit[0]["cpu_ns"] / 1e6)
if not wall:
    out = "%s/t-%s-1.out" % (scratch, tag)
    said = open(out).read().strip().splitlines() if os.path.exists(out) else []
    print("%-20s NO READING: no 'pass' phase named %s in any of the nine%s; its first line of "
          "output: %s"
          % (tag, word, (" (" + ", ".join(failed) + ")") if failed else "",
             said[0] if said else "(it printed nothing)"))
else:
    print("%-20s n=%d of 9  median_wall_ms=%8.2f [%8.2f, %8.2f]  median_cpu_ms=%8.2f  %s%s"
          % (tag, len(wall), statistics.median(wall), min(wall), max(wall),
             statistics.median(cpu), note,
             ("  failed: " + ", ".join(failed)) if failed else ""))
PY
}

echo "===== capture-all: the ECOOP section 6 readings and the e2e lane, one run ====="
echo "# the build every reading below ran on:"
echo "#   tree       $ROOT"
echo "#   git HEAD   $(git -C "$ROOT" rev-parse HEAD)"
echo "#   camel      $CAMEL"
python3 - "$CAMEL_HOME/build-info.json" <<'PY'
import json
import sys

d = json.load(open(sys.argv[1]))
fp = d.get("fingerprint", {})
opt = d.get("buildOptions", {})
print("#   %-12s %s" % ("config", d.get("config")))
print("#   %-12s %s" % ("revision", d.get("revision")))
print("#   %-12s %s" % ("dirty", "%s (%s path(s))" % (d.get("dirty"), d.get("dirtyCount"))))
print("#   %-12s %s" % ("builtAt", d.get("builtAt")))
print("#   %-12s %s" % ("footprint", d.get("footprint")))
print("#   %-12s %s of %s files under %s"
      % ("fingerprint", fp.get("hash"), fp.get("files"), ", ".join(fp.get("roots", []))))
print("#   %-12s %s platform %s" % ("buildOptions", opt.get("hash"), opt.get("platform")))
print("#   %-12s cmake %s" % ("", json.dumps(opt.get("cmake", {}), sort_keys=True)))
print("#   %-12s env   %s" % ("", json.dumps(opt.get("env", {}), sort_keys=True)))
PY
echo "#   version    $("$CAMEL" --version 2>&1)"
# The binary carries the sha of the sources it was linked from, inside its footprint; the reading
# is only reproducible if this tree is those sources, so the difference is printed.
FPSHA=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["footprint"].split("_")[-1])' "$CAMEL_HOME/build-info.json" 2>/dev/null)
if [ -n "$FPSHA" ]; then
    CHANGED=$(git -C "$ROOT" diff --name-only "$FPSHA" HEAD 2>/dev/null)
    if [ -z "$CHANGED" ]; then
        echo "#   linked at  $FPSHA, and this tree is exactly those sources"
    else
        echo "#   linked at  $FPSHA; the files this tree has on top of it (the binary does not include"
        echo "#             them, so no reading below depends on them):"
        printf '%s\n' "$CHANGED" | sed 's/^/#               /'
    fi
fi
echo "#"
echo "# No reading below came from another build. A row that could only be taken on an older pin"
echo "# would say so on the row; there is none in this run."
echo "# The scratch directory the module roots and the pass copies live in is $SCRATCH, fixed on"
echo "# purpose: a diagnostic quotes it, and a path that changed per run would make two runs differ."

# ---------------------------------------------------------------------------------------------
banner 1 "the six writings of the case-study program, and the counterexample"
echo "# The writing form is the library's: \`let dloss = grad(loss)\` then \`dloss<layer, y>(X)\`."
echo "# The six are the ways of writing the program that change no meaning; \`train-reorder-output\` is"
echo "# the counterexample, which moves the prints and so moves the order edges and must differ."
echo "# The reading is the canonical document of each, so it is the graph and not the file name."
echo
for f in train train-rename train-swap train-inline loss-a loss-b train-reorder-output; do
    doc "$f" "$VARIANTS/$f.cml" std::jgir canonical=true
done
echo "# the six against the baseline, then the counterexample against it"
for f in train-rename train-swap train-inline loss-a loss-b train-reorder-output; do
    printf '%-22s %s\n' "$f" "$(first_diff "$SCRATCH/train.json" "$SCRATCH/$f.json")"
done
echo

# ---------------------------------------------------------------------------------------------
banner 2 "the scale of train.cml"
echo "# Node = \`graphs[*].nodes\` summed; edge = one entry of a node's \`inputs\`, which is what a"
echo "# reference is in a canonical document (it has no \`edges\` array); region = \`graphs[*].regions\`;"
echo "# bytes = the canonical document itself. The two programs are printed apart: the six writings'"
echo "# baseline, and the copy the tree ships under examples/case-study/."
echo
doc case "$CASE" std::jgir canonical=true
python3 - "$SCRATCH/train.json" "$SCRATCH/case.json" <<'PY'
import json
import sys

for path in sys.argv[1:]:
    d = json.load(open(path))
    g = d["graphs"]
    nodes = sum(len(x.get("nodes", [])) for x in g)
    edges = sum(len(n.get("inputs", [])) for x in g for n in x["nodes"])
    regions = sum(len(x.get("regions", [])) for x in g)
    per = " ".join(str(len(x.get("nodes", []))) for x in g)
    print("%-12s graphs=%d nodes=%d edges=%d regions=%d bytes=%d"
          % (path.split("/")[-1].replace(".json", ""), len(g), nodes, edges, regions, len(open(path, "rb").read())))
    print("             per graph, in the document's order: %s" % per)
    print("             entry=%s root=%s" % (d.get("entry"), d.get("root")))
PY
echo

# ---------------------------------------------------------------------------------------------
banner 3 "the three backends on the training program"
echo "# The program is the one the tree ships, examples/case-study/train.cml. The paper's sentence is"
echo "# that the three backends print the same thing; the comparison at the end is the reading."
echo
for b in nvm fvm jit; do
    runkept "back-$b" "$CASE" "std::$b"
done
echo "# the three outputs against each other (the masked text: the program itself prints no time, and"
echo "# a comparison of the raw files would part on one if it ever did)"
printf 'nvm against fvm  %s\n' "$(first_diff "$SCRATCH/back-nvm.masked" "$SCRATCH/back-fvm.masked")"
printf 'nvm against jit  %s\n' "$(first_diff "$SCRATCH/back-nvm.masked" "$SCRATCH/back-jit.masked")"
echo

# ---------------------------------------------------------------------------------------------
banner 4 "the rows of tab:lang, under the library's grad"
echo "# \`edits-ecoop.cml\` is copied into the scratch root as \`eco.cml\`: a module's name is its file's"
echo "# stem, so a pass library reaches a command under that name. The file in the tree is not renamed"
echo "# and not edited. The program is the six writings' baseline."
echo "#   print_unordered  a print spliced at a pure node of main -- expected refused (order check)"
echo "#   print_ordered    the same body spliced at a statement of the chain -- expected accepted"
echo "#   loss_print       the same print in loss, whose body has no call the analysis cannot name --"
echo "#                    expected refused (effect check, PassEffectBeyondType)"
echo "#   drop_print       every println erased -- expected accepted, and nothing printed"
echo "#   break_rate       the targets tensor put in the learning-rate slot -- expected refused by the"
echo "#                    check on the edge's types"
echo "# The break_rate row selects the one constant of type float and the one of type f32[4, 1]; if"
echo "# either is not unique the pass returns the program unchanged, so the row would print the"
echo "# program's own output instead of a refusal. That is the one way this row can go quiet, and the"
echo "# record shows which of the two happened."
cp "$VARIANTS/edits-ecoop.cml" "$SCRATCH/eco.cml"
cp "$VARIANTS/train.cml" "$SCRATCH/train.cml"
echo
for p in eco::print_unordered eco::print_ordered eco::loss_print eco::drop_print eco::break_rate; do
    runp "$SCRATCH" "$SCRATCH/train.cml" "$p" std::nvm
done

# ---------------------------------------------------------------------------------------------
banner 5 "the optimization table: the MLP and the char-transformer"
echo "# Two kinds of row per pass, printed apart:"
echo "#   the document  -- \`<prog> <pass> std::jgir canonical=true\`, WITH NO --arg on either program:"
echo "#                    the canonical document is read from the program as written, and a document"
echo "#                    compared against one taken with another --arg set is not a comparison."
echo "#   the run       -- the program actually runs, with the arguments each program documents."
echo "#                    The MLP's data comes from probes/gen_mlp_data.py (same seed, same shapes),"
echo "#                    64 train / 16 val, 2 epochs; the transformer's is its own committed corpus."
echo "# The transformer cannot run on this build: its loss goes through the library's vjp, and that"
echo "# stops at 'jvp has no forward transform'. Its run row therefore shows the diagnostic once per"
echo "# pass, and its table row is the document alone. That is the reading, not a gap."
echo
echo "# --- the MLP: showcase/c2-mlp-mnist/train.cml ---"
for p in "" opt::cse opt::fold opt::dce opt::saturate; do
    doc "mlp-$(leaf "$p")" "$MLP" $p std::jgir canonical=true
done
echo "# the document rows against the baseline"
for p in opt::cse opt::fold opt::dce opt::saturate; do
    printf '%-22s %s\n' "$p" "$(first_diff "$SCRATCH/mlp-baseline.json" "$SCRATCH/mlp-$(leaf "$p").json")"
done
echo
echo "# the MLP's runs: does the pass leave the output alone (seconds= is masked)"
echo "### python3 $PROBES/gen_mlp_data.py --out $SCRATCH/mlp-data"
python3 "$PROBES/gen_mlp_data.py" --out "$SCRATCH/mlp-data" 2>&1
echo
for p in "" opt::cse opt::fold opt::dce opt::saturate; do
    runkept "mlp-run-$(leaf "$p")" \
        "--arg" "data=$SCRATCH/mlp-data" --arg steps=2 --arg batch=16 --arg lr=0.001 \
        --arg log_every=1 --arg threads=1 "$MLP" $p std::nvm
done
echo "# each run against the baseline run (the masked text, so the run's own seconds= is not the"
echo "# difference: that field moves every run and says nothing about the pass)"
for p in opt::cse opt::fold opt::dce opt::saturate; do
    printf '%-22s %s\n' "$p" "$(first_diff "$SCRATCH/mlp-run-baseline.masked" "$SCRATCH/mlp-run-$(leaf "$p").masked")"
done
echo
echo "# --- the char-transformer: showcase/c3-char-transformer/train.cml ---"
for p in "" opt::cse opt::fold opt::dce opt::saturate; do
    doc "tr-$(leaf "$p")" "$TRANSFORMER" $p std::jgir canonical=true
done
echo "# the document rows against the baseline"
for p in opt::cse opt::fold opt::dce opt::saturate; do
    printf '%-22s %s\n' "$p" "$(first_diff "$SCRATCH/tr-baseline.json" "$SCRATCH/tr-$(leaf "$p").json")"
done
echo
echo "# the transformer's run, with its own documented arguments"
for p in "" opt::cse opt::fold opt::dce opt::saturate; do
    runkept "tr-run-$(leaf "$p")" \
        --arg corpus=showcase/c3-char-transformer/corpus.txt --arg mode=train --arg steps=2 \
        --arg lr=0.001 --arg seed=1 "$TRANSFORMER" $p std::nvm
done

echo
echo "# --- the transformer's output, byte for byte, before and after each pass ---"
echo "# This is the column the paper quotes for the transformer. Each row is \`cmp\`, so \`same\` means"
echo "# every byte of the two outputs is equal, and any other answer names the first byte that parts;"
echo "# 'the output looks the same' is not a reading and is not what this line says."
echo "# Three forms per row, because the transformer's own run stops before it prints anything:"
echo "#   run raw     the program's own stdout+stderr with the pass and without it, unmasked. Today it"
echo "#               is the same diagnostic both times, so this is a byte equality of two sentences."
echo "#   run masked  the same with \`seconds=<n>\` masked. The mask is what makes the comparison a"
echo "#               reading of the pass rather than of a clock, so on a build where this program"
echo "#               runs, this is the column to quote and the raw one will part on that field."
echo "#   document    the canonical document of the program with the pass and without it: the pass's"
echo "#               output in the sense the rest of this section uses, and it is deterministic."
echo "# The section [5] note above stands: the transformer cannot run on this build (its loss goes"
echo "# through the library's vjp, which stops at 'jvp has no forward transform'), so the run rows are"
echo "# equal because the same refusal is printed both times -- that is the fact, not an assumption"
echo "# that the pass changes nothing at runtime."
for p in opt::cse opt::fold opt::dce opt::saturate; do
    l=$(leaf "$p")
    printf '%-14s run raw  %-36s run masked  %-36s document  %s\n' "$p" \
        "$(first_diff "$SCRATCH/tr-run-baseline.out" "$SCRATCH/tr-run-$l.out")" \
        "$(first_diff "$SCRATCH/tr-run-baseline.masked" "$SCRATCH/tr-run-$l.masked")" \
        "$(first_diff "$SCRATCH/tr-baseline.json" "$SCRATCH/tr-$l.json")"
done

echo
echo "# --- the same four passes on the case-study program, whole ---"
echo "# The rows above are two showcases. The paper's table is also asked for on the case-study"
echo "# program as written, so the same four passes run on examples/case-study/train.cml -- the whole"
echo "# program, no --arg, the document read from it as written. It has no run row here because its"
echo "# output is section [3]'s three backends, which is the same program."
for p in opt::cse opt::fold opt::dce opt::saturate; do
    doc "case-$(leaf "$p")" "$CASE" $p std::jgir canonical=true
done
echo "# each against the case-study program's own document, which section [2] took"
for p in opt::cse opt::fold opt::dce opt::saturate; do
    printf '%-22s %s\n' "$p" "$(first_diff "$SCRATCH/case.json" "$SCRATCH/case-$(leaf "$p").json")"
done

echo
echo "# --- the nodes before and after each pass ---"
echo "# Node = the sum of \`graphs[*].nodes\` of the canonical document, the count section [2] defines."
echo "# The two columns are two documents of the same program -- the one with no pass and the one with"
echo "# that pass -- so both numbers are read and neither is a proxy: the before column is a"
echo "# determinate count and repeats exactly, and the after column likewise."
echo "# A row whose delta is 0 is a pass that left the count alone, which is a reading too; it is not a"
echo "# missing measurement, and the document row above says whether it changed anything else."
python3 - "$SCRATCH" <<'PY'
import json
import sys

scratch = sys.argv[1]
# program tag, the document of the program with no pass
programs = [("case", "case.json"), ("mlp", "mlp-baseline.json"), ("tr", "tr-baseline.json")]
passes = ["cse", "fold", "dce", "saturate"]


def nodes_of(path):
    d = json.load(open(path))
    return sum(len(g.get("nodes", [])) for g in d["graphs"])


print("%-5s %-9s %9s %9s %8s" % ("prog", "pass", "nodes", "after", "delta"))
for tag, base_name in programs:
    before = nodes_of("%s/%s" % (scratch, base_name))
    print("%-5s %-9s %9d %9s %8s" % (tag, "-", before, "-", "-"))
    for p in passes:
        after = nodes_of("%s/%s-%s.json" % (scratch, tag, p))
        print("%-5s %-9s %9d %9d %+8d" % (tag, p, before, after, after - before))
PY

echo
echo "# --- the regions of each program, counted by kind ---"
echo "# Region = one entry of \`graphs[*].regions\`. The kinds are the runtime's GCRegionKind names as"
echo "# the document spells them: body, arm, loop_cond, loop_body, inline, block. The loop_body column"
echo "# is the answer to 'is LoopBody > 0' -- it is the count itself, so a 0 and a 3 are different"
echo "# readings and neither is the boolean."
echo "# Two shapes of the document have to be known to read a zero: the body region is written only"
echo "# when it carries a fact (the tags on the function), and a graph with no region besides the body"
echo "# writes no regions key at all. So a program of straight-line code shows every column 0 because"
echo "# it has no region to count -- the same 0 a broken count would give, which is why the total is"
echo "# printed beside the columns and why section [2] counts the same field."
python3 - "$SCRATCH" <<'PY'
import collections
import json
import sys

scratch = sys.argv[1]
KINDS = ["body", "arm", "loop_cond", "loop_body", "inline", "block"]
programs = [("case", "case.json"), ("mlp", "mlp-baseline.json"), ("tr", "tr-baseline.json")]
passes = ["cse", "fold", "dce", "saturate"]


def census(path):
    d = json.load(open(path))
    c = collections.Counter()
    for g in d["graphs"]:
        for r in g.get("regions", []):
            c[r.get("kind", "(no kind)")] += 1
    return c


print("%-5s %-9s %s %8s" % ("prog", "pass", " ".join("%10s" % k for k in KINDS), "regions"))
for tag, base_name in programs:
    rows = [("-", base_name)] + [(p, "%s-%s.json" % (tag, p)) for p in passes]
    for p, name in rows:
        c = census("%s/%s" % (scratch, name))
        print("%-5s %-9s %s %8d"
              % (tag, p, " ".join("%10d" % c.get(k, 0) for k in KINDS), sum(c.values())))
PY

echo
echo "# --- the time of each pass: nine fresh processes, the median ---"
echo "# The reading is the pass window itself (--time-phases: the entry named \`pass\` whose detail is"
echo "# the pass as the pipeline spelled it), so the terminator's printing is not in it. Wall follows"
echo "# the host's load and CPU time does not; both are printed with the load line beside them, and"
echo "# the exit code of every process is printed with the median, because a pass that failed still"
echo "# leaves a \`pass\` phase and its window would otherwise read as a fast one."
echo "# Absolute milliseconds here are only comparable with rows taken at the same load. The load at"
echo "# this block's start and its end is printed, and the median plus the range is the reading the"
echo "# paper's table takes; the nine samples are in the phase files under $SCRATCH."
echo "#   /proc/loadavg $(cat /proc/loadavg)"
timed case-cse "$CASE" opt::cse - ""
timed case-fold "$CASE" opt::fold - ""
timed case-dce "$CASE" opt::dce - ""
timed case-saturate "$CASE" opt::saturate - ""
timed mlp-cse "$MLP" opt::cse - ""
timed mlp-fold "$MLP" opt::fold - ""
timed mlp-dce "$MLP" opt::dce - ""
timed mlp-saturate "$MLP" opt::saturate - ""
timed tr-cse "$TRANSFORMER" opt::cse - ""
timed tr-fold "$TRANSFORMER" opt::fold - ""
timed tr-dce "$TRANSFORMER" opt::dce - ""
timed tr-saturate "$TRANSFORMER" opt::saturate - ""
echo "#   /proc/loadavg $(cat /proc/loadavg)"
echo

# ---------------------------------------------------------------------------------------------
banner 6 "the fusion"
echo "# The transform is \`greedy::fuse\`, a pass written in Camel (test/cases/opt/writings/"
echo "# greedy_fuse.cml.in), copied into the scratch root as \`greedy.cml\`. The reading below is the"
echo "# strict one: the two writings of one program must fuse to ONE canonical form."
echo
cp test/cases/opt/writings/greedy_fuse.cml.in "$SCRATCH/greedy.cml"
cp "$VARIANTS/loss-a.cml" "$VARIANTS/loss-b.cml" "$SCRATCH/"
echo "### python3 test/cases/opt/writings/greedy_fusion_writings.py --camel $CAMEL --require-one-form"
CAMEL_TEST_TMPDIR="$SCRATCH/fusion" python3 test/cases/opt/writings/greedy_fusion_writings.py --camel "$CAMEL" --require-one-form 2>&1
echo "### exit=$?"
echo
echo "# the two losses, each fused, and each against its own unfused document"
for f in loss-a loss-b; do
    docp "$SCRATCH" "fused-$f" "$SCRATCH/$f.cml" greedy::fuse std::jgir canonical=true
    printf '%-22s %s\n' "fused $f against unfused" "$(first_diff "$SCRATCH/$f.json" "$SCRATCH/fused-$f.json")"
done
echo "# and the two fused documents against each other"
printf 'fused loss-a against fused loss-b  %s\n' "$(first_diff "$SCRATCH/fused-loss-a.json" "$SCRATCH/fused-loss-b.json")"
echo

# ---------------------------------------------------------------------------------------------
banner 7 "commit and normalization time, and what one commit costs against the size of the program (these blocks float, as does the timing block that closes [5])"
echo "# Three probes in $PROBES, each run as an ordinary pass on examples/case-study/train.cml:"
echo "#   canonical_once::run    one normalization, one call of \`nodes(graph_of)\`"
echo "#   canonical_walk::walk   one normalization per graph, the way a pass that walks every graph"
echo "#                          pays it"
echo "#   commit_only::touch     \`open\` + \`commit\` with no edit in between (it includes open)"
echo "# Nine fresh processes each, because a repeat inside one process drifts upward; the number is"
echo "# the \`pass\` phase's wall and CPU time from --time-phases, median with the range beside it."
echo "# This whole section floats by construction: these are times, and the host's load line, which is"
echo "# printed here because a time with no load beside it cannot be read, moves with the host too."
echo
timing() {
    tag=$1
    word=$2
    echo "### CAMEL_PACKAGES=$PROBES_ROOT camel examples/case-study/train.cml $word std::jgir canonical=true --time-phases=$SCRATCH/ph-$tag-<n>.json   (nine times, a fresh process each)"
    i=1
    while [ "$i" -le 9 ]; do
        CAMEL_PACKAGES="$PROBES_ROOT" "$CAMEL" examples/case-study/train.cml "$word" std::jgir canonical=true \
            --time-phases="$SCRATCH/ph-$tag-$i.json" > "$SCRATCH/ph-$tag-$i.out" 2>&1
        i=$((i + 1))
    done
    python3 - "$tag" "$word" "$SCRATCH" <<'PY'
import json
import statistics
import sys

tag, word, scratch = sys.argv[1], sys.argv[2], sys.argv[3]
wall, cpu = [], []
for i in range(1, 10):
    d = json.load(open("%s/ph-%s-%d.json" % (scratch, tag, i)))
    hit = [p for p in d["phases"] if p["name"] == "pass" and p["detail"] == word]
    if not hit:
        said = open("%s/ph-%s-%d.out" % (scratch, tag, i)).read().strip().splitlines()
        print("run=%d no 'pass' phase named %s; its first line of output: %s"
              % (i, word, said[0] if said else "(it printed nothing)"))
        continue
    p = hit[0]
    wall.append(p["wall_ns"] / 1e6)
    cpu.append(p["cpu_ns"] / 1e6)
    print("run=%d wall_ms=%.2f cpu_ms=%.2f total_ms=%.2f" % (i, wall[-1], cpu[-1], d["total_ns"] / 1e6))
if not wall:
    print("%-22s NO READING: the probe did not run" % word)
else:
    print("%-22s n=%d median_wall_ms=%.2f [%.2f, %.2f] median_cpu_ms=%.2f"
          % (word, len(wall), statistics.median(wall), min(wall), max(wall), statistics.median(cpu)))
PY
    echo
}

echo "# the host's load, which is part of this section for the reason above"
echo "#   /proc/loadavg $(cat /proc/loadavg)"
echo
timing once canonical_once::run
timing walk canonical_walk::walk
timing commit commit_only::touch
echo "#   /proc/loadavg $(cat /proc/loadavg)"

echo
echo "# --- what one commit costs against the size of the program ---"
echo "# \`commit_only::touch\` opens a transaction on the program and commits it with no edit in"
echo "# between, so the number is the checks and the publish of a program nothing changed: the floor a"
echo "# commit of a change is read against. It includes \`open\` -- the pass window is the only window"
echo "# the recorder has, and there is no scope inside a pass -- and the row below it pays the same"
echo "# \`open\`, so the pair still isolates the edit."
echo "# Five sizes, nine fresh processes each, with the program's own node count on the row so the"
echo "# sizes are read and not assumed: the three the paper names, then the two bigger ones."
echo "#   /proc/loadavg $(cat /proc/loadavg)"
doc "scale-resnet" "benchmarks/resnetbn/model.cml" std::jgir canonical=true
doc "scale-mcts" "showcase/d4-mcts/search.cml" std::jgir canonical=true
echo
timed commit-case "$CASE" commit_only::touch "$PROBES_ROOT" "nodes=$(nodes_of "$SCRATCH/case.json")"
timed commit-mlp "$MLP" commit_only::touch "$PROBES_ROOT" "nodes=$(nodes_of "$SCRATCH/mlp-baseline.json")"
timed commit-tr "$TRANSFORMER" commit_only::touch "$PROBES_ROOT" "nodes=$(nodes_of "$SCRATCH/tr-baseline.json")"
timed commit-resnet "benchmarks/resnetbn/model.cml" commit_only::touch "$PROBES_ROOT" "nodes=$(nodes_of "$SCRATCH/scale-resnet.json")"
timed commit-mcts "showcase/d4-mcts/search.cml" commit_only::touch "$PROBES_ROOT" "nodes=$(nodes_of "$SCRATCH/scale-mcts.json")"

echo
echo "# --- the same commit with ONE node changed, on the char-transformer ---"
echo "# \`commit_one::poke\` is \`commit_only::touch\` plus exactly one edit: it adds one constant node to"
echo "# the graph the entry function value has and commits. No existing node moves and no second graph"
echo "# is touched, so this is the smallest edit there is; it is not what a rewriting pass pays, and"
echo "# the paper's sentence is about this one."
echo "# The document below is the proof the edit landed: it is the transformer after the poke, and its"
echo "# node count is the untouched program's plus one. The probe itself prints nothing, because a"
echo "# line per run would be charged inside the window it measures."
docp "$PROBES_ROOT" "tr-poked" "$TRANSFORMER" commit_one::poke std::jgir canonical=true
python3 - "$SCRATCH" <<'PY'
import json
import sys

scratch = sys.argv[1]


def nodes(path):
    d = json.load(open(path))
    return sum(len(g.get("nodes", [])) for g in d["graphs"])


before = nodes("%s/tr-baseline.json" % scratch)
after = nodes("%s/tr-poked.json" % scratch)
print("# the edit: %d nodes before, %d after, %+d node(s)" % (before, after, after - before))
PY
timed commit-tr-poke "$TRANSFORMER" commit_one::poke "$PROBES_ROOT" "nodes=$(nodes_of "$SCRATCH/tr-poked.json")"
echo "#   /proc/loadavg $(cat /proc/loadavg)"

# ---------------------------------------------------------------------------------------------
banner 8 "the two derivative graphs: loss-a against loss-b"
echo "# The two losses are two writings of one loss, and the derivative is a library transform that"
echo "# runs at compile time, so the derivative graph is a graph of the canonical document -- the"
echo "# comparison of the two documents is the comparison of the two derivative graphs. The node"
echo "# counts per graph are printed so the size of the derivative graph is visible, and the"
echo "# signatures are printed because they are the only content of a canonical document that says"
echo "# what a graph is."
echo
python3 - "$SCRATCH/loss-a.json" "$SCRATCH/loss-b.json" <<'PY'
import json
import sys

for path in sys.argv[1:]:
    d = json.load(open(path))
    print("%s" % path.split("/")[-1])
    for i, g in enumerate(d["graphs"]):
        print("  g%d id=%-4s nodes=%-3d regions=%-2d signature=%s"
              % (i, g.get("id"), len(g.get("nodes", [])), len(g.get("regions", [])), g.get("signature")))
PY
echo "# the two documents against each other"
printf 'loss-a against loss-b  %s\n' "$(first_diff "$SCRATCH/loss-a.json" "$SCRATCH/loss-b.json")"
echo
echo "# --- the SECOND derivative on the four backends: NOT TAKEN in this run, and what it waits for ---"
echo "# This block is a placeholder and it prints no measurement: no line below is a second derivative."
echo "# What is owed here, in the author's own words (review page of 2026-11-20 02:30): the second"
echo "# derivative of \`x*x\` at 2, of \`x^3\` at 2, and a Hessian example, each on std::nvm, std::fvm,"
echo "# std::jit and std::tf, with whether the four agree. The first derivative of the two losses above"
echo "# is what section [8] compares; a second derivative needs \`grad(grad(f))\`, and the outer \`grad\`"
echo "# is refused on every build in develop today: it needs \`split\` to hand out static code, which is"
echo "# the product of another lane. So the block is PENDING -- to be taken once 925c38791 (the head of"
echo "# item5-red-925c38791, unmerged) is in develop -- and the line below is the check of that"
echo "# condition, not a reading of the derivative."
PDEP=925c38791
if ! git -C "$ROOT" cat-file -e "$PDEP^{commit}" 2>/dev/null; then
    echo "#   wait state: $PDEP is not in this checkout at all (a lane's own commits do not travel to the"
    echo "#   host), so even the wait cannot be resolved here; re-take this block on the tree that holds"
    echo "#   it. The block stays owed either way."
elif git -C "$ROOT" merge-base --is-ancestor "$PDEP" HEAD 2>/dev/null; then
    echo "#   wait state: $PDEP IS an ancestor of this tree now, so this block can be taken -- and this"
    echo "#   run did not take it. That is the reading: the product arrived and the block is still owed."
else
    echo "#   wait state: $PDEP resolves here and is NOT an ancestor of HEAD, so the block is owed"
    echo "#   exactly as this line says. Nothing in this run stands in for it."
fi
echo

# ---------------------------------------------------------------------------------------------
banner 9 "the e2e lane: all of e2e/capture.sh, run unchanged"
echo "# This is that script, not a copy of its commands: if the two ever drift, this section says so."
echo "# It moves to the worktree root and to the e2e directory itself, which is where \`passes\` is"
echo "# found beside the program. Its own header prints the build it ran on, and its two readings that"
echo "# the paper quotes verbatim are in it: the forward edit refused, and the reverse one accepted"
echo "# with the marker never printing."
echo
sh "$E2E/capture.sh" 2>&1
echo "### e2e capture.sh exit=$?"
echo

echo "===== capture-all: the end ====="