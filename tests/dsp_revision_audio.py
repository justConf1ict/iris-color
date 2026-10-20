#!/usr/bin/env python3
"""Black-box regressions for the 0.2 DSP revision, with an immutable 0.1 baseline.

Runs the real WAV renderer; metrics deliberately separate transient damage,
local energy coupling, and the advertised dry-sidechain gate/morph contracts.
No test requires or implies equivalence to a commercial plugin.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np
from scipy.io import wavfile
from scipy import signal

FS = 48000
E4 = 440 * 2 ** ((64 - 69) / 12)
E6 = E4 * 4
BASE = ["--color", "100", "--snap", "100", "--scale", "custom", "--mask", "16",
        "--morph-off", "--gate-off", "--low", "60", "--high", "18000",
        "--transient", "0", "--width", "100", "--output", "0", "--no-limit", "--tail", "0"]


def rms(x):
    return float(np.sqrt(np.mean(np.square(np.asarray(x, dtype=np.float64)))))


def db(x):
    return float(20 * np.log10(max(1e-30, float(x))))


class Harness:
    def __init__(self, exe: Path, work: Path):
        self.exe = exe.resolve()
        self.work = work
        work.mkdir(parents=True, exist_ok=True)
        self.serial = 0
        self.cases = []

    def render(self, x, *args):
        stem = self.work / f"case_{self.serial:03d}"
        self.serial += 1
        source, dest = stem.with_suffix(".in.wav"), stem.with_suffix(".out.wav")
        wavfile.write(source, FS, np.asarray(x, dtype=np.float32))
        run = subprocess.run([str(self.exe), str(source), str(dest), *BASE, *args],
                             capture_output=True, text=True, timeout=90)
        if run.returncode:
            raise RuntimeError(run.stderr or run.stdout)
        fs, y = wavfile.read(dest)
        assert fs == FS and y.shape == (len(x), 2) and np.isfinite(y).all()
        return np.asarray(y[:, 0], dtype=np.float64)

    def check(self, name, metrics, checks):
        assertions = [{"requirement": name, "passed": bool(value)} for name, value in checks]
        self.cases.append({"name": name, "measurements": metrics, "assertions": assertions})
        print(("PASS" if all(c["passed"] for c in assertions) else "FAIL"), name, json.dumps(metrics), flush=True)


def tone_amplitude(x, frequency, begin, end):
    i = np.arange(round(begin * FS), round(end * FS))
    a = np.column_stack([np.sin(2*np.pi*frequency*i/FS), np.cos(2*np.pi*frequency*i/FS)])
    return float(np.linalg.norm(np.linalg.lstsq(a, x[i], rcond=None)[0]))


def run(h):
    t = np.arange(FS) / FS
    # An on-scale decaying sinusoid must not acquire a new random phase at its
    # attack, even with transient dry-return deliberately disabled.
    x = np.zeros(FS)
    start = round(.25 * FS)
    u = np.arange(FS-start)/FS
    x[start:] = .3*np.sin(2*np.pi*E6*u+.4)*np.minimum(1, u/.001)*np.exp(-u/.075)
    y = h.render(x)
    first = slice(start, start+round(.02*FS))
    before = slice(start-round(.025*FS), start)
    error = rms(y[first]-x[first])/rms(x[first])
    pre = rms(y[before])/rms(x[first])
    h.check("in_scale_burst_attack", {"first_20ms_relative_error": error, "pre_echo_relative_db": db(pre)},
            [("on-scale attack error below 0.2", error < .2), ("pre-echo below -40 dB", db(pre) < -40)])

    # An isolated click is broadband, not hundreds of sinusoidal carriers.
    # Its in-band attack should not be smeared by assigning every bin a peak.
    x = np.zeros(FS)
    x[start] = .5
    y = h.render(x)
    direct = abs(y[start])/abs(x[start])
    outside = np.copy(y)
    outside[start-1:start+2] = 0
    spread = rms(outside)/max(1e-30, rms(x))
    h.check("broadband_impulse", {"direct_peak_ratio": float(direct), "spread_energy_ratio": float(spread*spread)},
            [("direct peak at least 0.85 of original", direct > .85), ("spread energy below 0.1 of original", spread*spread < .1)])

    # A stable on-scale carrier coexists with a changing, separate cluster.
    # A collision at E6 must not pump the independent E4 carrier's level.
    x = .08*np.sin(2*np.pi*E4*t)
    cluster = np.zeros(FS)
    fade = np.clip((t-.25)/.01, 0, 1)*np.clip((.75-t)/.01, 0, 1)
    for j, f in enumerate((1030, 1130, 1240, 1420, 1540, 1640)):
        cluster += .04*np.sin(2*np.pi*f*t+j*.73)*fade
    x += cluster
    y = h.render(x)
    unoccupied = tone_amplitude(y, E4, .10, .22)
    occupied = tone_amplitude(y, E4, .40, .68)
    coupling = db(occupied/unoccupied)
    h.check("local_collision_no_global_pump", {"carrier_before": unoccupied, "carrier_during": occupied,
                                               "unrelated_carrier_change_db": coupling},
            [("unrelated carrier change within 0.6 dB", abs(coupling) < .6)])

    # A collision's attenuation must not be remembered across silence and
    # suppress a later, independent on-scale attack in the same frequency bins.
    t2 = np.arange(FS*2)/FS
    x = np.zeros(FS*2)
    gate = ((t2 > .1) & (t2 < .5)).astype(float)
    for j, f in enumerate((1030, 1130, 1240, 1420, 1540, 1640)):
        x += .04*np.sin(2*np.pi*f*t2+j*.73)*gate
    at = round(1.25*FS)
    u = np.arange(len(x)-at)/FS
    x[at:] = .3*np.sin(2*np.pi*E6*u+.4)*np.minimum(1, u/.001)*np.exp(-u/.075)
    y = h.render(x)
    first = slice(at, at+round(.02*FS))
    error = rms(y[first]-x[first])/rms(x[first])
    h.check("collision_history_after_silence", {"later_attack_relative_error": error},
            [("old collision attenuation does not damage later independent attack", error < .02)])

    # The threshold contract is bin-local. A centred sinusoid at -41 dBFS
    # exceeds a -42 dBFS threshold: its central bin must pass; -43 must close.
    # Its two Hann side lobes remain below threshold. Keeping only the centre
    # produces 2/3 amplitude after the four-overlap Hann WOLA, not unity.
    for level in (-41, -43):
        x = 10**(level/20)*np.sin(2*np.pi*1500*t)
        y = h.render(x, "--snap", "0", "--gate", "-42")
        # Stay clear of the finite WAV's stop edge: protected out-of-band dry
        # legitimately carries tiny STFT edge leakage around that event.
        gain = rms(y[FS//2:round(.90*FS)])/rms(x[FS//2:round(.90*FS)])
        h.check(f"gate_{level}_over_minus42", {"linear_gain": gain, "gain_db": db(gain)},
                [("central bin passes (2/3 WOLA amplitude), below threshold is silent",
                  abs(gain-2/3) < .003 if level > -42 else gain < 1e-5)])

    # Same dry-sidechain and same phase producer on every run. Spectral Morph
    # is a linear interpolation of magnitudes, not a waveshaper or dry bypass.
    x = np.zeros(FS)
    on = (t >= .20) & (t < .45)
    x[on] = .2*np.sin(2*np.pi*E6*t[on])
    y0 = h.render(x, "--color", "200", "--decay", "500", "--morph", "0")
    y50 = h.render(x, "--color", "200", "--decay", "500", "--morph", "50")
    y100 = h.render(x, "--color", "200", "--decay", "500", "--morph", "100")
    residual = float(np.max(np.abs(y50-(y0+y100)*.5)))
    tail = slice(round(.49*FS), round(.60*FS))
    ratio = rms(y50[tail])/rms(y0[tail])
    h.check("spectral_morph_magnitude_linearity", {"midpoint_residual_max": residual,
               "dry_silent_tail_50_to_0_db": db(ratio), "morph_100_silent_tail_rms": rms(y100[tail]),
               "morph100_differs_from_bypass_max": float(np.max(abs(y100-x)))},
            [("linear magnitude interpolation survives synthesis", residual < 2e-6),
             ("50% Morph makes dry-silent wet amplitude one half", abs(ratio-.5) < 1e-4),
             ("100% Morph suppresses dry-silent bins", rms(y100[tail]) < 1e-10)])

    ygate = h.render(x, "--color", "200", "--decay", "500", "--morph-off", "--gate", "-60")
    exact_zero = bool(np.all(ygate[tail] == 0))
    suppression = None if exact_zero else db(rms(ygate[tail])/rms(y0[tail]))
    h.check("gate_dry_reference_after_stop", {"dry_silent_tail_suppression_db": suppression,
                                            "tail_is_exact_digital_zero": exact_zero},
            [("gate mutes wet-only resonance after analysis-window overlap", exact_zero or suppression < -90)])

    # A neutral processing path must still reconstruct and report 3072.
    rng = np.random.default_rng(1821)
    x = rng.normal(0,.05,FS//2)
    y = h.render(x, "--snap", "0")
    error = float(np.max(abs(y-x)))
    h.check("neutral_unity", {"maximum_error": error}, [("neutral path remains unity", error < 2e-6)])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("renderer", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--measure-only", action="store_true")
    args = parser.parse_args()
    h = Harness(args.renderer, args.output.parent/(args.output.stem+"-fixtures"))
    run(h)
    result = {"sample_rate": FS, "renderer": str(h.exe), "renderer_calls": h.serial,
              "cases": h.cases, "passed": all(a["passed"] for c in h.cases for a in c["assertions"])}
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    raise SystemExit(0 if result["passed"] or args.measure_only else 1)
