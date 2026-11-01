"""The three phases of one C2 training step, read from what `std::profile` measured.

    python showcase/c2-mlp-mnist/phases.py --data artifacts/standard --steps 1000

Prints one line per phase -- forward, backward, optimizer -- with its absolute time, its share of
the step, and the operators it is made of, then a line with the sum, the ratio to the time the loop
itself reports, and the last logged loss.  The training program is not changed in any way: the
attribution comes from the profiler's per-operator rows (`operator_rows`), which is why a transform
that replaces the gradient's implementation shows up here as a different table rather than as a
different program.  See `docs/handover/reports/s2-with-grad-time-split.md`.

Blocks are by what the step is computing, not by call site: `with_grad(loss)` returns the loss and
the gradient in one call, so a call site cannot separate them.  `multiply` and `gt` run once per
step and have no place in the forward pass; they are counted in the backward one, and that choice
moves 2 % between two blocks without changing their sum.
"""

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import compare  # noqa: E402  (the sibling script: the data, the camel command, the parsers)

# The operators of each phase, most expensive first.  Every name is a `uri` of the profiler's table;
# a name that stops existing (a kernel that is removed or replaced) drops out of its block instead
# of failing the run, so the table can be read across a rewrite.
FORWARD = [
    "tensor:matmul",
    "tensor:take",
    "nn:softmax_cross_entropy_labels",
    "tensor:add",
    "tensor:relu",
    "tensor:cast_like",
    ":op/slice_sub",
    ":struct/append_arr_owned",
]
BACKWARD = [
    "tensor:matmul_grad_lhs",
    "tensor:matmul_grad_rhs",
    "nn:softmax_cross_entropy_labels_grad",
    "tensor:sum_to",
    "tensor:gt",
    "tensor:multiply",
]
OPTIMIZER = ["nn:adam"]

# Operators that run outside the training loop -- loading the data, the checkpoint, the setup.  They
# are in the profile but not in the step, so they are neither in a block nor in the denominator.
OUTSIDE = [
    "tensor:load_npy_f32",
    "tensor:load_npy_i64",
    "ckpt:save_tree",
    "ckpt:load_tree",
    "nn:adam_state",
    ":mark/filter_arr",
    ":op/add_l",
    "tensor:argmax_axis",
]

BLOCKS = (("forward", FORWARD), ("backward", BACKWARD), ("optimizer", OPTIMIZER))


def run(data: Path, steps: int, pipeline: str, profile_path: Path):
    """Run the training program through `std::profile` and return (parsed output, report)."""
    arguments = compare.PROFILES["standard"]
    args = argparse.Namespace(
        train=arguments["train"],
        val=arguments["val"],
        steps=steps,
        batch=arguments["batch"],
        lr=1.0e-3,
        log_every=arguments["log_every"],
        threads=1,
        seed=20261020,
        source=arguments["source"],
        mnist=None,
        profile="standard",
    )
    batches = args.train // args.batch
    if not (data / "manifest.json").is_file():
        compare.prepare_data(args, data, (args.steps + batches - 1) // batches)
    tokens = pipeline.split()
    if tokens and tokens[-1] == "std::nvm":
        tokens = tokens[:-1]
    passes = tokens + ["std::profile json=%s" % profile_path, "std::nvm"]
    command = compare.camel_arg_command(compare.camel_executable(None), data, args, passes)
    result = compare.run_command(command, compare.camel_env())
    if result["exit"] != 0:
        sys.stdout.write(result["stdout"])
        sys.stderr.write(result["stderr"])
        raise SystemExit("the program did not run: exit %d" % result["exit"])
    return compare.parse_camel_output(result["stdout"]), json.loads(
        profile_path.read_text(encoding="utf-8")
    )


def phases(report) -> tuple[dict, float, float]:
    """The three blocks, the loop's own time, and what ran outside the loop, in milliseconds."""
    rows = {row["uri"]: row["ns"] / 1e6 for row in report["operator_rows"]}
    blocks = {}
    for name, wanted in BLOCKS:
        parts = [(uri, rows[uri]) for uri in wanted if uri in rows]
        blocks[name] = (sum(ns for _, ns in parts), parts)
    loop = report["step"]["wall_per_round_ns"] * report["step"]["rounds"] / 1e6
    outside = sum(rows[uri] for uri in OUTSIDE if uri in rows)
    return blocks, loop, outside


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", required=True, help="the data directory make_data.py writes")
    parser.add_argument("--steps", type=int, default=1000)
    parser.add_argument(
        "--pipeline",
        default="std::macro std::nvm",
        help="the passes to run, e.g. 'std::macro std::vjp std::nvm' for the library's gradient",
    )
    parser.add_argument("--profile", default=None, help="where to write the profile JSON")
    args = parser.parse_args()

    data = Path(args.data).resolve()
    profile_path = Path(args.profile or data / "phases-profile.json").resolve()
    parsed, report = run(data, args.steps, args.pipeline, profile_path)
    blocks, loop, outside = phases(report)
    sys.stdout.write(
        "phases program=%s pipeline=%s steps=%d loop=%.3f ms\n"
        % (compare.TRAIN_CML.name, " ".join(args.pipeline.split()), args.steps, loop)
    )
    for name, _ in BLOCKS:
        total, parts = blocks[name]
        detail = " ".join("%s=%.1f" % (uri, ns) for uri, ns in parts[:5])
        sys.stdout.write(
            "phases %s %.3f ms %.1f%% [%s]\n" % (name, total, 100.0 * total / loop, detail)
        )
    total = sum(blocks[name][0] for name, _ in BLOCKS)
    sys.stdout.write(
        "phases sum=%.3f ms ratio=%.4f outside=%.3f ms last_loss=%.9f\n"
        % (total, total / loop, outside, parsed["steps"][-1][1])
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())