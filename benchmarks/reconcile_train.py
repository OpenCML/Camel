# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

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

  1. Gradient operators (operators whose name contains ``grad``). The rule:
     every gradient operator whose result reaches a parameter update or the
     returned loss is executed under the optimised configuration; operators
     whose results are provably unused (e.g. the gradient with respect to the
     model input) may be eliminated.
     The reference multiset is what ``camel_nvm`` (``std::macro std::nvm``)
     executes per step under the stateful protocol: NodeVM executes the
     macro-expanded program in source order without optimizing, so that is
     every gradient operator the differentiated program contains. It must be
     non-empty. A configuration without optimisation passes (only std::macro
     and a VM) must execute exactly the reference. An optimised configuration
     must execute at least the reference minus the operators this script
     proves unused, and nothing beyond the reference. The proof is a demand
     analysis of the model's ``step`` graph after ``std::macro`` (see
     "unused gradient operators" below): no per-model list. It is accepted
     only if the per-step gradient multiset it derives from the graph equals
     the measured reference; otherwise nothing is exempted. The analysis,
     the unused operators and every consumer of their results are written to
     ``evidence/<model>/unused_gradients.txt``.
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
  evidence/<model>/macro.rgir.dot                          the graph the analysis read
  evidence/<model>/unused_gradients.txt                    every gradient operator of the step: operands, demanded or unused, why

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
    # The rule's ingredients: what the reference executes, what the graph analysis proves
    # unused (and may therefore be left out by an optimised configuration), which operators
    # this configuration left out, and where the proof is.
    "grad_ops_reference",
    "grad_ops_proven_unused",
    "grad_ops_eliminated",
    "unused_evidence",
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


# ---------------------------------------------------------------- unused gradient operators
#
# The rule: every gradient operator whose result reaches a parameter update or the returned
# loss is executed under an optimised configuration; operators whose results are provably
# unused (e.g. the gradient with respect to the model input) may be eliminated.
#
# "Provably unused" is decided here, from the graph the optimiser receives (the program after
# ``std::macro``, printed by ``std::rgir``), not from a per-model list: a demand analysis that
# starts from everything the training step returns (the loss and the updated parameters) and
# walks the data edges backwards. A gradient operator the demand never reaches is unused.


ALL = "*"  # demand for a whole value; otherwise a demand is a set of first-level slot keys
_CLUSTER = re.compile(r"^\s*subgraph cluster_(\w+) \{\s*$")
_GLABEL = re.compile(r'^\s*label="(.*)";\s*$')
_NODE = re.compile(r'^\s*(\w+) \[label="(.*?)", type="(\w+)"(.*)\];\s*$')
_EDGE = re.compile(r'^\s*(\w+) -> (\w+) \[label="([^"]*)", type="(\w+)"\];\s*$')
_FUNCTION = re.compile(r"^Function\(graph=(.*), tupleSlots=(\d+)\)$")
_SPAN = re.compile(r'span="(\d+):(\d+)-(\d+):(\d+)", srcfile="([^"]*)"')


class StepGraph:
    """The ``std::rgir`` text of a program: graphs (clusters), nodes, slot-indexed data edges."""

    def __init__(self, text: str):
        self.label: Dict[str, str] = {}  # graph id -> name
        self.parent: Dict[str, Optional[str]] = {}
        self.children: Dict[str, List[str]] = {}
        self.node: Dict[str, dict] = {}  # node id -> {label, type, graph, span}
        self.members: Dict[str, List[str]] = {}  # graph id -> node ids in declaration order
        self.ins: Dict[str, Dict[str, Dict[int, str]]] = {}  # consumer -> edge type -> slot -> source
        self.outs: Dict[str, List[Tuple[str, str, int]]] = {}  # source -> [(consumer, edge type, slot)]
        self.port: Dict[Tuple[str, str, int], str] = {}  # (graph, WITH|NORM, index) -> port node
        self.exit_src: Dict[str, str] = {}  # graph -> node whose value the graph returns
        stack: List[str] = []
        for line in text.splitlines():
            m = _CLUSTER.match(line)
            if m:
                gid = m.group(1)
                self.parent[gid] = stack[-1] if stack else None
                self.children.setdefault(gid, [])
                self.members.setdefault(gid, [])
                if stack:
                    self.children[stack[-1]].append(gid)
                stack.append(gid)
                continue
            if line.strip() == "}":
                if stack:
                    stack.pop()
                continue
            if not stack:
                continue
            m = _GLABEL.match(line)
            if m and stack[-1] not in self.label:
                self.label[stack[-1]] = m.group(1)
                continue
            m = _EDGE.match(line)
            if m:
                src, dst, lab, etype = m.groups()
                if etype in ("WITH_PORT", "NORM_PORT"):
                    self.port[(src, etype[:4], int(lab))] = dst
                elif etype == "EXIT":
                    self.exit_src[self.node[dst]["graph"] if dst in self.node else stack[-1]] = src
                else:
                    slot = int(lab.split("|")[1])
                    self.ins.setdefault(dst, {}).setdefault(etype, {})[slot] = src
                    self.outs.setdefault(src, []).append((dst, etype, slot))
                continue
            m = _NODE.match(line)
            if m:
                nid, lab, ntype, rest = m.groups()
                span = _SPAN.search(rest)
                self.node[nid] = {"label": lab, "type": ntype, "graph": stack[-1], "span": span.groups() if span else None}
                self.members[stack[-1]].append(nid)
        self.by_label: Dict[str, List[str]] = {}
        for gid, lab in self.label.items():
            self.by_label.setdefault(lab, []).append(gid)
        self.cports = {
            g: [n for n in ns if self.node[n]["type"] == "CLOSURE_PORT"] for g, ns in self.members.items()
        }
        self.port_of = {n: key for key, n in self.port.items()}

    # -- structure

    def inputs(self, n: str, etype: str) -> List[str]:
        slots = self.ins.get(n, {}).get(etype, {})
        return [slots[i] for i in sorted(slots)]

    def resolve(self, name: str, frm: str) -> Optional[str]:
        """The graph called `name` as seen from graph `frm`: lexically nearest, else the only one."""
        g: Optional[str] = frm
        while g is not None:
            for c in self.children.get(g, []):
                if self.label.get(c) == name:
                    return c
            if self.label.get(g) == name:
                return g
            g = self.parent.get(g)
        found = self.by_label.get(name, [])
        return found[0] if len(found) == 1 else None

    def closure_target(self, n: str) -> Optional[str]:
        """The graph a ``Function(graph=...)`` constant (or the FILL that closes over it) stands for."""
        info = self.node[n]
        if info["type"] == "FILL":
            tmpl = self.inputs(n, "NORM")
            return self.closure_target(tmpl[0]) if tmpl else None
        if info["type"] == "DATA":
            m = _FUNCTION.match(info["label"])
            if m:
                return self.resolve(m.group(1), info["graph"])
        return None

    def fill_keys(self, n: str) -> Optional[List[str]]:
        """Slot keys of a tuple/struct FILL, one per WITH input, or None when they cannot be told."""
        tmpl = self.inputs(n, "NORM")
        if not tmpl or self.node[tmpl[0]]["type"] != "DATA":
            return None
        lab = self.node[tmpl[0]]["label"]
        if len(lab) < 2 or lab[0] + lab[-1] not in ("()", "{}") or any(c in lab[1:-1] for c in "(){}[]\"'"):
            return None
        parts = [p.strip() for p in lab[1:-1].split(",") if p.strip()]
        keys = [p.split(":")[0].strip() for p in parts] if lab[0] == "{" else [str(i) for i in range(len(parts))]
        return keys if len(keys) == len(self.inputs(n, "WITH")) else None

    def describe(self, n: str) -> str:
        info = self.node[n]
        where = ""
        if info["span"]:
            l0, c0, _, _, src = info["span"]
            where = f"  <{Path(src).name}:{int(l0) + 1}:{int(c0) + 1}>"  # spans are 0-based
        return f"{n} {info['type']} {info['label']!r} in {self.label.get(info['graph'], '?')}[{info['graph']}]{where}"


class Liveness:
    """Which closures a call may reach (a small fixpoint over the value flow) and, from the root
    graph's result, which values are demanded."""

    MAX_PATH = 8

    def __init__(self, g: StepGraph, root: str):
        self.g, self.root = g, root
        self.fills_of: Dict[str, List[str]] = {}  # graph -> FILL nodes creating closures of it
        self.func_sites: Dict[str, List[str]] = {}
        self.calls = [n for n, i in g.node.items() if i["type"] == "CALL"]
        self.func_target: Dict[str, Optional[str]] = {}
        for n, info in g.node.items():
            if info["type"] == "FUNC":
                self.func_target[n] = g.resolve(info["label"], info["graph"])
                if self.func_target[n]:
                    self.func_sites.setdefault(self.func_target[n], []).append(n)
            elif info["type"] == "FILL" and g.closure_target(n):
                self.fills_of.setdefault(g.closure_target(n), []).append(n)
        self.closure_graphs = sorted({t for n in g.node if (t := g.closure_target(n))})
        self.memo: Dict[Tuple[str, tuple], frozenset] = {}
        self.overflow = False
        self._dirty = False
        self._closure_fixpoint()
        self.sites: Dict[str, List[str]] = {h: list(s) for h, s in self.func_sites.items()}
        self.unresolved_calls = []
        for c in self.calls:
            callees = self.callees(c)
            if not callees:
                self.unresolved_calls.append(c)
                callees = self.closure_graphs  # may be any closure
            for h in callees:
                self.sites.setdefault(h, []).append(c)
        self.demand: Dict[str, set] = {}
        self._demand_fixpoint()

    # -- closures: which graphs may the value of `n` at access path `path` be a closure of

    def _get(self, n: str, path: tuple) -> frozenset:
        key = (n, path)
        if key not in self.memo:
            self.memo[key] = frozenset()
            self._dirty = True
        return self.memo[key]

    def _eval(self, n: str, path: tuple) -> frozenset:
        g, info = self.g, self.g.node[n]
        t = info["type"]
        if len(path) > self.MAX_PATH:
            self.overflow = True
            return frozenset()
        out: set = set()
        if t in ("DATA", "FILL") and g.closure_target(n):
            return frozenset([g.closure_target(n)]) if not path else frozenset()
        if t == "FILL":
            keys, withs = g.fill_keys(n), g.inputs(n, "WITH")
            if path and keys and (path[0] in keys or (path[0].isdigit() and int(path[0]) < len(keys))):
                j = keys.index(path[0]) if path[0] in keys else int(path[0])
                out |= self._get(withs[j], path[1:])
            elif keys is None:
                for w in withs:
                    out |= self._get(w, path[1:]) | self._get(w, path)
        elif t == "ACCS":
            src = g.inputs(n, "NORM")
            if src:
                out |= self._get(src[0], (info["label"].lstrip("."),) + path)
        elif t == "FUNC":
            h = self.func_target.get(n)
            if h and h in g.exit_src:
                out |= self._get(g.exit_src[h], path)
        elif t == "CALL":
            callee = g.inputs(n, "WITH")
            for h in self._get(callee[0], ()) if callee else ():
                if h in g.exit_src:
                    out |= self._get(g.exit_src[h], path)
        elif t == "PORT" and n in g.port_of:
            h, kind, idx = g.port_of[n]
            for s in self.func_sites.get(h, []):
                args = g.inputs(s, kind)
                if idx < len(args):
                    out |= self._get(args[idx], path)
            for c in self.calls:
                withs = g.inputs(c, "WITH")
                if withs and h in self._get(withs[0], ()):
                    args = withs[1:] if kind == "WITH" else g.inputs(c, "NORM")
                    if idx < len(args):
                        out |= self._get(args[idx], path)
        elif t == "CLOSURE_PORT":
            h = info["graph"]
            j = g.cports[h].index(n)
            for f in self.fills_of.get(h, []):
                withs = g.inputs(f, "WITH")
                if j < len(withs):
                    out |= self._get(withs[j], path)
        elif t in ("JOIN", "GATE"):
            for src in g.inputs(n, "WITH") + (g.inputs(n, "NORM") if t == "GATE" else []):
                out |= self._get(src, path)
        return frozenset(out)

    def _closure_fixpoint(self) -> None:
        for c in self.calls:
            withs = self.g.inputs(c, "WITH")
            if withs:
                self._get(withs[0], ())
        changed = True
        while changed:
            changed = False
            self._dirty = False
            for key in list(self.memo):
                new = self._eval(*key)
                if new != self.memo[key]:
                    self.memo[key] = self.memo[key] | new
                    changed = True
            changed = changed or self._dirty

    def callees(self, call: str) -> List[str]:
        withs = self.g.inputs(call, "WITH")
        return sorted(self.memo.get((withs[0], ()), ())) if withs else []

    def target(self, site: str) -> List[str]:
        if self.g.node[site]["type"] == "FUNC":
            return [self.func_target[site]] if self.func_target.get(site) else []
        return self.callees(site) or list(self.closure_graphs)

    # -- demand

    def _add(self, n: str, keys) -> None:
        cur = self.demand.setdefault(n, set())
        if ALL in cur:
            return
        new = {ALL} if ALL in keys else set(keys) - cur
        if new:
            cur |= new
            self._changed = True

    def _port_demand(self, h: str, kind: str, idx: int):
        p = self.g.port.get((h, kind, idx))
        return {ALL} if p is None else self.demand.get(p, set())

    def _push(self, c: str) -> None:
        """Propagates the demand on node `c` to the values it reads."""
        g, info, d = self.g, self.g.node[c], self.demand.get(c, set())
        if not d:
            return
        t, withs, norms = info["type"], g.inputs(c, "WITH"), g.inputs(c, "NORM")
        if t == "ACCS":
            if norms:
                self._add(norms[0], {info["label"].lstrip(".")})
        elif t == "FILL" and g.closure_target(c):
            ports = g.cports[g.closure_target(c)]
            for j, w in enumerate(withs):
                self._add(w, self.demand.get(ports[j], set()) if len(ports) == len(withs) else {ALL})
        elif t == "FILL":
            keys = g.fill_keys(c)
            for j, w in enumerate(withs):
                if keys is None or ALL in d or keys[j] in d or str(j) in d:
                    self._add(w, {ALL})
        elif t == "FUNC":
            h = self.func_target.get(c)
            for kind, args in (("WITH", withs), ("NORM", norms)):
                for j, a in enumerate(args):
                    self._add(a, self._port_demand(h, kind, j) if h else {ALL})
        elif t == "CALL":
            if withs:
                self._add(withs[0], {ALL})
            callees = self.callees(c)
            for j, a in enumerate(withs[1:]):
                self._add(a, set().union(*[self._port_demand(h, "WITH", j) for h in callees]) if callees else {ALL})
            for j, a in enumerate(norms):
                self._add(a, set().union(*[self._port_demand(h, "NORM", j) for h in callees]) if callees else {ALL})
        elif t == "JOIN":
            for a in withs:
                self._add(a, d)
            for a in norms:
                self._add(a, {ALL})
        elif t == "GATE":
            for a in norms + withs:
                self._add(a, d)
        else:  # operators, branches and anything unknown read their inputs whole (CTRL edges order, they carry no value)
            for a in withs + norms:
                self._add(a, {ALL})

    def _demand_fixpoint(self) -> None:
        g = self.g
        self._changed = True
        while self._changed:
            self._changed = False
            for h, src in g.exit_src.items():
                if h == self.root:
                    self._add(src, {ALL})
                for s in self.sites.get(h, []):
                    self._add(src, self.demand.get(s, set()))
            for c in g.node:
                self._push(c)

    # -- what the step executes

    def reachable(self) -> Dict[str, Optional[int]]:
        """Graphs the root's execution enters -> how many times per execution of the root (None
        when that is not a constant of the graph: a branch arm, a recursion, or below one)."""
        g = self.g
        callers: Dict[str, List[str]] = {}
        seen, todo = {self.root}, [self.root]
        while todo:
            h = todo.pop()
            for n in g.members.get(h, []):
                if g.node[n]["type"] in ("FUNC", "CALL"):
                    for t in self.target(n):
                        callers.setdefault(t, []).append(n)
                        if t not in seen:
                            seen.add(t)
                            todo.append(t)
        mult: Dict[str, Optional[int]] = {}

        def count(h: str, stack: tuple) -> Optional[int]:
            if h == self.root:
                return 1
            if h in stack:
                return None
            if h in mult:
                return mult[h]
            total: Optional[int] = 0
            for s in callers.get(h, []):
                arm = any(g.node[c]["type"] == "JOIN" for c, _, _ in g.outs.get(s, []))
                several = len(self.target(s)) != 1
                sub = None if arm or several else count(g.node[s]["graph"], stack + (h,))
                total = None if sub is None or total is None else total + sub
            mult[h] = total
            return total

        return {h: count(h, ()) for h in sorted(seen)}

    # -- evidence

    def origin(self, n: str, depth: int = 0) -> str:
        """Where a value comes from, followed through closure captures to a node with a source span."""
        g, info = self.g, self.g.node[n]
        if info["type"] == "CLOSURE_PORT" and depth < 6:
            h = info["graph"]
            fills = self.fills_of.get(h, [])
            if len(fills) == 1:
                withs = g.inputs(fills[0], "WITH")
                j = g.cports[h].index(n)
                if j < len(withs):
                    return f"{n} (captured slot {j} of {g.label[h]}) = " + self.origin(withs[j], depth + 1)
        if info["type"] == "PORT" and n in g.port_of:
            h, kind, idx = g.port_of[n]
            return f"{n} PORT ({kind} argument {idx} of {g.label[h]}[{h}])"
        return g.describe(n)

    def capture_source(self, n: str) -> Optional[str]:
        """The node a closure port captured, when the closure is created in one place."""
        g, info = self.g, self.g.node[n]
        if info["type"] != "CLOSURE_PORT":
            return None
        fills = self.fills_of.get(info["graph"], [])
        withs = g.inputs(fills[0], "WITH") if len(fills) == 1 else []
        j = g.cports[info["graph"]].index(n)
        return withs[j] if j < len(withs) else None

    def forward_operators(self, n: str) -> List[str]:
        """Forward operators that read every value the gradient operator `n` captured from the
        forward pass: the operator(s) it is the derivative of."""
        g = self.g
        captured = [c for c in (self.capture_source(s) for s in g.inputs(n, "NORM")) if c]
        if not captured:
            return []
        readers = None
        for c in captured:
            mine = {x for x, e, _ in g.outs.get(c, []) if e == "NORM" and g.node[x]["type"] == "OPER"}
            if g.node[c]["type"] == "OPER":
                mine.add(c)
            readers = mine if readers is None else readers & mine
        return sorted(readers or [])

    def explain(self, n: str) -> List[str]:
        """Every consumer of the value of `n`, transitively, until the demand analysis says why
        nothing demanded reads it."""
        g = self.g
        lines: List[str] = []
        seen = set()

        def walk(x: str, indent: str) -> None:
            if x in seen:
                lines.append(f"{indent}(consumers of {x} shown above)")
                return
            seen.add(x)
            data = [(c, e, s) for c, e, s in g.outs.get(x, []) if e != "CTRL"]
            h = g.node[x]["graph"]
            if g.exit_src.get(h) == x:
                lines.append(f"{indent}is the result of graph {g.label[h]}[{h}]; its call sites:")
                for s in self.sites.get(h, []):
                    d = self.demand.get(s, set())
                    lines.append(f"{indent}  {g.describe(s)}: demanded slots {sorted(d) or 'none'}")
                    for c, e, _ in g.outs.get(s, []):
                        if e != "CTRL":
                            lines.append(f"{indent}    read by {g.describe(c)} (demand {sorted(self.demand.get(c, set())) or 'none'})")
            if not data and g.exit_src.get(h) != x:
                lines.append(f"{indent}has no data consumer")
            for c, e, s in data:
                d = self.demand.get(c, set())
                keys = g.fill_keys(c) if g.node[c]["type"] == "FILL" and not g.closure_target(c) else None
                if keys is not None and e == "WITH":
                    lines.append(
                        f"{indent}-> slot {s} (key {keys[s]!r}) of {g.describe(c)}; demanded slots of that value: {sorted(d) or 'none'}"
                    )
                    if ALL not in d and keys[s] not in d and str(s) not in d:
                        lines.append(f"{indent}   slot {keys[s]!r} is not demanded. Who reads the value:")
                        walk(c, indent + "     ")
                else:
                    lines.append(f"{indent}-> {e} input {s} of {g.describe(c)}; demand on it: {sorted(d) or 'none'}")
                    if not d:
                        walk(c, indent + "     ")

        walk(n, "    ")
        return lines


def analyse_step_graph(text: str, root_name: str = "step") -> dict:
    """Gradient operators of the training step in the ``std::macro`` graph, split into demanded
    and unused, with per-step execution counts where the graph fixes them."""
    g = StepGraph(text)
    roots = g.by_label.get(root_name, [])
    if len(roots) != 1:
        raise RuntimeError(f"expected one graph named {root_name!r} in the program, found {len(roots)}")
    live = Liveness(g, roots[0])
    mult = live.reachable()
    ops = []
    for h, k in mult.items():
        for n in g.members.get(h, []):
            info = g.node[n]
            if info["type"] == "OPER" and "grad" in info["label"]:
                ops.append({"node": n, "op": info["label"].split(":")[-1], "graph": h, "per_step": k,
                            "unused": not live.demand.get(n)})
    return {"graph": g, "liveness": live, "ops": ops, "multiplicity": mult}


def step_graph_text(model: str) -> str:
    """The program after ``std::macro`` as ``std::rgir`` prints it (nothing is executed)."""
    stdout, _ = train.run_camel_full(model, ("std::macro", "std::rgir"), {"CAMEL_BENCH_TRAIN_PROTOCOL": train.STATEFUL})
    return stdout


def multiset_of(ops: List[dict]) -> Optional[Dict[str, int]]:
    out: Dict[str, int] = {}
    for o in ops:
        if o["per_step"] is None:
            return None
        if o["per_step"]:
            out[o["op"]] = out.get(o["op"], 0) + o["per_step"]
    return out


def evidence_text(model: str, an: dict, reference: Optional[Dict[str, int]]) -> str:
    g, live = an["graph"], an["liveness"]
    root = live.root
    lines = [
        f"# {model}: gradient operators of the training step and whether their results are used",
        "# Source: the program after std::macro (`camel model.cml std::macro std::rgir`, stateful protocol),",
        f"# analysed by reconcile_train.py from graph {g.label[root]}[{root}]: demand starts at everything",
        "# that graph returns (the loss and the updated parameters) and follows data edges backwards.",
        "# Source positions are file:line:column, 1-based.",
        "",
        f"calls whose callee could not be resolved (treated as calling any closure): {len(live.unresolved_calls)}",
        f"access-path limit hit while resolving closures: {'yes' if live.overflow else 'no'}",
        "",
        "## graphs the step enters (executions per step; n/a = decided at run time)",
    ]
    for h, k in an["multiplicity"].items():
        lines.append(f"  {g.label[h]}[{h}]: {'n/a' if k is None else k}")
    static = multiset_of(an["ops"])
    lines += ["", "## gradient operators", f"  per step, from the graph: {show(static) if static is not None else 'n/a (run-time dependent)'}"]
    if reference is not None:
        lines.append(f"  per step, executed by {REFERENCE} (measured): {show(reference)}")
        verdict = "n/a (the graph does not fix the count)" if static is None else "yes" if static == reference else "NO"
        lines.append(f"  graph count equals measured count: {verdict}")
    for o in an["ops"]:
        n = o["node"]
        lines.append("")
        lines.append(f"### {g.describe(n)}  [{'UNUSED' if o['unused'] else 'demanded'}; executions per step: {o['per_step'] if o['per_step'] is not None else 'n/a'}]")
        for etype in ("NORM", "WITH"):
            for j, src in enumerate(g.inputs(n, etype)):
                lines.append(f"    operand {etype} {j}: {live.origin(src)}")
        for f in live.forward_operators(n):
            lines.append(f"    differentiates: {g.describe(f)}")
        if o["unused"]:
            lines.append("    consumers of its result:")
            lines += live.explain(n)
        else:
            readers = [f"{c} {g.node[c]['label']!r}" for c, e, _ in g.outs.get(n, []) if e != "CTRL" and live.demand.get(c)]
            lines.append(f"    demanded: read by {', '.join(readers) or '(the graph result)'}")
    return "\n".join(lines) + "\n"


def unused_gradient_operators(model: str, reference: Optional[Dict[str, int]], evidence_dir: Optional[Path]) -> dict:
    """The gradient operators an optimised configuration may leave out for `model`, as a
    multiset, with the reason when no exemption can be granted. The analysis is trusted only if
    it reproduces what the reference executes: the per-step gradient multiset it derives from
    the graph must equal the measured one."""
    out = {"exempt": {}, "note": "", "evidence": ""}
    try:
        text = step_graph_text(model)
        an = analyse_step_graph(text)
    except Exception as exc:
        out["note"] = f"no exemption: graph analysis failed ({str(exc).strip()[-200:]})"
        return out
    if evidence_dir is not None:
        mdir = evidence_dir / model
        mdir.mkdir(parents=True, exist_ok=True)
        (mdir / "macro.rgir.dot").write_text(text)
        (mdir / "unused_gradients.txt").write_text(evidence_text(model, an, reference))
        out["evidence"] = f"evidence/{model}/unused_gradients.txt"
    unused = [o for o in an["ops"] if o["unused"]]
    static = multiset_of(an["ops"])
    if not unused:
        out["note"] = "no gradient operator of the step is unused"
    elif static is None:
        names = sorted({o["op"] for o in unused})
        out["note"] = f"no exemption: unused operators found ({','.join(names)}) but their per-step count is decided at run time"
    elif reference is not None and static != reference:
        out["note"] = f"no exemption: the graph analysis counts {show(static)} per step, the reference executes {show(reference)}"
    elif an["liveness"].overflow:
        out["note"] = "no exemption: closure resolution hit its access-path limit"
    else:
        out["exempt"] = multiset_of(unused) or {}
        out["nodes"] = [an["graph"].describe(o["node"]) for o in unused]
        out["note"] = "unused by graph analysis: " + "; ".join(out["nodes"])
    return out


def optimised(config: str) -> bool:
    """A configuration is optimised when its pipeline has a pass besides std::macro and the VM."""
    return any(p not in ("std::macro", "std::nvm", "std::fvm") for p in train.CAMEL_CONFIGS[config])


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

    expected: Optional[Dict[str, int]] = None  # the reference multiset
    unused: dict = {"exempt": {}, "note": "", "evidence": ""}
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
                unused = unused_gradient_operators(model, expected, args.out / "evidence")
            reasons = []
            row["grad_ops_measured"] = show(measured)
            if expected is None:
                row["grad_ops_expected"] = "n/a"
                reasons.append(f"no expectation: the stateful {REFERENCE} reference did not run")
            else:
                # Optimised configurations may leave out what is proven unused; nothing else.
                exempt = unused["exempt"] if optimised(config) else {}
                required = {op: n - exempt.get(op, 0) for op, n in expected.items() if n > exempt.get(op, 0)}
                row["grad_ops_expected"] = show(required)
                row["grad_ops_reference"] = show(expected)
                row["grad_ops_proven_unused"] = show(unused["exempt"]) if unused["exempt"] else f"(none) {unused['note']}"
                row["unused_evidence"] = unused["evidence"]
                missing, extra = difference(required, measured), difference(measured, expected)
                row["grad_ops_missing"], row["grad_ops_extra"] = show(missing), show(extra)
                row["grad_ops_eliminated"] = show(difference(expected, measured))
                row["all_ops_equal_reference"] = "yes" if all_ops == reference_ops else "no"
                if not expected:
                    reasons.append("the reference executes no gradient operator")
                if missing:
                    reasons.append(
                        "gradient operators not executed although their results are used (or not proven unused): " + show(missing)
                    )
                if extra:
                    reasons.append(f"gradient operators beyond the reference: {show(extra)}")

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
