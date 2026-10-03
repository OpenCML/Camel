"""E3 GPU portability check (MLSys 2027 eval, §A.4 framing).

One exported training-step graph, byte-identical, executed by ONNX Runtime's
CPU and CUDA execution providers; outputs are compared against each other and
against Camel-native reference values. This measures the *quality of the
exported artifact* — all GPU-side compute is ORT's kernels. Latency on the GPU
backend is recorded as context only, never as a Camel performance claim.

Two phases, split because camel runs on the dev machine while the GPUs live on
the server:

  prepare   (dev machine, needs camel + the benchmark artifacts):
              exports the train step of each model to <bundle>/<model>_step.onnx
              writes <bundle>/<model>_feeds.npz  (graph inputs: weights + input1)
              writes <bundle>/<model>_ref.npz    (Camel-native loss + w - lr*grad)

  run       (server, needs onnxruntime-gpu + numpy):
              loads each graph twice (CPU EP, CUDA EP), compares numerics,
              times a few dozen steps per backend as context, writes
              <bundle>/e3_results.csv (+ .json meta) and prints the table.

Usage:
  python e3_portability.py prepare --models mlp,gru,transformer --bundle .cache/e3/bundle
  python e3_portability.py run --bundle ~/camel-eval/e3/bundle [--steps 50]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))


def prepare(models: list[str], bundle: Path, rtol: float) -> None:
    import numpy as np
    import onnx

    import train  # benchmarks/train.py — reuse its camel plumbing read-only
    from common.weights import load

    bundle.mkdir(parents=True, exist_ok=True)
    for model in models:
        weights, x = load(model)
        path = bundle / f"{model}_step.onnx"
        train.run_camel(model, (), {"CAMEL_BENCH_WORKLOAD": "train_export", "CAMEL_BENCH_EXPORT": str(path)})
        proto = onnx.load(str(path))
        onnx.checker.check_model(proto, full_check=True)
        ref_loss = train.camel_loss(model)
        grads = train.camel_gradients(model)
        np.savez(bundle / f"{model}_feeds.npz", **weights, input1=x)
        np.savez(
            bundle / f"{model}_ref.npz",
            loss=np.asarray(ref_loss),
            **{name: weights[name] - train.LR * grads[name] for name in weights},
        )
        print(f"[prepare] {model}: {len(proto.graph.node)} nodes, checker ok -> {path}")


def run(bundle: Path, steps: int, rtol: float, rtol_xep: float) -> bool:
    import numpy as np
    import onnxruntime as ort

    ok = True
    rows = []
    print(f"onnxruntime {ort.__version__}; providers seen: {ort.get_available_providers()}")
    for onnx_path in sorted(bundle.glob("*_step.onnx")):
        model = onnx_path.name[: -len("_step.onnx")]
        feeds_npz = np.load(bundle / f"{model}_feeds.npz")
        ref = np.load(bundle / f"{model}_ref.npz")
        feeds = {k: feeds_npz[k] for k in feeds_npz.files}

        per_ep: dict[str, dict[str, np.ndarray]] = {}
        latency: dict[str, float] = {}
        partial_cuda = False
        for ep in ["CPUExecutionProvider", "CUDAExecutionProvider"]:
            try:
                opts = ort.SessionOptions()
                if ep == "CUDAExecutionProvider":
                    # Strict first: refuse CPU fallback so any op without a CUDA
                    # kernel fails session creation instead of silently running
                    # on CPU (which would fake the CUDA leg).
                    opts.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
                session = ort.InferenceSession(str(onnx_path), sess_options=opts, providers=[ep])
            except Exception as exc:
                if ep == "CUDAExecutionProvider":
                    # Boundary annotation (E3 acceptance): record the strict failure
                    # verbatim, then retry permissively — partial CPU offload still
                    # counts as unmodified execution, but is marked cuda_partial.
                    print(f"[run] {model} {ep} strict: {str(exc)[:200]}")
                    print(f"[run] {model} {ep}: retrying with CPU fallback allowed (cuda_partial)")
                    partial_cuda = True
                    try:
                        session = ort.InferenceSession(str(onnx_path), providers=[ep])
                    except Exception as exc2:
                        print(f"[run] {model} {ep}: SESSION FAILED: {exc2}")
                        ok = False
                        rows.append({"model": model, "ep": ep, "status": "session_failed", "error": str(exc2)[:300]})
                        continue
                else:
                    print(f"[run] {model} {ep}: SESSION FAILED: {exc}")
                    ok = False
                    rows.append({"model": model, "ep": ep, "status": "session_failed", "error": str(exc)[:300]})
                    continue
            outs = dict(zip([o.name for o in session.get_outputs()], session.run(None, feeds)))
            per_ep[ep] = outs
            for _ in range(3):
                session.run(None, feeds)  # warmup
            t0 = time.perf_counter()
            for _ in range(steps):
                session.run(None, feeds)
            latency[ep] = (time.perf_counter() - t0) / steps * 1000.0

        if len(per_ep) < 2:
            continue
        cpu, cuda = per_ep["CPUExecutionProvider"], per_ep["CUDAExecutionProvider"]
        for out_name in sorted(cpu):
            ref_arr = ref["loss"] if out_name == "output_0" else ref[out_name[len("output_1_"):]]
            scale = max(float(np.max(np.abs(ref_arr))), 1e-12)
            cc = float(np.max(np.abs(cpu[out_name] - cuda[out_name]))) / scale
            cr = float(np.max(np.abs(cpu[out_name] - ref_arr))) / scale
            gr = float(np.max(np.abs(cuda[out_name] - ref_arr))) / scale
            # Two tolerances, two different physical quantities:
            #  cpu_vs_ref <= rtol  : the artifact is numerically correct (the
            #      paper's A.2 claim leg — ORT CPU vs Camel native, ~1e-8 here)
            #  cpu_vs_cuda <= rtol_xep : cross-backend float32 accumulation-order
            #      noise (cuBLAS vs MLAS reduction tiling on large sums); cuda_vs_ref
            #      is the same noise and is reported for information only.
            status = "ok" if cr <= rtol and cc <= rtol_xep else "MISMATCH"
            ok &= status == "ok"
            rows.append(
                {
                    "model": model,
                    "output": out_name,
                    "status": status,
                    "cpu_vs_cuda": f"{cc:.2e}",
                    "cpu_vs_ref": f"{cr:.2e}",
                    "cuda_vs_ref": f"{gr:.2e}",
                    "cpu_ms": f"{latency['CPUExecutionProvider']:.3f}",
                    "cuda_ms": f"{latency['CUDAExecutionProvider']:.3f}",
                    "cuda_mode": "partial" if partial_cuda else "strict",
                }
            )
        worst = max(rows[-len(cpu) :], key=lambda r: float(r["cpu_vs_cuda"]))
        print(
            f"[run] {model}: {len(cpu)} outputs, worst cpu/cuda {worst['cpu_vs_cuda']} ({worst['output']}), "
            f"cuda_mode={'partial' if partial_cuda else 'strict'}; "
            f"step {latency['CPUExecutionProvider']:.3f} ms CPU, {latency['CUDAExecutionProvider']:.3f} ms CUDA (context only)"
        )

    import csv

    cols = ["model", "output", "status", "cpu_vs_cuda", "cpu_vs_ref", "cuda_vs_ref", "cpu_ms", "cuda_ms", "cuda_mode"]
    out_csv = bundle / "e3_results.csv"
    with out_csv.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    meta = {
        "ort_version": ort.__version__,
        "steps_per_backend": steps,
        "rtol": rtol,
        "rtol_xep": rtol_xep,
        "note": "Same .onnx file for both providers, zero modification. GPU latency is context, not a Camel claim.",
    }
    (bundle / "e3_results.json").write_text(json.dumps(meta, indent=2))
    print(f"[done] wrote {out_csv}; overall: {'PASS' if ok else 'FAIL'}")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("phase", choices=["prepare", "run"])
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--steps", type=int, default=50)
    parser.add_argument("--rtol", type=float, default=1e-5, help="artifact-vs-Camel-native tolerance (the A.2 claim leg)")
    parser.add_argument("--rtol-xep", type=float, default=1e-4, help="CPU-vs-CUDA float32 accumulation-order tolerance")
    args = parser.parse_args()
    if args.phase == "prepare":
        prepare([m for m in args.models.split(",") if m], args.bundle, args.rtol)
        return 0
    return 0 if run(args.bundle, args.steps, args.rtol, args.rtol_xep) else 1


if __name__ == "__main__":
    sys.exit(main())
