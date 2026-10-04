#!/usr/bin/env bash
# X1a: pipeline invariance on the existing programs (test cases and exemplars).
#
#   benchmarks/pipeline/x1a.sh <out-dir> [extra run.py options, e.g. --jobs 8 --timeout 60]
#
# Needs a built Camel (out/latest/bin/camel, or CAMEL_EXE) and Python >= 3.11 (standard
# library only). Writes <out-dir>/{corpus.json, run-meta.json, raw/, records/, summary/}.
# Exit code 0 means the run completed and every (program, pipeline, repeat) has a record; it
# says nothing about the outcomes, which are in summary/.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: x1a.sh <out-dir> [run.py options]}"
shift
mkdir -p "$out"
python3 "$here/corpus.py" --out "$out/corpus.json"
python3 "$here/run.py" --corpus "$out/corpus.json" --out "$out" "$@"
python3 "$here/classify.py" --records "$out/records" --out "$out/summary" > "$out/summary.stdout.json"
python3 - "$out" <<'EOF'
import json, sys
from pathlib import Path
out = Path(sys.argv[1])
meta = json.loads((out / "run-meta.json").read_text())
expected = meta["programs"] * len(meta["pipelines"]) * meta["repeats"]
found = sum(1 for _ in (out / "records").rglob("*.json"))
print(f"records: {found} / expected {expected}")
sys.exit(0 if found == expected else 1)
EOF
cat "$out/summary.stdout.json"
