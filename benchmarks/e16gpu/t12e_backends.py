"""T12e cross-backend numerics net (MLSys 2027 eval).

One single training step of each exported graph, executed unmodified by three
independent backends on the server, outputs compared three ways:

  ORT CPU EP  (MLAS kernels)        \
  OpenVINO CPU (Intel's own kernels)  -> CPU-family pair, tolerance 1e-5
  ORT CUDA EP (cuBLAS/cuDNN kernels)  -> GPU-involved pairs, tolerance 1e-4
                                       (float32 accumulation-order noise)

Models: the E3 bundle three (mlp, gru, transformer) plus resnetbn (T4 graph)
as a stretch — any backend failure is archived verbatim under errors/ and
reported as data. Relative errors are scaled by max|ORT-CPU output| per tensor
(E3 convention). TensorRT is absent from the net by prior record
(artifacts/e3/config6_tensorrt_fail.txt: libnvinfer.so.10 missing, nvidia feed
unreachable) — three backends it is.

Server usage:
  venv-bench/bin/python benchmarks/e16gpu/t12e_backends.py --out ~/camel-eval/e16gpu/t12e
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gpuenv  # noqa: E402

MODELS = ("mlp", "gru", "transformer", "resnetbn")
RTOL_CPU = 1e-5
RTOL_GPU = 1e-4


def run_model(model: str, graph: Path, errors: Path) -> list[dict]:
    import numpy as np
    import onnxruntime as ort

    feeds = gpuenv.load_feeds(model, graph)
    rows = []

    def sess_ort(providers, strict_cuda=False):
        opts = ort.SessionOptions()
        if strict_cuda:
            opts.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
        return ort.InferenceSession(str(graph), sess_options=opts, providers=providers)

    results: dict[str, dict[str, np.ndarray]] = {}

    try:
        s = sess_ort(["CPUExecutionProvider"])
        out_names = [o.name for o in s.get_outputs()]
        results["cpu"] = dict(zip(out_names, s.run(None, feeds)))
    except Exception as exc:
        (errors / f"{model}_ort_cpu.log").write_text(str(exc))
        return [{"model": model, "output": "*", "status": "failed", "error": f"ort cpu: {str(exc)[:200]}"}]

    cuda_mode = "strict"
    try:
        s = sess_ort(["CUDAExecutionProvider"], strict_cuda=True)
        results["cuda"] = dict(zip(out_names, s.run(None, feeds)))
    except Exception as exc:
        cuda_mode = "failed"
        (errors / f"{model}_ort_cuda.log").write_text(str(exc))
        rows.append({"model": model, "output": "*", "status": "cuda_session_failed", "error": str(exc)[:200]})

    try:
        import openvino as ov

        core = ov.Core()
        compiled = core.compile_model(str(graph), "CPU")
        req = compiled.create_infer_request()
        req.infer(feeds)
        ov_outs = {}
        for out in compiled.outputs:
            data = req.get_tensor(out).data
            names = out.get_names()
            key = next((n for n in names if n in out_names), None)
            ov_outs[key] = np.asarray(data)
        missing = [n for n in out_names if n not in ov_outs]
        if missing:
            # fall back to positional matching
            for i, out in enumerate(compiled.outputs):
                if i < len(out_names) and out_names[i] not in ov_outs:
                    ov_outs[out_names[i]] = np.asarray(req.get_tensor(out).data)
        results["ov"] = {n: ov_outs[n] for n in out_names if n in ov_outs}
    except Exception as exc:
        import traceback

        (errors / f"{model}_openvino.log").write_text(traceback.format_exc())
        rows.append({"model": model, "output": "*", "status": "ov_failed", "error": str(exc)[:200]})

    if len(results) < 2:
        return rows
    have_cuda = "cuda" in results
    have_ov = "ov" in results
    for name in out_names:
        cpu = results["cpu"].get(name)
        if cpu is None:
            continue
        scale = max(float(np.max(np.abs(cpu))), 1e-12)

        def rel(other: str) -> float | None:
            arr = results.get(other, {}).get(name)
            if arr is None or arr.shape != cpu.shape:
                return None
            return float(np.max(np.abs(cpu - arr))) / scale

        r_ov = rel("ov") if have_ov else None
        r_cuda = rel("cuda") if have_cuda else None
        r_ov_cuda = None
        if have_cuda and have_ov and name in results["ov"] and results["ov"][name].shape == results["cuda"][name].shape:
            s2 = max(float(np.max(np.abs(results["ov"][name]))), 1e-12)
            r_ov_cuda = float(np.max(np.abs(results["ov"][name] - results["cuda"][name]))) / s2
        ok = True
        if r_ov is not None:
            ok &= r_ov <= RTOL_CPU
        if r_cuda is not None:
            ok &= r_cuda <= RTOL_GPU
        if r_ov_cuda is not None:
            ok &= r_ov_cuda <= RTOL_GPU
        rows.append(
            {
                "model": model,
                "output": name,
                "status": "ok" if ok else "MISMATCH",
                "cpu_vs_ov": f"{r_ov:.2e}" if r_ov is not None else "",
                "cpu_vs_cuda": f"{r_cuda:.2e}" if r_cuda is not None else "",
                "ov_vs_cuda": f"{r_ov_cuda:.2e}" if r_ov_cuda is not None else "",
                "cuda_mode": cuda_mode,
            }
        )
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", default=",".join(MODELS))
    ap.add_argument("--resnetbn-graph", default="")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    gpuenv.ensure_loader_env(inprocess_ort=True)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    errors = out / "errors"
    errors.mkdir(parents=True, exist_ok=True)

    import csv

    all_rows = []
    for model in [m for m in args.models.split(",") if m]:
        graph = gpuenv.graph_path(model, Path(args.resnetbn_graph) if args.resnetbn_graph else None)
        if not graph.exists():
            all_rows.append({"model": model, "output": "*", "status": "graph_missing", "error": str(graph)})
            continue
        rows = run_model(model, graph, errors)
        all_rows.extend(rows)
        bad = [r for r in rows if r.get("status") not in ("ok",)]
        worst_cpu_ov = max((float(r["cpu_vs_ov"]) for r in rows if r.get("cpu_vs_ov")), default=None)
        worst_gpu = max(
            (float(x) for r in rows for x in (r.get("cpu_vs_cuda"), r.get("ov_vs_cuda")) if x), default=None
        )
        print(
            f"[t12e] {model:12s} outputs {len([r for r in rows if r.get('output') != '*'])} "
            f"worst cpu/ov {worst_cpu_ov and f'{worst_cpu_ov:.2e}'}  worst gpu-pair {worst_gpu and f'{worst_gpu:.2e}'}  "
            f"{'FAILURES: ' + '; '.join(sorted({r['status'] for r in bad})) if bad else 'all ok'}",
            flush=True,
        )

    cols = ["model", "output", "status", "cpu_vs_ov", "cpu_vs_cuda", "ov_vs_cuda", "cuda_mode", "error"]
    with (out / "t12e.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(all_rows)

    n_ok = len([r for r in all_rows if r.get("status") == "ok"])
    n_bad = len([r for r in all_rows if r.get("status") not in ("ok", None)])
    (out / "t12e_summary.txt").write_text(
        f"T12e cross-backend single training step: ORT CPU EP vs OpenVINO CPU vs ORT CUDA EP\n"
        f"tolerances: CPU-family {RTOL_CPU:.0e}, GPU-involved {RTOL_GPU:.0e} (relative to max|ORT-CPU output|)\n"
        f"outputs ok: {n_ok}, failed/mismatch: {n_bad}\n"
        f"TensorRT absent by prior record (artifacts/e3/config6_tensorrt_fail.txt); three-backend net.\n"
    )
    gpuenv.write_meta(out, {
        "subitem": "T12e",
        "framing": "one exported graph, three independent backends, single-step numerics net; "
                   "artifact portability evidence, not a performance claim",
        "models": args.models, "rtol_cpu_family": RTOL_CPU, "rtol_gpu_involved": RTOL_GPU,
    })
    print(f"[done] wrote {out}/t12e.csv, t12e_summary.txt, meta.json")
    return 0 if n_bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
