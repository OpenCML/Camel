# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

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
import shutil
import subprocess
from pathlib import Path
from typing import Dict, List, Optional

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


def _pinned_cpus() -> List[int]:
    try:
        return sorted(os.sched_getaffinity(0))
    except AttributeError:
        return []


def _sysfs_cpu(cpu: int, field: str) -> Optional[str]:
    try:
        return Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/{field}").read_text().strip()
    except OSError:
        return None


def cpu_governors(cpus: Optional[List[int]] = None) -> Optional[Dict[int, str]]:
    """scaling_governor per pinned CPU (None when cpufreq is not exposed)."""
    cpus = _pinned_cpus() if cpus is None else cpus
    out = {cpu: gov for cpu in cpus if (gov := _sysfs_cpu(cpu, "scaling_governor"))}
    return out or None


def cpu_freq_khz(cpus: Optional[List[int]] = None) -> Optional[Dict[int, int]]:
    """scaling_cur_freq per pinned CPU; a snapshot only — schedutil ramps it."""
    cpus = _pinned_cpus() if cpus is None else cpus
    out = {}
    for cpu in cpus:
        raw = _sysfs_cpu(cpu, "scaling_cur_freq")
        if raw and raw.isdigit():
            out[cpu] = int(raw)
    return out or None


def _cpu_flags() -> List[str]:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("flags"):
                return line.split(":", 1)[1].split()
    except OSError:
        pass
    return []


def cpu_vector_isa() -> Dict[str, object]:
    """SIMD support relevant to native kernels: AVX2 and the AVX-512 flag set."""
    flags = set(_cpu_flags())
    return {
        "avx2": "avx2" in flags or None,
        "avx512": sorted(f for f in flags if f.startswith("avx512")),
    }


def gpu_info() -> Optional[Dict[str, object]]:
    """nvidia-smi inventory: devices, driver/CUDA versions, current compute apps."""
    if not shutil.which("nvidia-smi"):
        return None

    def query(args: List[str]) -> Optional[List[str]]:
        try:
            out = subprocess.run(["nvidia-smi", *args], capture_output=True, text=True, timeout=15)
            return out.stdout.strip().splitlines() if out.returncode == 0 else None
        except (OSError, subprocess.SubprocessError):
            return None

    devices = query(
        [
            "--query-gpu=index,name,driver_version,memory.total,clocks.max.sm,utilization.gpu",
            "--format=csv,noheader,nounits",
        ]
    )
    if devices is None:
        return None
    cols = ("index", "name", "driver_version", "memory_total_mb", "clocks_max_sm_mhz", "utilization_pct")
    apps = query(["--query-compute-apps=gpu_uuid,pid,used_memory", "--format=csv,noheader,nounits"]) or []
    cuda = None
    for line in query([]) or []:
        if "CUDA Version:" in line:
            cuda = line.split("CUDA Version:", 1)[1].strip().split()[0]
            break
    return {
        "cuda_version": cuda,
        "devices": [dict(zip(cols, (p.strip() for p in line.split(",")))) for line in devices],
        "compute_apps": [line for line in apps if line.strip()],
    }


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
        "cpu_governor": cpu_governors(),
        "cpu_freq_khz": cpu_freq_khz(),
        "cpu_vector_isa": cpu_vector_isa(),
        "gpus": gpu_info(),
        "python": platform.python_version(),
        "versions": {
            "numpy": _version("numpy"),
            "torch": _version("torch"),
            "tensorflow": _version("tensorflow", "tensorflow-cpu", "tensorflow_cpu"),
            "onnx": _version("onnx"),
            "onnxruntime": _version("onnxruntime", "onnxruntime-gpu"),
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
