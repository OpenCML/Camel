"""Peak resident-set-size measurement for the current process and child processes.

On POSIX, ``resource.getrusage`` reports the kernel-tracked high-water mark.
On Windows (no ``resource`` module), psutil's ``peak_wset`` is used for the
current process, and a child is polled every 5 ms while it runs.
"""

from __future__ import annotations

import subprocess
import sys
import threading
from typing import Dict, List, Optional, Tuple

import psutil

try:
    import resource
except ImportError:  # Windows
    resource = None


def _ru_kb(value: int) -> int:
    # ru_maxrss is KiB on Linux but bytes on macOS.
    return value // 1024 if sys.platform == "darwin" else value


def peak_rss_self_kb() -> int:
    if resource is not None:
        return _ru_kb(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    info = psutil.Process().memory_info()
    return getattr(info, "peak_wset", info.rss) // 1024


def run_with_peak_rss(cmd: List[str], cwd, env: Dict[str, str]) -> Tuple[int, str, str, Optional[int]]:
    """Run ``cmd`` to completion; return (returncode, stdout, stderr, peak_rss_kb of the child)."""
    proc = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peak = [0]
    done = threading.Event()

    def poll() -> None:
        try:
            p = psutil.Process(proc.pid)
            while not done.wait(0.005):
                info = p.memory_info()
                peak[0] = max(peak[0], getattr(info, "peak_wset", info.rss))
        except psutil.Error:
            pass

    watcher = threading.Thread(target=poll, daemon=True)
    watcher.start()
    stdout, stderr = proc.communicate()
    done.set()
    watcher.join()
    if resource is not None:
        # Kernel high-water mark over all waited-for children; the worker that
        # calls this runs exactly one child, so it is that child's peak.
        return proc.returncode, stdout, stderr, _ru_kb(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
    return proc.returncode, stdout, stderr, (peak[0] // 1024) or None
