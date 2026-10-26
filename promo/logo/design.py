# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

import math
from shapely.geometry import Polygon, LineString, Point
from shapely.ops import unary_union
f = lambda v: ("%.2f" % v).rstrip("0").rstrip(".")
W = 2.4                     # the one line weight (48-unit grid: 0.8 px at 16 px, 51 px at 1024 px)
C0 = (24.0, 24.5); R0 = 19.5
GAP = (-72.0, -24.0)        # the ring opens at the upper right, where the head breaks out
def on_ring(deg, r=R0): a = math.radians(deg); return (C0[0] + r*math.cos(a), C0[1] + r*math.sin(a))
def ring_pts(n=240):
    a0, a1 = GAP[1], GAP[0] + 360
    return [on_ring(a0 + (a1 - a0)*i/n) for i in range(n + 1)]
def ring_d():
    p0 = on_ring(GAP[1]); p1 = on_ring(GAP[0] + 360)
    return "M%s %sA%s %s 0 1 1 %s %s" % (f(p0[0]), f(p0[1]), f(R0), f(R0), f(p1[0]), f(p1[1]))
# the camel: one set of points for every direction
P = dict(R=(9.5, 21.5), H1=(13.5, 15.0), H2=(19.0, 12.0), H3=(24.5, 14.0), W=(28.5, 19.0),
         N1=(31.5, 15.5), Pl=(34.0, 10.0), S=(42.5, 11.5), S2=(42.0, 14.5), J=(36.5, 15.5), N2=(35.0, 20.5),
         C=(31.0, 25.5), BF=(27.5, 27.5), BR=(12.5, 27.5),
         RK=(12.0, 34.0), RF=(13.5, 40.5), FK=(28.5, 34.5), FF=(29.5, 41.3),
         I1=(17.0, 21.0), I2=(24.0, 22.5))
OUTLINE = ["R", "H1", "H2", "H3", "W", "N1", "Pl", "S", "S2", "J", "N2", "C", "BF", "BR"]
INNER = [("R", "I1"), ("H2", "I1"), ("I1", "I2"), ("I2", "W"), ("I2", "BF"), ("W", "N2"), ("Pl", "J")]
LEGS = [("BR", "RK", "RF"), ("BF", "FK", "FF")]
CELLS = [["R", "H1", "H2", "I1"], ["H2", "H3", "W", "I2", "I1"], ["W", "C", "BF", "I2"], ["R", "I1", "I2", "BF", "BR"], ["W", "N1", "N2", "C"],
         ["N1", "Pl", "J", "N2"], ["Pl", "S", "S2", "J"]]
DX = 1.0  # the camel sits one unit right of the ring centre, to balance the head breaking out
P = {k: (x + DX, y) for k, (x, y) in P.items()}
q = lambda k: "%s %s" % (f(P[k][0]), f(P[k][1]))
def svg(body, title):
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 48 48\">\n  <title>%s</title>\n%s\n</svg>\n" % (title, body)
def dot(x, y, r): return "M%s %sa%s %s 0 1 0 %s 0a%s %s 0 1 0 -%s 0Z" % (f(x - r), f(y), f(r), f(r), f(2*r), f(r), f(r), f(2*r))
def poly_d(g):
    out = []
    for p in getattr(g, "geoms", [g]):
        for ring in [p.exterior] + list(p.interiors):
            c = list(ring.coords)[:-1]; out.append("M" + "L".join("%s %s" % (f(x), f(y)) for x, y in c) + "Z")
    return "".join(out)
# ---- A: graph. Straight edges; a node wherever three or more edges meet; the ring ends in nodes too ----
def dir_a():
    E = [(OUTLINE[i], OUTLINE[(i + 1) % len(OUTLINE)]) for i in range(len(OUTLINE))] + INNER
    for l in LEGS: E += [(l[0], l[1]), (l[1], l[2])]
    deg = {}
    for a, b in E: deg[a] = deg.get(a, 0) + 1; deg[b] = deg.get(b, 0) + 1
    e = "".join("M%sL%s" % (q(a), q(b)) for a, b in E)
    nodes = [k for k, d in deg.items() if d >= 3]
    dots = "".join(dot(*P[k], 1.75) for k in nodes) + "".join(dot(*on_ring(g), 1.75) for g in GAP)
    return svg("""  <g fill="none" stroke="currentColor" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round">
    <path d="%s"/>
    <path d="%s"/>
  </g>
  <path fill="currentColor" d="%s"/>""" % (f(W*0.8), ring_d(), e, dots), "Camel")
# ---- B: cells. The solid silhouette cut along the graph edges into rounded cells ----
def dir_b(gap=1.1, rnd=0.7):
    cells = []
    for c in CELLS:
        p = Polygon([P[k] for k in c]).buffer(-gap/2, join_style=2)
        cells.append(p.buffer(-rnd, join_style=1).buffer(rnd, join_style=1))
    legs = [LineString([P[k] for k in l]).buffer(W*0.62, cap_style=1, join_style=1).difference(Polygon([P[k] for k in OUTLINE]).buffer(gap/2)) for l in LEGS]
    ring = LineString(ring_pts()).buffer(W/2, cap_style=1)
    shape = unary_union(cells + legs + [ring]).difference(Point(39.3, 12.3).buffer(0.85))
    return svg("  <path fill=\"currentColor\" d=\"%s\"/>" % poly_d(shape.simplify(0.02)), "Camel")
# ---- C: monoline. One smooth outline in the same weight as the ring ----
def dir_c():
    outline = ("M" + q("RF") + "L" + q("RK") + "L" + q("BR") +
               "C12 26 10 23.5 " + q("R") +
               "C11 17 14.5 12 " + q("H2") +
               "C24 12 27.5 15 " + q("W") +
               "C31.5 18.5 32.5 13 " + q("Pl") +
               "C37.5 9 40.5 10.5 " + q("S") +
               "C44.5 12 44.2 14.5 " + q("S2") +
               "C41 15 39 15 " + q("J") +
               "C36.5 17.5 37 20.5 " + q("C") +
               "L" + q("BF") + "L" + q("FK") + "L" + q("FF"))
    belly = "M" + q("BR") + "L" + q("BF")
    return svg("""  <g fill="none" stroke="currentColor" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round">
    <path d="%s"/>
    <path d="%s"/>
    <path d="%s"/>
  </g>
  <path fill="currentColor" d="%s"/>""" % (f(W), ring_d(), outline, belly, dot(38.6, 12.2, 0.95)), "Camel")
for name, fn in [("a-graph", dir_a), ("b-cells", dir_b), ("c-monoline", dir_c)]:
    open("out/camel-%s.svg" % name, "w").write(fn())
print("ok")
