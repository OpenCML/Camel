"""JAX (jit + value_and_grad) baseline for the E2 training-step benchmark (CPU).

Same workload as ``train.py``: the loss is ``mean(y * y)`` of the model output
on the benchmark input; one training step is the loss, its gradient with
respect to every weight, and an SGD update (lr 0.01). Weights load from the
shared ``artifacts/<model>/weights/*.npy`` under their harness names, so the
initial parameters are bit-identical to every other framework's.

The models are direct float32 translations of ``common/reference_numpy.py``
(the NumPy ground truth). The GRU's time loop uses ``jax.lax.scan`` — the
idiomatic JAX way to carry state — which XLA compiles to a while loop; the
math is the same unrolled recurrence the other frameworks execute.

One step is a single ``jax.jit``-compiled function::

    loss, grads = jax.value_and_grad(loss_fn)(params, x)
    params = tree_map(lambda w, g: w - lr * g, params, grads)

so forward, backward, and the SGD update are all inside the XLA executable.
Every timed rep blocks on the outputs (JAX is asynchronous), and the updated
parameters are carried across reps exactly like the in-place updates of the
other frameworks. The first call (tracing + XLA compilation) is reported as
``compile_s``; ``warmup_s`` covers everything up to the end of warmup, compile
included — the same convention as ``train.py``.

Thread pinning: XLA:CPU sizes its intra-op pool from the visible cores, so the
§A.5 protocol pins the process (``taskset -c 0-3``) instead of a JAX knob;
``OMP_NUM_THREADS`` etc. are also set to ``--threads`` before importing jax.
There is deliberately no jax import at module level: importing this module
must work in environments without jax so ``run_all.py`` can record the
failure verbatim instead of crashing.

Usage:
  python baselines/jax_runner.py --models mlp --reps 5 --warmup 3 --out /tmp/jax.csv
  python baselines/jax_runner.py --check --golden baselines/golden
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import Dict, List

import numpy as np

BENCH_ROOT = Path(__file__).resolve().parents[1]
if str(BENCH_ROOT) not in sys.path:
    sys.path.insert(0, str(BENCH_ROOT))

from common import stats  # noqa: E402
from common.weights import load  # noqa: E402
from run import _write_csv  # noqa: E402

LR = 0.01  # same SGD learning rate as train.py
CONFIG = "jax_jit"


def _latency_fields(times: List[float]) -> Dict[str, float]:
    return {
        "latency_median_ms": stats.median(times),
        "latency_p10_ms": stats.percentile(times, 10),
        "latency_p90_ms": stats.percentile(times, 90),
        "latency_mean_ms": sum(times) / len(times),
    }


def _forwards():
    """Model forwards as (params, x) -> y, float32 jnp translations of reference_numpy."""
    import jax
    import jax.numpy as jnp

    from common.models import LN_EPS, N_HEADS

    def linear(p, x, name):
        return x @ p[f"{name}_w"] + p[f"{name}_b"]

    def mlp(p, x):
        h = jnp.maximum(linear(p, x, "fc1"), 0.0)
        h = jnp.maximum(linear(p, h, "fc2"), 0.0)
        return linear(p, h, "fc3")

    def gru(p, x):
        def cell(h, xt):
            r = jax.nn.sigmoid(xt @ p["w_ir"] + p["b_ir"] + h @ p["w_hr"] + p["b_hr"])
            z = jax.nn.sigmoid(xt @ p["w_iz"] + p["b_iz"] + h @ p["w_hz"] + p["b_hz"])
            n = jnp.tanh(xt @ p["w_in"] + p["b_in"] + r * (h @ p["w_hn"] + p["b_hn"]))
            return (1.0 - z) * n + z * h, None

        h0 = jnp.zeros((x.shape[0], p["w_hr"].shape[0]), x.dtype)
        hf, _ = jax.lax.scan(cell, h0, jnp.moveaxis(x, 1, 0))
        return linear(p, hf, "fc")

    def layer_norm(x, gamma, beta):
        mean = jnp.mean(x, axis=-1, keepdims=True)
        var = jnp.mean((x - mean) ** 2, axis=-1, keepdims=True)  # biased, as the reference
        return (x - mean) / jnp.sqrt(var + LN_EPS) * gamma + beta

    def transformer(p, x):
        return tf_block_nh(p, x, N_HEADS)

    def transformer_big(p, x):
        return tf_block_nh(p, x, 8)

    def tf_block_nh(p, x, n_heads):
        b, s, d = x.shape
        dh = d // n_heads

        def heads(t):  # [B,S,D] -> [B,H,S,Dh]
            return t.reshape(b, s, n_heads, dh).transpose(0, 2, 1, 3)

        a = layer_norm(x, p["ln1_gamma"], p["ln1_beta"])
        q, k, v = (heads(linear(p, a, n)) for n in ("q", "k", "v"))
        att = jax.nn.softmax(q @ k.transpose(0, 1, 3, 2) / jnp.sqrt(jnp.float32(dh)), axis=-1)
        ctx = (att @ v).transpose(0, 2, 1, 3).reshape(b, s, d)
        h = x + linear(p, ctx, "o")
        f = layer_norm(h, p["ln2_gamma"], p["ln2_beta"])
        f = linear(p, jax.nn.gelu(linear(p, f, "ffn1"), approximate=True), "ffn2")
        return h + f

    def tf_block(p, x, prefix):
        b, s, d = x.shape
        dh = d // N_HEADS

        def heads(t):
            return t.reshape(b, s, N_HEADS, dh).transpose(0, 2, 1, 3)

        def lin(t, name):
            return t @ p[f"{prefix}{name}_w"] + p[f"{prefix}{name}_b"]

        a = layer_norm(x, p[f"{prefix}ln1_gamma"], p[f"{prefix}ln1_beta"])
        q, k, v = (heads(lin(a, n)) for n in ("q", "k", "v"))
        att = jax.nn.softmax(q @ k.transpose(0, 1, 3, 2) / jnp.sqrt(jnp.float32(dh)), axis=-1)
        ctx = (att @ v).transpose(0, 2, 1, 3).reshape(b, s, d)
        h = x + lin(ctx, "o")
        f = layer_norm(h, p[f"{prefix}ln2_gamma"], p[f"{prefix}ln2_beta"])
        f = lin(jax.nn.gelu(lin(f, "ffn1"), approximate=True), "ffn2")
        return h + f

    def transformer4(p, x):
        for prefix in ("b1.", "b2.", "b3.", "b4."):
            x = tf_block(p, x, prefix)
        return x

    def conv2d(p, x, name, stride, pad):
        return jax.lax.conv_general_dilated(
            x,
            p[f"{name}_w"],
            (stride, stride),
            [(pad, pad), (pad, pad)],
            dimension_numbers=("NCHW", "OIHW", "NCHW"),
        ) + p[f"{name}_b"][None, :, None, None]

    def resnet(p, x):
        def block(h, nm):
            a = jnp.maximum(conv2d(p, h, f"{nm}_c1", 1, 1), 0.0)
            return jnp.maximum(conv2d(p, a, f"{nm}_c2", 1, 1) + h, 0.0)

        def block_ds(h, nm):
            a = jnp.maximum(conv2d(p, h, f"{nm}_c1", 2, 1), 0.0)
            b = conv2d(p, a, f"{nm}_c2", 1, 1)
            s = conv2d(p, h, f"{nm}_ds", 2, 0)
            return jnp.maximum(b + s, 0.0)

        h = jnp.maximum(conv2d(p, x, "stem", 1, 1), 0.0)
        h = block(block(h, "b11"), "b12")
        h = block(block_ds(h, "b21"), "b22")
        h = block(block_ds(h, "b31"), "b32")
        h = block(block_ds(h, "b41"), "b42")
        g = jnp.mean(h, axis=(2, 3))
        return linear(p, g, "fc")

    return {"mlp": mlp, "gru": gru, "transformer": transformer, "resnet": resnet, "transformer4": transformer4, "mlp_big": mlp, "gru_big": gru, "transformer_big": transformer_big}


def _make_step(fwd):
    """jit(loss + value_and_grad + SGD update); the whole step is one XLA executable."""
    import jax
    import jax.numpy as jnp

    def loss_fn(p, x):
        y = fwd(p, x)
        return jnp.mean(y * y)

    def step(p, x):
        loss, grads = jax.value_and_grad(loss_fn)(p, x)
        new = jax.tree_util.tree_map(lambda w, g: w - LR * g, p, grads)
        return loss, new

    return jax.jit(step)


def trial_row(model: str, reps: int, warmup: int, threads: int) -> dict:
    """One trial of (model, jax_jit); designed to run inside a fresh process."""
    os.environ.setdefault("JAX_PLATFORMS", "cpu")
    for var in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
        os.environ.setdefault(var, str(threads))
    try:
        import jax
        import jax.numpy as jnp
    except ImportError as exc:  # archived verbatim by run_all.py (failure is data)
        raise RuntimeError(f"jax is not importable in this environment: {exc}") from exc

    from common.memory import peak_rss_self_kb

    weights, x = load(model)
    params = {k: jnp.asarray(v) for k, v in weights.items()}
    xx = jnp.asarray(x)
    step = _make_step(_forwards()[model])

    start = time.perf_counter()
    loss, params = step(params, xx)
    jax.block_until_ready((loss, params))
    compile_s = time.perf_counter() - start
    for _ in range(warmup - 1):
        loss, params = step(params, xx)
    jax.block_until_ready((loss, params))
    warmup_s = time.perf_counter() - start
    times = []
    for _ in range(reps):
        begin = time.perf_counter()
        loss, params = step(params, xx)
        jax.block_until_ready((loss, params))
        times.append((time.perf_counter() - begin) * 1000.0)
    return {
        "compile_s": compile_s,
        "warmup_s": warmup_s,
        **_latency_fields(times),
        "peak_rss_mb": peak_rss_self_kb() / 1024.0,
    }


def check(model: str, golden_dir: Path, rtol: float) -> bool:
    """One jitted step from the shared weights vs the torch golden."""
    os.environ.setdefault("JAX_PLATFORMS", "cpu")
    import jax
    import jax.numpy as jnp

    golden = np.load(golden_dir / f"{model}_golden.npz")
    weights, x = load(model)
    params = {k: jnp.asarray(v) for k, v in weights.items()}
    step = _make_step(_forwards()[model])
    loss, params = step(params, jnp.asarray(x))
    jax.block_until_ready((loss, params))

    ok = True
    ref_loss = float(golden["__loss__"])
    loss_err = abs(float(loss) - ref_loss) / max(abs(ref_loss), 1e-12)
    ok &= loss_err <= rtol
    print(f"== {model} (jax jit+grad vs torch golden)")
    print(f"  {'loss':12s} {'ok' if loss_err <= rtol else 'MISMATCH':8s} |diff|/|ref| = {loss_err:.2e}")
    for name in sorted(weights):
        got, ref = np.asarray(params[name]), golden[name]
        scale = float(np.max(np.abs(weights[name]))) or 1.0
        err = float(np.max(np.abs(got - ref))) / scale
        status = "ok" if got.shape == ref.shape and err <= rtol else "MISMATCH"
        ok &= status == "ok"
        print(f"  {name:12s} {status:8s} max|diff|/max|w| = {err:.2e}")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--models", default="mlp,gru,transformer")
    parser.add_argument("--reps", type=int, default=20)
    parser.add_argument("--warmup", type=int, default=40)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--out", type=Path, help="write rows to this CSV (train.py columns)")
    parser.add_argument("--check", action="store_true", help="compare one jitted step against the torch golden")
    parser.add_argument("--golden", type=Path, default=BENCH_ROOT / "baselines" / "golden")
    parser.add_argument("--rtol", type=float, default=1e-4)
    args = parser.parse_args()
    models = [m for m in args.models.split(",") if m]

    if args.check:
        ok = all(check(m, args.golden, args.rtol) for m in models)
        print("jax golden check:", "PASS" if ok else "FAIL")
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


# train.py's CSV columns, duplicated literally so this module never imports train
# (the jax/ort environments do not necessarily have torch or psutil). Keep in sync.
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


if __name__ == "__main__":
    sys.exit(main())
