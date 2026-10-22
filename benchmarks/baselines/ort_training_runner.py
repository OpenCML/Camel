# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""ORT Training baseline for the E2 training-step benchmark (CPU).

Same workload as ``train.py``: the loss is ``mean(y * y)`` of the model output
on the benchmark input; one training step is forward+loss, the gradient with
respect to every weight, and an SGD update (lr 0.01). The forward graph is the
shared ``artifacts/<model>/torch.onnx`` (opset 17, the same file the existing
``torch_onnx_ort`` inference config consumes), and the initial weights are the
initializers of that graph — bit-identical to every other framework's.

How the executable training computation is obtained (the point of the E2
capability table):

  1. ``onnxruntime.training.artifacts.generate_artifacts`` takes the forward
     ONNX model plus a user-supplied list of parameter names to differentiate
     (``requires_grad``), a built-in loss type (``LossType.MSELoss`` — our
     ``mean(y*y)`` is expressed as MSE against a zero target) and a built-in
     optimizer (``OptimType.SGD``). The backward graph is *reconstructed by
     the runtime* from the forward graph via its per-operator GradientBuilder
     registry — it is not derived from the source program.
  2. The artifacts (checkpoint, training_model.onnx, eval_model.onnx,
     optimizer_model.onnx) are driven through the coupled training API
     (``api.CheckpointState`` / ``api.Module`` / ``api.Optimizer``); a plain
     ``InferenceSession`` is not the intended driver (see capability_notes.md).

One timed step is ``module(inputs) -> optimizer.step() -> lazy_reset_grad()``,
matching ``zero_grad -> backward -> opt.step()`` of the other frameworks. The
first step plus artifact generation and session construction is reported as
``compile_s``; ``warmup_s`` includes it, as in ``train.py``.

Thread control: ``api.Module`` exposes no session-options knob in current
onnxruntime-training versions, so intra-op threads follow the process-wide
default; the §A.5 protocol pins the process with ``taskset -c 0-3``. This
caveat is recorded in the capability JSON.

``--capability DIR`` keeps the generated artifacts and dumps, per model, a
JSON with the non-standard-domain operator census of training_model.onnx
(com.microsoft.* counts), the artifact file list, and what the user had to
supply — the raw material for capability_notes.md.

Nothing from onnxruntime is imported at module level, so importing this module
works without onnxruntime-training and run_all.py can archive the failure.

Usage:
  python baselines/ort_training_runner.py --models mlp --reps 5 --warmup 3 --out /tmp/ort.csv
  python baselines/ort_training_runner.py --capability baselines/capability
  python baselines/ort_training_runner.py --check --golden baselines/golden
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common import stats  # noqa: E402
from common.configs import onnx_path  # noqa: E402
from common.models import MODELS  # noqa: E402
from common.weights import load  # noqa: E402
from run import _write_csv  # noqa: E402

LR = 0.01  # same SGD learning rate as train.py
CONFIG = "ort_training"

# train.py's CSV columns, duplicated literally so this module never imports train
# (the ORT-training environment has no torch). Keep in sync.
COLUMNS = [
    "model",
    "config",
    "trial",
    "status",
    "threads",
    "warmup",
    "reps",
    "compile_s",
    "warmup_s",
    "latency_median_ms",
    "latency_p10_ms",
    "latency_p90_ms",
    "latency_mean_ms",
    "peak_rss_mb",
    "notes",
]

ARTIFACT_FILES = ("checkpoint", "training_model.onnx", "eval_model.onnx", "optimizer_model.onnx")


def _latency_fields(times: List[float]) -> Dict[str, float]:
    return {
        "latency_median_ms": stats.median(times),
        "latency_p10_ms": stats.percentile(times, 10),
        "latency_p90_ms": stats.percentile(times, 90),
        "latency_mean_ms": sum(times) / len(times),
    }


def _forward_onnx(model: str, weights: Dict[str, np.ndarray], x: np.ndarray) -> Path:
    """The shared torch-exported forward graph; export it if missing (needs torch)."""
    path = onnx_path(model, "torch")
    if path.exists():
        return path
    try:
        import importlib

        importlib.import_module(f"{model}.model_torch").export_onnx(weights, x, path)
    except ImportError as exc:
        raise RuntimeError(
            f"{path} is missing and torch is not importable here to re-export it; "
            f"run `python run.py --models {model} --configs torch_onnx_ort` in an environment with torch first"
        ) from exc
    return path


def _generate(fwd_path: Path, work_dir: Path) -> List[str]:
    """Run generate_artifacts; returns the requires_grad (initializer) names used."""
    import onnx
    from onnxruntime.training import artifacts

    proto = onnx.load(str(fwd_path))
    requires_grad = [i.name for i in proto.graph.initializer]
    artifacts.generate_artifacts(
        proto,
        requires_grad=requires_grad,
        frozen_params=[],
        loss=artifacts.LossType.MSELoss,
        optimizer=artifacts.OptimType.SGD,
        artifact_directory=str(work_dir),
    )
    return requires_grad


def _open_session(work_dir: Path):
    """Build the coupled training session; returns (module, optimizer, lr_control_note)."""
    from onnxruntime.training.api import checkpoint_state, module as module_api, optimizer as optimizer_api

    state = checkpoint_state.CheckpointState.load_checkpoint(str(work_dir / "checkpoint"))
    m = module_api.Module(str(work_dir / "training_model.onnx"), state, device="cpu")
    opt = optimizer_api.Optimizer(str(work_dir / "optimizer_model.onnx"), m)
    if hasattr(opt, "set_learning_rate"):
        opt.set_learning_rate(LR)
        note = f"Optimizer.set_learning_rate({LR})"
    else:
        note = "no set_learning_rate on this API version; optimizer-model default lr used"
    return m, opt, note


def _feeds(work_dir: Path, fwd_path: Path, x: np.ndarray, output_shape, param_names: List[str]) -> List[np.ndarray]:
    """Positional *user* inputs for the training module: the forward input gets x,
    every other user input (the MSE target) gets zeros of the output shape.
    Weights, grad-accumulation buffers and the lazy_reset_grad flag appear in
    training_model.onnx's graph.input but are checkpoint state, not user inputs —
    feeding them positionally trips Module's user-input count check. They are not
    listed in graph.initializer (the checkpoint owns them), so they are filtered
    by name against the forward graph's requires_grad list instead."""
    import onnx

    skip = set(param_names) | {n + "_grad.accumulation.buffer" for n in param_names} | {"lazy_reset_grad"}
    fwd_inputs = {i.name for i in onnx.load(str(fwd_path)).graph.input}
    feeds = []
    for vi in onnx.load(str(work_dir / "training_model.onnx")).graph.input:
        if vi.name in skip:
            continue
        feeds.append(np.ascontiguousarray(x) if vi.name in fwd_inputs else np.zeros(output_shape, dtype=np.float32))
    return feeds


def _loss_value(ret) -> float:
    if isinstance(ret, (list, tuple)):
        ret = ret[0]
    return float(np.asarray(ret).reshape(()))


def _one_step_loss_and_updated(m, opt, feeds, work_dir: Path, out_names: List[str], tag: str):
    """Export the current (updated) parameters baked into an inference model."""
    ret = m(*feeds)
    opt.step()
    m.lazy_reset_grad()
    out = work_dir / f"infer_{tag}.onnx"
    m.export_model_for_inferencing(str(out), out_names)
    import onnx

    inits = {i.name: onnx.numpy_helper.to_array(i) for i in onnx.load(str(out)).graph.initializer}
    return _loss_value(ret), inits


def _match_initializers(inits: Dict[str, np.ndarray], weights: Dict[str, np.ndarray]):
    """Map ONNX initializer names to harness weight names by value (direct or
    transposed, as nn.Linear stores [out, in]) — same convention as train.py."""
    mapping = {}
    for iname, value in inits.items():
        for wname, w in weights.items():
            if value.shape == w.shape and np.array_equal(value, w):
                mapping[wname] = (iname, False)
                break
            if value.ndim == 2 and value.T.shape == w.shape and np.array_equal(value.T, w):
                mapping[wname] = (iname, True)
                break
    return mapping


def trial_row(model: str, reps: int, warmup: int, threads: int) -> dict:
    """One trial of (model, ort_training); designed to run inside a fresh process."""
    for var in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        os.environ.setdefault(var, str(threads))
    try:
        import onnxruntime.training  # noqa: F401
    except ImportError as exc:  # archived verbatim by run_all.py (failure is data)
        raise RuntimeError(f"onnxruntime-training is not importable in this environment: {exc}") from exc

    from common.memory import peak_rss_self_kb

    weights, x = load(model)
    spec = MODELS[model]
    fwd_path = _forward_onnx(model, weights, x)

    with tempfile.TemporaryDirectory(prefix="ort_train_") as tmp:
        work = Path(tmp)
        start = time.perf_counter()
        requires_grad = _generate(fwd_path, work)
        m, opt, _ = _open_session(work)
        feeds = _feeds(work, fwd_path, x, spec.output_shape, requires_grad)
        m(*feeds)
        opt.step()
        m.lazy_reset_grad()
        compile_s = time.perf_counter() - start
        for _ in range(warmup - 1):
            m(*feeds)
            opt.step()
            m.lazy_reset_grad()
        warmup_s = time.perf_counter() - start
        times = []
        for _ in range(reps):
            begin = time.perf_counter()
            m(*feeds)
            opt.step()
            m.lazy_reset_grad()
            times.append((time.perf_counter() - begin) * 1000.0)
    return {
        "compile_s": compile_s,
        "warmup_s": warmup_s,
        **_latency_fields(times),
        "peak_rss_mb": peak_rss_self_kb() / 1024.0,
    }


def check(model: str, golden_dir: Path, rtol: float) -> bool:
    """One ORT-Training step from the shared weights vs the torch golden.

    Parameter-name mapping is established without torch: at zero steps the
    exported inference model's initializers equal the original weights, so they
    value-match to the harness weight names; the same mapping then reads the
    updated values after one step.
    """
    import onnx

    golden = np.load(golden_dir / f"{model}_golden.npz")
    weights, x = load(model)
    spec = MODELS[model]
    fwd_path = _forward_onnx(model, weights, x)
    out_names = [o.name for o in onnx.load(str(fwd_path)).graph.output]

    ok = True
    print(f"== {model} (ORT Training vs torch golden)")
    with tempfile.TemporaryDirectory(prefix="ort_train_check_") as tmp:
        work = Path(tmp)
        _generate(fwd_path, work)
        m, opt, lr_note = _open_session(work)
        feeds = _feeds(work, fwd_path, x, spec.output_shape, [i.name for i in onnx.load(str(fwd_path)).graph.initializer])
        inits0 = _export_inits(m, work, out_names, "before")
        mapping = _match_initializers(inits0, weights)
        missing = sorted(set(weights) - set(mapping))
        if missing:
            print(f"  initializers not value-matched to harness weights: {missing}")
            ok = False
        got_loss, inits1 = _one_step_loss_and_updated(m, opt, feeds, work, out_names, "after")
    print(f"  (lr control: {lr_note})")
    ref_loss = float(golden["__loss__"])
    loss_err = abs(got_loss - ref_loss) / max(abs(ref_loss), 1e-12)
    ok &= loss_err <= rtol
    print(f"  {'loss':12s} {'ok' if loss_err <= rtol else 'MISMATCH':8s} |diff|/|ref| = {loss_err:.2e}")
    for name in sorted(mapping):
        iname, transposed = mapping[name]
        got = inits1[iname].T if transposed else inits1[iname]
        ref = golden[name]
        scale = float(np.max(np.abs(weights[name]))) or 1.0
        err = float(np.max(np.abs(got - ref))) / scale
        status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
        ok &= status == "ok"
        print(f"  {name:12s} {status:8s} max|diff|/max|w| = {err:.2e}  ({iname}{'.T' if transposed else ''})")
    return ok


def _export_inits(m, work_dir: Path, out_names: List[str], tag: str) -> Dict[str, np.ndarray]:
    import onnx

    out = work_dir / f"infer_{tag}.onnx"
    m.export_model_for_inferencing(str(out), out_names)
    return {i.name: onnx.numpy_helper.to_array(i) for i in onnx.load(str(out)).graph.initializer}


def capability(model: str, out_dir: Path) -> dict:
    """Generate the artifacts persistently and census what the training graph contains.

    Writes ``<out_dir>/<model>.json`` and returns it as a dict (run_all.py calls
    this in a child process and stores the JSON next to the result CSV).
    """
    import onnx
    import onnxruntime as ort

    weights, x = load(model)
    fwd_path = _forward_onnx(model, weights, x)
    work = Path(out_dir) / model / "artifacts"
    work.mkdir(parents=True, exist_ok=True)
    requires_grad = _generate(fwd_path, work)

    proto = onnx.load(str(work / "training_model.onnx"))
    domains: Dict[str, int] = {}
    nonstandard_ops: Dict[str, List[str]] = {}
    for node in proto.graph.node:
        domain = node.domain or "ai.onnx"
        domains[domain] = domains.get(domain, 0) + 1
        if domain not in ("", "ai.onnx"):
            nonstandard_ops.setdefault(domain, [])
            if node.op_type not in nonstandard_ops[domain]:
                nonstandard_ops[domain].append(node.op_type)
    files = sorted(p.name for p in work.iterdir())
    info = {
        "model": model,
        "onnxruntime_version": ort.__version__,
        "forward_onnx": str(fwd_path),
        "artifact_dir": str(work),
        "artifact_files": files,
        "artifact_present": {name: (work / name).exists() for name in ARTIFACT_FILES},
        "training_model_nodes": len(proto.graph.node),
        "op_domains": domains,
        "nonstandard_domains": {d: c for d, c in domains.items() if d not in ("", "ai.onnx")},
        "nonstandard_op_types": nonstandard_ops,
        "requires_grad_supplied_by_user": requires_grad,
        "training_graph_inputs": [i.name for i in proto.graph.input],
        "training_graph_outputs": [o.name for o in proto.graph.output],
        "user_supplied": [
            "forward ONNX graph (torch.onnx.export, opset 17)",
            "requires_grad: explicit list of parameter names to differentiate",
            "loss: built-in LossType.MSELoss (mean(y*y) expressed as MSE vs a zero target)",
            "optimizer: built-in OptimType.SGD, learning rate set through the API",
        ],
        "api_used": [
            "onnxruntime.training.artifacts.generate_artifacts",
            "onnxruntime.training.api CheckpointState / Module / Optimizer",
        ],
        "gradient_origin": "runtime-reconstructed from the forward graph (per-operator GradientBuilder registry)",
        "thread_control": "no session-options knob on api.Module; process pinned externally (taskset), OMP_NUM_THREADS set",
    }
    out = Path(out_dir) / f"{model}.json"
    out.write_text(json.dumps(info, indent=2) + "\n")
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--out", type=Path, help="write rows to this CSV (train.py columns)")
    parser.add_argument("--check", action="store_true", help="compare one training step against the torch golden")
    parser.add_argument("--golden", type=Path, default=BENCH_ROOT / "baselines" / "golden")
    parser.add_argument("--rtol", type=float, default=1e-3)
    parser.add_argument("--capability", type=Path, metavar="DIR", help="generate artifacts + write <model>.json census")
    args = parser.parse_args()
    models = [m for m in args.models.split(",") if m]

    if args.capability:
        ok = True
        for model in models:
            try:
                info = capability(model, args.capability)
                ns = info["nonstandard_domains"]
                print(f"[capability] {model}: {info['training_model_nodes']} nodes, non-standard domains {ns}")
            except Exception as exc:
                ok = False
                print(f"[capability] {model}: FAILED: {exc}")
        return 0 if ok else 1
    if args.check:
        ok = all(check(m, args.golden, args.rtol) for m in models)
        print("ORT Training golden check:", "PASS" if ok else "FAIL")
        return 0 if ok else 1

    rows: List[dict] = []
    for model in models:
        base = {"model": model, "config": CONFIG, "trial": 1, "threads": args.threads, "warmup": args.warmup, "reps": args.reps}
        try:
            res = trial_row(model, args.reps, args.warmup, args.threads)
            rows.append({**base, "status": "ok", **res})
            print(f"{model:12s} {CONFIG:16s} {res['latency_median_ms']:8.3f} ms  compile {res['compile_s']:.2f} s")
        except Exception as exc:
            rows.append({**base, "status": "failed", "notes": str(exc)})
            print(f"{model:12s} {CONFIG:16s} FAILED: {exc}")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        _write_csv(args.out, COLUMNS, rows)
        print(f"[done] wrote {args.out}")
    else:
        print(json.dumps(rows, indent=2, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())
