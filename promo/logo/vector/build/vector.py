# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Rebuild the traced Camel mark as an animatable line drawing: one path per graph edge,
a circle per junction blob, the ring as stroked arcs, the eye on its own."""
import json, re
import numpy as np

f = lambda v: ("%.1f" % v).rstrip("0").rstrip(".")
SW = 24.0                         # the one stroke width (1024 frame)
NODE_R = (18.0, 21.0)             # node circle radii: ordinary and large blobs
S = 2.0
g = json.load(open("graph.json"))
mask = np.load("mask.npy")
nodes = [dict(n) for n in g["nodes"]]
edges = [dict(e) for e in g["edges"]]

# ---- ring geometry (from the trace) ----
geo = json.load(open("/work/promo/logo/geo.json"))
E2 = np.load("/work/promo/logo/ellipse_right.npy")
E2[4] %= 2 * np.pi
tc = json.load(open("/work/promo/logo/twocircles.json"))["top"]


def ell_pt(E, t):
    cx, cy, a, b, ph = E
    c, s = np.cos(ph), np.sin(ph)
    return np.array([cx + c * a * np.cos(t) - s * b * np.sin(t), cy + s * a * np.cos(t) + c * b * np.sin(t)])


TS = np.linspace(0, 2 * np.pi, 4000)
E2_PTS = np.array([ell_pt(E2, t) for t in TS])
RING_PTS = np.vstack([E2_PTS, np.array([[tc[0] + tc[2] * np.cos(t), tc[1] + tc[2] * np.sin(t)] for t in TS])])


def near_ring(p, d=20):
    return np.min(np.hypot(*(RING_PTS - p).T)) < d


# ---- 1. drop what the trace left of the ring, the pupil skeleton, and dust ----
kept = []
for e in edges:
    P = np.array(e["poly"])
    free = e["a"] is None or e["b"] is None
    if e["length"] < 2:
        continue
    if e["a"] is None and e["b"] is None:
        continue
    if free and np.mean([near_ring(p) for p in P[::4]]) > 0.7:
        continue
    kept.append(e)
edges = kept


# ---- 2. merge junctions joined by a very short edge ----
def merge_short(edges, L=20):
    while True:
        short = [e for e in edges if e["a"] is not None and e["b"] is not None and e["a"] != e["b"] and e["length"] < L]
        if not short:
            return edges
        e = short[0]
        a, b = e["a"], e["b"]
        na, nb = nodes[a], nodes[b]
        w = (na["r"], nb["r"])
        nodes[a] = dict(x=(na["x"] * w[0] + nb["x"] * w[1]) / sum(w), y=(na["y"] * w[0] + nb["y"] * w[1]) / sum(w), r=max(w))
        edges = [x for x in edges if x is not e]
        for x in edges:
            if x["a"] == b:
                x["a"] = a
            if x["b"] == b:
                x["b"] = a
        nodes[b] = None


edges = merge_short(edges)


# ---- 3. dissolve junctions of degree 2 that show no blob ----
def degree():
    d = {}
    for e in edges:
        for k in (e["a"], e["b"]):
            if k is not None:
                d[k] = d.get(k, 0) + 1
    return d


def from_node(e, k):
    return e["poly"] if e["a"] == k else e["poly"][::-1]


changed = True
while changed:
    changed = False
    d = degree()
    for k, n in enumerate(nodes):
        if n is None or d.get(k) != 2 or n["r"] >= 18:
            continue
        inc = [e for e in edges if k in (e["a"], e["b"])]
        if len(inc) != 2 or inc[0] is inc[1]:
            continue
        e1, e2 = inc
        p1 = from_node(e1, k)[::-1]
        p2 = from_node(e2, k)
        o1 = e1["b"] if e1["a"] == k else e1["a"]
        o2 = e2["b"] if e2["a"] == k else e2["a"]
        new = dict(a=o1, b=o2, poly=p1 + p2[1:], length=e1["length"] + e2["length"])
        edges = [e for e in edges if e is not e1 and e is not e2] + [new]
        nodes[k] = None
        changed = True
        break

for e in edges:
    P = [list(p) for p in e["poly"]]
    if e["a"] is not None:
        P[0] = [nodes[e["a"]]["x"], nodes[e["a"]]["y"]]
    if e["b"] is not None:
        P[-1] = [nodes[e["b"]]["x"], nodes[e["b"]]["y"]]
    e["poly"] = P

# ---- 4. the ear: the spur pruning removed at the back of the head ----
head_top = min((k for k, n in enumerate(nodes) if n and n["x"] > 700 and n["y"] < 230), key=lambda k: nodes[k]["y"])
n0 = nodes[head_top]
ys, xs = np.nonzero(mask)
X, Y = xs / S, ys / S
sel = (X < n0["x"] - 4) & (Y < n0["y"] - 4) & (np.hypot(X - n0["x"], Y - n0["y"]) < 50)
dd = np.hypot(X[sel] - n0["x"], Y[sel] - n0["y"])
far = np.array([X[sel][dd.argmax()], Y[sel][dd.argmax()]])
u = far - [n0["x"], n0["y"]]
u /= np.linalg.norm(u)
tip = far - u * SW / 2
edges.append(dict(a=head_top, b=None, poly=[[n0["x"], n0["y"]], tip.tolist()], length=float(np.hypot(*(tip - [n0["x"], n0["y"]])))))
print("ear tip", np.round(tip, 1))


# ---- 5. fit each edge: a line where it is straight, else as few cubics as fit ----
def smooth(P, k=4):
    P = np.array(P, float)
    if len(P) < 2 * k + 2:
        return P
    Q = P.copy()
    for i in range(1, len(P) - 1):
        lo, hi = max(0, i - k), min(len(P), i + k + 1)
        Q[i] = P[lo:hi].mean(0)
    return Q


def bez(c, t):
    t = t[:, None]
    return (1 - t) ** 3 * c[0] + 3 * (1 - t) ** 2 * t * c[1] + 3 * (1 - t) * t ** 2 * c[2] + t ** 3 * c[3]


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else v


def fit_cubic(P, t1, t2):
    u = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]
    u /= u[-1]
    c = None
    for _ in range(6):
        A1 = (3 * (1 - u) ** 2 * u)[:, None] * t1
        A2 = (3 * (1 - u) * u ** 2)[:, None] * t2
        Cm = np.array([[np.sum(A1 * A1), np.sum(A1 * A2)], [np.sum(A1 * A2), np.sum(A2 * A2)]])
        base = ((1 - u) ** 3 + 3 * (1 - u) ** 2 * u)[:, None] * P[0] + (3 * (1 - u) * u ** 2 + u ** 3)[:, None] * P[-1]
        Xv = np.array([np.sum(A1 * (P - base)), np.sum(A2 * (P - base))])
        try:
            a1, a2 = np.linalg.solve(Cm, Xv)
        except np.linalg.LinAlgError:
            a1 = a2 = -1
        seg = np.linalg.norm(P[-1] - P[0])
        if a1 < 1e-3 * seg or a2 < 1e-3 * seg:
            a1 = a2 = seg / 3
        c = np.array([P[0], P[0] + a1 * t1, P[-1] + a2 * t2, P[-1]])
        q = bez(c, u)
        d1 = 3 * ((1 - u) ** 2)[:, None] * (c[1] - c[0]) + 6 * ((1 - u) * u)[:, None] * (c[2] - c[1]) + 3 * (u ** 2)[:, None] * (c[3] - c[2])
        u = np.clip(u - np.sum((q - P) * d1, 1) / np.maximum(np.sum(d1 * d1, 1), 1e-9), 0, 1)
        u[0], u[-1] = 0, 1
        u = np.maximum.accumulate(u)
    err = np.linalg.norm(bez(c, u) - P, axis=1)
    return c, err.max(), int(err.argmax())


def fit(P, t1, t2, tol, depth=0):
    c, e, i = fit_cubic(P, t1, t2)
    if e <= tol or depth > 6 or len(P) < 8:
        return [c]
    i = min(max(i, 3), len(P) - 4)
    tm = unit(P[i - 2] - P[i + 2])
    return fit(P[:i + 1], t1, tm, tol, depth + 1) + fit(P[i:], -tm, t2, tol, depth + 1)


TOL = 6.0


def free_cubic(P):
    """Least-squares cubic with fixed ends and free handles, with reparameterisation."""
    u = np.r_[0, np.cumsum(np.linalg.norm(np.diff(P, axis=0), axis=1))]
    u /= u[-1]
    for _ in range(6):
        b1 = 3 * (1 - u) ** 2 * u
        b2 = 3 * (1 - u) * u ** 2
        R = P - ((1 - u) ** 3)[:, None] * P[0] - (u ** 3)[:, None] * P[-1]
        A = np.c_[b1, b2]
        sol, *_ = np.linalg.lstsq(A, R, rcond=None)
        c = np.array([P[0], sol[0], sol[1], P[-1]])
        q = bez(c, u)
        d1 = 3 * ((1 - u) ** 2)[:, None] * (c[1] - c[0]) + 6 * ((1 - u) * u)[:, None] * (c[2] - c[1]) + 3 * (u ** 2)[:, None] * (c[3] - c[2])
        u = np.clip(u - np.sum((q - P) * d1, 1) / np.maximum(np.sum(d1 * d1, 1), 1e-9), 0, 1)
        u[0], u[-1] = 0, 1
        u = np.maximum.accumulate(u)
    err = np.linalg.norm(bez(c, u) - P, axis=1)
    return c, err.max(), int(err.argmax())


def piece(P, depth=0):
    """A list of ('L', end) or ('C', c1, c2, end) segments for one corner-free run."""
    ch = P[-1] - P[0]
    L = np.linalg.norm(ch)
    dev = np.max(np.abs(ch[0] * (P[:, 1] - P[0][1]) - ch[1] * (P[:, 0] - P[0][0]))) / max(L, 1e-9)
    if dev < max(8.0, 0.08 * L) or len(P) < 6:
        return [("L", P[-1])]
    c, e, i = free_cubic(P)
    if e <= TOL or depth >= 3:
        return [("C", c[1], c[2], c[3])]
    i = min(max(i, 3), len(P) - 4)
    return piece(P[:i + 1], depth + 1) + piece(P[i:], depth + 1)


def corners(P, w=16, ang=25):
    idx = []
    for i in range(w, len(P) - w):
        a, b = unit(P[i] - P[i - w]), unit(P[i + w] - P[i])
        t = np.degrees(np.arccos(np.clip(a @ b, -1, 1)))
        if t > ang:
            idx.append((i, t))
    keep = []
    for i, t in sorted(idx, key=lambda x: -x[1]):
        if all(abs(i - j) > 2 * w for j in keep):
            keep.append(i)
    return sorted(keep)


def edge_d(poly, ra=None, rb=None, ang=25):
    P = smooth(poly)
    P[0], P[-1] = poly[0], poly[-1]
    # drop the hook the skeleton makes inside a junction blob, keep the junction centre itself
    keep = np.ones(len(P), bool)
    if ra:
        keep[1:-1] &= np.hypot(*(P[1:-1] - P[0]).T) > ra + 6
    if rb:
        keep[1:-1] &= np.hypot(*(P[1:-1] - P[-1]).T) > rb + 6
    if keep.sum() >= 4:
        P = P[keep]
    cut = [0] + corners(P, ang=ang) + [len(P) - 1]
    segs = []
    for a_, b_ in zip(cut[:-1], cut[1:]):
        segs += piece(P[a_:b_ + 1])
    d = "M%s %s" % (f(P[0][0]), f(P[0][1]))
    for sg in segs:
        if sg[0] == "L":
            d += "L%s %s" % (f(sg[1][0]), f(sg[1][1]))
        else:
            d += "C" + " ".join(f(v) for v in np.r_[sg[1], sg[2], sg[3]])
    return d, all(sg[0] == "L" for sg in segs) and len(segs) == 1


BODY_C = np.array([390.0, 420.0])


def group(c):
    x, y = c
    if x >= 690 and y <= 300:
        return "head"
    if x >= 600 and y < 560:
        return "neck"
    if y >= 545:
        return "legs"
    return "body"


ORIGIN = dict(body=BODY_C, legs=np.array([400.0, 500.0]), neck=np.array([600.0, 470.0]), head=np.array([740.0, 260.0]))
for e in edges:
    e["centroid"] = np.mean(e["poly"], axis=0)
    e["group"] = group(e["centroid"])
    o = ORIGIN[e["group"]]
    if np.linalg.norm(np.array(e["poly"][-1]) - o) < np.linalg.norm(np.array(e["poly"][0]) - o):
        e["poly"] = e["poly"][::-1]
        e["a"], e["b"] = e["b"], e["a"]
    ra = nodes[e["a"]]["r"] if e["a"] is not None else None
    rb = nodes[e["b"]]["r"] if e["b"] is not None else None
    e["d"], straight = edge_d(e["poly"], ra, rb, ang=50 if e["group"] in ("head", "neck") else 25)
    e["kind"] = "line" if straight else "path"

# ---- 6. drawing order: body cells outward, legs top down, neck from the body, head ----
order = []
for gname in ("body", "legs", "neck", "head"):
    o = ORIGIN[gname]
    key = (lambda e: e["centroid"][1]) if gname == "legs" else (lambda e, o=o: np.linalg.norm(e["centroid"] - o))
    order += sorted([e for e in edges if e["group"] == gname], key=key)

# ---- 7. nodes with a visible blob ----
used = sorted(set(k for e in edges for k in (e["a"], e["b"]) if k is not None))
nodes_out = [dict(id=k, x=nodes[k]["x"], y=nodes[k]["y"], r=NODE_R[1] if nodes[k]["r"] >= 22.5 else NODE_R[0]) for k in used if nodes[k]["r"] >= 18]

# ---- 8. the eye: the pupil shape from the trace ----
tbody = json.load(open("/work/promo/logo/parts_mark.json"))["body"]
eye = ""
for sp in re.findall(r"M[^M]+", tbody):
    nums = [float(v) for v in re.findall(r"-?\d+\.?\d*", sp)]
    xs_, ys_ = nums[0::2], nums[1::2]
    if 770 < min(xs_) and max(xs_) < 850 and 205 < min(ys_) and max(ys_) < 255:
        eye = sp

# ---- 9. ring: extend the right stroke so that it meets the rear leg ----
leg_ends = [np.array(p) for e in order if e["group"] == "legs" for p in (e["poly"][0], e["poly"][-1])]
leg_end = min(leg_ends, key=lambda p: p[0] - p[1])   # the lowest-left leg end: where the rear leg meets the ring


def t_of(p):
    return TS[np.argmin(np.hypot(*(E2_PTS - p).T))]


m = re.match(r"M([\d.]+) ([\d.]+)A", geo["ring2"])
pb = np.array([float(m.group(1)), float(m.group(2))])
t_b, t_e = t_of(pb), t_of(leg_end)
pe = ell_pt(E2, t_e)
sweep = (t_e - t_b) % (2 * np.pi)
ring2 = "M%s %sA%s %s %s %d 1 %s %s" % (f(pb[0]), f(pb[1]), f(E2[2]), f(E2[3]), f(np.degrees(E2[4])), int(sweep > np.pi), f(pe[0]), f(pe[1]))

out = dict(edges=[dict(id="%s-%02d" % (e["group"], i), group=e["group"], d=e["d"], kind=e["kind"], a=e["a"], b=e["b"]) for i, e in enumerate(order)],
           nodes=nodes_out, eye=eye, ring=[geo["ring1"], ring2], drop=dict(cx=133.7, cy=geo["drop"]["cy"], r=NODE_R[1]), sw=SW)
json.dump(out, open("vector.json", "w"), indent=1)
from collections import Counter
print("edges", len(order), dict(Counter(e["group"] for e in order)), "lines", sum(e["kind"] == "line" for e in order),
      "nodes", len(nodes_out), "eye", bool(eye), "leg end", np.round(leg_end, 1))
