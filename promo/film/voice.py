# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Scratch narration for the main film: one WAV per shot from script.md, with Kokoro-82M.
Usage: voice.py [VOICE] [SPEED]   (writes ../renders/voice/shotNN.wav and voice.json)
Kokoro runs in a virtualenv: python -m venv tts; tts/bin/pip install torch --index-url
https://download.pytorch.org/whl/cpu; tts/bin/pip install "kokoro>=0.9.4" soundfile"""
import json
import os
import re
import sys

import numpy as np
import soundfile as sf
from kokoro import KPipeline

HERE = os.path.dirname(os.path.abspath(__file__))
script_md = os.path.join(HERE, "..", "script.md")
out_dir = os.path.join(HERE, "..", "renders", "voice")
os.makedirs(out_dir, exist_ok=True)
voice = sys.argv[1] if len(sys.argv) > 1 else "af_heart"
speed = float(sys.argv[2]) if len(sys.argv) > 2 else 0.85

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
