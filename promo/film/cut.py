# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Assembles a cut of the main film: shots 00-10 joined by match cuts (a hard cut where the last
frame of a shot is the first frame of the next) or, where no shape carries over, a short dissolve
through the ground. The scratch narration (voice/shotNN.wav) is laid at each shot's start plus a
breath, and the English narration is burned in as a small caption under the Chinese subtitle.

It reads ../renders/shotNN/shotNN.mp4 (motion/encode.sh), ../renders/voice/shotNN.wav (voice.py)
and ../script.md, and writes the cut next to this script:
    python3 cut.py main-cut-v4.mp4
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, sys.argv[1] if len(sys.argv) > 1 else "main-cut.mp4")
# everything below is read relative to the renders
os.chdir(os.path.join(HERE, "..", "renders"))
FADE = 0.4
BREATH = 0.3
# the joins that are dissolves; every other join is a match cut
DISSOLVES = {(5, 6), (9, 10)}
shots = [f"shot{n:02d}" for n in range(11)]


def duration(path):
    out = subprocess.check_output(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path])
    return float(out)


durs = [duration(f"{s}/{s}.mp4") for s in shots]
fade_after = [FADE if (i, i + 1) in DISSOLVES else 0.0 for i in range(len(shots))]
fade_after[-1] = 0.0
script = open("../script.md", encoding="utf-8").read()
script = script[: script.index("# Part 2")]
narration = {int(m.group(1)): " ".join(m.group(2).split())
             for m in re.finditer(r"### Shot (\d+) .*?\*\*Narration:\*\* \"(.*?)\"", script, re.S)}

starts = []
t = 0.0
for i, d in enumerate(durs):
    starts.append(t)
    t += d - fade_after[i]
total = t


def ts(x):
    h, rem = divmod(x, 3600)
    m, s = divmod(rem, 60)
    return f"{int(h)}:{int(m):02d}:{s:05.2f}"


ass = [
    "[Script Info]", "ScriptType: v4.00+", "PlayResX: 1920", "PlayResY: 1080", "WrapStyle: 0", "",
    "[V4+ Styles]",
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, "
    "Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
    # chalk-dim #8E9AC0, bottom centre, under the Chinese subtitle band
    "Style: Caption,Noto Sans,30,&H00C09A8E,&H00C09A8E,&H00463924,&H00000000,0,0,0,0,100,100,0,0,1,0,0,2,120,120,18,1",
    "", "[Events]", "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
]
for i, (s, d) in enumerate(zip(starts, durs)):
    line = narration.get(i)
    if line:
        ass.append(f"Dialogue: 0,{ts(s + BREATH)},{ts(s + d - fade_after[i] - 0.05)},Caption,,0,0,0,,{{\\fad(150,150)}}{line}")
open("captions.ass", "w", encoding="utf-8").write("\n".join(ass) + "\n")

# video: runs of match-cut shots are concatenated; the runs are joined by dissolves
inputs = sum((["-i", f"{s}/{s}.mp4"] for s in shots), [])
runs, run = [], [0]
for i in range(1, len(shots)):
    if fade_after[i - 1] > 0:
        runs.append(run)
        run = []
    run.append(i)
runs.append(run)
graph = []
for r, members in enumerate(runs):
    graph.append("".join(f"[{i}:v]" for i in members) + f"concat=n={len(members)}:v=1:a=0[r{r}]")
prev, acc = "[r0]", sum(durs[i] for i in runs[0])
for r in range(1, len(runs)):
    off = acc - FADE
    graph.append(f"{prev}[r{r}]xfade=transition=fade:duration={FADE}:offset={off:.4f}[x{r}]")
    prev = f"[x{r}]"
    acc = off + sum(durs[i] for i in runs[r])
graph.append(f"{prev}subtitles=captions.ass:fontsdir=/usr/share/fonts[vout]")

# audio: each shot's line, a breath after the shot starts
voices = [i for i in range(len(shots)) if os.path.exists(f"voice/shot{i:02d}.wav")]
audio_inputs = sum((["-i", f"voice/shot{i:02d}.wav"] for i in voices), [])
base = len(shots)
for k, i in enumerate(voices):
    ms = int(round((starts[i] + BREATH) * 1000))
    graph.append(f"[{base + k}:a]aresample=48000,adelay={ms}:all=1[a{k}]")
graph.append("".join(f"[a{k}]" for k in range(len(voices))) + f"amix=inputs={len(voices)}:normalize=0,apad,atrim=0:{total:.3f}[aout]")

cmd = ["ffmpeg", "-y", "-loglevel", "error", *inputs, *audio_inputs, "-filter_complex", ";".join(graph),
       "-map", "[vout]", "-map", "[aout]",
       "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-r", "60",
       "-c:a", "aac", "-b:a", "192k", "-movflags", "+faststart", OUT]
subprocess.check_call(cmd)
print(f"total {total:.2f} s")
for i, (s, st, d) in enumerate(zip(shots, starts, durs)):
    join = "dissolve" if fade_after[i] else ("match cut" if i < len(shots) - 1 else "end")
    print(f"{s} {ts(st)} {d:.2f} -> {join}")
