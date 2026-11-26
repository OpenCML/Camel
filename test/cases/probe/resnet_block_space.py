"""Measures how much data-block space one resnet run needs, by narrowing the block limit.

The space fails as soon as the live data blocks would exceed `--block-limit`, and the failure
message reports how many megabytes were in use and how large the refused allocation was. So the
smallest limit at which a run completes is that run's peak block usage, to the size of one
segment (a few megabytes near the end).

Two workloads are measured over one ladder of limits, because the ratio between them is the
reading that matters: the forward alone, and one training step (forward and backward), of the
same model on the same input. Both go through the benchmark's own `model.cml`, folded by
`std::macro` and executed by `std::nvm`, which is how the gradient referee runs it.
"""

import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve()
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "benchmarks" / "tools"))

import grad_referee  # noqa: E402

PASSES = ("std::macro", "std::nvm")
# The forward workload writes its first output; give it a path so it does not fail after computing.
OUTPUT = Path(tempfile.gettempdir()) / "resnet_block_space_output.npy"
WORKLOADS = (("forward", ""), ("train", "train"))
LIMITS = {
    "forward": (64, 96, 128, 192),
    "train": (1024, 2048, 3072, 3584, 3776, 3808, 3840, 4096),
}


def run(limit, workload):
    grad_referee._block_limit_mb = limit
    try:
        grad_referee.camel_run(
            "resnet",
            None,
            None,
            PASSES,
            {"CAMEL_BENCH_WORKLOAD": workload, "CAMEL_BENCH_OUTPUT": str(OUTPUT)},
        )
    except Exception as error:  # noqa: BLE001 - the failure text is the reading
        text = str(error)
        for line in text.splitlines():
            if "exhausted" in line:
                return line.strip()
        first = text.strip().splitlines()
        return first[0] if first else repr(error)
    return None


def main():
    for name, workload in WORKLOADS:
        peak = None
        for limit in LIMITS[name]:
            failure = run(limit, workload)
            if failure is None:
                print(f"BLOCKSPACE {name} limit={limit} completed  <-- peak is at most this")
                peak = limit
                break
            used = failure.split("does not fit, ", 1)[-1].split(" of the limit", 1)[0]
            asked = failure.split("an allocation of ", 1)[-1].split(" does not fit", 1)[0]
            print(f"BLOCKSPACE {name} limit={limit} refused: asked {asked}, {used} in use")
        if peak is None:
            print(f"BLOCKSPACE {name} no limit in the ladder completed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())