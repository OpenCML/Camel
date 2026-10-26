# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

import json, re, subprocess, numpy as np
from PIL import Image
v = json.load(open("vector.json"))
f = lambda x: ("%.1f" % x).rstrip("0").rstrip(".")
GROUP_OF_NODE = {}
for e in v["edges"]:
    for k in (e["a"], e["b"]):
        if k is not None and k not in GROUP_OF_NODE: GROUP_OF_NODE[k] = e["group"]
def mark(ind="  "):
    L = ['%s<g fill="none" stroke="currentColor" stroke-width="%s" stroke-linecap="round" stroke-linejoin="round">' % (ind, f(v["sw"]))]
    L.append('%s  <g id="ring">' % ind)
    for i, d in enumerate(v["ring"]): L.append('%s    <path id="ring-%d" d="%s"/>' % (ind, i + 1, d))
    dr = v["drop"]; L.append('%s    <circle id="ring-end" cx="%s" cy="%s" r="%s" fill="currentColor" stroke="none"/>' % (ind, f(dr["cx"]), f(dr["cy"]), f(dr["r"])))
    L.append("%s  </g>" % ind)
    for gname in ("body", "legs", "neck", "head"):
        L.append('%s  <g id="%s">' % (ind, gname))
        for e in [e for e in v["edges"] if e["group"] == gname]:
            L.append('%s    <path id="%s" d="%s"/>' % (ind, e["id"], e["d"]))
        for n in [n for n in v["nodes"] if GROUP_OF_NODE.get(n["id"]) == gname]:
            L.append('%s    <circle id="node-%d" cx="%s" cy="%s" r="%s" fill="currentColor" stroke="none"/>' % (ind, n["id"], f(n["x"]), f(n["y"]), f(n["r"])))
        L.append("%s  </g>" % ind)
    L.append('%s  <path id="eye" d="%s" fill="currentColor" stroke="none"/>' % (ind, v["eye"]))
    L.append("%s</g>" % ind)
    return "\n".join(L)
head = lambda w, h: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">\n  <title>Camel</title>' % (w, h, w, h)
open("out/camel-mark.svg", "w").write(head(1024, 1024) + "\n" + mark() + "\n</svg>\n")
word = json.load(open("/work/promo/logo/parts_word.json"))["body"]
open("out/camel-wordmark.svg", "w").write(head(2423, 1024) + "\n" + mark() + '\n  <path id="wordmark" fill="currentColor" d="%s"/>\n</svg>\n' % word)
subprocess.run(["rsvg-convert", "-w", "1024", "-h", "1024", "-o", "out/camel-mark.png", "out/camel-mark.svg"], check=True)
subprocess.run(["rsvg-convert", "-h", "1024", "-o", "out/camel-wordmark.png", "out/camel-wordmark.svg"], check=True)
# overlay against the original at 2048
s = 2048
subprocess.run(["rsvg-convert", "-w", str(s), "-h", str(s), "-o", "/tmp/n.png", "out/camel-mark.svg"], check=True)
n = np.array(Image.open("/tmp/n.png").convert("RGBA"))[..., 3] > 127
o = np.array(Image.open("/work/promo/logo/src/logo_black.png").convert("RGBA").resize((s, s), Image.LANCZOS))[..., 3] > 127
rgb = np.full((s, s, 3), 255, np.uint8); rgb[o & n] = (60, 60, 70); rgb[o & ~n] = (230, 40, 40); rgb[~o & n] = (20, 140, 255)
Image.fromarray(rgb).save("out/overlay.png")
print("IoU %.3f  original-only %.1f%%  new-only %.1f%% of original ink" % ((o & n).sum() / (o | n).sum(), 100 * (o & ~n).sum() / o.sum(), 100 * (~o & n).sum() / o.sum()))
print("paths", open("out/camel-mark.svg").read().count("<path"), "circles", open("out/camel-mark.svg").read().count("<circle"))
