import numpy as np, json
from scipy import ndimage
from PIL import Image, ImageDraw
S = 2.0
sk = np.load("sk.npy"); dist = np.load("dist.npy"); m = np.load("mask.npy")
K = np.ones((3, 3)); K[1, 1] = 0
def degree(s): return ndimage.convolve(s.astype(int), K, mode="constant") * s
NODE_R = 7.0   # frame units: junction pixels this close belong to one node
def build(sk):
    deg = degree(sk)
    junc = (deg >= 3)
    jl, nj = ndimage.label(ndimage.binary_dilation(junc, iterations=int(NODE_R * S)) & sk, structure=np.ones((3, 3)))
    nodes = []
    for i in range(1, nj + 1):
        ys, xs = np.nonzero(jl == i)
        k = np.argmax(dist[ys, xs]); nodes.append(dict(x=xs[k] / S, y=ys[k] / S, r=float(dist[ys, xs].max()), px=(ys, xs)))
    seg = sk & (jl == 0)
    sl, ns = ndimage.label(seg, structure=np.ones((3, 3)))
    edges = []
    for i in range(1, ns + 1):
        comp = (sl == i)
        ys, xs = np.nonzero(comp)
        cd = degree(comp)
        ends = [(y, x) for y, x in zip(ys, xs) if cd[y, x] <= 1]
        if len(ys) == 1: ends = [(ys[0], xs[0])] * 2
        if len(ends) < 2:   # a closed loop with no junction (an island ring)
            ends = [(ys[0], xs[0]), (ys[0], xs[0])]
        # walk from the first end
        pts = [ends[0]]; seen = {ends[0]}; cur = ends[0]
        while True:
            nb = [(cur[0] + dy, cur[1] + dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy or dx)]
            nb = [p for p in nb if 0 <= p[0] < comp.shape[0] and 0 <= p[1] < comp.shape[1] and comp[p] and p not in seen]
            if not nb: break
            nb.sort(key=lambda p: abs(p[0] - cur[0]) + abs(p[1] - cur[1])); cur = nb[0]; seen.add(cur); pts.append(cur)
        def touch(p):
            y, x = p
            win = jl[max(y - 2, 0):y + 3, max(x - 2, 0):x + 3]; v = win[win > 0]
            return int(v[0]) - 1 if len(v) else None
        a, b = touch(pts[0]), touch(pts[-1])
        poly = [(x / S, y / S) for y, x in pts]
        if a is not None: poly = [(nodes[a]["x"], nodes[a]["y"])] + poly
        if b is not None: poly = poly + [(nodes[b]["x"], nodes[b]["y"])]
        L = sum(np.hypot(poly[j + 1][0] - poly[j][0], poly[j + 1][1] - poly[j][1]) for j in range(len(poly) - 1))
        edges.append(dict(a=a, b=b, poly=poly, length=L, comp=(ys, xs)))
    return nodes, edges
SPUR = 26.0
for it in range(6):
    nodes, edges = build(sk)
    spurs = [e for e in edges if (e["a"] is None) != (e["b"] is None) and e["length"] < SPUR]
    print("iteration", it, "nodes", len(nodes), "edges", len(edges), "spurs", len(spurs))
    if not spurs: break
    for e in spurs: sk[e["comp"]] = False
    from skimage.morphology import skeletonize
    sk = skeletonize(sk)
free = [e for e in edges if e["a"] is None or e["b"] is None]
print("edges with a free end:", len(free))
deg = {}
for e in edges:
    for k in (e["a"], e["b"]):
        if k is not None: deg[k] = deg.get(k, 0) + 1
out = dict(nodes=[dict(x=n["x"], y=n["y"], r=n["r"], deg=deg.get(i, 0)) for i, n in enumerate(nodes)],
           edges=[dict(a=e["a"], b=e["b"], poly=e["poly"], length=e["length"]) for e in edges])
json.dump(out, open("graph.json", "w"))
# picture: mask grey, edges coloured, nodes red with ids
img = Image.new("RGB", (2048, 2048), "white"); px = np.array(img); px[m] = (215, 215, 215); img = Image.fromarray(px)
d = ImageDraw.Draw(img)
rng = np.random.default_rng(3)
for i, e in enumerate(edges):
    col = tuple(int(c) for c in rng.integers(30, 200, 3))
    d.line([(x * S, y * S) for x, y in e["poly"]], fill=col, width=5)
    mx, my = e["poly"][len(e["poly"]) // 2]; d.text((mx * S + 4, my * S), "e%d" % i, fill=(0, 0, 0))
for i, n in enumerate(nodes):
    d.ellipse([n["x"] * S - 7, n["y"] * S - 7, n["x"] * S + 7, n["y"] * S + 7], fill=(220, 0, 0)); d.text((n["x"] * S + 8, n["y"] * S - 16), "n%d" % i, fill=(200, 0, 0))
img.save("graph.png")
