"""Plug-in driver for native Camel configurations (camel_native / camel_fvm / camel_jit).

Runs ``camel <benchmarks/<model>/model.cml> [passes...]`` from ``benchmarks/<model>/``
and parses its stdout. Protocol expected from ``model.cml`` (see README.md):

Environment provided to the Camel process:
  CAMEL_HOME            <repo>/out/latest (unless already set)
  CAMEL_PACKAGES        <repo>/benchmarks/common (the shared camel_bench driver module)
  CAMEL_BENCH_WEIGHTS   absolute path of artifacts/<model>/weights/
  CAMEL_BENCH_INPUT     absolute path of artifacts/<model>/input.npy
  CAMEL_BENCH_OUTPUT    absolute path where the model may save its output .npy
  CAMEL_BENCH_WARMUP    number of untimed warmup iterations
  CAMEL_BENCH_REPS      number of timed iterations
  CAMEL_BENCH_THREADS   thread budget (OMP_NUM_THREADS etc. are also set)

Stdout lines starting with ``CAMEL_BENCH`` carry ``key=value`` pairs:
  CAMEL_BENCH latency_ms=<float>        one line per timed rep (preferred), or a
                                        single summary line, optionally with
                                        p10_ms=<float> p90_ms=<float>
  CAMEL_BENCH compile_ms=<float>        optional: first-call overhead
  CAMEL_BENCH warmup_ms=<float>         optional: total warmup time
All other output is ignored.
"""

from __future__ import annotations

import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence

from common.configs import REPO_ROOT, camel_binary, camel_model
from common.memory import run_with_peak_rss
from common.weights import model_dir


@dataclass
class CamelResult:
    latencies_ms: List[float] = field(default_factory=list)
    summary: Dict[str, float] = field(default_factory=dict)
    wall_s: float = 0.0
    peak_rss_kb: Optional[int] = None
    output_path: Optional[Path] = None


def parse_bench_lines(stdout: str) -> CamelResult:
    res = CamelResult()
    for line in stdout.splitlines():
        line = line.strip()
        if not line.startswith("CAMEL_BENCH"):
            continue
        kv = dict(tok.split("=", 1) for tok in line.split()[1:] if "=" in tok)
        for key, val in kv.items():
            try:
                num = float(val)
            except ValueError:
                continue
            if key == "latency_ms":
                res.latencies_ms.append(num)
            else:
                res.summary[key] = num
    return res


def run_camel(model: str, passes: Sequence[str], threads: int, warmup: int, reps: int, env: Dict[str, str]) -> CamelResult:
    mdir = model_dir(model)
    out_npy = mdir / "camel_output.npy"
    if out_npy.exists():
        out_npy.unlink()
    child_env = dict(os.environ)
    child_env.update(env)
    child_env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    # Models import the shared driver module `camel_bench` from benchmarks/common.
    packages = str(REPO_ROOT / "benchmarks" / "common")
    existing = child_env.get("CAMEL_PACKAGES")
    child_env["CAMEL_PACKAGES"] = packages + (os.pathsep + existing if existing else "")
    child_env.update(
        {
            "CAMEL_BENCH_WEIGHTS": str(mdir / "weights"),
            "CAMEL_BENCH_INPUT": str(mdir / "input.npy"),
            "CAMEL_BENCH_OUTPUT": str(out_npy),
            "CAMEL_BENCH_WARMUP": str(warmup),
            "CAMEL_BENCH_REPS": str(reps),
            "CAMEL_BENCH_THREADS": str(threads),
        }
    )
    cml = camel_model(model)
    cmd = [str(camel_binary()), str(cml), *passes]
    t0 = time.perf_counter()
    returncode, stdout, stderr, peak_kb = run_with_peak_rss(cmd, cwd=cml.parent, env=child_env)
    wall = time.perf_counter() - t0
    if returncode != 0:
        raise RuntimeError(f"camel exited with {returncode}: {stderr.strip()[-2000:]}")
    res = parse_bench_lines(stdout)
    if not res.latencies_ms:
        raise RuntimeError("camel produced no `CAMEL_BENCH latency_ms=...` line")
    res.wall_s = wall
    res.peak_rss_kb = peak_kb
    res.output_path = out_npy if out_npy.exists() else None
    return res
