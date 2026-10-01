"""Machine-readable environment metadata for benchmark results.

Every result file gets a ``<result>.meta.json`` sidecar describing where and how
it was measured: CPU, core count, threads used, load average, OS, Python and
framework versions, and the Camel commit and binary. Numbers measured on a
shared or virtual machine (such as a development container) validate the tools
only; the paper's numbers come from runs on dedicated hardware, and ``note``
says so.
"""

from __future__ import annotations

import datetime as dt
import json
import os
import platform
import subprocess
from pathlib import Path
from typing import Dict, Optional

REPO_ROOT = Path(__file__).resolve().parents[2]

NOTE = (
    "Measured for tool validation. Authoritative numbers come from runs on dedicated "
    "hardware (quiet machine, pinned threads); do not cite numbers from shared or virtual machines."
)


def _cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def _git(*args: str) -> Optional[str]:
    try:
        out = subprocess.run(["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, timeout=10)
        return out.stdout.strip() if out.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


def _version(*packages: str) -> Optional[str]:
    """Version of the first installed distribution among `packages` (from its metadata:
    importing the frameworks would be slow and noisy)."""
    from importlib import metadata

    for package in packages:
        try:
            return metadata.version(package)
        except metadata.PackageNotFoundError:
            continue
    return None


def collect(threads: Optional[int] = None, extra: Optional[Dict[str, object]] = None) -> Dict[str, object]:
    from .configs import camel_binary

    binary = camel_binary()
    meta: Dict[str, object] = {
        "timestamp": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "host": platform.node(),
        "os": platform.platform(),
        "cpu": _cpu_model(),
        "logical_cpus": os.cpu_count(),
        "affinity_cpus": len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "threads": threads,
        "load_avg": list(os.getloadavg()) if hasattr(os, "getloadavg") else None,
        "python": platform.python_version(),
        "versions": {
            "numpy": _version("numpy"),
            "torch": _version("torch"),
            "tensorflow": _version("tensorflow", "tensorflow-cpu", "tensorflow_cpu"),
            "onnx": _version("onnx"),
            "onnxruntime": _version("onnxruntime"),
        },
        "camel_commit": _git("rev-parse", "HEAD"),
        "camel_dirty": bool(_git("status", "--porcelain", "--untracked-files=no")),
        "camel_binary": str(binary),
        "camel_binary_mtime": (
            dt.datetime.fromtimestamp(binary.stat().st_mtime).isoformat(timespec="seconds") if binary.exists() else None
        ),
        "note": NOTE,
    }
    if extra:
        meta.update(extra)
    return meta


def meta_path(result: Path) -> Path:
    """``results/x.csv`` -> ``results/x.meta.json``."""
    return result.with_name(f"{result.stem}.meta.json")


def write_meta(result: Path, threads: Optional[int] = None, extra: Optional[Dict[str, object]] = None) -> Path:
    path = meta_path(result)
    path.write_text(json.dumps(collect(threads, extra), indent=2) + "\n")
    return path
