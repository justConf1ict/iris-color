#!/usr/bin/env python3
"""Compare final Linux and Windows renderer audio on a supplied WAV.

The Windows executable runs in the separately provisioned private Wine runtime.
Both receive identical input, controls and block schedule. No level fitting or
time alignment is applied to the result; each renderer removes its own PDC.
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
    from scipy.io import wavfile
    import numpy as np
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    args = parser.parse_args()
    out = ROOT / 'build/reference/parity05'
    out.mkdir(parents=True, exist_ok=True)
    base = ['--engine','prism','--root','D#','--scale','minor-pent','--color','150',
            '--snap','100','--low','230','--high','4000','--decay','200',
            '--transient','0','--width','100','--output','0','--morph-off',
            '--gate-off','--no-limit','--tail','0.404172335600907','--block','0']
    profiles = {'reference': base, 'creative': base + [
        '--color','185','--output','-6','--morph','28','--gate','-66',
        '--attack-tail-on','--attack-color','15','--tail-color','170','--color-transition','100',
        '--dive-on','--dive-semitones','12','--dive-time','160',
        '--motion-on','--motion-depth','85','--motion-sync','--tempo','150',
        '--formant-on','--formant-mix','80','--vowel','2.6','--formant-motion','45',
        '--tamer-on','--tamer-amount','75','--tamer-max','8',
        '--color-low','80','--color-mid','145','--color-high','170',
        '--color-split-low','550','--color-split-high','2300',
        '--eq-band','5:3','--eq-band','8:-4','--eq-band','11:2']}
    executables = {'linux': ROOT / 'build/linux/iris-render',
                   'windows': ROOT / 'build/windows/iris-render.exe'}
    executable_hashes = {k: sha(p) for k,p in executables.items()}
    commands, checks = [], {}
    for profile, parameters in profiles.items():
        waves = {}
        for platform, exe in executables.items():
            path = out / (profile + '-' + platform + '.wav')
            command = [str(exe), str(args.input), str(path), *parameters]
            if platform == 'windows':
                command = [sys.executable, str(ROOT / 'tools/.runtime/run-wine.py'), *command]
            start = time.monotonic()
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=180)
            log = out / (profile + '-' + platform + '.log')
            log.write_text(result.stdout + result.stderr)
            commands.append({'profile':profile,'platform':platform,'command':command,
                             'exit_code':result.returncode,'seconds':time.monotonic()-start,
                             'log':str(log.relative_to(ROOT)),'log_sha256':sha(log)})
            result.check_returncode()
            fs, samples = wavfile.read(path)
            waves[platform] = (fs, samples.astype(np.float64), sha(path))
        fs,a,_ = waves['linux']; rate,b,_ = waves['windows']
        shape = fs == rate and a.shape == b.shape
        if not shape:
            raise RuntimeError('Windows and Linux sample shapes differ')
        finite = bool(np.isfinite(a).all() and np.isfinite(b).all())
        difference = a-b
        maximum = float(np.max(np.abs(difference)))
        rms = float(np.sqrt(np.mean(difference*difference)))
        relative = rms / max(1e-20,float(np.sqrt(np.mean(a*a))))
        checks[profile] = {'sample_rate':fs,'frames':len(a),'same_format_shape':shape,
                           'all_finite':finite,'max_absolute_sample_error':maximum,
                           'rms_sample_error':rms,'relative_rms_error':relative,
                           'limits':{'max_absolute_sample_error':5e-5,'relative_rms_error':1e-4},
                           'wav_sha256':{k:v[2] for k,v in waves.items()},
                           'passed':finite and maximum < 5e-5 and relative < 1e-4}
        print(profile, json.dumps(checks[profile]), flush=True)
    unchanged = executable_hashes == {k:sha(p) for k,p in executables.items()}
    report = {'passed':unchanged and all(c['passed'] for c in checks.values()),
              'input_sha256':sha(args.input),'executable_sha256':executable_hashes,
              'executables_unchanged':unchanged,'profiles':checks,'commands':commands,
              'scope':'Final cross-compiler sample comparison under Wine; no gain fitting, no extra time alignment.'}
    (ROOT / 'build/reference/renderer-parity05.json').write_text(json.dumps(report,indent=2)+'\n')
    if not report['passed']:
        raise RuntimeError('Final renderer parity failed')

if __name__ == '__main__':
    main()
