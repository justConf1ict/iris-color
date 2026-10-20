#!/usr/bin/env python3
"""Verify frozen Legacy DSP contracts and v0.4/v0.5 Legacy sample identity.

Example:
  python3 tools/verify_legacy05.py --input /path/to/original.wav \
      --legacy-root ../iris-color-04

Requires Python's standard library, g++, and an unchanged v0.4 source tree.
The existing v0.4 Linux renderer is used when available; otherwise its
unchanged source is compiled in a temporary directory. Current v0.5 tests
and renderer are always freshly compiled with -O2. Inputs are read only.
Temporary executables and rendered audio are deleted; only the JSON
receipt remains in build/reference/legacy05-validation.json.
"""

import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import time


ROOT = Path(__file__).resolve().parents[1]
TESTS = (
    "dsp_unit", "dsp_revision_visual", "dsp_color03_level",
    "dsp_color03_state", "dsp_color03_bands", "dsp_color04_features",
    "dsp_color04_contracts",
)
DSP_INPUTS = (
    "src/IrisDSP.cpp", "src/IrisDSP.h", "src/IrisPrism.h",
    "src/IrisCreative.h", "src/IrisColorBands.h", "src/IrisPresets.h",
    "tools/render.cpp",
)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def hashes(root, names):
    return {name: sha(root / name) for name in names if (root / name).is_file()}


def run(command):
    argv = [str(value) for value in command]
    started = time.monotonic()
    try:
        result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, timeout=300)
        return {
            "command": argv, "exit": result.returncode,
            "seconds": time.monotonic() - started,
            "stdout": result.stdout, "stderr": result.stderr,
        }
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
        return {
            "command": argv, "exit": None, "timed_out": True,
            "seconds": time.monotonic() - started,
            "stdout": decoded(error.stdout), "stderr": decoded(error.stderr),
        }


def float_wave(path):
    """Read the renderer's float32 RIFF data without a scientific dependency."""
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Renderer output is not RIFF WAVE")
    position, fmt, payload = 12, None, None
    while position + 8 <= len(data):
        kind = data[position:position + 4]
        size = struct.unpack_from("<I", data, position + 4)[0]
        first = position + 8
        if first + size > len(data):
            raise ValueError("Truncated renderer output")
        if kind == b"fmt ":
            if size < 16:
                raise ValueError("Short renderer fmt chunk")
            fmt = struct.unpack_from("<HHIIHH", data, first)
        elif kind == b"data":
            payload = data[first:first + size]
        position = first + size + (size & 1)
    if fmt is None or payload is None:
        raise ValueError("Renderer output lacks format or audio")
    encoding, channels, rate, byte_rate, align, bits = fmt
    if encoding != 3 or bits != 32 or channels != 2 or align != channels * 4:
        raise ValueError("Expected stereo float32 renderer output")
    if len(payload) % align or byte_rate != rate * align:
        raise ValueError("Invalid renderer audio alignment")
    values = array("f")
    values.frombytes(payload)
    if sys.byteorder != "little":
        values.byteswap()
    return {
        "rate": rate, "channels": channels, "frames": len(payload) // align,
        "payload": payload, "values": values, "file_sha256": sha(path),
        "data_sha256": hashlib.sha256(payload).hexdigest(),
    }


def compare_audio(old_path, new_path):
    old, new = float_wave(old_path), float_wave(new_path)
    metadata_equal = all(old[key] == new[key] for key in ("rate", "channels", "frames"))
    all_finite = all(math.isfinite(value) for wave in (old, new) for value in wave["values"])
    bit_exact = metadata_equal and old["payload"] == new["payload"]
    mismatches, max_error, square_error = 0, 0.0, 0.0
    if metadata_equal and not bit_exact:
        for i, (a, b) in enumerate(zip(old["values"], new["values"])):
            mismatches += old["payload"][i * 4:i * 4 + 4] != new["payload"][i * 4:i * 4 + 4]
            error = float(a) - float(b)
            if math.isfinite(error):
                max_error = max(max_error, abs(error))
                square_error += error * error
            else:
                max_error = square_error = None
                break
    count = len(old["values"])
    return {
        "pass": bit_exact and all_finite,
        "bit_exact_float32": bit_exact, "metadata_equal": metadata_equal,
        "all_finite": all_finite, "sample_rate": old["rate"],
        "channels": old["channels"], "frames": old["frames"],
        "compared_channel_samples": count if metadata_equal else 0,
        "different_channel_samples": mismatches if metadata_equal else None,
        "max_absolute_error": max_error if metadata_equal else None,
        "rms_error": math.sqrt(square_error / max(1, count)) if metadata_equal and square_error is not None else None,
        "legacy04_wav_sha256": old["file_sha256"],
        "legacy05_wav_sha256": new["file_sha256"],
        "legacy04_data_sha256": old["data_sha256"],
        "legacy05_data_sha256": new["data_sha256"],
    }


def configurations():
    common = [
        "--root", "D#", "--scale", "minor-pent", "--color", "150",
        "--snap", "100", "--low", "230", "--high", "4000",
        "--decay", "200", "--transient", "0", "--width", "100",
        "--output", "0", "--morph-off", "--gate-off", "--no-limit",
        "--tail", "0.6", "--fft", "2048", "--spectral", "1024", "--block", "0",
    ]
    all_effects = [
        *common, "--color", "185", "--decay", "340", "--transient", "17",
        "--width", "132", "--output", "-5", "--morph", "28", "--gate", "-66",
        "--color-low", "180", "--color-mid", "65", "--color-high", "140",
        "--color-split-low", "500", "--color-split-high", "2600",
        "--eq-low", "3", "--eq-mid", "-4", "--eq-high", "2",
        "--attack-tail-on", "--attack-color", "25", "--tail-color", "165", "--color-transition", "135",
        "--dive-on", "--dive-semitones", "9", "--dive-time", "180",
        "--motion-on", "--motion-depth", "87", "--motion-rate", "3.25",
        "--motion-sync", "--tempo", "137", "--motion-division", "3",
        "--formant-on", "--formant-mix", "68", "--vowel", "1.7",
        "--formant-motion", "45", "--formant-rate", "0.7",
        "--tamer-on", "--tamer-amount", "85", "--tamer-max", "11",
    ]
    for index, gain in enumerate((-5, -2, 1, 4, -6, 3, -8, 2, -3, 5, -7, 1, -4, 3, -2, -6)):
        all_effects.extend(("--eq-band", f"{index}:{gain}"))
    return {"reference_controls": common, "all_five_on_nonflat_eq": all_effects}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", type=Path, required=True, help="Original WAV; never modified or copied into the project.")
    parser.add_argument("--legacy-root", type=Path, default=ROOT.parent / "iris-color-04")
    parser.add_argument("--legacy-renderer", type=Path, help="Explicit unchanged v0.4 renderer, otherwise use its Linux build or compile its source.")
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel compilation count, clamped to 1..4; audio tests run sequentially.")
    parser.add_argument("--output", type=Path, default=ROOT / "build/reference/legacy05-validation.json")
    args = parser.parse_args()
    original, legacy_root, output = args.input.resolve(), args.legacy_root.resolve(), args.output.resolve()
    if not original.is_file():
        parser.error("Input WAV does not exist")
    if not (legacy_root / "src/IrisDSP.cpp").is_file():
        parser.error("The unchanged v0.4 source tree is required")
    source_names = (*DSP_INPUTS, *(f"tests/{name}.cpp" for name in TESTS), "tools/verify_legacy05.py")
    before = hashes(ROOT, source_names)
    old_before = hashes(legacy_root, DSP_INPUTS)
    report = {
        "scope": "Frozen pre-0.5 DSP contracts run through the actual 0.5 engine with engineMode=0; exact sample comparison against unchanged 0.4.",
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "input": str(original), "input_sha256": sha(original),
        "source_sha256": before, "legacy_source_sha256": old_before,
        "compiler": run([args.compiler, "--version"]),
        "tests": {}, "audio_comparisons": {},
        "temporary_audio_retained": False,
    }
    flags = ["-std=c++17", "-O2", "-DNDEBUG", "-DRELEASE=1", "-pthread", "-I" + str(ROOT / "src")]
    with TemporaryDirectory(prefix="iris-legacy05-") as directory:
        temporary = Path(directory)
        compile_jobs = {
            name: [args.compiler, *flags, ROOT / f"tests/{name}.cpp", ROOT / "src/IrisDSP.cpp", "-o", temporary / name]
            for name in TESTS
        }
        compile_jobs["renderer05"] = [
            args.compiler, *flags, ROOT / "tools/render.cpp", ROOT / "src/IrisDSP.cpp", "-o", temporary / "iris-render05",
        ]
        compiled = {}
        with ThreadPoolExecutor(max_workers=max(1, min(4, args.jobs))) as pool:
            futures = {pool.submit(run, command): name for name, command in compile_jobs.items()}
            for future in as_completed(futures):
                name = futures[future]
                compiled[name] = future.result()
                print(f"COMPILE {name}: {compiled[name]['exit']}", flush=True)
        for name in TESTS:
            result = {"compile": compiled[name]}
            if compiled[name]["exit"] == 0:
                binary = temporary / name
                result["binary_sha256"] = sha(binary)
                result["run"] = run([binary])
                lines = result["run"]["stdout"].splitlines()
                result["passing_checks"] = [line for line in lines if line.startswith("PASS ")]
                result["failing_checks"] = [line for line in lines if line.startswith("FAIL ")]
                result["pass"] = result["run"]["exit"] == 0 and not result["failing_checks"]
            else:
                result["pass"] = False
            report["tests"][name] = result
            print(f"{'PASS' if result['pass'] else 'FAIL'} {name}: {len(result.get('passing_checks', []))} checks", flush=True)
        report["renderer05_compile"] = compiled["renderer05"]
        old_renderer = args.legacy_renderer.resolve() if args.legacy_renderer else legacy_root / "build/linux/iris-render"
        report["legacy_renderer_origin"] = "existing unchanged v0.4 renderer"
        if not old_renderer.is_file() and args.legacy_renderer:
            report["legacy_renderer_error"] = "Explicit v0.4 renderer does not exist"
        elif not old_renderer.is_file():
            old_renderer = temporary / "iris-render04"
            old_flags = ["-std=c++17", "-O2", "-DNDEBUG", "-DRELEASE=1", "-pthread", "-I" + str(legacy_root / "src")]
            report["legacy_renderer_compile"] = run([
                args.compiler, *old_flags, legacy_root / "tools/render.cpp", legacy_root / "src/IrisDSP.cpp", "-o", old_renderer,
            ])
            report["legacy_renderer_origin"] = "fresh compile of unchanged v0.4 source"
        old_receipt = legacy_root / "build/linux/build-receipt.json"
        if old_receipt.is_file():
            frozen = json.loads(old_receipt.read_text())
            recorded_sources = frozen.get("source_sha256", {})
            report["legacy_build_receipt"] = {
                "path": str(old_receipt), "sha256": sha(old_receipt),
                "all_recorded_sources_match": all(
                    (legacy_root / path).is_file() and sha(legacy_root / path) == expected
                    for path, expected in recorded_sources.items()
                ),
                "renderer_matches_recorded_binary": old_renderer.is_file() and sha(old_renderer) == frozen.get("renderer_sha256"),
            }
        if old_renderer.is_file() and compiled["renderer05"]["exit"] == 0:
            new_renderer = temporary / "iris-render05"
            report["legacy_renderer_sha256"] = sha(old_renderer)
            report["renderer05_sha256"] = sha(new_renderer)
            for name, options in configurations().items():
                old_path, new_path = temporary / f"{name}.04.wav", temporary / f"{name}.05.wav"
                old_run = run([old_renderer, original, old_path, *options])
                new_run = run([new_renderer, original, new_path, "--engine", "legacy", *options])
                result = {"options": options, "legacy04_render": old_run, "legacy05_render": new_run, "pass": False}
                if old_run["exit"] == 0 and new_run["exit"] == 0:
                    result.update(compare_audio(old_path, new_path))
                report["audio_comparisons"][name] = result
                print(f"{'PASS' if result['pass'] else 'FAIL'} {name}: bit exact={result.get('bit_exact_float32')}, max error={result.get('max_absolute_error')}", flush=True)
        report["source_unchanged_during_validation"] = before == hashes(ROOT, source_names)
        report["legacy_source_unchanged_during_validation"] = old_before == hashes(legacy_root, DSP_INPUTS)
        report["input_unchanged"] = report["input_sha256"] == sha(original)
        old_receipt_ok = report.get("legacy_build_receipt", {}).get("all_recorded_sources_match", True)
        report["pass"] = (
            all(result["pass"] for result in report["tests"].values())
            and len(report["audio_comparisons"]) == 2
            and all(result["pass"] for result in report["audio_comparisons"].values())
            and report["source_unchanged_during_validation"]
            and report["legacy_source_unchanged_during_validation"]
            and report["input_unchanged"] and old_receipt_ok
        )
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"{'PASS' if report['pass'] else 'FAIL'} receipt: {output}", flush=True)
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
