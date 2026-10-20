#!/usr/bin/env python3
"""Reproducible original dubstep-style test signals; no downloaded audio assets.

Four-times oversampling and polyphase resampling reduce synthesis aliasing.
These are diagnostic sound-design loops, not a representative music dataset.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from scipy import signal
from scipy.io import wavfile


def create(destination: Path, rate: int = 44100):
    destination.mkdir(parents=True, exist_ok=True)
    tempo, duration, oversample = 150, 6.4, 4
    sr = rate * oversample
    t = np.arange(round(duration * sr), dtype=np.float64) / sr
    rng = np.random.default_rng(7002026)
    notes = [(0, 0, .34), (.4, 0, .3), (.8, 7, .18), (1., 3, .18),
             (1.2, 0, .32), (1.6, 10, .31), (2., 7, .18), (2.2, 3, .18),
             (2.4, 0, .32), (2.8, 0, .3)]
    notes += [(at + 3.2, note, length) for at, note, length in notes]
    rendered = {}
    for name in ("synthetic-fm-growl", "synthetic-color-gun", "synthetic-reese"):
        out = np.zeros((len(t), 2), dtype=np.float64)
        if name == "synthetic-reese":
            f = 440 * 2 ** ((39 - 69) / 12)  # MIDI 39 = D-sharp2.
            wobble = (.5 + .5 * np.sin(2 * np.pi * 2.5 * t - .6)) ** 1.7
            fade = np.minimum(1, t / .012) * np.minimum(1, (duration - t) / .025)
            for c, detune in enumerate((-.0035, .0035)):
                phase = 2 * np.pi * f * (1 + detune) * t
                pair = .6 * signal.sawtooth(phase) + .4 * signal.sawtooth(phase * 1.004 + 1.3)
                upper = np.tanh(2.2 * pair) * (.15 + .85 * wobble)
                out[:, c] = (upper * .5 + .26 * np.sin(2 * np.pi * f / 2 * t)) * fade
        else:
            for at, semitones, length in notes:
                idx = np.flatnonzero((t >= at) & (t < min(duration, at + length)))
                age = t[idx] - at
                f = 440 * 2 ** ((39 + semitones - 69) / 12)
                env = (1 - np.exp(-age / .0015)) * np.exp(-age / (.12 if name.endswith("gun") else .24))
                env *= np.minimum(1, (length - age) / .012)
                pitch = f * (1 + (.7 if name.endswith("gun") else .025) * np.exp(-age / .018))
                phase = 2 * np.pi * np.cumsum(pitch) / sr
                mod_index = (8 if name.endswith("gun") else 4.5) * np.exp(-age / .15) + .8
                burst = rng.standard_normal(len(age)) * np.exp(-age / .0018)
                sub = .25 * np.sin(phase * .5)
                for c, offset in enumerate((0, .018)):
                    fm = np.sin(phase + mod_index * np.sin(phase * 2.001 + offset))
                    formant = .27 * np.sin(phase * 5 + 1.7 * np.sin(phase * .5 + offset))
                    out[idx, c] += (np.tanh(1.9 * (fm + formant)) * .6 + sub) * env + .065 * burst
        out = signal.resample_poly(out, 1, oversample, axis=0)
        out = signal.sosfilt(signal.butter(2, 18, btype="highpass", fs=rate, output="sos"), out, axis=0)
        # Fixed per-file headroom, no dynamic normalization or limiting.
        out *= 10 ** (-9 / 20) / max(float(np.max(np.abs(out))), 1e-12)
        path = destination / (name + ".wav")
        wavfile.write(path, rate, out.astype(np.float32))
        rendered[name] = {"file": path.name, "rate": rate, "frames": len(out),
                          "duration_s": len(out) / rate, "peak_dbfs": -9,
                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    report = {"purpose": "original deterministic diagnostic bass; not third-party samples",
              "tempo_bpm": tempo, "root": "D#", "scale": "minor", "seed": 7002026,
              "synthesis_oversampling": oversample, "signals": rendered}
    (destination / "synthetic-provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--rate", type=int, default=44100)
    args = parser.parse_args()
    if args.rate not in (44100, 48000, 96000):
        parser.error("Use 44100, 48000 or 96000 Hz for this benchmark.")
    print(json.dumps(create(args.destination, args.rate), indent=2))
