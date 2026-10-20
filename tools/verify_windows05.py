#!/usr/bin/env python3
"""Run the actual final Windows binaries in the private Wine test runtime.

This is a development verification helper, not an installer. It requires the
separately provisioned private Wine/Xvfb runtime and scipy for the UI fixture.
It never starts FL Studio and does not imply Windows hardware verification.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    args = parser.parse_args()
    from scipy.io import wavfile
    from scipy.signal import resample_poly
    import numpy as np
    out = ROOT / 'build/reference'
    out.mkdir(parents=True, exist_ok=True)
    record = json.loads((ROOT / 'build/windows/build-receipt.json').read_text())
    sources = {p: sha(ROOT / p) for p in record['source_sha256']}
    if sources != record['source_sha256']:
        raise RuntimeError('Production source differs from final Windows build')
    module = ROOT / record['binary']
    if sha(module) != record['sha256']:
        raise RuntimeError('Windows module differs from build receipt')
    fs, original = wavfile.read(args.input)
    audio = original.astype(np.float64)
    if original.dtype.kind == 'i':
        audio /= 2 ** (np.iinfo(original.dtype).bits - 1)
    if fs != 48000:
        import math
        divisor = math.gcd(int(fs), 48000)
        audio = resample_poly(audio, 48000 // divisor, int(fs) // divisor, axis=0)
    ui_source = out / 'ui-source48k.wav'
    wavfile.write(ui_source, 48000, audio.astype(np.float32))
    capture = out / 'Iris05-native.bmp'
    runner = ROOT / 'tools/.runtime/run-wine.py'
    win = ROOT / 'build/windows'
    cases = [
        ('host', win / 'iris-vst3-host-test.exe', [module, capture, ui_source]),
        ('transport', win / 'transport04_test.exe', [module]),
        ('state02', win / 'state_compat_test.exe', []),
        ('state04', win / 'state_compat04_test.exe', []),
        ('state05', win / 'state_compat05_test.exe', []),
        ('presets', win / 'preset_test.exe', []),
    ]
    result = {
        'scope': 'Actual Windows x64 VST3 and native HWND under Wine 9.0; not an FL Studio session or Windows hardware test.',
        'source_sha256': sources, 'module_sha256': sha(module),
        'host_source_sha256': sha(ROOT / 'plugin/tests/host_test.cpp'),
        'transport_source_sha256': sha(ROOT / 'plugin/tests/transport04_test.cpp'),
        'input_sha256': sha(args.input), 'ui_fixture_sha256': sha(ui_source),
        'commands': [], 'passed': False,
    }
    receipt = out / 'windows05-final-receipt.json'
    for name, exe, arguments in cases:
        command = [sys.executable, str(runner), str(exe), *map(str, arguments)]
        before = sha(exe)
        start = time.monotonic()
        ran = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
        log = out / ('windows-' + name + '-final.log')
        log.write_text(ran.stdout + ran.stderr)
        item = {'name': name, 'command': command, 'exit_code': ran.returncode,
                'seconds': time.monotonic() - start, 'binary_sha256': before,
                'binary_unchanged': sha(exe) == before, 'log': log.name,
                'log_sha256': sha(log)}
        result['commands'].append(item)
        receipt.write_text(json.dumps(result, indent=2) + '\n')
        print(name, 'exit', ran.returncode, 'seconds', round(item['seconds'], 2), flush=True)
        if ran.returncode or not item['binary_unchanged']:
            print(ran.stdout + ran.stderr, flush=True)
            raise RuntimeError('Windows verification failed: ' + name)
    result['source_unchanged_through_run'] = sources == {p: sha(ROOT / p) for p in sources}
    result['module_unchanged_through_run'] = sha(module) == result['module_sha256']
    result['captures'] = {p.name: sha(p) for p in sorted(out.glob('Iris05-native.bmp*'))}
    result['passed'] = result['source_unchanged_through_run'] and result['module_unchanged_through_run']
    receipt.write_text(json.dumps(result, indent=2) + '\n')
    if not result['passed']:
        raise RuntimeError('Source or module changed during verification')
    print('All 6 Windows verification executables passed on the unchanged final module.', flush=True)

if __name__ == '__main__':
    main()
