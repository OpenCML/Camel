# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The score of the proof of concept, synthesised from the film's cue sheet so that every sound
lands on its frame. Quiet and sparse, on one beat grid (96 BPM):
  1. the cold open: a low drone; each tear a soft low knock and a dry tick; the recompile a deeper
     knock with a short wash; cut dead at the turn;
  2. the turn: silence, real silence, until the fragments gather;
  3. the reveal: a faint swell into the caret, a quiet bell for each node the code folds into, a
     soft chord when the branch region closes whole, a chime when each run reaches its result, and
     a resolving chord under the last line.
Usage: python score.py CUES_JSON OUT_WAV
"""
import json
import sys
import wave

import numpy as np
from scipy.signal import butter, fftconvolve, sosfilt

SR = 48000
cues = json.load(open(sys.argv[1]))
T = cues["times"]
DUR = cues["duration"]
N = int(SR * DUR)
rng = np.random.default_rng(7)


class Bus:
    def __init__(self):
        self.l, self.r, self.sl, self.sr = (np.zeros(N) for _ in range(4))

    def place(self, sig, at, gain=1.0, pan=0.0, verb=0.25):
        """A mono signal at time `at`, panned (-1 left .. 1 right), with a reverb send."""
        i = int(at * SR)
        if i >= N:
            return
        s = sig[: N - i] * gain
        gl, gr = np.sqrt(0.5 * (1 - pan)), np.sqrt(0.5 * (1 + pan))
        self.l[i: i + len(s)] += s * gl
        self.r[i: i + len(s)] += s * gr
        self.sl[i: i + len(s)] += s * gl * verb
        self.sr[i: i + len(s)] += s * gr * verb

    def wet(self):
        return (self.l + fftconvolve(self.sl, impulse(1))[:N], self.r + fftconvolve(self.sr, impulse(2))[:N])


def t_axis(n):
    return np.arange(n) / SR


def lp(x, f):
    return sosfilt(butter(2, f, "low", fs=SR, output="sos"), x)


def bp(x, lo, hi):
    return sosfilt(butter(2, [lo, hi], "band", fs=SR, output="sos"), x)


def note(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def impulse(seed, secs=2.6):
    r = np.random.default_rng(seed)
    n = int(secs * SR)
    return lp(r.standard_normal(n) * np.exp(-t_axis(n) * 2.6), 5000) * 0.02


def knock(f0, f1, d, g):
    n = int(1.2 * SR)
    t = t_axis(n)
    f = f1 + (f0 - f1) * np.exp(-t * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / d) * g


def tick(g):
    n = int(0.03 * SR)
    return bp(rng.standard_normal(n), 2000, 5000) * np.exp(-t_axis(n) * 220) * g


def bell(f, g, d=1.8):
    n = int(d * SR)
    t = t_axis(n)
    x = np.sin(2 * np.pi * f * t) + 0.25 * np.sin(2 * np.pi * 2 * f * t) * np.exp(-t * 4)
    a = np.minimum(t / 0.004, 1)
    return x * a * np.exp(-t * 3.2 / d) * g


def pad(chord, dur, g, cutoff=1100):
    n = int(dur * SR)
    t = t_axis(n)
    x = np.zeros(n)
    for m in chord:
        for d in (-0.003, 0.0, 0.004):
            x += np.sin(2 * np.pi * note(m) * (1 + d) * t) + 0.3 * np.sin(2 * np.pi * 2 * note(m) * (1 + d) * t)
    e = np.clip(t / 1.2, 0, 1) * np.clip((dur - t) / 1.2, 0, 1)
    return lp(x, cutoff) * e * g


# ---- 1. the cold open ----------------------------------------------------------------------------
A = Bus()
n = int(T["turn"] * SR)
t = t_axis(n)
drone = sum(np.sin(2 * np.pi * note(m) * t + i) for i, m in enumerate((33, 40, 45)))
A.place(lp(drone, 300) * np.clip(t / 2.0, 0, 1) * (0.6 + 0.4 * np.clip((t - 5) / 5, 0, 1)) * 0.02, 0, 1, 0, 0.2)
for key in ("tear1", "tear2", "tear3"):
    A.place(knock(70, 42, 0.18, 0.32), T[key], 1, 0, 0.25)
    A.place(tick(0.06), T[key], 1, 0.2, 0.2)
A.place(knock(60, 34, 0.45, 0.45), T["tear4"], 1, 0, 0.35)
wash = lp(rng.standard_normal(int(1.8 * SR)), 900) * np.exp(-t_axis(int(1.8 * SR)) * 2.4) * 0.04
A.place(wash, T["tear4"], 1, 0, 0.5)

# ---- 2. the turn is silent ------------------------------------------------------------------------

# ---- 3. the reveal --------------------------------------------------------------------------------
B = Bus()
dur = T["camType0"] - T["gather"]
n = int(dur * SR)
t = t_axis(n)
B.place(sum(np.sin(2 * np.pi * note(m) * t) for m in (69, 76, 81)) * (t / dur) ** 3 * 0.012, T["gather"], 1, 0, 0.6)
B.place(bell(note(81), 0.07, 2.4), T["camType0"], 1, -0.2, 0.6)
penta = [69, 71, 73, 76, 78, 81, 83, 85, 88]
spawns = cues["spawns"]
for i, s in enumerate(spawns):
    B.place(bell(note(penta[int(i * len(penta) / len(spawns))]), 0.03, 1.2), s, 1, -0.4 + 0.8 * i / len(spawns), 0.4)
B.place(pad([45, 57, 64, 69, 71, 76], T["final"] - cues["region"], 0.004), cues["region"], 1, 0, 0.5)
for run in cues["runs"]:
    B.place(bell(note(76), 0.035, 1.4), run["t"], 1, -0.3, 0.4)
    B.place(bell(note(88), 0.06, 2.2), run["t"] + 13 * run["hop"], 1, 0.3, 0.6)
B.place(pad([45, 57, 61, 64, 69, 73, 76], DUR - T["final"] + 0.2, 0.006, 1400), T["final"] - 0.1, 1, 0, 0.6)
B.place(bell(note(81), 0.06, 3.0), T["final"], 1, 0, 0.7)

# ---- the master -------------------------------------------------------------------------------------
al, ar = A.wet()
bl, br = B.wet()
cut = int(T["turn"] * SR)
fade = int(0.012 * SR)
keep = np.zeros(N)
keep[:cut] = 1
keep[cut: cut + fade] = np.linspace(1, 0, fade)
out = np.stack([al * keep + bl, ar * keep + br], 1)
# quiet: peaks at -6 dBFS, no compression
out *= 0.5 / np.max(np.abs(out))
fi = int(0.02 * SR)
out[-fi:] *= np.linspace(1, 0, fi)[:, None]
with wave.open(sys.argv[2], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((out * 32767).astype("<i2").tobytes())
print("wrote", sys.argv[2], f"{DUR}s")
