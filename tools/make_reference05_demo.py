#!/usr/bin/env python3
"""Render the supplied dry/Chroma pair through frozen Iris 0.4 and 0.5.

Requires Python 3.11+, NumPy, SciPy and Matplotlib, the two user-supplied WAVs,
and both actual Linux iris-render executables. The deliverable uses one fixed
gain per montage, with at least 6 dB attenuation; no segment is normalized.
Float render files remain in build/reference. Public WAVs are PCM24 stereo.

The reference informed development. The retained 3.25--6.4 s measurements are
descriptive same-source validation, not independent or blind listening tests.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import html
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import wave

sys.dont_write_bytecode = True
os.environ.setdefault("MPLCONFIGDIR", "/tmp/iris-reference05-matplotlib")
import numpy as np
from scipy import signal
from scipy.io import wavfile

import reference05_metrics as metrics

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
FS = 44100
SOURCE_FRAMES = 282240
SEGMENT_FRAMES = 300064
GAP_FRAMES = 15435
PEAK_CEILING = .89
MINIMUM_ATTENUATION_DB = 6.0
PCM_SCALE = 8388608
REGIONS = {"train": (.05, 3.15), "holdout": (3.25, 6.4)}
SCOPE = ("The supplied reference informed development, including its tail. "
         "These retained-segment measurements concern this same musical pair; "
         "they are not independent data, blind listening results, or a perceptual similarity score.")

BASE_OPTIONS = [
    "--preset", "init", "--root", "D#", "--scale", "minor-pent",
    "--color", "150", "--snap", "100", "--morph-off", "--gate-off",
    "--low", "230", "--high", "4000", "--decay", "200",
    "--transient", "0", "--width", "100", "--output", "0", "--no-limit",
    "--color-low", "100", "--color-mid", "100", "--color-high", "100",
    "--color-split-low", "700", "--color-split-high", "3000",
    "--eq-low", "0", "--eq-mid", "0", "--eq-high", "0", "--eq-flat",
    "--attack-tail-off", "--attack-color", "25", "--tail-color", "125", "--color-transition", "80",
    "--dive-off", "--dive-semitones", "12", "--dive-time", "100",
    "--motion-off", "--motion-depth", "75", "--motion-rate", "4",
    "--motion-sync", "--motion-division", "3", "--tempo", "150",
    "--formant-off", "--formant-mix", "50", "--vowel", "0",
    "--formant-motion", "0", "--formant-rate", ".5",
    "--tamer-off", "--tamer-amount", "50", "--tamer-max", "6",
    "--fft", "2048", "--spectral", "1024", "--block", "0",
    "--tail", "0.4041723356",
]

BASE_PARAMETERS = {
    "root": "D#", "scale": "minor-pent", "pitch_classes": ["C#", "D#", "F#", "G#", "A#"],
    "color_percent": 150, "snap_percent": 100, "processed_range_hz": [230, 4000],
    "decay_parameter_ms": 200, "morph_enabled": False, "gate_enabled": False,
    "transient_percent": 0, "width_percent": 100, "output_db": 0, "soft_limit_enabled": False,
    "band_color_percent": [100, 100, 100], "color_splits_hz": [700, 3000],
    "three_band_eq_db": [0, 0, 0], "graphic_eq_db": [0] * 16,
    "attack_tail_enabled": False, "attack_color_percent": 25, "tail_color_percent": 125,
    "color_transition_ms": 80, "dive_enabled": False, "dive_semitones": 12, "dive_ms": 100,
    "motion_enabled": False, "motion_depth_percent": 75, "motion_rate_hz": 4,
    "motion_sync": True, "motion_division": "1/8", "tempo_bpm": 150,
    "formant_enabled": False, "formant_mix_percent": 50, "formant_vowel": 0,
    "formant_motion_percent": 0, "formant_rate_hz": .5,
    "tamer_enabled": False, "tamer_amount_percent": 50, "tamer_max_db": 6,
    "core_fft": 2048, "spectral_fft": 1024, "block_pattern": [1, 17, 64, 511, 128, 7, 256, 1024, 31],
    "tail_frames": SEGMENT_FRAMES - SOURCE_FRAMES, "latency_compensated_samples": 3072,
}

RECIPES = [
    ("iris04", "Iris 0.4", "legacy", [], {}),
    ("iris05", "Iris 0.5 · Prism", "prism", [], {}),
    ("color100", "Color 100%", "prism", ["--color", "100"], {"color_percent": 100}),
    ("color200", "Color 200%", "prism", ["--color", "200"], {"color_percent": 200}),
    ("attack_tail", "Attack / Tail", "prism",
     ["--attack-tail-on", "--attack-color", "5", "--tail-color", "175", "--color-transition", "100"],
     {"attack_tail_enabled": True, "attack_color_percent": 5, "tail_color_percent": 175, "color_transition_ms": 100}),
    ("dive", "Pitch Dive", "prism", ["--dive-on", "--dive-semitones", "12", "--dive-time", "160"],
     {"dive_enabled": True, "dive_semitones": 12, "dive_ms": 160}),
    ("motion", "Scale Motion", "prism",
     ["--motion-on", "--motion-depth", "100", "--motion-sync", "--motion-division", "3", "--tempo", "150"],
     {"motion_enabled": True, "motion_depth_percent": 100}),
    ("formant", "Formant", "prism",
     ["--formant-on", "--formant-mix", "90", "--vowel", "2.6", "--formant-motion", "45", "--formant-rate", ".7"],
     {"formant_enabled": True, "formant_mix_percent": 90, "formant_vowel": 2.6,
      "formant_motion_percent": 45, "formant_rate_hz": .7}),
    ("tamer", "Peak Tamer", "prism",
     ["--tamer-on", "--tamer-amount", "75", "--tamer-max", "8"],
     {"tamer_enabled": True, "tamer_amount_percent": 75, "tamer_max_db": 8}),
]
LABELS = {"dry": "原音", "chroma": "Chroma 参考", **{r[0]: r[1] for r in RECIPES}}


def sha(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def db(value: float) -> float:
    return float(20 * np.log10(max(abs(value), 1e-30)))


def level_info(data: np.ndarray) -> dict:
    if data.ndim != 2 or data.shape[1] != 2 or not np.isfinite(data).all():
        raise ValueError("Expected finite stereo audio.")
    peak = float(np.max(np.abs(data)))
    rms = float(np.sqrt(np.mean(np.asarray(data, np.float64) ** 2)))
    return {"sample_peak": peak, "sample_peak_dbfs": db(peak), "rms": rms, "rms_dbfs": db(rms)}


def dependency_hashes(project: Path) -> dict:
    """Hash only actual renderer/DSP includes, independent of concurrent UI/docs work."""
    seen = set()
    pending = [project / "tools/render.cpp", project / "src/IrisDSP.cpp"]
    while pending:
        path = pending.pop().resolve()
        if path in seen:
            continue
        if not path.is_relative_to(project.resolve()) or not path.is_file():
            raise FileNotFoundError(path)
        seen.add(path)
        for include in re.findall(r'^\s*#include\s+"([^"]+)"', path.read_text(), re.MULTILINE):
            candidates = [path.parent / include, project / "src" / include]
            found = next((p for p in candidates if p.is_file()), None)
            if found is None:
                raise FileNotFoundError(f"Cannot resolve renderer dependency: {include}")
            pending.append(found)
    return {str(p.relative_to(project.resolve())): sha(p) for p in sorted(seen)}


def file_info(path: Path) -> dict:
    return {"path": str(path.resolve()), "sha256": sha(path), "size_bytes": path.stat().st_size}


def read_stereo(path: Path, frames: int, *, floating=False) -> np.ndarray:
    rate, raw = wavfile.read(path)
    if rate != FS or raw.shape != (frames, 2):
        raise ValueError(f"Unexpected source structure at {path}: {rate}, {raw.shape}")
    if floating and raw.dtype != np.float32:
        raise ValueError(f"Renderer must return float32 WAV: {path}")
    rate, data = metrics.read(path)
    level_info(data)
    return data


def write_pcm24(path: Path, data: np.ndarray) -> dict:
    data = np.asarray(data, np.float64)
    level_info(data)
    if np.max(np.abs(data)) > PEAK_CEILING:
        raise ValueError("Audition would exceed the agreed sample-peak ceiling.")
    quantized = np.rint(data * PCM_SCALE).astype(np.int32).ravel()
    if np.min(quantized) <= -PCM_SCALE or np.max(quantized) >= PCM_SCALE - 1:
        raise ValueError("PCM clipping before packing.")
    packed = np.empty((quantized.size, 3), np.uint8)
    packed[:, 0] = quantized & 255
    packed[:, 1] = (quantized >> 8) & 255
    packed[:, 2] = (quantized >> 16) & 255
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(3)
        stream.setframerate(FS)
        stream.writeframes(packed.tobytes())
    rate, decoded = wavfile.read(path)
    restored = decoded.astype(np.float64) / 2147483648
    error = float(np.max(np.abs(restored - data)))
    if rate != FS or decoded.dtype != np.int32 or error > 2 ** -24 + 1e-12:
        raise AssertionError("PCM24 round-trip failed.")
    return {"file": path.name, "sha256": sha(path), "size_bytes": path.stat().st_size,
            "frames": len(data), "duration_seconds": len(data) / FS, "sample_rate": FS,
            "channels": 2, "bits_per_sample": 24, "encoding": "signed PCM24, deterministic rounding, no dither",
            "quantization_max_error": error, **level_info(restored)}


def montage(out: Path, filename: str, key: str, title: str, subtitle: str,
            keys: list[str], arrays: dict, *, gain=None, measurement_filter=None) -> dict:
    peak = max(float(np.max(np.abs(arrays[k]))) for k in keys)
    if gain is None:
        gain = min(10 ** (-MINIMUM_ATTENUATION_DB / 20), (PEAK_CEILING - 2 / PCM_SCALE) / max(peak, 1e-20))
    if gain > 10 ** (-MINIMUM_ATTENUATION_DB / 20) + 1e-12:
        raise AssertionError("Less than 6 dB common attenuation.")
    position, clips, timeline = 0, [], []
    for index, name in enumerate(keys):
        data = arrays[name]
        if data.shape != (SEGMENT_FRAMES, 2):
            raise AssertionError("Every segment must have the same exact length.")
        clips.append(data * gain)
        timeline.append({
            "key": name, "label": LABELS[name], "start_frame": position,
            "end_frame_exclusive": position + SEGMENT_FRAMES,
            "start_seconds": position / FS, "end_seconds": (position + SEGMENT_FRAMES) / FS,
            "source_window_seconds": [0, SEGMENT_FRAMES / FS],
            "common_playback_gain": gain, "common_playback_gain_db": db(gain),
            "before_playback_gain": level_info(data),
        })
        position += SEGMENT_FRAMES
        if index < len(keys) - 1:
            clips.append(np.zeros((GAP_FRAMES, 2), np.float64))
            position += GAP_FRAMES
    result = write_pcm24(out / filename, np.concatenate(clips))
    result.update({"key": key, "title": title, "subtitle": subtitle, "timeline": timeline,
                   "gap_frames": GAP_FRAMES, "gap_seconds": GAP_FRAMES / FS,
                   "common_gain": gain, "common_gain_db": db(gain),
                   "raw_group_sample_peak": peak, "measurement_filter": measurement_filter,
                   "per_segment_normalization": False, "edge_fades": False})
    return result


def render_recipe(recipe, args, raw_dir: Path) -> tuple[np.ndarray, dict]:
    key, label, mode, overrides, changed = recipe
    renderer = args.legacy_renderer if mode == "legacy" else args.renderer
    output = raw_dir / f"{key}.float.wav"
    options = BASE_OPTIONS + ([] if mode == "legacy" else ["--engine", "prism"]) + overrides
    command = [str(renderer), str(args.dry), str(output), *options]
    run = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if run.returncode:
        raise RuntimeError(f"Renderer failed for {key}: {run.stderr or run.stdout}")
    lines = [line for line in run.stdout.splitlines() if line.strip().startswith("{")]
    if not lines:
        raise RuntimeError(f"Renderer returned no JSON metadata for {key}")
    metadata = json.loads(lines[-1])
    for name, expected in [("sample_rate", FS), ("input_frames", SOURCE_FRAMES),
                           ("output_frames", SEGMENT_FRAMES), ("latency_samples", 3072)]:
        if metadata[name] != expected:
            raise AssertionError(f"{key}: {name}={metadata[name]}, expected {expected}")
    data = read_stereo(output, SEGMENT_FRAMES, floating=True)
    params = {**BASE_PARAMETERS, "engine": "original 0.4 executable" if mode == "legacy" else "Prism", **changed}
    record = {"key": key, "label": label, "raw": file_info(output), "parameters": params,
              "command": command, "renderer_metadata": metadata, "stdout": run.stdout.strip(),
              "stderr": run.stderr.strip(), "latency_removed_once_by_renderer": True,
              "post_render_normalization": False, **level_info(data)}
    print(json.dumps({"rendered": key, "frames": len(data), "raw_peak": record["sample_peak"]}), flush=True)
    return data, record


def spectrum_figure(out: Path, arrays: dict, dry: np.ndarray, measured: dict) -> dict:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FixedLocator, FixedFormatter

    start, end = (int(v * FS) for v in REGIONS["holdout"])
    f, px = metrics.psd(dry[start:end], FS)
    curves = {}
    for key in ["chroma", "iris04", "iris05"]:
        _, p = metrics.psd(arrays[key][start:end], FS)
        curves[key] = 10 * np.log10((p + 1e-25) / (px + 1e-25))
    styles = {"chroma": ("#826480", "Chroma supplied reference", 1.9, "-"),
              "iris04": ("#91a89b", "Iris 0.4", 1.5, "--"),
              "iris05": ("#276b78", "Iris 0.5 / Prism", 1.85, "-")}
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11,
                         "axes.spines.top": False, "axes.spines.right": False})
    fig, ax = plt.subplots(figsize=(13.5, 6.9), dpi=170)
    fig.patch.set_facecolor("#f5f1e8")
    ax.set_facecolor("#fffdf7")
    fig.subplots_adjust(left=.083, right=.977, top=.75, bottom=.20)
    fig.text(.083, .923, "IRIS COLOR  /  REFERENCE SESSION 05", color="#51706d", fontsize=10.5, weight="bold")
    fig.text(.083, .856, "The same bass. A closer look at the tuned bands.", color="#233f46", fontsize=21, weight="bold")
    fig.text(.083, .804, "Color 150%  ·  D# minor pentatonic  ·  230–4,000 Hz  ·  original render levels",
             color="#5f6a66", fontsize=11)
    use = (f >= 340) & (f <= 3400)
    for hz in metrics.NOTE_HZ:
        if 340 < hz < 3400:
            ax.axvline(hz, color="#ddd9cb", lw=.65, zorder=0)
    for key in ["iris04", "chroma", "iris05"]:
        color, label, width, style = styles[key]
        ax.plot(f[use], curves[key][use], color=color, label=label, lw=width, ls=style)
    ax.axhline(0, color="#b1b7aa", lw=.8)
    ax.set_xscale("log")
    ax.set_xlim(340, 3400)
    ymin = min(-30., 5 * np.floor(min(float(curves[k][use].min()) for k in curves) / 5))
    ymax = max(15., 5 * np.ceil(max(float(curves[k][use].max()) for k in curves) / 5))
    ax.set_ylim(ymin, ymax)
    ticks = [340, 500, 700, 1000, 1500, 2000, 3000, 3400]
    ax.xaxis.set_major_locator(FixedLocator(ticks))
    ax.xaxis.set_major_formatter(FixedFormatter(["340", "500", "700", "1k", "1.5k", "2k", "3k", "3.4k"]))
    ax.minorticks_off()
    ax.tick_params(axis="both", colors="#5c6c69", labelsize=10)
    ax.set_xlabel("Frequency / Hz", labelpad=10, color="#4b615f")
    ax.set_ylabel("Gain relative to original input / dB", labelpad=12, color="#4b615f")
    ax.grid(axis="y", color="#e6e5dc", lw=.65)
    ax.spines["left"].set_color("#bfc9c1")
    ax.spines["bottom"].set_color("#bfc9c1")
    ax.legend(loc="lower left", frameon=True, facecolor="#fffdf7", edgecolor="#e1dfd3",
              fontsize=10, framealpha=.96, ncol=3)
    old = measured["iris04"]["holdout"]
    new = measured["iris05"]["holdout"]
    fig.text(.083, .086, f"Log-spectrum RMSE to this reference:  0.4  {old['log_spectral_rmse_db']:.2f} dB"
             f"     →     0.5  {new['log_spectral_rmse_db']:.2f} dB", color="#315d64", fontsize=11, weight="bold")
    fig.text(.083, .039, "Measured over 3.25–6.40 s using the same Welch PSD. Reference informed development;"
             " this is same-source validation, not an independent blind test.", color="#69736e", fontsize=9)
    path = out / "Reference05-Spectrum.png"
    fig.savefig(path, dpi=170, facecolor=fig.get_facecolor())
    plt.close(fig)
    return {"file": path.name, **file_info(path), "frequency_range_hz": [340, 3400],
            "region_seconds": list(REGIONS["holdout"]), "level_adjustment": "none",
            "source": "raw original-level stereo mean Welch PSD", "scope": SCOPE}


def listen_html(out: Path, artifacts: list[dict], measured: dict) -> dict:
    cards = []
    for index, item in enumerate(artifacts, 1):
        buttons = []
        for j, segment in enumerate(item["timeline"]):
            label = segment["label"]
            if item["key"] == "color" and segment["key"] == "iris05":
                label = "Color 150%"
            if item["key"] == "creative" and segment["key"] == "iris05":
                label = "新版基线"
            buttons.append(f'<button type="button" class="segment" data-player="audio-{index}" '
                           f'data-start="{segment["start_seconds"]:.12f}" data-end="{segment["end_seconds"]:.12f}" '
                           f'aria-pressed="false"><span class="segment-number">{j+1:02d}</span>'
                           f'<span>{html.escape(label)}</span><small>{segment["start_seconds"]:.2f}s</small></button>')
        filter_note = ('<p class="filter-note">测量试听：三段均经过同一套 300–3700 Hz 因果 Butterworth 带通。'
                       '用于集中听处理频段，不能当作插件全频输出。</p>' if item["measurement_filter"] else "")
        cards.append(
            f'<section class="track" id="{item["key"]}"><div class="track-heading"><div class="number">{index:02d}</div>'
            f'<div><h2>{html.escape(item["title"])}</h2><p>{html.escape(item["subtitle"])}</p></div>'
            f'<a class="download" href="{item["file"]}" download>下载 WAV <span aria-hidden="true">↓</span></a></div>'
            f'<div class="segments">{"".join(buttons)}</div>'
            f'<audio id="audio-{index}" controls preload="metadata" src="{item["file"]}">浏览器不能播放 PCM24 时，请下载 WAV 在 DAW 中试听。</audio>'
            f'<div class="track-foot"><span class="now" id="now-audio-{index}" aria-live="polite">尚未播放</span>'
            f'<span>组内共同增益 {item["common_gain_db"]:.2f} dB · {item["duration_seconds"]:.2f}s</span></div>{filter_note}</section>'
        )
    old, ref, new = (measured[k]["holdout"] for k in ["iris04", "chroma", "iris05"])
    rows = []
    for label, key in [("Iris 0.4", "iris04"), ("Chroma 参考", "chroma"), ("Iris 0.5 / Prism", "iris05")]:
        m = measured[key]["holdout"]
        rows.append(f'<tr><th scope="row">{label}</th><td>{m["log_spectral_rmse_db"]:.2f} dB</td>'
                    f'<td>{m["note_gap_contrast_db"]:.2f} dB</td><td>{m["midband_envelope_correlation"]:.3f}</td></tr>')
    page = r'''<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light">
<title>Iris Color 0.5 · 同源对照试听</title>
<style>
:root{--paper:#f4f1e8;--ink:#203d45;--muted:#647773;--line:#dce2d7;--teal:#2e6e79;--lilac:#85738e;--white:#fffef8}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--paper);color:var(--ink);font-family:"Avenir Next","Segoe UI","PingFang SC","Microsoft YaHei",sans-serif;font-size:16px;line-height:1.7}
body:before{content:"";position:absolute;z-index:-1;inset:0 0 auto;height:580px;background:radial-gradient(ellipse at 86% 15%,#c2d5ca99 0,transparent 47%),radial-gradient(ellipse at 6% 18%,#d2c8dd91 0,transparent 45%);pointer-events:none}
a{color:inherit}button,a,audio{outline-offset:5px}button:focus-visible,a:focus-visible{outline:3px solid #518b96}button{font:inherit}main{width:min(1120px,calc(100% - 48px));margin:0 auto}
.masthead{display:flex;align-items:center;justify-content:space-between;gap:18px;padding:29px 0;border-bottom:1px solid #bcc9bd70;font-size:12px;font-weight:650;letter-spacing:.14em}.brand{display:flex;gap:10px;align-items:center}.iris-mark{width:23px;height:23px;display:inline-block;border:1.5px solid #597e7b;border-radius:50%;background:conic-gradient(from 25deg,#a7c5b3,#d2bad1,#e6cdab,#a7c5b3)}.masthead span:last-child{color:var(--muted)}
.hero{padding:69px 0 44px;max-width:870px}.eyebrow{text-transform:uppercase;font-size:12px;font-weight:650;letter-spacing:.15em;color:#668782;margin:0 0 18px}.hero h1{font-size:clamp(31px,4.5vw,51px);font-weight:600;line-height:1.32;letter-spacing:-.035em;margin:0 0 22px}.hero h1 em{font-style:normal;color:var(--teal)}.intro{max-width:760px;color:var(--muted);font-size:16px;line-height:1.9;margin:0}
.settings{display:flex;flex-wrap:wrap;gap:8px;margin-top:24px}.settings span{border:1px solid #c7d3c8;background:#fafbf67a;padding:5px 12px;border-radius:20px;font-size:12px;color:#456a69;letter-spacing:.025em}
.guide{display:flex;align-items:start;gap:14px;border-left:3px solid #97aaa0;padding:3px 0 3px 18px;margin-bottom:28px;color:#596d69;font-size:13px}.guide p{margin:0}.guide strong{color:#34585d;font-weight:650}
.track{background:var(--white);border:1px solid #dce2d7;border-radius:19px;padding:27px 29px 22px;margin:18px 0;box-shadow:0 5px 20px #4f685509}.track-heading{display:flex;align-items:flex-start;gap:15px}.number{font-size:12px;letter-spacing:.09em;color:#88a299;padding-top:6px;width:24px;flex:none}.track h2{font-size:20px;letter-spacing:-.025em;font-weight:650;margin:0 0 5px;line-height:1.45}.track-heading p{font-size:13px;color:var(--muted);margin:0;max-width:740px}.download{margin-left:auto;white-space:nowrap;border:1px solid #d3dfd5;border-radius:8px;padding:6px 12px;text-decoration:none;font-size:12px;color:#507471;transition:background .15s}.download:hover{background:#ebf1e9}.download span{padding-left:5px}
.segments{display:flex;gap:8px;flex-wrap:wrap;margin:23px 0 15px}.segment{display:flex;align-items:center;gap:8px;background:#f4f6ef;border:1px solid #dce4d8;border-radius:9px;padding:8px 12px;color:#456b69;font-size:13px;cursor:pointer;transition:background .15s,border-color .15s;min-height:42px}.segment-number{color:#94a99e;font-size:10px}.segment small{font-size:10px;color:#82938b;margin-left:4px}.segment:hover{border-color:#84a49b;background:#edf3ec}.segment[aria-pressed="true"]{background:#e3eee7;border-color:#7ba497;color:#235f66}.segment[aria-pressed="true"] .segment-number{color:#386f75}
audio{display:block;width:100%;height:42px;accent-color:var(--teal);border-radius:9px}.track-foot{display:flex;justify-content:space-between;gap:15px;margin-top:10px;color:#88958b;font-size:11px}.now{color:#63877b}.filter-note{margin:14px 0 0;padding-top:12px;border-top:1px dashed #dce2d6;font-size:12px;color:#728477}
.section-title{font-size:24px;font-weight:600;letter-spacing:-.025em;margin:49px 0 10px}.measurement>p{color:var(--muted);font-size:14px;margin:0 0 20px}.figure{margin:0;background:#f5f1e8;border:1px solid #dce2d7;border-radius:15px;overflow:hidden}.figure img{display:block;width:100%;height:auto}.figure figcaption{padding:0 20px 18px;font-size:12px;color:#647872}
.table-wrap{overflow:auto;margin-top:20px;border:1px solid #dce2d7;border-radius:12px;background:#fafaf3}table{border-collapse:collapse;width:100%;font-size:13px;white-space:nowrap;text-align:left}th,td{padding:13px 18px;border-bottom:1px solid #e2e6dd}thead th{font-size:11px;font-weight:650;letter-spacing:.015em;color:#668178;background:#eef2e9}tbody th{font-weight:550}tbody tr:last-child th,tbody tr:last-child td{border:0}
details{margin:24px 0 0;border-top:1px solid #d6dfd2;padding-top:18px;color:#64766e;font-size:13px}summary{cursor:pointer;font-weight:600;color:#456b65}details p{margin:13px 0}details ul{padding-left:21px;margin:12px 0}details li{margin:7px 0}footer{display:flex;justify-content:space-between;gap:20px;border-top:1px solid #cdd8cc;margin-top:45px;padding:25px 0 38px;color:#839387;font-size:11px}
@media(max-width:720px){main{width:calc(100% - 28px)}.masthead{padding:20px 0;font-size:10px;letter-spacing:.08em}.hero{padding:44px 0 30px}.intro{font-size:14px}.track{padding:21px 17px 18px}.number{display:none}.track h2{font-size:18px}.track-heading{flex-wrap:wrap;gap:12px}.download{margin-left:0;font-size:11px}.track-heading>div:not(.number){flex:1;min-width:220px}.segments{gap:7px}.segment{font-size:12px;padding:7px 9px}.track-foot{font-size:10px}.track-foot span:last-child{text-align:right}.section-title{font-size:22px}.hero h1{letter-spacing:-.04em}footer{flex-direction:column;gap:5px}.figure{border-radius:9px}}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}*{transition:none!important}}
</style>
</head>
<body><main>
<header class="masthead"><div class="brand"><i class="iris-mark" aria-hidden="true"></i>IRIS COLOR <span>0.5 / PRISM</span></div><span>REFERENCE SESSION 05</span></header>
<div class="hero"><p class="eyebrow">同一素材 · 原始电平关系 · 可直接切换</p>
<h1>同一段 Bass，<br><em>重新听染色。</em></h1>
<p class="intro">把原音、0.4、你提供的 Chroma 参考与新版 Prism 放在同一条时间线上。先听整体，再单独听音阶频段；最后比较 Color 强度与五个创意处理。</p>
<div class="settings"><span>COLOR 150%</span><span>D# MINOR PENTATONIC</span><span>230–4,000 Hz</span><span>44.1 kHz / 24-bit / Stereo</span></div></div>
<div class="guide"><p><strong>点选片段即可从该段播放。</strong> 每段 6.804 秒，间隔 0.35 秒。组内只使用一个共同衰减，未逐段响度归一；参数带来的实际电平差异也被保留。页面不会自动播放，同时只播放一条音轨。</p></div>
__CARDS__
<section class="measurement"><h2 class="section-title">频谱上的变化，也留给你核对。</h2>
<p>下图使用未做试听衰减的原始输出，显示相对同一干声的增益。竖线对应所选音阶；频谱起伏和尾音包络只是描述指标，不能直接换算成“相似百分比”。</p>
<figure class="figure"><img src="Reference05-Spectrum.png" alt="Chroma参考、Iris0.4与Iris0.5在340至3400Hz的相对干声频谱增益对照" loading="lazy"><figcaption>统一取 3.25–6.40 秒，使用同一组 Welch 功率谱参数。无额外曲线电平匹配。</figcaption></figure>
<div class="table-wrap"><table><thead><tr><th scope="col">输出</th><th scope="col">相对参考的频谱 RMSE</th><th scope="col">音阶峰 / 间隙差</th><th scope="col">中频包络相关性</th></tr></thead><tbody>__ROWS__</tbody></table></div>
<details><summary>参数与比较方法</summary>
<p>新版基线：Prism、D# 小调五声音阶、Color 150%、Snap 100%、230–4000 Hz、Decay 200、Width 100%、Output 0 dB；Morph、Gate、Transient、软限幅和全部创意效果关闭，Color Bands 与 EQ 为中性。0.4 对照使用原版 0.4 renderer 和相同参数。</p>
<ul><li>Attack / Tail：5% → 175%，100 ms。</li><li>Pitch Dive：+12 半音，160 ms。</li><li>Scale Motion：Depth 100%，1/8 音符，150 BPM。</li><li>Formant：Mix 90%，Vowel 2.6，Motion 45%，Rate 0.7 Hz。</li><li>Peak Tamer：Amount 75%，最大抑制 8 dB。</li></ul>
<p>全部新版渲染由同一冻结的 DSP 生成，renderer 已移除固定 3072 样本延迟。每段都保留 0.404172 秒尾音；原音尾部补零。试听仅施加组内共同增益和 PCM24 量化，没有逐段归一、淡入淡出或新增限幅；不改变参考中原有的波形失真。</p>
<p>这条参考参与了开发与参数研究，后半段也已在迭代中观察过。因此这里是同一素材的保留片段验证，不是独立素材泛化结果，也不是盲测。Chroma 的上传设置为 Color 150%、230–4000 Hz、High Quality；其余未提供的状态不能从音频中完整确认。</p>
<p>需要重现时，阅读同目录的 Reproduce_zh.md。原始 float 渲染、精确帧数、参数、命令、波形与二进制哈希保存在项目 build/reference/reference05-audio.json 的记录中。</p>
</details></section>
<footer><span>IRIS COLOR · Independent color bass processor</span><span>本页面与音频可离线使用，无外部字体、脚本或统计请求。</span></footer>
</main>
<script>
const players = Array.from(document.querySelectorAll("audio"));
const buttons = Array.from(document.querySelectorAll("button.segment"));
players.forEach(function (player) {
  player.addEventListener("play", function () {players.forEach(function (other) {if (other !== player) other.pause();});});
  function update() {
    let selected = null;
    buttons.filter(function (button) {return button.dataset.player === player.id;}).forEach(function (button) {
      const active = player.currentTime >= Number(button.dataset.start) && player.currentTime < Number(button.dataset.end);
      button.setAttribute("aria-pressed", active && !player.paused ? "true" : "false");
      if (active) selected = button;
    });
    const status = document.getElementById("now-" + player.id);
    if (player.ended) status.textContent = "播放结束";
    else if (selected && !player.paused) status.textContent = "正在播放 · " + selected.children[1].textContent;
    else if (player.currentTime > 0) status.textContent = player.paused ? "已暂停" : "片段间隔";
  }
  ["timeupdate","play","pause","ended"].forEach(function (event) {player.addEventListener(event, update);});
  player.addEventListener("error", function () {
    document.getElementById("now-" + player.id).textContent = "此浏览器无法解码 PCM24；请使用下载按钮，在 DAW 中打开。";
  });
});
buttons.forEach(function (button) {
  button.addEventListener("click", function () {
    const player = document.getElementById(button.dataset.player);
    function seekAndPlay() {
      player.currentTime = Number(button.dataset.start);
      const started = player.play();
      if (started && started.catch) started.catch(function () {
        document.getElementById("now-" + player.id).textContent = "请点击播放器的播放键，或下载 WAV 试听。";
      });
    }
    if (player.readyState >= 1) seekAndPlay();
    else {player.addEventListener("loadedmetadata", seekAndPlay, {once:true}); player.load();}
  });
});
</script></body></html>'''
    page = page.replace("__CARDS__", "\n".join(cards)).replace("__ROWS__", "\n".join(rows))
    path = out / "Listen.html"
    path.write_text(page, encoding="utf-8")
    return {"file": path.name, **file_info(path), "external_dependencies": [], "autoplay_on_load": False}


def write_reproduction(out: Path) -> dict:
    text = """# Iris Color 0.5 — 同源对比试听

双击 Listen.html 可离线播放；也可以把同目录的 PCM24 WAV 拖入 DAW。
每段恰好 300064 帧（6.8041723356 秒），段间 15435 帧（0.35 秒）。
每个 montage 使用一个共同增益，至少衰减 6 dB，导出采样峰值不超过 0.89。
没有逐段响度归一，也没有为单个片段额外补偿音量。

## 文件

- 01-Iris-Color05-Full.wav：新版 Prism 全频输出。
- 02-Dry-Color04-Chroma-Color05.wav：原音 → 原版 Iris 0.4 → 上传的 Chroma 参考 → 新版 Iris 0.5。
- 03-Color04-Chroma-Color05-Band300-3700.wav：0.4 → Chroma → 0.5，均使用相同的 300–3700 Hz 测量带通。
- 04-Color100-150-200.wav：只改变新版 Color 参数，依次 100%、150%、200%。
- 05-Creative-Effects.wav：新版基线 → Attack/Tail → Dive → Motion → Formant → Tamer。
- Reference05-Spectrum.png：未衰减原始输出相对原音的频谱增益。

## 重现所需文件

需要 Python 3.11 或更新版本以及 NumPy、SciPy、Matplotlib。
另外需要原始 dry WAV、上传的 Chroma reference WAV、Iris 0.4 的原版 iris-render
可执行文件，以及从本项目冻结源码构建出的 Iris 0.5 iris-render。
两个用户输入音频不额外复制到源码目录；请通过参数指向你持有的原文件。
本组验证固定为 44100 Hz 立体声；dry 282240 帧，reference 300064 帧。

在 Iris 0.5 项目根目录运行：

    python3 tools/make_reference05_demo.py \\
      --dry /path/to/original-dry.wav \\
      --reference /path/to/chroma-reference.wav \\
      --legacy-renderer /path/to/iris-color-04/iris-render \\
      --renderer build/linux/iris-render

如 0.4 二进制不在原源码树的 build/linux 目录，还应传入：

    --legacy-project /path/to/iris-color-04-source

默认交付目录为 audio/reference05；原始 float32 渲染只写入
build/reference/reference05-raw，完整收据为 build/reference/reference05-audio.json。
可用 --out / --work / --receipt 修改这些位置。导出后可独立复读检查：

    python3 tools/make_reference05_demo.py --verify-only

脚本使用同项目 tools/reference05_metrics.py，不依赖临时分析目录。
输入、renderer、实际 DSP / renderer include 文件、脚本、输出都有 SHA-256 记录。
源码或二进制在导出期间变化会令导出失败。

## 比较方法

基线为 D# minor pentatonic、Color150、Snap100、230–4000 Hz、Decay200、
Width100、Output0、Morph/Gate/Transient/soft-limit/创意功能关闭，中性 Color Bands 和 EQ。
0.4 使用原版可执行文件和相同参数。0.5 使用新 Prism 核心。
renderer 已剔除 3072 样本固定延迟；脚本不会再移除一次。
输入干声补零到统一段长；Chroma 参考不经过重渲染。

Band300-3700 是辅助测量试听：三段分别从零状态经过同一因果 Butterworth
带通（SciPy butter(4, [300,3700], btype='bandpass')，八阶传递函数），
再施加组内共同增益。这个带通不属于插件，也不是插件全频输出。

后半段指标固定取 3.25–6.4 秒，频谱比较采用未做试听衰减的原始输出。
同一条参考参与了算法开发与尾音参数研究；后半段也曾被迭代观察。
因此这些结果只能称为同一素材的保留片段验证，不能称为独立盲测，
也不能把 RMSE、峰谷差或相关系数直接当作感知相似百分比。
"""
    path = out / "Reproduce_zh.md"
    path.write_text(text, encoding="utf-8")
    return {"file": path.name, **file_info(path)}


def verify_artifacts(receipt: dict, out: Path) -> dict:
    """Independent on-disk PCM parser checks structure, hashes, gaps and clipping."""
    rows = []
    for item in receipt["artifacts"]:
        path = out / item["file"]
        if sha(path) != item["sha256"]:
            raise AssertionError(f"WAV hash mismatch: {path}")
        with wave.open(str(path), "rb") as stream:
            if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(),
                stream.getnframes(), stream.getcomptype()) != (2, 3, FS, item["frames"], "NONE"):
                raise AssertionError(f"WAV header mismatch: {path}")
            packed = np.frombuffer(stream.readframes(stream.getnframes()), np.uint8).reshape(-1, 3)
        words = (packed[:, 0].astype(np.int32) | (packed[:, 1].astype(np.int32) << 8)
                 | (packed[:, 2].astype(np.int32) << 16))
        signed = ((words ^ 0x800000) - 0x800000).reshape(-1, 2)
        restored = signed.astype(np.float64) / PCM_SCALE
        info = level_info(restored)
        if info["sample_peak"] > PEAK_CEILING or np.any(signed == -PCM_SCALE) or np.any(signed == PCM_SCALE - 1):
            raise AssertionError(f"PCM sample clipping: {path}")
        expected = len(item["timeline"]) * SEGMENT_FRAMES + (len(item["timeline"]) - 1) * GAP_FRAMES
        if expected != len(restored):
            raise AssertionError("Montage frame count differs from exact timeline.")
        for index, segment in enumerate(item["timeline"]):
            start = index * (SEGMENT_FRAMES + GAP_FRAMES)
            if segment["start_frame"] != start or segment["end_frame_exclusive"] != start + SEGMENT_FRAMES:
                raise AssertionError("Segment boundaries disagree with frame arithmetic.")
            if segment["common_playback_gain"] != item["common_gain"]:
                raise AssertionError("Per-segment playback gain detected.")
            if index < len(item["timeline"]) - 1 and np.any(signed[start + SEGMENT_FRAMES:start + SEGMENT_FRAMES + GAP_FRAMES]):
                raise AssertionError("Inter-segment gap is not digital silence.")
        rows.append({"file": item["file"], "sha256_matches": True, "format_checked": True,
                     "frames": len(restored), "finite": True, "no_new_sample_clipping": True,
                     "exact_timeline_and_silent_gaps": True, **info})
    for key in ["spectrum", "listen_page", "reproduction_instructions"]:
        item = receipt[key]
        if sha(out / item["file"]) != item["sha256"]:
            raise AssertionError(f"Non-audio artifact hash mismatch: {item['file']}")
    class PageParser(HTMLParser):
        def __init__(self):
            super().__init__()
            self.audio = {}
            self.buttons = []
            self.references = []
        def handle_starttag(self, tag, attributes):
            a = dict(attributes)
            if tag == "audio":
                if "autoplay" in a:
                    raise AssertionError("Offline page enables autoplay.")
                self.audio[a["id"]] = a["src"]
            if tag == "button" and "data-player" in a:
                self.buttons.append(a)
            for attr in ("src", "href"):
                if attr in a:
                    self.references.append(a[attr])
    page = PageParser()
    page.feed((out / "Listen.html").read_text(encoding="utf-8"))
    if len(page.audio) != 5 or len(page.buttons) != 17:
        raise AssertionError("Offline player count or segment-button count differs.")
    for value in page.references:
        if re.match(r"(?:https?:)?//|data:|javascript:", value):
            raise AssertionError("External resource or unsafe URI in offline page.")
        if not (out / value).is_file():
            raise AssertionError(f"Offline page resource missing: {value}")
    for button in page.buttons:
        filename = page.audio[button["data-player"]]
        item = next(a for a in receipt["artifacts"] if a["file"] == filename)
        starts = [s["start_seconds"] for s in item["timeline"]]
        if min(abs(float(button["data-start"]) - s) for s in starts) > .1 / FS:
            raise AssertionError("Offline segment jump differs from audio timeline.")
    return {"passed": True, "method": "fresh on-disk PCM24 byte decoding and HTMLParser link/timeline checks",
            "waves": rows, "non_audio_hashes_match": True, "offline_page_resources_exist": True,
            "offline_page_audio_count": len(page.audio), "offline_page_segment_buttons": len(page.buttons),
            "offline_page_has_no_external_resource": True, "autoplay_on_load": False}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry", type=Path, default=WORKSPACE / "upload/BOS_MCB_150_Bass_Synth_Loop_Healing_D#m.wav")
    parser.add_argument("--reference", type=Path, default=WORKSPACE / "upload/untitled.wav")
    parser.add_argument("--renderer", type=Path, default=PROJECT / "build/linux/iris-render")
    parser.add_argument("--legacy-renderer", type=Path, default=WORKSPACE / "iris-color-04/build/linux/iris-render")
    parser.add_argument("--legacy-project", type=Path, default=WORKSPACE / "iris-color-04")
    parser.add_argument("--out", type=Path, default=PROJECT / "audio/reference05")
    parser.add_argument("--work", type=Path, default=PROJECT / "build/reference/reference05-raw")
    parser.add_argument("--receipt", type=Path, default=PROJECT / "build/reference/reference05-audio.json")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    for name in ["dry", "reference", "renderer", "legacy_renderer", "legacy_project", "out", "work", "receipt"]:
        setattr(args, name, getattr(args, name).expanduser().resolve())
    if args.verify_only:
        receipt = json.loads(args.receipt.read_text())
        print(json.dumps(verify_artifacts(receipt, args.out), indent=2, ensure_ascii=False, allow_nan=False))
        return
    args.out.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    inputs = {"dry": file_info(args.dry), "chroma_reference": file_info(args.reference)}
    renderers = {"iris05": file_info(args.renderer), "iris04": file_info(args.legacy_renderer)}
    source_hashes = {"iris05": dependency_hashes(PROJECT), "iris04": dependency_hashes(args.legacy_project)}
    script_hashes = {path.name: sha(path) for path in [Path(__file__).resolve(), Path(metrics.__file__).resolve()]}
    dry = read_stereo(args.dry, SOURCE_FRAMES)
    reference = read_stereo(args.reference, SEGMENT_FRAMES)
    arrays = {"dry": np.pad(dry, ((0, SEGMENT_FRAMES - SOURCE_FRAMES), (0, 0))), "chroma": reference}
    inputs["dry"].update({"frames": len(dry), "sample_rate": FS, **level_info(dry)})
    inputs["chroma_reference"].update({"frames": len(reference), "sample_rate": FS, **level_info(reference)})
    records = {}
    for recipe in RECIPES:
        arrays[recipe[0]], records[recipe[0]] = render_recipe(recipe, args, args.work)
    measured = {}
    for key, data in arrays.items():
        measured[key] = {name: metrics.evaluate(data, dry, reference, FS, region)
                         for name, region in REGIONS.items()}
    comparison = montage(args.out, "02-Dry-Color04-Chroma-Color05.wav", "comparison",
                         "全频对照：原音、0.4、Chroma、新版", "同一个乐句，依次比较低频轮廓、音阶染色与尾音。",
                         ["dry", "iris04", "chroma", "iris05"], arrays)
    standalone = montage(args.out, "01-Iris-Color05-Full.wav", "standalone", "新版 Prism，先单独听一次",
                         "Color 150% 的完整频段输出；使用与下一组全频对照完全相同的播放增益。",
                         ["iris05"], arrays, gain=comparison["common_gain"])
    sos = signal.butter(4, [300, 3700], btype="bandpass", fs=FS, output="sos")
    filtered = {key: signal.sosfilt(sos, arrays[key], axis=0) for key in ["iris04", "chroma", "iris05"]}
    filter_record = {"method": "scipy.signal.butter(4, [300, 3700], btype='bandpass', fs=44100, output='sos')",
                     "application": "sosfilt, causal, independent zero state per complete segment, identical coefficients",
                     "transfer_function_order": 8, "cutoffs_hz": [300, 3700], "sos": sos.tolist(),
                     "scope": "Measurement audition filter only; not part of the plugin or full-band output."}
    band = montage(args.out, "03-Color04-Chroma-Color05-Band300-3700.wav", "band",
                   "集中听染色：300–3700 Hz", "只对处理频段做同源测量试听，顺序为 0.4 → Chroma → 新版。",
                   ["iris04", "chroma", "iris05"], filtered, measurement_filter=filter_record)
    color = montage(args.out, "04-Color100-150-200.wav", "color", "Color 100 → 150 → 200",
                    "只改变 Color。注意音阶峰的集中程度、频率间隙与共振尾音；电平变化一并保留。",
                    ["color100", "iris05", "color200"], arrays)
    creative = montage(args.out, "05-Creative-Effects.wav", "creative", "五个创意处理，逐个听",
                       "每一段都从 Color 150% 基线出发，只启用对应的一个功能，不逐层叠加。",
                       ["iris05", "attack_tail", "dive", "motion", "formant", "tamer"], arrays)
    artifacts = [standalone, comparison, band, color, creative]
    spectrum = spectrum_figure(args.out, arrays, dry, measured)
    listen = listen_html(args.out, artifacts, measured)
    reproduction = write_reproduction(args.out)
    if inputs["dry"]["sha256"] != sha(args.dry) or inputs["chroma_reference"]["sha256"] != sha(args.reference):
        raise AssertionError("An input changed during export.")
    if renderers["iris05"]["sha256"] != sha(args.renderer) or renderers["iris04"]["sha256"] != sha(args.legacy_renderer):
        raise AssertionError("A renderer changed during export.")
    if source_hashes != {"iris05": dependency_hashes(PROJECT), "iris04": dependency_hashes(args.legacy_project)}:
        raise AssertionError("A renderer/DSP source dependency changed during export.")
    if script_hashes != {path.name: sha(path) for path in [Path(__file__).resolve(), Path(metrics.__file__).resolve()]}:
        raise AssertionError("An export or metric script changed during export.")
    feature_deltas = {}
    baseline = arrays["iris05"][:SOURCE_FRAMES]
    baseline_mid = filtered["iris05"][:SOURCE_FRAMES]
    baseline_mid_rms = float(np.sqrt(np.mean(baseline_mid ** 2)))
    for key in ["color100", "color200", "attack_tail", "dive", "motion", "formant", "tamer"]:
        y = arrays[key][:SOURCE_FRAMES]
        y_mid = signal.sosfilt(sos, arrays[key], axis=0)[:SOURCE_FRAMES]
        y_mid_rms = float(np.sqrt(np.mean(y_mid ** 2)))
        mid_difference = float(np.sqrt(np.mean((y_mid - baseline_mid) ** 2)) / max(baseline_mid_rms, 1e-30))
        coefficient = float(np.sum(baseline * y) / max(np.sum(baseline * baseline), 1e-30))
        feature_deltas[key] = {
            "raw_difference_rms_relative_to_baseline": float(np.sqrt(np.mean((y - baseline) ** 2) / np.mean(baseline ** 2))),
            "best_scalar_y_from_baseline": coefficient,
            "residual_after_best_scalar_relative_to_y": float(np.sqrt(np.mean((y - coefficient * baseline) ** 2) / np.mean(y ** 2))),
            "midband_frequency_hz": [300, 3700], "midband_window_seconds": [0, SOURCE_FRAMES / FS],
            "midband_difference_rms_relative_to_baseline": mid_difference,
            "midband_difference_db_relative_to_baseline": db(mid_difference),
            "midband_rms_gain_db": db(y_mid_rms / max(baseline_mid_rms, 1e-30)),
            "midband_baseline_rms": baseline_mid_rms, "midband_variant_rms": y_mid_rms,
            "midband_filter": "identical causal Butterworth measurement filter, applied before windowing",
            "scope": "Signal difference only; neither a quality score nor loudness normalization.",
        }
    receipt = {
        "version": "0.5.0", "created_utc": datetime.now(timezone.utc).isoformat(), "scope": SCOPE,
        "provenance": {"dry": "User-provided BOS bass loop; not claimed as original Iris composition.",
                       "chroma_reference": "User-provided commercial-plugin render, reproduced only in labeled comparisons.",
                       "iris_outputs": "Generated from the same dry input by the respective Iris DSP renderer.",
                       "reference_used_as_iris_dsp_input": False},
        "inputs": inputs, "renderers": renderers, "source_sha256": source_hashes, "script_sha256": script_hashes,
        "sources_and_renderers_unchanged_through_run": True, "sample_rate": FS, "source_frames": SOURCE_FRAMES,
        "segment_frames": SEGMENT_FRAMES, "segment_seconds": SEGMENT_FRAMES / FS,
        "gap_frames": GAP_FRAMES, "gap_seconds": GAP_FRAMES / FS,
        "latency": {"reported_samples": 3072, "removed_by": "renderer default", "additional_script_shift": 0},
        "raw_render_directory": str(args.work), "raw_renders": records,
        "metric_definitions": {"module": "tools/reference05_metrics.py", "region_seconds": REGIONS,
            "holdout_label_meaning": "retained interval of the same already-inspected pair, not untouched data",
            "frequency_range_hz": [340, 3400], "level_matching_before_measurement": False,
            "welch": {"nperseg": 16384, "noverlap": 12288, "nfft": 65536, "stereo_power_average": True},
            "gain_error_grid": "1200 log-spaced frequencies, 340--3400 Hz",
            "envelope_filter": "same scipy butter(4,[300,3700],bandpass), 512-sample RMS, stride128"},
        "metrics": measured, "feature_signal_deltas": feature_deltas,
        "audition_policy": {"minimum_common_attenuation_db": MINIMUM_ATTENUATION_DB,
            "sample_peak_ceiling": PEAK_CEILING, "gain_rule": "one minimum(-6dB, safety ceiling / max group peak) per montage",
            "standalone_gain": "identical to four-way full-band comparison", "per_segment_normalization": False,
            "resampling": False, "edge_fades": False, "limiting": False, "true_peak_claim": False,
            "dry_extension": "zero padding only", "reference_editing": "common playback gain and PCM24 quantization only"},
        "artifacts": artifacts, "spectrum": spectrum, "listen_page": listen, "reproduction_instructions": reproduction,
    }
    receipt["verification"] = verify_artifacts(receipt, args.out)
    receipt["passed"] = receipt["verification"]["passed"]
    args.receipt.write_text(json.dumps(receipt, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    # Read the serialized receipt, then verify its exact on-disk targets again.
    verified = verify_artifacts(json.loads(args.receipt.read_text()), args.out)
    print(json.dumps({"passed": verified["passed"], "receipt": str(args.receipt), "out": str(args.out),
                      "audio_bytes": sum(a["size_bytes"] for a in artifacts),
                      "common_gains_db": {a["key"]: a["common_gain_db"] for a in artifacts},
                      "retained_segment": {k: measured[k]["holdout"] for k in ["iris04", "chroma", "iris05"]}},
                     indent=2, ensure_ascii=False, allow_nan=False), flush=True)


if __name__ == "__main__":
    main()
