#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Compare two canonical JSON GIR documents for graph equivalence.

The definition of equivalence is the one of `docs/design/graph-store/plan.cn.md` section 2.2:
two programs are equivalent when there is a structure-preserving bijection between their
canonical documents -- graphs, live nodes, control regions, types (every named declaration an
atom), tag atoms -- that keeps node kinds, inputs by kind and target slot, operator identity,
payloads, attributes, region kind/parent/owner/index/ports/params/result and static values by
value.

The document itself is produced by `std::jgir(canonical=true)`
(`docs/technical/gir-interchange.md`, "Canonical documents"). This checker is independent of
the dump: it reads only the JSON. Names, origins and node numbers are not in the document, so
they cannot be compared -- that is the point of the canonical form, not a gap.

Usage:
    cml_roundtrip.py LEFT.json RIGHT.json [--max-nodes N] [--max-steps N] [--quiet]

Exit status:
    0  the documents are equivalent
    1  they are not (the first explainable difference is printed)
    2  over budget: the comparison stopped without an answer. It is never reported as equal.
"""

import argparse
import hashlib
import json
import sys
from collections import Counter, defaultdict

FORMAT = "camel.gir"
VERSION = 2
DEFAULT_MAX_NODES = 20000
DEFAULT_MAX_STEPS = 200000


class Budget(Exception):
    """The comparison hit its documented step or vertex budget."""


class Invalid(Exception):
    """A document is not a canonical GIR document at all."""


def stable_hash(parts):
    """A hash that does not change between runs, so diagnostics are reproducible."""
    text = json.dumps(parts, sort_keys=True, separators=(",", ":"))
    return hashlib.sha1(text.encode("utf-8")).hexdigest()[:16]


def canon(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


class Document:
    """A canonical document as a labeled directed multigraph.

    Vertices are graphs, nodes, regions (0 is the body vertex of its graph), types,
    declarations and tag atoms. Every field that names one of them becomes an edge, so the
    numbers a dump assigns take part only as vertices of the bijection, never as data: two
    equivalent programs whose dumps number their nodes or regions differently are equal.
    """

    def __init__(self, raw, name, max_nodes):
        if raw.get("format") != FORMAT:
            raise Invalid(f"{name}: not a {FORMAT} document")
        if raw.get("version") != VERSION or raw.get("canonical") is not True:
            raise Invalid(
                f"{name}: not a canonical document (version {VERSION}, canonical=true); "
                f"got version {raw.get('version')!r}, canonical {raw.get('canonical')!r}"
            )
        self.name = name
        self.V = {}
        self.pretty = {}
        self.out = defaultdict(list)
        self.inn = defaultdict(list)
        self._build(raw)
        for source, edges in self.out.items():
            if source not in self.V:
                raise Invalid(f"{name}: edge from unknown vertex {source}")
            for label, target in edges:
                if target not in self.V:
                    raise Invalid(f"{name}: {source} --{label}--> unknown vertex {target}")
        if len(self.V) > max_nodes:
            raise Budget(f"{name}: {len(self.V)} vertices is over the node budget of {max_nodes}")

    # -- construction --------------------------------------------------------------------

    def vertex(self, vid, label):
        if vid in self.V:
            raise Invalid(f"{self.name}: duplicate vertex {vid}")
        self.V[vid] = label

    def edge(self, src, dst, label):
        self.out[src].append((label, dst))
        self.inn[dst].append((label, src))

    def _build(self, raw):
        graphs = raw.get("graphs")
        if not isinstance(graphs, list):
            raise Invalid(f"{self.name}: no graphs")
        self.entry = raw.get("entry")
        self.vertex("NULL", {"k": "dangling"})
        for index, entry in enumerate(raw.get("types") or []):
            self._type(index, entry)
        for entry in raw.get("decls") or []:
            self.vertex(f"D:{entry['id']}", self._decl_label(entry))
            self.pretty[f"D:{entry['id']}"] = entry
        for tag in raw.get("tags") or []:
            self.vertex(f"A:{tag}", {"k": "tag"})
        for graph in graphs:
            self._graph(graph)

    def _decl_label(self, entry):
        label = {"k": "decl", "decl_kind": entry["kind"], "params": entry.get("params", 0)}
        variants = entry.get("variants")
        if variants is not None:
            label["variants"] = [
                [variant.get("name"), variant.get("shape"), list(variant.get("fields", []))]
                for variant in variants
            ]
        return label

    def _type(self, index, entry):
        vid = f"T:{index}"
        label = {"k": "type", "type_kind": entry.get("k")}
        for key in ("tag", "mods", "rest", "code", "spelling"):
            if key in entry:
                label[key] = entry[key]
        if "fields" in entry:
            label["fieldnames"] = [field["name"] for field in entry["fields"]]
        if "variants" in entry:
            label["variant_lengths"] = [len(variant) for variant in entry["variants"]]
        if "elems" in entry:
            label["elems_len"] = len(entry["elems"])
        if "with" in entry:
            label["with_var"] = [item["var"] for item in entry["with"]]
        if "norm" in entry:
            label["norm_var"] = [item["var"] for item in entry["norm"]]
        self.V[vid] = label
        self.pretty[vid] = entry
        for key in ("elem", "key", "value", "exit", "decl"):
            if entry.get(key) is not None:
                self.edge(vid, f"T:{entry[key]}", key)
        for key, field in (("args", "arg"), ("elems", "elem"), ("variants", "variant")):
            for i, ref in enumerate(entry.get(key) or []):
                if key == "variants":
                    for j, item in enumerate(ref):
                        self.edge(vid, f"T:{item}", f"{field}:{i}:{j}")
                else:
                    self.edge(vid, f"T:{ref}", f"{field}:{i}")
        if "fields" in entry:
            for i, field in enumerate(entry["fields"]):
                self.edge(vid, f"T:{field['type']}", f"field:{i}")
        for key, field in (("with", "with"), ("norm", "norm")):
            for i, item in enumerate(entry.get(key) or []):
                self.edge(vid, f"T:{item['type']}", f"{field}:{i}")

    def _attrs(self, owner, attrs):
        for tag, value in attrs.items():
            atom = f"A:{tag}"
            if atom not in self.V:
                raise Invalid(f"{self.name}: attribute on {owner} names unknown tag {tag}")
            if isinstance(value, dict) and "graph" in value:
                target = "NULL" if value["graph"] is None else f"G:{value['graph']}"
                self.edge(owner, target, "attr:graph")
            else:
                self.edge(owner, atom, "attr:" + canon(value))

    def _node_ref(self, gid, node):
        return "NULL" if node is None else f"N:{gid}:{node}"

    def _region_ref(self, gid, region):
        return f"B:{gid}" if not region else f"R:{gid}:{region}"

    def _graph(self, graph):
        gid = graph["id"]
        g = f"G:{gid}"
        self.vertex(
            g,
            {
                "k": "graph",
                "flags": graph.get("flags", 0),
                "root": bool(graph.get("root")),
                "entry": self.entry == gid,
            },
        )
        self.pretty[g] = {key: value for key, value in graph.items() if key != "nodes"}
        body = f"B:{gid}"
        self.vertex(body, {"k": "body"})
        self.edge(g, body, "body")
        self.edge(g, f"T:{graph['signature']}", "signature")
        for field in ("normalPorts", "contextPorts", "capturePorts"):
            for i, port in enumerate(graph.get(field) or []):
                self.edge(g, self._node_ref(gid, port), f"port:{field}:{i}")
        self.edge(g, self._node_ref(gid, graph.get("exit")), "exit")
        for node in graph.get("nodes") or []:
            self._node(gid, node)
        for region in graph.get("regions") or []:
            self._region(gid, region)

    def _node(self, gid, node):
        vid = f"N:{gid}:{node['id']}"
        self.vertex(vid, {"k": "node", "node_kind": node["kind"], "perms": node.get("perms", 0)})
        self.pretty[vid] = node
        self.edge(f"G:{gid}", vid, "node")
        self.edge(vid, f"T:{node['type']}", "type")
        self.edge(vid, self._region_ref(gid, node.get("region")), "region")
        for item in node.get("inputs") or []:
            self.edge(
                vid,
                self._node_ref(gid, item.get("source")),
                f"in:{item.get('kind')}:{item.get('slot')}",
            )
        if "param" in node:
            self.V[vid]["param"] = [node["param"].get("kind"), node["param"].get("index")]
        if "attrs" in node:
            self._attrs(vid, node["attrs"])
        if "payload" in node:
            for key, value in node["payload"].items():
                if key == "pair":
                    self.edge(vid, self._node_ref(gid, value), "pair")
                else:
                    self.V[vid][key] = value
        if "callee" in node:
            target = "NULL" if node["callee"] is None else f"G:{node['callee']}"
            self.edge(vid, target, "callee")
        if "loop" in node:
            loop = node["loop"]
            self.V[vid]["loop"] = [loop.get("kind"), loop.get("collectionCount", 0)]
            for key in ("condition", "body"):
                target = "NULL" if loop.get(key) is None else f"G:{loop[key]}"
                self.edge(vid, target, f"loop_{key}")
            for key in ("conditionRegion", "bodyRegion"):
                if loop.get(key):
                    self.edge(vid, self._region_ref(gid, loop[key]), f"loop_{key}")
        if "value" in node:
            self._value(vid, node["value"], "value")

    def _value(self, vid, value, base):
        """A static value as a label, with the graphs it names as edges (they are compared)."""
        refs = []

        def encode(item):
            if isinstance(item, dict):
                if item.get("k") == "func" and "graph" in item:
                    refs.append(item["graph"])
                    return {"k": "func", "graph": "@"}
                return {key: encode(inner) for key, inner in item.items()}
            if isinstance(item, list):
                return [encode(inner) for inner in item]
            return item

        self.V[vid][base] = canon(encode(value))
        for i, ref in enumerate(refs):
            target = "NULL" if ref is None else f"G:{ref}"
            self.edge(vid, target, f"{base}:graph:{i}")

    def _region(self, gid, region):
        vid = f"R:{gid}:{region['id']}"
        self.vertex(
            vid,
            {"k": "region", "region_kind": region["kind"], "index": region.get("index", 0)},
        )
        self.pretty[vid] = region
        self.edge(f"G:{gid}", vid, "region")
        self.edge(vid, self._region_ref(gid, region.get("parent")), "parent")
        if region.get("owner") is not None:
            self.edge(vid, self._node_ref(gid, region["owner"]), "owner")
        if region.get("result") is not None:
            self.edge(vid, self._node_ref(gid, region["result"]), "result")
        for i, port in enumerate(region.get("ports") or []):
            self.edge(vid, self._node_ref(gid, port), f"region_port:{i}")
        params = region.get("params")
        if params is not None:
            for i, ref in enumerate(params.get("with") or []):
                self.edge(vid, f"T:{ref}", f"param_with:{i}")
            for i, ref in enumerate(params.get("norm") or []):
                self.edge(vid, f"T:{ref}", f"param_norm:{i}")
        if "attrs" in region:
            self._attrs(vid, region["attrs"])


# -- the matcher ---------------------------------------------------------------------------


def refine(docs):
    """1-WL color refinement: a vertex's color is its label and its labeled neighbors' colors."""
    colors = []
    for doc in docs:
        colors.append({vid: stable_hash(["0", label]) for vid, label in doc.V.items()})
    for _ in range(64):
        fresh = []
        stable = True
        for doc, color in zip(docs, colors):
            new = {}
            for vid in doc.V:
                around = sorted([("out", label, color[other]) for label, other in doc.out[vid]])
                into = sorted([("in", label, color[other]) for label, other in doc.inn[vid]])
                new[vid] = stable_hash([color[vid], around, into])
            fresh.append(new)
            stable = stable and new == color
        colors = fresh
        if stable:
            break
    return colors


def describe(doc, vid):
    pretty = doc.pretty.get(vid)
    if pretty is None:
        return f"{vid} {canon(doc.V[vid])}"
    text = json.dumps(pretty, sort_keys=True)
    if len(text) > 300:
        text = text[:297] + "..."
    return f"{vid}: {text}"


def class_difference(left, right, lcolors, rcolors):
    """The first color class whose multiplicities differ, with a representative of each side."""

    def classes(colors, doc):
        out = defaultdict(list)
        for vid in doc.V:
            out[colors[vid]].append(vid)
        return out

    lc = classes(lcolors, left)
    rc = classes(rcolors, right)
    best = None
    for color in sorted(set(lc) | set(rc)):
        mine = lc.get(color, [])
        other = rc.get(color, [])
        if len(mine) == len(other):
            continue
        kinds = set()
        for vid in mine:
            kinds.add(left.V[vid]["k"])
        for vid in other:
            kinds.add(right.V[vid]["k"])
        score = (0 if kinds & {"node", "region", "type"} else 1, -len(mine), color)
        if best is None or score < best[0]:
            best = (score, color, mine, other)
    if best is None:
        return None
    _, color, mine, other = best
    lines = [
        f"a structural class has {len(mine)} vertex/vertices on the left and "
        f"{len(other)} on the right"
    ]
    if mine:
        lines.append("  left:  " + describe(left, mine[0]))
    if other:
        lines.append("  right: " + describe(right, other[0]))
    if mine and other:
        lines.append("  left neighbors:  " + neighbor_text(left, mine[0]))
        lines.append("  right neighbors: " + neighbor_text(right, other[0]))
    return "\n".join(lines)


def neighbor_text(doc, vid):
    around = sorted(f"{label}->{target}" for label, target in doc.out[vid])
    into = sorted(f"{label}<-{source}" for label, source in doc.inn[vid])
    text = ", ".join(around + into)
    return text if len(text) <= 300 else text[:297] + "..."


def equivalent(left, right, max_steps):
    lcolors, rcolors = refine([left, right])
    if Counter(lcolors.values()) != Counter(rcolors.values()):
        return False, class_difference(left, right, lcolors, rcolors)

    by_color = defaultdict(list)
    for vid in right.V:
        by_color[rcolors[vid]].append(vid)
    candidates = {vid: by_color[lcolors[vid]] for vid in left.V}
    order = sorted(left.V, key=lambda vid: (len(candidates[vid]), -len(left.out[vid])))

    lout = {vid: Counter((label, lcolors[other]) for label, other in left.out[vid]) for vid in left.V}
    lin = {vid: Counter((label, lcolors[other]) for label, other in left.inn[vid]) for vid in left.V}
    rout = {vid: Counter((label, rcolors[other]) for label, other in right.out[vid]) for vid in right.V}
    rin = {vid: Counter((label, rcolors[other]) for label, other in right.inn[vid]) for vid in right.V}

    assigned = {}
    used = set()
    steps = [0]
    failure = {"vid": None, "depth": -1}

    def consistent(u, v):
        for label, other in left.out[u]:
            image = assigned.get(other)
            if image is not None and (label, image) not in Counter(right.out[v]):
                return False
        for label, other in left.inn[u]:
            image = assigned.get(other)
            if image is not None and (label, image) not in Counter(right.inn[v]):
                return False
        return lout[u] == rout[v] and lin[u] == rin[v]

    def verify():
        for u, v in assigned.items():
            if Counter(left.out[u]) != Counter((label, assigned[o]) for label, o in right.out[v]):
                return False
            if Counter(left.inn[u]) != Counter((label, assigned[o]) for label, o in right.inn[v]):
                return False
        return True

    def search(i):
        if i == len(order):
            return verify()
        u = order[i]
        for v in candidates[u]:
            if v in used:
                continue
            steps[0] += 1
            if steps[0] > max_steps:
                raise Budget(
                    f"comparison stopped after {max_steps} steps without an answer "
                    f"(left {len(left.V)} vertices, right {len(right.V)})"
                )
            if not consistent(u, v):
                continue
            assigned[u] = v
            used.add(v)
            if search(i + 1):
                return True
            del assigned[u]
            used.discard(v)
        if i > failure["depth"]:
            failure.update(vid=u, depth=i)
        return False

    if search(0):
        return True, None
    if failure["vid"] is not None:
        return False, (
            "no structure-preserving bijection exists; the first vertex that could not be "
            "paired is\n  " + describe(left, failure["vid"]) + "\n  its neighbors: "
            + neighbor_text(left, failure["vid"])
        )
    return False, "no structure-preserving bijection exists"


# -- entry point ---------------------------------------------------------------------------


def load(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as error:
        raise Invalid(f"{path}: {error.strerror}") from error
    except json.JSONDecodeError as error:
        raise Invalid(f"{path}: not JSON ({error})") from error


def main(argv):
    parser = argparse.ArgumentParser(description="compare two canonical JSON GIR documents")
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--max-nodes", type=int, default=DEFAULT_MAX_NODES)
    parser.add_argument("--max-steps", type=int, default=DEFAULT_MAX_STEPS)
    parser.add_argument("--quiet", action="store_true", help="print the verdict line only")
    args = parser.parse_args(argv)

    try:
        left = Document(load(args.left), args.left, args.max_nodes)
        right = Document(load(args.right), args.right, args.max_nodes)
    except Invalid as error:
        print(f"DIFFERENT: {error}")
        return 1
    except Budget as error:
        print(f"BUDGET: {error}")
        return 2

    try:
        same, detail = equivalent(left, right, args.max_steps)
    except Budget as error:
        print(f"BUDGET: {error}")
        return 2

    if same:
        print(
            "EQUAL: the two canonical documents are equivalent "
            f"({len(left.V)} vertices, {sum(len(v) for v in left.out.values())} edges)"
        )
        return 0
    print("DIFFERENT: the two canonical documents are not equivalent")
    if detail and not args.quiet:
        print(detail)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))