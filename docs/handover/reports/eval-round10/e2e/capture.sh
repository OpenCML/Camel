#!/bin/sh
# The raw output of the ECOOP e2e lane, captured on the host in one run: every command the report
# quotes, in the order the report quotes it. Run from the worktree root:
#
#   sh docs/handover/reports/eval-round10/e2e/capture.sh > docs/handover/reports/eval-round10/e2e/raw-output.txt 2>&1
set -u
CAMEL_HOME=$PWD/out/latest
export CAMEL_HOME
export LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib:$CAMEL_HOME/libs:$CAMEL_HOME/lib
CAMEL=$CAMEL_HOME/bin/camel
ROOT=$PWD
cd docs/handover/reports/eval-round10/e2e || exit 1

run() {
    echo "### camel $*"
    $CAMEL "$@" 2>&1
    echo "### exit=$?"
    echo
}

sha() {
    echo "### camel $* | sha256sum"
    $CAMEL "$@" 2>&1 | sha256sum
    echo
}

echo "===== build ====="
head -c 400 "$ROOT/out/latest/build-info.json"
echo
echo

echo "===== A: before the edit ====="
run program.cml std::nvm
sha program.cml std::jgir canonical=true
run program.cml passes::report std::nvm

echo "===== B: the same program, written differently ====="
run program-writing-b.cml std::nvm
sha program-writing-b.cml std::jgir canonical=true
run program-writing-b.cml passes::report std::nvm

echo "===== the edit that runs: the marker, once per closure call ====="
run program.cml passes::instrument std::nvm
run program.cml passes::instrument passes::report std::nvm
sha program.cml passes::instrument std::jgir canonical=true
run program-writing-b.cml passes::instrument passes::report std::nvm
sha program-writing-b.cml passes::instrument std::jgir canonical=true

echo "===== the edit that runs: the dedupe, output unchanged ====="
run program.cml passes::dedupe passes::report std::nvm
sha program.cml passes::dedupe std::jgir canonical=true
run program-writing-b.cml passes::dedupe passes::report std::nvm
sha program-writing-b.cml passes::dedupe std::jgir canonical=true

echo "===== the edits the commit refuses ====="
run program.cml passes::instrument_unordered std::nvm
run program.cml passes::instrument_ordered std::nvm

echo "===== and the one it accepts and drops: the marker never runs ====="
run program.cml passes::instrument_reversed std::nvm
run program.cml passes::instrument_reversed passes::report std::nvm
run program.cml passes::instrument_reversed passes::dump std::nvm

echo "===== the reading the edit's choice is checked against ====="
run program.cml passes::dump std::nvm