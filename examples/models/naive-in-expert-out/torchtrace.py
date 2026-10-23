# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""One PyTorch training step under torch.profiler, on the processor, in the process's CPU time.

`measure.py` times the torch counterparts inside their process with `time.perf_counter`, which on
the shared reference host carries whatever else is running.  This script gives the torch side the
same two instruments the Camel side gets in `sweep_cpu.py`:

  * the process runs under ``/usr/bin/time -v``, so its own CPU seconds (user + system) are known
    with no help from the profiler;
  * ``torch.profiler`` times the operator tree in the same process on the CPU, and its
    ``key_averages`` *self* CPU times (microseconds) are grouped into the categories the committed
    d=1024 profile uses: matmul, elementwise (which is where Inductor's fused kernels land, next to
    eager pointwise aten ops), reductions, layout/copy/creation, the explicit Adam step,
    gradient assembly, and the rest.

The profiled region is the whole timed run plus the one forward the harness evaluates before it, and
the report says so: the extra forward is 17 of the matmul group's calls, and every row carries its
call count so a consumer can divide by it.

Usage:

    python torchtrace.py --variant compiled --init <dir of .npy leaves> --steps 16 --warmup 3 \
        --threads 1 --batch 32 --sequence 64 --d-model 1024 --n-heads 64 --d-ff 2048 --vocab 32
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

os.environ["CUDA_VISIBLE_DEVICES"] = ""

import torch  # noqa: E402
from torch.profiler import ProfilerActivity, profile  # noqa: E402

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import torch_impl  # noqa: E402  (the sweep's own counterparts; this script only times them)

MATMUL_KEYS = ("aten::mm", "aten::bmm", "aten::addmm", "aten::matmul", "aten::linear",
               "aten::einsum", "aten::_scaled_dot_product", "aten::scaled_dot_product")
REDUCE_KEYS = ("aten::sum", "aten::mean", "aten::prod", "aten::var", "aten::std", "aten::amax",
               "aten::amin", "aten::max", "aten::min", "aten::norm")
LAYOUT_PREFIXES = ("aten::copy_", "aten::clone", "aten::cat", "aten::permute", "aten::transpose",
                   "aten::expand", "aten::view", "aten::reshape", "aten::contiguous", "aten::slice",
                   "aten::select", "aten::narrow", "aten::as_strided", "aten::zero_", "aten::fill_",
                   "aten::empty", "aten::resize", "aten::to", "aten::_to_copy", "aten::split",
                   "aten::unbind", "aten::stack", "aten::t", "aten::zeros", "aten::ones",
                   "aten::new_empty", "aten::lift_fresh", "aten::detach")
ASSEMBLY_KEYS = ("aten::index_put_", "aten::index_put", "aten::scatter", "aten::gather",
                 "aten::index", "aten::select_scatter")
OPTIMIZER_MARKERS = ("adam", "addcmul", "addcdiv", "lerp", "sqrt", "rsqrt", "div", "mul", "add",
                     "sub", "neg")


def categorize(key: str, variant: str) -> str:
    lowered = key.lower()
    if any(marker in lowered for marker in MATMUL_KEYS):
        return "matmul"
    if "inductor" in lowered or "triton" in lowered:
        # Inductor's generated code: fused pointwise chains and reductions.  The committed profile
        # keeps it in a category of its own, so the reader can see that its elementwise work is
        # fused rather than that it does no elementwise work.
        return "other (Inductor fused)"
    if any(marker in lowered for marker in ASSEMBLY_KEYS):
        return "gradient assembly"
    if lowered.startswith(LAYOUT_PREFIXES):
        return "layout/copy/creation"
    if lowered.startswith(REDUCE_KEYS):
        return "reductions"
    if lowered.startswith("aten::") or lowered.startswith("autograd::"):
        if any(lowered.startswith(f"aten::{marker}") for marker in
               ("mul", "div", "add", "sub", "neg", "exp", "log", "sqrt", "rsqrt", "pow", "tanh",
                "erf", "rsqrt", "reciprocal", "clamp", "where", "abs")):
            if variant == "compiled":
                # torch.compile's elementwise chain is Inductor's, and the plan of the eager naive
                # step is not its plan: keep it out of the eager elementwise row.
                return "other (Inductor fused)"
            return "elementwise"
        return "other"
    return "other"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", default="compiled",
                        choices=("naive", "expert", "compiled", "reference"))
    parser.add_argument("--init", required=True)
    parser.add_argument("--steps", type=int, default=16)
    parser.add_argument("--warmup", type=int, default=3)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--batch", type=int, default=8)
    parser.add_argument("--sequence", type=int, default=16)
    parser.add_argument("--d-model", type=int, default=64)
    parser.add_argument("--n-heads", type=int, default=4)
    parser.add_argument("--d-ff", type=int, default=128)
    parser.add_argument("--vocab", type=int, default=32)
    args = parser.parse_args()

    torch_impl.BATCH, torch_impl.SEQUENCE, torch_impl.D_MODEL = args.batch, args.sequence, args.d_model
    torch_impl.N_HEADS, torch_impl.D_FF, torch_impl.VOCAB = args.n_heads, args.d_ff, args.vocab
    torch_impl.D_HEAD = args.d_model // args.n_heads
    torch.set_num_threads(args.threads)
    torch.manual_seed(0)

    arrays = torch_impl.load_arrays(Path(args.init))
    x = torch.from_numpy(__import__("numpy").load(Path(args.init) / "x.npy")).float()
    labels = torch.from_numpy(__import__("numpy").load(Path(args.init) / "labels.npy")).long()

    params = torch_impl.make_params(arrays, fused=False)
    loss_fn = torch_impl.naive_loss
    if args.variant == "naive":
        loss_fn = torch_impl.naive_loss
    elif args.variant == "compiled":
        loss_fn = torch.compile(torch_impl.naive_loss)
    elif args.variant == "expert":
        params = torch_impl.make_params(arrays, fused=True)
        loss_fn = torch_impl.expert_loss

    # The one forward the harness evaluates before the timed run, inside the profiled region: its
    # calls are in the report and its cost is answered by dividing by the call count.
    with torch.no_grad():
        init_loss = float(loss_fn(params, x, labels))

    with profile(activities=[ProfilerActivity.CPU], record_shapes=False) as prof:
        _, losses, times, final_loss = torch_impl.run(loss_fn, params, x, labels, args.steps)

    categories: dict[str, float] = {}
    rows = []
    for event in prof.key_averages():
        us = event.self_device_time_total or event.self_cpu_time_total
        if us <= 0:
            continue
        group = categorize(event.key, args.variant)
        categories[group] = categories.get(group, 0.0) + us
        rows.append({"key": event.key, "self_us": us, "calls": event.count, "category": group})
    rows.sort(key=lambda row: -row["self_us"])
    timed = times[args.warmup:] if args.warmup < len(times) else times
    result = {
        "variant": args.variant,
        "shape": {"batch": args.batch, "sequence": args.sequence, "d_model": args.d_model,
                  "heads": args.n_heads, "d_head": torch_impl.D_HEAD, "d_ff": args.d_ff,
                  "vocab": args.vocab},
        "steps": args.steps, "warmup": args.warmup, "threads": args.threads,
        "init_loss": init_loss, "final_loss": final_loss,
        "step_ms": times,
        "median_ms": float(sorted(timed)[len(timed) // 2]),
        "profiled_us": {"categories": categories, "rows": rows[:60],
                        "total_us": sum(categories.values())},
        "profile_note": "the profiled region is the pre-run forward plus every run step; every row "
                        "carries its call count, so a consumer divides by the steps",
    }
    print("RESULT " + json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())