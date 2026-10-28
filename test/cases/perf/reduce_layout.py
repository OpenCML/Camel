"""Per-operator cost of the reduction and layout kernels, at the shapes the d=256 step uses.

The step budget (`step_budget.py`) measures one whole step, and the step's own CPU time cannot
resolve a two-millisecond row on the reference host.  This case measures one operator at a time:
for every shape the step issues, a generated program runs one forward+backward of a single
expression, so the operator's row in `std::profile` is the operator's own time and its calls per
round are a number to pin.

The shapes come from `examples/models/naive-in-expert-out/train_step.cml` at the sweep's d=256
cell (batch 16 x sequence 32, d_model 256, 16 heads of width 16, FFN 512, vocabulary 32, two
blocks, 512 tokens):

  sum_to          512x256 -> 256       the bias and gain gradients: a leading-axis reduction
  sum_to          512x256 -> 512x1     the RMSNorm denominator's gradient: a trailing-axis one
  sum_to          512x256 -> 512x256   the identity case (most of the 60 calls a step makes)
  expand_axis     512x1   -> 512x256   the mean/sum VJP that spreads a denominator back
  permute         [16,32,16,16] <-> [16,16,32,16]   split_heads and merge_heads
  transpose       [16,16,32,32]        the attention product's right operand
  sum_axis        [16,16,32,32] -> [16,16,32,1]     the softmax denominator

Modes

  count   Runs one program per shape and pins three properties of the kernels:

            * the operator calls of a round (one `sum_to` a round, two permutes, ...), taken from
              the difference between two round counts so the probe's own warm-up call cancels;
            * `sum_to` allocates exactly one block per call -- its output -- so a fast path may not
              add a temporary, and nothing at all for the identity case;
            * the printed checksum, to nine decimals, is the one pinned here.  That number is the
              kernel's own output, and the declaration evaluator (feat.modules.tensor.
              index_semantics) already holds the kernel to the declaration bit for bit; pinning it
              here is what makes a change to the *order* of a reduction fail loudly instead of
              being absorbed as a rounding difference.  The same program one round longer must
              print it too, so the result may not depend on how many rounds ran before it.
              The three sum_to pins are the f64 sums of the f32 gradient the probe returns: the
              probe declares `f32` and converts to f64 before the checksum's sum (the ruled
              reduction returns its element type, so summing the f32 values in f64 is spelled
              out), which gives 80.390660226 / 15571443.036040597 / 80.390715818.  They were
              pinned to the same values rounded once to f32 for a while (2026-10-26), when the
              checksum's own sum was the rounding one; the probe stopped doing that the same day,
              and the pins were not moved back.  The other four probes are integer-exact and did
              not move.

  ops     The measurement: the operator's ns per call and its share of the round, both from
          `std::profile`, `SAMPLES` runs a shape with the least contended round kept; the same shape
          is measured a second time at a different round count as the null control, so every row has
          two independent estimates of one number and their ratio is what this instrument resolves.

          The share -- the row's ns divided by the round's mean ns, both wall times from one run
          -- is the number a change is judged by, because it is a ratio inside one run.  The wall
          times themselves are not: this host runs about twenty tasks per core, so a fixed
          workload's wall time moves by a factor of three inside a minute, and the step budget's
          own normalisation carries a row's *share* onto a measured CPU step for exactly that
          reason.  `share x 64.9 ms` is that row's milliseconds of the d=256 step.

Usage
  python reduce_layout.py count
  python reduce_layout.py ops
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent          # test/cases/perf
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE))

import step_budget                              # noqa: E402  (same harness, same normalisation)

# The same expression at two round counts: the null pair.  A different count is a different
# program, so the two estimates do not share a run.  The count is large because the numbers are
# wall times on a host that runs twenty tasks per core: a single preemption inside one call adds
# its whole cost to that call's row, and two thousand rounds divide it away.
REPS = {"a": 32, "b": 33}
MEASURE_REPS = {"a": 2048, "b": 2049}
SAMPLES = 5
# The d=256 step's CPU time, from the recorded budget (results/2026-10-23-d256-step-budget.json).
STEP_CPU_MS = 64.896

# One program per shape.  `body` is the expression the differentiated function returns; the
# gradient of the `with` parameter is what the loop carries, so every round runs the forward graph
# and its backward once.  The shapes are the d=256 cell's, spelled out.
PROBES = [
    {
        "name": "sum_to_leading",
        "with": "b: f32[256]",
        "args": "x: f32[512, 256]",
        "body": "sum(x * b)",
        "lets": ["let x: f32[512, 256] = randn([512, 256], 0.0, 1.0) "
                 "as f32[512, 256]",
                 "let w0: f32[256] = randn([256], 0.0, 1.0) as f32[256]"],
        "calls": {"tensor:sum_to": 1},
        "note": "512x256 summed to [256]",
        "reference": "tensor:broadcast_like",
        "check": "probe name=sum_to_leading reps=N check=80.390660226"
    },
    {
        "name": "sum_to_trailing",
        "with": "s: f32[512, 1]",
        "args": "x: f32[512, 256]",
        "body": "sum(x / s)",
        "lets": ["let x: f32[512, 256] = randn([512, 256], 0.0, 1.0) "
                 "as f32[512, 256]",
                 "let w0: f32[512, 1] = randn([512, 1], 0.0, 1.0) "
                 "as f32[512, 1]"],
        "calls": {"tensor:sum_to": 1},
        "note": "512x256 summed to [512,1] along the trailing axis",
        "reference": "tensor:broadcast_like",
        "check": "probe name=sum_to_trailing reps=N check=15571443.036040597"
    },
    {
        "name": "sum_to_identity",
        "with": "b: f32[512, 256]",
        "args": "x: f32[512, 256]",
        "body": "sum(x * b)",
        "lets": ["let x: f32[512, 256] = randn([512, 256], 0.0, 1.0) "
                 "as f32[512, 256]",
                 "let w0: f32[512, 256] = randn([512, 256], 0.0, 1.0) "
                 "as f32[512, 256]"],
        "calls": {"tensor:sum_to": 1},
        "alloc_per_call": 0.0,
        "note": "the floor: the call is the identity path",
        "reference": "tensor:broadcast_like",
        "check": "probe name=sum_to_identity reps=N check=80.390715818"
    },
    {
        "name": "expand_axis",
        "with": "w: f32[512, 256]",
        "args": "",
        "body": "sum(mean<-1, true>(w))",
        "lets": ["let w0: f32[512, 256] = randn([512, 256], 0.0, 1.0) "
                 "as f32[512, 256]"],
        "calls": {"tensor:expand_axis": 1, "tensor:mean_axis": 1},
        "note": "the mean's VJP spreads [512,1] back over [512,256]",
        "reference": "tensor:mean_axis",
        "check": "probe name=expand_axis reps=N check=512.000000000"
    },
    {
        "name": "permute",
        "with": "w: f32[16, 32, 16, 16]",
        "args": "",
        "body": "sum(permute<[0, 2, 1, 3]>(permute<[0, 2, 1, 3]>(w)))",
        "lets": ["let w0: f32[16, 32, 16, 16] = randn([16, 32, 16, 16], 0.0, 1.0) "
                 "as f32[16, 32, 16, 16]"],
        "calls": {"tensor:permute": 2, "tensor:permute_inverse": 2},
        "note": "split_heads and merge_heads, forward and back",
        "reference": "tensor:broadcast_like",
        "check": "probe name=permute reps=N check=131072.000000000"
    },
    {
        "name": "transpose",
        "with": "w: f32[16, 16, 32, 32]",
        "args": "",
        "body": "sum(transpose(w))",
        "lets": ["let w0: f32[16, 16, 32, 32] = randn([16, 16, 32, 32], 0.0, 1.0) "
                 "as f32[16, 16, 32, 32]"],
        "calls": {"tensor:transpose": 2},
        "note": "the attention product's right operand, forward and back",
        "reference": "tensor:broadcast_like",
        "check": "probe name=transpose reps=N check=262144.000000000"
    },
    {
        "name": "sum_axis",
        "with": "w: f32[16, 16, 32, 32]",
        "args": "",
        "body": "sum(sum<-1, true>(w))",
        "lets": ["let w0: f32[16, 16, 32, 32] = randn([16, 16, 32, 32], 0.0, 1.0) "
                 "as f32[16, 16, 32, 32]"],
        "calls": {"tensor:sum_axis": 1},
        "note": "the softmax denominator",
        "reference": "tensor:broadcast_like",
        "check": "probe name=sum_axis reps=N check=262144.000000000"
    },
]

IMPORTS = "import { mean, permute, randn, seed, sum, transpose } from tensor"


def program(probe: dict, reps: int) -> str:
    """The generated program: one expression, its gradient, `reps` rounds of it."""
    lets = "\n    ".join(probe["lets"])
    return f"""// Generated by test/cases/perf/reduce_layout.py: one operator of the d=256 step,
// measured on its own.  Do not edit.

{IMPORTS}
import {{ grad }} from autodiff
import {{ now }} from time

with <{probe["with"]}>
func body({probe["args"]}): f32 {{
    return {probe["body"]}
}}

func main(reps: int): int sync {{
    seed(11)
    {lets}
    let g = grad(body)
    let start = now()
    let (i, last) = loop i = 0, last = w0 while i < reps do {{
        return (i + 1, {probe["call"]})
    }}
    let elapsed = now() - start
    `probe name={probe["name"]} reps=${{reps}} check=${{sum(f64(last)):.9f}}`->println
    `probe elapsed name={probe["name"]} reps=${{reps}} ms=${{elapsed * 1000.0:.6f}}`->println
    return 0
}}
"""


# The gradient call is the same expression on every round: `g<w0>(x)` or `g<w0>()`.
for _probe in PROBES:
    _probe["call"] = "g<w0>(x)" if _probe["args"] else "g<w0>()"


def write_program(directory: Path, probe: dict, reps: int, tag: str) -> Path:
    path = directory / f"probe_{probe['name']}_{tag}_{reps}.cml"
    path.write_text(program(probe, reps), encoding="utf-8")
    return path


def profile(probe: dict, reps: int) -> dict:
    with tempfile.TemporaryDirectory() as t:
        directory = Path(t)
        path = write_program(directory, probe, reps, "profile")
        report = directory / "profile.json"
        result = step_budget.run(
            path,
            ["tensor::fuse", "std::profile", "report=true", f'json="{report.as_posix()}"',
             "top=0", "std::jit"],
            ["--arg", f"reps={reps}"])
        if not report.exists():
            raise SystemExit(f"{probe['name']}: std::profile wrote no report:\n"
                             f"{result['stdout'][-2000:]}")
        doc = json.loads(report.read_text(encoding="utf-8"))
        doc["_stdout"] = result["stdout"]
        return doc


def checksum(doc: dict) -> str:
    for line in doc["_stdout"].splitlines():
        if line.startswith("probe name="):
            return line
    raise SystemExit("the probe printed no checksum line")


def rows(doc: dict) -> dict[str, dict]:
    return {row["uri"]: row for row in doc["operator_rows"]}


def count_mode() -> int:
    failures = []
    for probe in PROBES:
        doc = profile(probe, REPS["a"])
        moved = profile(probe, REPS["b"])
        # `MEASURE_REPS` is where the rates the ops table reports are taken, so the guard pins the
        # same numbers the measurement uses.  The round count divides the probe's one warm-up call
        # away at that length, so it is the rate itself that has to be the pinned count.
        long_a = profile(probe, MEASURE_REPS["a"])
        long_b = profile(probe, MEASURE_REPS["b"])
        counts = {uri: (rows(long_a).get(uri, {}).get("calls", 0),
                        rows(long_b).get(uri, {}).get("calls", 0)) for uri in probe["calls"]}
        rate = {uri: (counts[uri][1] - counts[uri][0]) / (MEASURE_REPS["b"] - MEASURE_REPS["a"])
                for uri in probe["calls"]}
        for uri in probe["calls"]:
            if counts[uri][1] <= counts[uri][0]:
                failures.append(f"{probe['name']}: {uri} ran {counts[uri][1]} calls in "
                                f"{MEASURE_REPS['b']} rounds against {counts[uri][0]} in "
                                f"{MEASURE_REPS['a']}: the counts are not a rate")
        for uri, want in probe["calls"].items():
            if rate[uri] != want:
                failures.append(f"{probe['name']}: {uri} ran {rate[uri]} times a round, "
                                f"the probe pins {want}")
        # sum_to holds the property the fast path relies on: the reduction writes its output and
        # allocates nothing else, and the identity case allocates nothing at all.
        blocks_text = ""
        row = rows(doc).get("tensor:sum_to")
        if row is not None and row["calls"]:
            blocks = row["alloc_blocks"] / row["calls"]
            want = probe.get("alloc_per_call", 1.0)
            blocks_text = f"  sum_to alloc {blocks:.2f} blocks/call (pinned {want:g})"
            if blocks != want:
                failures.append(f"{probe['name']}: sum_to allocated {blocks:.2f} blocks a call, "
                                f"expected {want:g} (its output, and no temporary)")
        # The same program at another round count must print the same reduction, and that reduction
        # must be the pinned one: the value the declaration folds is what the kernels write, and no
        # fast path may move it.
        here = checksum(doc).replace(f"reps={REPS['a']}", "reps=N")
        other = checksum(moved).replace(f"reps={REPS['b']}", "reps=N")
        if other != here:
            failures.append(f"{probe['name']}: the checksum moved with the round count:\n"
                            f"  {here}\n  {other}")
        pinned = probe.get("check")
        if pinned is None:
            print(f"  {probe['name']}: no checksum pinned; this run prints {here}")
        elif here != pinned:
            failures.append(f"{probe['name']}: the reduction moved:\n"
                            f"  pinned {pinned}\n  now    {here}")
        print(f"  {probe['name']:<20} calls/round {rate}{blocks_text}  {probe['note']}")
    for message in failures:
        print(f"FAIL: {message}")
    if failures:
        return 1
    print("reduce layout counts: PASS")
    return 0


def measure(probe: dict, variant: str) -> dict[str, dict]:
    """Three runs of one shape; the least contended run gives every row of this shape its number.

    The runs are picked as a whole rather than per row: taking each row's minimum on its own lets
    two rows of the same round be minimized in different runs, and then their shares no longer
    belong to one round and do not add up.  The round's mean wall time per round is the measure of
    contention, and the smallest one is the round the numbers are read from.

    Each row is also reported against the probe's reference kernel -- a fill of the same shape, or
    the reduction beside it, neither of which a change to these kernels touches.  Both rows are wall
    times from one run, so their ratio is what survives the host's load and what a change is
    compared by; the ns are for the reader who wants the raw number.
    """
    best = None
    for _ in range(SAMPLES):
        doc = profile(probe, MEASURE_REPS[variant])
        per_round = step_budget.per_step(doc)
        for uri in list(probe["calls"]) + [probe["reference"]]:
            if uri not in per_round:
                raise SystemExit(f"{probe['name']}: no {uri} row in the report")
        if best is None or doc["step"]["wall_per_round_ns"] < best[0]:
            best = (doc["step"]["wall_per_round_ns"], per_round)
    round_mean, per_round = best
    reference = per_round[probe["reference"]]
    out = {"_reference": {"uri": probe["reference"],
                          "ns_per_call": reference["ns"] / reference["calls"]}}
    for uri, calls in probe["calls"].items():
        out[uri] = {"ns_per_call": per_round[uri]["ns"] / calls,
                    "share": per_round[uri]["ns"] / round_mean / calls,
                    "reference_ns": reference["ns"] / reference["calls"],
                    "calls": per_round[uri]["calls"]}
    return out


def ops_mode() -> int:
    table = []
    for probe in PROBES:
        a = measure(probe, "a")
        b = measure(probe, "b")
        for uri, want in probe["calls"].items():
            table.append({
                "probe": probe["name"], "uri": uri, "shape": probe["note"], "calls": want,
                "reference": a["_reference"]["uri"],
                "ns_per_call": a[uri]["ns_per_call"], "share": a[uri]["share"],
                "relative": a[uri]["ns_per_call"] / a[uri]["reference_ns"],
                "null_ns_per_call": b[uri]["ns_per_call"], "null_share": b[uri]["share"],
                "null_relative": b[uri]["ns_per_call"] / b[uri]["reference_ns"],
            })
    print(f"  {'probe':<20} {'operator':<24} {'calls':>5} {'ns/call':>9} {'ref ns':>8} "
          f"{'rel':>6} {'null rel':>8} {'share':>7} {'step ms':>8}  shape")
    for row in table:
        print(f"  {row['probe']:<20} {row['uri']:<24} {row['calls']:>5} "
              f"{row['ns_per_call']:>9.0f} {row['ns_per_call'] / row['relative']:>8.0f} "
              f"{row['relative']:>6.2f} {row['null_relative']:>8.2f} "
              f"{row['share'] * 100:>6.2f}% {row['share'] * STEP_CPU_MS:>8.3f}  {row['shape']}")
    print(f"  reference kernels (same run, same shape): "
          f"{', '.join(sorted({p['reference'] for p in PROBES}))}")
    print(f"  one thread, {SAMPLES} runs a shape, least contended round kept; null = the same "
          f"shape at {MEASURE_REPS['a']} and {MEASURE_REPS['b']} rounds; load average "
          f"{step_budget.load_average():.1f}")
    print("reduce layout ops: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["count", "ops"])
    args = parser.parse_args()
    return count_mode() if args.mode == "count" else ops_mode()


if __name__ == "__main__":
    raise SystemExit(main())
