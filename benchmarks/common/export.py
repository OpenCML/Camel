"""Export one model to ONNX with one framework exporter, in a fresh process.

``python common/export.py --model M --exporter torch|tf`` writes
``artifacts/<model>/<exporter>.onnx`` and prints ``RESULT {json}`` with the
export wall time (framework import excluded) so run.py can report it
separately from ONNX Runtime session creation.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent.parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common import configs as cfgs  # noqa: E402
from common.weights import load  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--exporter", required=True, choices=["torch", "tf"])
    ap.add_argument("--threads", type=int, default=4)
    args = ap.parse_args()

    weights, x = load(args.model)
    framework = "torch" if args.exporter == "torch" else "tf"
    __import__("torch" if framework == "torch" else "tensorflow")
    cfgs.pin_framework_threads(framework, args.threads)

    t0 = time.perf_counter()
    path = cfgs.export(args.model, args.exporter, weights, x)
    export_s = time.perf_counter() - t0
    print("RESULT " + json.dumps({"export_s": export_s, "path": str(path)}), flush=True)


if __name__ == "__main__":
    main()
