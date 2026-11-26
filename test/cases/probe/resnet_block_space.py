"""Measures how much data-block space one full-batch resnet run needs.

Runs the benchmark's own model through the same command the gradient referee uses
(`std::macro` then `std::nvm`), with `std::profile gc_stats=1` added so the run prints the
allocator's statistics at exit, at several `--block-limit` settings. Reports, for each limit,
whether the run completed and the block statistics it printed: live bytes, peak bytes, live
block count and blocks ever created.
"""

import sys
from pathlib import Path

HERE = Path(__file__).resolve()
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "benchmarks" / "tools"))

import grad_referee  # noqa: E402

PASSES = ("std::macro", "std::nvm", "std::profile", "gc_stats=1")


def run(limit):
    grad_referee._block_limit_mb = limit
    try:
        out = grad_referee.camel_run("resnet", None, None, PASSES, {})
    except Exception as error:  # noqa: BLE001 - the failure text is the reading
        first = str(error).strip().splitlines()
        return None, first[0] if first else repr(error)
    return out, None


def main():
    for limit in (2048, 3072, 4096, 8192):
        out, failure = run(limit)
        if failure is not None:
            print(f"BLOCKSPACE limit={limit} failed: {failure}")
            continue
        blocks = [
            line.strip()
            for line in out.splitlines()
            if line.strip().startswith("blocks:")
        ]
        print(f"BLOCKSPACE limit={limit} ok")
        for line in blocks:
            print(f"BLOCKSPACE {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())