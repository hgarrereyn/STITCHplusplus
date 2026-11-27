"""Synthesize the soundtrack for the STITCH++ launch film from the cue list render.cjs dumps.

usage: python3 sound.py events.json out.wav
Everything is generated: needle punches, a sewing-machine patter, a plucked thread, the snap, a pad.
"""
import json
import sys
import wave

import numpy as np

SR = 48000
DUR = 20.0
rng = np.random.default_rng(3)


def t_axis(sec):
    return np.arange(int(sec * SR)) / SR


def biquad(x, kind, f, q=0.707):
    """RBJ cookbook biquad; f may be a scalar or an array (time-varying)."""
    n = len(x)
    f = np.broadcast_to(np.asarray(f, dtype=float), (n,))
    y = np.zeros(n)
    x1 = x2 = y1 = y2 = 0.0
    for i in range(n):
        w = 2 * np.pi * f[i] / SR
        c, s = np.cos(w), np.sin(w)
        a = s / (2 * q)
        if kind == "lp":
            b0, b1, b2 = (1 - c) / 2, 1 - c, (1 - c) / 2
        elif kind == "hp":
            b0, b1, b2 = (1 + c) / 2, -(1 + c), (1 + c) / 2
        else:  # band-pass, 0 dB peak
            b0, b1, b2 = a, 0.0, -a
        a0, a1, a2 = 1 + a, -2 * c, 1 - a
        yi = (b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, x[i], y1, yi
        y[i] = yi
    return y


def env(n, attack, decay):
    t = np.arange(n) / SR
    return np.minimum(1, t / max(attack, 1e-4)) * np.exp(-t / decay)


def noise(sec):
    return rng.standard_normal(int(sec * SR))


# ---------- one-shots ----------
def punch(g=1.0):
    n = int(0.06 * SR)
    click = biquad(noise(0.06), "bp", 3200, 1.4) * env(n, 0.0005, 0.007)
    body = np.sin(2 * np.pi * 900 * t_axis(0.06)) * env(n, 0.0005, 0.012) * 0.25
    thump = np.sin(2 * np.pi * 140 * t_axis(0.06)) * env(n, 0.001, 0.02) * 0.35
    return (click * 1.4 + body + thump) * g


def out_(g=1.0):
    n = int(0.12 * SR)
    return biquad(noise(0.12), "bp", np.linspace(2500, 5200, n), 1.2) * env(n, 0.02, 0.035) * 0.5 * g


def tick(g=1.0):
    f = 2600 * (0.8 + 0.5 * rng.random())
    n = int(0.03 * SR)
    return (biquad(noise(0.03), "bp", f, 2.0) * env(n, 0.0003, 0.004) * 1.4
            + np.sin(2 * np.pi * 110 * t_axis(0.03)) * env(n, 0.0005, 0.008) * 0.25) * g


def pop(g=1.0):
    n = int(0.25 * SR)
    t = t_axis(0.25)
    f = 210 * np.exp(-t * 18) + 95
    ph = 2 * np.pi * np.cumsum(f) / SR
    return (np.sin(ph) * env(n, 0.002, 0.07) * 0.6 + biquad(noise(0.25), "lp", 1800) * env(n, 0.001, 0.012) * 0.3) * g


def zip_(g=1.0):
    n = int(0.36 * SR)
    sweep = np.geomspace(600, 6000, n)
    e = np.sin(np.linspace(0, np.pi, n)) ** 1.5
    return biquad(noise(0.36), "bp", sweep, 2.5) * e * 0.55 * g


def pluck(freq, sec, damp=0.996, bright=0.5):
    """Karplus-Strong plucked string."""
    n = int(sec * SR)
    period = int(SR / freq)
    buf = list(rng.uniform(-1, 1, period))
    out = np.zeros(n)
    for i in range(n):
        v = buf[i % period]
        out[i] = v
        nxt = buf[(i + 1) % period]
        buf[i % period] = damp * (bright * v + (1 - bright) * nxt) if i >= period else damp * 0.5 * (v + nxt)
    return out


def twang(g=1.0):
    s = pluck(196, 1.4, 0.9975) * 0.5 + pluck(294, 1.4, 0.997) * 0.18
    return biquad(s, "lp", 3500) * env(len(s), 0.002, 0.5) * g


def snap(g=1.0):
    n = int(1.2 * SR)
    t = t_axis(1.2)
    crack = biquad(noise(1.2), "hp", 1800) * env(n, 0.0002, 0.012) * 2.2
    tail = biquad(noise(1.2), "bp", 900, 0.8) * env(n, 0.002, 0.09) * 0.6
    thud = np.sin(2 * np.pi * (48 + 40 * np.exp(-t * 30)) * t) * env(n, 0.001, 0.22) * 1.1
    s = pluck(233, 1.2, 0.993, 0.3) + pluck(247, 1.2, 0.993, 0.3)  # a sour, broken string
    string = biquad(s, "lp", 2600) * env(n, 0.001, 0.25) * 0.35
    return (crack + tail + thud + string) * g


def key(g=1.0):
    n = int(0.02 * SR)
    return biquad(noise(0.02), "bp", 4200 + 800 * rng.random(), 2.5) * env(n, 0.0003, 0.003) * g


def chord(sec):
    """A warm, open D major 9 that blooms under the logo."""
    t = t_axis(sec)
    notes = [73.42, 146.83, 220.0, 329.63, 369.99, 440.0, 659.25]
    amps = [0.5, 0.35, 0.3, 0.22, 0.16, 0.12, 0.05]
    s = np.zeros_like(t)
    for f, a in zip(notes, amps):
        for det in (-0.7, 0.7):
            s += a * (np.sin(2 * np.pi * (f + det) * t + rng.random() * 6) + 0.15 * np.sin(2 * np.pi * 2 * (f + det) * t))
    attack = np.minimum(1, t / 0.9) ** 2
    release = np.clip((sec - t) / 1.4, 0, 1)
    shimmer = 1 + 0.06 * np.sin(2 * np.pi * 0.35 * t)
    return s * attack * release * shimmer * 0.11


# ---------- mix ----------
def main(ev_path, out_path):
    events = json.load(open(ev_path))
    n = int(DUR * SR)
    L = np.zeros(n)
    R = np.zeros(n)

    def place(sig, at, pan=0.0, gain=1.0):
        i = int(at * SR)
        if i >= n:
            return
        sig = sig[: n - i] * gain
        pan = float(np.clip(pan, -1, 1))
        a = (pan + 1) * np.pi / 4
        L[i:i + len(sig)] += sig * np.cos(a)
        R[i:i + len(sig)] += sig * np.sin(a)

    synth = {"punch": punch, "out": out_, "tick": tick, "pop": pop, "zip": zip_, "twang": twang, "snap": snap, "key": key}
    t_freeze = next(e["t"] for e in events if e["type"] == "stop")
    t_snap = next(e["t"] for e in events if e["type"] == "snap")
    t_chord = next(e["t"] for e in events if e["type"] == "chord")
    for e in events:
        f = synth.get(e["type"])
        if f is None:
            continue
        g = e.get("g", 1.0)
        if e["type"] == "tick":
            g *= 0.55 + 0.45 * rng.random()
        place(f(), e["t"], e.get("pan", 0.0), g)

    t = np.arange(n) / SR
    # low bed under the sewing; rises with the fuzzing and is cut dead at the freeze
    bed_amp = (np.clip(t / 2.0, 0, 1) * (0.5 + 0.5 * np.clip((t - 7.9) / 3.2, 0, 1)))
    bed = (np.sin(2 * np.pi * 55 * t) * 0.5 + np.sin(2 * np.pi * 82.41 * t) * 0.3 + np.sin(2 * np.pi * 110.3 * t) * 0.12)
    bed *= bed_amp * (1 + 0.12 * np.sin(2 * np.pi * 0.25 * t)) * 0.16
    # sewing-machine hum during the montage: band-limited noise, pitch climbing
    m0, m1 = int(7.9 * SR), int(t_freeze * SR)
    mt = t[m0:m1] - 7.9
    hum = biquad(noise((m1 - m0) / SR), "bp", 300 + 900 * (mt / mt[-1]) ** 2, 1.5)
    hum *= np.clip(mt / 1.2, 0, 1) * 0.11
    bed[m0:m1] += hum
    gate = np.ones(n)
    k = int(0.012 * SR)
    gate[m1:] = 0
    gate[m1 - k:m1] = np.linspace(1, 0, k)
    bedL, bedR = bed * gate, bed * gate
    # also cut every one-shot still ringing at the freeze
    L[m1:m1 + int(0.4 * SR)] *= 0
    R[m1:m1 + int(0.4 * SR)] *= 0
    L[m1 - k:m1] *= np.linspace(1, 0, k)
    R[m1 - k:m1] *= np.linspace(1, 0, k)
    # tension: a sub swell from the zoom to the snap
    s0, s1 = int((t_freeze + 0.3) * SR), int(t_snap * SR)
    st = t[s0:s1] - t[s0]
    ten = np.sin(2 * np.pi * np.cumsum(38 + 22 * st / st[-1]) / SR) * (st / st[-1]) ** 2 * 0.2
    ten += np.sin(2 * np.pi * np.cumsum(1760 + 30 * st / st[-1]) / SR) * (st / st[-1]) ** 3 * 0.012
    bedL[s0:s1] += ten
    bedR[s0:s1] += ten
    L += bedL
    R += bedR
    c = chord(DUR - t_chord)
    place(c, t_chord, -0.08)
    place(c[::-1][::-1] * 0.98, t_chord + 0.013, 0.08)

    mix = np.stack([L, R], 1)
    mix = np.tanh(mix * 1.6) / 1.6  # gentle soft clip
    mix *= 0.89 / np.max(np.abs(mix))
    fade = int(0.3 * SR)
    mix[-fade:] *= np.linspace(1, 0, fade)[:, None]
    pcm = (mix * 32767).astype("<i2")
    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
