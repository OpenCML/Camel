"""E7 route C: TF1-graph-mode training step -> tf2onnx.

The premise is honored first: a TF1 training step IS a graph (tf.gradients +
GradientDescentOptimizer.minimize) and it runs fine in a TF1 session. Then we
try to get that graph out as ONNX, twice:
  C2a  unfrozen GraphDef (variables live as VarHandle/ReadVariableOp) -> tf2onnx
  C2b  frozen GraphDef (convert_variables_to_constants) -> tf2onnx
  C3   whatever C2b produced, if anything: census + ORT run — check whether
       variable-update semantics survived (expect: baked constants, one-shot)

Runs in venv-diy (tf 2.21 via tf.compat.v1.disable_v2_behavior, tf2onnx 1.17).
Writes route_c.log / route_c.json.
"""

from __future__ import annotations

import json
import traceback
from collections import Counter
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
LOG = open(HERE / "route_c.log", "w", encoding="utf-8")
OUT = {}
LR = 0.01


def say(*a):
    line = " ".join(str(x) for x in a)
    print(line)
    LOG.write(line + "\n")
    LOG.flush()


def census_of(path):
    import onnx

    m = onnx.load(str(path))
    return {
        "nodes": len(m.graph.node),
        "ops": dict(Counter(n.op_type for n in m.graph.node)),
        "inputs": [i.name for i in m.graph.input],
        "outputs": [o.name for o in m.graph.output],
        "initializers": [i.name for i in m.graph.initializer],
    }


def main() -> None:
    import tensorflow as tf

    say(f"tf {tf.__version__}")
    OUT["tf"] = tf.__version__
    tf.compat.v1.disable_v2_behavior()

    rng = np.random.default_rng(0)
    w1v = (rng.standard_normal((10, 32)) * 0.1).astype(np.float32)
    b1v = np.zeros(32, dtype=np.float32)
    w2v = (rng.standard_normal((32, 1)) * 0.1).astype(np.float32)
    b2v = np.zeros(1, dtype=np.float32)
    xv = rng.standard_normal((8, 10)).astype(np.float32)

    g = tf.Graph()
    with g.as_default():
        x = tf.compat.v1.placeholder(tf.float32, shape=[None, 10], name="x")
        w1 = tf.Variable(w1v, name="w1")
        b1 = tf.Variable(b1v, name="b1")
        w2 = tf.Variable(w2v, name="w2")
        b2 = tf.Variable(b2v, name="b2")
        y = tf.nn.relu(x @ w1 + b1) @ w2 + b2
        loss = tf.reduce_mean(y * y, name="loss")
        train_op = tf.compat.v1.train.GradientDescentOptimizer(LR).minimize(loss, name="train_op")

    # ---- C1: premise check — the TF1 training step runs in a session
    say("=" * 72)
    say("[C1] TF1 session executes the training step (premise)")
    with tf.compat.v1.Session(graph=g) as sess:
        sess.run(tf.compat.v1.global_variables_initializer())
        losses = []
        for i in range(5):
            l, _ = sess.run([loss, train_op], feed_dict={x: xv})
            losses.append(float(l))
        say("  losses:", [f"{v:.6g}" for v in losses])
        OUT["C1_losses"] = losses

    graph_def = g.as_graph_def()
    op_types = sorted({n.op for n in graph_def.node})
    say("  GraphDef op types:", op_types)
    OUT["C1_graphdef_ops"] = op_types

    # ---- C2a: unfrozen -> tf2onnx
    say("=" * 72)
    say("[C2a] tf2onnx on the UNFROZEN training-step GraphDef")
    import tf2onnx

    try:
        tf2onnx.convert.from_graph_def(
            graph_def,
            input_names=["x:0"],
            output_names=["loss:0", "train_op:0"],
            output_path=str(HERE / "route_c_unfrozen.onnx"),
        )
        c = census_of(HERE / "route_c_unfrozen.onnx")
        say("  CONVERTED (unexpected):", json.dumps(c))
        OUT["C2a"] = {"converted": c}
    except Exception:
        tb = traceback.format_exc()
        say(tb)
        OUT["C2a"] = {"error": tb}

    # ---- C2b: frozen -> tf2onnx
    say("=" * 72)
    say("[C2b] freeze variables, then tf2onnx")
    with tf.compat.v1.Session(graph=g) as sess:
        sess.run(tf.compat.v1.global_variables_initializer())
        frozen_def = tf.compat.v1.graph_util.convert_variables_to_constants(
            sess, graph_def, output_node_names=["loss", "train_op"]
        )
    frozen_ops = sorted({n.op for n in frozen_def.node})
    say("  frozen GraphDef op types:", frozen_ops)
    OUT["C2b_frozen_ops"] = frozen_ops
    try:
        tf2onnx.convert.from_graph_def(
            frozen_def,
            input_names=["x:0"],
            output_names=["loss:0", "train_op:0"],
            output_path=str(HERE / "route_c_frozen.onnx"),
        )
        c = census_of(HERE / "route_c_frozen.onnx")
        say("  CONVERTED:", json.dumps(c))
        OUT["C2b"] = {"converted": c}

        # ---- C3: did update semantics survive?
        say("=" * 72)
        say("[C3] ORT execution of the frozen product: do weights update?")
        try:
            import onnxruntime as ort

            sess2 = ort.InferenceSession(str(HERE / "route_c_frozen.onnx"), providers=["CPUExecutionProvider"])
            out_names = [o.name for o in sess2.get_outputs()]
            r1 = sess2.run(None, {"x:0": xv})
            r2 = sess2.run(None, {"x:0": xv})
            same = all(np.array_equal(a, b) for a, b in zip(r1, r2))
            say("  two identical runs produce identical outputs:", same)
            say("  graph inputs:", [i.name for i in sess2.get_inputs()], "-> no parameter port to feed updated weights back")
            with tf.compat.v1.Session(graph=g) as sess3:
                sess3.run(tf.compat.v1.global_variables_initializer())
                for i in range(2):
                    l, _ = sess3.run([loss, train_op], feed_dict={x: xv})
                    say(f"  TF session step {i}: loss {float(l):.6g}  (weights DO update here)")
            OUT["C3"] = {"identical_outputs": same, "verdict": "update semantics dead: constants cannot be assigned; the ONNX graph is one-shot inference of the step-0 loss"}
        except Exception:
            tb = traceback.format_exc()
            say(tb)
            OUT["C3"] = {"error": tb}
    except Exception:
        tb = traceback.format_exc()
        say(tb)
        OUT["C2b"] = {"error": tb}

    LOG.close()
    (HERE / "route_c.json").write_text(json.dumps(OUT, indent=2, default=str))
    print("[done] wrote route_c.log / route_c.json")


if __name__ == "__main__":
    main()
