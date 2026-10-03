"""E1 helper: run Camel-native multi-step training and collect its losses.

Invokes ``.cache/out-stable/bin/camel.exe`` on ``native_train_<model>.cml``
(default pass: std::nvm), with the initial weights from the benchmark
artifacts and the pre-split batches from make_data.py. The Camel process
prints ``CAMEL_BENCH step=<i> loss=<v>`` lines and dumps per-step updated
parameters under ``<out>/trace/step_<i>/``; this wrapper parses the losses
into ``<out>/losses.npy``.

Usage:
  python run_native.py --model mlp --steps 100 --out out/runs/mlp/native
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]  # repo root
CAMEL_HOME = ROOT / ".cache" / "out-stable"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", required=True, choices=["mlp", "transformer"])
    p.add_argument("--data", default="", help="batches dir (default: out/data/<model>/batches)")
    p.add_argument("--steps", type=int, default=100)
    p.add_argument("--passes", default="std::macro",
                   help="CLI passes before the std::nvm fallback (zero passes segfault the "
                        "transformer graph in this binary, so macro expansion runs first)")
    p.add_argument("--out", required=True, help="output dir for losses.npy and trace/")
    args = p.parse_args()

    here = Path(__file__).resolve().parent
    batches = Path(args.data) if args.data else here / "out" / "data" / args.model / "batches"
    nb = len(list(batches.glob("batch_*.npy")))
    if nb == 0:
        raise SystemExit(f"no batch_*.npy in {batches} (run make_data.py first)")
    out_dir = Path(args.out)
    (out_dir / "trace").mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.update(
        {
            "CAMEL_HOME": str(CAMEL_HOME),
            "CAMEL_PACKAGES": str(ROOT / "benchmarks" / "common"),
            "CAMEL_BENCH_WEIGHTS": str(ROOT / "benchmarks" / "artifacts" / args.model / "weights"),
            "CAMEL_FT_BATCHES": str(batches.resolve()),
            "CAMEL_FT_NBATCHES": str(nb),
            "CAMEL_FT_STEPS": str(args.steps),
            "CAMEL_FT_DUMP": str((out_dir / "trace").resolve()),
        }
    )
    cmd = [str(CAMEL_HOME / "bin" / "camel.exe"), str(here / f"native_train_{args.model}.cml")]
    cmd += [s for s in args.passes.split(",") if s]
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=3600)
    sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        sys.stdout.write(proc.stdout)
        raise SystemExit(f"camel exited with {proc.returncode}")

    losses = {}
    for line in proc.stdout.splitlines():
        m = re.match(r"CAMEL_BENCH step=(\d+) loss=([-+0-9.eE]+)", line.strip())
        if m:
            losses[int(m.group(1))] = float(m.group(2))
    if sorted(losses) != list(range(args.steps)):
        sys.stdout.write(proc.stdout[-2000:])
        raise SystemExit(f"expected steps 0..{args.steps - 1}, got {sorted(losses)}")
    seq = np.asarray([losses[i] for i in range(args.steps)], dtype=np.float64)
    np.save(out_dir / "losses.npy", seq)
    print(f"[run_native] {args.model}: {args.steps} steps, "
          f"loss {seq[0]:.6g} -> {seq[-1]:.6g}, trace in {out_dir / 'trace'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
