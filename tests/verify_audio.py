#!/usr/bin/env python3
"""Independent, black-box acceptance checks on iris-render's actual WAV output.

Requires Python 3.10+, NumPy and SciPy. No Iris implementation code is imported.
Run from any directory:
    python tests/verify_audio.py --renderer build/iris-render

Acceptance limits describe this independent implementation. They are not claims
of equivalence to Chroma or of compatibility with a particular VST3 host.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from typing import Callable

import numpy as np
from scipy import fft, signal
from scipy.io import wavfile


PROJECT = Path(__file__).resolve().parents[1]
SAMPLE_RATES = (44100, 48000, 96000)
F4 = 440.0 * 2.0 ** ((65 - 69) / 12.0)
E4 = 440.0 * 2.0 ** ((64 - 69) / 12.0)
PURE = [
    "--preset", "clean", "--root", "E", "--scale", "minor-pent",
    "--color", "100", "--snap", "100", "--morph-off", "--gate-off",
    "--transient", "0", "--width", "100", "--output", "0", "--no-limit",
    "--low", "80", "--high", "8000",
]


def rms(audio: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.asarray(audio, dtype=np.float64) ** 2)))


def db(value: float) -> float:
    return float(20.0 * np.log10(max(float(value), 1.0e-15)))


def fixture(sample_rate: int, seconds: float = 0.8) -> np.ndarray:
    """Unequal stereo channels expose channel swaps, delay and lost samples."""
    count = round(seconds * sample_rate)
    t = np.arange(count, dtype=np.float64) / sample_rate
    rng = np.random.default_rng(771013)
    left = (0.10 * np.sin(2 * np.pi * 137.3 * t)
            + 0.045 * np.sin(2 * np.pi * 1251.7 * t + 0.3)
            + 0.022 * rng.standard_normal(count))
    right = (0.065 * np.sin(2 * np.pi * 217.1 * t + 0.47)
             + 0.052 * np.sin(2 * np.pi * 2813.4 * t)
             + 0.026 * rng.standard_normal(count))
    fade_count = min(round(0.025 * sample_rate), count // 4)
    fade = np.sin(np.linspace(0.0, np.pi / 2.0, fade_count)) ** 2
    envelope = np.ones(count)
    envelope[:fade_count] = fade
    envelope[-fade_count:] = fade[::-1]
    return np.column_stack((left, right)).astype(np.float32) * envelope[:, None].astype(np.float32)


def sine(sample_rate: int, frequency: float = F4, seconds: float = 2.4) -> np.ndarray:
    t = np.arange(round(seconds * sample_rate), dtype=np.float64) / sample_rate
    x = 0.2 * np.sin(2 * np.pi * frequency * t)
    ramp_count = round(0.035 * sample_rate)
    x[:ramp_count] *= np.sin(np.linspace(0.0, np.pi / 2.0, ramp_count)) ** 2
    x[-ramp_count:] *= np.sin(np.linspace(np.pi / 2.0, 0.0, ramp_count)) ** 2
    return np.column_stack((x, x)).astype(np.float32)


def spectrum_measurement(audio: np.ndarray, sample_rate: int) -> dict:
    """Steady-state Hann analysis, with interpolation independent of DSP bins."""
    x = np.asarray(audio[round(0.75 * sample_rate):round(2.15 * sample_rate), 0], dtype=np.float64)
    x = x - np.mean(x)
    window = signal.windows.hann(len(x), sym=False)
    nfft = fft.next_fast_len(8 * len(x))
    mag = np.abs(fft.rfft(x * window, n=nfft))
    freqs = fft.rfftfreq(nfft, 1.0 / sample_rate)
    lo = int(np.searchsorted(freqs, 250.0))
    hi = int(np.searchsorted(freqs, 450.0))
    peak = lo + int(np.argmax(mag[lo:hi]))
    log_three = np.log(np.maximum(mag[peak - 1:peak + 2], 1.0e-30))
    denominator = log_three[0] - 2.0 * log_three[1] + log_three[2]
    correction = 0.0 if abs(denominator) < 1.0e-20 else 0.5 * (log_three[0] - log_three[2]) / denominator
    peak_hz = float((peak + np.clip(correction, -0.5, 0.5)) * sample_rate / nfft)
    target = float(np.sum(mag[np.abs(freqs - E4) <= 2.0] ** 2))
    original = float(np.sum(mag[np.abs(freqs - F4) <= 2.0] ** 2))
    return {
        "dominant_frequency_hz": peak_hz,
        "target_over_original_db": float(10.0 * np.log10(max(target, 1.0e-30) / max(original, 1.0e-30))),
        "steady_state_rms": rms(x),
    }


class Case:
    def __init__(self, name: str, purpose: str):
        self.name = name
        self.purpose = purpose
        self.assertions: list[dict] = []
        self.measurements: dict = {}

    def expect(self, condition: bool, name: str, actual, requirement: str) -> None:
        self.assertions.append({
            "name": name,
            "passed": bool(condition),
            "actual": actual,
            "requirement": requirement,
        })


class Renderer:
    def __init__(self, executable: Path, work: Path):
        self.executable = executable
        self.work = work
        self.calls: list[dict] = []

    def render(self, x: np.ndarray, sample_rate: int, arguments: list[str], *, keep_latency: bool = False) -> np.ndarray:
        stem = f"render_{len(self.calls):03d}"
        source = self.work / f"{stem}_input.wav"
        destination = self.work / f"{stem}_output.wav"
        wavfile.write(source, sample_rate, np.asarray(x, dtype=np.float32))
        command = [str(self.executable), str(source), str(destination), *arguments, "--tail", "0"]
        if keep_latency:
            command.append("--keep-latency")
        begin = time.perf_counter()
        completed = subprocess.run(command, capture_output=True, text=True, timeout=90)
        elapsed = time.perf_counter() - begin
        call = {
            "arguments": arguments + ["--tail", "0"] + (["--keep-latency"] if keep_latency else []),
            "sample_rate": sample_rate,
            "input_frames": len(x),
            "elapsed_seconds": elapsed,
            "return_code": completed.returncode,
            "stdout": completed.stdout.strip(),
            "stderr": completed.stderr.strip(),
        }
        self.calls.append(call)
        if completed.returncode != 0:
            raise RuntimeError(f"iris-render exited {completed.returncode}: {completed.stderr.strip() or completed.stdout.strip()}")
        result_rate, result = wavfile.read(destination)
        if result_rate != sample_rate:
            raise RuntimeError(f"WAV sample rate changed: {sample_rate} -> {result_rate}")
        if result.ndim != 2 or result.shape[1] != 2 or result.dtype != np.dtype("float32"):
            raise RuntimeError(f"Expected stereo FLOAT32 WAV, received {result.shape} / {result.dtype}")
        if not np.all(np.isfinite(result)):
            raise RuntimeError("Rendered WAV contains NaN or infinity")
        if not keep_latency and len(result) != len(x):
            raise RuntimeError(f"Latency-trimmed --tail 0 length changed: {len(x)} -> {len(result)}")
        call["output_frames"] = len(result)
        call["peak"] = float(np.max(np.abs(result))) if result.size else 0.0
        return result


def build_cases(renderer: Renderer) -> list[tuple[str, str, Callable[[Case], None]]]:
    cases: list[tuple[str, str, Callable[[Case], None]]] = []

    for rate in SAMPLE_RATES:
        def frequency(case: Case, fs=rate) -> None:
            y = renderer.render(sine(fs), fs, PURE)
            measurements = spectrum_measurement(y, fs)
            case.measurements.update(measurements)
            peak = measurements["dominant_frequency_hz"]
            dominance = measurements["target_over_original_db"]
            case.expect(abs(peak - E4) <= 0.8, "F4 moves to E4", peak, f"within 0.8 Hz of {E4:.9f} Hz")
            case.expect(dominance >= 18.0, "Target replaces the original frequency", dominance, "target/source band energy ratio >= 18 dB")
            case.expect(measurements["steady_state_rms"] >= 0.01, "Tuning does not merely silence input", measurements["steady_state_rms"], "RMS >= 0.01")
        cases.append((f"frequency_{rate}", "Catch a non-tuning EQ, failed phase advance, or inaccurate pitch remapping.", frequency))

    for name, extra in (("bypass_identity", ["--bypass"]), ("color_zero_identity", ["--color", "0"])):
        def identity(case: Case, options=extra) -> None:
            x = fixture(48000)
            y = renderer.render(x, 48000, PURE + options)
            maximum = float(np.max(np.abs(y - x)))
            case.measurements["maximum_absolute_error"] = maximum
            case.expect(maximum <= 2.0e-7, "Unity samples after latency compensation", maximum, "maximum absolute error <= 2e-7")
        cases.append((name, "Catch gain changes, channel mistakes or dry-path delay mistakes.", identity))

    def neutral(case: Case) -> None:
        x = fixture(48000)
        y = renderer.render(x, 48000, PURE + ["--snap", "0"])
        error = y - x
        case.measurements.update(maximum_absolute_error=float(np.max(np.abs(error))), residual_rms=rms(error))
        case.expect(case.measurements["maximum_absolute_error"] <= 1.0e-4, "Neutral WOLA reconstructs original audio", case.measurements["maximum_absolute_error"], "maximum absolute error <= 1e-4 over entire rendered file")
    cases.append(("neutral_reconstruction", "Catch FFT window normalization, boundary or matched-delay faults.", neutral))

    for core, spectral in ((2048, 1024), (4096, 512)):
        def latency(case: Case, n=core, s=spectral) -> None:
            x = np.zeros((12000, 2), dtype=np.float32)
            x[137] = (0.5, -0.25)
            y = renderer.render(x, 48000, PURE + ["--bypass", "--fft", str(n), "--spectral", str(s)], keep_latency=True)
            location = int(np.argmax(np.abs(y[:, 0])))
            measured = location - 137
            expected = n + s
            case.measurements.update(measured_latency_samples=measured, expected_latency_samples=expected, output_frames=len(y))
            case.expect(measured == expected, "Measured unity-path latency", measured, f"exactly {expected} samples")
            if measured >= 0 and len(y) >= measured + len(x):
                residual = float(np.max(np.abs(y[measured:measured + len(x)] - x)))
                case.expect(residual <= 2.0e-7, "Aligned delayed impulse preserves both channels", residual, "maximum absolute error <= 2e-7")
            else:
                case.expect(False, "Untrimmed output contains full input plus delay", len(y), f"at least {len(x) + expected} frames")
        cases.append((f"latency_{core}_{spectral}", "Catch incorrect delay compensation and mismatched declared FFT latency.", latency))

    def blocks(case: Case) -> None:
        x = fixture(48000, 0.55)
        arguments = ["--preset", "glass", "--root", "E", "--scale", "minor-pent", "--output", "0"]
        reference = renderer.render(x, 48000, arguments + ["--block", "512"])
        for block in (1, 127, 0):
            y = renderer.render(x, 48000, arguments + ["--block", str(block)])
            error = float(np.max(np.abs(reference - y)))
            case.expect(error <= 1.0e-6, f"Block {block} agrees with 512", error, "maximum absolute difference <= 1e-6; 0 means irregular deterministic blocks")
    cases.append(("block_size_independence", "Catch processing that changes when hosts divide audio into different blocks.", blocks))

    for rate in SAMPLE_RATES:
        def silence(case: Case, fs=rate) -> None:
            x = np.zeros((round(0.35 * fs), 2), dtype=np.float32)
            y = renderer.render(x, fs, PURE + ["--color", "200", "--decay", "1500", "--output", "12"])
            peak = float(np.max(np.abs(y)))
            case.expect(peak <= 1.0e-12, "Silence stays silent at maximum resonance", peak, "peak <= 1e-12, all samples finite")
        cases.append((f"silence_{rate}", "Catch self-oscillation, numerical instability or uncleared state.", silence))

    for rate in SAMPLE_RATES:
        def rates(case: Case, fs=rate) -> None:
            x = fixture(fs, 0.65) * 3.0
            x[round(fs * 0.22)] += (1.5, -1.7)
            options = ["--preset", "water", "--color", "200", "--decay", "1500", "--morph", "40", "--gate", "-80", "--width", "160", "--output", "12"]
            y = renderer.render(x, fs, options)
            peak = float(np.max(np.abs(y)))
            case.measurements.update(peak=peak, rms=rms(y))
            case.expect(peak <= 1.000001, "Limiter bounds high-drive transient output", peak, "peak <= 1.000001, all samples finite")
            case.expect(rms(y) >= 1.0e-5, "Stability is not achieved by muting", rms(y), "output RMS >= 1e-5")
        cases.append((f"stress_{rate}", "Catch sample-rate errors and unstable loud impulse/noise processing.", rates))

    def tails(case: Case) -> None:
        fs = 48000
        stop = 0.35
        x = np.zeros((round(1.65 * fs), 2), dtype=np.float32)
        start, end = round(0.06 * fs), round(stop * fs)
        noise = np.random.default_rng(5571).standard_normal((end - start, 2)) * 0.14
        noise = signal.sosfilt(signal.butter(3, (150, 7000), btype="bandpass", fs=fs, output="sos"), noise, axis=0)
        noise[:96] *= np.linspace(0.0, 1.0, 96)[:, None]
        x[start:end] = noise.astype(np.float32)  # Deliberately abrupt stop.
        base_options = PURE + ["--color", "200", "--decay", "1200"]
        base = renderer.render(x, fs, base_options)
        morph = renderer.render(x, fs, base_options + ["--morph", "100"])
        gate = renderer.render(x, fs, base_options + ["--gate", "-60"])
        window = slice(round((stop + 0.25) * fs), round(1.35 * fs))
        active = slice(round(0.15 * fs), round(0.30 * fs))
        baseline = rms(base[window])
        case.measurements["tail_window_seconds"] = [stop + 0.25, 1.35]
        case.measurements["uncontrolled_tail_rms"] = baseline
        case.measurements["uncontrolled_tail_rms_dbfs"] = db(baseline)
        case.expect(baseline >= 1.0e-5, "Baseline has an actual resonant tail to suppress", baseline, "tail RMS >= 1e-5")
        for label, y in (("morph_100", morph), ("gate_minus_60", gate)):
            tail = rms(y[window])
            attenuation = db(tail / max(baseline, 1.0e-15))
            actual = {"tail_rms": tail, "tail_rms_dbfs": db(tail) if tail > 0.0 else None, "tail_change_db": attenuation if tail > 0.0 else None, "exact_digital_silence": tail == 0.0, "active_rms": rms(y[active])}
            case.measurements[label] = actual
            case.expect(attenuation <= -12.0, f"{label} shortens the tail", {"tail_rms": tail, "tail_change_db": actual["tail_change_db"]}, "tail RMS at least 12 dB below uncontrolled resonance; a zero RMS is digital silence, not a finite dB value")
            case.expect(rms(y[active]) >= 1.0e-4, f"{label} retains active audio", rms(y[active]), "active-region RMS >= 1e-4")
    cases.append(("spectral_tail_suppression", "Catch absent spectral sidechaining or a false pass caused by silent output.", tails))

    def gain(case: Case) -> None:
        x = fixture(48000, 0.45)
        a = renderer.render(x, 48000, PURE + ["--snap", "0"])
        b = renderer.render(x, 48000, PURE + ["--snap", "0", "--output", "6"])
        factor = 10.0 ** (6.0 / 20.0)
        maximum = float(np.max(np.abs(b - a * factor)))
        case.measurements["measured_rms_gain_db"] = db(rms(b) / max(rms(a), 1.0e-15))
        case.expect(maximum <= 1.0e-5, "Output gain follows decibel amplitude law", maximum, "+6 dB matches 10^(6/20) within 1e-5 with limiter off")
    cases.append(("output_gain_control", "Catch a disconnected output control or incorrect dB conversion.", gain))

    def width(case: Case) -> None:
        fs = 48000
        t = np.arange(round(0.8 * fs), dtype=np.float64) / fs
        # Width belongs to the affected frequency region. A broadband fixture
        # would retain untouched stereo above highHz and below lowHz. Use tones
        # well inside both crossover shoulders and measure their steady portion.
        x = np.column_stack((0.15 * np.sin(2 * np.pi * 1500 * t),
                             0.11 * np.sin(2 * np.pi * 3000 * t + 0.3))).astype(np.float32)
        steady = slice(round(0.2 * fs), round(0.6 * fs))
        mono = renderer.render(x, 48000, PURE + ["--snap", "0", "--width", "0"])
        difference = rms(mono[steady, 0] - mono[steady, 1])
        case.expect(difference <= 1.0e-7, "Width zero collapses processed-band stereo", difference, "steady in-band left-minus-right RMS <= 1e-7")
        original = renderer.render(x, 48000, PURE + ["--snap", "0", "--width", "100"])
        wide = renderer.render(x, 48000, PURE + ["--snap", "0", "--width", "160"])
        side_ratio = rms(wide[steady, 0] - wide[steady, 1]) / max(rms(original[steady, 0] - original[steady, 1]), 1.0e-15)
        case.expect(abs(side_ratio - 1.6) <= 0.01, "Width 160 scales the processed side channel", side_ratio, "steady in-band side RMS ratio within 0.01 of 1.6")
        outside = np.column_stack((0.15 * np.sin(2 * np.pi * 12000 * t),
                                   0.11 * np.sin(2 * np.pi * 15000 * t + 0.3))).astype(np.float32)
        untouched = renderer.render(outside, fs, PURE + ["--snap", "0", "--width", "0"])
        outside_error = float(np.max(np.abs(untouched[steady] - outside[steady])))
        case.expect(outside_error <= 1.0e-4, "Width preserves stereo outside the selected band", outside_error, "steady out-of-band maximum absolute error <= 1e-4")
        case.measurements["scope"] = "Width applies only to the selected processing band, including its transition shoulders. Unprocessed bass and air retain dry stereo."
    cases.append(("stereo_width_control", "Catch inactive in-band width processing or damaged out-of-band stereo.", width))

    def key_and_mask(case: Case) -> None:
        x = sine(48000)
        in_key = renderer.render(x, 48000, PURE + ["--root", "D"])
        peak = spectrum_measurement(in_key, 48000)["dominant_frequency_hz"]
        case.expect(abs(peak - F4) <= 0.8, "F4 remains in D minor pentatonic", peak, f"within 0.8 Hz of {F4:.9f} Hz")
        custom = renderer.render(x, 48000, PURE + ["--scale", "custom", "--mask", str(1 << 4)])
        peak_custom = spectrum_measurement(custom, 48000)["dominant_frequency_hz"]
        case.expect(abs(peak_custom - E4) <= 0.8, "Absolute custom E pitch-class mask", peak_custom, f"within 0.8 Hz of {E4:.9f} Hz")
    cases.append(("root_and_custom_mask", "Catch ignored scale controls or confusing absolute masks with relative intervals.", key_and_mask))

    def root_f_custom(case: Case) -> None:
        x = sine(48000)
        y = renderer.render(x, 48000, PURE + ["--root", "5", "--scale", "custom", "--mask", str(1 << 4)])
        measurement = spectrum_measurement(y, 48000)
        case.measurements.update(measurement)
        peak = measurement["dominant_frequency_hz"]
        case.expect(abs(peak - E4) <= 0.8, "Absolute E mask is independent of F root", peak, f"within 0.8 Hz of {E4:.9f} Hz; root=5 must not rotate absolute custom bits")
    cases.append(("root_f_custom_mask", "Regress the observed bug where a nonzero root rotated an absolute custom mask.", root_f_custom))

    def empty_custom(case: Case) -> None:
        x = fixture(48000)
        y = renderer.render(x, 48000, PURE + ["--root", "5", "--scale", "custom", "--mask", "0"])
        error = float(np.max(np.abs(y - x)))
        case.measurements["maximum_absolute_error"] = error
        case.expect(error <= 1.0e-4, "An empty pitch-class mask leaves neutral audio", error, "maximum absolute error <= 1e-4; no fallback note is inserted")
    cases.append(("empty_custom_mask_neutral", "Regress the observed bug where deselecting all notes forced tuning to one note.", empty_custom))

    def glide_settles(case: Case) -> None:
        fs = 48000
        t = np.arange(4 * fs, dtype=np.float64) / fs
        frequency = np.where(t < 1.0, F4,
                             np.where(t < 1.5, F4 + (E4 - F4) * (t - 1.0) / 0.5, E4))
        phase = 2.0 * np.pi * np.cumsum(frequency) / fs
        mono = (0.2 * np.sin(phase)).astype(np.float32)
        x = np.column_stack((mono, mono))
        y = renderer.render(x, fs, PURE + ["--root", "0", "--scale", "custom", "--mask", "16",
                                           "--low", "60", "--high", "18000"])
        envelope = np.abs(signal.hilbert(y[:, 0].astype(np.float64)))[2 * fs:round(3.8 * fs)]
        low = float(np.min(envelope))
        high = float(np.max(envelope))
        spread = float(np.std(envelope))
        case.measurements.update({"input_amplitude": 0.2, "settled_window_seconds": [2.0, 3.8],
                                  "envelope_minimum": low, "envelope_maximum": high,
                                  "envelope_standard_deviation": spread})
        case.expect(low > 0.19, "A settled glide retains its amplitude", low,
                    "Hilbert envelope minimum > 0.19 for an input amplitude of 0.2")
        case.expect(spread < 0.001, "A settled glide has continuous tracked phase", spread,
                    "Hilbert envelope standard deviation < 0.001 after the glide ends")
    cases.append(("glide_phase_continuity", "Regress phase switching and sustained level dips when a detuned glide settles exactly on a permitted pitch.", glide_settles))

    def mono_input(case: Case) -> None:
        x = fixture(48000, 0.3)[:, 0]
        y = renderer.render(x, 48000, PURE + ["--bypass"])
        error = float(np.max(np.abs(y - x[:, None])))
        case.expect(error <= 2.0e-7, "Mono input duplicates faithfully to stereo", error, "both output channels match mono input within 2e-7")
    cases.append(("mono_input", "Catch mono WAV routing mistakes independently of the plugin host.", mono_input))
    return cases


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--renderer", type=Path, default=PROJECT / "build" / "iris-render")
    parser.add_argument("--report", type=Path, default=PROJECT / "docs" / "audio-validation.json")
    parser.add_argument("--work-dir", type=Path, help="Keep diagnostic input/output WAVs in this directory.")
    parser.add_argument("--only", help="Comma-separated case names, for investigation after a full run.")
    parser.add_argument("--list", action="store_true", help="List tests without requiring a renderer.")
    args = parser.parse_args()
    temporary = None
    work = args.work_dir
    if work is None:
        temporary = tempfile.TemporaryDirectory(prefix="iris-audio-acceptance-")
        work = Path(temporary.name)
    work.mkdir(parents=True, exist_ok=True)
    executable = args.renderer.expanduser().resolve()
    renderer = Renderer(executable, work)
    cases = build_cases(renderer)
    if args.list:
        for name, purpose, _ in cases:
            print(f"{name}: {purpose}")
        return 0
    selected = set(args.only.split(",")) if args.only else None
    known = {item[0] for item in cases}
    if selected and selected - known:
        parser.error("Unknown cases: " + ", ".join(sorted(selected - known)))
    if not executable.is_file():
        print(f"Renderer not found: {executable}. Build it, then run this acceptance suite.", file=sys.stderr)
        return 2
    report = {
        "schema_version": 1,
        "suite": "Iris Color independent rendered-audio acceptance",
        "scope": "Tests this original DSP and WAV renderer; not sonic equivalence to Chroma or VST3 host compatibility.",
        "renderer": str(executable),
        "renderer_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "python": sys.version.split()[0],
        "numpy": np.__version__,
        "full_suite": selected is None,
        "tests": [],
    }
    begin = time.perf_counter()
    for name, purpose, function in cases:
        if selected is not None and name not in selected:
            continue
        case = Case(name, purpose)
        started = time.perf_counter()
        error = None
        try:
            function(case)
        except Exception as exception:
            error = f"{type(exception).__name__}: {exception}"
        passed = bool(case.assertions) and all(item["passed"] for item in case.assertions) and error is None
        result = {
            "name": name,
            "purpose": purpose,
            "status": "error" if error else ("passed" if passed else "failed"),
            "elapsed_seconds": time.perf_counter() - started,
            "measurements": case.measurements,
            "assertions": case.assertions,
        }
        if error:
            result["error"] = error
        report["tests"].append(result)
        print(f"{result['status'].upper():6} {name}", flush=True)
        if error:
            print(f"       {error}", flush=True)
        for assertion in case.assertions:
            if not assertion["passed"]:
                print(f"       {assertion['name']}: {assertion['actual']} (requires {assertion['requirement']})", flush=True)
    report["elapsed_seconds"] = time.perf_counter() - begin
    report["render_calls"] = renderer.calls
    report["passed"] = all(item["status"] == "passed" for item in report["tests"])
    report["summary"] = {state: sum(item["status"] == state for item in report["tests"]) for state in ("passed", "failed", "error")}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"ok": report["passed"], "summary": report["summary"], "report": str(args.report)}, ensure_ascii=False))
    if temporary is not None:
        temporary.cleanup()
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
