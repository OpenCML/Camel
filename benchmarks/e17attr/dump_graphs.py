"""T13 account 1 (static side): dump the program graph after `std::macro` and after
`std::macro std::opt`, per model.

Outputs per model:
  <out>/<model>.macro.rgir.dot        AI-readable DOT (std::rgir) of the macro graph
  <out>/<model>.simplify.rgir.dot     DOT of the macro+simplify graph
  <out>/<model>.stats.jsonl           GRAPH_STATS lines at both points
  <out>/<model>.op_histogram.json     per-graph, per-operator node counts parsed from
                                      the DOTs (OPER nodes only), both variants

Note: std::opt inlines calls, so node totals over ALL graphs go UP while
the executed closure's work goes down; the histogram is context. The executed-op
ground truth is run_opstats.py's calls/step.

Usage: .venv/Scripts/python e17attr/dump_graphs.py --out <dir>
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

BENCH_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(BENCH_ROOT))

from common.camel import _camel_env  # noqa: E402
from common.configs import camel_binary, camel_model  # noqa: E402

MODELS = ["mlp_big", "lenet_big"]

NODE = re.compile(r'^\s*([NF]\w+) \[label="((?:[^"\\]|\\.)*)", type="(\w+)"')
SUBGRAPH = re.compile(r"^\s*subgraph cluster_(\w+) \{")
LABEL = re.compile(r'^\s*label="((?:[^"\\]|\\.)*)";')


def parse_dot(text: str) -> dict:
    """graph name -> {operator label -> count} for OPER nodes; other node types as <TYPE>."""
    graphs: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    stack: list[list] = []  # frames of [fid, name|None]
    for line in text.splitlines():
        m = SUBGRAPH.match(line)
        if m:
            stack.append([m.group(1), None])
            continue
        m = LABEL.match(line)
        if m and stack and stack[-1][1] is None:
            stack[-1][1] = m.group(1)
            fid, name = stack[-1]
            graphs[f"{name} ({fid})"]  # ensure key exists even if no OPERs
            continue
        if line.strip() == "}" and stack:
            stack.pop()
            continue
        m = NODE.match(line)
        if m and stack:
            fid, name = stack[-1]
            key = f"{name or fid} ({fid})"
            label, ntype = m.group(2), m.group(3)
            graphs[key][label if ntype == "OPER" else f"<{ntype}>"] += 1
    return {g: dict(c) for g, c in graphs.items()}


def run_passes(model: str, passes: list[str]) -> str:
    env = _camel_env(model, {})
    cmd = [str(camel_binary()), str(camel_model(model)), *passes]
    proc = subprocess.run(cmd, cwd=camel_model(model).parent, env=env, capture_output=True, text=True, timeout=600)
    # std::rgir ends the pipeline with an empty graph; nothing executes.
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({proc.returncode}):\n{proc.stderr}")
    return proc.stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--models", default=",".join(MODELS))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    for model in args.models.split(","):
        stats_lines = []
        histogram = {}
        for tag, passes in [("macro", ["std::macro"]), ("simplify", ["std::macro", "std::opt"])]:
            out = run_passes(model, [*passes, "std::stats", "std::rgir"])
            dot_lines, in_dot = [], False
            for line in out.splitlines():
                if line.startswith("GRAPH_STATS "):
                    stats_lines.append({"variant": tag, **json.loads(line[len("GRAPH_STATS "):])})
                if line.startswith("digraph "):
                    in_dot = True
                if in_dot:
                    dot_lines.append(line)
            dot = "\n".join(dot_lines)
            (args.out / f"{model}.{tag}.rgir.dot").write_text(dot)
            histogram[tag] = parse_dot(dot)
            print(f"{model:10s} {tag:9s} graphs={stats_lines[-1]['graphs']:3d} nodes={stats_lines[-1]['nodes']:5d} opers={stats_lines[-1]['opers']:4d}")

        (args.out / f"{model}.stats.jsonl").write_text("\n".join(json.dumps(s) for s in stats_lines) + "\n")
        (args.out / f"{model}.op_histogram.json").write_text(json.dumps(histogram, indent=1, sort_keys=True))
    print(f"[done] wrote DOTs, stats and histograms under {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
