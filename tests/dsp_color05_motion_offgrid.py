#!/usr/bin/env python3
"""Small independent regression for per-note Scale Motion off FFT-bin centres.

Three synthetic E4+G4 pairs at 44.1 kHz; G is detuned by 0/-40/+40 cents.
Each is rendered once with Motion off and once with Motion on. The checks
compare selected-note energy to its own Motion-off baseline, not a hard-coded
FFT bin. No user audio or production helpers are imported.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,warnings
import numpy as np
from scipy import signal
from scipy.io import wavfile

FS=44100
E4=329.6275569128699
G4=391.99543598174927
BASE=['--preset','init','--engine','prism','--root','E','--scale','custom','--mask','144',
      '--color','100','--snap','100','--morph-off','--gate-off','--low','60','--high','18000',
      '--transient','0','--width','100','--output','0','--no-limit','--tail','0','--block','0',
      '--fft','2048','--spectral','1024','--motion-depth','100','--motion-free','--motion-rate','2']

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def db(a):return float(20*np.log10(max(float(a),1e-30)))
def band_energy(z,a,b,low,high):
    z=np.asarray(z[round(a*FS):round(b*FS)],float).mean(axis=1)
    f,p=signal.periodogram(z,FS,window='blackmanharris',nfft=262144,detrend=False)
    mask=(f>=low)&(f<=high);ids=np.flatnonzero(mask);k=ids[np.argmax(p[ids])]
    logp=np.log(np.maximum(p[k-1:k+2],1e-30));off=.5*(logp[0]-logp[2])/(logp[0]-2*logp[1]+logp[2]+1e-30)
    peak_hz=(k+np.clip(off,-.5,.5))*FS/262144
    return float(np.sqrt(np.sum(p[mask])*(f[1]-f[0]))),float(peak_hz)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--renderer',type=Path,required=True)
    ap.add_argument('--output',type=Path,default=Path('build/reference/05-motion-offgrid.json'))
    args=ap.parse_args();exe=args.renderer.resolve();before=sha(exe)
    work=args.output.parent/(args.output.stem+'-fixtures');work.mkdir(parents=True,exist_ok=True)
    cases=[];runs=[];t=np.arange(round(2.05*FS))/FS
    windows=[(.2,.4,'E'),(.7,.9,'G'),(1.2,1.4,'E'),(1.7,1.9,'G')]
    regions={'E':(E4*2**(-80/1200),E4*2**(80/1200)),
             'G':(G4*2**(-80/1200),G4*2**(80/1200))}
    for cents in [0,-40,40]:
        g=G4*2**(cents/1200);x=.055*(np.sin(2*np.pi*E4*t+.17)+np.sin(2*np.pi*g*t+.93))
        src=work/f'pair_G{cents:+d}.input.wav';wavfile.write(src,FS,np.column_stack([x,x]).astype(np.float32))
        rendered={}
        for mode in ['off','on']:
            dest=work/f'pair_G{cents:+d}_motion_{mode}.output.wav'
            cmd=[str(exe),str(src),str(dest),*BASE,'--motion-'+mode]
            p=subprocess.run(cmd,text=True,capture_output=True,timeout=120)
            rec={'command':cmd,'exit_code':p.returncode,'stdout':p.stdout.strip(),'stderr':p.stderr.strip()};runs.append(rec)
            if p.returncode:raise RuntimeError(p.stderr or p.stdout)
            with warnings.catch_warnings():
                warnings.simplefilter('ignore',wavfile.WavFileWarning);fs,y=wavfile.read(dest)
            if fs!=FS or y.shape!=(len(t),2) or not np.isfinite(y).all():raise RuntimeError('Unexpected audio format or nonfinite data')
            rendered[mode]=y;rec['sha256']=sha(dest)
        observations=[];ratios=[];retention=[]
        for a,b,selected in windows:
            row={'interval_s':[a,b],'selected':selected,'notes':{}}
            for name,(lo,hi) in regions.items():
                base,basef=band_energy(rendered['off'],a,b,lo,hi)
                moving,movingf=band_energy(rendered['on'],a,b,lo,hi)
                row['notes'][name]={'baseline_band_rms':base,'motion_band_rms':moving,
                    'retained_db':db(moving/max(base,1e-30)),
                    'baseline_peak_hz':basef,'motion_peak_hz':movingf}
                if name==selected:retention.append(row['notes'][name]['retained_db'])
            ratio=db(row['notes']['E']['motion_band_rms']/max(row['notes']['G']['motion_band_rms'],1e-30))
            ratios.append(ratio);row['E_to_G_db']=ratio;observations.append(row)
        swing=float(np.ptp(ratios));minimum=float(min(retention))
        checks={'selected_note_loses_no_more_than3_db_vs_own_baseline':minimum>=-3,
                'E_G_balance_swings_by_at_least10_db':swing>=10}
        case={'G_input_detuning_cents':cents,'E_input_hz':E4,'G_input_hz':g,
              'input_sha256':sha(src),'minimum_selected_note_retention_db':minimum,
              'E_G_balance_swing_db':swing,'intervals':observations,'checks':checks,
              'passed':all(checks.values())}
        cases.append(case)
        print(('PASS'if case['passed']else'FAIL'),f'G{cents:+d}c',json.dumps({'retention_db':minimum,'swing_db':swing}),flush=True)
    unchanged=sha(exe)==before
    result={'description':__doc__,'sample_rate':FS,'renderer':str(exe),'renderer_sha256':before,
            'renderer_unchanged':unchanged,'script_sha256':sha(Path(__file__)),'cases':cases,
            'render_calls':runs,'passed':unchanged and all(c['passed']for c in cases),
            'scope':'Tests Motion retention away from FFT-bin centres. Local peak frequencies are reported, not certified as exact polyphonic pitch quantization.'}
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['passed']else 1

if __name__=='__main__':raise SystemExit(main())
