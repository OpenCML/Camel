"""Benchmark configuration registry and runner factories.

A configuration names one way of executing a model (framework + mode). This
module decides which configurations apply to which model, whether their
prerequisites exist (skip reasons), how to export ONNX graphs, and how to
build an in-process ``Runner``. The Camel configurations are plug-ins: they are
listed here but only become runnable once ``out/latest/bin/camel`` and
``benchmarks/<model>/model.cml`` (or ``artifacts/<model>/camel.onnx``) exist.

Framework imports happen lazily inside the factories so that a worker process
for one configuration never loads the other frameworks (keeps peak RSS honest).
"""

from __future__ import annotations

import importlib
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, Optional

import numpy as np

from common.runner import Runner
from common.weights import ARTIFACTS, BENCH_ROOT

REPO_ROOT = BENCH_ROOT.parent
EXE_SUFFIX = ".exe" if sys.platform == "win32" else ""


@dataclass(frozen=True)
class Config:
    name: str
    framework: str  # torch | tf | ort | camel
    description: str
    models: Optional[tuple] = None  # None -> applies to every model
    exporter: Optional[str] = None  # ONNX producer consumed by an ORT config
    camel_passes: tuple = ()  # passes appended to `camel <model.cml>`
    display: str = ""  # short label for reports/figures (defaults to ``name``)
    group: str = ""  # report grouping (defaults to the framework's GROUPS label)

    @property
    def label(self) -> str:
        return self.display or self.name

    @property
    def group_label(self) -> str:
        return self.group or GROUPS.get(self.framework, self.framework)


# Report groups, in display order. ORT configs share one group so the three ONNX
# producers are compared against each other on the same runtime.
GROUPS: Dict[str, str] = {"torch": "PyTorch", "tf": "TensorFlow", "ort": "ONNX Runtime", "camel": "Camel"}
GROUP_ORDER = list(GROUPS.values())

# Report labels of the ONNX exporters (export stage), keyed by ``Config.exporter``.
EXPORTERS: Dict[str, str] = {
    "torch": "torch.onnx.export",
    "tf": "tf2onnx",
    "camel": "Camel onnx.export_model",
}


CONFIGS: Dict[str, Config] = {
    c.name: c
    for c in (
        Config("torch_eager", "torch", "PyTorch eager, inference_mode", display="PyTorch eager"),
        Config("torch_compile", "torch", "torch.compile (inductor, default mode)", display="torch.compile"),
        Config(
            "torch_onnx_ort",
            "ort",
            "torch.onnx.export opset 17 -> ONNX Runtime",
            exporter="torch",
            display="ORT (torch.onnx)",
        ),
        Config(
            "torch_nn_gru",
            "torch",
            "library-kernel reference: torch.nn.GRU, same weights",
            models=("gru",),
            display="torch.nn.GRU",
        ),
        Config("tf_eager", "tf", "TensorFlow eager", display="TF eager"),
        Config("tf_function", "tf", "tf.function (graph mode)", display="tf.function"),
        Config("tf_xla", "tf", "tf.function(jit_compile=True)", display="TF XLA"),
        Config("tf_onnx_ort", "ort", "tf2onnx opset 17 -> ONNX Runtime", exporter="tf", display="ORT (tf2onnx)"),
        Config("camel_native", "camel", "camel <model.cml> (default std::nvm)", display="Camel nvm"),
        Config("camel_fvm", "camel", "camel <model.cml> std::fvm", camel_passes=("std::fvm",), display="Camel fvm"),
        Config("camel_jit", "camel", "camel <model.cml> std::jit", camel_passes=("std::jit",), display="Camel jit"),
        Config(
            "camel_opt",
            "camel",
            "camel <model.cml> tensor::fuse (graph optimization, then std::nvm)",
            camel_passes=("tensor::fuse",),
            display="Camel fuse+nvm",
        ),
        Config(
            "camel_opt_fvm",
            "camel",
            "camel <model.cml> tensor::fuse std::fvm",
            camel_passes=("tensor::fuse", "std::fvm"),
            display="Camel fuse+fvm",
        ),
        Config(
            "camel_onnx_ort", "ort", "Camel ONNX export -> ONNX Runtime", exporter="camel", display="ORT (Camel ONNX)"
        ),
    )
}

DEFAULT_CONFIGS = [
    "torch_eager",
    "torch_compile",
    "torch_onnx_ort",
    "torch_nn_gru",
    "tf_eager",
    "tf_function",
    "tf_xla",
    "tf_onnx_ort",
    "camel_native",
    "camel_opt",
    "camel_onnx_ort",
]


def applies(config: str, model: str) -> bool:
    models = CONFIGS[config].models
    return models is None or model in models


def onnx_path(model: str, exporter: str) -> Path:
    return ARTIFACTS / model / f"{exporter}.onnx"


def camel_binary() -> Path:
    override = os.environ.get("CAMEL_BENCH_BIN")
    if override:
        return Path(override)
    return REPO_ROOT / "out" / "latest" / "bin" / f"camel{EXE_SUFFIX}"


def camel_model(model: str) -> Path:
    return BENCH_ROOT / model / "model.cml"


def skip_reason(config: str, model: str) -> Optional[str]:
    """Return a human-readable reason if the config cannot run yet, else None."""
    cfg = CONFIGS[config]
    if not applies(config, model):
        return f"{config} only applies to {', '.join(cfg.models)}"
    if cfg.framework == "camel" or cfg.exporter == "camel":
        if not camel_binary().exists():
            return f"Camel binary not found at {camel_binary()} (build it with `npm run build`)"
        if not camel_model(model).exists():
            return f"{camel_model(model).relative_to(REPO_ROOT)} does not exist yet"
    return None


# ---------------------------------------------------------------------------
# Thread pinning
# ---------------------------------------------------------------------------


def thread_env(threads: int, interop: int = 1) -> Dict[str, str]:
    """Environment variables that pin every runtime's thread pools."""
    n = str(threads)
    return {
        "OMP_NUM_THREADS": n,
        "MKL_NUM_THREADS": n,
        "OPENBLAS_NUM_THREADS": n,
        "TF_NUM_INTRAOP_THREADS": n,
        "TF_NUM_INTEROP_THREADS": str(interop),
        "CAMEL_BENCH_THREADS": n,
        "TF_CPP_MIN_LOG_LEVEL": "2",
    }


def pin_framework_threads(framework: str, threads: int, interop: int = 1) -> None:
    if framework == "torch":
        from common import torch_utils

        torch_utils.set_threads(threads, interop)
    elif framework == "tf":
        from common import tf_utils

        tf_utils.set_threads(threads, interop)


# ---------------------------------------------------------------------------
# ONNX export and runner construction
# ---------------------------------------------------------------------------


def export(model: str, exporter: str, weights: Dict[str, np.ndarray], x: np.ndarray) -> Path:
    """Export ``model`` to ONNX with the given framework exporter."""
    path = onnx_path(model, exporter)
    if exporter == "torch":
        importlib.import_module(f"{model}.model_torch").export_onnx(weights, x, path)
    elif exporter == "tf":
        importlib.import_module(f"{model}.model_tf").export_onnx(weights, path)
    elif exporter == "camel":
        from common import camel

        camel.export_onnx(model, path)
    else:
        raise ValueError(f"exporter {exporter!r} is not driven by the harness")
    return path


def ort_runner(path: Path, threads: int, interop: int = 1) -> Runner:
    import onnxruntime as ort

    opts = ort.SessionOptions()
    opts.intra_op_num_threads = threads
    opts.inter_op_num_threads = interop
    opts.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    sess = ort.InferenceSession(str(path), sess_options=opts, providers=["CPUExecutionProvider"])
    name = sess.get_inputs()[0].name
    return Runner(
        prepare=lambda a: np.ascontiguousarray(a, dtype=np.float32),
        call=lambda a: sess.run(None, {name: a})[0],
    )


_FACTORIES: Dict[str, Callable] = {
    "torch_eager": lambda m, w: importlib.import_module(f"{m}.model_torch").make_eager(w),
    "torch_compile": lambda m, w: importlib.import_module(f"{m}.model_torch").make_compiled(w),
    "torch_nn_gru": lambda m, w: importlib.import_module(f"{m}.model_torch").make_nn_gru(w),
    "tf_eager": lambda m, w: importlib.import_module(f"{m}.model_tf").make_eager(w),
    "tf_function": lambda m, w: importlib.import_module(f"{m}.model_tf").make_function(w),
    "tf_xla": lambda m, w: importlib.import_module(f"{m}.model_tf").make_xla(w),
}


def build_runner(config: str, model: str, weights: Dict[str, np.ndarray], threads: int, interop: int = 1) -> Runner:
    """Build an in-process runner (not valid for camel_* native configs)."""
    cfg = CONFIGS[config]
    if cfg.framework == "ort":
        path = onnx_path(model, cfg.exporter)
        if not path.exists():
            raise FileNotFoundError(f"{path} missing; run the export stage first")
        return ort_runner(path, threads, interop)
    if cfg.framework == "camel":
        raise ValueError("camel native configs run out of process; use common.camel.run_camel")
    return _FACTORIES[config](model, weights)
