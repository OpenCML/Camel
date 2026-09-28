"""Numerical agreement check of every available configuration vs the NumPy reference.

For each model, runs every applicable configuration once on ``input.npy`` and
compares the output against ``common/reference_numpy.py`` with
``np.allclose(out, ref, rtol=1e-4, atol=1e-5)``, reporting the max absolute
error. ONNX files are exported first when missing (or always with --export).
Camel configurations are skipped with a message until their prerequisites
exist. Exits non-zero if any configuration disagrees or fails.

Usage:  python common/check.py [--models ...] [--configs ...] [--export]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Tuple

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parent.parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

RTOL = 1e-4
ATOL = 1e-5


def compare(out: np.ndarray, ref: np.ndarray) -> Tuple[float, bool]:
    """Return (max abs error, allclose) of ``out`` against ``ref``."""
    out = np.asarray(out)
    if out.shape != ref.shape:
        return float("inf"), False
    err = float(np.max(np.abs(out.astype(np.float64) - ref.astype(np.float64))))
    return err, bool(np.allclose(out, ref, rtol=RTOL, atol=ATOL))


def main() -> int:
    from common import configs as cfgs
    from common.models import MODELS
    from common.reference_numpy import FORWARD
    from common.weights import load

    all_configs = [c for c in cfgs.CONFIGS]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", default=",".join(MODELS))
    ap.add_argument("--configs", default=",".join(all_configs))
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--export", action="store_true", help="re-export ONNX files even if present")
    args = ap.parse_args()

    # check.py hosts both frameworks in one process. Importing triton (pulled in
    # by torch._dynamo) after TensorFlow segfaults with the torch 2.14 / TF 2.21
    # wheels, so the torch compile/export stacks must be loaded first.
    # run.py is unaffected: it measures each config in its own process.
    import torch._dynamo  # noqa: F401
    import torch.onnx  # noqa: F401

    for fw in ("torch", "tf"):
        cfgs.pin_framework_threads(fw, args.threads)

    rows = []
    failed = False
    for model in args.models.split(","):
        weights, x = load(model)
        ref = FORWARD[model](weights, x)
        for config in args.configs.split(","):
            if not cfgs.applies(config, model):
                continue
            reason = cfgs.skip_reason(config, model)
            if reason:
                rows.append((model, config, "-", "skipped", reason))
                continue
            cfg = cfgs.CONFIGS[config]
            try:
                if cfg.framework == "camel":
                    from common.camel import run_camel

                    res = run_camel(model, cfg.camel_passes, args.threads, 0, 1, cfgs.thread_env(args.threads))
                    if res.output_path is None:
                        rows.append((model, config, "-", "n/a", "model.cml wrote no CAMEL_BENCH_OUTPUT"))
                        continue
                    out = np.load(res.output_path)
                else:
                    if cfg.exporter in ("torch", "tf"):
                        path = cfgs.onnx_path(model, cfg.exporter)
                        if args.export or not path.exists():
                            cfgs.export(model, cfg.exporter, weights, x)
                    out = cfgs.build_runner(config, model, weights, args.threads)(x)
                err, ok = compare(out, ref)
                rows.append((model, config, f"{err:.3e}", "PASS" if ok else "FAIL", ""))
                failed |= not ok
            except Exception as exc:  # report and continue with other configs
                rows.append((model, config, "-", "ERROR", f"{type(exc).__name__}: {exc}"[:200]))
                failed = True

    header = ("model", "config", "max_abs_err", "result", "note")
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(4)]
    print(f"tolerance: rtol={RTOL:g}, atol={ATOL:g} (vs NumPy float64 reference)")
    print("  ".join(h.ljust(w) for h, w in zip(header, widths)) + "  note")
    for r in rows:
        print("  ".join(str(v).ljust(w) for v, w in zip(r, widths)) + ("  " + r[4] if r[4] else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
