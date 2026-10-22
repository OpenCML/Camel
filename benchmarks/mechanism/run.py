#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""T10 mechanism counts -- one-key orchestrator.

Runs counts.py locally (Camel repo: camel + relax modes) and on the server
(onnxruntime + executorch clones), merges every emitted row into details.csv,
and renders the comparison matrix counts.csv. Every number in counts.csv comes
from a counts.py row printed to stdout in the same run; the prose cells
(artifact format / optimizer location / runtime requirement) are constants
here, each carrying its evidence path under MLSys2027/artifacts/.

Usage:
    python run.py [--out-dir DIR] [--skip-server]

Environment overrides: CAMEL_REPO, T10_SSH_HOST, T10_SERVER_DIR.
"""

from __future__ import annotations

import argparse
import csv
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
COUNTS = HERE / "counts.py"
REPO_ROOT = HERE.parents[1]

HEADER = ["section", "item", "scope", "metric", "value", "unit", "rule"]

# Prose cells: (Camel, ORT Training, ExecuTorch, evidence). These are curated
# conclusions with citations, not measurements; every numeric cell below them
# is taken from the merged counts.py rows.
PROSE = {
    "制品格式 artifact format": (
        "single .onnx file, opset 17, standard operators only",
        "4-file bundle (checkpoint + training_model.onnx carrying com.microsoft private ops; 9/38 nodes in MLP)",
        ".pte (proprietary binary format)",
        "artifacts/e2/capability_notes.md; artifacts/e9/ORT_TRAINING_CONTRAST.md; artifacts/e2/executorch_row.md",
    ),
    "优化器位置 optimizer location": (
        "in-graph: nn:sgd lowered to standard Sub/Mul nodes",
        "out-of-graph: separate optimizer_model.onnx driven through the coupled TrainingSession API",
        "out-of-graph C++ ('Currently the optimizer is not captured', extension/training README)",
        "artifacts/e9/ORT_TRAINING_CONTRAST.md; artifacts/e2/executorch_row.md",
    ),
    "runtime 要求 runtime requirement": (
        "unmodified onnxruntime InferenceSession",
        "ORT TrainingSession (private-op support + coupled API)",
        "proprietary ExecuTorch runtime (one artifact, exactly one runtime)",
        "artifacts/e2/capability_notes.md; artifacts/e9/ort_lora_contrast.json; artifacts/e3/runtime_configs.md",
    ),
}


def run(cmd: list[str], **kw) -> str:
    return subprocess.check_output(cmd, text=True, **kw).strip()


def collect(rows_out: list[list[str]], csv_text: str) -> None:
    for row in csv.reader(csv_text.splitlines()):
        if not row or row == HEADER:
            continue
        assert len(row) == 7, f"bad row: {row}"
        rows_out.append(row)


def lookup(rows: list[list[str]], section: str, item: str, metric: str) -> str:
    for r in rows:
        if r[0] == section and r[1] == item and r[3] == metric:
            return r[4]
    raise KeyError(f"row not found: {section} / {item} / {metric}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--camel-repo", default=os.environ.get("CAMEL_REPO", str(REPO_ROOT)))
    ap.add_argument("--ssh-host", default=os.environ.get("T10_SSH_HOST", ""))
    ap.add_argument("--server-dir", default=os.environ.get("T10_SERVER_DIR", "~/camel-eval/t10"))
    ap.add_argument("--torch-prefix", default=None, help="server site-packages; default auto-detected from venv-bench")
    ap.add_argument("--skip-server", action="store_true", help="reuse server_*_rows.csv already in --out-dir")
    args = ap.parse_args()

    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    rows: list[list[str]] = []
    meta: list[tuple[str, str]] = [
        ("run_timestamp_utc", datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")),
        ("camel_repo", str(Path(args.camel_repo).resolve())),
        ("ssh_host", args.ssh_host),
        ("server_dir", args.server_dir),
    ]

    print("== camel mode (local) ==", flush=True)
    collect(rows, run([sys.executable, str(COUNTS), "--mode", "camel", "--repo", args.camel_repo]))
    print("== relax mode (local) ==", flush=True)
    collect(rows, run([sys.executable, str(COUNTS), "--mode", "relax", "--repo", args.camel_repo]))

    ort_csv = out_dir / "server_ort_rows.csv"
    et_csv = out_dir / "server_executorch_rows.csv"
    if args.skip_server and ort_csv.is_file() and et_csv.is_file():
        print("== ort/executorch modes: reusing cached server rows ==", flush=True)
        collect(rows, ort_csv.read_text(encoding="utf-8"))
        collect(rows, et_csv.read_text(encoding="utf-8"))
    else:
        if not args.ssh_host:
            ap.error("--ssh-host (or T10_SSH_HOST) is required unless --skip-server is given")
        print("== scp counts.py to server ==", flush=True)
        run(["scp", "-o", "BatchMode=yes", str(COUNTS), f"{args.ssh_host}:{args.server_dir}/counts.py"])
        torch_prefix = args.torch_prefix
        if torch_prefix is None:
            torch_prefix = run(
                [
                    "ssh",
                    "-o",
                    "BatchMode=yes",
                    args.ssh_host,
                    "~/camel-eval/venv-bench/bin/python -c "
                    '"import torch,os;print(os.path.dirname(os.path.dirname(torch.__file__)))"',
                ]
            )
            meta.append(("server_torch_version", run(["ssh", "-o", "BatchMode=yes", args.ssh_host, "~/camel-eval/venv-bench/bin/python -c \"import torch;print(torch.__version__)\""])))
        meta.append(("server_torch_prefix", torch_prefix))
        print("== ort mode (server) ==", flush=True)
        ort_text = run(
            ["ssh", "-o", "BatchMode=yes", args.ssh_host, f"cd {args.server_dir} && python3 counts.py --mode ort --root {args.server_dir}/onnxruntime"]
        )
        ort_csv.write_text(ort_text + "\n", encoding="utf-8")
        collect(rows, ort_text)
        print("== executorch mode (server) ==", flush=True)
        et_text = run(
            [
                "ssh",
                "-o",
                "BatchMode=yes",
                args.ssh_host,
                f"cd {args.server_dir} && python3 counts.py --mode executorch --root {args.server_dir}/executorch --torch-prefix {torch_prefix}",
            ]
        )
        et_csv.write_text(et_text + "\n", encoding="utf-8")
        collect(rows, et_text)

    # Derived row: ExecuTorch training mechanism core (examples/tests excluded).
    core = sum(
        int(lookup(rows, "executorch", f"extension/training/{sub}", "lines_total")) for sub in ("module", "optimizer", "pybindings")
    )
    core_ncnb = sum(
        int(lookup(rows, "executorch", f"extension/training/{sub}", "lines_ncnb")) for sub in ("module", "optimizer", "pybindings")
    )
    derived = [
        ["executorch", "extension/training CORE", "module+optimizer+pybindings", "lines_total", str(core), "lines", "sum of the three extension/training subdir rows (examples/test excluded)"],
        ["executorch", "extension/training CORE", "module+optimizer+pybindings", "lines_ncnb", str(core_ncnb), "lines", "sum of the three extension/training subdir rows (examples/test excluded)"],
    ]
    for k, v in meta:
        derived.append(["meta", k, "run.py", "meta", v, "-", "orchestration metadata"])

    details_path = out_dir / "details.csv"
    with details_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(HEADER)
        w.writerows(rows + derived)

    # ------------------------------------------------------------- matrix
    camel_onnx_loc = lookup(rows, "camel_onnx", "TOTAL", "lines_total")
    ort_train_loc = lookup(rows, "ort", "orttraining_tree_cpp", "lines_total")
    ort_train_py = lookup(rows, "ort", "orttraining_tree_py", "lines_total")
    et_core_loc = str(core)
    camel_vjp = lookup(rows, "camel_setvjp", "TOTAL", "registrations")
    camel_vjp_rules = lookup(rows, "camel_setvjp", "TOTAL", "of_which_rules")
    camel_vjp_stubs = lookup(rows, "camel_setvjp", "TOTAL", "of_which_zero_grad_stubs")
    ort_gb = lookup(rows, "ort", "per_op_gradient_registrations", "registrations")
    ort_impl = lookup(rows, "ort", "per_op_gradient_registrations", "of_which_implemented_via_implement_macro")
    et_grad = lookup(rows, "executorch", "per_op_gradient_registrations", "registrations")

    matrix_rows = [
        ["aspect", "Camel", "ORT Training", "ExecuTorch", "evidence"],
        [
            "制品格式 artifact format",
            PROSE["制品格式 artifact format"][0],
            PROSE["制品格式 artifact format"][1],
            PROSE["制品格式 artifact format"][2],
            PROSE["制品格式 artifact format"][3],
        ],
        [
            "导出/训练链行数 export/training chain LOC (total lines)",
            camel_onnx_loc + " (modules/onnx/, *.cpp/*.h)",
            ort_train_loc + f" (orttraining/ tree, C++ only; +{ort_train_py} Python)",
            et_core_loc + " (extension/training: module+optimizer+pybindings)",
            "details.csv of this run",
        ],
        [
            "逐算子梯度注册项数 per-op gradient registrations",
            f"{camel_vjp} setVjp entries ({camel_vjp_rules} rules + {camel_vjp_stubs} zero-grad stubs)",
            f"{ort_gb} REGISTER_GRADIENT_BUILDER registry entries ({ort_impl} via IMPLEMENT_GRADIENT_BUILDER)",
            et_grad + " (script-verified absence; grep pattern in details.csv)",
            "details.csv of this run",
        ],
        [
            "优化器位置 optimizer location",
            PROSE["优化器位置 optimizer location"][0],
            PROSE["优化器位置 optimizer location"][1],
            PROSE["优化器位置 optimizer location"][2],
            PROSE["优化器位置 optimizer location"][3],
        ],
        [
            "runtime 要求 runtime requirement",
            PROSE["runtime 要求 runtime requirement"][0],
            PROSE["runtime 要求 runtime requirement"][1],
            PROSE["runtime 要求 runtime requirement"][2],
            PROSE["runtime 要求 runtime requirement"][3],
        ],
    ]
    counts_path = out_dir / "counts.csv"
    with counts_path.open("w", newline="", encoding="utf-8") as f:
        csv.writer(f, lineterminator="\n").writerows(matrix_rows)

    print("\n===== counts.csv =====", flush=True)
    for r in matrix_rows:
        print(",".join(f'"{c}"' if ("," in c or '"' in c) else c for c in r))
    print(f"\nwrote {counts_path}")
    print(f"wrote {details_path} ({len(rows) + len(derived)} rows)")
    print("\nkey numbers:")
    print(f"  Camel export chain (modules/onnx): {camel_onnx_loc} lines")
    print(f"  Camel opt passes (src/passes/opt): {lookup(rows, 'camel_opt_passes', 'TOTAL', 'lines_total')} lines")
    print(f"  Camel autodiff package (modules/autodiff): {lookup(rows, 'camel_autodiff', 'TOTAL', 'lines_total')} lines")
    print(f"  Camel setVjp registrations: {camel_vjp} (rules {lookup(rows, 'camel_setvjp', 'TOTAL', 'of_which_rules')}, zero-grad stubs {lookup(rows, 'camel_setvjp', 'TOTAL', 'of_which_zero_grad_stubs')})")
    print(f"  Camel ONNX mappings: forward {lookup(rows, 'camel_onnx_map', 'forward', 'registrations_total')} (literal {lookup(rows, 'camel_onnx_map', 'lowering.cpp', 'registrations_literal')} + dynamic), gradient {lookup(rows, 'camel_onnx_map', 'gradient', 'registrations_total')}")
    print(f"  ORT Training: {ort_gb} registry entries ({ort_impl} IMPLEMENT builders), orttraining C++ tree {ort_train_loc} lines")
    print(f"  ExecuTorch: extension/training core {et_core_loc} lines (dir total {lookup(rows, 'executorch', 'extension/training TOTAL', 'lines_total')}), per-op gradient registrations {et_grad}")
    print(f"  T8 relax translator: {lookup(rows, 't8_relax', 'onnx2relax.py', 'lines_total')} lines total, onnx_to_relax {lookup(rows, 't8_relax', 'def onnx_to_relax', 'function_lines')} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
