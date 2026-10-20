#!/usr/bin/env python3
"""Verify the built Windows plugin, including its actual HWND editor.

Run on Windows directly, or supply a Python Wine wrapper with --runner.
The wrapper is a separately provisioned developer dependency. It is never
packaged with Iris Color and a Wine run is not a real Windows/DAW acceptance.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--runner', type=Path, help='Python wrapper accepting a Windows executable and its arguments')
    ap.add_argument('--input', type=Path, help='Optional WAV for the actual spectrum screenshots; synthetic audio otherwise')
    ap.add_argument('--out', type=Path, default=ROOT / 'build/validation07/windows')
    args = ap.parse_args()
    import numpy as np
    from scipy.io import wavfile
    from scipy.signal import resample_poly
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    build = ROOT / 'build/windows'
    record = json.loads((build / 'build-receipt.json').read_text())
    sources = {p: sha(ROOT / p) for p in record['source_sha256']}
    if sources != record['source_sha256']:
        raise RuntimeError('Production source differs from the Windows build receipt')
    module = ROOT / record['binary']
    if sha(module) != record['sha256']:
        raise RuntimeError('Windows module differs from the build receipt')
    if args.input:
        fs, raw = wavfile.read(args.input)
        audio = raw.astype(np.float64)
        if raw.dtype.kind == 'i': audio /= 2 ** (np.iinfo(raw.dtype).bits - 1)
        if audio.ndim == 1: audio = np.column_stack((audio, audio))
        if audio.shape[1] != 2: raise ValueError('The UI input must be mono or stereo')
        divisor = math.gcd(int(fs), 48000)
        if fs != 48000: audio = resample_poly(audio, 48000 // divisor, int(fs) // divisor, axis=0)
    else:
        t = np.arange(96000) / 48000
        rng = np.random.default_rng(601035)
        mono = .08 * np.sin(2*np.pi*82.4069*t) + .035 * np.sin(2*np.pi*659.255*t) + .01*rng.normal(size=len(t))
        audio = np.column_stack((mono, mono))
    if len(audio) < 32*512: raise ValueError('The UI input must contain at least 16384 frames at 48 kHz')
    ui = out / 'ui-source48k.wav'; wavfile.write(ui, 48000, audio.astype(np.float32))
    capture = out / 'Iris07-native.bmp'
    # Wine maps the cloud filesystem through Z:. Native Windows uses its own
    # resolved paths. The EXE itself is passed unchanged to the wrapper.
    def argument(path):
        return 'Z:' + str(path).replace('/', '\\') if args.runner and sys.platform != 'win32' else str(path)
    cases = [
        ('host', 'iris-vst3-host-test.exe', [module, capture, ui]),
        ('transport', 'transport04_test.exe', [module]),
        ('state02', 'state_compat_test.exe', []),
        ('state04', 'state_compat04_test.exe', []),
        ('state05', 'state_compat05_test.exe', []),
        ('state06', 'state_compat06_test.exe', []),
        ('state07', 'state_compat07_test.exe', []),
        ('presets', 'preset_test.exe', []),
    ]
    report = {'scope': 'Windows x64 binary and native HWND under Wine; not a Windows hardware or FL Studio session.'
              if args.runner else 'Windows x64 binary and native HWND in the test host; not an FL Studio session.',
              'source_sha256': sources, 'module_sha256': sha(module),
              'host_source_sha256': sha(ROOT / 'plugin/tests/host_test.cpp'),
              'ui_fixture_sha256': sha(ui), 'commands': [], 'passed': False}
    receipt = out / 'windows07-final-receipt.json'
    for name, filename, extra in cases:
        exe = build / filename
        command = [str(exe), *map(argument, extra)]
        if args.runner: command = [sys.executable, str(args.runner.resolve()), *command]
        before = sha(exe); start = time.monotonic()
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
        log = out / (name + '.log'); log.write_text(result.stdout + result.stderr)
        item = {'name': name, 'command': command, 'exit_code': result.returncode,
                'seconds': time.monotonic()-start, 'binary_sha256': before,
                'binary_unchanged': before == sha(exe), 'log': log.name, 'log_sha256': sha(log)}
        report['commands'].append(item); receipt.write_text(json.dumps(report, indent=2)+'\n')
        print(name, 'exit', result.returncode, 'seconds', round(item['seconds'], 2), flush=True)
        if result.returncode or not item['binary_unchanged']:
            print(log.read_text(), flush=True); raise RuntimeError('Windows verification failed: ' + name)
    report['source_unchanged_through_run'] = sources == {p: sha(ROOT / p) for p in sources}
    report['module_unchanged_through_run'] = report['module_sha256'] == sha(module)
    report['captures'] = {p.name: sha(p) for p in sorted(out.glob('Iris07-native.bmp*'))}
    report['passed'] = report['source_unchanged_through_run'] and report['module_unchanged_through_run']
    receipt.write_text(json.dumps(report, indent=2)+'\n')
    if not report['passed']: raise RuntimeError('Source or module changed during verification')
    print('All eight Windows verification executables passed on the unchanged module.', flush=True)

if __name__ == '__main__': main()
