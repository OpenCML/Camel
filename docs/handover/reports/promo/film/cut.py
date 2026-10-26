"""Assembles the main film's first cut: shots 00-10 joined by a short dissolve through the ground,
with the English narration burned in as a small caption under the Chinese subtitle.
Run in /work/promo/cut with the shot MP4s and script.md beside it."""
import re
import subprocess

FADE = 0.4
shots = [f"shot{n:02d}" for n in range(11)]


def duration(path):
    out = subprocess.check_output(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path])
    return float(out)


durs = [duration(f"{s}.mp4") for s in shots]
script = open("script.md", encoding="utf-8").read()
narration = {int(m.group(1)): " ".join(m.group(2).split())
             for m in re.finditer(r"### Shot (\d+) .*?\*\*Narration:\*\* \"(.*?)\"", script, re.S)}

# the timeline: each shot starts FADE before the previous one ends
starts = []
t = 0.0
for d in durs:
    starts.append(t)
    t += d - FADE
total = t + FADE


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
        ass.append(f"Dialogue: 0,{ts(s + 0.3)},{ts(s + d - FADE - 0.05)},Caption,,0,0,0,,{{\\fad(150,150)}}{line}")
open("captions.ass", "w", encoding="utf-8").write("\n".join(ass) + "\n")

# the dissolve chain
inputs = sum((["-i", f"{s}.mp4"] for s in shots), [])
chain = []
prev = "[0:v]"
acc = durs[0]
for i in range(1, len(shots)):
    off = acc - FADE
    out = f"[v{i}]"
    chain.append(f"{prev}[{i}:v]xfade=transition=fade:duration={FADE}:offset={off:.4f}{out}")
    prev = out
    acc = off + durs[i]
chain.append(f"{prev}subtitles=captions.ass:fontsdir=/usr/share/fonts[out]")
cmd = ["ffmpeg", "-y", "-loglevel", "error", *inputs, "-filter_complex", ";".join(chain), "-map", "[out]",
       "-c:v", "libx264", "-preset", "slow", "-crf", "18", "-pix_fmt", "yuv420p", "-r", "60", "-movflags", "+faststart",
       "main-cut-v1.mp4"]
subprocess.check_call(cmd)
print(f"total {total:.2f} s")
for s, st, d in zip(shots, starts, durs):
    print(f"{s} {ts(st)} {d:.2f}")
