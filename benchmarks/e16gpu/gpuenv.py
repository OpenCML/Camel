# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Shared plumbing for the T12 GPU deployment-overhead measurements (MLSys 2027).

Framing (applies to every T12 sub-item): all GPU numbers describe "ONNX Runtime
executing our exported training-step graph" versus "the PyTorch framework
training the same model". What is measured is the *artifact's deployment
overhead* and the *framework's resident cost* — never a kernel-performance
contest between Camel and Inductor/XLA. Throughput/speed figures are context
only. Recursive structures (GRU) would attribute cuDNN fused kernels if used.

Server layout (defaults, overridable by env vars):
  E16_REPO      ~/camel-eval/Camel          (benchmarks + artifacts + out/latest)
  E16_BUNDLE    ~/camel-eval/e3/bundle      (E3 step graphs + feeds + refs)
  E16_OUT       ~/camel-eval/e16gpu         (measurement output root)
  E16_LD        LD_LIBRARY_PATH needed by ORT's CUDA EP (cuDNN 9 + cudart 12.3)
  CUDA_VISIBLE_DEVICES  set by the caller; default "1" (GPU0 belongs to others)

Children that create ORT CUDA sessions need E16_LD in the loader environment at
process start; ``ensure_loader_env`` re-execs the current process when ORT must
be imported in-process, and ``child_env`` patches the environment for children.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
import time
from importlib import metadata
from pathlib import Path

HOME = Path.home()
REPO = Path(os.environ.get("E16_REPO", HOME / "camel-eval" / "Camel"))
BENCH = REPO / "benchmarks"
BUNDLE = Path(os.environ.get("E16_BUNDLE", HOME / "camel-eval" / "e3" / "bundle"))
OUT_ROOT = Path(os.environ.get("E16_OUT", HOME / "camel-eval" / "e16gpu"))
CAMEL = REPO / "out" / "latest" / "bin" / "camel"

ORT_LD = os.environ.get(
    "E16_LD",
    str(HOME / "camel-eval" / "venv-bench" / "lib" / "python3.10" / "site-packages" / "nvidia" / "cudnn" / "lib")
    + ":/usr/local/cuda-12.3/lib64",
)

# Benchmark weights for the E3 graphs are exactly artifacts/<model>/weights (the
# E3 prepare phase serialized them into feeds npz); resnetbn has no bundle entry
# because its graph comes from T4, so its feeds are built from the artifacts.
E3_MODELS = ("mlp", "gru", "transformer")
RESNETBN_GRAPH_DEFAULT = OUT_ROOT / "graphs" / "resnetbn-step.onnx"

ROW = "T12ROW "


def ensure_loader_env(inprocess_ort: bool) -> None:
    """Make sure ORT's CUDA EP will find cuDNN/cudart in this process and children.

    The dynamic loader reads LD_LIBRARY_PATH at process start, so when ORT is
    imported in this very process we must re-exec once with the patched env.
    """
    ld = os.environ.get("LD_LIBRARY_PATH", "")
    os.environ.setdefault("CUDA_VISIBLE_DEVICES", "1")
    if ORT_LD.split(":")[0] in ld:
        return
    new_ld = ORT_LD + ((":" + ld) if ld else "")
    if inprocess_ort:
        env = dict(os.environ)
        env["LD_LIBRARY_PATH"] = new_ld
        os.execvpe(sys.executable, [sys.executable, *sys.argv], env)
    os.environ["LD_LIBRARY_PATH"] = new_ld


def child_env() -> dict:
    env = dict(os.environ)
    env.setdefault("CUDA_VISIBLE_DEVICES", "1")
    ld = env.get("LD_LIBRARY_PATH", "")
    if ORT_LD.split(":")[0] not in ld:
        env["LD_LIBRARY_PATH"] = ORT_LD + ((":" + ld) if ld else "")
    return env


# ---------------------------------------------------------------------------
# nvidia-smi process-memory sampler
# ---------------------------------------------------------------------------


class SmiSampler:
    """Samples per-process GPU memory (MiB, nvidia-smi compute-apps) for `pids`.

    One sample per `interval` seconds; each sample is (perf_counter, {pid: mb}).
    Pids absent from a sample read as 0 (process not yet on the GPU / exited).
    """

    def __init__(self, pids, interval: float = 0.02):
        self.pids = list(pids)
        self.interval = interval
        self.samples: list[tuple[float, dict[int, float]]] = []
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def add(self, pid: int) -> None:
        if pid not in self.pids:
            self.pids.append(pid)

    def _read(self) -> dict[int, float]:
        out = subprocess.run(
            ["nvidia-smi", "--query-compute-apps=pid,used_memory", "--format=csv,noheader,nounits"],
            capture_output=True,
            text=True,
        )
        got = {}
        for line in out.stdout.splitlines():
            parts = [p.strip() for p in line.split(",")]
            if len(parts) == 2 and parts[0].isdigit():
                pid = int(parts[0])
                if pid in self.pids:
                    got[pid] = float(parts[1])
        return {pid: got.get(pid, 0.0) for pid in self.pids}

    def _run(self) -> None:
        while not self._stop.is_set():
            t = time.perf_counter()
            try:
                self.samples.append((t, self._read()))
            except Exception:
                pass
            self._stop.wait(self.interval)

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._stop.set()
        self._thread.join(timeout=5)
        return False

    def series(self, pid: int) -> list[tuple[float, float]]:
        return [(t, s.get(pid, 0.0)) for t, s in self.samples]


def window_stats(series: list[tuple[float, float]], t_lo: float, t_hi: float) -> dict:
    vals = [v for t, v in series if t_lo <= t <= t_hi]
    if not vals:
        return {"n": 0, "peak": None, "median": None}
    vals.sort()
    return {"n": len(vals), "peak": vals[-1], "median": vals[len(vals) // 2]}


# ---------------------------------------------------------------------------
# Feeds and model plumbing
# ---------------------------------------------------------------------------


def sys_path_bench() -> None:
    if str(BENCH) not in sys.path:
        sys.path.insert(0, str(BENCH))


def graph_path(model: str, resnetbn_graph: Path | None = None) -> Path:
    if model == "resnetbn":
        return Path(resnetbn_graph or RESNETBN_GRAPH_DEFAULT)
    return BUNDLE / f"{model}_step.onnx"


def load_feeds(model: str, resnetbn_graph: Path | None = None) -> dict:
    """Graph feeds {input_name: ndarray}: benchmark weights + input1."""
    import numpy as np

    if model in E3_MODELS:
        npz = np.load(BUNDLE / f"{model}_feeds.npz")
        return {k: npz[k] for k in npz.files}
    if model == "resnetbn":
        sys_path_bench()
        from common.weights import load

        weights, x = load("resnetbn")
        return {**weights, "input1": x}
    raise ValueError(model)


def feedback_map(session) -> dict:
    """output name -> input name for feeding a training step's outputs back in."""
    ins = {i.name for i in session.get_inputs()}
    m = {}
    for o in session.get_outputs():
        name = o.name
        for prefix in ("output_1_", "output_2_"):
            if name.startswith(prefix) and name[len(prefix):] in ins:
                m[name] = name[len(prefix):]
    return m


# ---------------------------------------------------------------------------
# Environment metadata
# ---------------------------------------------------------------------------


def _pkg_version(*names: str) -> str | None:
    for n in names:
        try:
            return metadata.version(n)
        except metadata.PackageNotFoundError:
            continue
    return None


def gpu_meta(extra: dict | None = None) -> dict:
    def q(args):
        try:
            out = subprocess.run(["nvidia-smi", *args], capture_output=True, text=True, timeout=15)
            return out.stdout.strip().splitlines() if out.returncode == 0 else []
        except Exception:
            return []

    devs = q(["--query-gpu=index,name,driver_version,memory.total,clocks.sm,clocks.max.sm,utilization.gpu",
              "--format=csv,noheader,nounits"])
    apps = q(["--query-compute-apps=pid,gpu_uuid,used_memory", "--format=csv,noheader,nounits"])
    meta = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "host": os.uname().nodename if hasattr(os, "uname") else "?",
        "cuda_visible_devices": os.environ.get("CUDA_VISIBLE_DEVICES"),
        "ld_library_path": os.environ.get("LD_LIBRARY_PATH"),
        "nvidia_smi_gpus": devs,
        "nvidia_smi_compute_apps_at_start": apps,
        "versions": {
            "python": sys.version.split()[0],
            "torch": _pkg_version("torch"),
            "onnxruntime": _pkg_version("onnxruntime-gpu", "onnxruntime"),
            "onnx": _pkg_version("onnx"),
            "openvino": _pkg_version("openvino"),
            "numpy": _pkg_version("numpy"),
            "nvidia_cudnn": _pkg_version("nvidia-cudnn-cu13", "nvidia-cudnn-cu12"),
            "nvidia_cublas": _pkg_version("nvidia-cublas-cu13", "nvidia-cublas-cu12"),
            "triton": _pkg_version("triton"),
        },
        "camel_commit": _git_commit(),
    }
    if extra:
        meta.update(extra)
    return meta


def _git_commit() -> str | None:
    try:
        out = subprocess.run(["git", "-C", str(REPO), "rev-parse", "HEAD"], capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else None
    except Exception:
        return None


def write_meta(out_dir: Path, extra: dict | None = None) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "meta.json"
    path.write_text(json.dumps(gpu_meta(extra), indent=2) + "\n")
    return path


def emit_row(**kv) -> None:
    """Child -> parent result line on stdout."""
    print(ROW + json.dumps(kv), flush=True)


def parse_row(stdout: str) -> dict:
    for line in reversed(stdout.splitlines()):
        if line.startswith(ROW):
            return json.loads(line[len(ROW):])
    raise RuntimeError(f"no {ROW.strip()} line in child output")
