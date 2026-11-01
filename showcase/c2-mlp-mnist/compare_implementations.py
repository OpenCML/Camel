"""Train C2 twice, once per gradient implementation, and compare the two runs' last loss.

    python showcase/c2-mlp-mnist/compare_implementations.py --data artifacts/standard

The two runs are the same program under two pipelines, which is what "swap the implementation"
means for a function transform: `with_grad(loss)` is one call either way, and what differs is
which machinery answers it -- today the pass mechanism with the native gradient builder behind it,
after M4 the library's `vjp = transpose . split . reinterp<jvp>`.  So the switch is
`--a-pipeline` / `--b-pipeline`, the program is the showcase's own `train.cml`, and neither run
changes what a step computes.

Both runs print the same three-phase table (see `phases.py`) plus the last logged loss, and the
comparison is on that loss:

    |loss_a - loss_b| <= 1e-4 * max(1, |loss_a|, |loss_b|)

Why 1e-4, by hand and not read off a run: the two implementations compute the same gradient --
the same partial derivatives, the same product of the same factors -- so what they cannot share is
the order the sum is taken in, and a training run amplifies an f32 rounding difference (one ulp,
about 6e-8 relative) through 1000 steps of Adam into something of order 1e-4.  The bound is two
orders of magnitude above the rounding a single step can produce and two below the difference a
real defect makes (a dropped term or a wrong operator moves the loss by whole percent).  It is
relative to the loss; the unit floor keeps it meaningful when the loss is near zero.

Exit 0 when the two agree and both ran; non-zero with the reason otherwise -- including "the
second pipeline does not run at all", which is today's state until M4 lands (the plan entry that
calls this is skipped by `test/tools/have-library-grad.mjs` for exactly that reason).
"""

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

A_PIPELINE = "std::macro std::nvm"
B_PIPELINE = "std::macro std::vjp std::nvm"
TOLERANCE = 1.0e-4


def table(data: Path, steps: int, pipeline: str, profile: Path) -> dict:
    """Run one implementation through `phases.py` and read its summary back."""
    command = [
        sys.executable,
        str(HERE / "phases.py"),
        "--data",
        str(data),
        "--steps",
        str(steps),
        "--pipeline",
        pipeline,
        "--profile",
        str(profile),
    ]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit(
            "the %s implementation did not run (exit %d)" % (pipeline, result.returncode)
        )
    summary = {}
    for line in result.stdout.splitlines():
        if not line.startswith("phases sum="):
            continue
        for piece in line.split():
            if "=" in piece:
                key, value = piece.split("=", 1)
                summary[key] = value
    if "last_loss" not in summary:
        raise SystemExit("phases.py printed no summary line")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", required=True)
    parser.add_argument("--steps", type=int, default=1000)
    parser.add_argument("--a-pipeline", default=A_PIPELINE)
    parser.add_argument("--b-pipeline", default=B_PIPELINE)
    parser.add_argument("--tolerance", type=float, default=TOLERANCE)
    args = parser.parse_args()

    data = Path(args.data).resolve()
    sys.stdout.write("implementations A (with_grad) pipeline=%s\n" % args.a_pipeline)
    left = table(data, args.steps, args.a_pipeline, data / "a-profile.json")
    sys.stdout.write("implementations B (library grad) pipeline=%s\n" % args.b_pipeline)
    right = table(data, args.steps, args.b_pipeline, data / "b-profile.json")

    a = float(left["last_loss"])
    b = float(right["last_loss"])
    difference = abs(a - b) / max(1.0, abs(a), abs(b))
    verdict = "PASS" if difference <= args.tolerance else "FAIL"
    sys.stdout.write(
        "compare last_loss A=%.9f B=%.9f difference=%.3e tolerance=%.1e %s\n"
        % (a, b, difference, args.tolerance, verdict)
    )
    if verdict != "PASS":
        sys.stderr.write(
            "the two implementations disagree on the last loss by %.3e, over the %.1e the two "
            "orders of summarisation can account for\n" % (difference, args.tolerance)
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())