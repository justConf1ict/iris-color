#!/usr/bin/env python3
"""Reproducible Iris Body comparison; fixed gain audition matching only.

Render the frozen 0.6 executable and current renderer, then analyze their actual
WAV files alongside optional separately rendered reference VST3 examples.
The script never loads or redistributes third-party plugin binaries.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import time
import warnings
import numpy as np
from scipy import signal
from scipy.io import wavfile

INPUTS = ("user-bass-Dsharp-minor", "synthetic-fm-growl", "synthetic-color-gun", "synthetic-reese")
MATERIALS = ("glass", "metal", "wood", "string")
PRESETS = ("crystal-punch", "chrome-growl", "wooden-pluck", "string-halo")
NAMES = {"dry": "Dry", "iris06": "Iris 0.6 / Glass", "iris07-off": "Iris 0.7 / Body off",
         **{"iris07-"+m: "Iris 0.7 / "+m.title()+" Body only change" for m in MATERIALS},
         **{"preset-"+p: "Iris 0.7 / "+p.replace("-", " ").title()+" preset" for p in PRESETS},
         "masterspark-modal": "MASTER SPARK / Modal glass scene",
         "masterspark-resochord": "MASTER SPARK / Resochord scene"}

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read(path):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", wavfile.WavFileWarning)
        rate, x = wavfile.read(path)
    if x.dtype.kind in "iu":
        if x.dtype.kind == "u":
            x = (x.astype(np.float64)-128)/128
        else:
            x = x.astype(np.float64)/2**(8*x.dtype.itemsize-1)
    else:
        x = x.astype(np.float64)
    if x.ndim == 1:
        x = np.column_stack((x, x))
    if x.shape[1] != 2 or not np.isfinite(x).all():
        raise ValueError(f"Invalid stereo audio: {path}")
    return rate, x

def db(value):
    return 20*math.log10(max(float(value), 1e-15))

def render(lab, old, new, jobs):
    raw = lab/"raw"
    raw.mkdir(parents=True, exist_ok=True)
    shared = ["--root", "D#", "--scale", "minor", "--output", "0", "--no-limit",
              "--tempo", "150", "--tail", "1.6", "--block", "512"]
    scenes = [("iris06", old, ["--preset", "glass"]),
              ("iris07-off", new, ["--preset", "glass", "--body-mix", "0"])]
    for material in MATERIALS:
        scenes.append(("iris07-"+material, new, ["--preset", "glass", "--body-material", material,
                      "--body-mix", "40", "--body-decay", "360", "--body-damping", "45",
                      "--body-inharmonicity", "25", "--body-strike", "40"]))
    for preset in PRESETS:
        scenes.append(("preset-"+preset, new, ["--preset", preset]))
    tasks = []
    for stem in INPUTS:
        inp = lab/"input"/(stem+".wav")
        if not inp.exists():
            continue
        rate, x = read(inp)
        wavfile.write(raw/("dry--"+stem+".wav"), rate,
                      np.pad(x, ((0, round(rate*1.6)), (0, 0))).astype(np.float32))
        for key, executable, opts in scenes:
            out = raw/(key+"--"+stem+".wav")
            tasks.append((key, stem, [str(executable), str(inp), str(out), *opts, *shared]))
    def run(item):
        key, stem, cmd = item
        started = time.perf_counter()
        proc = subprocess.run(cmd, text=True, capture_output=True, check=True)
        return {"scene": key, "input": stem, "command": cmd,
                "elapsed_seconds": time.perf_counter()-started,
                "renderer": json.loads(proc.stdout.strip()), "output_sha256": digest(cmd[2])}
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        receipts = list(pool.map(run, tasks))
    equality = []
    for stem in INPUTS:
        a = raw/("iris06--"+stem+".wav")
        b = raw/("iris07-off--"+stem+".wav")
        if a.exists() and b.exists():
            sr, x = read(a); _, y = read(b)
            if x.shape != y.shape:
                raise AssertionError("Old/new render lengths differ")
            err = float(np.max(np.abs(x-y)))
            equality.append({"input": stem, "sample_rate": sr, "frames": len(x),
                             "max_sample_error": err, "bit_identical_samples": bool(np.array_equal(x, y))})
            if err != 0:
                raise AssertionError(f"Body-off regression on {stem}: {err}")
    report = {"baseline_executable_sha256": digest(old), "current_executable_sha256": digest(new),
              "controlled_comparison": "Iris 0.6 and 0.7 use the same Glass preset; Body variants change only six Body parameters.",
              "shared_settings": shared, "body_off_equality": equality, "renders": receipts}
    (lab/"iris-render-receipt.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({"renders": len(receipts), "body_off_equality": equality}, indent=2))

def integrated_lufs(path, seconds=6.4):
    # This output is discarded. Only input measurements from loudnorm are used.
    proc = subprocess.run(["ffmpeg", "-hide_banner", "-nostdin", "-i", str(path),
        "-af", f"atrim=end={seconds},loudnorm=I=-18:TP=-1:LRA=11:print_format=json",
        "-f", "null", "-"], text=True, capture_output=True, check=True)
    blocks = re.findall(r'\{\s*"input_i".*?\}', proc.stderr, flags=re.S)
    if not blocks:
        raise RuntimeError("FFmpeg loudness result missing")
    return float(json.loads(blocks[-1])["input_i"])

def metrics(path, seconds=6.4):
    sr, x = read(path)
    main = x[:round(seconds*sr)]
    tail = x[round(seconds*sr):]
    freq, psd = signal.welch(main, fs=sr, nperseg=8192, axis=0)
    psd = np.mean(psd, axis=1)
    bands = {"sub_20_120": (20, 120), "low_mid_120_500": (120, 500),
             "mid_500_2500": (500, 2500), "high_2500_8000": (2500, 8000),
             "air_8000_16000": (8000, 16000)}
    energies = {k: float(np.sum(psd[(freq>=lo)&(freq<hi)])) for k, (lo, hi) in bands.items()}
    total = sum(energies.values())
    peak = float(np.max(np.abs(x)))
    tp = float(np.max(np.abs(signal.resample_poly(x, 4, 1, axis=0))))
    rms = float(np.sqrt(np.mean(main**2)))
    return {"sample_rate": sr, "frames": len(x), "sha256": digest(path),
            "input_window_seconds": seconds, "integrated_lufs": integrated_lufs(path, seconds),
            "sample_peak_dbfs": db(peak), "true_peak_4x_dbfs": db(tp), "main_rms_dbfs": db(rms),
            "crest_factor_db": db(np.max(np.abs(main)))-db(rms),
            "tail_1_6s_rms_dbfs": db(np.sqrt(np.mean(tail**2))) if len(tail) else None,
            "spectral_energy_percent": {k: 100*v/max(total, 1e-30) for k, v in energies.items()},
            "outside_unit_range_samples": int(np.sum(np.abs(x)>1)), "all_finite": True}

def analyze(lab, jobs):
    paths = sorted((lab/"raw").glob("*--*.wav"))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        values = list(pool.map(metrics, paths))
    rows = []
    for p, data in zip(paths, values):
        scene, stem = p.stem.split("--", 1)
        rows.append({"scene": scene, "input": stem, "raw_file": str(p.relative_to(lab)), **data})
    matched = lab/"matched"
    matched.mkdir(exist_ok=True)
    groups = {}
    for stem in INPUTS:
        group = [r for r in rows if r["input"] == stem]
        if not group:
            continue
        # A single target per source, with headroom for every included scene.
        target = min([-18.0]+[r["integrated_lufs"]-r["true_peak_4x_dbfs"]-1.05 for r in group])
        target = math.floor(target*10)/10
        groups[stem] = {"target_lufs": target, "scenes": [r["scene"] for r in group]}
        for row in group:
            gain = target-row["integrated_lufs"]
            row["audition_gain_db"] = gain
            row["audition_target_lufs"] = target
            row["audition_true_peak_4x_dbfs"] = row["true_peak_4x_dbfs"]+gain
            dst = matched/(row["scene"]+"--"+stem+".wav")
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", str(lab/row["raw_file"]), "-af", f"volume={gain:.12f}dB",
                "-c:a", "pcm_s24le", str(dst)], check=True)
            row["matched_file"] = str(dst.relative_to(lab))
            row["matched_sha256"] = digest(dst)
    controlled = []
    for stem in INPUTS:
        off = lab/"raw"/("iris07-off--"+stem+".wav")
        if not off.exists():
            continue
        sr, x = read(off)
        n = round(6.4*sr)
        for material in MATERIALS:
            on = lab/"raw"/("iris07-"+material+"--"+stem+".wav")
            _, y = read(on)
            delta = y-x
            # Quantifies the added layer. It does not measure preference or quality.
            controlled.append({"input": stem, "material": material,
                "body_delta_rms_relative_db": db(np.sqrt(np.mean(delta[:n]**2))/max(np.sqrt(np.mean(x[:n]**2)),1e-15)),
                "max_sample_difference": float(np.max(np.abs(delta))),
                "body_delta_tail_rms_dbfs": db(np.sqrt(np.mean(delta[n:]**2)))})
    report = {"method": {
        "loudness": "FFmpeg loudnorm input_i, first 6.4 seconds; output discarded",
        "matching": "One constant gain per clip, one shared target per source, no EQ/limiter/compression; target lowered when necessary to keep >=1.05 dB estimated peak headroom",
        "true_peak": "SciPy 4x polyphase estimate over complete clip; not a certified true-peak meter",
        "spectrum": "Stereo-averaged Welch PSD, 8192-sample windows; each band percent of combined 20 Hz to 16 kHz energy",
        "tail": "RMS of the 1.6 second extension after source ends; source loops already contain internal gaps",
        "interpretation": "Differences describe these authored settings and these four clips; they do not rank products or establish listener preference",
        "reference": "MasterSpark settings are independent authored scenes, not equivalent numerical knobs or a clone of Iris"},
        "groups": groups, "body_isolation": controlled, "clips": rows}
    (lab/"comparison-metrics.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")
    print(json.dumps({"clips": len(rows), "targets": {k:v["target_lufs"] for k,v in groups.items()},
                      "body_isolation": controlled}, indent=2))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("render", "analyze"))
    parser.add_argument("lab", type=Path)
    parser.add_argument("--old", type=Path)
    parser.add_argument("--new", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.phase == "render":
        if not args.old or not args.new:
            parser.error("render requires --old and --new")
        render(args.lab.resolve(), args.old.resolve(), args.new.resolve(), args.jobs)
    else:
        analyze(args.lab.resolve(), args.jobs)

if __name__ == "__main__":
    main()
