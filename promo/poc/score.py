# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The score of the proof of concept: synthesised from the film's cue sheet, so every hit lands on
the frame it belongs to. Three movements on one beat grid (96 BPM):
  1. the cold open: a low drone that tightens, a pulse, hats under the trace, a key click per
     character, and a glitch hit on each tear (the last one, the recompile, the largest), then a
     riser cut dead at the turn;
  2. the turn: near silence, one high shimmer under the question;
  3. the reveal: a reverse swell into the caret, a soft bell for every node the code folds into, a
     chord that opens when the branch region closes whole, a pulse and an arpeggio as the light
     runs, and a resolving chord under the last line.
Usage: python score.py CUES_JSON OUT_WAV
"""
import json
import sys

import numpy as np
from scipy.signal import butter, fftconvolve, sosfilt

SR = 48000
cues = json.load(open(sys.argv[1]))
T = cues["times"]
BEAT = cues["beat"]
DUR = cues["duration"]
N = int(SR * DUR)
rng = np.random.default_rng(7)
L = np.zeros(N)
R = np.zeros(N)
# a send to the reverb, per channel
RL = np.zeros(N)
RR = np.zeros(N)


def t_axis(n):
    return np.arange(n) / SR


def place(sig, at, gain=1.0, pan=0.0, verb=0.25):
    """Add a mono signal at time `at`, panned (-1 left .. 1 right), with a reverb send."""
    i = int(at * SR)
    if i >= N or i + len(sig) <= 0:
        return
    s = sig[: max(0, N - i)] * gain
    gl = np.sqrt(0.5 * (1 - pan))
    gr = np.sqrt(0.5 * (1 + pan))
    L[i: i + len(s)] += s * gl
    R[i: i + len(s)] += s * gr
    RL[i: i + len(s)] += s * gl * verb
    RR[i: i + len(s)] += s * gr * verb


def env(n, a, d, curve=4.0):
    e = np.ones(n)
    na = max(1, int(a * SR))
    e[:na] = np.linspace(0, 1, na)
    e[na:] = np.exp(-curve * t_axis(n - na) / max(d, 1e-3))
    return e


def lp(x, f, order=2):
    return sosfilt(butter(order, f, "low", fs=SR, output="sos"), x)


def hp(x, f, order=2):
    return sosfilt(butter(order, f, "high", fs=SR, output="sos"), x)


def bp(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], "band", fs=SR, output="sos"), x)


def saw(f, n, detune=0.0):
    ph = np.cumsum(np.full(n, f * (1 + detune)) / SR)
    return 2 * (ph % 1) - 1


def note(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# ---- 1. the cold open --------------------------------------------------------------------------

def drone():
    n = int(T["turn"] * SR)
    t = t_axis(n)
    x = np.zeros(n)
    for f, g in ((note(33), 1.0), (note(40), 0.5), (note(45), 0.35), (note(46), 0.12)):
        for d in (-0.003, 0.0, 0.004):
            x += saw(f, n, d) * g
    # the filter opens as the trace goes wrong
    out = np.zeros(n)
    edges = np.linspace(0, n, 25).astype(int)
    for a, b in zip(edges[:-1], edges[1:]):
        k = a / n
        out[a:b] = lp(x, 180 + 900 * k ** 2)[a:b]
    e = np.clip(t / 1.5, 0, 1) * (0.55 + 0.45 * np.clip((t - 5) / 5, 0, 1))
    return out * e * 0.06


place(drone(), 0, 1.0, 0, 0.3)


def kick(g=1.0, f0=110, f1=42, d=0.35):
    n = int(0.5 * SR)
    t = t_axis(n)
    f = f1 + (f0 - f1) * np.exp(-t * 28)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / d * 4) * g


def hat(g=1.0):
    n = int(0.06 * SR)
    return hp(rng.standard_normal(n), 7000) * np.exp(-t_axis(n) * 70) * g


def click(g=1.0):
    n = int(0.012 * SR)
    x = bp(rng.standard_normal(n), 3500 + rng.uniform(-600, 900), 9000) * np.exp(-t_axis(n) * 700)
    return x * g


for c in cues["pyChars"]:
    place(click(0.035), c, 1, rng.uniform(-0.5, 0.5), 0.03)
b = 4 * BEAT
while b < T["tear4"] - 0.01:
    place(kick(0.55), b, 1, 0, 0.1)
    b += BEAT
b = T["trace0"]
while b < T["tear4"]:
    place(hat(0.06), b, 1, 0.3, 0.05)
    b += BEAT / 2
for s in cues["traceSpawns"]:
    place(click(0.07), s, 1, 0.4, 0.2)


def glitch(scale=1.0):
    n = int(0.55 * SR * scale)
    t = t_axis(n)
    sub = np.sin(2 * np.pi * np.cumsum(38 + 60 * np.exp(-t * 18)) / SR) * np.exp(-t * 5 / scale)
    noise = rng.standard_normal(n)
    crushed = np.round(noise * 3) / 3
    burst = bp(crushed, 300, 9000) * np.exp(-t * 14)
    chirp = np.sin(2 * np.pi * np.cumsum(2400 * np.exp(-t * 9) + 80) / SR) * np.exp(-t * 10) * 0.5
    x = sub * 0.9 + burst * 0.5 + chirp
    # a stutter: the first slice repeats
    sl = int(0.035 * SR)
    for k in (1, 2, 3):
        x[k * sl: (k + 1) * sl] += x[:sl] * (0.7 ** k)
    return x


for key in ("tear1", "tear2", "tear3"):
    place(glitch(), T[key], 0.75, 0, 0.35)


def riser(a, b, g):
    n = int((b - a) * SR)
    t = t_axis(n)
    x = rng.standard_normal(n)
    out = np.zeros(n)
    edges = np.linspace(0, n, 30).astype(int)
    for p, q in zip(edges[:-1], edges[1:]):
        k = p / n
        out[p:q] = bp(x, 300 + 4000 * k ** 2, 900 + 9000 * k ** 2)[p:q]
    return out * (t / t[-1]) ** 2 * g


place(riser(T["call2"], T["tear4"], 0.35), T["call2"], 1, 0, 0.2)
big = glitch(2.4)
place(big, T["tear4"], 1.1, 0, 0.6)
place(kick(1.2, 90, 30, 1.2), T["tear4"], 1, 0, 0.4)
place(riser(T["line1"] - 0.6, T["turn"], 0.22), T["line1"] - 0.6, 1, 0, 0.3)

# the cold open is its own stem: it is cut dead at the turn, reverb and all
STEM_A = [x.copy() for x in (L, R, RL, RR)]
for x in (L, R, RL, RR):
    x[:] = 0

# ---- 2. the turn: silence, and a shimmer under the question ----------------------------------

def shimmer(dur, freqs, g):
    n = int(dur * SR)
    t = t_axis(n)
    x = sum(np.sin(2 * np.pi * f * t + rng.uniform(0, 6)) * (1 + 0.3 * np.sin(2 * np.pi * 0.3 * t + i)) for i, f in enumerate(freqs))
    e = np.clip(t / 1.8, 0, 1) * np.clip((dur - t) / 1.5, 0, 1)
    return x * e * g


place(shimmer(T["gather"] - T["question"] + 0.4, [note(76), note(83), note(88)], 0.0065), T["question"], 1, 0, 0.9)
place(lp(rng.standard_normal(int(2.5 * SR)), 200) * env(int(2.5 * SR), 1.0, 1.5) * 0.03, T["turn"] + 0.4, 1, 0, 0.6)

# ---- 3. the reveal -----------------------------------------------------------------------------

def swell(dur, g):
    n = int(dur * SR)
    t = t_axis(n)
    x = sum(np.sin(2 * np.pi * note(m) * t) for m in (69, 73, 76, 81, 85))
    return x * (t / dur) ** 3 * g


place(swell(T["camType0"] - T["gather"], 0.05), T["gather"], 1, 0, 0.8)


def bell(f, g=1.0, d=1.6):
    n = int(d * SR)
    t = t_axis(n)
    x = np.sin(2 * np.pi * f * t) + 0.35 * np.sin(2 * np.pi * f * 2.0 * t) * np.exp(-t * 3) + 0.18 * np.sin(2 * np.pi * f * 3.01 * t) * np.exp(-t * 6)
    return x * env(n, 0.003, d, 3.5) * g


place(bell(note(81), 0.22, 2.5), T["camType0"], 1, -0.3, 0.7)
for c in cues["camChars"]:
    place(click(0.025), c, 1, rng.uniform(-0.4, 0.2), 0.03)
# a bell per node, rising through A major pentatonic, on the nearest sixteenth
penta = [69, 71, 73, 76, 78, 81, 83, 85, 88, 90, 93, 95, 97, 100, 102, 105, 107, 109, 112]
for i, s in enumerate(cues["spawns"]):
    q = round(s / (BEAT / 4)) * (BEAT / 4)
    place(bell(note(penta[min(i, len(penta) - 1)] - 12), 0.13, 1.4), q, 1, -0.6 + 1.2 * (i / len(cues["spawns"])), 0.5)


def pad(chord, dur, g, cutoff=1400):
    n = int(dur * SR)
    t = t_axis(n)
    x = np.zeros(n)
    for m in chord:
        for d in (-0.004, 0.0, 0.005):
            x += saw(note(m), n, d)
    x = lp(x, cutoff, 2)
    e = np.clip(t / 0.9, 0, 1) * np.clip((dur - t) / 0.9, 0, 1)
    return x * e * g


reg = cues["region"]
place(pad([45, 57, 64, 69, 71, 76], T["final"] - reg + 0.4, 0.018), reg, 1, 0, 0.6)
place(shimmer(1.6, [note(88), note(93), note(97)], 0.03), reg, 1, 0.2, 0.9)
place(pad([38, 50, 57, 62, 66, 69], 2.6, 0.012, 1100), T["camType0"] + 0.2, 1, 0, 0.6)
# the pulse once the camera pulls back
b = T["pull"]
while b < T["final"]:
    place(kick(0.5, 100, 40, 0.3), b, 1, 0, 0.1)
    place(hat(0.05), b + BEAT / 2, 1, 0.3, 0.05)
    b += BEAT
# the light: an arpeggio along each run, a chime on the result
arp = [69, 73, 76, 80, 81, 85, 88, 92, 93, 97, 100, 104, 105, 109]
for run in cues["runs"]:
    for k in range(run["hops"]):
        place(bell(note(arp[k % len(arp)]), 0.06, 0.7), run["t"] + k * run["hop"], 1, -0.5 + k / run["hops"], 0.4)
    place(bell(note(93), 0.16, 2.2), run["t"] + 13 * run["hop"], 1, 0.4, 0.8)
place(pad([45, 57, 61, 64, 69, 73, 76], DUR - T["final"] + 0.2, 0.022, 1800), T["final"] - 0.1, 1, 0, 0.8)
place(bell(note(81), 0.16, 3.0), T["final"], 1, 0, 0.9)

# ---- the room, the master ------------------------------------------------------------------------

def impulse(seed, secs=2.8):
    r = np.random.default_rng(seed)
    n = int(secs * SR)
    t = t_axis(n)
    ir = r.standard_normal(n) * np.exp(-t * 2.4)
    return lp(ir, 6000) * 0.02


def wet(l, r, rl, rr):
    return l + fftconvolve(rl, impulse(1))[:N], r + fftconvolve(rr, impulse(2))[:N]


al, ar = wet(*STEM_A)
bl, br = wet(L, R, RL, RR)
# the turn is silent: nothing of the cold open survives the cut
cut = int(T["turn"] * SR)
fade = int(0.012 * SR)
keep = np.zeros(N)
keep[:cut] = 1
keep[cut: cut + fade] = np.linspace(1, 0, fade)
out = np.stack([al * keep + bl, ar * keep + br], 1)
out = np.tanh(out * 1.6) / 1.6
out /= np.max(np.abs(out)) / 0.89
fi = int(0.02 * SR)
out[-fi:] *= np.linspace(1, 0, fi)[:, None]

import wave

with wave.open(sys.argv[2], "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((out * 32767).astype("<i2").tobytes())
print("wrote", sys.argv[2], f"{DUR}s")
