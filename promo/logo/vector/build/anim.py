# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Render the Camel mark drawing itself: edges draw on in file order, nodes pop at their
junctions as the first edge reaches them, the eye last, then the ring closes.
Frames are SVGs with per-element dash offsets, rasterised by rsvg-convert."""
import json, math, os, re, subprocess, sys
import numpy as np

FPS = 60
T_TOTAL = 3.0
v = json.load(open("vector.json"))
f = lambda x: ("%.2f" % x).rstrip("0").rstrip(".")


# ---- path lengths (librsvg ignores pathLength, so dashes are in user units) ----
def arc_points(x1, y1, rx, ry, phi, fa, fs, x2, y2, n=400):
    phi = math.radians(phi)
    c, s = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = c * dx + s * dy, -s * dx + c * dy
    lam = (x1p / rx) ** 2 + (y1p / ry) ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(num / den, 0)) * (-1 if fa == fs else 1)
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx, cy = c * cxp - s * cyp + (x1 + x2) / 2, s * cxp + c * cyp + (y1 + y2) / 2
    ang = lambda ux, uy, vx, vy: math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not fs and dt > 0:
        dt -= 2 * math.pi
    if fs and dt < 0:
        dt += 2 * math.pi
    ts = t1 + dt * np.linspace(0, 1, n)
    return np.c_[cx + c * rx * np.cos(ts) - s * ry * np.sin(ts), cy + s * rx * np.cos(ts) + c * ry * np.sin(ts)]


def length(d):
    toks = re.findall(r"[MLCA]|-?\d+\.?\d*(?:e-?\d+)?", d)
    i, cur, pts = 0, None, []
    cmd = None
    while i < len(toks):
        if toks[i] in "MLCA":
            cmd = toks[i]
            i += 1
        n = dict(M=2, L=2, C=6, A=7)[cmd]
        a = list(map(float, toks[i:i + n]))
        i += n
        if cmd == "M":
            cur = np.array(a)
            pts.append([cur])
        elif cmd == "L":
            pts[-1].append(np.array(a))
            cur = np.array(a)
        elif cmd == "C":
            t = np.linspace(0, 1, 60)[1:, None]
            p0, p1, p2, p3 = cur, np.array(a[0:2]), np.array(a[2:4]), np.array(a[4:6])
            pts[-1].extend(list((1 - t) ** 3 * p0 + 3 * (1 - t) ** 2 * t * p1 + 3 * (1 - t) * t ** 2 * p2 + t ** 3 * p3))
            cur = p3
        else:
            pts[-1].extend(list(arc_points(cur[0], cur[1], a[0], a[1], a[2], int(a[3]), int(a[4]), a[5], a[6])[1:]))
            cur = np.array(a[5:7])
    return sum(np.sum(np.linalg.norm(np.diff(np.array(p), axis=0), axis=1)) for p in pts if len(p) > 1)


# ---- timing ----
ease = lambda t: 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2      # easeInOutCubic


def ease_back(t, s=1.9):
    t -= 1
    return 1 + (s + 1) * t ** 3 + s * t ** 2


edges = v["edges"]
N = len(edges)
E_START, E_SPAN, E_DUR = 0.05, 1.25, 0.32
for k, e in enumerate(edges):
    e["L"] = length(e["d"])
    e["t0"] = E_START + E_SPAN * k / max(N - 1, 1)
node_t = {}
for e in edges:
    for nid, frac in ((e["a"], 0.5), (e["b"], 1.0)):
        if nid is None:
            continue
        t = e["t0"] + E_DUR * frac
        node_t[nid] = min(node_t.get(nid, 9), t)
NODE_POP = 0.22
t_eye = max(e["t0"] for e in edges) + E_DUR
RING_T0, RING_DUR = 1.55, 0.9
rings = [dict(d=d, L=length(d)) for d in v["ring"]]
drop = v["drop"]
t_drop = RING_T0 + RING_DUR


def frame_svg(t, fg, bg):
    sw = f(v["sw"])
    L = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">']
    if bg:
        L.append('<rect width="1024" height="1024" fill="%s"/>' % bg)
    L.append('<g fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round">' % (fg, sw))
    for r in rings:
        p = ease(min(max((t - RING_T0) / RING_DUR, 0), 1))
        if p > 0:
            L.append('<path d="%s" stroke-dasharray="%s %s" stroke-dashoffset="%s"/>' % (r["d"], f(r["L"]), f(r["L"] + 50), f(r["L"] * (1 - p))))
    for e in edges:
        p = ease(min(max((t - e["t0"]) / E_DUR, 0), 1))
        if p > 0:
            L.append('<path d="%s" stroke-dasharray="%s %s" stroke-dashoffset="%s"/>' % (e["d"], f(e["L"]), f(e["L"] + 50), f(e["L"] * (1 - p))))
    L.append("</g>")
    for n in v["nodes"]:
        t0 = node_t.get(n["id"])
        if t0 is None:
            continue
        p = min(max((t - t0) / NODE_POP, 0), 1)
        if p > 0:
            L.append('<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (f(n["x"]), f(n["y"]), f(n["r"] * max(ease_back(p), 0)), fg))
    p = min(max((t - t_drop) / NODE_POP, 0), 1)
    if p > 0:
        L.append('<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (f(drop["cx"]), f(drop["cy"]), f(drop["r"] * max(ease_back(p), 0)), fg))
    p = min(max((t - t_eye) / NODE_POP, 0), 1)
    if p > 0:
        sc = max(ease_back(p), 0)
        L.append('<g transform="translate(812 229) scale(%s) translate(-812 -229)"><path d="%s" fill="%s"/></g>' % (f(sc), v["eye"], fg))
    L.append("</svg>")
    return "\n".join(L)


def render(name, fg, bg, size):
    os.makedirs("frames/" + name, exist_ok=True)
    n = int(T_TOTAL * FPS)
    for i in range(n):
        open("/tmp/fr.svg", "w").write(frame_svg(i / FPS, fg, bg))
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), "-o", "frames/%s/%04d.png" % (name, i), "/tmp/fr.svg"], check=True)
    return n


if __name__ == "__main__":
    print("edge lengths: total %.0f, ring %s, eye at %.2f s, ring %.2f-%.2f s" % (sum(e["L"] for e in edges), [round(r["L"]) for r in rings], t_eye, RING_T0, RING_T0 + RING_DUR))
    which = sys.argv[1:] or ["white", "navy"]
    if "white" in which:
        render("white", "#000000", "#FFFFFF", 1080)
    if "navy" in which:
        render("navy", "#D8DEF5", "#293158", 1080)
