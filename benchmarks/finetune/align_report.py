"""E1 alignment report: three-party loss/weight agreement tables and curves.

Compares the loss sequences and per-step updated parameters of the three
parties — ORT-driven exported graph (driver.py), Camel-native loop
(run_native.py), PyTorch reference (reference_torch.py) — following the
paper's verification chain: ORT <-> Camel native, Camel native <-> PyTorch.
Writes a per-step CSV, a text summary, and a PNG with the overlaid
convergence curves and the error trajectory.

Usage:
  python align_report.py --model mlp --runs out/runs/mlp --out out/report
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

PARTIES = ["ort", "native", "torch"]
CHAINS = [("ort", "native"), ("native", "torch")]


def load_party(run_dir: Path, party: str, steps: int):
    base = run_dir / party
    losses = np.load(base / "losses.npy")
    weights = []
    for i in range(steps):
        step_dir = base / "trace" / f"step_{i}"
        weights.append({p.stem: np.load(p) for p in step_dir.glob("*.npy")})
    return losses, weights


def max_wdiff(a: dict[str, np.ndarray], b: dict[str, np.ndarray]) -> tuple[float, str]:
    best, name = -1.0, ""
    for k in a:
        d = float(np.max(np.abs(a[k].astype(np.float64) - b[k].astype(np.float64))))
        if d > best:
            best, name = d, k
    return best, name


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", required=True)
    p.add_argument("--runs", required=True, help="dir with ort/ native/ torch/ subdirs")
    p.add_argument("--out", required=True, help="report output dir")
    args = p.parse_args()

    runs = Path(args.runs)
    steps = len(np.load(runs / "ort" / "losses.npy"))
    losses, weights = {}, {}
    for party in PARTIES:
        losses[party], weights[party] = load_party(runs, party, steps)

    rows = []
    for i in range(steps):
        row = {"step": i}
        for party in PARTIES:
            row[f"loss_{party}"] = float(losses[party][i])
        for a, b in CHAINS:
            row[f"dloss_{a}_{b}"] = abs(row[f"loss_{a}"] - row[f"loss_{b}"])
            d, name = max_wdiff(weights[a][i], weights[b][i])
            row[f"dW_{a}_{b}"] = d
            row[f"dW_{a}_{b}_at"] = name
        rows.append(row)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0].keys())
    csv_path = out / f"{args.model}_errors.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    lines = [
        f"E1 three-way alignment -- {args.model} ({steps} steps)",
        f"loss: {PARTIES[0]} {losses[PARTIES[0]][0]:.8g} -> {losses[PARTIES[0]][-1]:.8g}"
        f"   native {losses['native'][0]:.8g} -> {losses['native'][-1]:.8g}"
        f"   torch {losses['torch'][0]:.8g} -> {losses['torch'][-1]:.8g}",
    ]
    for a, b in CHAINS:
        dl = max(r[f"dloss_{a}_{b}"] for r in rows)
        i_dl = max(range(steps), key=lambda i: rows[i][f"dloss_{a}_{b}"])
        dw = max(r[f"dW_{a}_{b}"] for r in rows)
        i_dw = max(range(steps), key=lambda i: rows[i][f"dW_{a}_{b}"])
        lines.append(
            f"{a} vs {b}: max|dloss| = {dl:.3e} (step {i_dl}), "
            f"max|dW| = {dw:.3e} (step {i_dw}, param {rows[i_dw][f'dW_{a}_{b}_at']})"
        )
    summary = "\n".join(lines)
    (out / f"{args.model}_summary.txt").write_text(summary + "\n")
    print(summary)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(11, 4.2))
    style = {"ort": dict(color="tab:blue", lw=2.0), "native": dict(color="tab:orange", lw=1.4, ls="--"),
             "torch": dict(color="tab:green", lw=1.0, ls=":")}
    label = {"ort": "ORT InferenceSession (exported step)", "native": "Camel native", "torch": "PyTorch"}
    for party in PARTIES:
        ax1.plot(range(steps), losses[party], **style[party], label=label[party])
    ax1.set_yscale("log")
    ax1.set_xlabel("step")
    ax1.set_ylabel("loss (log)")
    ax1.set_title(f"{args.model}: fine-tuning convergence, three parties")
    ax1.legend(fontsize=8)
    ax1.grid(alpha=0.3)
    for a, b in CHAINS:
        ax2.plot(range(steps), [r[f"dloss_{a}_{b}"] for r in rows], label=f"|dloss| {a}-{b}")
        ax2.plot(range(steps), [r[f"dW_{a}_{b}"] for r in rows], ls="--", label=f"max|dW| {a}-{b}")
    ax2.set_yscale("log")
    ax2.set_xlabel("step")
    ax2.set_ylabel("absolute difference (log)")
    ax2.set_title(f"{args.model}: per-step numerical agreement")
    ax2.legend(fontsize=8)
    ax2.grid(alpha=0.3)
    fig.tight_layout()
    png_path = out / f"{args.model}_curves.png"
    fig.savefig(png_path, dpi=160)
    print(f"[align_report] wrote {csv_path}, {out / (args.model + '_summary.txt')}, {png_path}")


if __name__ == "__main__":
    main()
