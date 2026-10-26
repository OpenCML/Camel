# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Checks every rendered frame before it is encoded, and makes a contact sheet to look at.

A frame is good when it exists, is 1920x1080 and opaque, and shows the ground the grade paints in
every corner, which only a working WebGL canvas draws: a frame whose canvas was lost is pure black
there. The first and last 0.4 s fade from and to black and are exempt from that test.
Usage: python verify.py FRAMES_DIR FPS SECONDS SHEET_PNG [t1,t2,...]
"""
import os
import sys

from PIL import Image, ImageStat

frames, fps, secs, sheet = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
times = [float(x) for x in sys.argv[5].split(",")] if len(sys.argv) > 5 else [1.5, 5.7, 8.8, 10.1, 11.8, 14.5, 17.9, 21.5, 23.0, 25.5, 27.6, 29.0]

problems = []
luma = []
for i in range(int(round(fps * secs))):
    f = os.path.join(frames, f"{i:06d}.png")
    if not os.path.exists(f):
        problems.append(f"{i}: missing")
        continue
    im = Image.open(f)
    if im.size != (1920, 1080):
        problems.append(f"{i}: size {im.size}")
    if im.mode == "RGBA" and im.getchannel("A").getextrema()[0] < 255:
        problems.append(f"{i}: not opaque")
    g = im.convert("L")
    t = i / fps
    if 0.4 < t < secs - 0.4 and max(im.convert("RGB").getpixel((4, 4))) == 0:
        problems.append(f"{i}: no ground (a lost canvas)")
    luma.append(ImageStat.Stat(g.reduce(8)).mean[0])

print(f"{len(problems)} problems in {int(round(fps * secs))} frames")
for p in problems[:40]:
    print("  ", p)
# mean luma per second, to see the shape of the film
for s in range(int(secs)):
    chunk = luma[s * fps:(s + 1) * fps]
    if chunk:
        print(f"{s:2d}s  luma {sum(chunk) / len(chunk):5.1f}  {'#' * int(sum(chunk) / len(chunk))}")

W, H = 640, 360
cols = 2
rows = (len(times) + cols - 1) // cols
S = Image.new("RGB", (W * cols, H * rows))
for k, t in enumerate(times):
    f = os.path.join(frames, f"{int(round(t * fps)):06d}.png")
    if os.path.exists(f):
        S.paste(Image.open(f).convert("RGB").resize((W, H)), ((k % cols) * W, (k // cols) * H))
S.save(sheet)
sys.exit(1 if problems else 0)
