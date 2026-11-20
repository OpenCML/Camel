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
# better, and no file under src/, no expected value and no pass is touched. The blocks are the nine
# readings the dispatch of 2026-11-20 01:10 lists, in that order, plus the five the capture lanes
# were handed afterwards. A lane reading sits inside the block it extends, marked with its letter:
#
#   [1] the six writings of the case-study program, in the library's `grad` form, and the counterexample
#       [A] then the six rewrites of the e2e program, each against the program's own canonical hash
#   [2] the scale of train.cml: graphs, nodes, edges, regions, bytes
#   [3] the three backends' output on the training program, and that they agree byte for byte
#   [4] the rows of tab:lang: the two prints in `main`, the print in `loss`, the deleted prints,
#       and `break_rate` (the edge-type check refusing the wrong value in the learning-rate slot)
#       [B] then the three graph checks -- exit, acyclic, scoped -- which layer refuses each, verbatim
#   [5] the optimization table on the MLP and on the char-transformer
#   [6] the fusion: the strict writings reading, then loss-a and loss-b fused
#   [7] commit and normalization time (the one floating section, see its head)
#   [8] loss-a against loss-b: the two documents, what the derivative graph is NOT, and the numbering
#       once `std::opt` has run (read that head: this is the numbering-stability reading)
#   [9] the e2e lane, which is e2e/capture.sh run unchanged, so the two never drift apart
#   [10] [C] `std::prl` on the case and on the e2e program, 200 runs each, and what is compared
#   [11] [E] the scheduling seed: the switch does not exist, and where the random actually lives
#
# Run it from anywhere; it moves to the worktree root itself:
#
#   sh docs/handover/reports/eval-round10/capture-all.sh \
#       > docs/handover/reports/eval-round10/capture-all.raw.txt 2>&1
#
# Repeatability: two runs of this script on one build differ in section [7] and in the scratch path.
# Section [7] is time (wall and CPU per probe, which is what it reads) plus the host's load line
# beside it, because a time with no load beside it cannot be read. The scratch path is per run
# because it used to be one fixed directory (`/tmp/camel-capture-all`) and that made two runs share
# state: two lanes running this script at once wrote into it, and a run whose scratch another run had
# emptied printed readings that were wrong and said nothing about it. The path carries the run's own
# pid, and the record names it, so a reading that quotes a path says which run it came from.
# Everything else is byte for byte the same: the MLP's own `seconds=` field is masked below because
# every run moves it. PYTHONHASHSEED is pinned so tools that walk sets print in one order.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
cd "$ROOT" || exit 1

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/camel-capture-all.$$.XXXXXX") || exit 1
trap 'rm -rf "$SCRATCH"' EXIT HUP INT TERM

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
echo "# The scratch directory the module roots and the pass copies live in is $SCRATCH: this run's own,"
echo "# named here because a diagnostic quotes it and because two runs would otherwise share one."

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
echo "# --- [A] the six rewrites of the e2e program: each one's canonical hash against the program's ---"
echo "# A hash is only comparable beside its definition, so it is written here:"
echo "#   canonical hash = sha256 of the bytes \`camel <program> std::jgir canonical=true\` writes to"
echo "#                    stdout (the pass prints the document; canonical=true fixes its key order,"
echo "#                    which docs/cli.md's pass-parameter table says makes two runs compare byte"
echo "#                    for byte). There is no --hash option: this is the document's own digest."
echo "#   the second reading = the \`e2e canonical=<sha256>\` line \`passes::report\` prints, which is a"
echo "#                    digest the pass computes over its own canonical node text. Two independent"
echo "#                    digests, so a row that agrees in both is not one tool agreeing with itself."
echo "# The program is e2e/program.cml as the tree ships it. Each rewrite changes only how the same"
echo "# program is written, and must print the same five lines; the rewrites are in $VARIANTS:"
echo "#   lift     the closure's body lifted into a named declaration, the lambda calls it"
echo "#   direct   the call through a known function value made a direct call (\`let ap = apply\`)"
echo "#   merge    a copy of a function declared, and the copy called"
echo "#   drop     a dead pure statement added -- text that produces no node"
echo "#   unreach  a function nobody calls -- text that produces no node, but does it produce a graph?"
echo "#   unsync-a the \`sync\` dropped from the closure's body"
echo "#   unsync-b the \`sync\` dropped from \`apply\`"
echo "# Seven rows and not six: this program writes \`sync\` twice, and removing it in one place and in"
echo "# the other are two different edits. The dispatch's six names count the intent, not the placement."
echo
doc "e2e-program" "$E2E/program.cml" std::jgir canonical=true
for f in lift direct merge drop unreach unsync-a unsync-b; do
    doc "e2e-$f" "$VARIANTS/e2e-$f.cml" std::jgir canonical=true
done
echo "# the seven against the program's own document (both digests are on each doc= line above)"
for f in lift direct merge drop unreach unsync-a unsync-b; do
    printf '%-22s %s\n' "$f" "$(first_diff "$SCRATCH/e2e-program.json" "$SCRATCH/e2e-$f.json")"
done
echo
echo "# the second reading: what \`passes::report\` hashes on the same eight programs. It reads the"
echo "# document as the program hands it over, with no instrumentation pass in front of it: that pass" 
echo "# adds its own nodes, and a digest over an instrumented graph would not be the program's."
for f in program lift direct merge drop unreach unsync-a unsync-b; do
    prog="$E2E/program.cml"
    if [ "$f" != program ]; then prog="$VARIANTS/e2e-$f.cml"; fi
    out="$SCRATCH/e2e-report-$f.txt"
    echo "### CAMEL_PACKAGES=$E2E camel $prog passes::report std::nvm"
    CAMEL_PACKAGES="$ROOT/$E2E" "$CAMEL" "$prog" passes::report std::nvm > "$out" 2>&1
    st=$?
    echo "### exit=$st"
    sed -n 's/^\(e2e graphs=.*\|e2e canonical=.*\)$/'"$f"' \1/p' "$out"
done
echo
echo "# the other half of every row: a rewrite that changes no meaning must not move the output"
runkept "e2e-run-program" "$E2E/program.cml"
for f in lift direct merge drop unreach unsync-a unsync-b; do
    runkept "e2e-run-$f" "$VARIANTS/e2e-$f.cml"
done
for f in lift direct merge drop unreach unsync-a unsync-b; do
    printf '%-22s %s\n' "$f" "$(first_diff "$SCRATCH/e2e-run-program.masked" "$SCRATCH/e2e-run-$f.masked")"
done
echo "# Read the two halves together: a rewrite whose document is the program's document AND whose"
echo "# output is the program's output is a rewrite that changed nothing this program can observe."
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
echo "#   print_ordered    the same body spliced at a statement of the chain -- expected accepted, and"
echo "#                    the announcement takes that line's place, so main still prints three lines"
echo "#   print_appended   the same body at the same pure node as print_unordered, then ordered *after*"
echo "#                    the third print -- expected accepted, and main prints four lines (the"
echo "#                    additive form the take-over row above cannot show)"
echo "#   print_appended_reversed  the same edge anchored at the third print instead -- refused: the"
echo "#                    line lands between two prints and nothing orders it against the earlier one"
echo "#   print_prepended  the same splice ordered *before* the first print of the chain -- expected"
echo "#                    accepted, with the new line first (the reverse edge anchored where it can"
echo "#                    be complete: against the head, not against the third)"
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
for p in eco::print_unordered eco::print_ordered eco::print_appended eco::print_appended_reversed eco::print_prepended eco::loss_print eco::drop_print eco::break_rate; do
    runp "$SCRATCH" "$SCRATCH/train.cml" "$p" std::nvm
done
echo
echo "# --- [B] the three graph checks: exit, acyclic, scoped -- which layer refuses each, verbatim ---"
echo "# The three are the first three of the six a commit asks (docs/design/graph-editing/README.cn.md"
echo "# 8.1). A bad edit meets two layers before a graph changes: the operator's own step check, which"
echo "# runs where the edit is made, and the commit, which is the seal in src/runtime/graph.cpp. Every"
echo "# row below names the layer that answered, because 'refused' without the layer is not a reading."
echo "# The programs are the repo's own commit-check fixtures -- the same two files the repo's test::"
echo "# passes use -- so a reader can put the two readings side by side and see the same text twice."
echo
cp "$VARIANTS/edits-bad.cml" "$SCRATCH/bad.cml"
cp "$VARIANTS/edits-bad-scope.cml" "$SCRATCH/bad2.cml"
cp test/cases/opt/commit_checks/identity_at_exit.cml test/cases/opt/commit_checks/sibling_arm.cml "$SCRATCH/"
cp test/cases/opt/commit_checks/loop_body_read.cml test/cases/opt/commit_checks/fold_function_type.cml "$SCRATCH/"
echo "#   move_exit     replace(exit, a node an arm computes)     expected: the step check, an anchor"
echo "#   self_read     replace(a value, the exit that reads it)  expected: the step check, a cycle"
echo "#   cross_region  replace(the exit's value, an arm's node)  expected: the commit, the exit region"
echo "#   cross_arm     replace(an arm's body-read value, another arm's node)  expected: the commit,"
echo "#                 the branch-arm invariant -- the exit is untouched, so the exit rule cannot be"
echo "#                 the one that answers"
echo
for p in bad::move_exit bad::self_read bad::cross_region; do
    runp "$SCRATCH" "$SCRATCH/identity_at_exit.cml" "$p" std::nvm
done
runp "$SCRATCH" "$SCRATCH/sibling_arm.cml" bad2::cross_arm std::nvm
echo "# The commit's own reading of the two rows this op set cannot hand it, quoted from the repo's"
echo "# fixtures. These are the fixtures' drafts and not edits made above; the layer is the reading,"
echo "# and leaving the row out would make the layer look absent:"
run "$SCRATCH/fold_function_type.cml" test::commit_cycle std::nvm
run "$SCRATCH/sibling_arm.cml" test::commit_sibling_arm std::nvm
run "$SCRATCH/loop_body_read.cml" test::commit_loop_body_read std::nvm
run "$SCRATCH/identity_at_exit.cml" test::commit_exit_region std::nvm
echo "# The two rows this op set cannot construct, written as the reason and not as a dropped row:"
echo "#   a pure exit violation -- the only edit that would put the exit inside an arm is naming the"
echo "#     exit in replace, and the step check refuses that because an exit anchors its graph. The"
echo "#     other route (make the exit READ an arm's value) does reach the commit and its exit text, but"
echo "#     it also makes the body read what the arm wrote, so it is not a pure exit violation."
echo "#   a cycle at the commit -- the step check refuses every cycle these ops can close ('the"
echo "#     replacement is the node itself or is computed from it'), so the commit never sees one; the"
echo "#     commit's cycle text is the test::commit_cycle row above."
echo "# The accepted twins are in the same two fixtures (a copy that reads a value of its own arm, and"
echo "# one of its own region, is published), so a refusal here is about the read, not about editing."
echo

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
banner 7 "commit and normalization time (this section floats: it is the only one that does)"
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

# ---------------------------------------------------------------------------------------------
banner 8 "loss-a against loss-b: the two documents, what is NOT in them, and the numbering"
echo "# READ THIS FIRST. This section used to say the derivative graph is a graph of the canonical"
echo "# document ('the derivative is a library transform that runs at compile time, so the derivative"
echo "# graph is a graph of the canonical document'). On this build it is not:"
echo "#   std::stats on loss-a prints graphs=5, nodes=57, and the five graphs carry [2,17,14,8,16] nodes;"
echo "#   the document holds ONE autodiff:grad@1.0 node (in step's graph) whose result is called"
echo "#   indirectly (\"call\":\"indirect\"), and no graph of the derivative. The derivative is built by"
echo "#   modules/autodiff's GradResolver after the program's graphs exist, and never enters the"
echo "#   canonical form."
echo "# So this section is two readings and nothing more, and neither of them is a comparison of two"
echo "# derivative graphs -- that reading has no entry point today:"
echo "#   (1) the two losses' PROGRAM documents, and"
echo "#   (2) the numbering once std::opt has run."
echo "# The question page that records this is docs/handover/questions/"
echo "# 2026-11-20-the-two-derivative-graphs-are-not-in-the-canonical-document.cn.md"
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
echo "# and the derivative's own node, counted where it actually is"
python3 - "$SCRATCH/loss-a.json" <<'PY'
import json
import sys

d = json.load(open(sys.argv[1]))
grads, indirect = [], 0
for i, g in enumerate(d["graphs"]):
    for n in g.get("nodes", []):
        p = n.get("payload", {})
        if str(p.get("op", "")).startswith("autodiff:grad"):
            grads.append((i, n.get("id"), p.get("op")))
        if p.get("call") == "indirect":
            indirect += 1
print("graphs=%d nodes=%d grad_nodes=%d indirect_calls=%d"
      % (len(d["graphs"]), sum(len(g.get("nodes", [])) for g in d["graphs"]), len(grads), indirect))
for i, nid, op in grads:
    print("  g%d %s %s" % (i, nid, op))
PY
echo "# the two documents against each other"
printf 'loss-a against loss-b  %s\n' "$(first_diff "$SCRATCH/loss-a.json" "$SCRATCH/loss-b.json")"
echo
echo "# (2) the numbering after std::opt. The reading is a triple and the three lines are read"
echo "# together or not at all: equal per-graph node counts with unequal bytes means the two graphs are"
echo "# ISOMORPHIC and the difference is the numbering; 'bytes differ' alone cannot tell that from a"
echo "# graph that really differs, and those are different findings."
for f in loss-a loss-b; do
    doc "opt-$f" "$VARIANTS/$f.cml" std::opt std::jgir canonical=true
done
python3 - "$SCRATCH/opt-loss-a.json" "$SCRATCH/opt-loss-b.json" <<'PY'
import hashlib
import json
import sys


def shape(path):
    raw = open(path, "rb").read()
    d = json.loads(raw)
    return ([len(g.get("nodes", [])) for g in d["graphs"]],
            len(raw),
            hashlib.sha256(raw).hexdigest(),
            [g.get("signature") for g in d["graphs"]])


a, b = shape(sys.argv[1]), shape(sys.argv[2])
print("loss-a after opt  graphs=%d per_graph=%s bytes=%d sha256=%s" % (len(a[0]), a[0], a[1], a[2]))
print("loss-b after opt  graphs=%d per_graph=%s bytes=%d sha256=%s" % (len(b[0]), b[0], b[1], b[2]))
print("per_graph_node_counts_equal = %s" % (a[0] == b[0]))
print("same_bytes = %s" % (a[2] == b[2]))
print("same_signatures = %s" % (a[3] == b[3]))
PY
printf 'loss-a after opt against loss-b after opt  %s\n' "$(first_diff "$SCRATCH/opt-loss-a.json" "$SCRATCH/opt-loss-b.json")"
echo "# std::opt is deterministic, so the difference above is the two writings and not the run:"
doc "opt-loss-a-again" "$VARIANTS/loss-a.cml" std::opt std::jgir canonical=true
printf 'loss-a after opt, twice  %s\n' "$(first_diff "$SCRATCH/opt-loss-a.json" "$SCRATCH/opt-loss-a-again.json")"
echo

# ---------------------------------------------------------------------------------------------
banner 9 "the e2e lane: all of e2e/capture.sh, run unchanged"
echo "# This is that script, not a copy of its commands: if the two ever drift, this section says so."
echo "# It moves to the worktree root and to the e2e directory itself, which is where \`passes\` is"
echo "# found beside the program. Its own header prints the build it ran on. The three versions of the"
echo "# instrumentation -- every one of them \`inline\` + \`order\` on the same program, differing in the"
echo "# edge alone -- are the three sections that run them, and their readings are these:"
echo "#   instrument_ordered   \`inline\` beside the closure's print, then \`order(before, added)\`: the"
echo "#                        accepted form, the five lines with \`# tick\` after each of the four"
echo "#   instrument_unordered the same edit with no order edge: refused, \`PassEffectOrder\`, the message"
echo "#                        quoted verbatim in NOTES.md's 'Item 4' (same two nodes, same position)"
echo "#   instrument_reversed  the same edge the other way round: accepted, and the marker runs in front"
echo "#                        of each line (before \`3fd76e564\` it was accepted and never ran)"
echo
sh "$E2E/capture.sh" 2>&1
echo "### e2e capture.sh exit=$?"
echo

# ---------------------------------------------------------------------------------------------
banner 10 "[C] std::prl: 200 runs of the case and of the e2e program, and what is compared"
echo "# The question is whether the scheduler's parallel execution is observable in the output. The"
echo "# comparison is over RAW BYTES: nothing is masked. Neither program prints a time, a path or a"
echo "# pid, so there is nothing to mask -- and a mask here would hide the very thing asked about, so"
echo "# this section says that instead of masking quietly. The count per group is 200, and the number"
echo "# that actually ran is printed on each summary line: a count is a copy of the loop's own value,"
echo "# not a claim about how long the loop ran."
echo "# Three groups: the case alone; the e2e program alone; the e2e program through its two passes"
echo "# (instrument then report), because a run whose passes walk the graph is the run in which work"
echo "# could be ordered differently."
echo
runs200() {
    tag=$1
    pkg=$2
    shift 2
    if [ -n "$pkg" ]; then
        echo "### $RUNS runs, a fresh process each: CAMEL_PACKAGES=$pkg $CAMEL $*"
    else
        echo "### $RUNS runs, a fresh process each: $CAMEL $*"
    fi
    i=1
    while [ "$i" -le "$RUNS" ]; do
        if [ -n "$pkg" ]; then
            CAMEL_PACKAGES="$pkg" "$CAMEL" "$@" > "$SCRATCH/$tag-$i.out" 2> "$SCRATCH/$tag-$i.err"
        else
            "$CAMEL" "$@" > "$SCRATCH/$tag-$i.out" 2> "$SCRATCH/$tag-$i.err"
        fi
        echo "$?" > "$SCRATCH/$tag-$i.rc"
        i=$((i + 1))
    done
    python3 - "$tag" "$SCRATCH" "$RUNS" <<'PY'
import collections
import hashlib
import sys

tag, scratch, runs = sys.argv[1], sys.argv[2], int(sys.argv[3])


def digest(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def size(path):
    return len(open(path, "rb").read())


outs, errs, rcs = collections.Counter(), collections.Counter(), collections.Counter()
bytes_of = {}
for i in range(1, runs + 1):
    o = digest("%s/%s-%d.out" % (scratch, tag, i))
    outs[o] += 1
    bytes_of[o] = size("%s/%s-%d.out" % (scratch, tag, i))
    errs[digest("%s/%s-%d.err" % (scratch, tag, i))] += 1
    rcs[open("%s/%s-%d.rc" % (scratch, tag, i)).read().strip()] += 1
for label, counter in (("stdout", outs), ("stderr", errs), ("exit", rcs)):
    for value, n in sorted(counter.items()):
        if label == "stdout":
            print("%-22s stdout sha256=%s bytes=%d runs=%d" % (tag, value, bytes_of[value], n))
        elif label == "stderr":
            print("%-22s stderr sha256=%s runs=%d" % (tag, value, n))
        else:
            print("%-22s exit   code=%s runs=%d" % (tag, value, n))
print("%-22s runs=%d distinct_stdout=%d distinct_stderr=%d distinct_exit=%d => %s"
      % (tag, runs, len(outs), len(errs), len(rcs),
         "byte-identical" if len(outs) == 1 and len(errs) == 1 and len(rcs) == 1 else "NOT identical"))
PY
    echo
}

RUNS=200
runs200 case-prl "" "$CASE" std::prl
runs200 e2e-prl "" "$E2E/program.cml" std::prl
runs200 e2e-prl-passes "$ROOT/$E2E" "$E2E/program.cml" passes::instrument passes::report std::prl
echo "# The three groups must not agree with each other: they are different programs (the case prints"
echo "# three loss lines, the e2e prints five step/total lines, the third adds the pass's own two)."
echo "# That is the control for 'identical within a group' not being 'everything prints the same':"
printf 'case against e2e (must differ)             %s\n' "$(first_diff "$SCRATCH/case-prl-1.out" "$SCRATCH/e2e-prl-1.out")"
printf 'e2e against e2e+passes (must differ)       %s\n' "$(first_diff "$SCRATCH/e2e-prl-1.out" "$SCRATCH/e2e-prl-passes-1.out")"
echo

# ---------------------------------------------------------------------------------------------
banner 11 "[E] the scheduling seed: the switch does not exist, and where the random actually lives"
echo "# One of the five readings has no row, and this section says so with the searches that would have"
echo "# found a switch. Three sentences in this order: what was searched; the hits and why none of them"
echo "# is a seed; where the random actually lives."
echo
echo "# 1. what was searched"
echo "### git grep -rn 'seed' -- src/passes/sched/ src/passes/sched/fastvm include/camel/passes/sched/"
git -C "$ROOT" grep -rn 'seed' -- src/passes/sched/ src/passes/sched/fastvm include/camel/passes/sched/ 2>/dev/null || echo "(no hit)"
echo "### git grep -n 'seed' -- docs/cli.md"
git -C "$ROOT" grep -n 'seed' -- docs/cli.md 2>/dev/null || echo "(no hit)"
echo "### $CAMEL --help | grep -iE 'seed|thread'   (the option table itself)"
"$CAMEL" --help 2>&1 | grep -iE 'seed|thread' || echo "(no --seed and no --threads on this line)"
echo
echo "# 2. the hits, each excluded: both are JIT comments about a register, not about scheduling."
echo "#    src/passes/sched/fastvm/jit/mir/mir_builder.h:684   'The entry wrapper seeds ...'"
echo "#    src/passes/sched/fastvm/jit/mir/mir_encode.cpp:571  'r13 is seeded once by the SysV entry"
echo "#                                                         wrapper'"
echo "#    Neither is a value a caller can set: r13 is the register the wrapper restores jitCtx from."
echo "#    The only 'seed' a program can pass is its own argument -- the transformer's --arg seed=1 in"
echo "#    section [5] seeds that program's data, not the scheduler."
echo
echo "# 3. where the random actually lives"
echo "### git grep -n 'mt19937\|random_device' -- include/camel/utils/random.h"
git -C "$ROOT" grep -n 'mt19937\|random_device' -- include/camel/utils/random.h 2>/dev/null || echo "(no hit)"
echo
echo "# => a 100-seed sweep cannot be run: there is no --seed option, the only scheduler knobs are the"
echo "#    thread count (-t/--threads) and CAMEL_TF_LINEAR / FANOUT_ALL / BYTECODE, and none of them is a"
echo "#    seed. origin/lane-determinism is fully merged (0 commits ahead of develop) and carries no"
echo "#    switch either. This is a reason and not a gap: no row was dropped to make the section short,"
echo "#    and the 200 runs of [10] are not offered as a substitute for the seeds that section asked for."
echo

echo "===== capture-all: the end ====="
