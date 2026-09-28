"""Measure one (model, configuration) pair inside a fresh process.

Invoked by run.py as ``python common/worker.py --model M --config C ...`` with
thread-pinning environment variables already set, so that peak RSS and
first-call overhead are not polluted by other configurations. Prints exactly
one ``RESULT {json}`` line on stdout.

Timeline measured (all wall-clock, time.perf_counter):
  import_s   importing the framework module for this config
  load_s     reading weights/input .npy files
  compile_s  building the runner + the first call (tracing, torch.compile,
             XLA compilation, ORT session creation + graph optimization)
  warmup_s   total of the ``--warmup`` calls after the first call
  latency    each of ``--reps`` calls timed individually (NumPy in -> NumPy out,
             input conversion excluded, output materialization included)
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

import numpy as np  # noqa: E402

from common import configs as cfgs  # noqa: E402
from common.check import compare  # noqa: E402
from common.memory import peak_rss_self_kb  # noqa: E402
from common.models import MODELS  # noqa: E402
from common.reference_numpy import FORWARD  # noqa: E402
from common.weights import load  # noqa: E402

_FRAMEWORK_MODULE = {"torch": "torch", "tf": "tensorflow", "ort": "onnxruntime"}


def latency_stats(samples_ms: list, batch: int) -> dict:
    a = np.asarray(samples_ms, dtype=np.float64)
    median = float(np.median(a))
    return {
        "latency_median_ms": median,
        "latency_p10_ms": float(np.percentile(a, 10)),
        "latency_p90_ms": float(np.percentile(a, 90)),
        "latency_mean_ms": float(a.mean()),
        "throughput_sps": batch / (median / 1000.0) if median > 0 else float("nan"),
    }


def measure_inprocess(args, cfg, weights, x) -> dict:
    t0 = time.perf_counter()
    __import__(_FRAMEWORK_MODULE[cfg.framework])
    cfgs.pin_framework_threads(cfg.framework, args.threads, args.interop)
    import_s = time.perf_counter() - t0

    t0 = time.perf_counter()
    runner = cfgs.build_runner(args.config, args.model, weights, args.threads, args.interop)
    inp = runner.prepare(x)
    out = runner.call(inp)
    compile_s = time.perf_counter() - t0

    t0 = time.perf_counter()
    for _ in range(args.warmup):
        runner.call(inp)
    warmup_s = time.perf_counter() - t0

    samples = []
    for _ in range(args.reps):
        t = time.perf_counter()
        runner.call(inp)
        samples.append((time.perf_counter() - t) * 1000.0)

    return {"import_s": import_s, "compile_s": compile_s, "warmup_s": warmup_s, "samples": samples, "output": out}


def measure_camel(args, cfg) -> dict:
    from common.camel import run_camel

    res = run_camel(args.model, cfg.camel_passes, args.threads, args.warmup, args.reps, cfgs.thread_env(args.threads, args.interop))
    s = res.summary
    return {
        "import_s": None,
        "compile_s": s["compile_ms"] / 1000.0 if "compile_ms" in s else None,
        "warmup_s": s["warmup_ms"] / 1000.0 if "warmup_ms" in s else None,
        "samples": res.latencies_ms,
        "summary": s,
        "output": np.load(res.output_path) if res.output_path else None,
        "peak_rss_kb": res.peak_rss_kb,
        "notes": f"process_wall_s={res.wall_s:.3f}",
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--config", required=True)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--interop", type=int, default=1)
    ap.add_argument("--warmup", type=int, default=10)
    ap.add_argument("--reps", type=int, default=50)
    args = ap.parse_args()

    cfg = cfgs.CONFIGS[args.config]
    spec = MODELS[args.model]

    t0 = time.perf_counter()
    weights, x = load(args.model)
    load_s = time.perf_counter() - t0

    m = measure_camel(args, cfg) if cfg.framework == "camel" else measure_inprocess(args, cfg, weights, x)

    result = {"model": args.model, "config": args.config, "status": "ok", "load_s": load_s}
    result.update({k: m.get(k) for k in ("import_s", "compile_s", "warmup_s")})
    result.update(latency_stats(m["samples"], spec.batch))
    # Summary-line percentiles from Camel override single-sample stats.
    for key in ("p10_ms", "p90_ms"):
        if key in m.get("summary", {}) and len(m["samples"]) == 1:
            result[f"latency_{key}"] = m["summary"][key]
    peak_kb = m.get("peak_rss_kb") if cfg.framework == "camel" else peak_rss_self_kb()
    result["peak_rss_mb"] = peak_kb / 1024.0 if peak_kb else None

    out = m["output"]
    if out is None:
        result.update({"max_abs_err": None, "agree": "n/a"})
    else:
        err, ok = compare(out, FORWARD[args.model](weights, x))
        result.update({"max_abs_err": err, "agree": "yes" if ok else "NO"})
    result["notes"] = m.get("notes", "")
    print("RESULT " + json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
