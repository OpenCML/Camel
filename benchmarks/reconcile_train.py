"""Operator-count reconciliation of the training workload (run before any timing).

A step time only means "a training step" if the configuration executed one: the
forward pass, the backward pass, and an update that something consumes. This
script checks that per (model, configuration, protocol) and writes a verdict;
train.py --time --reconciliation times only the pairs whose verdict is PASS.

Per Camel configuration it runs the training workload twice with
``CAMEL_TENSOR_OPSTATS=1`` (same warmup, ``--reps0`` and ``--reps`` timed steps)
and takes the difference of the operator call counts divided by the difference
in steps: the operators executed per step, exact when every step does the same
work (a non-integral quotient is an error). Set-up operators, the first-step
overhead and the final loss evaluation cancel in the difference.

Criteria of a PASS (all required):

  1. Gradient operators. The per-step multiset of gradient operators (operators
     whose name contains ``grad``) equals the expected one. The expectation is
     what ``camel_nvm`` (``std::macro std::nvm``) executes per step under the
     stateful protocol: NodeVM executes the macro-expanded program in source
     order without optimizing, so that is every gradient operator the
     differentiated program contains. It must be non-empty.
  2. The update is consumed: the loss changes from step to step and the final
     parameters differ from the initial ones. Under the stateless protocol the
     program starts every step from the same weights and never produces final
     parameters, so this is "no" by construction.
  3. The final parameters after all steps agree with PyTorch running the same
     number of SGD steps from the same weights on the same input (``--tol``,
     max|diff| / max|ref| per tensor), and so does the loss of every step.

``camel_export_ort`` (the exported training step on ONNX Runtime) has no Camel
operator counts; its row is the artifact gate only: K steps on ORT against K
steps of Camel's own execution (not against PyTorch). ``torch_eager`` is the
reference and gets the verdict REFERENCE.

Outputs under ``--out``:
  raw/recon_<protocol>_<model>_<config>_{short,long}.log   every Camel run, unedited
  summary/reconciliation.csv                               one row per model x config x protocol
  summary/opcounts.csv                                     per-step operator counts behind it

Exit status: 0 if every stateful row passes, 1 otherwise (stateless rows are
expected to fail; they show that the check detects a removed backward pass).

Usage:
  .venv/bin/python reconcile_train.py --models mlp,lenet --out results/reconcile
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

BENCH_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_ROOT))

import train  # noqa: E402
from common import stats  # noqa: E402

REFERENCE = "camel_nvm"
SETUP_OPS = {"load_npy", "save_npy", "set_num_threads"}
OPROW = re.compile(r"^  (\S+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*$", re.M)

COLUMNS = [
    "model",
    "config",
    "grad_ops_expected",
    "grad_ops_measured",
    "update_consumed",
    "params_err_vs_torch",
    "verdict",
    # Beyond the plan's columns: what the row is about and why it failed.
    "protocol",
    "steps",
    "grad_ops_missing",
    "grad_ops_extra",
    "all_ops_equal_reference",
    "loss_err_vs_torch",
    "params_err_vs_camel_native",
    "latency_over_span",
    "reason",
]
OP_COLUMNS = ["model", "protocol", "config", "operator", "calls_per_step", "kernel_ms_per_step"]


def parse_opstats(stderr: str) -> Dict[str, Tuple[int, float]]:
    """operator -> (calls, total ms) from the CAMEL_TENSOR_OPSTATS table."""
    return {m.group(1): (int(m.group(2)), float(m.group(3))) for m in OPROW.finditer(stderr)}


def per_step_ops(short: str, long: str, steps: int) -> Dict[str, Tuple[int, float]]:
    """Operators executed per step: (long run - short run) / `steps` more steps."""
    a, b = parse_opstats(short), parse_opstats(long)
    if not a or not b:
        raise RuntimeError("no operator table in stderr (is CAMEL_TENSOR_OPSTATS supported by this build?)")
    out: Dict[str, Tuple[int, float]] = {}
    for op in sorted(set(a) | set(b)):
        if op in SETUP_OPS:
            continue
        calls = b.get(op, (0, 0.0))[0] - a.get(op, (0, 0.0))[0]
        ms = b.get(op, (0, 0.0))[1] - a.get(op, (0, 0.0))[1]
        if calls % steps:
            raise RuntimeError(f"operator {op}: {calls} calls over {steps} steps is not a whole number per step")
        if calls:
            out[op] = (calls // steps, ms / steps)
    return out


def grad_ops(ops: Dict[str, Tuple[int, float]]) -> Dict[str, int]:
    return {op: calls for op, (calls, _) in ops.items() if "grad" in op}


def show(multiset: Dict[str, int]) -> str:
    return ";".join(f"{op}:{n}" for op, n in sorted(multiset.items())) or "(none)"


def difference(a: Dict[str, int], b: Dict[str, int]) -> Dict[str, int]:
    """Multiset a - b."""
    return {op: n - b.get(op, 0) for op, n in a.items() if n > b.get(op, 0)}


def measure(model: str, config: str, protocol: str, args, raw: Path) -> dict:
    """Two op-stats runs of one Camel configuration; per-step operators, losses, final parameters."""
    env = {"CAMEL_TENSOR_OPSTATS": "1", "CAMEL_BENCH_TRAIN_PROTOCOL": protocol}
    runs = {}
    for label, reps in (("short", args.reps0), ("long", args.reps)):
        log = raw / f"recon_{protocol}_{model}_{config}_{label}.log"
        runs[label] = train.camel_ksteps(model, config, args.warmup, reps, threads=args.threads, log=log, extra_env=env)
        ran = train.protocol_of(runs[label]["stdout"])
        if ran != protocol:
            raise RuntimeError(f"asked for the {protocol} protocol, the model ran {ran!r}")
    long = runs["long"]
    steps = long["steps"]
    timed = [s["ms"] for s in steps if s["timed"]]
    spans = train.step_spans(steps)
    return {
        "ops": per_step_ops(runs["short"]["stderr"], long["stderr"], args.reps - args.reps0),
        "losses": [s["loss"] for s in steps],
        "final_params_loss": long["final_params_loss"],
        "params": long["params"],
        "latency_over_span": stats.median(timed) / stats.median(spans) if timed and spans else None,
    }


def fmt(value: Optional[float]) -> str:
    return "n/a" if value is None else f"{value:.3g}"


def reconcile_model(model: str, args, raw: Path) -> Tuple[List[dict], List[dict]]:
    rows: List[dict] = []
    op_rows: List[dict] = []
    steps = max(args.warmup, 1) + args.reps
    ref = train.torch_ksteps(model, steps)
    camel_configs = [c for c in args.configs if c in train.CAMEL_CONFIGS]
    if REFERENCE not in camel_configs:
        camel_configs.insert(0, REFERENCE)
    camel_configs.sort(key=lambda c: c != REFERENCE)  # the reference first

    if train.STATEFUL in args.protocols:
        moved = train.params_moved(ref["params"], ref["initial"])
        rows.append(
            {
                "model": model, "config": train.TORCH_CONFIG, "protocol": train.STATEFUL, "steps": steps,
                "grad_ops_expected": "n/a", "grad_ops_measured": "n/a (PyTorch operators are not counted here)",
                "update_consumed": "yes" if moved > 0 and len(set(ref["losses"])) > 1 else "no",
                "params_err_vs_torch": "reference", "verdict": "REFERENCE",
                "reason": f"largest relative parameter change over {steps} steps {moved:.3g}",
            }
        )

    expected: Optional[Dict[str, int]] = None
    reference_ops: Optional[Dict[str, int]] = None
    native_run: Optional[dict] = None
    for protocol in args.protocols:
        for config in camel_configs:
            row = {"model": model, "config": config, "protocol": protocol, "steps": steps}
            try:
                got = measure(model, config, protocol, args, raw)
            except Exception as exc:
                rows.append({**row, "verdict": "FAIL", "reason": f"run failed: {str(exc).strip()[-600:]}"})
                continue
            for op, (calls, ms) in got["ops"].items():
                op_rows.append(
                    {"model": model, "protocol": protocol, "config": config, "operator": op,
                     "calls_per_step": calls, "kernel_ms_per_step": f"{ms:.4f}"}
                )
            measured = grad_ops(got["ops"])
            all_ops = {op: calls for op, (calls, _) in got["ops"].items()}
            if config == REFERENCE and protocol == train.STATEFUL:
                expected, reference_ops, native_run = measured, all_ops, got
            reasons = []
            row["grad_ops_measured"] = show(measured)
            if expected is None:
                row["grad_ops_expected"] = "n/a"
                reasons.append(f"no expectation: the stateful {REFERENCE} reference did not run")
            else:
                row["grad_ops_expected"] = show(expected)
                missing, extra = difference(expected, measured), difference(measured, expected)
                row["grad_ops_missing"], row["grad_ops_extra"] = show(missing), show(extra)
                row["all_ops_equal_reference"] = "yes" if all_ops == reference_ops else "no"
                if not expected:
                    reasons.append("the reference executes no gradient operator")
                if missing:
                    reasons.append(f"gradient operators not executed: {show(missing)}")
                if extra:
                    reasons.append(f"gradient operators beyond the expectation: {show(extra)}")

            losses = got["losses"]
            if protocol == train.STATEFUL:
                moved = train.params_moved(got["params"], ref["initial"])
                consumed = len(set(losses)) > 1 and moved > 0
                traj = train.compare_trajectory(losses, got["final_params_loss"], ref)
                par = train.compare_params(got["params"], ref["params"])
                loss_err = max(traj["loss_err_max"], traj["final_params_loss_err"])
                row["params_err_vs_torch"] = f"{par['params_err']:.3g}"
                row["loss_err_vs_torch"] = f"{loss_err:.3g}"
                if par["params_err"] > args.tol:
                    reasons.append(f"final parameters differ from PyTorch by {par['params_err']:.3g} > {args.tol:g}")
                if loss_err > args.tol:
                    reasons.append(f"losses differ from PyTorch by {loss_err:.3g} > {args.tol:g}")
            else:
                consumed = False
                row["params_err_vs_torch"] = "n/a (no final parameters: every step starts from the same weights)"
                if len(set(losses)) > 1:
                    reasons.append("stateless steps reported different losses")
            row["update_consumed"] = "yes" if consumed else "no"
            if not consumed:
                reasons.append("updates not consumed")
            row["latency_over_span"] = fmt(got["latency_over_span"])
            row["verdict"] = "FAIL" if reasons else "PASS"
            row["reason"] = "; ".join(reasons)
            rows.append(row)

    if train.ORT_CONFIG in args.configs and train.STATEFUL in args.protocols:
        row = {
            "model": model, "config": train.ORT_CONFIG, "protocol": train.STATEFUL, "steps": steps,
            "grad_ops_expected": "n/a", "grad_ops_measured": "n/a (ORT executes the exported graph)",
            "params_err_vs_torch": "n/a (artifact gate is against Camel native)",
        }
        try:
            if native_run is None:
                raise RuntimeError(f"the stateful {REFERENCE} run to compare with did not succeed")
            ort = train.ort_ksteps(model, steps, args.threads)
            native = {"losses": native_run["losses"], "final_params_loss": 0.0, "params": native_run["params"]}
            traj = train.compare_trajectory(ort["losses"], 0.0, native)
            par = train.compare_params(ort["params"], native["params"])
            moved = train.params_moved(ort["params"], ref["initial"])
            consumed = len(set(ort["losses"])) > 1 and moved > 0
            reasons = []
            if not consumed:
                reasons.append("updates not consumed")
            if par["params_err"] > args.tol:
                reasons.append(f"final parameters differ from Camel native by {par['params_err']:.3g} > {args.tol:g}")
            if traj["loss_err_max"] > args.tol:
                reasons.append(f"losses differ from Camel native by {traj['loss_err_max']:.3g} > {args.tol:g}")
            row.update(
                update_consumed="yes" if consumed else "no",
                params_err_vs_camel_native=f"{par['params_err']:.3g}",
                verdict="FAIL" if reasons else "PASS",
                reason="; ".join(reasons) or "artifact gate only (K steps on ORT equal K native steps); operators not counted",
            )
        except Exception as exc:
            row.update(verdict="FAIL", reason=f"run failed: {str(exc).strip()[-600:]}")
        rows.append(row)
    return rows, op_rows


def write_csv(path: Path, columns: List[str], rows: List[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({c: row.get(c, "") for c in columns})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--models", default=",".join(train.MODELS))
    ap.add_argument("--configs", default=",".join([*train.CAMEL_CONFIGS, train.ORT_CONFIG]))
    ap.add_argument("--protocols", default=f"{train.STATEFUL},{train.STATELESS}")
    ap.add_argument("--warmup", type=int, default=40)
    ap.add_argument("--reps", type=int, default=100, help="timed steps of the long run (the one compared with PyTorch)")
    ap.add_argument("--reps0", type=int, default=50, help="timed steps of the short run")
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--tol", type=float, default=1e-4)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    args.configs = [c for c in args.configs.split(",") if c]
    args.protocols = [p for p in args.protocols.split(",") if p]
    if train.STATEFUL in args.protocols:  # the stateful reference provides the expectation
        args.protocols.sort(key=lambda p: p != train.STATEFUL)
    if not 0 < args.reps0 < args.reps:
        ap.error("need 0 < --reps0 < --reps")

    raw = args.out / "raw"
    rows: List[dict] = []
    op_rows: List[dict] = []
    for model in [m for m in args.models.split(",") if m]:
        try:
            got, ops = reconcile_model(model, args, raw)
        except Exception as exc:
            got, ops = [{"model": model, "config": "*", "verdict": "FAIL", "reason": f"model failed: {str(exc).strip()[-600:]}"}], []
        rows += got
        op_rows += ops
        for r in got:
            print(
                f"{r.get('verdict', '?'):9s} {r['model']:16s} {r['config']:16s} {r.get('protocol', ''):9s} "
                f"grads {r.get('grad_ops_measured', '')}  consumed {r.get('update_consumed', '')}  "
                f"params_err {r.get('params_err_vs_torch') or r.get('params_err_vs_camel_native', '')}"
                + (f"\n          -> {r['reason']}" if r.get("reason") else ""),
                flush=True,
            )
    write_csv(args.out / "summary" / "reconciliation.csv", COLUMNS, rows)
    write_csv(args.out / "summary" / "opcounts.csv", OP_COLUMNS, op_rows)
    stateful_fail = [r for r in rows if r.get("protocol", train.STATEFUL) == train.STATEFUL and r.get("verdict") == "FAIL"]
    print(f"[done] wrote {args.out / 'summary' / 'reconciliation.csv'}; stateful rows failing: {len(stateful_fail)}")
    return 1 if stateful_fail else 0


if __name__ == "__main__":
    sys.exit(main())
