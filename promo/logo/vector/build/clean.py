# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Cleanup of the vector mark: one node per leg joint, no bumps on the back and at the base of the neck."""
import json, re
v = json.load(open("vector_orig.json"))
f = lambda x: ("%.1f" % x).rstrip("0").rstrip(".")
nodes = {n["id"]: n for n in v["nodes"]}
pos = {n["id"]: (n["x"], n["y"]) for n in v["nodes"]}
# endpoints of every edge, including nodes without a circle
for e in v["edges"]:
    nums = [float(x) for x in re.findall(r"-?\d+\.?\d*", e["d"])]
    if e["a"] is not None: pos.setdefault(e["a"], (nums[0], nums[1]))
    if e["b"] is not None: pos.setdefault(e["b"], (nums[-2], nums[-1]))
def snap(d, old, new):
    """Moves an end of the path that sits at `old` to `new`."""
    toks = re.findall(r"[MLCA]|-?\d+\.?\d*", d)
    nums = [i for i, t in enumerate(toks) if not t.isalpha()]
    first, last = nums[:2], nums[-2:]
    for pair in (first, last):
        x, y = float(toks[pair[0]]), float(toks[pair[1]])
        if abs(x - old[0]) < 1.5 and abs(y - old[1]) < 1.5:
            toks[pair[0]], toks[pair[1]] = f(new[0]), f(new[1])
    out = ""
    for t in toks:
        out += t if t.isalpha() else ("" if out and out[-1].isalpha() else " ") + t
    return out
def merge(keep, gone, short_edge):
    a, b = pos[keep], pos[gone]
    mid = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
    v["edges"] = [e for e in v["edges"] if e["id"] != short_edge]
    for e in v["edges"]:
        for k in (keep, gone):
            if e["a"] == k or e["b"] == k:
                e["d"] = snap(e["d"], pos[k], mid)
        if e["a"] == gone: e["a"] = keep
        if e["b"] == gone: e["b"] = keep
    pos[keep] = mid
    r = max(nodes[keep]["r"] if keep in nodes else 0, nodes[gone]["r"] if gone in nodes else 0)
    nodes.pop(gone, None)
    nodes[keep] = dict(id=keep, x=mid[0], y=mid[1], r=r)
# the front legs: one node where the belly meets each leg
merge(33, 32, "legs-30")
merge(34, 30, "legs-28")
# the bumps: the dot on the back and the one at the base of the neck, and the hook of the chest line
nodes.pop(9, None)
nodes.pop(27, None)
for e in v["edges"]:
    if e["id"] == "body-23":
        nums = [float(x) for x in re.findall(r"-?\d+\.?\d*", e["d"])]
        e["d"] = "M%s %sL%s %s" % (f(nums[0]), f(nums[1]), f(nums[-2]), f(nums[-1]))
v["nodes"] = sorted(nodes.values(), key=lambda n: n["id"])
json.dump(v, open("vector.json", "w"), indent=1)
for e in v["edges"]:
    if e["id"] in ("body-16", "legs-27", "legs-31", "legs-34", "body-23", "neck-50", "legs-32", "legs-35"): print(e["id"], e["a"], e["b"], e["d"][:70])
print(len(v["edges"]), "edges", len(v["nodes"]), "nodes")
