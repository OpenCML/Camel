# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""E13/T5 Level-2 probes: ONNX checker + ONNX Runtime vs Camel native dumps.

For every exported graph under <artifacts>/onnx/<probe>_<what>.onnx this runs
  1. onnx.checker.check_model(full_check=True) and an op/domain census
     (every node must be ai.onnx standard domain),
  2. one ORT (CPUExecutionProvider) execution per probe arm (a / b / 0) with
     the initial parameters and the arm input produced by the Camel MODE=run
     dumps, comparing the loss output against Camel's own native loss
     (artifact-correctness gate: max|diff| <= 1e-6).

Probes whose export failed (no .onnx file) are reported as SKIP; their error
logs under logs/ are the evidence for the matrix's cross cells.

Usage: python ort_check.py --artifacts <e13 artifacts dir>
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np

GATE = 1e-6
ARMS = ("a", "b", "0")


def check_one(art: Path, probe: str, what: str, report: dict) -> bool:
    import onnx
    import onnxruntime as ort

    path = art / "onnx" / f"{probe}_{what}.onnx"
    if not path.exists():
        print(f"{probe} {what}: SKIP (no exported graph — see logs/{probe}_export_{what}.log)")
        report[f"{probe}_{what}"] = {"status": "missing"}
        return True

    model = onnx.load(str(path))
    onnx.checker.check_model(model, full_check=True)

    def census(graph, domains, ops):
        for n in graph.node:
            domains[n.domain or "ai.onnx"] += 1
            ops[n.op_type] += 1
            for a in n.attribute:
                if a.g:
                    census(a.g, domains, ops)

    domains, ops = Counter(), Counter()
    census(model.graph, domains, ops)
    nonstandard = {d: c for d, c in domains.items() if d != "ai.onnx"}
    print(f"{probe} {what}: checker PASS, nodes={sum(ops.values())}, if={ops.get('If', 0)}, domains={dict(domains)}")

    raw = art / "raw" / probe
    session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    params = {p.stem: np.load(p) for p in (raw / "init").glob("*.npy")}
    ok = True
    arms_out = {}
    for arm in ARMS:
        x = np.load(raw / f"x{arm}.npy")
        feeds = {}
        for inp in session.get_inputs():
            feeds[inp.name] = x if inp.name.startswith("input") else params[inp.name]
        got = session.run(None, feeds)
        native_loss = float(np.load(raw / f"loss_{arm}.npy").reshape(-1)[0])
        got_loss = float(np.asarray(got[0]).reshape(-1)[0])
        d = abs(got_loss - native_loss)
        ok &= d <= GATE
        arms_out[arm] = {"ort_loss": got_loss, "native_loss": native_loss, "|d|": d}
        line = f"  arm {arm}: ort={got_loss:.9g} native={native_loss:.9g} |d|={d:.2e}"
        if what == "step":
            # A training step also yields the updated parameters, one output per leaf; each
            # must match the native post-step dump as well (same gate).
            worst = 0.0
            for i, out in enumerate(session.get_outputs()):
                if not out.name.startswith("output_1_"):
                    continue
                native_w = np.load(raw / f"arm_{arm}" / f"{out.name[len('output_1_'):]}.npy")
                dw = float(np.max(np.abs(np.asarray(got[i]) - native_w)))
                worst = max(worst, dw)
            ok &= worst <= GATE
            line += f" weights|d|={worst:.2e}"
        print(f"{line} {'ok' if d <= GATE else 'FAIL'}")
    report[f"{probe}_{what}"] = {
        "status": "checked",
        "nodes": sum(ops.values()),
        "if_ops": ops.get("If", 0),
        "op_census": dict(ops),
        "nonstandard_domains": nonstandard,
        "arms": arms_out,
        "ok": bool(ok and not nonstandard),
    }
    return ok and not nonstandard


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--probes", nargs="*", default=["l2a", "l2b", "l2c", "l2d"])
    args = parser.parse_args()

    report = {"gate": GATE}
    ok_all = True
    for probe in args.probes:
        whats = ("step",) if probe == "l2x2" else ("fwd", "step")
        for what in whats:
            ok_all &= check_one(args.artifacts, probe, what, report)
    out = args.artifacts / "raw" / "ort_vs_native.json"
    out.write_text(json.dumps(report, indent=2))
    print(f"ort vs native (gate {GATE}):", "PASS" if ok_all else "FAIL", "->", out)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
