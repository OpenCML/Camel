"""Runtime load monitor for the E5 measurement protocol (MLSys 2027 eval).

§A.5 discipline: trials run while system load is known and stable; a trial
whose window is contaminated by an unrelated load spike is discarded and
rerun. This module samples CPU load and (optionally) GPU utilization during
each measurement window and decides contamination.

Contamination rule (v2): the busy fraction of the CPUs *outside* the pinned
(affinity) set is computed from /proc/stat deltas. A window is contaminated
when that fraction rises more than ``busy_delta`` above the baseline sampled
during a short settle period *before* the window starts. The busy fraction of
the pinned cores themselves is excluded on purpose: it is dominated by our own
benchmark processes, so it cannot separate an intruder from our own work.

Why not load1 (the v1 rule): /proc/loadavg counts runnable threads on the
whole machine, including the benchmark's own 4 pinned threads, so a fixed
delta threshold flags our own steady work as contamination. load1 is still
sampled and reported (it is the number the paper protocol quotes), but it no
longer decides contamination.

Linux-first (the 8x3090 server). Degrades gracefully elsewhere: per-CPU busy
falls back to psutil.cpu_percent(percpu=True); without psutil the busy rule is
inactive and only load samples are recorded.

Usage as library:
    with LoadMonitor(busy_delta=0.15) as mon:   # blocks ~baseline_window first
        ...run one interleaved trial...
    if mon.contaminated:
        ...discard and rerun...
    print(mon.stats())

CLI:
    python benchmarks/common/loadmon.py --duration 10 --interval 1.0
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import statistics
import subprocess
import threading
import time
from dataclasses import dataclass, field
from typing import Dict, FrozenSet, List, Optional, Tuple

_TRY_NVIDIA_SMI = shutil.which("nvidia-smi")


def _load_avg() -> Optional[Tuple[float, float, float]]:
    try:
        return tuple(os.getloadavg())
    except (AttributeError, OSError):
        return None


def _read_proc_stat() -> Optional[Dict[int, Tuple[int, int]]]:
    """Cumulative (busy, total) jiffies per CPU from /proc/stat (Linux)."""
    out: Dict[int, Tuple[int, int]] = {}
    try:
        with open("/proc/stat") as f:
            for line in f:
                if not line.startswith("cpu") or line[3] == " ":
                    continue
                parts = line.split()
                idx = int(parts[0][3:])
                vals = [int(x) for x in parts[1:9]]
                user, nice, system, idle, iowait, irq, softirq, steal = vals
                busy = user + nice + system + irq + softirq + steal
                out[idx] = (busy, busy + idle + iowait)
    except (OSError, ValueError, IndexError):
        return None
    return out or None


def _busy_fraction(prev: Dict[int, Tuple[int, int]], cur: Dict[int, Tuple[int, int]], exclude: FrozenSet[int]) -> Optional[float]:
    """Aggregate busy fraction over the non-excluded CPUs between two /proc/stat snapshots."""
    db = dt = 0
    for cpu, (b, t) in cur.items():
        if cpu in exclude or cpu not in prev:
            continue
        pb, pt = prev[cpu]
        db += b - pb
        dt += t - pt
    return db / dt if dt > 0 else None


def _psutil_percpu() -> Optional[List[float]]:
    """Per-CPU busy fractions (0..1) since the previous call; None without psutil."""
    try:
        import psutil

        return [v / 100.0 for v in psutil.cpu_percent(interval=None, percpu=True)]
    except Exception:
        return None


def _gpu_util(index: int) -> Optional[float]:
    if not _TRY_NVIDIA_SMI:
        return None
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits", "-i", str(index)],
            capture_output=True,
            text=True,
            timeout=10,
        )
        return float(out.stdout.strip().splitlines()[0])
    except Exception:
        return None


class _BusySampler:
    """Yields (elapsed_s, busy_fraction) of the non-excluded CPUs, one per interval."""

    def __init__(self, exclude: FrozenSet[int]):
        self.exclude = exclude
        self._prev = _read_proc_stat()
        self._psutil_primed = False
        if self._prev is None:
            _psutil_percpu()  # prime psutil's per-cpu counters
            self._psutil_primed = True

    def sample(self, elapsed: float) -> Optional[Tuple[float, float]]:
        cur = _read_proc_stat()
        if cur is not None and self._prev is not None:
            frac = _busy_fraction(self._prev, cur, self.exclude)
            self._prev = cur
            return (elapsed, frac) if frac is not None else None
        percpu = _psutil_percpu()
        if percpu is None:
            return None
        vals = [v for i, v in enumerate(percpu) if i not in self.exclude]
        return (elapsed, statistics.fmean(vals)) if vals else None


@dataclass
class LoadMonitor:
    """Samples load in a background thread; flags contaminated windows.

    ``start()`` first samples a settle baseline for ``baseline_window`` seconds
    (blocking) — run it *before* the measurements of the window begin. A window
    is contaminated when any busy sample of the non-pinned CPUs exceeds the
    baseline mean by more than ``busy_delta``, or any GPU sample of the
    reserved GPU exceeds ``gpu_threshold`` percent.
    """

    interval: float = 1.0
    busy_delta: float = 0.15  # other-CPU busy fraction above baseline -> contaminated
    baseline_window: float = 2.0  # seconds of pre-window settle sampling
    gpu_threshold: float = 5.0  # percent util on the reserved GPU
    gpu_index: Optional[int] = None
    exclude_cpus: Optional[FrozenSet[int]] = None  # default: current affinity set
    baseline_load: Optional[Tuple[float, float, float]] = None
    baseline_busy: Optional[float] = None
    busy_samples: List[Tuple[float, float]] = field(default_factory=list)
    load_samples: List[Tuple[float, float]] = field(default_factory=list)  # (elapsed, load1)
    gpu_samples: List[Tuple[float, float]] = field(default_factory=list)
    _stop: threading.Event = field(default_factory=threading.Event, init=False)
    _thread: Optional[threading.Thread] = field(default=None, init=False)
    _t0: float = field(default=0.0, init=False)

    def _affinity(self) -> FrozenSet[int]:
        if self.exclude_cpus is not None:
            return self.exclude_cpus
        try:
            return frozenset(os.sched_getaffinity(0))
        except AttributeError:
            return frozenset()

    def _settle_sample(self, sampler: _BusySampler, elapsed: float) -> None:
        got = sampler.sample(elapsed)
        if got is not None:
            self.busy_samples.append(got)

    def _loop(self) -> None:
        sampler = _BusySampler(self._affinity())
        while not self._stop.wait(self.interval):
            self._settle_sample(sampler, time.monotonic() - self._t0)
            load = _load_avg()
            if load is not None:
                self.load_samples.append((time.monotonic() - self._t0, load[0]))
            if self.gpu_index is not None:
                g = _gpu_util(self.gpu_index)
                if g is not None:
                    self.gpu_samples.append((time.monotonic() - self._t0, g))

    def start(self) -> "LoadMonitor":
        """Record the pre-window baseline (blocks ``baseline_window`` seconds)."""
        self._t0 = time.monotonic()
        self.baseline_load = _load_avg()
        sampler = _BusySampler(self._affinity())
        settle: List[float] = []
        end = self._t0 + self.baseline_window
        while True:
            remain = end - time.monotonic()
            if remain <= 0:
                break
            time.sleep(min(self.interval, remain))
            got = sampler.sample(time.monotonic() - self._t0)
            if got is not None:
                settle.append(got[1])
        self.baseline_busy = statistics.fmean(settle) if settle else None
        self.busy_samples = []
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=self.interval * 2 + 5)

    def __enter__(self) -> "LoadMonitor":
        return self.start()

    def __exit__(self, *exc) -> None:
        self.stop()

    @property
    def contaminated(self) -> bool:
        return bool(self.reasons)

    @property
    def reasons(self) -> List[str]:
        out: List[str] = []
        if self.baseline_busy is not None and self.busy_samples:
            peak = max(v for _, v in self.busy_samples)
            if peak - self.baseline_busy > self.busy_delta:
                out.append(f"other-cpu busy {peak:.2f} > baseline {self.baseline_busy:.2f} + {self.busy_delta:.2f}")
        if self.gpu_samples:
            peak = max(v for _, v in self.gpu_samples)
            if peak > self.gpu_threshold:
                out.append(f"gpu{self.gpu_index} util {peak:.0f}% > {self.gpu_threshold:.0f}%")
        return out

    def stats(self) -> dict:
        def summ(xs: List[float]) -> dict:
            if not xs:
                return {"n": 0}
            return {"n": len(xs), "max": max(xs), "mean": statistics.fmean(xs)}

        return {
            "baseline_load": self.baseline_load,
            "baseline_busy": self.baseline_busy,
            "busy": summ([v for _, v in self.busy_samples]),
            "load1": summ([v for _, v in self.load_samples]),
            "gpu": summ([v for _, v in self.gpu_samples]),
            "contaminated": self.contaminated,
            "reasons": self.reasons,
        }


def main() -> None:
    ap = argparse.ArgumentParser(description="Sample system load for a window.")
    ap.add_argument("--duration", type=float, default=10.0)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--baseline-window", type=float, default=2.0)
    ap.add_argument("--busy-delta", type=float, default=0.15)
    ap.add_argument("--gpu", type=int, default=None)
    args = ap.parse_args()
    with LoadMonitor(
        interval=args.interval, baseline_window=args.baseline_window, busy_delta=args.busy_delta, gpu_index=args.gpu
    ) as mon:
        time.sleep(args.duration)
    print(json.dumps(mon.stats(), default=list))


if __name__ == "__main__":
    main()
