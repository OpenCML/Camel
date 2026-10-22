# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Where the naive forms lose bits, measured: bf16 overflow, f32 underflow, and fp64 error.

The operations are the four of the training step: softmax, log_softmax, softmax cross-entropy,
and the matmul reduction.  For each, the naive implementation is the one written in
``train_step.cml`` (exp / sum / log); the expert is the stable form the design names
(``softmax``, ``log_softmax``, a fused cross-entropy, and an f32 accumulation for bf16 inputs).

The thresholds are found by measurement, not assumed: an input range is scanned for the first
value whose result is not finite.  The fp64 error is the maximum absolute error over a fixed
random sample, with the float64 computation as the reference.

The Camel program ``accuracy.cml`` runs the same f32 measurements on the language side; pass
``--camel`` to include them in the same file.

  python accuracy.py --output accuracy.json
  python accuracy.py --camel /path/to/out/latest/bin/camel --output accuracy.json
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F

HERE = Path(__file__).resolve().parent


def is_finite(t: torch.Tensor) -> bool:
    return bool(torch.isfinite(t).all())


def naive_softmax(x: torch.Tensor) -> torch.Tensor:
    e = torch.exp(x)
    return e / e.sum(-1, keepdim=True)


def naive_log_softmax(x: torch.Tensor) -> torch.Tensor:
    return torch.log(naive_softmax(x))


def naive_cross_entropy(logits: torch.Tensor, labels: torch.Tensor) -> torch.Tensor:
    p = naive_softmax(logits)
    return -torch.log(p.gather(-1, labels.unsqueeze(-1))).mean()


# ---------------------------------------------------------------------------
# Thresholds


def first_bad(values, fn):
    for value in values:
        if not is_finite(fn(value)):
            return value
    return None


def overflow_threshold(dtype, high: int) -> dict:
    """The first largest-logit value where naive softmax stops being finite."""
    def naive(m):
        return naive_softmax(torch.tensor([[0.0, float(m)]], dtype=dtype))

    def stable(m):
        return torch.softmax(torch.tensor([[0.0, float(m)]], dtype=dtype), -1)

    bad = first_bad(range(64, high), naive)
    return {
        "naive_overflow_at": bad,
        "naive_at_1000_finite": is_finite(naive(1000)),
        "stable_at_1000_finite": is_finite(stable(1000)),
        "stable_at_100000_finite": is_finite(stable(100000)),
    }


def underflow_threshold(dtype, high: int) -> dict:
    """The first offset where exp underflows to zero: naive 0/0, stable finite."""
    def naive(m):
        return naive_softmax(torch.tensor([[-float(m), -float(m) - 1.0]], dtype=dtype))

    def stable(m):
        return torch.softmax(torch.tensor([[-float(m), -float(m) - 1.0]], dtype=dtype), -1)

    bad = first_bad(range(64, high), naive)
    return {
        "naive_nonfinite_at": bad,
        "naive_at_1000_finite": is_finite(naive(1000)),
        "stable_at_1000_finite": is_finite(stable(1000)),
    }


def log_softmax_threshold(dtype, high: int) -> dict:
    def naive(m):
        return naive_log_softmax(torch.tensor([[-float(m), -float(m) - 1.0]], dtype=dtype))

    def stable(m):
        return F.log_softmax(torch.tensor([[-float(m), -float(m) - 1.0]], dtype=dtype), -1)

    bad = first_bad(range(64, high), naive)
    return {
        "naive_nonfinite_at": bad,
        "naive_at_1000_finite": is_finite(naive(1000)),
        "stable_at_1000_finite": is_finite(stable(1000)),
        "stable_1000_value": float(stable(1000)[0, 0]),
    }


def cross_entropy_threshold(dtype, high: int) -> dict:
    labels = torch.zeros(1, dtype=torch.long)

    def offset(k):
        logits = torch.zeros(1, 8, dtype=dtype) + float(k)
        return torch.tensor([float(naive_cross_entropy(logits, labels))])

    def offset_stable(k):
        logits = torch.zeros(1, 8, dtype=dtype) + float(k)
        return F.cross_entropy(logits, labels).reshape(1)

    bad = first_bad(range(64, high), offset)

    def underflow(m):
        logits = torch.zeros(1, 8, dtype=dtype)
        logits[0, 0] = -float(m)
        return torch.tensor([float(naive_cross_entropy(logits, labels))])

    def underflow_stable(m):
        logits = torch.zeros(1, 8, dtype=dtype)
        logits[0, 0] = -float(m)
        return F.cross_entropy(logits, labels).reshape(1)

    return {
        "naive_offset_overflow_at": bad,
        "naive_offset_1000_finite": is_finite(offset(1000)),
        "stable_offset_1000_finite": is_finite(offset_stable(1000)),
        "naive_true_class_underflow_at": first_bad(range(64, high), underflow),
        "naive_true_class_1000_finite": is_finite(underflow(1000)),
        "stable_true_class_1000_finite": is_finite(underflow_stable(1000)),
    }


# ---------------------------------------------------------------------------
# fp64 error


def errors(value: torch.Tensor, reference: torch.Tensor) -> dict:
    value = value.double()
    diff = (value - reference).abs()
    denom = reference.abs().clamp_min(1e-30)
    scale = float(reference.abs().max())
    return {
        "max_abs": float(diff.max()),
        "max_rel": float((diff / denom).max()),
        "norm_rel": float(diff.max()) / max(scale, 1e-30),
        "rms_abs": float((diff * diff).mean().sqrt()),
    }


def precision(dtype) -> dict:
    torch.manual_seed(7)
    x64 = torch.randn(1024, 128, dtype=torch.float64) * 4.0
    x = x64.to(dtype)
    soft_ref = torch.softmax(x64, -1)
    log_ref = F.log_softmax(x64, -1)

    logits64 = torch.randn(4096, 32, dtype=torch.float64) * 4.0
    logits = logits64.to(dtype)
    labels = torch.randint(0, 32, (4096,), generator=torch.Generator().manual_seed(11))
    ce_ref = F.cross_entropy(logits64, labels).reshape(1)

    out = {
        "softmax_naive": errors(naive_softmax(x), soft_ref),
        "softmax_stable": errors(torch.softmax(x, -1), soft_ref),
        "log_softmax_naive": errors(naive_log_softmax(x), log_ref),
        "log_softmax_stable": errors(F.log_softmax(x, -1), log_ref),
        "cross_entropy_naive": errors(naive_cross_entropy(logits, labels).reshape(1), ce_ref),
        "cross_entropy_stable": errors(F.cross_entropy(logits, labels).reshape(1), ce_ref),
    }
    return out


def matmul_precision(dtype, ks=(16, 64, 256, 1024)) -> dict:
    torch.manual_seed(13)
    rows = {}
    for k in ks:
        a64 = torch.randn(256, k, dtype=torch.float64)
        b64 = torch.randn(k, 128, dtype=torch.float64)
        ref = a64 @ b64
        a, b = a64.to(dtype), b64.to(dtype)
        rows[str(k)] = errors(a @ b, ref)
    return rows


def bf16_accumulation() -> dict:
    """A dot product whose exact result is 1: bf16 accumulation rounds it away, f32 does not.

    1e4 + 1 is representable in f32 (its spacing at 1e4 is about 0.001) but not in bf16 (spacing
    64), so the bf16 accumulator drops the 1 before the -1e4 is added.
    """
    a = torch.tensor([[1e4, 1.0, -1e4]], dtype=torch.float32)
    b = torch.tensor([[1.0], [1.0], [1.0]], dtype=torch.float32)
    exact = float((a.double() @ b.double())[0, 0])
    f32 = float((a @ b)[0, 0])

    a_bf, b_bf = a.to(torch.bfloat16), b.to(torch.bfloat16)
    # A naive reduction keeps the accumulator in bf16 after every step.
    acc = torch.zeros(1, 1, dtype=torch.bfloat16)
    for k in range(a.shape[1]):
        acc = acc + a_bf[:, k : k + 1] * b_bf[k : k + 1, :]
    naive = float(acc[0, 0])
    # The expert reduction multiplies bf16 inputs and accumulates in f32.
    acc32 = torch.zeros(1, 1, dtype=torch.float32)
    for k in range(a.shape[1]):
        acc32 = acc32 + a_bf[:, k : k + 1].float() * b_bf[k : k + 1, :].float()
    expert = float(acc32[0, 0])
    torch_bf16 = float((a_bf @ b_bf)[0, 0])
    return {
        "inputs": a.tolist(),
        "exact_fp64": exact,
        "f32_matmul": f32,
        "bf16_naive_accumulation": naive,
        "bf16_f32_accumulation": expert,
        "torch_bf16_matmul": torch_bf16,
    }


# ---------------------------------------------------------------------------
# Camel f32 measurements


def camel_env(camel: Path) -> dict[str, str]:
    install = camel.resolve().parents[1]
    env = os.environ.copy()
    env["CAMEL_HOME"] = str(install)
    env["PATH"] = str(install / "bin") + os.pathsep + env.get("PATH", "")
    env["CAMEL_NUM_THREADS"] = "1"
    return env


def run_camel(camel: Path) -> dict:
    result = subprocess.run(
        [str(camel), str(HERE / "accuracy.cml"), "std::fvm"],
        capture_output=True, text=True, env=camel_env(camel),
    )
    if result.returncode != 0:
        return {"error": result.stdout + result.stderr}
    records = []
    for line in result.stdout.splitlines():
        match = re.match(r"accuracy camel (\w+) (.*)", line)
        if match:
            fields = {}
            for item in match.group(2).split():
                if "=" in item:
                    key, value = item.split("=", 1)
                    fields[key] = value
            records.append({"operation": match.group(1), **fields})
    return {"records": records, "stdout": result.stdout}


def table(rows, header):
    lines = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    for row in rows:
        lines.append("| " + " | ".join(str(cell) for cell in row) + " |")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(HERE / "accuracy.json"))
    parser.add_argument("--camel", default=None)
    args = parser.parse_args()

    dtypes = [
        ("bfloat16", torch.bfloat16, 96),
        ("float32", torch.float32, 120),
        ("float64", torch.float64, 780),
    ]
    overflow = {}
    for name, dtype, high in dtypes:
        overflow[name] = {
            "softmax": overflow_threshold(dtype, high),
            "softmax_underflow": underflow_threshold(dtype, high + 20),
            "log_softmax": log_softmax_threshold(dtype, high + 20),
            "cross_entropy": cross_entropy_threshold(dtype, high),
        }

    prec = {name: precision(dtype) for name, dtype, _ in dtypes[:2]}
    matmul = {name: matmul_precision(dtype) for name, dtype, _ in dtypes[:2]}
    acc = bf16_accumulation()
    camel = run_camel(Path(args.camel)) if args.camel else None

    result = {
        "schema": 1,
        "torch": torch.__version__,
        "thresholds": overflow,
        "precision": prec,
        "matmul": matmul,
        "bf16_accumulation": acc,
        "camel_f32": camel,
    }
    output = Path(args.output)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    print("## Where the naive form stops being finite")
    rows = []
    for dtype in ("bfloat16", "float32", "float64"):
        data = overflow[dtype]
        rows.append((dtype, "softmax", data["softmax"]["naive_overflow_at"],
                     "finite" if data["softmax"]["stable_at_100000_finite"] else "inf"))
        rows.append((dtype, "softmax underflow", data["softmax_underflow"]["naive_nonfinite_at"],
                     "finite" if data["softmax_underflow"]["stable_at_1000_finite"] else "inf"))
        rows.append((dtype, "log_softmax", data["log_softmax"]["naive_nonfinite_at"],
                     "finite" if data["log_softmax"]["stable_at_1000_finite"] else "inf"))
        rows.append((dtype, "cross-entropy offset", data["cross_entropy"]["naive_offset_overflow_at"],
                     "finite" if data["cross_entropy"]["stable_offset_1000_finite"] else "inf"))
        rows.append((dtype, "cross-entropy true class",
                     data["cross_entropy"]["naive_true_class_underflow_at"],
                     "finite" if data["cross_entropy"]["stable_true_class_1000_finite"] else "inf"))
    print(table(rows, ("dtype", "operation", "first non-finite input", "expert at 1000")))

    print()
    print("## Error against float64 (random sample, max absolute)")
    rows = []
    for dtype in ("bfloat16", "float32"):
        for op in ("softmax", "log_softmax", "cross_entropy"):
            naive = prec[dtype][f"{op}_naive"]["max_abs"]
            stable = prec[dtype][f"{op}_stable"]["max_abs"]
            rows.append((dtype, op, f"{naive:.3e}", f"{stable:.3e}"))
        for k in ("16", "64", "256", "1024"):
            rows.append((dtype, f"matmul K={k}", f"{matmul[dtype][k]['norm_rel']:.3e}",
                         "f32-accumulated"))
    print(table(rows, ("dtype", "operation", "naive", "expert")))

    print()
    print("## The bf16 reduction that loses the one")
    print(f"inputs {acc['inputs'][0]}, exact fp64 {acc['exact_fp64']}, f32 matmul "
          f"{acc['f32_matmul']}, bf16 naive accumulation {acc['bf16_naive_accumulation']}, "
          f"bf16 expert (f32 accumulation) {acc['bf16_f32_accumulation']}, "
          f"torch bf16 matmul {acc['torch_bf16_matmul']}")

    if camel is not None:
        print()
        print("## Camel f32")
        if "records" in camel:
            for record in camel["records"]:
                operation = record.pop("operation", "?")
                print(operation, " ".join(f"{k}={v}" for k, v in record.items()))
        else:
            print(camel["error"])

    print()
    print(f"result file: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())