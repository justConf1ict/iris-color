#!/usr/bin/env python3
"""Compare final Linux and Windows renderers with Clarity and creative effects.

Requires a separately provisioned Python Wine wrapper, NumPy and SciPy. No
level matching or extra time alignment is applied to the comparison.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path, required=True)
    ap.add_argument('--runner', type=Path, required=True)
    ap.add_argument('--out', type=Path, default=ROOT / 'build/validation06/parity')
    args = ap.parse_args()
    import numpy as np
    from scipy.io import wavfile
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    base = ['--engine','prism','--root','D#','--scale','minor-pent','--color','150',
            '--snap','100','--low','230','--high','4000','--decay','200',
            '--transient','0','--width','100','--output','0','--morph-off',
            '--gate-off','--no-limit','--tail','0','--block','0','--clarity','35']
    profiles = {'clarity35':base, 'creative70':base+[
        '--clarity','70','--color','185','--output','-6','--morph','28','--gate','-66',
        '--attack-tail-on','--attack-color','15','--tail-color','170','--color-transition','100',
        '--dive-on','--dive-semitones','12','--dive-time','160',
        '--motion-on','--motion-depth','85','--motion-sync','--tempo','150',
        '--formant-on','--formant-mix','80','--vowel','2.6','--formant-motion','45',
        '--tamer-on','--tamer-amount','75','--tamer-max','8','--width','130',
        '--color-low','80','--color-mid','145','--color-high','170',
        '--color-split-low','550','--color-split-high','2300',
        '--eq-band','5:3','--eq-band','8:-4','--eq-band','11:2']}
    executables = {'linux':ROOT/'build/linux/iris-render', 'windows':ROOT/'build/windows/iris-render.exe'}
    hashes = {k:sha(v) for k,v in executables.items()}
    report = {'input_sha256':sha(args.input),'executable_sha256':hashes,'profiles':{},
              'scope':'Final cross-compiler audio comparison under Wine, without gain fitting or extra alignment.', 'passed':False}
    win = lambda p:'Z:'+str(p.resolve()).replace('/','\\')
    for profile, settings in profiles.items():
        waves = {}
        for platform, exe in executables.items():
            path = out/(profile+'-'+platform+'.wav')
            command = [str(exe),str(args.input.resolve()),str(path),*settings]
            if platform == 'windows': command = [sys.executable,str(args.runner.resolve()),str(exe),win(args.input),win(path),*settings]
            result = subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=180)
            (out/(profile+'-'+platform+'.log')).write_text(result.stdout+result.stderr)
            result.check_returncode()
            rate, samples = wavfile.read(path); waves[platform] = (rate,samples.astype(np.float64),sha(path))
        rate,a,_ = waves['linux']; rate2,b,_ = waves['windows']
        if rate != rate2 or a.shape != b.shape: raise RuntimeError('Renderer formats or sample counts differ')
        finite = bool(np.isfinite(a).all() and np.isfinite(b).all())
        difference = a-b; maximum = float(np.max(np.abs(difference)))
        rms = float(np.sqrt(np.mean(difference*difference)))
        relative = rms/max(1e-20,float(np.sqrt(np.mean(a*a))))
        report['profiles'][profile] = {'sample_rate':rate,'frames':len(a),'all_finite':finite,
            'max_absolute_sample_error':maximum,'relative_rms_error':relative,
            'wav_sha256':{k:v[2] for k,v in waves.items()},'settings':settings,
            'passed':finite and maximum<5e-5 and relative<1e-4}
        print(profile,json.dumps(report['profiles'][profile]),flush=True)
    report['executables_unchanged'] = hashes == {k:sha(v) for k,v in executables.items()}
    report['passed'] = report['executables_unchanged'] and all(v['passed'] for v in report['profiles'].values())
    (out/'renderer-parity06.json').write_text(json.dumps(report,indent=2)+'\n')
    if not report['passed']: raise RuntimeError('Renderer parity failed')

if __name__ == '__main__': main()
