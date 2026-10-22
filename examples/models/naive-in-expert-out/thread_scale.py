"""Why the sweep measures "all cores" at 16 threads and not at 32.

`measure.py --threads all` would use `os.cpu_count()`, which is 32 on the host: 16 physical cores
with SMT.  This script checks whether 32 is a usable thread budget for PyTorch's per-operator
fork-join on that host, by timing two matmuls at 1, 8, 16 and 32 threads.  A small matmul is the
case that matters for this directory: the d=64 step is a chain of small operators, and its cost at
N threads is dominated by how much each operator pays to gather N threads, not by arithmetic.

    python examples/models/naive-in-expert-out/thread_scale.py \
        --output examples/models/naive-in-expert-out/results/2026-10-23-thread-scaling-linux.json

The result is one file next to the sweep, with the machine and the load it saw.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import platform
import statistics
import sys
import time
from pathlib import Path

import torch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import measure  # noqa: E402  (the sweep's provenance helpers)

THREADS = (1, 8, 16, 32)
BIG = (2048, 1024, 1024)
SMALL = (8, 16, 64, 64)
SMALL_REPEATS = 200
BIG_REPEATS = 9


def median_ms(operation, repeats: int) -> float:
    for _ in range(2):
        operation()
    samples = []
    for _ in range(repeats):
        start = time.perf_counter()
        operation()
        samples.append((time.perf_counter() - start) * 1e3)
    return round(statistics.median(samples), 6)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    torch.manual_seed(0)
    big = (torch.randn(BIG[0], BIG[1]), torch.randn(BIG[1], BIG[2]))
    small = (torch.randn(*SMALL[:3]), torch.randn(*SMALL[2:]))
    rows = []
    for threads in THREADS:
        torch.set_num_threads(threads)
        big_ms = median_ms(lambda: big[0] @ big[1], BIG_REPEATS)
        small_ms = median_ms(lambda: small[0] @ small[1], SMALL_REPEATS)
        rows.append({"threads": threads, "big_matmul_ms": big_ms, "small_matmul_ms": small_ms})
        print(f"threads={threads}: {BIG[0]}x{BIG[1]}@{BIG[1]}x{BIG[2]} {big_ms:.3f} ms, "
              f"{SMALL[0]}x{SMALL[1]}x{SMALL[2]}@{SMALL[2]}x{SMALL[3]} {small_ms:.4f} ms")

    result = {
        "schema": 1,
        "what": "PyTorch matmul cost against the thread budget, on the host of the sweep; it is "
                "why the sweep's all-core column is 16 threads (one per physical core) and not "
                "the 32 hardware threads",
        "generated_at": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "machine": measure.machine(),
        "git": measure.git_state(),
        "python": platform.python_version(),
        "torch": torch.__version__,
        "load_average": measure.load_average(),
        "matmul": {
            "dtype": "float32",
            "big": {"a": list(BIG[:2]), "b": list(BIG[1:]), "repeats": BIG_REPEATS},
            "small": {"a": list(SMALL[:3]), "b": list(SMALL[2:]), "repeats": SMALL_REPEATS},
            "note": "medians in-process, after two untimed calls; torch.set_num_threads sets the "
                    "budget, OMP/MKL are not overridden here",
        },
        "rows": rows,
    }
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())