"""Scratch narration for the main film: one WAV per shot from script.md, with Kokoro-82M.
Usage: voice.py SCRIPT_MD OUT_DIR [VOICE] [SPEED]"""
import json
import re
import sys

import numpy as np
import soundfile as sf
from kokoro import KPipeline

script_md, out_dir = sys.argv[1], sys.argv[2]
voice = sys.argv[3] if len(sys.argv) > 3 else "af_heart"
speed = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0

text = open(script_md, encoding="utf-8").read()
part1 = text[: text.index("# Part 2")]
lines = {int(m.group(1)): " ".join(m.group(2).split())
         for m in re.finditer(r"### Shot (\d+) .*?\*\*Narration:\*\* \"(.*?)\"", part1, re.S)}

pipe = KPipeline(lang_code="a")
SR = 24000
report = {}
for n, line in sorted(lines.items()):
    # spoken forms the reader would use
    spoken = line.replace("C++", "C plus plus").replace("ONNX", "onyx")
    chunks = [audio.numpy() if hasattr(audio, "numpy") else np.asarray(audio)
              for _, _, audio in pipe(spoken, voice=voice, speed=speed)]
    wav = np.concatenate(chunks)
    # trim the leading and trailing silence
    loud = np.where(np.abs(wav) > 0.01)[0]
    wav = wav[max(loud[0] - 240, 0): loud[-1] + 2400]
    sf.write(f"{out_dir}/shot{n:02d}.wav", wav, SR)
    report[n] = {"line": line, "seconds": round(len(wav) / SR, 2), "words": len(re.findall(r"[A-Za-z0-9+']+", line))}
    print(n, report[n]["seconds"], report[n]["words"], line)
json.dump(report, open(f"{out_dir}/voice.json", "w"), indent=1)
