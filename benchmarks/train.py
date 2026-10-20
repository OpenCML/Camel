"""Training workload: agreement with PyTorch and training-step timing.

For each model, the loss is ``mean(y * y)`` of the benchmark model's output ``y``
on the benchmark input (the same in every framework), and a training step is the
loss, its gradient with respect to every weight, and an SGD update (lr 0.01).

Protocols. The default protocol is **stateful**: the model's ``train_loop`` runs
``max(warmup, 1) + reps`` consecutive steps, each starting from the parameters the
previous step produced, so every update is consumed (by the next step, and the
last one by the loss evaluated at the final parameters, which the model prints).
``--stateless`` selects the earlier protocol, in which every timed step starts
from the same weights and discards its update: the **updates are not consumed**,
so an optimizing pipeline may remove the update and the backward pass that feeds
it (``std::opt`` does). Stateless timings are not training-step timings;
the protocol is kept for comparison with earlier results and to show that the
reconciliation (reconcile_train.py) detects the removal.

Gradient check (``--check``): Camel's ``grad`` of the loss (dumped by the model's
training workload through ``nn.save_params``) is compared with PyTorch autograd
weight by weight. Weights are matched to torch parameters by value (directly or
transposed, as ``nn.Linear`` stores ``[out, in]``), so no per-model name mapping
is needed.

K-step check (``--check-ksteps``): the stateful loop's loss at every step and its
final parameters are compared with PyTorch running the same number of SGD steps
from the same weights on the same input (tolerance ``--ktol``, default 1e-4:
losses relative to the reference loss, parameters relative to the largest
magnitude in the reference tensor).

Training-step export (``--check-onnx``): the model's training step (loss,
gradient, SGD update) is exported with ``onnx.export_model`` as a function of
the parameters and the input, checked with ``onnx.checker``, and run once on
ONNX Runtime; its loss and updated parameters are compared with one step of
Camel's own execution (the loss it reports and ``w - lr * grad`` from the
dumped gradients).

Timing (``--time``): the configurations of ``config_commands()``, each run in
``--trials`` fresh processes, interleaved. Every configuration name maps to one
command line (asserted at import). ``camel_export_ort`` runs the exported
training step on ONNX Runtime, the driver feeding the updated parameters back as
the next step's inputs. With ``--reconciliation <csv>`` (written by
reconcile_train.py) only (model, configuration) pairs whose verdict is PASS are
timed; ``--include-unreconciled`` times the others too and marks their rows.
Output has run.py's format: raw rows per trial (first step, warmup and
steady-state step latencies, peak RSS of the process running the model) and a
summary with the median step time, a bootstrap 95% CI over trial medians and the
ratio to PyTorch, plus a ``.meta.json`` with the environment (common/envinfo.py).

Usage:
  .venv/bin/python train.py --check
  .venv/bin/python train.py --check-ksteps --warmup 40 --reps 100
  .venv/bin/python train.py --check-onnx
  .venv/bin/python train.py --time --trials 5 --out results/train.csv
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(BENCH_ROOT))

from common import envinfo, stats  # noqa: E402
from common.camel import _camel_env, parse_bench_lines  # noqa: E402
from common.configs import camel_binary, camel_model  # noqa: E402
from run import _write_csv  # noqa: E402
from common.weights import load  # noqa: E402

MODELS = ["mlp", "gru", "transformer", "lenet"]
LR = 0.01

STATEFUL = "stateful"
STATELESS = "stateless"
STATELESS_LABEL = "stateless (updates not consumed)"

# Camel configurations of the training workload: passes after `camel <model.cml>`.
CAMEL_CONFIGS: Dict[str, Tuple[str, ...]] = {
    "camel_nvm": ("std::macro", "std::nvm"),
    "camel_fvm": ("std::macro", "std::fvm"),
    "camel_fuse_nvm": ("std::macro", "tensor::fuse", "std::nvm"),
    # Generic simplification first: pullback closures collapse into straight-line code.
    "camel_opt_nvm": ("std::macro", "std::opt", "std::nvm"),
}
TORCH_CONFIG = "torch_eager"
ORT_CONFIG = "camel_export_ort"


def config_commands() -> Dict[str, Tuple[str, ...]]:
    """Every configuration of the training workload and the command line it stands for."""
    commands: Dict[str, Tuple[str, ...]] = {
        TORCH_CONFIG: ("python", "train.torch_trial_row"),
        # The training step exported by `onnx.export_model` (default pipeline), run by ORT.
        ORT_CONFIG: ("camel", "<model.cml>", "CAMEL_BENCH_WORKLOAD=train_export", "|", "python", "train.ort_trial_row"),
    }
    for name, passes in CAMEL_CONFIGS.items():
        commands[name] = ("camel", "<model.cml>", *passes)
    return commands


def assert_unique_pipelines() -> None:
    """No two configuration names may stand for the same command line."""
    seen: Dict[Tuple[str, ...], str] = {}
    for name, command in config_commands().items():
        assert command not in seen, f"configurations {seen[command]!r} and {name!r} are the same command line: {command}"
        seen[command] = name


assert_unique_pipelines()
ALL_CONFIGS = list(config_commands())


# ---------------------------------------------------------------- camel


def camel_command(model: str, passes: Tuple[str, ...]) -> List[str]:
    return [str(camel_binary()), str(camel_model(model)), *passes]


def write_raw_log(path: Path, cmd: List[str], env: Dict[str, str], code: int, stdout: str, stderr: str) -> None:
    """One file per run: the command, the CAMEL_* environment, then stdout and stderr as produced."""
    shown = {k: v for k, v in sorted(env.items()) if k.startswith("CAMEL_")}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"# cmd: {' '.join(shlex.quote(c) for c in cmd)}\n# env: {json.dumps(shown)}\n# exit: {code}\n"
        f"# ---- stdout ----\n{stdout}\n# ---- stderr ----\n{stderr}"
    )


def run_camel_full(
    model: str, passes: Tuple[str, ...], env: Dict[str, str], timeout: float = 600.0, log: Optional[Path] = None
) -> Tuple[str, str]:
    """(stdout, stderr) of the training workload under `passes`; the run is saved to `log` if given."""
    child_env = _camel_env(model, {"CAMEL_BENCH_WORKLOAD": "train", **env})
    child_env.setdefault("CAMEL_BENCH_THREADS", str(os.cpu_count() or 1))
    child_env.setdefault("CAMEL_BENCH_WARMUP", "2")
    child_env.setdefault("CAMEL_BENCH_REPS", "10")
    cmd = camel_command(model, passes)
    proc = subprocess.run(
        cmd, cwd=camel_model(model).parent, env=child_env, capture_output=True, text=True, timeout=timeout
    )
    if log is not None:
        write_raw_log(log, cmd, child_env, proc.returncode, proc.stdout, proc.stderr)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout, proc.stderr


def run_camel(model: str, passes: Tuple[str, ...], env: Dict[str, str], timeout: float = 600.0) -> str:
    return run_camel_full(model, passes, env, timeout)[0]


def camel_gradients(model: str, config: str = "camel_nvm") -> Dict[str, np.ndarray]:
    load(model)  # the Camel program reads the benchmark artifacts; generate them on first use
    with tempfile.TemporaryDirectory() as tmp:
        run_camel(model, CAMEL_CONFIGS[config], {"CAMEL_BENCH_GRADS": tmp})
        return {p.stem: np.load(p) for p in Path(tmp).glob("*.npy")}


_STEP_LINE = re.compile(
    r"^CAMEL_BENCH (warm_ms|latency_ms)=(\S+) loss=(\S+)(?: step=(\d+) since_start_ms=(\S+))?\s*$", re.M
)


def parse_steps(stdout: str) -> List[dict]:
    """The steps a training run reported, in order: whether timed, the step time, the loss and
    (stateful protocol) the time since the loop started when the step was reported."""
    return [
        {
            "timed": m.group(1) == "latency_ms",
            "ms": float(m.group(2)),
            "loss": float(m.group(3)),
            "since_start_ms": float(m.group(5)) if m.group(5) else None,
        }
        for m in _STEP_LINE.finditer(stdout)
    ]


def protocol_of(stdout: str) -> str:
    found = re.search(r"^CAMEL_BENCH protocol=(\w+)", stdout, re.M)
    return found.group(1) if found else "unknown"


def step_spans(steps: List[dict]) -> List[float]:
    """Time between consecutive step reports, for the timed steps: one step plus its report. A
    step whose work ran outside its own timer shows up as a span far above its latency."""
    return [
        cur["since_start_ms"] - prev["since_start_ms"]
        for prev, cur in zip(steps, steps[1:])
        if cur["timed"] and cur["since_start_ms"] is not None and prev["since_start_ms"] is not None
    ]


def camel_ksteps(
    model: str,
    config: str,
    warmup: int,
    reps: int,
    threads: Optional[int] = None,
    log: Optional[Path] = None,
    extra_env: Optional[Dict[str, str]] = None,
) -> dict:
    """Runs the stateful loop and returns its per-step losses, the loss at the final parameters,
    the final parameters and the raw output."""
    with tempfile.TemporaryDirectory() as tmp:
        env = {"CAMEL_BENCH_WARMUP": str(warmup), "CAMEL_BENCH_REPS": str(reps), "CAMEL_BENCH_PARAMS": tmp}
        if threads:
            env["CAMEL_BENCH_THREADS"] = str(threads)
        stdout, stderr = run_camel_full(model, CAMEL_CONFIGS[config], {**env, **(extra_env or {})}, log=log)
        params = {p.stem: np.load(p) for p in Path(tmp).glob("*.npy")}
    found = re.search(r"^CAMEL_BENCH final_params_loss=(\S+)", stdout, re.M)
    return {
        "steps": parse_steps(stdout),
        "final_params_loss": float(found.group(1)) if found else None,
        "params": params,
        "stdout": stdout,
        "stderr": stderr,
    }


# ---------------------------------------------------------------- torch


def torch_model(model: str, weights: Dict[str, np.ndarray]):
    import importlib

    module = importlib.import_module(f"{model}.model_torch")
    net = module.build_model(weights)
    net.train()
    for p in net.parameters():  # inference references may freeze some weights
        p.requires_grad_(True)
    return net


def torch_loss(net, x):
    y = net(x)
    return (y * y).mean()


def _match_parameters(net, weights: Dict[str, np.ndarray]) -> Dict[str, Tuple[str, bool]]:
    """weight name -> (torch parameter name, transposed), matched by value."""
    params = [(n, p.detach().numpy()) for n, p in net.named_parameters()]
    matched: Dict[str, Tuple[str, bool]] = {}
    for wname, w in weights.items():
        for pname, value in params:
            if value.shape == w.shape and np.array_equal(value, w):
                matched[wname] = (pname, False)
                break
            if value.ndim == 2 and value.T.shape == w.shape and np.array_equal(value.T, w):
                matched[wname] = (pname, True)
                break
    return matched


def torch_gradients(model: str) -> Tuple[Dict[str, np.ndarray], Dict[str, np.ndarray]]:
    """(weight name -> gradient, weight name -> torch parameter name) by value matching."""
    import torch

    weights, x = load(model)
    net = torch_model(model, weights)
    matched = _match_parameters(net, weights)
    loss = torch_loss(net, torch.from_numpy(x))
    loss.backward()
    by_name = dict(net.named_parameters())
    grads, names = {}, {}
    for wname, (pname, transposed) in matched.items():
        if by_name[pname].grad is None:
            continue
        grad = by_name[pname].grad.numpy()
        grads[wname], names[wname] = (grad.T, pname + ".T") if transposed else (grad, pname)
    return grads, names


def torch_ksteps(model: str, steps: int) -> dict:
    """`steps` SGD steps of PyTorch from the benchmark weights on the benchmark input: the loss
    of every step (at the parameters the step starts from), the loss at the final parameters and
    the final parameters under the benchmark's weight names."""
    import torch

    weights, x = load(model)
    # Some references build their parameters on the arrays' memory, which SGD then updates in
    # place: keep an untouched copy as the initial weights.
    initial = {name: w.copy() for name, w in weights.items()}
    net = torch_model(model, weights)
    matched = _match_parameters(net, weights)
    opt = torch.optim.SGD(net.parameters(), lr=LR)
    xt = torch.from_numpy(x)
    losses = []
    for _ in range(steps):
        opt.zero_grad(set_to_none=True)
        loss = torch_loss(net, xt)
        loss.backward()
        opt.step()
        losses.append(float(loss.detach()))
    with torch.no_grad():
        final_loss = float(torch_loss(net, xt))
    by_name = dict(net.named_parameters())
    params = {}
    for wname, (pname, transposed) in matched.items():
        value = by_name[pname].detach().numpy().copy()
        params[wname] = value.T if transposed else value
    return {"losses": losses, "final_params_loss": final_loss, "params": params, "initial": initial}


def check(models: List[str], rtol: float, configs: List[str]) -> bool:
    ok = True
    for model, config in [(m, c) for m in models for c in configs]:
        camel = camel_gradients(model, config)
        torch_grads, names = torch_gradients(model)
        print(f"== {model} ({config})")
        # Scale errors by the weight's largest gradient, floored so that weights whose exact
        # gradient is zero (e.g. the key bias under softmax, which shifts every score of a
        # query equally) compare rounding noise against the model's gradient scale instead.
        floor = 1e-3 * max(float(np.max(np.abs(g))) for g in torch_grads.values())
        for name in sorted(torch_grads):
            ref = torch_grads[name]
            got = camel.get(name)
            if got is None:
                print(f"  {name:12s} MISSING in Camel's gradient")
                ok = False
                continue
            scale = max(float(np.max(np.abs(ref))), floor) or 1.0
            err = float(np.max(np.abs(got - ref))) / scale
            status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
            ok &= status == "ok"
            print(f"  {name:12s} {status:8s} max|diff|/max|ref| = {err:.2e}  ({names[name]}, {list(ref.shape)})")
        unmatched = set(camel) - set(torch_grads)
        if unmatched:
            print(f"  (no torch parameter holds: {sorted(unmatched)})")
    return ok


# ---------------------------------------------------------------- K-step agreement


def compare_trajectory(losses: List[float], final_loss: Optional[float], ref: dict) -> dict:
    """Loss errors of a K-step run against a reference run (relative to the reference loss)."""
    ref_losses = ref["losses"]
    out = {"steps": len(losses), "ref_steps": len(ref_losses)}
    if len(losses) != len(ref_losses) or not losses:
        return {**out, "loss_err_max": float("inf"), "loss_err_last": float("inf"), "final_params_loss_err": float("inf")}
    errs = [abs(a - b) / max(abs(b), 1e-12) for a, b in zip(losses, ref_losses)]
    out["loss_err_max"] = max(errs)
    out["loss_err_last"] = errs[-1]
    ref_final = ref["final_params_loss"]
    out["final_params_loss_err"] = (
        abs(final_loss - ref_final) / max(abs(ref_final), 1e-12) if final_loss is not None else float("inf")
    )
    return out


def compare_params(got: Dict[str, np.ndarray], ref: Dict[str, np.ndarray]) -> dict:
    """Final-parameter errors: per tensor max|diff| / max|ref|, and the worst of them. Reference
    tensors missing from `got` (or of another shape) make the error infinite."""
    per: Dict[str, float] = {}
    for name, want in ref.items():
        have = got.get(name)
        if have is None or have.shape != want.shape:
            per[name] = float("inf")
            continue
        per[name] = float(np.max(np.abs(have - want))) / (float(np.max(np.abs(want))) or 1.0)
    return {"per_tensor": per, "params_err": max(per.values()) if per else float("inf"), "not_compared": sorted(set(got) - set(ref))}


def params_moved(final: Dict[str, np.ndarray], initial: Dict[str, np.ndarray]) -> float:
    """Largest relative change of a parameter tensor from its initial value (0 = nothing moved)."""
    moved = 0.0
    for name, start in initial.items():
        if name in final and final[name].shape == start.shape:
            moved = max(moved, float(np.max(np.abs(final[name] - start))) / (float(np.max(np.abs(start))) or 1.0))
    return moved


def check_ksteps(models: List[str], configs: List[str], warmup: int, reps: int, tol: float, raw_dir: Optional[Path]) -> bool:
    ok = True
    steps = max(warmup, 1) + reps
    for model in models:
        ref = torch_ksteps(model, steps)
        for config in configs:
            name = f"{model} ({config}, {steps} consecutive steps)"
            try:
                if config == ORT_CONFIG:
                    got = ort_ksteps(model, steps, os.cpu_count() or 1)
                    # Artifact gate: the exported step on ORT against Camel's own execution.
                    native = camel_ksteps(model, "camel_nvm", warmup, reps)
                    ref_run = {
                        "losses": [s["loss"] for s in native["steps"]],
                        "final_params_loss": native["final_params_loss"],
                        "params": native["params"],
                    }
                    traj = compare_trajectory(got["losses"], None, ref_run)
                    traj["final_params_loss_err"] = 0.0  # the exported step has no such output
                    against = "Camel native (camel_nvm)"
                else:
                    log = raw_dir / f"ksteps_{model}_{config}.log" if raw_dir else None
                    run = camel_ksteps(model, config, warmup, reps, log=log)
                    got = {"params": run["params"]}
                    traj = compare_trajectory([s["loss"] for s in run["steps"]], run["final_params_loss"], ref)
                    ref_run = ref
                    against = "PyTorch"
            except Exception as exc:
                print(f"== {name}\n  FAILED: {exc}")
                ok = False
                continue
            par = compare_params(got["params"], ref_run["params"])
            worst = max(traj["loss_err_max"], traj["final_params_loss_err"], par["params_err"])
            status = "ok" if worst <= tol else "MISMATCH"
            ok &= status == "ok"
            print(f"== {name} against {against}: {status}")
            print(
                f"  loss  max over steps {traj['loss_err_max']:.2e}  last step {traj['loss_err_last']:.2e}  "
                f"at final parameters {traj['final_params_loss_err']:.2e}   (|diff|/|ref|)"
            )
            print(f"  final parameters  worst tensor {par['params_err']:.2e}   (max|diff|/max|ref|, {len(par['per_tensor'])} tensors)")
            if par["not_compared"]:
                print(f"  (no reference tensor for: {par['not_compared']})")
    return ok


# ---------------------------------------------------------------- onnx export


def camel_loss(model: str) -> float:
    """The loss of the first native training step (the one that starts from the benchmark weights)."""
    out = run_camel(model, CAMEL_CONFIGS["camel_nvm"], {"CAMEL_BENCH_WARMUP": "1", "CAMEL_BENCH_REPS": "1"})
    found = re.search(r"loss=([-+0-9.eE]+)", out)
    if not found:
        raise RuntimeError(f"no loss in Camel's output:\n{out}")
    return float(found.group(1))


def _harness_key(weights: dict, graph_name: str) -> str:
    """Graph input/output names flatten nested parameter paths with '_' while the
    harness and Camel's gradient dump use '.' — map back (e.g. transformer4)."""
    if graph_name in weights:
        return graph_name
    alt = graph_name.replace("_", ".", 1)
    return alt if alt in weights else graph_name


def export_train_step(model: str, path: Path) -> float:
    """Exports the model's training step to `path`; returns the wall time of the export process."""
    start = time.perf_counter()
    run_camel(model, (), {"CAMEL_BENCH_WORKLOAD": "train_export", "CAMEL_BENCH_EXPORT": str(path)})
    return time.perf_counter() - start


def check_onnx(models: List[str], rtol: float) -> bool:
    import onnx
    import onnxruntime as ort

    ok = True
    for model in models:
        print(f"== {model} (training step exported to ONNX)")
        weights, x = load(model)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / f"{model}_step.onnx"
            export_s = export_train_step(model, path)
            proto = onnx.load(str(path))
            onnx.checker.check_model(proto, full_check=True)
            session = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
            feeds = {i.name: (x if i.name == "input1" else weights[_harness_key(weights, i.name)]) for i in session.get_inputs()}
            outputs = dict(zip([o.name for o in session.get_outputs()], session.run(None, feeds)))
        print(f"  exported in {export_s:.2f} s: {len(proto.graph.node)} nodes, onnx.checker ok")
        ref_loss = camel_loss(model)
        got_loss = float(outputs["output_0"])
        loss_err = abs(got_loss - ref_loss) / max(abs(ref_loss), 1e-12)
        status = "ok" if loss_err <= rtol else "MISMATCH"
        ok &= status == "ok"
        print(f"  {'loss':12s} {status:8s} |diff|/|ref| = {loss_err:.2e}  (ORT {got_loss:.6g}, Camel {ref_loss:.6g})")
        grads = camel_gradients(model)
        for name in sorted(weights):
            got = outputs.get(f"output_1_{name}")
            if got is None:  # exporter flattens '.' to '_' in output names (nested params)
                got = outputs.get(f"output_1_{name.replace('.', '_', 1)}")
            if got is not None:
                ref = weights[name] - LR * grads[name]
                what = "max|diff|/max|w|"
            else:
                # Not an updated parameter: a carried state (e.g. resnetbn's running
                # statistics), an output_2_<name> compared with the native dump.
                got = outputs.get(f"output_2_{name}")
                if got is None:
                    print(f"  {name:12s} MISSING in the exported step's outputs")
                    ok = False
                    continue
                ref = grads[name]
                what = "max|diff|/max|s|"
            scale = float(np.max(np.abs(weights[name]))) or 1.0
            err = float(np.max(np.abs(got - ref))) / scale
            status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
            ok &= status == "ok"
            print(f"  {name:12s} {status:8s} {what} = {err:.2e}")
    return ok


# ---------------------------------------------------------------- exported step on ORT


class OrtTrainStep:
    """The exported training step on ONNX Runtime. The step is a function of the parameters and
    the input; `step()` runs it and feeds every updated parameter (``output_1_<name>``) and
    carried state (``output_2_<name>``) back as the next call's input ``<name>``."""

    def __init__(self, model: str, path: Path, threads: int):
        import onnxruntime as ort

        weights, x = load(model)
        opts = ort.SessionOptions()
        opts.intra_op_num_threads = threads
        opts.inter_op_num_threads = 1
        opts.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        self.session = ort.InferenceSession(str(path), sess_options=opts, providers=["CPUExecutionProvider"])
        self.outputs = [o.name for o in self.session.get_outputs()]
        self.feeds = {}
        self.feedback: Dict[str, str] = {}  # output name -> input name
        self.keys: Dict[str, str] = {}  # input name -> benchmark weight name
        for inp in self.session.get_inputs():
            if inp.name == "input1":
                self.feeds[inp.name] = x
                continue
            self.keys[inp.name] = _harness_key(weights, inp.name)
            self.feeds[inp.name] = weights[self.keys[inp.name]]
            produced = [o for o in (f"output_1_{inp.name}", f"output_2_{inp.name}") if o in self.outputs]
            if len(produced) != 1:
                raise RuntimeError(f"exported step has no single updated value for input {inp.name!r}: {produced}")
            self.feedback[produced[0]] = inp.name

    def step(self) -> float:
        values = self.session.run(None, self.feeds)
        loss = None
        for name, value in zip(self.outputs, values):
            if name == "output_0":
                loss = float(value)
            elif name in self.feedback:
                self.feeds[self.feedback[name]] = value
        return loss

    def params(self) -> Dict[str, np.ndarray]:
        return {self.keys[name]: np.asarray(value) for name, value in self.feeds.items() if name in self.keys}


def ort_ksteps(model: str, steps: int, threads: int) -> dict:
    """`steps` consecutive steps of the exported training step on ORT: losses and final parameters."""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / f"{model}_step.onnx"
        export_train_step(model, path)
        runner = OrtTrainStep(model, path, threads)
        losses = [runner.step() for _ in range(steps)]
        return {"losses": losses, "params": runner.params()}


# ---------------------------------------------------------------- timing


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
    # Training protocol of the row, the median time between consecutive step reports (stateful
    # Camel rows; see step_spans), the loss of the last timed step, and the reconciliation
    # verdict of the (model, config) pair when a reconciliation file was given.
    "protocol",
    "span_median_ms",
    "last_loss",
    "reconciled",
]


def _latency_fields(times: List[float]) -> Dict[str, float]:
    return {
        "latency_median_ms": stats.median(times),
        "latency_p10_ms": stats.percentile(times, 10),
        "latency_p90_ms": stats.percentile(times, 90),
        "latency_mean_ms": sum(times) / len(times),
    }


def torch_trial_row(model: str, reps: int, warmup: int, threads: int) -> dict:
    """One PyTorch trial, run inside a fresh process (see torch_trial). PyTorch's steps are
    stateful under either protocol: the optimizer updates the parameters in place."""
    import torch

    from common.memory import peak_rss_self_kb

    torch.set_num_threads(threads)
    weights, x = load(model)
    net = torch_model(model, weights)
    opt = torch.optim.SGD(net.parameters(), lr=LR)
    xt = torch.from_numpy(x)
    last = [float("nan")]

    def step():
        opt.zero_grad(set_to_none=True)
        loss = torch_loss(net, xt)
        loss.backward()
        opt.step()
        last[0] = loss.detach()

    start = time.perf_counter()
    step()
    compile_s = time.perf_counter() - start
    for _ in range(warmup - 1):
        step()
    warmup_s = time.perf_counter() - start
    times = []
    for _ in range(reps):
        begin = time.perf_counter()
        step()
        times.append((time.perf_counter() - begin) * 1000.0)
    return {
        "compile_s": compile_s,
        "warmup_s": warmup_s,
        **_latency_fields(times),
        "peak_rss_mb": peak_rss_self_kb() / 1024.0,
        "protocol": STATEFUL,
        "last_loss": float(last[0]),
    }


def camel_trial_row(
    model: str,
    passes: Tuple[str, ...],
    reps: int,
    warmup: int,
    threads: int,
    protocol: str = STATEFUL,
    log: Optional[str] = None,
) -> dict:
    """One Camel trial; runs inside a fresh Python process that has no other child (see
    camel_trial), so the kernel's children high-water mark is this run's peak RSS."""
    from common.memory import run_with_peak_rss

    env = _camel_env(
        model,
        {
            "CAMEL_BENCH_WORKLOAD": "train",
            "CAMEL_BENCH_TRAIN_PROTOCOL": protocol,
            "CAMEL_BENCH_REPS": str(reps),
            "CAMEL_BENCH_WARMUP": str(warmup),
            "CAMEL_BENCH_THREADS": str(threads),
        },
    )
    cmd = camel_command(model, passes)
    code, stdout, stderr, peak_kb = run_with_peak_rss(cmd, camel_model(model).parent, env)
    if log:
        write_raw_log(Path(log), cmd, env, code, stdout, stderr)
    if code != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed ({code}): {stderr.strip()[-400:]}")
    if protocol_of(stdout) != protocol:
        raise RuntimeError(f"asked for the {protocol} protocol, the model ran {protocol_of(stdout)!r}")
    res = parse_bench_lines(stdout)
    steps = parse_steps(stdout)
    spans = step_spans(steps)
    row = {
        "compile_s": res.summary.get("compile_ms", float("nan")) / 1000.0,
        "warmup_s": res.summary.get("warmup_ms", float("nan")) / 1000.0,
        **_latency_fields(res.latencies_ms),
        "peak_rss_mb": peak_kb / 1024.0 if peak_kb else None,
        "protocol": STATEFUL if protocol == STATEFUL else STATELESS_LABEL,
        "span_median_ms": stats.median(spans) if spans else None,
        "last_loss": steps[-1]["loss"] if steps else None,
    }
    return row


def ort_trial_row(model: str, reps: int, warmup: int, threads: int) -> dict:
    """One trial of the exported training step on ORT (fresh process): the export is done first
    and not timed (its wall time is in the notes); the first step includes creating the session."""
    from common.memory import peak_rss_self_kb

    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / f"{model}_step.onnx"
        export_s = export_train_step(model, path)
        start = time.perf_counter()
        runner = OrtTrainStep(model, path, threads)
        loss = runner.step()
        compile_s = time.perf_counter() - start
        for _ in range(warmup - 1):
            loss = runner.step()
        warmup_s = time.perf_counter() - start
        times = []
        for _ in range(reps):
            begin = time.perf_counter()
            loss = runner.step()
            times.append((time.perf_counter() - begin) * 1000.0)
    return {
        "compile_s": compile_s,
        "warmup_s": warmup_s,
        **_latency_fields(times),
        "peak_rss_mb": peak_rss_self_kb() / 1024.0,
        "protocol": STATEFUL,
        "last_loss": loss,
        "notes": f"export {export_s:.2f} s (process wall, untimed); peak RSS includes Python and the exporter's parent",
    }


def _in_fresh_process(call: str) -> dict:
    code = f"import json, sys; sys.path.insert(0, {str(BENCH_ROOT)!r}); import train; print(json.dumps(train.{call}))"
    proc = subprocess.run([sys.executable, "-c", code], cwd=BENCH_ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip()[-400:])
    return json.loads(proc.stdout.strip().splitlines()[-1])


def load_reconciliation(path: Path) -> Dict[Tuple[str, str], str]:
    """(model, config) -> verdict from a reconciliation.csv of the stateful protocol."""
    import csv

    with path.open(newline="") as f:
        return {(r["model"], r["config"]): r["verdict"] for r in csv.DictReader(f) if r.get("protocol", STATEFUL) == STATEFUL}


def time_models(
    models: List[str],
    trials: int,
    reps: int,
    warmup: int,
    threads: int,
    configs: Optional[List[str]] = None,
    protocol: str = STATEFUL,
    verdicts: Optional[Dict[Tuple[str, str], str]] = None,
    include_unreconciled: bool = False,
    raw_dir: Optional[Path] = None,
) -> List[dict]:
    """Raw rows, one per (model, config, trial), in run.py's format."""
    configs = configs or [TORCH_CONFIG, *CAMEL_CONFIGS]
    rows: List[dict] = []
    for trial in range(1, trials + 1):
        for model in models:
            # Rotate the order per trial so no configuration always runs first.
            k = (trial - 1) % len(configs)
            for config in configs[k:] + configs[:k]:
                base = {"model": model, "config": config, "trial": trial, "threads": threads, "warmup": warmup, "reps": reps}
                verdict = None
                if verdicts is not None and config != TORCH_CONFIG:
                    verdict = verdicts.get((model, config), "MISSING")
                    base["reconciled"] = verdict
                    if verdict != "PASS" and not include_unreconciled:
                        rows.append({**base, "status": "skipped", "notes": f"not timed: reconciliation verdict {verdict}"})
                        print(f"trial {trial}/{trials} {model:12s} {config:16s} not timed: reconciliation verdict {verdict}", flush=True)
                        continue
                try:
                    if config == TORCH_CONFIG:
                        res = _in_fresh_process(f"torch_trial_row({model!r}, {reps}, {warmup}, {threads})")
                    elif config == ORT_CONFIG:
                        if protocol != STATEFUL:
                            raise RuntimeError("the exported step is only driven statefully")
                        res = _in_fresh_process(f"ort_trial_row({model!r}, {reps}, {warmup}, {threads})")
                    else:
                        passes = CAMEL_CONFIGS[config]
                        log = str(raw_dir / f"time_{protocol}_{model}_{config}_t{trial}.log") if raw_dir else None
                        res = _in_fresh_process(
                            f"camel_trial_row({model!r}, {passes!r}, {reps}, {warmup}, {threads}, {protocol!r}, {log!r})"
                        )
                    rows.append({**base, "status": "ok", **res})
                    span = res.get("span_median_ms")
                    print(
                        f"trial {trial}/{trials} {model:12s} {config:16s} {res['latency_median_ms']:8.3f} ms  "
                        f"warmup {res['warmup_s']:.2f} s  rss {res['peak_rss_mb'] or 0:.0f} MB"
                        + (f"  span {span:.3f} ms" if span else "")
                        + (f"  [reconciliation {verdict}]" if verdict and verdict != "PASS" else ""),
                        flush=True,
                    )
                except Exception as exc:
                    rows.append({**base, "status": "failed", "notes": str(exc)})
                    print(f"trial {trial}/{trials} {model:12s} {config:16s} FAILED: {exc}", flush=True)
    return rows


def print_summary(summary: List[dict]) -> None:
    print()
    for r in summary:
        if r.get("median_ms") is None:
            print(f"  {r['model']:<12} {r['config']:<16} {r['status']}: {r.get('notes') or ''}")
            continue
        sp = r.get("speedup")
        sp_txt = f"  x{sp:.2f} [{r['speedup_ci_low']:.2f}, {r['speedup_ci_high']:.2f}] vs {r['baseline']}" if sp else ""
        print(
            f"  {r['model']:<12} {r['config']:<16} {r['median_ms']:8.3f} ms [{r['ci_low_ms']:.3f}, {r['ci_high_ms']:.3f}]"
            f" n={r['trials']}  warmup {r['warmup_s']:.2f} s  rss {r['peak_rss_mb'] or 0:.0f} MB{sp_txt}"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default=",".join(MODELS))
    parser.add_argument("--check", action="store_true", help="compare gradients with PyTorch")
    parser.add_argument(
        "--check-configs", default="camel_nvm,camel_opt_nvm", help="Camel configurations to check"
    )
    parser.add_argument("--rtol", type=float, default=1e-4, help="gradient tolerance relative to max |grad| (see check)")
    parser.add_argument("--check-ksteps", action="store_true", help="compare the stateful loop with PyTorch's K steps")
    parser.add_argument("--ktol", type=float, default=1e-4, help="K-step tolerance (losses and final parameters)")
    parser.add_argument("--check-onnx", action="store_true", help="export the training step and run it on ORT")
    parser.add_argument("--time", action="store_true", help="time training steps")
    parser.add_argument(
        "--stateless",
        action="store_true",
        help="time the stateless protocol: every step starts from the same weights; updates are NOT consumed",
    )
    parser.add_argument("--configs", default=",".join([TORCH_CONFIG, *CAMEL_CONFIGS]), help=f"timed configurations, from {ALL_CONFIGS}")
    parser.add_argument("--reconciliation", type=Path, help="reconciliation.csv; only PASS pairs are timed")
    parser.add_argument("--include-unreconciled", action="store_true", help="time non-PASS pairs too (rows are marked)")
    parser.add_argument("--raw-dir", type=Path, help="save every Camel run's output there, one file per run")
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--reps", type=int, default=20)
    # Time the steady state, as for PyTorch. (FastVM once retained intermediates longer than
    # NodeVM and needed many steps to reach steady state; since ec34de33 it polls the GC
    # slow-path flag at every call boundary and matches NodeVM's peak RSS, but warmup=40
    # keeps the measurement on the same footing as the torch baseline.)
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--threads", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    models = [m for m in args.models.split(",") if m]
    configs = [c for c in args.configs.split(",") if c]
    for config in configs:
        if config not in ALL_CONFIGS:
            parser.error(f"unknown configuration {config!r}; known: {ALL_CONFIGS}")
    protocol = STATELESS if args.stateless else STATEFUL
    ok = True
    if args.check_onnx:
        ok = check_onnx(models, args.rtol)
        print("onnx training-step check:", "PASS" if ok else "FAIL")
    if args.check_ksteps:
        kconfigs = [c for c in args.check_configs.split(",") if c]
        kok = check_ksteps(models, kconfigs, args.warmup, args.reps, args.ktol, args.raw_dir)
        print("K-step check:", "PASS" if kok else "FAIL")
        ok &= kok
    if args.check or not (args.time or args.check_onnx or args.check_ksteps):
        gok = check(models, args.rtol, [c for c in args.check_configs.split(",") if c])
        print("gradient check:", "PASS" if gok else "FAIL")
        if not gok:
            # This check compares two float32 evaluations. Where a relu input lies within
            # float32 rounding of zero the two may take different sides of the kink, and the
            # gradient is discontinuous there, so a mismatch is not yet a verdict on Camel's
            # rules: tools/grad_referee.py compares both with a float64 reference and names
            # the relu units involved (README, "Gradient agreement").
            print("  a MISMATCH on a model with relu may be a relu decision, not a rule: run tools/grad_referee.py --model <model> --kinks")
        ok &= gok
    if args.time:
        if args.stateless:
            configs = [c for c in configs if c != ORT_CONFIG]
            print(
                "[protocol] stateless: every Camel step starts from the same weights and its update is NOT consumed; "
                "an optimizing pipeline may remove the update and the backward pass. These are not training-step timings."
            )
        verdicts = load_reconciliation(args.reconciliation) if args.reconciliation else None
        rows = time_models(
            models, args.trials, args.reps, args.warmup, args.threads, configs, protocol,
            verdicts, args.include_unreconciled, args.raw_dir,
        )
        summary = stats.summarize(rows, baseline=TORCH_CONFIG)
        print_summary(summary)
        out = args.out or BENCH_ROOT / "results" / f"train-{time.strftime('%Y%m%d-%H%M%S')}.csv"
        out.parent.mkdir(parents=True, exist_ok=True)
        _write_csv(out, COLUMNS, rows)
        spath = out.with_name(f"{out.stem}_summary{out.suffix or '.csv'}")
        _write_csv(spath, stats.SUMMARY_COLUMNS, summary)
        meta = envinfo.write_meta(
            out,
            threads=args.threads,
            extra={
                "workload": "training",
                "protocol": STATEFUL if protocol == STATEFUL else STATELESS_LABEL,
                "trials": args.trials,
                "warmup": args.warmup,
                "reps": args.reps,
                "configs": {c: " ".join(config_commands()[c]) for c in configs},
                "reconciliation": str(args.reconciliation) if args.reconciliation else None,
            },
        )
        print(f"[done] wrote {out}, {spath} and {meta}")
        print(f"[note] {envinfo.NOTE}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
