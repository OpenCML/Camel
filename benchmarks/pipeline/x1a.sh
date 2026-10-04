#!/usr/bin/env bash
# X1a: pipeline invariance on the existing programs (test cases, exemplars, benchmark models),
# the ONNX terminal for programs with an export entry, and the must-refuse set.
#
#   benchmarks/pipeline/x1a.sh <out-dir> [run.py options, e.g. --jobs 10 --shard 0/4]
#
# Needs a built Camel (out/latest/bin/camel, or CAMEL_EXE) and Python >= 3.11. numpy is needed
# for the benchmark models (weights are generated into <out-dir>/bench-artifacts) and numpy +
# onnxruntime (+ onnx for the checker) for the ONNX cells; without onnxruntime those cells are
# classified UNDECIDED. Set PYTHON to choose the interpreter. The repository root is the
# checkout this script is in, unless CAMEL_REPO_ROOT says otherwise.
#
# Writes <out-dir>/{corpus.json, cells*.json, run-meta*.json, raw/, records/, files/, summary/}.
# Exit code 0 means the run completed and every cell (program, pipeline, repeat) listed in
# cells*.json has a record; it says nothing about the outcomes, which are in summary/.
#
# Sharding: run `x1a.sh <out-dir> --shard K/N` for K = 0..N-1 (same or different machines; the
# shards write disjoint files, so they may share <out-dir> or be copied into one afterwards),
# then classify everything once:
#   python3 benchmarks/pipeline/classify.py --records <out-dir>/records --out <out-dir>/summary
# With --shard, the summary written here covers the records present in <out-dir> so far.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: x1a.sh <out-dir> [run.py options]}"
shift
py="${PYTHON:-python3}"
mkdir -p "$out"
"$py" "$here/corpus.py" --out "$out/corpus.json"
"$py" "$here/run.py" --corpus "$out/corpus.json" --out "$out" "$@"
"$py" - "$out" <<'EOF'
import json, sys
from pathlib import Path
out = Path(sys.argv[1])
expected = found = 0
for manifest in sorted(out.glob("cells*.json")):
    for program, pipeline, repeat in json.loads(manifest.read_text()):
        expected += 1
        found += (out / "records" / program / f"{pipeline}.r{repeat}.json").exists()
print(f"records: {found} / expected {expected}")
sys.exit(0 if found == expected and expected > 0 else 1)
EOF
"$py" "$here/classify.py" --records "$out/records" --out "$out/summary" > "$out/summary.stdout.json"
cat "$out/summary.stdout.json"
echo "per-pipeline table: $out/summary/by_pipeline.csv; must-refuse: $out/summary/refusal_by_pipeline.csv"
