"""E7 route A: jax2tf -> tf2onnx — the route is structurally dead; produce the evidence.

Phases (choose by --phase):
  current   (venv-diy, jax 0.6.2 + tf 2.21 + tf2onnx 1.17)
            A1  inspect.signature of jax2tf.convert: enable_xla /
                native_serialization must be GONE (the legacy door is removed)
            A2  verbatim reproduction of jax-ml/jax#15510 (f converts; grad fails
                or degenerates). Crash-prone part runs in a subprocess; rc + tail
                are archived verbatim.
  legacy    (venv-jaxold, jax 0.4.30 + tf 2.21 + tf2onnx 1.17)
            A3  unified MLP (Linear(10,32)->ReLU->Linear(32,1), MSE) gradient
                subgraph via enable_xla=False: census + ORT numerics + missing
            A4  same with the SGD update wrapped in the JAX function: census +
                10-step ORT closed loop vs JAX reference trajectory

Writes route_a_<phase>.log / route_a_<phase>.json next to this file.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
LR = 0.01


def say(log, *a):
    line = " ".join(str(x) for x in a)
    print(line)
    log.write(line + "\n")
    log.flush()


# ------------------------------------------------------------------ current phase
def phase_current(log) -> dict:
    import inspect

    out = {}
    import jax

    say(log, f"[A1] jax {jax.__version__}: jax2tf.convert signature")
    from jax.experimental import jax2tf

    sig = str(inspect.signature(jax2tf.convert))
    say(log, "signature:", sig)
    out["convert_signature"] = sig
    for gone in ("enable_xla", "native_serialization"):
        present = gone in sig
        say(log, f"  parameter {gone!r} present: {present}")
        out[f"{gone}_present"] = present

    say(log, "=" * 72)
    say(log, "[A1b] does enable_xla=False still WORK in current jax, or is it dead-in-effect?")
    try:
        import jax.numpy as jnp
        import numpy as np

        def g(x):
            return jax.grad(lambda v: jnp.sum(v * v))(x)

        import tensorflow as tf

        spec = [tf.TensorSpec((4,), tf.float32)]
        tf_fn = tf.function(jax2tf.convert(g, enable_xla=False), input_signature=spec, autograph=False)
        out["a1b_enable_xla_false"] = "accepted (convert returned without error)"
        say(log, "  enable_xla=False accepted by convert")
        try:
            import tf2onnx

            tf2onnx.convert.from_function(tf_fn, input_signature=spec)
            say(log, "  AND tf2onnx conversion SUCCEEDED (route partially alive at toy level)")
            out["a1b_enable_xla_false"] = "convert ok; tf2onnx ok (toy level)"
        except Exception:
            tail = traceback.format_exc().strip().splitlines()[-1]
            say(log, "  but tf2onnx conversion fails:", tail[:300])
            out["a1b_enable_xla_false"] = f"convert ok; tf2onnx fails: {tail[:300]}"
    except Exception:
        tail = traceback.format_exc().strip().splitlines()[-1]
        say(log, "  enable_xla=False REJECTED:", tail[:300])
        out["a1b_enable_xla_false"] = f"rejected: {tail[:300]}"

    say(log, "=" * 72)
    say(log, "[A2] verbatim reproduction of jax-ml/jax#15510 in a subprocess")
    child = HERE / "route_a_a2_child.py"
    child.write_text(
        '''"""Verbatim jax#15510 reproduction (minus tf2onnx crash isolation)."""
import jax, jax.experimental.jax2tf as j2t
jax.config.update("jax_platform_name", "cpu")
import tensorflow as tf
import tf2onnx

def f(x):
    w = jax.lax.full_like(x, 1.0)
    return jax.lax.dot(w, x)

def g(x):
    return jax.grad(f)(x)

spec = [tf.TensorSpec((1,), tf.float32)]

print("Converting f", flush=True)
f_tf = tf.function(j2t.convert(f), input_signature=spec, autograph=False)
f_onnx, _ = tf2onnx.convert.from_function(f_tf, input_signature=spec)
print("Converting f done", flush=True)

print("Converting g", flush=True)
g_tf = tf.function(j2t.convert(g), input_signature=spec, autograph=False)
g_onnx, _ = tf2onnx.convert.from_function(g_tf, input_signature=spec)
print("Converting g done", flush=True)

import onnx
for name, m in (("f", f_onnx), ("g", g_onnx)):
    ops = sorted({(n.op_type, n.domain or "ai.onnx") for n in m.graph.node})
    print(f"{name}: nodes={len(m.graph.node)} (op_type, domain) set = {ops}", flush=True)
''',
        encoding="utf-8",
    )
    proc = subprocess.run(
        [sys.executable, str(child)], capture_output=True, text=True, timeout=600, cwd=HERE
    )
    say(log, f"child rc={proc.returncode}" + (" (SIGSEGV)" if proc.returncode == -11 else ""))
    tail = (proc.stdout + "\n--- stderr tail ---\n" + proc.stderr[-2000:]).strip()
    say(log, tail)
    out["a2_child"] = {"rc": proc.returncode, "stdout_tail": proc.stdout[-1500:], "stderr_tail": proc.stderr[-2000:]}
    return out


# ------------------------------------------------------------------ legacy phase
def _unified_mlp():
    import jax
    import jax.numpy as jnp

    def fwd(p, x):
        return jnp.maximum(x @ p["w1"] + p["b1"], 0.0) @ p["w2"] + p["b2"]

    def loss(p, x):
        y = fwd(p, x)
        return jnp.mean(y * y)

    def grad_step(p, x):
        return jax.grad(loss)(p, x)

    def full_step(p, x):
        l, g = jax.value_and_grad(loss)(p, x)
        new = jax.tree_util.tree_map(lambda w, gg: w - LR * gg, p, g)
        return l, new

    return fwd, loss, grad_step, full_step


def _convert_and_check(log, out, key, fn, p, x, tag):
    """jax2tf(enable_xla=False) -> tf2onnx -> census + ORT numerics vs JAX.

    fn takes (p, x) with p a dict of arrays; the converted graph exposes each
    array as its own input (flattened pytree)."""
    import numpy as np
    import tensorflow as tf
    import tf2onnx
    from jax.experimental import jax2tf

    say(log, f"[{key}] jax2tf(enable_xla=False) -> tf2onnx ({tag})")
    rec = out[key] = {}
    spec = ({k: tf.TensorSpec(v.shape, tf.float32) for k, v in p.items()}, tf.TensorSpec(x.shape, tf.float32))
    try:
        tf_fn = tf.function(
            jax2tf.convert(fn, enable_xla=False), input_signature=spec, autograph=False
        )
        model_proto, _ = tf2onnx.convert.from_function(tf_fn, input_signature=spec)
        from collections import Counter

        import onnx

        census = {
            "nodes": len(model_proto.graph.node),
            "ops": dict(Counter(n.op_type for n in model_proto.graph.node)),
            "inputs": [i.name for i in model_proto.graph.input],
            "initializers": [i.name for i in model_proto.graph.initializer],
        }
        say(log, "census:", json.dumps(census))
        rec["census"] = census
        path = HERE / f"route_a_{key}.onnx"
        onnx.save(model_proto, str(path))
        import onnxruntime as ort

        sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
        feeds = {
            i.name: (x if i.name == "args_tf_1" else p[i.name.removeprefix("args_tf_0_")])
            for i in sess.get_inputs()
        }
        got = sess.run(None, feeds)
        ref = fn(p, x)
        import jax

        flat_ref = jax.tree_util.tree_leaves(ref)
        errs = []
        for g, r in zip(got, flat_ref):
            r = np.asarray(r)
            errs.append(float(np.max(np.abs(g - r))) / max(float(np.max(np.abs(r))), 1e-12))
        say(log, "ORT vs JAX:", [f"{e:.2e}" for e in errs])
        rec["numeric"] = [f"{e:.2e}" for e in errs]
    except Exception:
        tb = traceback.format_exc()
        say(log, tb)
        rec["error"] = tb


def phase_legacy(log) -> dict:
    import jax
    import numpy as np

    out = {"jax": jax.__version__}
    say(log, f"legacy phase: jax {jax.__version__}")
    rng = np.random.default_rng(0)
    w1 = (rng.standard_normal((10, 32)) * 0.1).astype(np.float32)
    b1 = np.zeros(32, dtype=np.float32)
    w2 = (rng.standard_normal((32, 1)) * 0.1).astype(np.float32)
    b2 = np.zeros(1, dtype=np.float32)
    x = rng.standard_normal((8, 10)).astype(np.float32)
    params = [w1, b1, w2, b2]
    p = {"w1": w1, "b1": b1, "w2": w2, "b2": b2}

    fwd, loss, grad_step, full_step = _unified_mlp()

    def grad_only(p, x):
        return grad_step(p, x)

    def step_only(p, x):
        return full_step(p, x)

    _convert_and_check(log, out, "A3", grad_only, p, x, "gradient subgraph")
    _convert_and_check(log, out, "A4", step_only, p, x, "full training step (SGD in-graph)")

    if "census" in out.get("A4", {}) and "numeric" in out["A4"]:
        # 10-step ORT closed loop vs JAX trajectory
        import numpy as np
        import onnxruntime as ort

        say(log, "[A4-loop] 10-step ORT closed loop vs JAX reference")
        sess = ort.InferenceSession(str(HERE / "route_a_A4.onnx"), providers=["CPUExecutionProvider"])
        cur = dict(p)
        losses_ort = []
        for i in range(10):
            feeds = {
                n.name: (x if n.name == "args_tf_1" else cur[n.name.removeprefix("args_tf_0_")])
                for n in sess.get_inputs()
            }
            got = sess.run(None, feeds)
            losses_ort.append(float(got[0]))
            # full_step returns (loss, new_dict); dict leaves flatten in key order
            cur = dict(zip(("b1", "b2", "w1", "w2"), got[1:]))
        pj = dict(p)
        losses_jax = []
        for i in range(10):
            l, pj = full_step(pj, x)
            losses_jax.append(float(l))
        diffs = [f"{abs(a - b):.2e}" for a, b in zip(losses_ort, losses_jax)]
        say(log, "losses ort :", [f"{v:.6g}" for v in losses_ort])
        say(log, "losses jax :", [f"{v:.6g}" for v in losses_jax])
        say(log, "|diff|     :", diffs)
        out["A4_loop"] = {"losses_ort": losses_ort, "losses_jax": losses_jax, "max_diff": max(diffs)}
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", required=True, choices=["current", "legacy"])
    args = ap.parse_args()
    log = open(HERE / f"route_a_{args.phase}.log", "w", encoding="utf-8")
    out = phase_current(log) if args.phase == "current" else phase_legacy(log)
    log.close()
    (HERE / f"route_a_{args.phase}.json").write_text(json.dumps(out, indent=2, default=str))
    print(f"[done] wrote route_a_{args.phase}.log/.json")


if __name__ == "__main__":
    main()
